// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRecap/ControlRecapPlayerController.h"

#include "Blueprint/UserWidget.h"
#include "Boot/RecordingBootFlags.h"
#include "ControlRecap/ControlRecapGameMode.h"
#include "ControlRecap/ControlRecapNavigationConfig.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameMapsSettings.h"
#include "InputMappingContext.h"
#include "InputRecordingLog.h"
#include "Kismet/GameplayStatics.h"
#include "Settings/InputRecordingSettings.h"
#include "Settings/RecordingUIInputConfig.h"
#include "Store/RecordingStore.h"
#include "Subsystem/InputRecordingSubsystem.h"
#include "UI/ControlRecapWidget.h"

AControlRecapPlayerController::AControlRecapPlayerController()
{
	bShowMouseCursor = true;
}

void AControlRecapPlayerController::BeginPlay()
{
	Super::BeginPlay();

	UInputRecordingSubsystem* Subsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UInputRecordingSubsystem>() : nullptr;
	if (!Subsystem)
	{
		UE_LOG(LogMatchInput, Error, TEXT("Review map has no recording subsystem; nothing to review."));
		return;
	}

	ReplayComponent = Subsystem->ResolveReplayComponent();

	FRecordingSessionInfo Session;
	const bool bFound = ResolveSessionToReview(Session);

	const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();

	RecapWidget = Cast<UControlRecapWidget>(Subsystem->CreateWidgetFromSettingsClass(
		Settings ? Settings->ControlRecapWidgetClass : FSoftClassPath(),
		UControlRecapWidget::StaticClass(),
		TEXT("ControlRecapWidgetClass")));

	if (!RecapWidget)
	{
		UE_LOG(LogMatchInput, Error, TEXT("Could not create the review widget."));
		return;
	}

	RecapWidget->AddToViewport();
	RecapWidget->OnRecapClosed.AddUniqueDynamic(this, &AControlRecapPlayerController::HandleRecapClosed);

	// Input first, so the mapping contexts are live before MatchInput starts listening.
	SetUpReviewInputMode();
	PushGameplayMappingContexts();

	if (bFound)
	{
		RecapWidget->BeginReview(Session);
	}
	else
	{
		RecapWidget->ShowEmptyState(TEXT("No playable recording was found. Record a take, then run ir.record.test."));
	}

	K2_OnRecapSessionResolved(Session, bFound);

	Subsystem->ClearPendingReviewSpecifier();
}

void AControlRecapPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (PreviousNavigationConfig.IsValid() && FSlateApplication::IsInitialized())
	{
		// Analog navigation is scoped to this level; restoring the previous config stops the
		// stick from moving UI focus once the player is back in gameplay.
		FSlateApplication::Get().SetNavigationConfig(PreviousNavigationConfig.ToSharedRef());
		PreviousNavigationConfig.Reset();
	}

	Super::EndPlay(EndPlayReason);
}

void AControlRecapPlayerController::PreProcessInput(const float DeltaTime, const bool bGamePaused)
{
	Super::PreProcessInput(DeltaTime, bGamePaused);

	if (UInputReplayComponent* Component = ReplayComponent.Get())
	{
		Component->HandlePreProcessInput(DeltaTime);
	}
}

void AControlRecapPlayerController::PostProcessInput(const float DeltaTime, const bool bGamePaused)
{
	Super::PostProcessInput(DeltaTime, bGamePaused);

	// After Enhanced Input has evaluated the stack, so a judged input is never a frame stale.
	if (UInputReplayComponent* Component = ReplayComponent.Get())
	{
		Component->HandlePostProcessInput(DeltaTime);
	}
}

// -------------------------------------------------------------------------------------------
// Input
// -------------------------------------------------------------------------------------------

void AControlRecapPlayerController::SetUpReviewInputMode()
{
	// FInputModeGameAndUI, never FInputModeUIOnly. MatchInput reads live Enhanced Input action
	// values to judge what was pressed, and UI-only mode stops gameplay input reaching the input
	// stack entirely - every cue would hang forever waiting for input that can never arrive.
	// This is easy to get backwards: "lock the UI to input-only" sounds like the right instinct
	// and is exactly wrong here.
	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);

	bShowMouseCursor = true;

	// Deliberately no SetIgnoreMoveInput / SetIgnoreLookInput either. There is nothing to move -
	// the pawn is non-interactive - and suppressing input is precisely what must not happen in
	// a level whose whole job is observing input.

	if (FSlateApplication::IsInitialized())
	{
		PreviousNavigationConfig = FSlateApplication::Get().GetNavigationConfig();
		FSlateApplication::Get().SetNavigationConfig(MakeShared<FControlRecapNavigationConfig>());
	}
}

