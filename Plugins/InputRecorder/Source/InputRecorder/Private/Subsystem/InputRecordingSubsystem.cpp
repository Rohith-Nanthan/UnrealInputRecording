// Copyright Epic Games, Inc. All Rights Reserved.

#include "Subsystem/InputRecordingSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputRecordingLog.h"
#include "InputReplay/InputRecordingSerializer.h"
#include "Kismet/GameplayStatics.h"
#include "Library/InputRecordingFormatLibrary.h"
#include "MatchInput/MatchInputCueBuilder.h"
#include "Settings/InputRecordingSettings.h"
#include "Store/RecordingStore.h"
#include "UI/InputRecorderOverlayWidget.h"
#include "UI/RecordingListWidget.h"
#include "UI/RecordingToastWidget.h"
#include "Video/InputRecordingVideoCapture.h"
#include "Video/InputRecordingVideoPlayer.h"

namespace InputRecordingSubsystemPrivate
{
	/** Roughly once a second, per the quota-guard spec. */
	constexpr float QuotaPollIntervalSeconds = 1.0f;
}

// -------------------------------------------------------------------------------------------
// Lifecycle
// -------------------------------------------------------------------------------------------

void UInputRecordingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();

	Store = NewObject<URecordingStore>(this);
	Store->Initialize(Settings ? Settings->GetQuotaBytes() : 0);

	VideoCapture = NewObject<UInputRecordingVideoCapture>(this);
	VideoPlayer = NewObject<UInputRecordingVideoPlayer>(this);

	// The world is torn down - actors destroyed, replay component with them - before the game
	// instance shuts down. Waiting until Deinitialize to rescue an in-progress take is too late:
	// the component holding the samples is already gone, so this has to hook world teardown.
	WorldTearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &UInputRecordingSubsystem::HandleWorldBeginTearDown);

	UE_LOG(LogInputRecording, Log, TEXT("Input recording subsystem ready. Video capture supported: %s."),
		UInputRecordingVideoCapture::IsVideoCaptureSupported() ? TEXT("yes") : TEXT("no"));
}

void UInputRecordingSubsystem::Deinitialize()
{
	if (WorldTearDownHandle.IsValid())
	{
		FWorldDelegates::OnWorldBeginTearDown.Remove(WorldTearDownHandle);
		WorldTearDownHandle.Reset();
	}

	// Last-ditch net. By this point the replay component is normally already destroyed, so
	// HandleWorldBeginTearDown is what actually rescues a take; this only catches the case where
	// the subsystem somehow outlives its world without a teardown notification.
	SaveInProgressTake(TEXT("game instance shutdown"));

	if (UInputReplayComponent* Component = CachedComponent.Get())
	{
		UnbindComponentEvents(Component);
	}

	if (VideoCapture)
	{
		VideoCapture->StopCapture();
	}

	if (VideoPlayer)
	{
		VideoPlayer->CloseVideo();
	}

	Super::Deinitialize();
}

bool UInputRecordingSubsystem::IsTickable() const
{
	// Never tick the CDO, and never tick before the store exists.
	return !HasAnyFlags(RF_ClassDefaultObject) && Store != nullptr;
}

TStatId UInputRecordingSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UInputRecordingSubsystem, STATGROUP_Tickables);
}

void UInputRecordingSubsystem::Tick(float DeltaTime)
{
	const UInputReplayComponent* Component = CachedComponent.Get();

	if (Component && Component->IsMatchingInput() && VideoPlayer && VideoPlayer->IsVideoOpen())
	{
		// The clock freezes on a cue, and the picture freezes with it.
		VideoPlayer->SyncToClock(Component->GetMatchClockSeconds(), !Component->IsAwaitingMatchInput());
	}

	if (Component && Component->IsRecording())
	{
		QuotaPollAccumulator += DeltaTime;
		if (QuotaPollAccumulator >= InputRecordingSubsystemPrivate::QuotaPollIntervalSeconds)
		{
			QuotaPollAccumulator = 0.0f;
			PollQuotaWhileRecording();
		}
	}
	else
	{
		QuotaPollAccumulator = 0.0f;
	}
}

