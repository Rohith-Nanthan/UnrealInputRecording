// Copyright (c) Your Studio. All Rights Reserved.

#include "InputRecordingSubsystem.h"

#include "EngineUtils.h"                            // TActorIterator
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputRecordingSettings.h"
#include "InputReplay/InputMatchCue.h"              // LogInputMatch
#include "InputReplay/InputRecordingAssetTools.h"
#include "InputReplay/InputRecordingDataAsset.h"
#include "InputReplay/InputReplaySerializer.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "Storage/RecordingStore.h"
#include "UI/RecordingControllerWidget.h"
#include "UI/RecordingToastWidget.h"
#include "Video/InputRecordingScreenRecorder.h"
#include "Video/InputRecordingVideoPlayer.h"
#include "Video/InputRecordingVideoTypes.h"

// ---------------------------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------------------------

void UInputRecordingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();
	if (Settings)
	{
		ActiveRecordingName = Settings->DefaultRecordingName;
		bCaptureVideoWithRecording = Settings->bCaptureVideoWithRecording;
		bPlayVideoDuringMatchInput = Settings->bPlayVideoDuringMatchInput;
	}

	// The store scans, migrates the old flat layout, and trims to quota during Initialize, so by the
	// time anything can ask for a recording the folder is in a known state. Doing this lazily on first
	// record would put a directory walk and possibly several deletes in the middle of a take.
	SessionStore = NewObject<URecordingStore>(this, TEXT("RecordingStore"));
	SessionStore->Initialize(Settings ? Settings->GetQuotaBytes() : 900 * ::RecordingStore::BytesPerMegabyte);

	// The video playhead has to be corrected every frame, and a GameInstanceSubsystem does not tick.
	// Registering here rather than making the widget responsible means sync is correct even with no UI
	// on screen - a console-driven MatchInput session still gets a synchronised video.
	VideoSyncTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UInputRecordingSubsystem::TickVideoSync));

	// No component resolution here on purpose: a GameInstance subsystem initialises before there is
	// a world or a local player. Resolution is lazy, on first use.
	UE_LOG(LogInputReplay, Log, TEXT("InputRecordingSubsystem ready (default recording '%s', video %s)."),
		*ActiveRecordingName,
		UInputRecordingVideoLibrary::IsVideoCaptureSupported() ? TEXT("supported") : TEXT("unavailable on this platform"));
}

void UInputRecordingSubsystem::Deinitialize()
{
	if (VideoSyncTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(VideoSyncTickerHandle);
		VideoSyncTickerHandle.Reset();
	}

	// Stopping capture before teardown matters more than it looks: an unfinalised .mp4 has no index and
	// will not open, so a capture abandoned at shutdown would leave an unplayable file on disk.
	if (ScreenRecorder)
	{
		ScreenRecorder->StopCapture();
		ScreenRecorder = nullptr;
	}

	if (VideoPlayer)
	{
		VideoPlayer->Close();
		VideoPlayer = nullptr;
	}

	if (UInputReplayComponent* Component = CachedReplayComponent.Get())
	{
		UnbindFromComponent(Component);
	}
	CachedReplayComponent.Reset();

	Super::Deinitialize();
}

// ---------------------------------------------------------------------------------------------
// Video helpers
// ---------------------------------------------------------------------------------------------

UInputRecordingScreenRecorder* UInputRecordingSubsystem::GetScreenRecorder()
{
	if (!ScreenRecorder)
	{
		ScreenRecorder = NewObject<UInputRecordingScreenRecorder>(this);

		if (const UInputRecordingSettings* Settings = UInputRecordingSettings::Get())
		{
			ScreenRecorder->Options = Settings->VideoOptions;
			ScreenRecorder->bCaptureIncludingUI = Settings->bCaptureVideoIncludingUI;
		}
	}

	return ScreenRecorder;
}

UInputRecordingVideoPlayer* UInputRecordingSubsystem::GetVideoPlayer()
{
	if (!VideoPlayer)
	{
		VideoPlayer = NewObject<UInputRecordingVideoPlayer>(this);
	}

	return VideoPlayer;
}

bool UInputRecordingSubsystem::HasVideoForRecording(const FString& FileName) const
{
	return UInputRecordingVideoLibrary::DoesVideoExist(ResolveRecordingName(FileName));
}

bool UInputRecordingSubsystem::TickVideoSync(float DeltaSeconds)
{
	if (VideoPlayer && VideoPlayer->IsVideoReady())
	{
		if (const UInputReplayComponent* Component = FindReplayComponent())
		{
			if (Component->IsMatchingInput())
			{
				VideoPlayer->SyncToMatchClock(
					Component->GetMatchClockSeconds(), Component->IsAwaitingMatchInput());
			}
		}
	}

	TickQuotaGuard(DeltaSeconds);

	// Keep ticking for the subsystem's lifetime; Deinitialize removes the handle.
	return true;
}

