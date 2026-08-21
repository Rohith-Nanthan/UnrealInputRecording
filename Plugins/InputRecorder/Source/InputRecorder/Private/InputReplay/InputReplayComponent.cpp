// Copyright Epic Games, Inc. All Rights Reserved.

#include "InputReplay/InputReplayComponent.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputRecordingLog.h"
#include "InputReplay/InputRecordingSerializer.h"
#include "MatchInput/MatchInputCueBuilder.h"
#include "Misc/EngineVersion.h"
#include "Subsystem/InputRecordingSubsystem.h"

namespace InputReplayComponentPrivate
{
	/** A logical-step catch-up longer than this is a hitch, not real input; clamp rather than spiral. */
	constexpr int32 MaxLogicalStepsPerFrame = 8;

	FString ToShortName(const FString& PathOrName)
	{
		int32 DotIndex = INDEX_NONE;
		if (PathOrName.FindLastChar(TEXT('.'), DotIndex))
		{
			return PathOrName.Mid(DotIndex + 1);
		}
		return PathOrName;
	}

	FVector ToVector(const FInputActionValue& Value)
	{
		// operator[] is type-agnostic and yields 0 for components the action does not use, which
		// is exactly the "always a full vector" contract FRecordedInputSample promises.
		return FVector(Value[0], Value[1], Value[2]);
	}
}

UInputReplayComponent::UInputReplayComponent()
{
	// Sampling normally runs off the PlayerController's input hooks. The tick exists only as the
	// fallback for projects that do not subclass their controller, and it is off until a take
	// or a review actually starts - see SetTickFallbackEnabled.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	bWantsInitializeComponent = false;
}

void UInputReplayComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Last chance to rescue a take: this is the only notification guaranteed to arrive while the
	// samples are still in memory. World teardown fires first in the normal case and this becomes
	// a no-op; when it does not fire, this is what stops the recording being silently lost.
	if (IsRecording())
	{
		if (const UWorld* World = GetWorld())
		{
			if (UGameInstance* GameInstance = World->GetGameInstance())
			{
				if (UInputRecordingSubsystem* Subsystem = GameInstance->GetSubsystem<UInputRecordingSubsystem>())
				{
					Subsystem->SaveInProgressTake(TEXT("replay component EndPlay"));
				}
			}
		}
	}

	// The lambdas bound into the PlayerController's input component capture this. They must not
	// outlive it.
	ReleaseTrackedActionDelegates();
	SetTickFallbackEnabled(false);
	Super::EndPlay(EndPlayReason);
}

void UInputReplayComponent::SetMode(EInputReplayMode NewMode)
{
	if (Mode == NewMode)
	{
		return;
	}

	Mode = NewMode;
	OnModeChanged.Broadcast(Mode);
}

// -------------------------------------------------------------------------------------------
// Resolution
// -------------------------------------------------------------------------------------------

APlayerController* UInputReplayComponent::ResolveOwningPlayerController() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	if (APlayerController* AsController = Cast<APlayerController>(Owner))
	{
		return AsController;
	}

	if (const APawn* AsPawn = Cast<APawn>(Owner))
	{
		return Cast<APlayerController>(AsPawn->GetController());
	}

	return Owner->GetInstigatorController<APlayerController>();
}

UEnhancedInputLocalPlayerSubsystem* UInputReplayComponent::ResolveEnhancedInputSubsystem() const
{
	const APlayerController* Controller = ResolveOwningPlayerController();
	const ULocalPlayer* LocalPlayer = Controller ? Controller->GetLocalPlayer() : nullptr;
	return LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
}

UEnhancedPlayerInput* UInputReplayComponent::ResolveEnhancedPlayerInput() const
{
	if (const UEnhancedInputLocalPlayerSubsystem* Subsystem = ResolveEnhancedInputSubsystem())
	{
		if (UEnhancedPlayerInput* PlayerInput = Subsystem->GetPlayerInput())
		{
			return PlayerInput;
		}
	}

	// Falls back to the controller's own PlayerInput, which is the same object in practice but
	// still resolves during the window where the subsystem has not finished initialising.
	const APlayerController* Controller = ResolveOwningPlayerController();
	return Controller ? Cast<UEnhancedPlayerInput>(Controller->PlayerInput) : nullptr;
}

