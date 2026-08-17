// Copyright (c) Your Studio. All Rights Reserved.

#include "InputReplayComponent.h"

#include "EnhancedInputSubsystems.h"		// UEnhancedInputLocalPlayerSubsystem
#include "EnhancedPlayerInput.h"			// UEnhancedPlayerInput
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/MovementComponent.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "UObject/SoftObjectPtr.h"
#include "InputAction.h"					// UInputAction, FInputActionInstance
#include "InputMappingContext.h"
#include "InputReplaySerializer.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"

DEFINE_LOG_CATEGORY(LogInputReplay);

UInputReplayComponent::UInputReplayComponent()
{
	// The component can tick as a *fallback* driver, but the correct hook points are
	// APlayerController::PreProcessInput / PostProcessInput (see AReplayPlayerController).
	// TG_PrePhysics keeps us ahead of movement in the fallback path.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	bAutoActivate = true;
}

// ---------------------------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------------------------

void UInputReplayComponent::BeginPlay()
{
	Super::BeginPlay();
	BuildActionRegistry();
}

void UInputReplayComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Never leave the engine in fixed-timestep mode or the player without their mappings.
	if (Mode == EInputReplayMode::Playing)
	{
		StopPlayback();
	}
	else if (Mode == EInputReplayMode::Recording)
	{
		StopRecording();
	}

	Super::EndPlay(EndPlayReason);
}

void UInputReplayComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (Mode == EInputReplayMode::Idle)
	{
		return;
	}

	// If the owning PlayerController is forwarding PreProcessInput/PostProcessInput we have
	// already done our work this frame and must not double-step the logical clock.
	if (LastHookFrameCounter == GFrameCounter)
	{
		return;
	}

	if (!bWarnedAboutMissingHooks)
	{
		bWarnedAboutMissingHooks = true;
		UE_LOG(LogInputReplay, Warning,
			TEXT("UInputReplayComponent is running from TickComponent. Injected input will land one ")
			TEXT("frame late and sampling happens before the input stack settles. Forward ")
			TEXT("APlayerController::PreProcessInput/PostProcessInput to TickPreInput/TickPostInput ")
			TEXT("(see AReplayPlayerController) for frame-accurate results."));
	}

	const bool bPaused = GetWorld() ? GetWorld()->IsPaused() : false;
	TickPreInput(DeltaTime, bPaused);
	TickPostInput(DeltaTime, bPaused);
	LastHookFrameCounter = 0; // keep the fallback path active
}

// ---------------------------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------------------------

APlayerController* UInputReplayComponent::GetOwningPlayerController() const
{
	if (APlayerController* AsPC = Cast<APlayerController>(GetOwner()))
	{
		return AsPC;
	}
	if (const APawn* AsPawn = Cast<APawn>(GetOwner()))
	{
		return Cast<APlayerController>(AsPawn->GetController());
	}
	return nullptr;
}

APawn* UInputReplayComponent::GetTargetPawn() const
{
	APlayerController* PC = GetOwningPlayerController();
	if (PC)
{
    return PC->GetPawn();
}
return Cast<APawn>(GetOwner());
}

UEnhancedPlayerInput* UInputReplayComponent::GetEnhancedPlayerInput() const
{
	const APlayerController* PC = GetOwningPlayerController();
	return PC ? Cast<UEnhancedPlayerInput>(PC->PlayerInput) : nullptr;
}

UEnhancedInputLocalPlayerSubsystem* UInputReplayComponent::GetEnhancedInputSubsystem() const
{
	const APlayerController* PC = GetOwningPlayerController();
	const ULocalPlayer* LP = PC ? PC->GetLocalPlayer() : nullptr;
	return LP ? LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
}

FVector UInputReplayComponent::Quantise(const FVector& In) const
{
	if (ValueQuantisationStep <= 0.0f)
	{
		return In;
	}

	const double Step = static_cast<double>(ValueQuantisationStep);
	return FVector(FMath::GridSnap(In.X, Step), FMath::GridSnap(In.Y, Step), FMath::GridSnap(In.Z, Step));
}