void UInputRecordingSubsystem::TickQuotaGuard(float DeltaSeconds)
{
	if (!SessionStore || !ActiveSession.IsValid() || !IsRecording())
	{
		return;
	}

	const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();
	if (Settings && !Settings->bStopRecordingWhenQuotaReached)
	{
		return;
	}

	QuotaPollAccumulator += DeltaSeconds;
	if (QuotaPollAccumulator < 1.f)
	{
		return;
	}
	QuotaPollAccumulator = 0.f;

	// Only the growing file needs measuring - every other session's size was settled at scan time, and
	// stat-ing the whole tree once a second during a take would be wasteful for no extra information.
	const int64 LiveBytes = ScreenRecorder
		? FMath::Max<int64>(0, IFileManager::Get().FileSize(*ScreenRecorder->GetOutputPath()))
		: 0;

	const int64 CommittedBytes = SessionStore->GetTotalBytes() - ActiveSession.TotalBytes;

	if (CommittedBytes + LiveBytes < SessionStore->GetQuotaBytes())
	{
		return;
	}

	// Hard stop rather than evicting to make room. Deleting another session while this one is still
	// being written trades a finished recording for an unfinished one, and on console that is a
	// trade you can lose twice - the eviction is not instant and the encoder does not pause for it.
	UE_LOG(LogRecordingStore, Warning,
		TEXT("Quota reached mid-take (%s of %s). Stopping the recording now; what has been captured ")
		TEXT("so far is still saved."),
		*::RecordingStore::FormatBytes(CommittedBytes + LiveBytes),
		*::RecordingStore::FormatBytes(SessionStore->GetQuotaBytes()));

	bActiveTakeStoppedByQuota = true;
	StopRecording();
}

// ---------------------------------------------------------------------------------------------
// UI ownership
// ---------------------------------------------------------------------------------------------

void UInputRecordingSubsystem::ShowRecordingController()
{
	if (ControllerWidget)
	{
		if (!ControllerWidget->IsInViewport())
		{
			ControllerWidget->AddToViewport(RecordingControllerZOrder);
		}
		ControllerWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		return;
	}

	APlayerController* PlayerController = GetGameInstance()
		? GetGameInstance()->GetFirstLocalPlayerController()
		: nullptr;

	if (!PlayerController)
	{
		// Called before there is a local player - during Initialize, or between travels. The next
		// StartRecording will raise it.
		return;
	}

	// A Blueprint subclass is the styling hook; falling back to the C++ class means the panel always
	// appears even in a project that never made one.
	// The class comes from project settings: URecordingControllerWidget builds no tree of its own, so
	// the Blueprint *is* the layout and there is nothing meaningful to fall back to when it is unset.
	// LoadWidgetClass logs loudly in that case rather than leaving a blank corner of the screen.
	const UInputRecordingSettings* ControllerSettings = UInputRecordingSettings::Get();
	UClass* WidgetClass = UInputRecordingSettings::LoadWidgetClass(
		ControllerSettings ? ControllerSettings->RecordingControllerWidgetClass : FSoftClassPath(),
		URecordingControllerWidget::StaticClass(),
		TEXT("Recording Controller"));

	ControllerWidget = CreateWidget<URecordingControllerWidget>(PlayerController, WidgetClass);
	if (ControllerWidget)
	{
		ControllerWidget->AddToViewport(RecordingControllerZOrder);
	}
}

void UInputRecordingSubsystem::HideRecordingController()
{
	if (ControllerWidget && ControllerWidget->IsInViewport())
	{
		ControllerWidget->RemoveFromParent();
	}
}

bool UInputRecordingSubsystem::IsRecordingControllerVisible() const
{
	return ControllerWidget && ControllerWidget->IsInViewport();
}

void UInputRecordingSubsystem::ShowToast(const FString& Message, float DurationSeconds)
{
	UE_LOG(LogInputReplay, Log, TEXT("%s"), *Message);

	APlayerController* PlayerController = GetGameInstance()
		? GetGameInstance()->GetFirstLocalPlayerController()
		: nullptr;

	if (!PlayerController)
	{
		return;
	}

	if (!ToastWidget)
	{
		// The toast is the one widget that still builds a usable default tree in C++, because a save
		// confirmation that fails to appear is worse than an unstyled one.
		const UInputRecordingSettings* ToastSettings = UInputRecordingSettings::Get();
		UClass* WidgetClass = (ToastSettings && ToastSettings->ToastWidgetClass.IsValid())
			? UInputRecordingSettings::LoadWidgetClass(
				ToastSettings->ToastWidgetClass, URecordingToastWidget::StaticClass(), TEXT("Recording Toast"))
			: URecordingToastWidget::StaticClass();

		ToastWidget = CreateWidget<URecordingToastWidget>(PlayerController, WidgetClass);
	}

	if (ToastWidget)
	{
		ToastWidget->ShowMessage(Message, DurationSeconds);
	}
}

// ---------------------------------------------------------------------------------------------
// Component discovery
// ---------------------------------------------------------------------------------------------