UEnhancedInputComponent* UInputReplayComponent::ResolveEnhancedInputComponent() const
{
	const APlayerController* Controller = ResolveOwningPlayerController();
	return Controller ? Cast<UEnhancedInputComponent>(Controller->InputComponent) : nullptr;
}

// -------------------------------------------------------------------------------------------
// Tracked action list
// -------------------------------------------------------------------------------------------

bool UInputReplayComponent::ShouldRecordAction_Implementation(const UInputAction* Action) const
{
	return Action != nullptr;
}

bool UInputReplayComponent::BuildTrackedActionList()
{
	TrackedActions.Reset();
	FrameDeltaTrackedIndices.Reset();
	IgnoredTrackedIndices.Reset();

	TSet<const UInputAction*> Candidates;

	if (RecordedMappingContexts.Num() > 0)
	{
		for (const TSoftObjectPtr<UInputMappingContext>& SoftContext : RecordedMappingContexts)
		{
			const UInputMappingContext* Context = SoftContext.LoadSynchronous();
			if (!Context)
			{
				UE_LOG(LogInputRecording, Warning, TEXT("Recorded mapping context %s could not be loaded."),
					*SoftContext.ToString());
				continue;
			}

			for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
			{
				if (Mapping.Action)
				{
					Candidates.Add(Mapping.Action);
				}
			}
		}
	}
	else if (const UEnhancedPlayerInput* PlayerInput = ResolveEnhancedPlayerInput())
	{
		// No explicit context list configured, so record whatever is actually applied to this
		// player right now. This is what makes the component drop into an unknown project.
		for (const FEnhancedActionKeyMapping& Mapping : PlayerInput->GetEnhancedActionMappingsView())
		{
			if (Mapping.Action)
			{
				Candidates.Add(Mapping.Action);
			}
		}
	}

	for (const TSoftObjectPtr<UInputAction>& SoftAction : AdditionalActions)
	{
		if (const UInputAction* Action = SoftAction.LoadSynchronous())
		{
			Candidates.Add(Action);
		}
	}

	// A TSet iterates in insertion-hash order, which is stable within a run but not across
	// them. Sorting by path makes ActionIndex reproducible, which matters because it is the
	// key the whole file format is built on.
	TArray<const UInputAction*> Ordered = Candidates.Array();
	Ordered.Sort([](const UInputAction& A, const UInputAction& B)
	{
		return A.GetPathName() < B.GetPathName();
	});

	const bool bWhitelisting = FilterMode == EInputRecordingFilterMode::WhitelistOnly;
	int32 ExcludedByWhitelist = 0;

	TSet<const UInputAction*> FrameDeltaSet;
	for (const TSoftObjectPtr<UInputAction>& SoftAction : FrameDeltaActions)
	{
		if (const UInputAction* Action = SoftAction.LoadSynchronous())
		{
			FrameDeltaSet.Add(Action);
		}
	}

	for (const UInputAction* Action : Ordered)
	{
		if (bWhitelisting && !UMatchInputCueBuilder::IsActionIgnored(Action->GetPathName(), RecordedActionWhitelist))
		{
			// IsActionIgnored is a name-or-path membership test; here it is being asked the
			// inverse question - "is this in the list" - which is the same test.
			++ExcludedByWhitelist;
			continue;
		}

		if (!ShouldRecordAction(Action))
		{
			continue;
		}

		const int32 TrackedIndex = TrackedActions.Add(Action);

		if (FrameDeltaSet.Contains(Action))
		{
			FrameDeltaTrackedIndices.Add(TrackedIndex);
		}

		if (UMatchInputCueBuilder::IsActionIgnored(Action->GetPathName(), CueBuildOptions.IgnoredActions))
		{
			IgnoredTrackedIndices.Add(TrackedIndex);
		}
	}

	if (bWhitelisting && TrackedActions.Num() == 0 && ExcludedByWhitelist > 0)
	{
		// Worth shouting about: an empty recording looks exactly like "nobody pressed anything",
		// so without this the misconfiguration is invisible until somebody reviews the take.
		UE_LOG(LogInputRecording, Warning,
			TEXT("The recorded action whitelist excluded all %d reachable action(s). This take will be empty."),
			ExcludedByWhitelist);
	}

	ResetTrackingState();

	UE_LOG(LogInputRecording, Verbose, TEXT("Tracking %d action(s), %d of them frame-delta, %d ignored for matching."),
		TrackedActions.Num(), FrameDeltaTrackedIndices.Num(), IgnoredTrackedIndices.Num());

	return TrackedActions.Num() > 0;
}