// ---------------------------------------------------------------------------------------------
// Action registry
// ---------------------------------------------------------------------------------------------

void UInputReplayComponent::BuildActionRegistry()
{
	TrackedActions.Reset();
	TrackedActionIsDelta.Reset();

	auto AddAction = [this](const UInputAction* Action)
	{
		if (Action && !TrackedActions.Contains(Action))
		{
			TrackedActions.Add(Action);
		}
	};

	for (const UInputMappingContext* Context : RecordedContexts)
	{
		if (!Context)
		{
			continue;
		}
		for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
		{
			AddAction(Mapping.Action);
		}
	}

	for (const UInputAction* Action : AdditionalActions)
	{
		AddAction(Action);
	}

	TrackedActionIsDelta.SetNum(TrackedActions.Num());
	for (int32 Index = 0; Index < TrackedActions.Num(); ++Index)
	{
		TrackedActionIsDelta[Index] = FrameDeltaActions.Contains(const_cast<UInputAction*>(TrackedActions[Index].Get()));
	}

	RecorderStates.Reset();
	RecorderStates.SetNum(TrackedActions.Num());
	PlaybackStates.Reset();
	PlaybackStates.SetNum(TrackedActions.Num());

	UE_LOG(LogInputReplay, Log, TEXT("Tracking %d input action(s)."), TrackedActions.Num());
}

bool UInputReplayComponent::ResolveActionRegistry(FString& OutError)
{
	TrackedActions.Reset();
	TrackedActionIsDelta.Reset();
	TrackedActions.Reserve(Recording.Header.ActionPaths.Num());

	for (const FString& Path : Recording.Header.ActionPaths)
	{
		// Recordings reference actions by soft path, so a recording made in a previous session
		// still resolves as long as the asset exists. Missing entries are kept as nullptr so the
		// remaining indices stay aligned.
		FSoftObjectPath SoftPath(Path);
		UInputAction* Resolved = Cast<UInputAction>(SoftPath.TryLoad());

		if (!Resolved)
		{
			UE_LOG(LogInputReplay, Warning, TEXT("Could not resolve UInputAction '%s'; it will be skipped."), *Path);
		}
		TrackedActions.Add(Resolved);
	}

	if (TrackedActions.Num() == 0)
	{
		OutError = TEXT("Recording contains no resolvable Input Actions.");
		return false;
	}

	TrackedActionIsDelta.Init(false, TrackedActions.Num());
	for (const int32 DeltaIndex : Recording.Header.FrameDeltaActionIndices)
	{
		if (TrackedActionIsDelta.IsValidIndex(DeltaIndex))
		{
			TrackedActionIsDelta[DeltaIndex] = true;
		}
	}

	PlaybackStates.Reset();
	PlaybackStates.SetNum(TrackedActions.Num());
	return true;
}

// ---------------------------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------------------------