UInputReplayComponent* UInputRecordingSubsystem::FindReplayComponent() const
{
	// 1. The one we already know about, if its owner is still alive.
	if (UInputReplayComponent* Cached = CachedReplayComponent.Get())
	{
		const AActor* Owner = Cached->GetOwner();
		if (IsValid(Owner) && !Owner->IsActorBeingDestroyed())
		{
			return Cached;
		}
	}

	const UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		return nullptr;
	}

	// 2. The PlayerController - the correct home for the component, because that is where the
	//    PreProcessInput / PostProcessInput hooks live (see AReplayPlayerController).
	APlayerController* PlayerController = GameInstance->GetFirstLocalPlayerController();
	if (PlayerController)
	{
		if (UInputReplayComponent* OnController = PlayerController->FindComponentByClass<UInputReplayComponent>())
		{
			return OnController;
		}

		// 3. The pawn - supported because the component's GetOwningPlayerController() walks up from
		//    a pawn owner, though input hooks will fall back to TickComponent.
		if (APawn* Pawn = PlayerController->GetPawn())
		{
			if (UInputReplayComponent* OnPawn = Pawn->FindComponentByClass<UInputReplayComponent>())
			{
				return OnPawn;
			}
		}
	}

	// 4. Anywhere in the world. Covers a dedicated "replay manager" actor placed in the level.
	if (UWorld* World = GameInstance->GetWorld())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (UInputReplayComponent* Found = It->FindComponentByClass<UInputReplayComponent>())
			{
				return Found;
			}
		}
	}

	return nullptr;
}

UInputReplayComponent* UInputRecordingSubsystem::GetReplayComponent()
{
	UInputReplayComponent* Component = FindReplayComponent();

	// 5. Nothing out there - make one, if the project allows it.
	if (!Component)
	{
		const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();
		const bool bMayCreate = !Settings || Settings->bAutoCreateReplayComponent;

		if (bMayCreate)
		{
			if (const UGameInstance* GameInstance = GetGameInstance())
			{
				Component = CreateReplayComponentOn(GameInstance->GetFirstLocalPlayerController());
			}
		}
	}

	if (Component && Component != CachedReplayComponent.Get())
	{
		if (UInputReplayComponent* Previous = CachedReplayComponent.Get())
		{
			UnbindFromComponent(Previous);
		}

		CachedReplayComponent = Component;
		BindToComponent(Component);

		// Project defaults must not stomp a component a designer configured on the controller, so
		// non-created components only get the fields they left empty.
		if (const UInputRecordingSettings* Settings = UInputRecordingSettings::Get())
		{
			Settings->ApplyDefaultsTo(Component, /*bForce=*/false);
		}
	}

	if (!Component)
	{
		UE_LOG(LogInputReplay, Error,
			TEXT("No UInputReplayComponent found. Use AReplayPlayerController (or reparent your ")
			TEXT("PlayerController Blueprint to it), add the component to your pawn, or enable ")
			TEXT("'Auto Create Replay Component' in Project Settings > Game > Input Recording."));
	}

	return Component;
}

UInputReplayComponent* UInputRecordingSubsystem::CreateReplayComponentOn(APlayerController* PlayerController)
{
	if (!PlayerController)
	{
		return nullptr;
	}

	UInputReplayComponent* Component = NewObject<UInputReplayComponent>(
		PlayerController, UInputReplayComponent::StaticClass(), TEXT("InputReplayComponent_Runtime"));

	if (!Component)
	{
		return nullptr;
	}

	PlayerController->AddInstanceComponent(Component);
	Component->RegisterComponent();

	// A code-created component has no designer-authored setup, so the project settings are the only
	// place its recorded contexts can come from - force them on.
	if (const UInputRecordingSettings* Settings = UInputRecordingSettings::Get())
	{
		Settings->ApplyDefaultsTo(Component, /*bForce=*/true);
	}

	if (!bWarnedAboutAutoCreatedComponent)
	{
		bWarnedAboutAutoCreatedComponent = true;

		// Worth stating plainly: without the controller hooks the component runs from TickComponent,
		// which is fine for MatchInput (it only reads live input) but adds a frame of latency to
		// ghost playback.
		UE_LOG(LogInputReplay, Warning,
			TEXT("Created a UInputReplayComponent on '%s' at runtime. It will run from TickComponent ")
			TEXT("because this controller does not forward PreProcessInput/PostProcessInput. ")
			TEXT("MatchInput and recording work; for frame-accurate ghost playback, use ")
			TEXT("AReplayPlayerController instead."),
			*PlayerController->GetName());
	}

	UE_LOG(LogInputReplay, Log, TEXT("Auto-created UInputReplayComponent on '%s'."), *PlayerController->GetName());
	return Component;
}

void UInputRecordingSubsystem::SetReplayComponent(UInputReplayComponent* Component)
{
	if (Component == CachedReplayComponent.Get())
	{
		return;
	}

	if (UInputReplayComponent* Previous = CachedReplayComponent.Get())
	{
		UnbindFromComponent(Previous);
	}

	CachedReplayComponent = Component;

	if (Component)
	{
		BindToComponent(Component);
	}
}