void UInputReplayComponent::WriteHeaderActionPaths()
{
	CurrentRecording.Header.ActionPaths.Reset();
	CurrentRecording.Header.FrameDeltaActionIndices.Reset();

	for (int32 Index = 0; Index < TrackedActions.Num(); ++Index)
	{
		CurrentRecording.Header.ActionPaths.Add(TrackedActions[Index]->GetPathName());

		if (FrameDeltaTrackedIndices.Contains(Index))
		{
			CurrentRecording.Header.FrameDeltaActionIndices.Add(Index);
		}
	}
}

void UInputReplayComponent::ResetTrackingState()
{
	const int32 Count = TrackedActions.Num();

	LastRecordedValues.Reset();
	LastRecordedValues.Init(FVector::ZeroVector, Count);

	LastRecordedTriggerEvents.Reset();
	LastRecordedTriggerEvents.Init(0, Count);

	bHasRecordedValue.Reset();
	bHasRecordedValue.Init(false, Count);

	PendingTriggerEvents.Reset();
	PendingTriggerEvents.Init(0, Count);

	bWasPressedLastFrame.Reset();
	bWasPressedLastFrame.Init(false, Count);
}

void UInputReplayComponent::RefreshTrackedActions()
{
	ReleaseTrackedActionDelegates();
	BuildTrackedActionList();
	BindTrackedActionDelegates();
}

// -------------------------------------------------------------------------------------------
// Delegate bindings
// -------------------------------------------------------------------------------------------

void UInputReplayComponent::BindTrackedActionDelegates()
{
	UEnhancedInputComponent* InputComponent = ResolveEnhancedInputComponent();
	if (!InputComponent)
	{
		// Not fatal: the polling pass in PostProcessInput still reads values correctly. Only the
		// per-sample ETriggerEvent tag degrades to whatever the poll observes.
		UE_LOG(LogInputRecording, Verbose,
			TEXT("No EnhancedInputComponent on the owning controller; trigger events will come from polling only."));
		return;
	}

	static const ETriggerEvent EventsToObserve[] =
	{
		ETriggerEvent::Started,
		ETriggerEvent::Ongoing,
		ETriggerEvent::Triggered,
		ETriggerEvent::Completed,
		ETriggerEvent::Canceled,
	};

	for (int32 Index = 0; Index < TrackedActions.Num(); ++Index)
	{
		const UInputAction* Action = TrackedActions[Index];
		if (!Action)
		{
			continue;
		}

		for (const ETriggerEvent Event : EventsToObserve)
		{
			FEnhancedInputActionEventBinding& Binding = InputComponent->BindActionInstanceLambda(
				Action, Event,
				[this, Index](const FInputActionInstance& Instance)
				{
					if (PendingTriggerEvents.IsValidIndex(Index))
					{
						PendingTriggerEvents[Index] = static_cast<uint8>(Instance.GetTriggerEvent());
					}
				});

			ActionBindingHandles.Add(Binding.GetHandle());
		}
	}
}