// -------------------------------------------------------------------------------------------
// Component resolution
// -------------------------------------------------------------------------------------------

APlayerController* UInputRecordingSubsystem::GetLocalPlayerController() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	return World ? World->GetFirstPlayerController() : nullptr;
}

UInputReplayComponent* UInputRecordingSubsystem::ResolveReplayComponent()
{
	if (UInputReplayComponent* Cached = CachedComponent.Get())
	{
		if (IsValid(Cached) && Cached->GetOwner() && IsValid(Cached->GetOwner()))
		{
			return Cached;
		}

		UnbindComponentEvents(Cached);
		CachedComponent.Reset();
	}

	APlayerController* Controller = GetLocalPlayerController();

	UInputReplayComponent* Found = nullptr;

	if (Controller)
	{
		Found = Controller->FindComponentByClass<UInputReplayComponent>();

		if (!Found)
		{
			if (const APawn* Pawn = Controller->GetPawn())
			{
				Found = Pawn->FindComponentByClass<UInputReplayComponent>();
			}
		}
	}

	if (!Found)
	{
		// Any actor in the world carrying one. This is the escape hatch for a project that puts
		// the component somewhere unusual on purpose.
		if (const UWorld* World = Controller ? Controller->GetWorld() : (GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr))
		{
			for (TActorIterator<AActor> It(const_cast<UWorld*>(World)); It; ++It)
			{
				if (UInputReplayComponent* Candidate = It->FindComponentByClass<UInputReplayComponent>())
				{
					Found = Candidate;
					break;
				}
			}
		}
	}

	const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();

	if (!Found && Controller && Settings && Settings->bAutoCreateReplayComponent)
	{
		Found = NewObject<UInputReplayComponent>(Controller, UInputReplayComponent::StaticClass(), TEXT("InputReplayComponent"));
		if (Found)
		{
			Found->RegisterComponent();

			// bForce: this component is ours, so project settings own it entirely. A component a
			// designer placed by hand keeps its own configuration instead.
			Settings->ApplyDefaultsTo(Found, /*bForce=*/true);

			UE_LOG(LogInputRecording, Log, TEXT("Auto-created an InputReplayComponent on %s."), *Controller->GetName());
		}
	}
	else if (Found && Settings)
	{
		Settings->ApplyDefaultsTo(Found, /*bForce=*/false);
	}

	if (Found)
	{
		CachedComponent = Found;
		BindComponentEvents(Found);
	}

	return Found;
}

void UInputRecordingSubsystem::BindComponentEvents(UInputReplayComponent* Component)
{
	if (!Component)
	{
		return;
	}

	Component->OnModeChanged.AddUniqueDynamic(this, &UInputRecordingSubsystem::HandleComponentModeChanged);
	Component->OnSampleRecorded.AddUniqueDynamic(this, &UInputRecordingSubsystem::HandleComponentSampleRecorded);
	Component->OnMatchCuePresented.AddUniqueDynamic(this, &UInputRecordingSubsystem::HandleComponentCuePresented);
	Component->OnMatchInputMatched.AddUniqueDynamic(this, &UInputRecordingSubsystem::HandleComponentCueMatched);
	Component->OnMatchInputMismatched.AddUniqueDynamic(this, &UInputRecordingSubsystem::HandleComponentCueMismatched);
	Component->OnMatchInputFinished.AddUniqueDynamic(this, &UInputRecordingSubsystem::HandleComponentMatchFinished);
}

void UInputRecordingSubsystem::UnbindComponentEvents(UInputReplayComponent* Component)
{
	if (!IsValid(Component))
	{
		return;
	}

	Component->OnModeChanged.RemoveDynamic(this, &UInputRecordingSubsystem::HandleComponentModeChanged);
	Component->OnSampleRecorded.RemoveDynamic(this, &UInputRecordingSubsystem::HandleComponentSampleRecorded);
	Component->OnMatchCuePresented.RemoveDynamic(this, &UInputRecordingSubsystem::HandleComponentCuePresented);
	Component->OnMatchInputMatched.RemoveDynamic(this, &UInputRecordingSubsystem::HandleComponentCueMatched);
	Component->OnMatchInputMismatched.RemoveDynamic(this, &UInputRecordingSubsystem::HandleComponentCueMismatched);
	Component->OnMatchInputFinished.RemoveDynamic(this, &UInputRecordingSubsystem::HandleComponentMatchFinished);
}

