// Copyright (c) Your Studio. All Rights Reserved.
//
// InputRecordingSubsystem.h
//
// The global entry point for recording, ghost playback and interactive MatchInput.
//
// Everything a UI needs lives here, for two reasons:
//
//   1. Lifetime. A UInputReplayComponent belongs to a PlayerController and dies with it; a widget
//      that bound directly to the component's delegates would be left holding a stale pointer after
//      every seamless travel or respawn. The subsystem outlives all of that, so the widget binds
//      once and the subsystem re-binds to whichever component is current.
//
//   2. Discovery. The component may sit on AReplayPlayerController, on the pawn, or not exist at all.
//      GetReplayComponent() resolves it (and, if allowed, creates it) so no caller has to care.
//
// Reach it from Blueprint with "Get Game Instance Subsystem -> Input Recording Subsystem", or from
// C++ with GetGameInstance()->GetSubsystem<UInputRecordingSubsystem>().

#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "InputReplay/InputMatchCue.h"
#include "InputReplay/InputReplayComponent.h"
#include "InputReplay/InputReplayTypes.h"
#include "Storage/RecordingSessionTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "InputRecordingSubsystem.generated.h"

class APlayerController;
class UInputRecordingDataAsset;
class UInputRecordingScreenRecorder;
class UInputRecordingVideoPlayer;
class URecordingControllerWidget;
class URecordingStore;
class URecordingToastWidget;