void UInputRecordingSubsystem::BindToComponent(UInputReplayComponent* Component)
{
	if (!Component)
	{
		return;
	}

	// AddUniqueDynamic rather than AddDynamic: re-resolving the same component (a respawn that keeps
	// the controller, for instance) must not double up the relays.
	Component->OnRecordingStarted.AddUniqueDynamic(this, &UInputRecordingSubsystem::HandleRecordingStarted);
	Component->OnRecordingStopped.AddUniqueDynamic(this, &UInputRecordingSubsystem::HandleRecordingStopped);
	Component->OnPlaybackStarted.AddUniqueDynamic(this, &UInputRecordingSubsystem::HandlePlaybackStarted);
	Component->OnPlaybackFinished.AddUniqueDynamic(this, &UInputRecordingSubsystem::HandlePlaybackFinished);
	Component->OnMatchInputStarted.AddUniqueDynamic(this, &UInputRecordingSubsystem::HandleMatchInputStarted);
	Component->OnMatchInputCuePresented.AddUniqueDynamic(this, &UInputRecordingSubsystem::HandleMatchCuePresented);
	Component->OnMatchInputMatched.AddUniqueDynamic(this, &UInputRecordingSubsystem::HandleMatchInputMatched);
	Component->OnMatchInputMismatch.AddUniqueDynamic(this, &UInputRecordingSubsystem::HandleMatchInputMismatch);
	Component->OnMatchInputFinished.AddUniqueDynamic(this, &UInputRecordingSubsystem::HandleMatchInputFinished);
	Component->OnInputSyncPointRecorded.AddUniqueDynamic(this, &UInputRecordingSubsystem::HandleInputSyncPointRecorded);
}

void UInputRecordingSubsystem::UnbindFromComponent(UInputReplayComponent* Component)
{
	if (!Component)
	{
		return;
	}

	Component->OnRecordingStarted.RemoveAll(this);
	Component->OnRecordingStopped.RemoveAll(this);
	Component->OnPlaybackStarted.RemoveAll(this);
	Component->OnPlaybackFinished.RemoveAll(this);
	Component->OnMatchInputStarted.RemoveAll(this);
	Component->OnMatchInputCuePresented.RemoveAll(this);
	Component->OnMatchInputMatched.RemoveAll(this);
	Component->OnMatchInputMismatch.RemoveAll(this);
	Component->OnMatchInputFinished.RemoveAll(this);
	Component->OnInputSyncPointRecorded.RemoveAll(this);
}

// ---------------------------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------------------------

FString UInputRecordingSubsystem::ResolveRecordingName(const FString& Requested) const
{
	if (!Requested.IsEmpty())
	{
		return Requested;
	}
	if (!ActiveRecordingName.IsEmpty())
	{
		return ActiveRecordingName;
	}

	const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();
	const FString Fallback = Settings ? Settings->DefaultRecordingName : FString();
	return Fallback.IsEmpty() ? TEXT("Recording01") : Fallback;
}

bool UInputRecordingSubsystem::StartRecording(const FString& DisplayName)
{
	UInputReplayComponent* Component = GetReplayComponent();
	if (!Component)
	{
		return false;
	}

	if (Component->GetMode() != EInputReplayMode::Idle)
	{
		UE_LOG(LogInputReplay, Warning, TEXT("StartRecording: already busy (mode=%d) - stopping first."),
			static_cast<int32>(Component->GetMode()));
		StopAll();
	}

	Component->StartRecording(DisplayName);

	const bool bStarted = Component->IsRecording();
	if (!bStarted)
	{
		// StartRecording only refuses for one reason: an empty action registry.
		UE_LOG(LogInputReplay, Error,
			TEXT("StartRecording failed - the component has no tracked actions. Assign Recorded ")
			TEXT("Contexts on the component, or set them in Project Settings > Game > Input Recording."));
	}

	if (!bStarted)
	{
		BroadcastModeChanged();
		return false;
	}

	// Claim the session only once input recording is definitely running. Doing it earlier would burn
	// an index and create a folder for every failed start, and those folders would count against the
	// quota until someone noticed them.
	const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();
	const int64 Reservation = Settings ? Settings->GetReserveBytesPerTake() : 150 * ::RecordingStore::BytesPerMegabyte;

	const FString MapName = GetWorld() ? GetWorld()->GetMapName() : FString();
	ActiveSession = SessionStore->BeginSession(DisplayName, MapName, Reservation);

	if (!ActiveSession.IsValid())
	{
		// The store already logged why. Unwind the component so we are not left recording into nothing.
		UE_LOG(LogInputReplay, Error, TEXT("Could not open a session folder; abandoning the take."));
		Component->StopRecording();
		BroadcastModeChanged();
		return false;
	}

	bActiveTakeStoppedByQuota = false;
	QuotaPollAccumulator = 0.f;

	// Video starts only once input recording is definitely running, so a failed take never leaves an
	// orphan .mp4 behind. The reverse ordering is deliberately *not* used: a video with no .ghost is
	// useless to this system, whereas a .ghost with no video degrades gracefully everywhere.
	//
	// Capture writes straight to the session's final path - there is no rename step any more, because
	// the folder is claimed before the first frame is encoded.
	if (bCaptureVideoWithRecording)
	{
		GetScreenRecorder()->StartCapture(this, ActiveSession.GetVideoPath());
	}

	ShowRecordingController();

	BroadcastModeChanged();
	return true;
}

bool UInputRecordingSubsystem::StopRecording()
{
	return StopRecordingAndSave(ActiveRecordingName, bUseJsonFormat);
}