void UInputReplayComponent::StartRecording(const FString& DisplayName)
{
	if (Mode != EInputReplayMode::Idle)
	{
		UE_LOG(LogInputReplay, Warning, TEXT("StartRecording ignored: component is busy."));
		return;
	}

	BuildActionRegistry();
	if (TrackedActions.Num() == 0)
	{
		UE_LOG(LogInputReplay, Error, TEXT("StartRecording aborted: no tracked actions. Assign RecordedContexts."));
		return;
	}

	Recording.Reset();
	Recording.Header.RecordingId = FGuid::NewGuid();
	Recording.Header.DisplayName = DisplayName;
	Recording.Header.RecordedAtUtc = FDateTime::UtcNow();
	Recording.Header.EngineVersion = FEngineVersion::Current().ToString();
	Recording.Header.LevelName = GetWorld() ? GetWorld()->GetMapName() : FString();
	Recording.Header.TimeMode = static_cast<uint8>(TimeMode);
	Recording.Header.LogicalTicksPerSecond = LogicalTicksPerSecond;

	Recording.Header.ActionPaths.Reserve(TrackedActions.Num());
	for (int32 Index = 0; Index < TrackedActions.Num(); ++Index)
	{
		Recording.Header.ActionPaths.Add(TrackedActions[Index]->GetPathName());
		if (TrackedActionIsDelta[Index])
		{
			Recording.Header.FrameDeltaActionIndices.Add(Index);
		}
	}

	// Seed the RNG NOW, not just on playback. If the recording session and the playback session
	// do not start from the same seed, any gameplay code that calls FMath::Rand will diverge no
	// matter how perfect the input reproduction is.
	Recording.Header.RandomSeed = static_cast<int32>(FDateTime::UtcNow().GetTicks() & 0x7FFFFFFF);
	if (bReseedGlobalRandom)
	{
		FMath::RandInit(Recording.Header.RandomSeed);
		FMath::SRandInit(Recording.Header.RandomSeed);
	}

	for (FRecorderActionState& State : RecorderStates)
	{
		State = FRecorderActionState();
	}

	TimeAccumulator = 0.0f;
	ElapsedSeconds = 0.0f;
	CurrentFrameIndex = 0;

	Mode = EInputReplayMode::Recording;
	OnRecordingStarted.Broadcast();

	UE_LOG(LogInputReplay, Log, TEXT("Recording started (%d actions, mode=%d, %d Hz)."),
		TrackedActions.Num(), static_cast<int32>(TimeMode), LogicalTicksPerSecond);
}

void UInputReplayComponent::StopRecording()
{
	if (Mode != EInputReplayMode::Recording)
	{
		return;
	}

	Recording.Header.TotalFrames = CurrentFrameIndex;
	Mode = EInputReplayMode::Idle;
	OnRecordingStopped.Broadcast();

	UE_LOG(LogInputReplay, Log, TEXT("Recording stopped: %d ticks, %d samples, %.2fs."),
		Recording.Header.TotalFrames, Recording.Frames.Num(), Recording.GetDurationSeconds());
}

