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
#include "Subsystems/GameInstanceSubsystem.h"

#include "InputRecordingSubsystem.generated.h"

class APlayerController;
class UInputRecordingDataAsset;
class UInputRecordingScreenRecorder;
class UInputRecordingVideoPlayer;

/** Fired whenever the underlying component changes mode. Drives button enable/disable in the UI. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInputRecordingModeChanged, EInputReplayMode, NewMode);

/** A take's .mp4 finished writing. bSuccess is false when capture never started or the encoder failed. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInputRecordingVideoSaved, bool, bSuccess, const FString&, VideoPath);

/** Relayed from the component: an action crossed its onset threshold while recording. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnInputRecordingSyncPoint, FName, ActionName, float, TimeSeconds, FVector, Value);

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

	FTSTicker::FDelegateHandle VideoSyncTickerHandle;

	/** Last mode we told listeners about, so OnModeChanged only fires on real transitions. */
	EInputReplayMode LastBroadcastMode = EInputReplayMode::Idle;

	int32 MismatchCount = 0;
	FString LastMismatchDescription;

	/** True once we have warned that the component is not receiving the PlayerController hooks. */
	bool bWarnedAboutAutoCreatedComponent = false;
};