void UInputReplayComponent::ReleaseTrackedActionDelegates()
{
	if (ActionBindingHandles.Num() == 0)
	{
		return;
	}

	if (UEnhancedInputComponent* InputComponent = ResolveEnhancedInputComponent())
	{
		for (const uint32 Handle : ActionBindingHandles)
		{
			InputComponent->RemoveBindingByHandle(Handle);
		}
	}

	ActionBindingHandles.Reset();
}

// -------------------------------------------------------------------------------------------
// Recording
// -------------------------------------------------------------------------------------------

bool UInputReplayComponent::StartRecording(const FString& DisplayName)
{
	if (Mode != EInputReplayMode::Idle)
	{
		UE_LOG(LogInputRecording, Warning, TEXT("StartRecording ignored - component is already busy (mode %d)."),
			static_cast<int32>(Mode));
		return false;
	}

	ReleaseTrackedActionDelegates();

	if (!BuildTrackedActionList())
	{
		UE_LOG(LogInputRecording, Error,
			TEXT("StartRecording found no trackable input actions. Check the recorded mapping contexts and the whitelist."));
		return false;
	}

	CurrentRecording.Reset();
	CurrentRecording.Header.RecordingId = FGuid::NewGuid();
	CurrentRecording.Header.DisplayName = DisplayName;
	CurrentRecording.Header.RecordedAtUtc = FDateTime::UtcNow();
	CurrentRecording.Header.LevelName = GetWorld() ? GetWorld()->GetMapName() : FString();
	CurrentRecording.Header.EngineVersion = FEngineVersion::Current().ToString();
	CurrentRecording.Header.TimeMode = TimeMode;
	CurrentRecording.Header.LogicalTicksPerSecond = FMath::Max(1, LogicalTicksPerSecond);
	CurrentRecording.Header.RandomSeed = FMath::Rand();
	WriteHeaderActionPaths();

	RecordingFrameIndex = 0;
	RecordingTimeSeconds = 0.0f;
	LogicalStepAccumulator = 0.0f;
	LastSteppedFrameCounter = 0;

	BindTrackedActionDelegates();
	SetTickFallbackEnabled(true);
	SetMode(EInputReplayMode::Recording);

	UE_LOG(LogInputRecording, Log, TEXT("Recording started: %s (%d tracked action(s), %s at %d Hz)."),
		*DisplayName, TrackedActions.Num(),
		TimeMode == EInputReplayTimeMode::FixedLogicalStep ? TEXT("fixed logical step") : TEXT("real time"),
		CurrentRecording.Header.LogicalTicksPerSecond);

	K2_OnRecordingStarted(DisplayName);
	return true;
}

void UInputReplayComponent::StopRecording()
{
	if (Mode != EInputReplayMode::Recording)
	{
		return;
	}

	CurrentRecording.Header.TotalFrames = RecordingFrameIndex;

	ReleaseTrackedActionDelegates();
	SetTickFallbackEnabled(false);
	SetMode(EInputReplayMode::Idle);

	UE_LOG(LogInputRecording, Log, TEXT("Recording stopped: %d sample(s) over %.2fs (%d frames)."),
		CurrentRecording.Samples.Num(), CurrentRecording.GetDurationSeconds(), RecordingFrameIndex);

	K2_OnRecordingStopped(CurrentRecording.Samples.Num(), CurrentRecording.GetDurationSeconds());
}

bool UInputReplayComponent::SaveCurrentRecording(const FString& AbsoluteBasePath, bool bAlsoExportJson)
{
	return UInputRecordingSerializer::SaveRecording(CurrentRecording, AbsoluteBasePath, bAlsoExportJson);
}

bool UInputReplayComponent::LoadRecordingFromFile(const FString& AbsoluteBasePath)
{
	return UInputRecordingSerializer::LoadRecording(AbsoluteBasePath, CurrentRecording);
}

// -------------------------------------------------------------------------------------------
// Sampling
// -------------------------------------------------------------------------------------------