void UInputReplayComponent::SampleRecording(float DeltaSeconds)
{
	UEnhancedPlayerInput* PlayerInput = GetEnhancedPlayerInput();
	if (!PlayerInput)
	{
		return;
	}

	// ---- 1. Sample every tracked action for this ENGINE frame -------------------------------
	for (int32 Index = 0; Index < TrackedActions.Num(); ++Index)
	{
		const UInputAction* Action = TrackedActions[Index];
		if (!Action)
		{
			continue;
		}

		FRecorderActionState& State = RecorderStates[Index];

		const FInputActionValue Sampled = PlayerInput->GetActionValue(Action);
		const FVector Value = Quantise(Sampled.Get<FVector>());

		// FindActionInstanceData gives us the trigger event the action resolved to this frame.
		// It returns null until the action has been evaluated at least once.
		ETriggerEvent Event = ETriggerEvent::None;
		if (const FInputActionInstance* Instance = PlayerInput->FindActionInstanceData(Action))
		{
			Event = Instance->GetTriggerEvent();
		}

		State.LatestValue = Value;
		State.LatestEvent = Event;

		if (TrackedActionIsDelta[Index])
		{
			// Deltas are additive: never overwrite, always sum.
			State.AccumulatedDelta += Value;
		}

		if (Value.SizeSquared() > State.PeakValue.SizeSquared())
		{
			State.PeakValue = Value;
			State.bSawTransient = true;
		}
		if (Event == ETriggerEvent::Started || Event == ETriggerEvent::Completed || Event == ETriggerEvent::Canceled)
		{
			State.bSawTransient = true;
		}
	}

	// ---- 2. Advance the logical clock -------------------------------------------------------
	int32 Steps = 1;
	if (TimeMode == EInputReplayTimeMode::FixedLogicalStep || TimeMode == EInputReplayTimeMode::FreeRun)
	{
		const float FixedStep = Recording.Header.GetFixedStepSeconds();
		TimeAccumulator += DeltaSeconds;
		Steps = FMath::FloorToInt(TimeAccumulator / FixedStep);

		if (Steps <= 0)
		{
			// Running faster than the logical rate. Keep latching; nothing is lost because
			// PeakValue / AccumulatedDelta carry over to the next emitted tick.
			return;
		}
		TimeAccumulator -= Steps * FixedStep;
		ElapsedSeconds += Steps * FixedStep;
	}
	else // RecordedDeltas: one logical tick per engine frame, delta stored verbatim.
	{
		Recording.FrameDeltaSeconds.Add(DeltaSeconds);
		ElapsedSeconds += DeltaSeconds;
	}

	const int32 FirstStep = CurrentFrameIndex;
	const int32 LastStep = CurrentFrameIndex + Steps - 1;

	// ---- 3. Emit samples --------------------------------------------------------------------
	for (int32 Index = 0; Index < TrackedActions.Num(); ++Index)
	{
		if (!TrackedActions[Index])
		{
			continue;
		}

		FRecorderActionState& State = RecorderStates[Index];
		const EInputActionValueType ValueType = TrackedActions[Index]->ValueType;

		if (TrackedActionIsDelta[Index])
		{
			// Spread this engine frame's accumulated delta evenly across the logical ticks it
			// covers. Playback SUMS the ticks it consumes, so the total is preserved exactly
			// regardless of the framerate it is replayed at.
			if (!State.AccumulatedDelta.IsNearlyZero())
			{
				const FVector Share = State.AccumulatedDelta / static_cast<double>(Steps);
				for (int32 Step = 0; Step < Steps; ++Step)
				{
					EmitFrame(FirstStep + Step, Index, State.LatestEvent, FInputActionValue(ValueType, Share));
				}
			}
			State.AccumulatedDelta = FVector::ZeroVector;
		}
		else
		{
			// A press that started AND ended inside a single engine frame would otherwise vanish.
			// Emit the peak first, then push the settled value out by at least one tick so the
			// injected press is visible for a full logical tick on playback.
			const bool bTapCollapsed = State.bSawTransient && !State.PeakValue.Equals(State.LatestValue, KINDA_SMALL_NUMBER);
			if (bTapCollapsed)
			{
				EmitFrame(FirstStep, Index, ETriggerEvent::Triggered, FInputActionValue(ValueType, State.PeakValue));
			}

			const int32 SettleStep = bTapCollapsed ? FMath::Max(LastStep, FirstStep + 1) : LastStep;
			EmitFrame(SettleStep, Index, State.LatestEvent, FInputActionValue(ValueType, State.LatestValue));
		}

		State.PeakValue = FVector::ZeroVector;
		State.bSawTransient = false;
	}

	// ---- 4. Sync points ---------------------------------------------------------------------
	if (SyncPointIntervalFrames > 0)
	{
		for (int32 Step = FirstStep; Step <= LastStep; ++Step)
		{
			if (Step % SyncPointIntervalFrames == 0)
			{
				CaptureSyncPoint(Step);
				break;
			}
		}
	}

	CurrentFrameIndex += Steps;
	Recording.Header.TotalFrames = CurrentFrameIndex;
}

void UInputReplayComponent::EmitFrame(int32 FrameIndex, int32 ActionIndex, ETriggerEvent Event, const FInputActionValue& Value)
{
	FRecorderActionState& State = RecorderStates[ActionIndex];
	const FVector Raw = Value.Get<FVector>();

	// Delta-compression: "rate" actions only need a sample when something actually changed,
	// because playback holds the last value. Deltas are always written (and only when non-zero),
	// because playback consumes and clears them.
	if (!TrackedActionIsDelta[ActionIndex])
	{
		if (State.bHasEmitted && State.LastEmittedValue.Equals(Raw, KINDA_SMALL_NUMBER) && State.LastEmittedEvent == Event)
		{
			return;
		}
		State.LastEmittedValue = Raw;
		State.LastEmittedEvent = Event;
		State.bHasEmitted = true;
	}

	const float TimeSeconds = (TimeMode == EInputReplayTimeMode::RecordedDeltas)
		? ElapsedSeconds
		: FrameIndex * Recording.Header.GetFixedStepSeconds();

	Recording.Frames.Emplace(FrameIndex, TimeSeconds, ActionIndex, Event, Value);
}