void UInputRecordingSubsystem::HandleWorldBeginTearDown(UWorld* World)
{
	// Ownership is tested through the world's own game instance pointer, not through
	// GetGameInstance()->GetWorld(). The latter is already null by the time teardown runs, so
	// comparing against it silently skipped every save - which is exactly the data loss this
	// hook exists to prevent.
	if (!World || World->GetGameInstance() != GetGameInstance())
	{
		return;
	}

	SaveInProgressTake(TEXT("world teardown"));
}

void UInputRecordingSubsystem::SaveInProgressTake(const TCHAR* Reason)
{
	if (!IsRecording())
	{
		return;
	}

	// A take still running when the world goes away has to be saved, not dropped. The .mp4 would
	// survive regardless - the capture pipeline finalises on teardown - so without this the
	// video outlives the input, which is exactly the wrong way round: a missing video is an
	// inconvenience, a missing ghost is a take somebody has to re-perform.
	const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();
	UE_LOG(LogInputRecording, Log, TEXT("Saving in-progress take (%s)."), Reason);

	StopRecordingAndSave(FString(), Settings && Settings->bAlsoExportJsonOnSave);
}

void UInputRecordingSubsystem::HandleComponentModeChanged(EInputReplayMode NewMode)
{
	OnModeChanged.Broadcast(NewMode);
}

void UInputRecordingSubsystem::HandleComponentSampleRecorded(FName ActionName, float TimeSeconds, FVector Value)
{
	OnSyncPointRecorded.Broadcast(ActionName, TimeSeconds, Value);
}

void UInputRecordingSubsystem::HandleComponentCuePresented(int32 CueIndex, int32 TotalCues, const FString& ExpectedDescription)
{
	OnMatchCuePresented.Broadcast(CueIndex, TotalCues, ExpectedDescription);
}

void UInputRecordingSubsystem::HandleComponentCueMatched(int32 CueIndex, int32 TotalCues)
{
	OnMatchInputMatched.Broadcast(CueIndex, TotalCues);
}

void UInputRecordingSubsystem::HandleComponentCueMismatched(const FString& ExpectedDescription, const FString& ReceivedDescription)
{
	OnMatchInputMismatched.Broadcast(ExpectedDescription, ReceivedDescription);
}

void UInputRecordingSubsystem::HandleComponentMatchFinished(bool bCompletedAllCues)
{
	if (VideoPlayer)
	{
		VideoPlayer->CloseVideo();
	}

	if (Store && ReviewedSession.IsValid())
	{
		Store->UnpinSession(ReviewedSession.Index);
	}

	OnMatchInputFinished.Broadcast(bCompletedAllCues);
}

// -------------------------------------------------------------------------------------------
// Recording
// -------------------------------------------------------------------------------------------