FVector UInputReplayComponent::ReadActionValue(int32 TrackedIndex, uint8& OutValueType, uint8& OutTriggerEvent) const
{
	OutValueType = 0;
	OutTriggerEvent = 0;

	if (!TrackedActions.IsValidIndex(TrackedIndex))
	{
		return FVector::ZeroVector;
	}

	const UInputAction* Action = TrackedActions[TrackedIndex];
	if (!Action)
	{
		return FVector::ZeroVector;
	}

	OutValueType = static_cast<uint8>(Action->ValueType);

	const UEnhancedPlayerInput* PlayerInput = ResolveEnhancedPlayerInput();
	if (!PlayerInput)
	{
		return FVector::ZeroVector;
	}

	if (const FInputActionInstance* Instance = PlayerInput->FindActionInstanceData(Action))
	{
		OutTriggerEvent = static_cast<uint8>(Instance->GetTriggerEvent());
		return InputReplayComponentPrivate::ToVector(Instance->GetValue());
	}

	// No instance data means the action is mapped but produced nothing this frame.
	if (PendingTriggerEvents.IsValidIndex(TrackedIndex))
	{
		OutTriggerEvent = PendingTriggerEvents[TrackedIndex];
	}
	return FVector::ZeroVector;
}

bool UInputReplayComponent::IsFrameDeltaAction(int32 TrackedIndex) const
{
	return FrameDeltaTrackedIndices.Contains(TrackedIndex);
}

void UInputReplayComponent::SampleTrackedActions(int32 FrameIndex, float TimeSeconds)
{
	for (int32 Index = 0; Index < TrackedActions.Num(); ++Index)
	{
		uint8 ValueType = 0;
		uint8 TriggerEvent = 0;
		const FVector Value = ReadActionValue(Index, ValueType, TriggerEvent);

		// Delta compression: a sample exists only where something actually changed. Playback
		// reconstructs the dense stream by holding the last known value.
		const bool bFirstSample = !bHasRecordedValue[Index];
		const bool bValueChanged = !Value.Equals(LastRecordedValues[Index], KINDA_SMALL_NUMBER);
		const bool bEventChanged = TriggerEvent != LastRecordedTriggerEvents[Index];

		// The very first observation of a resting action is not worth a sample - zero is the
		// implied starting state for everything.
		if (bFirstSample && Value.IsNearlyZero())
		{
			bHasRecordedValue[Index] = true;
			LastRecordedTriggerEvents[Index] = TriggerEvent;
			continue;
		}

		if (!bFirstSample && !bValueChanged && !bEventChanged)
		{
			continue;
		}

		bHasRecordedValue[Index] = true;
		LastRecordedValues[Index] = Value;
		LastRecordedTriggerEvents[Index] = TriggerEvent;

		const FName ActionName(*InputReplayComponentPrivate::ToShortName(TrackedActions[Index]->GetPathName()));
		CurrentRecording.Samples.Emplace(ActionName, Index, FrameIndex, TimeSeconds, TriggerEvent, ValueType, Value);

		OnSampleRecorded.Broadcast(ActionName, TimeSeconds, Value);
	}

	// Consumed - the next frame's events refill this.
	for (uint8& Pending : PendingTriggerEvents)
	{
		Pending = 0;
	}
}

// -------------------------------------------------------------------------------------------
// Input hooks
// -------------------------------------------------------------------------------------------

void UInputReplayComponent::SetTickFallbackEnabled(bool bEnabled)
{
	APlayerController* Controller = ResolveOwningPlayerController();

	if (bEnabled && Controller)
	{
		// The prerequisite is the whole point: without it the component could tick before the
		// controller and every judged input would be a frame stale.
		AddTickPrerequisiteActor(Controller);
	}
	else if (Controller)
	{
		RemoveTickPrerequisiteActor(Controller);
	}

	SetComponentTickEnabled(bEnabled);
}

void UInputReplayComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// A no-op whenever the owning controller already forwarded PostProcessInput this frame; the
	// frame-counter guard inside HandlePostProcessInput sorts that out.
	HandlePostProcessInput(DeltaTime);
}

void UInputReplayComponent::HandlePreProcessInput(float DeltaSeconds)
{
	// Nothing to sample yet - Enhanced Input has not evaluated the stack. This hook exists so a
	// subclass has a place to inject or observe pre-evaluation state.
}

void UInputReplayComponent::HandlePostProcessInput(float DeltaSeconds)
{
	if (Mode == EInputReplayMode::Idle || Mode == EInputReplayMode::PlayingGhost)
	{
		return;
	}

	// Two controllers in the stack, or a single controller whose input is processed twice in one
	// engine frame, would otherwise advance the clock twice for one frame of real input.
	if (LastSteppedFrameCounter == GFrameCounter)
	{
		return;
	}
	LastSteppedFrameCounter = GFrameCounter;

	if (Mode == EInputReplayMode::MatchingInput)
	{
		StepMatchInput(DeltaSeconds);
		return;
	}

	if (TimeMode == EInputReplayTimeMode::FixedLogicalStep)
	{
		const float StepSeconds = 1.0f / static_cast<float>(FMath::Max(1, CurrentRecording.Header.LogicalTicksPerSecond));
		LogicalStepAccumulator += DeltaSeconds;

		int32 StepsTaken = 0;
		while (LogicalStepAccumulator >= StepSeconds && StepsTaken < InputReplayComponentPrivate::MaxLogicalStepsPerFrame)
		{
			LogicalStepAccumulator -= StepSeconds;
			++RecordingFrameIndex;
			++StepsTaken;
			RecordingTimeSeconds = static_cast<float>(RecordingFrameIndex) * StepSeconds;
			SampleTrackedActions(RecordingFrameIndex, RecordingTimeSeconds);
		}

		if (StepsTaken == InputReplayComponentPrivate::MaxLogicalStepsPerFrame)
		{
			// Drop the backlog rather than carrying it into the next frame, which would turn one
			// hitch into a permanently lagging clock.
			LogicalStepAccumulator = 0.0f;
		}
	}
	else
	{
		++RecordingFrameIndex;
		RecordingTimeSeconds += DeltaSeconds;
		CurrentRecording.FrameDeltaSeconds.Add(DeltaSeconds);
		SampleTrackedActions(RecordingFrameIndex, RecordingTimeSeconds);
	}
}

// -------------------------------------------------------------------------------------------
// Match Input
// -------------------------------------------------------------------------------------------