void UInputReplayComponent::CaptureSyncPoint(int32 FrameIndex)
{
	const APawn* Pawn = GetTargetPawn();
	if (!Pawn)
	{
		return;
	}

	FReplaySyncPoint Point;
	Point.FrameIndex = FrameIndex;
	Point.Location = Pawn->GetActorLocation();
	Point.Rotation = Pawn->GetActorRotation();
	Point.Velocity = Pawn->GetVelocity();

	if (const APlayerController* PC = GetOwningPlayerController())
	{
		Point.ControlRotation = PC->GetControlRotation();
	}

	Recording.SyncPoints.Add(Point);
}

// ---------------------------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------------------------

bool UInputReplayComponent::SaveRecordingToFile(const FString& FileName, bool bAsJson)
{
	if (Mode == EInputReplayMode::Recording)
	{
		StopRecording();
	}

	if (!Recording.IsValidRecording())
	{
		UE_LOG(LogInputReplay, Error, TEXT("SaveRecordingToFile: nothing to save."));
		return false;
	}

	FString Error;
	if (!UInputReplaySerializer::Save(Recording, FileName, bAsJson, Error))
	{
		UE_LOG(LogInputReplay, Error, TEXT("SaveRecordingToFile failed: %s"), *Error);
		return false;
	}
	return true;
}

bool UInputReplayComponent::LoadRecordingFromFile(const FString& FileName, bool bAsJson)
{
	if (Mode != EInputReplayMode::Idle)
	{
		UE_LOG(LogInputReplay, Warning, TEXT("LoadRecordingFromFile ignored: component is busy."));
		return false;
	}

	FString Error;
	if (!UInputReplaySerializer::Load(Recording, FileName, bAsJson, Error))
	{
		UE_LOG(LogInputReplay, Error, TEXT("LoadRecordingFromFile failed: %s"), *Error);
		return false;
	}

	// Soft validation - a level mismatch is nearly always a mistake, but not worth blocking on.
	const FString CurrentLevel = GetWorld() ? GetWorld()->GetMapName() : FString();
	if (!Recording.Header.LevelName.IsEmpty() && Recording.Header.LevelName != CurrentLevel)
	{
		UE_LOG(LogInputReplay, Warning, TEXT("Recording was made on '%s' but the current level is '%s'. Expect divergence."),
			*Recording.Header.LevelName, *CurrentLevel);
	}

	return true;
}

// ---------------------------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------------------------

bool UInputReplayComponent::StartPlayback()
{
	if (Mode != EInputReplayMode::Idle)
	{
		UE_LOG(LogInputReplay, Warning, TEXT("StartPlayback ignored: component is busy."));
		return false;
	}
	if (!Recording.IsValidRecording())
	{
		UE_LOG(LogInputReplay, Error, TEXT("StartPlayback: no recording loaded."));
		return false;
	}

	FString Error;
	if (!ResolveActionRegistry(Error))
	{
		UE_LOG(LogInputReplay, Error, TEXT("StartPlayback: %s"), *Error);
		return false;
	}

	// Always replay in the mode the recording was captured with; mixing them is meaningless.
	TimeMode = static_cast<EInputReplayTimeMode>(Recording.Header.TimeMode);
	LogicalTicksPerSecond = Recording.Header.LogicalTicksPerSecond;

	CurrentFrameIndex = 0;
	FrameCursor = 0;
	SyncPointCursor = 0;
	TimeAccumulator = 0.0f;
	ElapsedSeconds = 0.0f;

	for (FPlaybackActionState& State : PlaybackStates)
	{
		State = FPlaybackActionState();
	}

	if (bReseedGlobalRandom)
	{
		FMath::RandInit(Recording.Header.RandomSeed);
		FMath::SRandInit(Recording.Header.RandomSeed);
	}

	if (bSuppressLiveInputDuringPlayback)
	{
		ApplyLiveInputSuppression(true);
	}

	if (TimeMode == EInputReplayTimeMode::RecordedDeltas)
	{
		BeginFixedTimeStepOverride();
	}

	Mode = EInputReplayMode::Playing;
	OnPlaybackStarted.Broadcast();

	UE_LOG(LogInputReplay, Log, TEXT("Playback started: %d ticks, %.2fs, mode=%d."),
		Recording.Header.TotalFrames, Recording.GetDurationSeconds(), static_cast<int32>(TimeMode));
	return true;
}