/** Fired whenever the underlying component changes mode. Drives button enable/disable in the UI. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInputRecordingModeChanged, EInputReplayMode, NewMode);

/** A take's .mp4 finished writing. bSuccess is false when capture never started or the encoder failed. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInputRecordingVideoSaved, bool, bSuccess, const FString&, VideoPath);

/** Relayed from the component: an action crossed its onset threshold while recording. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnInputRecordingSyncPoint, FName, ActionName, float, TimeSeconds, FVector, Value);

/**
 * A take finished and its session is committed. SessionPath is the folder, which is what the
 * "recording successful, saved to ..." message shows.
 *
 * bQuotaStopped is true when the take ended because the store filled up rather than because someone
 * pressed stop - the recording is still valid and still saved, it is just shorter than intended.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnInputRecordingSaved, bool, bSuccess, const FString&, SessionPath, bool, bQuotaStopped);

UCLASS()
class UNREALINPUTRECORDING_API UInputRecordingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem interface

	// -----------------------------------------------------------------------------------------
	// Component discovery
	// -----------------------------------------------------------------------------------------

	/**
	 * Finds the active replay component, searching in order:
	 *   1. the cached one, if it is still alive
	 *   2. the first local PlayerController (covers AReplayPlayerController)
	 *   3. that controller's pawn
	 *   4. any actor in the world carrying the component
	 *   5. a new component added to the PlayerController, if the project settings allow it
	 *
	 * @return the component, or nullptr if there is no local player yet.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input Recording")
	UInputReplayComponent* GetReplayComponent();

	UFUNCTION(BlueprintPure, Category = "Input Recording")
	bool HasReplayComponent() const { return CachedReplayComponent.IsValid(); }

	/** Force the subsystem to drive a specific component (multiplayer splitscreen, test harnesses). */
	UFUNCTION(BlueprintCallable, Category = "Input Recording")
	void SetReplayComponent(UInputReplayComponent* Component);

	// -----------------------------------------------------------------------------------------
	// Recording
	// -----------------------------------------------------------------------------------------

	/** Begin capturing live input. Safe to call from a UI button; returns false if already busy. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording")
	bool StartRecording(const FString& DisplayName = TEXT("Recording"));

	/**
	 * Stop capturing and write the result to <ProjectSaved>/InputRecordings using ActiveRecordingName.
	 * This is the one the UI's Stop button should call - a recording that is not saved is lost.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input Recording")
	bool StopRecording();

	/** Stop capturing and save under an explicit name. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording")
	bool StopRecordingAndSave(const FString& FileName, bool bAsJson = false);

	/** Stop capturing and keep the result in memory only. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording")
	void StopRecordingWithoutSaving();

	/**
	 * Abandon the current take and delete its session folder.
	 *
	 * Distinct from StopRecordingWithoutSaving, which leaves the recording in memory and the folder
	 * on disk. This is the "I fluffed that, throw it away" path, and it reclaims the quota.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input Recording")
	void CancelRecording();

	/**
	 * Stop and save the current take, then open the control recap map for it.
	 *
	 * What the Test button and ir.record.test both call. Safe to call when nothing is recording: it
	 * just opens the recap map on the most recent session.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input Recording")
	void RunControlRecapTest();

	// -----------------------------------------------------------------------------------------
	// Storage
	// -----------------------------------------------------------------------------------------

	/** The session store. Created and scanned during Initialize, so this is never null after startup. */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Storage")
	URecordingStore* GetRecordingStore() const { return SessionStore; }

	/** The session currently being written, or an invalid one when nothing is recording. */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Storage")
	const FRecordingSessionInfo& GetActiveSession() const { return ActiveSession; }

	/** Folder of the most recently committed take. What the save confirmation shows. */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Storage")
	FString GetLastSavedSessionPath() const { return LastSavedSessionPath; }

	/** Loads a session and starts MatchInput against it. Touches the session so LRU protects it. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Match Input")
	bool StartMatchInputFromSession(const FRecordingSessionInfo& Session);

	// -----------------------------------------------------------------------------------------
	// UI ownership
	// -----------------------------------------------------------------------------------------

	/**
	 * Show the recording controller overlay, creating it if needed.
	 *
	 * The subsystem owns this widget rather than the level or the HUD because StartRecording can be
	 * called from anywhere - console, gameplay code, another widget - and all of those have to raise
	 * the same panel. A widget owned by a level would also die on travel, which is precisely when a
	 * recording most wants to keep going.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|UI")
	void ShowRecordingController();

	UFUNCTION(BlueprintCallable, Category = "Input Recording|UI")
	void HideRecordingController();

	UFUNCTION(BlueprintPure, Category = "Input Recording|UI")
	bool IsRecordingControllerVisible() const;

	/** Puts a message on screen. Real UMG, so it survives into shipping builds where debug text does not. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|UI")
	void ShowToast(const FString& Message, float DurationSeconds = 4.f);

	// Widget classes are configured in Project Settings > Game > Input Recording > UI rather than
	// here: a GameInstanceSubsystem has no details panel, so a UPROPERTY on it can only be set from
	// code or Blueprint at runtime - which is no use for something needed the first time a recording
	// starts. See UInputRecordingSettings::RecordingControllerWidgetClass.

	/** Z order for the overlay. High enough to sit above a typical gameplay HUD. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording|UI")
	int32 RecordingControllerZOrder = 1000;

	/** Ask the next capture to dump its first frame as a PNG. See ir.video.dumpframe. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Video")
	void RequestVideoFrameDump();

	// -----------------------------------------------------------------------------------------
	// MatchInput - interactive playback
	// -----------------------------------------------------------------------------------------

	/**
	 * Loads a recording and starts interactive MatchInput mode.
	 *
	 * The system waits out the interval to each recorded input, then blocks until the live player
	 * reproduces that exact input. A wrong press is logged as an error naming both the expected and
	 * the actual input, and the system keeps waiting.
	 *
	 * @param FileName  Recording to use. Empty falls back to ActiveRecordingName, then to the
	 *                  project's Default Recording Name.
	 * @param bJson     Read the .ghost.json instead of the binary .ghost.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Match Input")
	bool StartMatchInputMode(const FString& FileName = TEXT(""), bool bJson = false);

	/** As above, but sourced from a Data Asset instead of a file on disk. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Match Input")
	bool StartMatchInputModeFromAsset(UInputRecordingDataAsset* RecordingAsset);

	UFUNCTION(BlueprintCallable, Category = "Input Recording|Match Input")
	void StopMatchInputMode();

	// -----------------------------------------------------------------------------------------
	// Ghost playback (the original non-interactive mode)
	// -----------------------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Input Recording|Playback")
	bool StartPlayback(const FString& FileName = TEXT(""), bool bJson = false);

	UFUNCTION(BlueprintCallable, Category = "Input Recording|Playback")
	void StopPlayback();

	/** Stops whatever is running, whichever mode it is. Wire this to a single Stop button. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording")
	void StopAll();

	// -----------------------------------------------------------------------------------------
	// Queries - all null-safe, so a widget can poll them before any component exists
	// -----------------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Input Recording")
	EInputReplayMode GetMode() const;

	UFUNCTION(BlueprintPure, Category = "Input Recording")
	bool IsRecording() const { return GetMode() == EInputReplayMode::Recording; }

	UFUNCTION(BlueprintPure, Category = "Input Recording")
	bool IsMatchingInput() const { return GetMode() == EInputReplayMode::MatchInput; }

	UFUNCTION(BlueprintPure, Category = "Input Recording")
	bool IsPlayingBack() const { return GetMode() == EInputReplayMode::Playing; }

	UFUNCTION(BlueprintPure, Category = "Input Recording")
	bool IsIdle() const { return GetMode() == EInputReplayMode::Idle; }

	/** True while MatchInput is blocked waiting for the player to press the expected input. */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Match Input")
	bool IsAwaitingMatchInput() const;

	/** "IA_Jump [pressed]", or empty when nothing is pending. */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Match Input")
	FString GetExpectedInputDescription() const;

	/** Seconds left before the next cue becomes due. 0 while blocked on the player. */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Match Input")
	float GetTimeUntilNextCue() const;

	/** 0..1 over the cue list. */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Match Input")
	float GetMatchProgress() const;

	/** 0..1 over the recorded ticks, for non-interactive ghost playback. */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Playback")
	float GetPlaybackProgress() const;

	UFUNCTION(BlueprintPure, Category = "Input Recording|Match Input")
	int32 GetMatchCueCount() const;

	UFUNCTION(BlueprintPure, Category = "Input Recording|Match Input")
	int32 GetCurrentMatchCueIndex() const;

	/**
	 * Position along the recorded timeline, in seconds - the clock that freezes while MatchInput waits.
	 * This is what the video playhead and the timeline progress bar both follow.
	 */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Match Input")
	float GetMatchClockSeconds() const;

	/** The full cue list for the loaded recording. The UI uses it to lay out its timeline markers. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Match Input")
	TArray<FMatchInputCue> GetMatchCues() const;

	/** Wall-clock length of the loaded recording. Horizontal scale for anything drawing the timeline. */
	UFUNCTION(BlueprintPure, Category = "Input Recording")
	float GetRecordingDurationSeconds() const;

	// -----------------------------------------------------------------------------------------
	// Video
	// -----------------------------------------------------------------------------------------

	/**
	 * Screen capture, paired with the .ghost by file name. Created on first use.
	 * StartRecording drives this automatically; reach for it directly only to tune Options mid-session.
	 */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	UInputRecordingScreenRecorder* GetScreenRecorder();

	/**
	 * Playback of the recorded .mp4, with its playhead bound to the MatchInput clock. Created on first
	 * use, so a widget can call GetMediaTexture() on it before any session has started.
	 */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	UInputRecordingVideoPlayer* GetVideoPlayer();

	/** True if a .mp4 exists for this recording name. */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	bool HasVideoForRecording(const FString& FileName) const;

	/** Capture the viewport to an .mp4 alongside the .ghost. Seeded from the project settings. */
	UPROPERTY(BlueprintReadWrite, Category = "Input Recording|Video")
	bool bCaptureVideoWithRecording = true;

	/** Open and drive the paired .mp4 when a MatchInput session starts. */
	UPROPERTY(BlueprintReadWrite, Category = "Input Recording|Video")
	bool bPlayVideoDuringMatchInput = true;

	/** Wrong presses so far this MatchInput session. */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Match Input")
	int32 GetMismatchCount() const { return MismatchCount; }

	/** Most recent wrong press, formatted for display. Empty if there has not been one. */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Match Input")
	FString GetLastMismatchDescription() const { return LastMismatchDescription; }

	/** The action currently pressed hardest while recording, for the controller UI's current-input area. */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Recording")
	bool GetLiveInputSnapshot(FString& OutActionName, FVector& OutValue) const;

	/** One-line human-readable state, ready to drop into a status label. */
	UFUNCTION(BlueprintPure, Category = "Input Recording")
	FString GetStatusText() const;

	UFUNCTION(BlueprintCallable, Category = "Input Recording")
	TArray<FString> GetAvailableRecordings(bool bJson = false) const;

	// -----------------------------------------------------------------------------------------
	// Editor tooling
	// -----------------------------------------------------------------------------------------

	/** Parse a recording file into a UInputRecordingDataAsset in the Content Browser (editor only). */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Editor")
	UInputRecordingDataAsset* GenerateDataAssetFromFile(const FString& FileName, bool bJson = false);

	/** Same, for whatever ActiveRecordingName currently points at. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Editor")
	UInputRecordingDataAsset* GenerateDataAssetFromLastRecording();

	// -----------------------------------------------------------------------------------------
	// Events - bind these from the UI, not the component's
	// -----------------------------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "Input Recording|Events")
	FOnInputRecordingModeChanged OnModeChanged;

	UPROPERTY(BlueprintAssignable, Category = "Input Recording|Events")
	FOnMatchInputCuePresented OnMatchCuePresented;

	UPROPERTY(BlueprintAssignable, Category = "Input Recording|Events")
	FOnMatchInputMatched OnMatchInputMatched;

	UPROPERTY(BlueprintAssignable, Category = "Input Recording|Events")
	FOnMatchInputMismatch OnMatchInputMismatch;

	UPROPERTY(BlueprintAssignable, Category = "Input Recording|Events")
	FOnMatchInputFinished OnMatchInputFinished;

	UPROPERTY(BlueprintAssignable, Category = "Input Recording|Events")
	FOnInputRecordingVideoSaved OnVideoSaved;

	/** A take committed to disk. Carries the session folder for the save confirmation. */
	UPROPERTY(BlueprintAssignable, Category = "Input Recording|Events")
	FOnInputRecordingSaved OnRecordingSaved;

	/** Fired for every sync point captured while recording. The controller UI appends a history row. */
	UPROPERTY(BlueprintAssignable, Category = "Input Recording|Events")
	FOnInputRecordingSyncPoint OnInputSyncPointRecorded;

	// -----------------------------------------------------------------------------------------
	// Session state
	// -----------------------------------------------------------------------------------------

	/** File name used by StopRecording / StartMatchInputMode when none is supplied. */
	UPROPERTY(BlueprintReadWrite, Category = "Input Recording")
	FString ActiveRecordingName;

	/** Prefer the JSON format for the next save / load. Binary is exact; JSON is readable. */
	UPROPERTY(BlueprintReadWrite, Category = "Input Recording")
	bool bUseJsonFormat = false;