int32 UInputReplayComponent::ResolveCueTrackedIndex(const FMatchInputCue& Cue) const
{
	const FString CuePath = Cue.Action.ToString();

	if (!CuePath.IsEmpty())
	{
		for (int32 Index = 0; Index < TrackedActions.Num(); ++Index)
		{
			if (TrackedActions[Index] && TrackedActions[Index]->GetPathName() == CuePath)
			{
				return Index;
			}
		}
	}

	// The path went stale - the asset was renamed or moved since the take. The short name is
	// what survives that, which is exactly why the cue carries both.
	for (int32 Index = 0; Index < TrackedActions.Num(); ++Index)
	{
		if (!TrackedActions[Index])
		{
			continue;
		}

		if (InputReplayComponentPrivate::ToShortName(TrackedActions[Index]->GetPathName()).Equals(Cue.ActionName, ESearchCase::IgnoreCase))
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

bool UInputReplayComponent::StartMatchInput(const FInputRecording& Recording)
{
	if (!Recording.IsValidRecording())
	{
		UE_LOG(LogMatchInput, Error, TEXT("StartMatchInput refused - the supplied recording is not valid."));
		return false;
	}

	ReleaseTrackedActionDelegates();

	if (!BuildTrackedActionList())
	{
		UE_LOG(LogMatchInput, Error,
			TEXT("StartMatchInput found no live input actions to listen to. Is a gameplay mapping context pushed in this level?"));
		return false;
	}

	CurrentRecording = Recording;
	MatchCues = UMatchInputCueBuilder::BuildMatchInputCues(CurrentRecording, CueBuildOptions);

	MatchCueTrackedIndices.Reset();
	MatchCueTrackedIndices.Reserve(MatchCues.Num());

	int32 UnresolvedCues = 0;
	for (const FMatchInputCue& Cue : MatchCues)
	{
		const int32 TrackedIndex = ResolveCueTrackedIndex(Cue);
		MatchCueTrackedIndices.Add(TrackedIndex);

		if (TrackedIndex == INDEX_NONE)
		{
			++UnresolvedCues;
			UE_LOG(LogMatchInput, Warning,
				TEXT("Cue %s does not resolve to any live input action; it will be unanswerable."), *Cue.Description);
		}
	}

	if (MatchCues.Num() == 0)
	{
		UE_LOG(LogMatchInput, Warning, TEXT("Recording %s produced no cues - nothing to review."),
			*CurrentRecording.Header.DisplayName);
	}
	else if (UnresolvedCues > 0)
	{
		UE_LOG(LogMatchInput, Warning, TEXT("%d of %d cue(s) could not be resolved against the live input stack."),
			UnresolvedCues, MatchCues.Num());
	}

	CurrentCueIndex = 0;
	MatchClockSeconds = 0.0f;
	bAwaitingMatchInput = false;
	MismatchCount = 0;
	LastMismatchDescription.Reset();
	LastSteppedFrameCounter = 0;

	BindTrackedActionDelegates();
	SetTickFallbackEnabled(true);
	SetMode(EInputReplayMode::MatchingInput);

	UE_LOG(LogMatchInput, Log, TEXT("Match Input started against %s: %d cue(s) over %.2fs."),
		*CurrentRecording.Header.DisplayName, MatchCues.Num(), CurrentRecording.GetDurationSeconds());

	K2_OnMatchInputStarted(MatchCues.Num());
	return true;
}

bool UInputReplayComponent::StartMatchInputFromLoaded()
{
	return StartMatchInput(CurrentRecording);
}

void UInputReplayComponent::StopMatchInput(bool bCompleted)
{
	if (Mode != EInputReplayMode::MatchingInput)
	{
		return;
	}

	ReleaseTrackedActionDelegates();
	SetTickFallbackEnabled(false);
	bAwaitingMatchInput = false;
	SetMode(EInputReplayMode::Idle);

	UE_LOG(LogMatchInput, Log, TEXT("Match Input finished (%s) - %d of %d cue(s) answered, %d mismatch(es)."),
		bCompleted ? TEXT("completed") : TEXT("aborted"), CurrentCueIndex, MatchCues.Num(), MismatchCount);

	OnMatchInputFinished.Broadcast(bCompleted);
}

FString UInputReplayComponent::GetExpectedInputDescription() const
{
	return MatchCues.IsValidIndex(CurrentCueIndex) ? MatchCues[CurrentCueIndex].Description : FString();
}

void UInputReplayComponent::StepMatchInput(float DeltaSeconds)
{
	if (!MatchCues.IsValidIndex(CurrentCueIndex))
	{
		StopMatchInput(/*bCompleted=*/true);
		return;
	}

	const FMatchInputCue& Cue = MatchCues[CurrentCueIndex];

	if (!bAwaitingMatchInput)
	{
		// The virtual clock. Pausing it is what pauses the video too - the subsystem syncs the
		// media player to this value every tick.
		MatchClockSeconds += DeltaSeconds;

		if (MatchClockSeconds < Cue.TimeSeconds)
		{
			return;
		}

		bAwaitingMatchInput = true;
		MatchClockSeconds = Cue.TimeSeconds;

		// Anything already held when the cue appears is not an answer to it. Seeding the edge
		// state here stops a held key from instantly counting as a wrong press.
		for (int32 Index = 0; Index < TrackedActions.Num(); ++Index)
		{
			uint8 ValueType = 0;
			uint8 TriggerEvent = 0;
			bWasPressedLastFrame[Index] = ReadActionValue(Index, ValueType, TriggerEvent).Size() >= CueBuildOptions.PressThreshold;
		}

		UE_LOG(LogMatchInput, Verbose, TEXT("Cue %d/%d presented: %s"),
			CurrentCueIndex + 1, MatchCues.Num(), *Cue.Description);

		OnMatchCuePresented.Broadcast(CurrentCueIndex, MatchCues.Num(), Cue.Description);
		return;
	}

	const int32 ExpectedTrackedIndex = MatchCueTrackedIndices.IsValidIndex(CurrentCueIndex)
		? MatchCueTrackedIndices[CurrentCueIndex]
		: INDEX_NONE;

	bool bMatched = false;

	for (int32 Index = 0; Index < TrackedActions.Num(); ++Index)
	{
		// Camera and mouse movement must never count as a wrong answer.
		if (IgnoredTrackedIndices.Contains(Index) || IsFrameDeltaAction(Index))
		{
			continue;
		}

		uint8 ValueType = 0;
		uint8 TriggerEvent = 0;
		const FVector Value = ReadActionValue(Index, ValueType, TriggerEvent);
		const bool bPressed = Value.Size() >= CueBuildOptions.PressThreshold;
		const bool bRisingEdge = bPressed && !bWasPressedLastFrame[Index];
		bWasPressedLastFrame[Index] = bPressed;

		if (bMatched || !bRisingEdge)
		{
			continue;
		}

		if (Index == ExpectedTrackedIndex &&
			UMatchInputCueBuilder::DoesValueMatch(Cue.ValueType, Cue.ExpectedValue, Value, MatchDirectionTolerance, CueBuildOptions.PressThreshold))
		{
			bMatched = true;
			continue;
		}

		// Wrong press. Name what was actually pressed, keep waiting for the right thing - never
		// stall, never fail the session, never skip the cue.
		const FString Received = UMatchInputCueBuilder::FormatInputDescription(
			InputReplayComponentPrivate::ToShortName(TrackedActions[Index]->GetPathName()), ValueType, Value);

		++MismatchCount;
		LastMismatchDescription = Received;

		UE_LOG(LogMatchInput, Verbose, TEXT("Mismatch on cue %d/%d - expected %s, received %s."),
			CurrentCueIndex + 1, MatchCues.Num(), *Cue.Description, *Received);

		OnMatchInputMismatched.Broadcast(Cue.Description, Received);
	}

	if (!bMatched)
	{
		return;
	}

	const int32 MatchedIndex = CurrentCueIndex;
	++CurrentCueIndex;
	bAwaitingMatchInput = false;

	OnMatchInputMatched.Broadcast(MatchedIndex, MatchCues.Num());

	if (CurrentCueIndex >= MatchCues.Num())
	{
		StopMatchInput(/*bCompleted=*/true);
	}
}

// -------------------------------------------------------------------------------------------
// Live read-out
// -------------------------------------------------------------------------------------------

bool UInputReplayComponent::GetLiveInputSnapshot(FString& OutActionName, FVector& OutValue) const
{
	OutActionName.Reset();
	OutValue = FVector::ZeroVector;

	float BestMagnitude = 0.0f;
	int32 BestIndex = INDEX_NONE;

	for (int32 Index = 0; Index < TrackedActions.Num(); ++Index)
	{
		uint8 ValueType = 0;
		uint8 TriggerEvent = 0;
		const FVector Value = ReadActionValue(Index, ValueType, TriggerEvent);
		const float Magnitude = Value.Size();

		if (Magnitude > BestMagnitude)
		{
			BestMagnitude = Magnitude;
			BestIndex = Index;
			OutValue = Value;
		}
	}

	if (BestIndex == INDEX_NONE || BestMagnitude <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	OutActionName = InputReplayComponentPrivate::ToShortName(TrackedActions[BestIndex]->GetPathName());
	return true;
}