bool UInputRecordingSubsystem::StopRecordingAndSave(const FString& FileName, bool bAsJson)
{
	UInputReplayComponent* Component = GetReplayComponent();
	if (!Component)
	{
		return false;
	}

	if (!Component->IsRecording())
	{
		UE_LOG(LogInputReplay, Warning, TEXT("StopRecordingAndSave: not recording."));
		return false;
	}

	Component->StopRecording();

	const FString Name = ResolveRecordingName(FileName);
	ActiveRecordingName = Name;

	// Stop the video first. StopCapture blocks until the encoder drains and writes the MP4 index, so by
	// the time the .ghost lands below, both halves of the pair are complete on disk.
	if (ScreenRecorder && ScreenRecorder->IsCapturing())
	{
		ScreenRecorder->StopCapture();

		const bool bVideoOk = (ScreenRecorder->GetState() != EInputRecordingVideoState::Failed)
			&& IFileManager::Get().FileExists(*ScreenRecorder->GetOutputPath());

		OnVideoSaved.Broadcast(bVideoOk, ScreenRecorder->GetOutputPath());
	}

	// The session base path, not a bare name: both the serializer and the video path helper pass an
	// absolute path through untouched and append their own extension, so one base produces all three
	// files inside the session folder with no string surgery here.
	const bool bHasSession = ActiveSession.IsValid();
	const FString SaveTarget = bHasSession ? ActiveSession.GetBasePath() : Name;

	const bool bSaved = Component->SaveRecordingToFile(SaveTarget, bAsJson);

	// Writing the readable companion copy by default is what makes the Data Asset workflow painless:
	// the binary stays authoritative for playback, the JSON is there to read and to import.
	const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();
	if (bSaved && !bAsJson && Settings && Settings->bAlsoExportJsonOnSave)
	{
		Component->SaveRecordingToFile(SaveTarget, /*bAsJson=*/true);
	}

	FString SessionPath;

	if (bHasSession)
	{
		SessionPath = ActiveSession.AbsolutePath;

		if (bSaved)
		{
			// Cues are extracted here rather than read off the component: MatchCues is only populated
			// while MatchInput is running, so straight after a recording it is empty. The manifest is
			// the one place that number is worth paying for, since the recap UI shows it before it has
			// loaded the .ghost.
			TArray<FMatchInputCue> Cues;
			UInputMatchLibrary::BuildMatchInputCues(
				Component->GetRecording(), Component->MatchCueOptions, Cues);

			SessionStore->CommitSession(
				ActiveSession.Index,
				Component->GetRecording().GetDurationSeconds(),
				Cues.Num());

			LastSavedSessionPath = SessionPath;
		}
		else
		{
			// Nothing usable was written, so the folder is just quota being held hostage.
			UE_LOG(LogInputReplay, Error, TEXT("Saving failed; discarding session %s."), *ActiveSession.FolderName);
			SessionStore->AbortSession(ActiveSession.Index);
		}

		ActiveSession = FRecordingSessionInfo();
	}

	if (bSaved)
	{
		UE_LOG(LogInputReplay, Log, TEXT("Recording saved to %s"), *SessionPath);
	}

	HideRecordingController();

	OnRecordingSaved.Broadcast(bSaved, SessionPath, bActiveTakeStoppedByQuota);

	if (bSaved)
	{
		ShowToast(bActiveTakeStoppedByQuota
			? FString::Printf(TEXT("Storage full - recording stopped early and saved to %s"), *SessionPath)
			: FString::Printf(TEXT("Recording successful, saved to %s"), *SessionPath));
	}
	else
	{
		ShowToast(TEXT("Recording failed - nothing was saved. See the log for details."));
	}

	bActiveTakeStoppedByQuota = false;

	BroadcastModeChanged();
	return bSaved;
}

void UInputRecordingSubsystem::StopRecordingWithoutSaving()
{
	if (UInputReplayComponent* Component = FindReplayComponent())
	{
		Component->StopRecording();

		// The .ghost is being thrown away, so the .mp4 has to go too - leaving it would pair a stale
		// video with whatever recording is saved under this name next.
		if (ScreenRecorder && ScreenRecorder->IsCapturing())
		{
			const FString AbandonedPath = ScreenRecorder->GetOutputPath();
			ScreenRecorder->StopCapture();
			IFileManager::Get().Delete(*AbandonedPath, /*RequireExists=*/false, /*EvenReadOnly=*/true);
		}

		BroadcastModeChanged();
	}
}

void UInputRecordingSubsystem::CancelRecording()
{
	if (UInputReplayComponent* Component = FindReplayComponent())
	{
		Component->StopRecording();
	}

	if (ScreenRecorder && ScreenRecorder->IsCapturing())
	{
		ScreenRecorder->StopCapture();
	}

	// AbortSession deletes the whole folder, so there is no need to unlink the .mp4 individually -
	// and doing so first would only race the encoder's final write.
	if (ActiveSession.IsValid() && SessionStore)
	{
		SessionStore->AbortSession(ActiveSession.Index);
		ActiveSession = FRecordingSessionInfo();
	}

	bActiveTakeStoppedByQuota = false;

	HideRecordingController();
	ShowToast(TEXT("Recording cancelled - nothing was kept."));

	BroadcastModeChanged();
}