bool UInputRecordingSubsystem::StartRecording(const FString& DisplayName)
{
	const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();
	if (!Settings || !Store)
	{
		return false;
	}

	UInputReplayComponent* Component = ResolveReplayComponent();
	if (!Component)
	{
		UE_LOG(LogInputRecording, Error, TEXT("Cannot record: no InputReplayComponent could be resolved or created."));
		return false;
	}

	if (Component->IsRecording())
	{
		// Say what is actually going on rather than "already busy". A user who types
		// ir.record.start twice does it because the first attempt looked like it failed, so the
		// message that matters is "it did not - here is the take you already have, and here is
		// how to finish it".
		UE_LOG(LogInputRecording, Display,
			TEXT("Already recording '%s' into %s (%.1fs so far). Use ir.record.stop to finish it, ")
			TEXT("or ir.record.cancel to throw it away."),
			*ActiveSession.DisplayName, *ActiveSession.FolderName, Component->GetRecordingDurationSeconds());
		return false;
	}

	if (Component->GetMode() != EInputReplayMode::Idle)
	{
		UE_LOG(LogInputRecording, Warning,
			TEXT("Cannot record: the replay component is mid-review (mode %d). Finish or leave the review first."),
			static_cast<int32>(Component->GetMode()));
		return false;
	}

	const FString ResolvedName = DisplayName.IsEmpty() ? Settings->DefaultRecordingName : DisplayName;

	// Input recording starts FIRST. The session folder is only claimed once it has definitely
	// started, so a failed start never burns a folder index or leaves an empty directory behind.
	if (!Component->StartRecording(ResolvedName))
	{
		return false;
	}

	const UWorld* World = Component->GetWorld();
	const FString MapName = World ? World->GetMapName() : FString();

	const int32 SessionIndex = Store->BeginSession(ResolvedName, MapName, Settings->GetReserveBytesPerTake());
	if (SessionIndex == INDEX_NONE)
	{
		Component->StopRecording();
		UE_LOG(LogInputRecording, Error, TEXT("Take abandoned: the store could not reserve room for it."));
		ShowToast(NSLOCTEXT("InputRecording", "NoRoom", "Not enough room to record"),
			NSLOCTEXT("InputRecording", "NoRoomDetail", "Free space with ir.store.trim, or raise the quota."));
		return false;
	}

	Store->FindSession(SessionIndex, ActiveSession);
	bLastRecordingQuotaStopped = false;

	const bool bCapturingVideo = Settings->bCaptureVideoWithRecording && VideoCapture != nullptr;

	// Decide overlay visibility once, here, before anything is shown. Viewport capture includes
	// whatever is drawn over it, so with video on and bCaptureVideoIncludingUI off the overlay
	// cannot be on screen - but it must never be shown and then torn down, which is what made a
	// successful first take read as a failure.
	const bool bOverlayWouldPolluteCapture = bCapturingVideo && !Settings->bCaptureVideoIncludingUI;

	if (bOverlayWouldPolluteCapture)
	{
		if (IsOverlayVisible())
		{
			HideOverlay();
			bOverlayHiddenForCapture = true;
		}
	}
	else
	{
		ShowOverlay();
	}

	if (bCapturingVideo)
	{
		// Failure here is deliberately non-fatal: a .ghost with no .mp4 is a usable recording,
		// while a lost .ghost is a take somebody has to re-perform.
		VideoCapture->StartCapture(ActiveSession.GetVideoPath(), Settings->VideoOptions);
	}

	// Display, not Log: this is the confirmation the person at the console is waiting for, and it
	// is the only feedback they get when the overlay has to stay hidden for the capture.
	UE_LOG(LogInputRecording, Display,
		TEXT("Recording '%s' -> %s (%d tracked action(s)%s). ir.record.stop to save, ir.record.cancel to discard."),
		*ResolvedName, *ActiveSession.FolderName, Component->GetTrackedActionCount(),
		bOverlayWouldPolluteCapture
			? TEXT("; overlay hidden so it stays out of the video - set bCaptureVideoIncludingUI to keep it on screen")
			: TEXT(""));

	// A toast is UMG like the overlay, so it can only be raised when the overlay could be.
	if (!bOverlayWouldPolluteCapture)
	{
		ShowToast(NSLOCTEXT("InputRecording", "TakeStarted", "Recording"),
			FText::FromString(ResolvedName));
	}

	return true;
}