void UInputReplayComponent::StopPlayback()
{
	if (Mode != EInputReplayMode::Playing)
	{
		return;
	}

	Mode = EInputReplayMode::Idle;

	EndFixedTimeStepOverride();
	ApplyLiveInputSuppression(false);

	OnPlaybackFinished.Broadcast();
	UE_LOG(LogInputReplay, Log, TEXT("Playback finished at tick %d."), CurrentFrameIndex);
}

float UInputReplayComponent::GetPlaybackProgress() const
{
	const int32 Total = Recording.Header.TotalFrames;
	return (Total > 0) ? FMath::Clamp(static_cast<float>(CurrentFrameIndex) / static_cast<float>(Total), 0.0f, 1.0f) : 0.0f;
}

void UInputReplayComponent::TickPreInput(float DeltaSeconds, bool bGamePaused)
{
	LastHookFrameCounter = GFrameCounter;

	if (bGamePaused || Mode != EInputReplayMode::Playing)
	{
		return;
	}

	int32 TargetFrame = CurrentFrameIndex;
	const float FixedStep = Recording.Header.GetFixedStepSeconds();

	switch (TimeMode)
	{
	case EInputReplayTimeMode::FixedLogicalStep:
	{
		TimeAccumulator += DeltaSeconds;
		const int32 Steps = FMath::FloorToInt(TimeAccumulator / FixedStep);
		if (Steps > 0)
		{
			TimeAccumulator -= Steps * FixedStep;
			TargetFrame += Steps;
		}
		break;
	}

	case EInputReplayTimeMode::RecordedDeltas:
		// Exactly one logical tick per engine frame - the engine's delta is the recorded delta.
		TargetFrame = CurrentFrameIndex + 1;
		break;

	case EInputReplayTimeMode::FreeRun:
		ElapsedSeconds += DeltaSeconds;
		TargetFrame = FMath::FloorToInt(ElapsedSeconds / FixedStep);
		break;
	}

	const int32 StepsConsumed = TargetFrame - CurrentFrameIndex;
	AdvanceStateTo(TargetFrame, StepsConsumed);
	CurrentFrameIndex = TargetFrame;

	InjectCurrentState();

	if (TimeMode == EInputReplayTimeMode::RecordedDeltas)
	{
		// The engine samples the fixed delta at the START of a frame, so whatever we set here is
		// consumed by the NEXT frame. Pre-arm the delta belonging to the tick we are about to run.
		if (Recording.FrameDeltaSeconds.IsValidIndex(CurrentFrameIndex))
		{
			FApp::SetFixedDeltaTime(static_cast<double>(Recording.FrameDeltaSeconds[CurrentFrameIndex]));
		}
	}

	if (CurrentFrameIndex >= Recording.Header.TotalFrames)
	{
		if (bLoopPlayback)
		{
			CurrentFrameIndex = 0;
			FrameCursor = 0;
			SyncPointCursor = 0;
			TimeAccumulator = 0.0f;
			ElapsedSeconds = 0.0f;
			for (FPlaybackActionState& State : PlaybackStates)
			{
				State = FPlaybackActionState();
			}
		}
		else
		{
			StopPlayback();
		}
	}
}

void UInputReplayComponent::TickPostInput(float DeltaSeconds, bool bGamePaused)
{
	if (bGamePaused)
	{
		return;
	}

	if (Mode == EInputReplayMode::Recording)
	{
		// Sampling AFTER the input stack has been processed is what makes the recorded values
		// authoritative: modifiers, triggers, dead zones and negate/swizzle have all been applied.
		SampleRecording(DeltaSeconds);
	}
	else if (Mode == EInputReplayMode::Playing)
	{
		ValidateSyncPoint(CurrentFrameIndex);
	}
}

