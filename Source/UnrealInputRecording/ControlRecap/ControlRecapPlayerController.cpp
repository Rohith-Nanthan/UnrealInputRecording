// Copyright (c) Your Studio. All Rights Reserved.

#include "ControlRecap/ControlRecapPlayerController.h"

#include "Blueprint/UserWidget.h"
#include "Boot/RecordingBootFlags.h"
#include "ControlRecap/ControlRecapGameMode.h"
#include "ControlRecap/ControlRecapWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Input/RecordingUIInputConfig.h"
#include "InputRecordingSettings.h"
#include "InputRecordingSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Storage/RecordingStore.h"

AControlRecapPlayerController::AControlRecapPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
}

void AControlRecapPlayerController::BeginPlay()
{
	Super::BeginPlay();

	ApplyRecapInputLock();

	if (URecordingUIInputConfig* Config = LoadUIInputConfig())
	{
		UIInputConfig = Config;

		// Analog navigation on: this is a full-screen review UI, and the first thing anyone does with
		// a pad in front of one is push the stick.
		Config->ApplyTo(this, /*bAllowAnalogNavigation=*/true);
	}

	const bool bHasSession = ResolveSessionToReview(ReviewedSession);

	UClass* WidgetClass = RecapWidgetClass.IsNull()
		? UControlRecapWidget::StaticClass()
		: RecapWidgetClass.LoadSynchronous();

	if (!WidgetClass)
	{
		WidgetClass = UControlRecapWidget::StaticClass();
	}

	RecapWidget = CreateWidget<UControlRecapWidget>(this, WidgetClass);
	if (!RecapWidget)
	{
		UE_LOG(LogRecordingStore, Error, TEXT("Could not create the control recap widget."));
		return;
	}

	RecapWidget->OnClosed.AddDynamic(this, &AControlRecapPlayerController::HandleRecapClosed);
	RecapWidget->AddToViewport();

	if (bHasSession)
	{
		RecapWidget->BeginReview(ReviewedSession);
	}
	else
	{
		// Deliberately not a silent failure or an immediate travel back out: someone who booted with
		// -ControlRecap needs to be told the store is empty, not dropped back into the game.
		UE_LOG(LogRecordingStore, Warning,
			TEXT("No playable session to review. Record something first, or check -RecordingRoot."));

		RecapWidget->ShowEmptyState(TEXT("No recordings found. Record a take first, then come back."));
	}
}

void AControlRecapPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Balanced teardown: the navigation config this pushed is global to Slate, so failing to remove it
	// would leave analog menu navigation applied to the whole game after leaving the recap map.
	if (UIInputConfig)
	{
		UIInputConfig->RemoveFrom(this);
		UIInputConfig = nullptr;
	}

	if (RecapWidget)
	{
		RecapWidget->OnClosed.RemoveDynamic(this, &AControlRecapPlayerController::HandleRecapClosed);

		if (RecapWidget->IsInViewport())
		{
			RecapWidget->RemoveFromParent();
		}

		RecapWidget = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

void AControlRecapPlayerController::ApplyRecapInputLock()
{
	// Freeze the pawn rather than the input stack. See the header: MatchInput still needs Enhanced
	// Input to evaluate actions so it can tell whether the player pressed the right one.
	if (APawn* ControlledPawn = GetPawn())
	{
		ControlledPawn->DisableInput(this);
	}

	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(true);

	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);

	bShowMouseCursor = true;
}

bool AControlRecapPlayerController::ResolveSessionToReview(FRecordingSessionInfo& OutSession) const
{
	UInputRecordingSubsystem* Subsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UInputRecordingSubsystem>()
		: nullptr;

	URecordingStore* Store = Subsystem ? Subsystem->GetRecordingStore() : nullptr;
	if (!Store)
	{
		return false;
	}

	// Re-scan first. The store was last read at subsystem startup, and a take recorded since then -
	// which is exactly what the Test button just did - would otherwise be invisible here.
	Store->Rescan();

	// 1. A level that pins itself to one take.
	if (const AControlRecapGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AControlRecapGameMode>() : nullptr)
	{
		if (!GameMode->ForcedSessionFolder.IsEmpty()
			&& Store->FindSessionByFolder(GameMode->ForcedSessionFolder, OutSession))
		{
			UE_LOG(LogRecordingStore, Log, TEXT("Reviewing %s, pinned by the game mode."), *OutSession.FolderName);
			return OutSession.IsPlayable();
		}
	}

	// 2. -ControlRecap=Recording_5
	const FString RequestedFolder = RecordingBootFlags::GetRequestedSessionFolder();
	if (!RequestedFolder.IsEmpty())
	{
		if (Store->FindSessionByFolder(RequestedFolder, OutSession) && OutSession.IsPlayable())
		{
			UE_LOG(LogRecordingStore, Log, TEXT("Reviewing %s, named on the command line."), *OutSession.FolderName);
			return true;
		}

		UE_LOG(LogRecordingStore, Warning,
			TEXT("-ControlRecap asked for '%s', which is not a playable session. Falling back to the ")
			TEXT("most recent one."), *RequestedFolder);
	}

	// 3. Whatever was touched last.
	if (Store->GetMostRecentSession(OutSession))
	{
		UE_LOG(LogRecordingStore, Log, TEXT("Reviewing %s, the most recently updated session."), *OutSession.FolderName);
		return true;
	}

	return false;
}

void AControlRecapPlayerController::LeaveRecap()
{
	FSoftObjectPath Target;

	if (const AControlRecapGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AControlRecapGameMode>() : nullptr)
	{
		Target = GameMode->TargetOnCancelMap;
	}

	if (Target.IsNull())
	{
		if (const UInputRecordingSettings* Settings = UInputRecordingSettings::Get())
		{
			Target = Settings->GameplayMap;
		}
	}

	if (Target.IsNull())
	{
		// Nothing configured anywhere. RestartLevel would trap the player in the recap map, so fall
		// back to the project's own startup map instead.
		UE_LOG(LogRecordingStore, Warning,
			TEXT("Cancel has no destination: set Target On Cancel Map on the game mode, or Gameplay Map ")
			TEXT("in Project Settings > Game > Input Recording. Falling back to the default map."));

		UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this)));
		return;
	}

	UE_LOG(LogRecordingStore, Log, TEXT("Leaving the recap map for '%s'."), *Target.ToString());
	UGameplayStatics::OpenLevelBySoftObjectPtr(this, TSoftObjectPtr<UWorld>(Target));
}

void AControlRecapPlayerController::HandleRecapClosed(bool bCompletedAllCues)
{
	UE_LOG(LogRecordingStore, Log, TEXT("Control recap closed (%s)."),
		bCompletedAllCues ? TEXT("all cues matched") : TEXT("cancelled"));

	LeaveRecap();
}

URecordingUIInputConfig* AControlRecapPlayerController::LoadUIInputConfig() const
{
	const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();
	if (!Settings || Settings->UIInputConfig.IsNull())
	{
		// Not fatal. Slate's stock keyboard and d-pad navigation still works; only analog navigation
		// and the semantic Back/Accept actions are missing.
		return nullptr;
	}

	return Settings->UIInputConfig.LoadSynchronous();
}