bool UInputRecordingSubsystem::FinishRecording(const FString& DisplayName, bool bAlsoExportJson, bool bSave, bool bDeleteFolder)
{
	UInputReplayComponent* Component = CachedComponent.Get();
	if (!Component || !Component->IsRecording())
	{
		return false;
	}

	// Video first, so the .mp4 is closed before the manifest reports its size.
	if (VideoCapture && VideoCapture->IsCapturing())
	{
		const FString VideoPath = VideoCapture->GetCurrentVideoPath();
		VideoCapture->StopCapture();
		OnVideoSaved.Broadcast(!bDeleteFolder, VideoPath);
	}

	if (bOverlayHiddenForCapture)
	{
		ShowOverlay();
		bOverlayHiddenForCapture = false;
	}

	Component->StopRecording();

	if (!bSave)
	{
		if (bDeleteFolder && Store && ActiveSession.IsValid())
		{
			Store->AbortSession(ActiveSession.Index);
			UE_LOG(LogInputRecording, Log, TEXT("Take cancelled; %s deleted."), *ActiveSession.FolderName);
		}
		else if (Store && ActiveSession.IsValid())
		{
			// Stopped without saving: the folder and the in-memory take both survive, but the
			// pin must not, or this session becomes permanently un-evictable.
			Store->UnpinSession(ActiveSession.Index);
			UE_LOG(LogInputRecording, Log, TEXT("Take stopped without saving; %s left in place."), *ActiveSession.FolderName);
		}

		ActiveSession = FRecordingSessionInfo();
		return true;
	}

	if (!ActiveSession.IsValid())
	{
		UE_LOG(LogInputRecording, Error, TEXT("Take finished but no session folder was ever claimed; nothing written."));
		OnRecordingSaved.Broadcast(false, FString(), bLastRecordingQuotaStopped);
		return false;
	}

	if (!DisplayName.IsEmpty())
	{
		ActiveSession.DisplayName = DisplayName;
	}

	const bool bSaved = Component->SaveCurrentRecording(ActiveSession.GetBasePath(), bAlsoExportJson);

	const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();
	const TArray<FMatchInputCue> Cues = UMatchInputCueBuilder::BuildMatchInputCues(
		Component->GetCurrentRecordingRef(),
		Settings ? Settings->CueBuildOptions : FMatchInputCueBuildOptions());

	if (Store)
	{
		Store->CommitSession(ActiveSession.Index, Component->GetCurrentRecordingRef().GetDurationSeconds(), Cues.Num());
		Store->FindSession(ActiveSession.Index, ActiveSession);
	}

	const FString SessionPath = ActiveSession.AbsolutePath;

	if (bLastRecordingQuotaStopped)
	{
		ShowToast(NSLOCTEXT("InputRecording", "QuotaStopped", "Recording stopped early - storage quota reached"),
			FText::FromString(FString::Printf(TEXT("%s saved with %s of footage."),
				*ActiveSession.FolderName, *UInputRecordingFormatLibrary::FormatDurationClock(ActiveSession.DurationSeconds))));
	}
	else if (bSaved)
	{
		ShowToast(NSLOCTEXT("InputRecording", "Saved", "Recording saved"),
			FText::FromString(FString::Printf(TEXT("%s  -  %s, %d cue(s)"),
				*ActiveSession.FolderName,
				*UInputRecordingFormatLibrary::FormatDurationClock(ActiveSession.DurationSeconds),
				ActiveSession.CueCount)));
	}

	OnRecordingSaved.Broadcast(bSaved, SessionPath, bLastRecordingQuotaStopped);

	ActiveSession = FRecordingSessionInfo();
	return bSaved;
}

bool UInputRecordingSubsystem::StopRecordingAndSave(const FString& DisplayName, bool bAlsoExportJson)
{
	return FinishRecording(DisplayName, bAlsoExportJson, /*bSave=*/true, /*bDeleteFolder=*/false);
}

bool UInputRecordingSubsystem::StopRecording()
{
	const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();
	return StopRecordingAndSave(FString(), Settings && Settings->bAlsoExportJsonOnSave);
}

void UInputRecordingSubsystem::StopRecordingWithoutSaving()
{
	FinishRecording(FString(), false, /*bSave=*/false, /*bDeleteFolder=*/false);
}

void UInputRecordingSubsystem::CancelRecording()
{
	FinishRecording(FString(), false, /*bSave=*/false, /*bDeleteFolder=*/true);

	ShowToast(NSLOCTEXT("InputRecording", "Cancelled", "Recording cancelled"),
		NSLOCTEXT("InputRecording", "CancelledDetail", "The session folder was deleted."));
}