void AControlRecapPlayerController::PushGameplayMappingContexts()
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!InputSubsystem)
	{
		return;
	}

	const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();
	if (!Settings)
	{
		return;
	}

	// Every recorded action has to be live and observable in this level. A player pressing
	// something wrong must produce a real, named Enhanced Input event, or there is nothing to
	// report back to them.
	int32 PushedContexts = 0;
	for (const TSoftObjectPtr<UInputMappingContext>& SoftContext : Settings->RecordedMappingContexts)
	{
		if (const UInputMappingContext* Context = SoftContext.LoadSynchronous())
		{
			InputSubsystem->AddMappingContext(Context, 0);
			++PushedContexts;
		}
	}

	if (PushedContexts == 0)
	{
		UE_LOG(LogMatchInput, Warning,
			TEXT("No gameplay mapping contexts are configured, so the review map has nothing to listen to. ")
			TEXT("Set Recorded Mapping Contexts in Project Settings > Game > Input Recorder."));
	}

	// The UI verbs sit above gameplay so Accept and Back win over a gameplay binding on the same key.
	if (const URecordingUIInputConfig* UIConfig = Settings->UIInputConfig.LoadSynchronous())
	{
		if (const UInputMappingContext* UIContext = UIConfig->UIMappingContext.LoadSynchronous())
		{
			InputSubsystem->AddMappingContext(UIContext, UIConfig->PushPriority);
		}
	}
}

// -------------------------------------------------------------------------------------------
// Session resolution
// -------------------------------------------------------------------------------------------

bool AControlRecapPlayerController::ResolveSessionToReview(FRecordingSessionInfo& OutSession) const
{
	const UInputRecordingSubsystem* Subsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UInputRecordingSubsystem>() : nullptr;
	URecordingStore* Store = Subsystem ? Subsystem->GetStore() : nullptr;
	if (!Store)
	{
		return false;
	}

	Store->Rescan();

	const FRecordingBootFlags& BootFlags = RecordingBootFlags::Get();

	// 1. A bare -IR=1 asks for the most recent and skips every other rule.
	const bool bForceMostRecent = BootFlags.Mode == ERecordingBootMode::ControlRecap && BootFlags.RequestedSession.IsEmpty();

	// 2. The command line naming a session outranks the level's own pin on purpose: the pin is a
	//    design-time choice baked into a map, and somebody typing a flag is overriding it for
	//    this run deliberately.
	FString Specifier = BootFlags.RequestedSession;

	if (Specifier.IsEmpty() && Subsystem)
	{
		// ir.record.test <session> travels through here too, via the pending specifier.
		Specifier = Subsystem->GetPendingReviewSpecifier();
	}

	// 3. The game mode's pin.
	if (Specifier.IsEmpty() && !bForceMostRecent)
	{
		if (const AControlRecapGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AControlRecapGameMode>() : nullptr)
		{
			Specifier = GameMode->ForcedSessionFolder;
		}
	}

	// 4. Most recently updated playable session.
	if (Store->ResolveSessionSpecifier(Specifier, OutSession) && OutSession.IsPlayable())
	{
		UE_LOG(LogMatchInput, Log, TEXT("Reviewing %s (%s)."), *OutSession.FolderName, *OutSession.DisplayName);
		return true;
	}

	if (!Specifier.IsEmpty())
	{
		UE_LOG(LogMatchInput, Error, TEXT("Review map was asked for '%s', which does not exist or has no ghost."), *Specifier);
	}

	return false;
}

// -------------------------------------------------------------------------------------------
// Leaving
// -------------------------------------------------------------------------------------------

void AControlRecapPlayerController::HandleRecapClosed(bool bCompletedAllCues)
{
	LeaveRecap();
}

void AControlRecapPlayerController::LeaveRecap()
{
	FString Destination;

	if (const AControlRecapGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AControlRecapGameMode>() : nullptr)
	{
		if (GameMode->TargetOnCancelMap.IsValid())
		{
			Destination = GameMode->TargetOnCancelMap.GetLongPackageName();
		}
	}

	if (Destination.IsEmpty())
	{
		if (const UInputRecordingSettings* Settings = UInputRecordingSettings::Get())
		{
			if (Settings->GameplayMap.IsValid())
			{
				Destination = Settings->GameplayMap.GetLongPackageName();
			}
		}
	}

	if (Destination.IsEmpty())
	{
		// Never leave a dead end with nowhere to go.
		Destination = UGameMapsSettings::GetGameDefaultMap();
	}

	if (Destination.IsEmpty())
	{
		UE_LOG(LogMatchInput, Error, TEXT("Nowhere to travel to on leaving the review map; staying put."));
		return;
	}

	UE_LOG(LogMatchInput, Log, TEXT("Leaving the review map for %s."), *Destination);
	UGameplayStatics::OpenLevel(this, FName(*Destination));
}