void UInputReplayComponent::AdvanceStateTo(int32 TargetFrame, int32 StepsConsumed)
{
	// Fold every sample up to and including TargetFrame into the reconstructed live state.
	while (Recording.Frames.IsValidIndex(FrameCursor) && Recording.Frames[FrameCursor].FrameIndex <= TargetFrame)
	{
		const FRecordedInputFrame& Frame = Recording.Frames[FrameCursor++];
		if (!PlaybackStates.IsValidIndex(Frame.ActionIndex))
		{
			continue;
		}

		FPlaybackActionState& State = PlaybackStates[Frame.ActionIndex];
		const FVector Value = Frame.ToActionValue().Get<FVector>();

		if (TrackedActionIsDelta[Frame.ActionIndex])
		{
			// SUM deltas. Whether the recorded 10 ticks of mouse movement arrive across 10 engine
			// frames or all in one, the total rotation applied is identical.
			State.PendingDelta += Value;
		}
		else
		{
			State.CurrentValue = Value;
			State.bTouchedThisSpan = true;
			if (Value.SizeSquared() > State.SpanPeakValue.SizeSquared())
			{
				State.SpanPeakValue = Value;
			}
		}
	}

	// Tap preservation when catching up: if a press started and released entirely inside the span
	// of ticks we just collapsed into one engine frame, hold the press for this frame and release
	// on the next. Only kicks in while behind - at a healthy framerate playback is exact.
	for (int32 Index = 0; Index < PlaybackStates.Num(); ++Index)
	{
		if (TrackedActionIsDelta.IsValidIndex(Index) && TrackedActionIsDelta[Index])
		{
			continue;
		}

		FPlaybackActionState& State = PlaybackStates[Index];

		if (State.bReleasePending && !State.bTouchedThisSpan)
		{
			State.CurrentValue = FVector::ZeroVector;
			State.bReleasePending = false;
		}
		else if (StepsConsumed > 1 && State.bTouchedThisSpan
			&& State.CurrentValue.IsNearlyZero() && !State.SpanPeakValue.IsNearlyZero())
		{
			State.CurrentValue = State.SpanPeakValue;
			State.bReleasePending = true;
		}

		State.SpanPeakValue = FVector::ZeroVector;
		State.bTouchedThisSpan = false;
	}
}

void UInputReplayComponent::InjectCurrentState()
{
	UEnhancedInputLocalPlayerSubsystem* Subsystem = GetEnhancedInputSubsystem();
	if (!Subsystem)
	{
		return;
	}

	// Injecting with no extra modifiers/triggers: the UInputAction asset's own modifiers and
	// triggers still run, which is exactly what we want - the recorded value is the post-modifier
	// value, and the triggers regenerate Started/Ongoing/Triggered/Completed for free.
	const TArray<UInputModifier*> NoModifiers;
	const TArray<UInputTrigger*> NoTriggers;

	for (int32 Index = 0; Index < TrackedActions.Num(); ++Index)
	{
		const UInputAction* Action = TrackedActions[Index];
		if (!Action)
		{
			continue;
		}

		FPlaybackActionState& State = PlaybackStates[Index];
		const bool bIsDelta = TrackedActionIsDelta[Index];
		const FVector Value = bIsDelta ? State.PendingDelta : State.CurrentValue;

		if (!Value.IsNearlyZero())
		{
			// Injected values live for a single evaluation, so held inputs MUST be re-injected
			// every single engine frame for as long as they are held.
			Subsystem->InjectInputForAction(Action, FInputActionValue(Action->ValueType, Value), NoModifiers, NoTriggers);
			State.bNeedsZeroFlush = true;
		}
		else if (State.bNeedsZeroFlush)
		{
			// Push exactly one zero so the action's triggers observe the release and fire
			// Completed/Canceled, then go quiet.
			Subsystem->InjectInputForAction(Action, FInputActionValue(Action->ValueType, FVector::ZeroVector), NoModifiers, NoTriggers);
			State.bNeedsZeroFlush = false;
		}

		if (bIsDelta)
		{
			State.PendingDelta = FVector::ZeroVector;
		}
	}
}