void UInputRecordingSubsystem::PollQuotaWhileRecording()
{
	const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();
	if (!Settings || !Settings->bStopRecordingWhenQuotaReached || !Store || !ActiveSession.IsValid())
	{
		return;
	}

	// GetTotalBytes excludes the in-progress folder, whose cached size is still zero, so adding
	// the live measurement gives the real total without double counting.
	const int64 LiveBytes = Store->GetSessionBytesOnDisk(ActiveSession.Index);
	const int64 Projected = Store->GetTotalBytes() + LiveBytes;
	const int64 Quota = Settings->GetQuotaBytes();

	if (Projected <= Quota)
	{
		return;
	}

	// Stop the take rather than evicting something else mid-write: trading a finished recording
	// for an unfinished one is the wrong trade. The partial take is still saved, and the UI has
	// to say it stopped early rather than claiming a normal stop.
	bLastRecordingQuotaStopped = true;

	UE_LOG(LogRecordingStore, Warning,
		TEXT("Quota reached mid-take (%s of %s). Stopping the recording and saving what there is."),
		*UInputRecordingFormatLibrary::FormatByteSize(Projected),
		*UInputRecordingFormatLibrary::FormatByteSize(Quota));

	StopRecordingAndSave(FString(), Settings->bAlsoExportJsonOnSave);
}

// -------------------------------------------------------------------------------------------
// Review
// -------------------------------------------------------------------------------------------

bool UInputRecordingSubsystem::RunControlRecapTest(const FString& SessionSpecifier)
{
	if (!Store)
	{
		return false;
	}

	if (IsRecording())
	{
		StopRecording();
	}

	Store->Rescan();

	FRecordingSessionInfo Session;
	const bool bResolved = Store->ResolveSessionSpecifier(SessionSpecifier, Session);

	if (!bResolved || !Session.IsPlayable())
	{
		// Never fall back silently. Reviewing a different take than the one asked for is worse
		// than doing nothing.
		TArray<FString> Available;
		for (const FRecordingSessionInfo& Candidate : Store->GetSessions())
		{
			Available.Add(FString::Printf(TEXT("%s%s"), *Candidate.FolderName, Candidate.IsPlayable() ? TEXT("") : TEXT(" (no ghost)")));
		}

		if (SessionSpecifier.IsEmpty())
		{
			UE_LOG(LogInputRecording, Error, TEXT("No playable recording to review. Available: %s"),
				Available.Num() > 0 ? *FString::Join(Available, TEXT(", ")) : TEXT("none"));
		}
		else
		{
			UE_LOG(LogInputRecording, Error, TEXT("No playable recording matches '%s'. Available: %s"),
				*SessionSpecifier, Available.Num() > 0 ? *FString::Join(Available, TEXT(", ")) : TEXT("none"));
		}

		return false;
	}

	Store->PinSession(Session.Index);
	PendingReviewSpecifier = Session.FolderName;

	const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();
	const FSoftObjectPath MapPath = Settings ? Settings->ControlRecapMap : FSoftObjectPath();

	if (!MapPath.IsValid())
	{
		UE_LOG(LogInputRecording, Error, TEXT("Control Recap map is not set in project settings; cannot travel."));
		return false;
	}

	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}

	UE_LOG(LogInputRecording, Log, TEXT("Opening the review map for %s."), *Session.FolderName);
	UGameplayStatics::OpenLevel(World, FName(*MapPath.GetLongPackageName()));
	return true;
}

bool UInputRecordingSubsystem::StartMatchInputFromSession(const FRecordingSessionInfo& Session)
{
	UInputReplayComponent* Component = ResolveReplayComponent();
	if (!Component || !Store)
	{
		return false;
	}

	if (!Session.IsPlayable())
	{
		UE_LOG(LogMatchInput, Error, TEXT("Session %s has no ghost to review."), *Session.FolderName);
		return false;
	}

	// Touch before anything else: reviewing counts as using, and this is what protects the take
	// from being the next thing evicted.
	Store->TouchSession(Session.Index);
	Store->PinSession(Session.Index);

	FInputRecording Recording;
	if (!UInputRecordingSerializer::LoadRecording(Session.GetBasePath(), Recording))
	{
		Store->UnpinSession(Session.Index);
		return false;
	}

	if (!Component->StartMatchInput(Recording))
	{
		Store->UnpinSession(Session.Index);
		return false;
	}

	ReviewedSession = Session;

	const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();
	if (Settings && Settings->bPlayVideoDuringMatchInput && VideoPlayer && Session.bHasVideo)
	{
		VideoPlayer->OpenVideo(Session.GetVideoPath());
	}

	return true;
}