private:
	/** Resolve the component without creating one; used by the const query helpers. */
	UInputReplayComponent* FindReplayComponent() const;

	/** Create and register a component on the PlayerController, applying project defaults. */
	UInputReplayComponent* CreateReplayComponentOn(APlayerController* PlayerController);

	void BindToComponent(UInputReplayComponent* Component);
	void UnbindFromComponent(UInputReplayComponent* Component);

	/** ActiveRecordingName, or the project default if that is empty. */
	FString ResolveRecordingName(const FString& Requested) const;

	void BroadcastModeChanged();

	//~ Video -----------------------------------------------------------------------------------

	/**
	 * Per-frame drift correction for the video playhead.
	 *
	 * A GameInstanceSubsystem does not tick, and the pause/resume events alone cannot keep a media
	 * player's own clock in step with a clock that keeps stopping. One ticker delegate is cheaper than
	 * making the subsystem tickable, and it means the sync works whether or not the UI widget is up.
	 */
	bool TickVideoSync(float DeltaSeconds);

	//~ Component event relays ------------------------------------------------------------------

	UFUNCTION() void HandleRecordingStarted();
	UFUNCTION() void HandleRecordingStopped();
	UFUNCTION() void HandlePlaybackStarted();
	UFUNCTION() void HandlePlaybackFinished();
	UFUNCTION() void HandleMatchInputStarted();
	UFUNCTION() void HandleMatchCuePresented(int32 CueIndex, int32 TotalCues, const FString& ExpectedInput);
	UFUNCTION() void HandleMatchInputMatched(int32 CueIndex, int32 TotalCues);
	UFUNCTION() void HandleMatchInputMismatch(const FString& ExpectedInput, const FString& ActualInput);
	UFUNCTION() void HandleMatchInputFinished(bool bCompletedAllCues);
	UFUNCTION() void HandleInputSyncPointRecorded(FName ActionName, float TimeSeconds, FVector Value);

	//~ State -----------------------------------------------------------------------------------

	/**
	 * Weak on purpose: the component belongs to a PlayerController, and the subsystem must not keep
	 * a dead controller alive across a level change.
	 */
	TWeakObjectPtr<UInputReplayComponent> CachedReplayComponent;

	/**
	 * Hard references, unlike the component: these are owned by the subsystem and are meant to outlive
	 * level changes so a recording can be reviewed after travelling.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UInputRecordingScreenRecorder> ScreenRecorder;

	UPROPERTY(Transient)
	TObjectPtr<UInputRecordingVideoPlayer> VideoPlayer;

	UPROPERTY(Transient)
	TObjectPtr<URecordingStore> SessionStore;

	UPROPERTY(Transient)
	TObjectPtr<URecordingControllerWidget> ControllerWidget;

	UPROPERTY(Transient)
	TObjectPtr<URecordingToastWidget> ToastWidget;

	/** The take in progress. Invalid whenever nothing is recording. */
	FRecordingSessionInfo ActiveSession;

	FString LastSavedSessionPath;

	/**
	 * Accumulator for the quota poll, which shares the video sync ticker rather than adding a second
	 * one. Checking every frame would stat the disk 60 times a second for no benefit; once a second
	 * is well inside the margin the per-take reservation buys.
	 */
	float QuotaPollAccumulator = 0.f;

	/** True when the current take was cut short by the quota rather than by a stop request. */
	bool bActiveTakeStoppedByQuota = false;

	/** Polls the store during a take and stops recording the moment the quota is reached. */
	void TickQuotaGuard(float DeltaSeconds);

	FTSTicker::FDelegateHandle VideoSyncTickerHandle;

	/** Last mode we told listeners about, so OnModeChanged only fires on real transitions. */
	EInputReplayMode LastBroadcastMode = EInputReplayMode::Idle;

	int32 MismatchCount = 0;
	FString LastMismatchDescription;

	/** True once we have warned that the component is not receiving the PlayerController hooks. */
	bool bWarnedAboutAutoCreatedComponent = false;
};