void UInputRecordingSubsystem::RunControlRecapTest()
{
	// Stopping saves and commits, which is what makes the take reachable from the recap map. A test
	// that discarded the recording it was about to review would be worse than useless.
	if (IsRecording())
	{
		StopRecording();
	}

	HideRecordingController();

	const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();
	if (!Settings || Settings->ControlRecapMap.IsNull())
	{
		UE_LOG(LogInputReplay, Error,
			TEXT("Test needs a Control Recap Map in Project Settings > Game > Input Recording."));
		ShowToast(TEXT("No control recap map is configured."));
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UE_LOG(LogInputReplay, Log, TEXT("Opening the control recap map '%s'."),
		*Settings->ControlRecapMap.ToString());

	// The recap map picks its own session up on load - see AControlRecapPlayerController. Passing it
	// through here as an option would only duplicate that logic and disagree with the boot flag path.
	UGameplayStatics::OpenLevelBySoftObjectPtr(World, TSoftObjectPtr<UWorld>(Settings->ControlRecapMap));
}

void UInputRecordingSubsystem::RequestVideoFrameDump()
{
	GetScreenRecorder()->RequestFrameDump();
}

// ---------------------------------------------------------------------------------------------
// MatchInput
// ---------------------------------------------------------------------------------------------

bool UInputRecordingSubsystem::StartMatchInputFromSession(const FRecordingSessionInfo& Session)
{
	if (!Session.IsPlayable())
	{
		UE_LOG(LogInputMatch, Error,
			TEXT("Session '%s' has no .ghost and cannot be replayed."), *Session.FolderName);
		return false;
	}

	// Touch before starting, not after: a session that is being reviewed should already be protected
	// from eviction by the time anything else can ask the store for room.
	if (SessionStore)
	{
		SessionStore->TouchSession(Session.Index);
	}

	ActiveRecordingName = Session.FolderName;

	// The absolute base path resolves to the .ghost and the .mp4 in one go; StartMatchInputMode passes
	// absolute paths through to the serializer untouched.
	return StartMatchInputMode(Session.GetBasePath(), /*bJson=*/false);
}

bool UInputRecordingSubsystem::StartMatchInputMode(const FString& FileName, bool bJson)
{
	UInputReplayComponent* Component = GetReplayComponent();
	if (!Component)
	{
		return false;
	}

	if (Component->GetMode() != EInputReplayMode::Idle)
	{
		StopAll();
	}

	const FString Name = ResolveRecordingName(FileName);
	const bool bPreferJson = bJson || bUseJsonFormat;

	if (!Component->LoadRecordingFromFile(Name, bPreferJson))
	{
		// Saving writes both formats by default, so try the other one before giving up rather than
		// reporting "not found" for a file that is sitting right there in the other format.
		if (!Component->LoadRecordingFromFile(Name, !bPreferJson))
		{
			UE_LOG(LogInputMatch, Error,
				TEXT("StartMatchInputMode: could not load recording '%s' from '%s'. ")
				TEXT("Record and save one first (console: InputReplay.ListRecordings)."),
				*Name, *UInputReplaySerializer::GetRecordingDirectory());
			return false;
		}
	}

	ActiveRecordingName = Name;
	MismatchCount = 0;
	LastMismatchDescription.Reset();

	// Opened before the session starts, not after: opening is asynchronous, and giving the media player
	// a head start means the first frame is usually decoded by the time the first cue is due.
	if (bPlayVideoDuringMatchInput)
	{
		GetVideoPlayer()->OpenRecordingVideo(Name);
	}

	const bool bStarted = Component->StartMatchInput();
	BroadcastModeChanged();
	return bStarted;
}

bool UInputRecordingSubsystem::StartMatchInputModeFromAsset(UInputRecordingDataAsset* RecordingAsset)
{
	if (!RecordingAsset || !RecordingAsset->HasValidRecording())
	{
		UE_LOG(LogInputMatch, Error,
			TEXT("StartMatchInputModeFromAsset: asset is null or has never been imported."));
		return false;
	}

	UInputReplayComponent* Component = GetReplayComponent();
	if (!Component)
	{
		return false;
	}

	if (Component->GetMode() != EInputReplayMode::Idle)
	{
		StopAll();
	}

	Component->SetRecording(RecordingAsset->BuildRecording());

	// Adopt the asset's cue tuning so the sequence the player is asked for is exactly the one
	// previewed in the asset's details panel.
	Component->MatchCueOptions = RecordingAsset->CueOptions;

	ActiveRecordingName = RecordingAsset->SourceFileName;
	MismatchCount = 0;
	LastMismatchDescription.Reset();

	// The asset caches the recording, but the .mp4 still lives on disk next to the .ghost that asset was
	// imported from - SourceFileName is what pairs them.
	if (bPlayVideoDuringMatchInput && !ActiveRecordingName.IsEmpty())
	{
		GetVideoPlayer()->OpenRecordingVideo(ActiveRecordingName);
	}

	const bool bStarted = Component->StartMatchInput();
	BroadcastModeChanged();
	return bStarted;
}

void UInputRecordingSubsystem::StopMatchInputMode()
{
	if (UInputReplayComponent* Component = FindReplayComponent())
	{
		Component->StopMatchInput();
		BroadcastModeChanged();
	}
}

// ---------------------------------------------------------------------------------------------
// Ghost playback
// ---------------------------------------------------------------------------------------------

bool UInputRecordingSubsystem::StartPlayback(const FString& FileName, bool bJson)
{
	UInputReplayComponent* Component = GetReplayComponent();
	if (!Component)
	{
		return false;
	}

	if (Component->GetMode() != EInputReplayMode::Idle)
	{
		StopAll();
	}

	const FString Name = ResolveRecordingName(FileName);
	const bool bPreferJson = bJson || bUseJsonFormat;

	if (!Component->LoadRecordingFromFile(Name, bPreferJson) &&
		!Component->LoadRecordingFromFile(Name, !bPreferJson))
	{
		UE_LOG(LogInputReplay, Error, TEXT("StartPlayback: could not load recording '%s'."), *Name);
		return false;
	}

	ActiveRecordingName = Name;

	const bool bStarted = Component->StartPlayback();
	BroadcastModeChanged();
	return bStarted;
}

void UInputRecordingSubsystem::StopPlayback()
{
	if (UInputReplayComponent* Component = FindReplayComponent())
	{
		Component->StopPlayback();
		BroadcastModeChanged();
	}
}

void UInputRecordingSubsystem::StopAll()
{
	UInputReplayComponent* Component = FindReplayComponent();
	if (!Component)
	{
		return;
	}

	switch (Component->GetMode())
	{
	case EInputReplayMode::Recording:
		// Stop *and save*: a recording the user has to re-perform because a button stopped it
		// without writing anything is the worst possible outcome here.
		StopRecordingAndSave(ActiveRecordingName, bUseJsonFormat);
		break;

	case EInputReplayMode::Playing:
		Component->StopPlayback();
		break;

	case EInputReplayMode::MatchInput:
		Component->StopMatchInput();
		if (VideoPlayer)
		{
			VideoPlayer->Close();
		}
		break;

	default:
		break;
	}

	BroadcastModeChanged();
}

// ---------------------------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------------------------

EInputReplayMode UInputRecordingSubsystem::GetMode() const
{
	const UInputReplayComponent* Component = FindReplayComponent();
	return Component ? Component->GetMode() : EInputReplayMode::Idle;
}

bool UInputRecordingSubsystem::IsAwaitingMatchInput() const
{
	const UInputReplayComponent* Component = FindReplayComponent();
	return Component && Component->IsAwaitingMatchInput();
}

FString UInputRecordingSubsystem::GetExpectedInputDescription() const
{
	const UInputReplayComponent* Component = FindReplayComponent();
	return Component ? Component->GetExpectedInputDescription() : FString();
}

float UInputRecordingSubsystem::GetTimeUntilNextCue() const
{
	const UInputReplayComponent* Component = FindReplayComponent();
	return Component ? Component->GetTimeUntilNextCue() : 0.0f;
}

float UInputRecordingSubsystem::GetMatchProgress() const
{
	const UInputReplayComponent* Component = FindReplayComponent();
	return Component ? Component->GetMatchProgress() : 0.0f;
}

float UInputRecordingSubsystem::GetPlaybackProgress() const
{
	const UInputReplayComponent* Component = FindReplayComponent();
	return Component ? Component->GetPlaybackProgress() : 0.0f;
}

int32 UInputRecordingSubsystem::GetMatchCueCount() const
{
	const UInputReplayComponent* Component = FindReplayComponent();
	return Component ? Component->GetMatchCueCount() : 0;
}

int32 UInputRecordingSubsystem::GetCurrentMatchCueIndex() const
{
	const UInputReplayComponent* Component = FindReplayComponent();
	return Component ? Component->GetCurrentMatchCueIndex() : 0;
}

float UInputRecordingSubsystem::GetMatchClockSeconds() const
{
	const UInputReplayComponent* Component = FindReplayComponent();
	return Component ? Component->GetMatchClockSeconds() : 0.0f;
}

TArray<FMatchInputCue> UInputRecordingSubsystem::GetMatchCues() const
{
	const UInputReplayComponent* Component = FindReplayComponent();
	return Component ? Component->GetMatchCues() : TArray<FMatchInputCue>();
}

float UInputRecordingSubsystem::GetRecordingDurationSeconds() const
{
	const UInputReplayComponent* Component = FindReplayComponent();
	return Component ? Component->GetRecording().GetDurationSeconds() : 0.0f;
}

TArray<FString> UInputRecordingSubsystem::GetAvailableRecordings(bool bJson) const
{
	return UInputReplaySerializer::ListRecordings(bJson);
}

FString UInputRecordingSubsystem::GetStatusText() const
{
	const UInputReplayComponent* Component = FindReplayComponent();
	if (!Component)
	{
		return TEXT("No replay component");
	}

	switch (Component->GetMode())
	{
	case EInputReplayMode::Recording:
		return FString::Printf(TEXT("Recording '%s' - tick %d"),
			*ActiveRecordingName, Component->GetCurrentFrameIndex());

	case EInputReplayMode::Playing:
		return FString::Printf(TEXT("Replaying '%s' - %.0f%%"),
			*ActiveRecordingName, Component->GetPlaybackProgress() * 100.0f);

	case EInputReplayMode::MatchInput:
		if (Component->IsAwaitingMatchInput())
		{
			return FString::Printf(TEXT("Match Input %d/%d - press %s"),
				Component->GetCurrentMatchCueIndex() + 1, Component->GetMatchCueCount(),
				*Component->GetExpectedInputDescription());
		}
		return FString::Printf(TEXT("Match Input %d/%d - next cue in %.1fs"),
			Component->GetCurrentMatchCueIndex() + 1, Component->GetMatchCueCount(),
			Component->GetTimeUntilNextCue());

	default:
		return FString::Printf(TEXT("Idle - active recording '%s'"), *ActiveRecordingName);
	}
}

// ---------------------------------------------------------------------------------------------
// Editor tooling
// ---------------------------------------------------------------------------------------------

UInputRecordingDataAsset* UInputRecordingSubsystem::GenerateDataAssetFromFile(const FString& FileName, bool bJson)
{
	const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();
	const FString Destination = (Settings && !Settings->DataAssetPackagePath.IsEmpty())
		? Settings->DataAssetPackagePath
		: FString(UInputRecordingAssetTools::DefaultPackagePath);

	return UInputRecordingAssetTools::GenerateRecordingDataAsset(
		ResolveRecordingName(FileName), bJson, Destination, TEXT(""), /*bOpenInEditor=*/true);
}

UInputRecordingDataAsset* UInputRecordingSubsystem::GenerateDataAssetFromLastRecording()
{
	return GenerateDataAssetFromFile(ActiveRecordingName, bUseJsonFormat);
}

// ---------------------------------------------------------------------------------------------
// Component event relays
// ---------------------------------------------------------------------------------------------

void UInputRecordingSubsystem::BroadcastModeChanged()
{
	const EInputReplayMode Mode = GetMode();
	if (Mode == LastBroadcastMode)
	{
		return;
	}

	LastBroadcastMode = Mode;
	OnModeChanged.Broadcast(Mode);
}

void UInputRecordingSubsystem::HandleRecordingStarted()	{ BroadcastModeChanged(); }
void UInputRecordingSubsystem::HandleRecordingStopped()	{ BroadcastModeChanged(); }
void UInputRecordingSubsystem::HandlePlaybackStarted()	{ BroadcastModeChanged(); }
void UInputRecordingSubsystem::HandlePlaybackFinished()	{ BroadcastModeChanged(); }

void UInputRecordingSubsystem::HandleMatchInputStarted()
{
	// The session's clock is back at zero, so the video has to be too - a restart after a completed run
	// would otherwise resume from wherever the last one left it.
	if (VideoPlayer && VideoPlayer->IsVideoOpen())
	{
		VideoPlayer->RestartFromBeginning();
		VideoPlayer->ResumeVideo();
	}

	BroadcastModeChanged();
}

void UInputRecordingSubsystem::HandleMatchCuePresented(int32 CueIndex, int32 TotalCues, const FString& ExpectedInput)
{
	// The cue is due and the clock has frozen. Pausing here rather than waiting for the next sync tick
	// is what makes the video stop on the right frame instead of a frame or two past it.
	if (VideoPlayer)
	{
		VideoPlayer->PauseVideo();
	}

	OnMatchCuePresented.Broadcast(CueIndex, TotalCues, ExpectedInput);
}

void UInputRecordingSubsystem::HandleMatchInputMatched(int32 CueIndex, int32 TotalCues)
{
	// The one and only thing that restarts the video. Note there is no counterpart in
	// HandleMatchInputMismatch: a wrong press leaves the video exactly where it is, which is the
	// behaviour the tutorial wants.
	if (VideoPlayer)
	{
		VideoPlayer->ResumeVideo();
	}

	OnMatchInputMatched.Broadcast(CueIndex, TotalCues);
}

void UInputRecordingSubsystem::HandleMatchInputMismatch(const FString& ExpectedInput, const FString& ActualInput)
{
	++MismatchCount;
	LastMismatchDescription = FString::Printf(TEXT("Expected %s, got %s"), *ExpectedInput, *ActualInput);

	OnMatchInputMismatch.Broadcast(ExpectedInput, ActualInput);
}

void UInputRecordingSubsystem::HandleMatchInputFinished(bool bCompletedAllCues)
{
	// Paused rather than closed: the UI usually wants to keep showing the final frame, and a widget
	// still holding the media texture should not have it cleared out from under it. StopAll() closes.
	if (VideoPlayer)
	{
		VideoPlayer->PauseVideo();
	}

	OnMatchInputFinished.Broadcast(bCompletedAllCues);
	BroadcastModeChanged();
}

void UInputRecordingSubsystem::HandleInputSyncPointRecorded(FName ActionName, float TimeSeconds, FVector Value)
{
	OnInputSyncPointRecorded.Broadcast(ActionName, TimeSeconds, Value);
}

bool UInputRecordingSubsystem::GetLiveInputSnapshot(FString& OutActionName, FVector& OutValue) const
{
	const UInputReplayComponent* Component = FindReplayComponent();
	return Component && Component->GetLiveInputSnapshot(OutActionName, OutValue);
}