void UInputRecordingSubsystem::StopMatchInput(bool bCompleted)
{
	if (UInputReplayComponent* Component = CachedComponent.Get())
	{
		Component->StopMatchInput(bCompleted);
	}
}

// -------------------------------------------------------------------------------------------
// Widgets
// -------------------------------------------------------------------------------------------

UUserWidget* UInputRecordingSubsystem::CreateWidgetFromSettingsClass(const FSoftClassPath& Path, UClass* FallbackClass, const TCHAR* SettingName)
{
	const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();
	if (!Settings)
	{
		return nullptr;
	}

	APlayerController* Controller = GetLocalPlayerController();
	if (!Controller)
	{
		return nullptr;
	}

	UClass* WidgetClass = Settings->ResolveWidgetClass(Path, FallbackClass, SettingName);
	return WidgetClass ? CreateWidget<UUserWidget>(Controller, WidgetClass) : nullptr;
}

UInputRecorderOverlayWidget* UInputRecordingSubsystem::ShowOverlay()
{
	const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();
	if (!Settings)
	{
		return nullptr;
	}

	if (!OverlayWidget)
	{
		OverlayWidget = Cast<UInputRecorderOverlayWidget>(CreateWidgetFromSettingsClass(
			Settings->OverlayWidgetClass, UInputRecorderOverlayWidget::StaticClass(), TEXT("OverlayWidgetClass")));
	}

	if (!OverlayWidget)
	{
		return nullptr;
	}

	if (!OverlayWidget->IsInViewport())
	{
		OverlayWidget->AddToViewport();
	}

	OverlayWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// The live read-out needs a tracked-action list even before a take starts.
	if (UInputReplayComponent* Component = ResolveReplayComponent())
	{
		if (Component->GetMode() == EInputReplayMode::Idle)
		{
			Component->RefreshTrackedActions();
		}
	}

	return OverlayWidget;
}

void UInputRecordingSubsystem::HideOverlay()
{
	if (OverlayWidget)
	{
		OverlayWidget->SetVisibility(ESlateVisibility::Collapsed);
		OverlayWidget->RemoveFromParent();
	}
}

bool UInputRecordingSubsystem::IsOverlayVisible() const
{
	return OverlayWidget && OverlayWidget->IsInViewport();
}

URecordingListWidget* UInputRecordingSubsystem::ShowRecordingList()
{
	const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();
	if (!Settings)
	{
		return nullptr;
	}

	if (!RecordingListWidget)
	{
		RecordingListWidget = Cast<URecordingListWidget>(CreateWidgetFromSettingsClass(
			Settings->RecordingListWidgetClass, URecordingListWidget::StaticClass(), TEXT("RecordingListWidgetClass")));
	}

	if (!RecordingListWidget)
	{
		return nullptr;
	}

	if (!RecordingListWidget->IsInViewport())
	{
		// Above the corner overlay, since it is a modal-ish browser raised over whatever is on screen.
		RecordingListWidget->AddToViewport(100);
	}

	RecordingListWidget->SetVisibility(ESlateVisibility::Visible);
	RecordingListWidget->RefreshList();

	return RecordingListWidget;
}

void UInputRecordingSubsystem::HideRecordingList()
{
	if (RecordingListWidget)
	{
		RecordingListWidget->RemoveFromParent();
	}
}

void UInputRecordingSubsystem::ShowToast(const FText& Message, const FText& Detail)
{
	// Saving a take on the way out routes through here. Creating widgets during engine shutdown
	// is a good way to crash on the last frame, and nobody would see the toast anyway.
	if (IsEngineExitRequested())
	{
		return;
	}

	const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();
	if (!Settings)
	{
		return;
	}

	if (!ToastWidget)
	{
		ToastWidget = Cast<URecordingToastWidget>(CreateWidgetFromSettingsClass(
			Settings->RecordingToastWidgetClass, URecordingToastWidget::StaticClass(), TEXT("RecordingToastWidgetClass")));
	}

	if (!ToastWidget)
	{
		// Not worth failing over - the log line above already said what happened.
		return;
	}

	if (!ToastWidget->IsInViewport())
	{
		ToastWidget->AddToViewport(200);
	}

	ToastWidget->ShowToast(Message, Detail, 4.0f);
}