void UInputReplayComponent::ValidateSyncPoint(int32 FrameIndex)
{
	APawn* Pawn = GetTargetPawn();
	APlayerController* PC = GetOwningPlayerController();
	if (!Pawn || !PC)
	{
		return;
	}

	while (Recording.SyncPoints.IsValidIndex(SyncPointCursor)
		&& Recording.SyncPoints[SyncPointCursor].FrameIndex <= FrameIndex)
	{
		const FReplaySyncPoint& Point = Recording.SyncPoints[SyncPointCursor++];

		const float PositionErrorCm = static_cast<float>(FVector::Dist(Pawn->GetActorLocation(), Point.Location));
		const float RotationErrorDeg = static_cast<float>(
			FMath::Abs(FRotator::NormalizeAxis((PC->GetControlRotation() - Point.ControlRotation).Yaw)));

		if (PositionErrorCm > DesyncToleranceCm)
		{
			OnDesyncDetected.Broadcast(Point.FrameIndex, PositionErrorCm, RotationErrorDeg);

			UE_LOG(LogInputReplay, Warning, TEXT("Desync at tick %d: %.1fcm / %.1fdeg."),
				Point.FrameIndex, PositionErrorCm, RotationErrorDeg);

			if (bCorrectDriftAtSyncPoints)
			{
				// Teleport rather than sweep: a swept correction can be blocked and make it worse.
				Pawn->SetActorLocationAndRotation(Point.Location, Point.Rotation, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
				PC->SetControlRotation(Point.ControlRotation);

				if (UMovementComponent* Movement = Cast<UMovementComponent>(Pawn->GetMovementComponent()))
				{
					Movement->Velocity = Point.Velocity;
				}
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Playback environment
// ---------------------------------------------------------------------------------------------

void UInputReplayComponent::ApplyLiveInputSuppression(bool bSuppress)
{
	UEnhancedInputLocalPlayerSubsystem* Subsystem = GetEnhancedInputSubsystem();
	if (!Subsystem)
	{
		return;
	}

	if (bSuppress)
	{
		SuppressedContexts.Reset();
		for (UInputMappingContext* Context : RecordedContexts)
		{
			if (Context && Subsystem->HasMappingContext(Context))
			{
				Subsystem->RemoveMappingContext(Context);
				SuppressedContexts.Add(Context);
			}
		}

		// Note: we deliberately do NOT call DisableInput() or SetIgnoreMoveInput(). Those would
		// suppress the replayed input too. Removing the key mappings leaves the action bindings
		// intact, and injection bypasses key mappings entirely.
	}
	else
	{
		for (UInputMappingContext* Context : SuppressedContexts)
		{
			if (Context)
			{
				Subsystem->AddMappingContext(Context, RestoreContextPriority);
			}
		}
		SuppressedContexts.Reset();
	}
}

void UInputReplayComponent::BeginFixedTimeStepOverride()
{
	if (bPushedFixedTimeStep || Recording.FrameDeltaSeconds.Num() == 0)
	{
		return;
	}

	bSavedUseFixedTimeStep = FApp::UseFixedTimeStep();
	SavedFixedDeltaTime = FApp::GetFixedDeltaTime();

	FApp::SetFixedDeltaTime(static_cast<double>(Recording.FrameDeltaSeconds[0]));
	FApp::SetUseFixedTimeStep(true);
	bPushedFixedTimeStep = true;

	UE_LOG(LogInputReplay, Log, TEXT("Fixed timestep engaged for playback; the sim is now decoupled from the wall clock."));
}

void UInputReplayComponent::EndFixedTimeStepOverride()
{
	if (!bPushedFixedTimeStep)
	{
		return;
	}

	FApp::SetUseFixedTimeStep(bSavedUseFixedTimeStep);
	FApp::SetFixedDeltaTime(SavedFixedDeltaTime);
	bPushedFixedTimeStep = false;
}