// -------------------------------------------------------------------------------------------
// State
// -------------------------------------------------------------------------------------------

EInputReplayMode UInputRecordingSubsystem::GetMode() const
{
	const UInputReplayComponent* Component = CachedComponent.Get();
	return Component ? Component->GetMode() : EInputReplayMode::Idle;
}

bool UInputRecordingSubsystem::IsAwaitingMatchInput() const
{
	const UInputReplayComponent* Component = CachedComponent.Get();
	return Component && Component->IsAwaitingMatchInput();
}

FString UInputRecordingSubsystem::GetExpectedInputDescription() const
{
	const UInputReplayComponent* Component = CachedComponent.Get();
	return Component ? Component->GetExpectedInputDescription() : FString();
}

float UInputRecordingSubsystem::GetMatchProgress() const
{
	const int32 Total = GetMatchCueCount();
	return Total > 0 ? FMath::Clamp(static_cast<float>(GetCurrentMatchCueIndex()) / static_cast<float>(Total), 0.0f, 1.0f) : 0.0f;
}

int32 UInputRecordingSubsystem::GetMatchCueCount() const
{
	const UInputReplayComponent* Component = CachedComponent.Get();
	return Component ? Component->GetMatchCuesRef().Num() : 0;
}

int32 UInputRecordingSubsystem::GetCurrentMatchCueIndex() const
{
	const UInputReplayComponent* Component = CachedComponent.Get();
	return Component ? Component->GetCurrentMatchCueIndex() : INDEX_NONE;
}

float UInputRecordingSubsystem::GetMatchClockSeconds() const
{
	const UInputReplayComponent* Component = CachedComponent.Get();
	return Component ? Component->GetMatchClockSeconds() : 0.0f;
}

TArray<FMatchInputCue> UInputRecordingSubsystem::GetMatchCues() const
{
	const UInputReplayComponent* Component = CachedComponent.Get();
	return Component ? Component->GetMatchCuesRef() : TArray<FMatchInputCue>();
}

int32 UInputRecordingSubsystem::GetMismatchCount() const
{
	const UInputReplayComponent* Component = CachedComponent.Get();
	return Component ? Component->GetMismatchCount() : 0;
}

FString UInputRecordingSubsystem::GetLastMismatchDescription() const
{
	const UInputReplayComponent* Component = CachedComponent.Get();
	return Component ? Component->GetLastMismatchDescription() : FString();
}

float UInputRecordingSubsystem::GetRecordingDurationSeconds() const
{
	const UInputReplayComponent* Component = CachedComponent.Get();
	return Component ? Component->GetRecordingDurationSeconds() : 0.0f;
}

float UInputRecordingSubsystem::GetReviewedRecordingDurationSeconds() const
{
	const UInputReplayComponent* Component = CachedComponent.Get();
	return Component ? Component->GetCurrentRecordingRef().GetDurationSeconds() : 0.0f;
}

bool UInputRecordingSubsystem::GetLiveInputSnapshot(FString& OutActionName, FVector& OutValue) const
{
	const UInputReplayComponent* Component = CachedComponent.Get();
	return Component && Component->GetLiveInputSnapshot(OutActionName, OutValue);
}

FString UInputRecordingSubsystem::GetStatusText() const
{
	switch (GetMode())
	{
	case EInputReplayMode::Recording:
		return FString::Printf(TEXT("Recording  %s"),
			*UInputRecordingFormatLibrary::FormatDurationClock(GetRecordingDurationSeconds()));

	case EInputReplayMode::MatchingInput:
		return FString::Printf(TEXT("Reviewing  cue %d / %d"), GetCurrentMatchCueIndex() + 1, GetMatchCueCount());

	case EInputReplayMode::PlayingGhost:
		return TEXT("Playing ghost");

	default:
		break;
	}

	if (Store)
	{
		const FRecordingStoreStats Stats = Store->GetStats();
		return FString::Printf(TEXT("Idle  -  %d take(s), %s free"),
			Stats.SessionCount, *UInputRecordingFormatLibrary::FormatByteSize(Stats.GetFreeBytes()));
	}

	return TEXT("Idle");
}
