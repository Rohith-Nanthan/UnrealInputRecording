// Copyright (c) Your Studio. All Rights Reserved.
//
// InputRecordingVideoPlayer.h
//
// A self-contained UMediaPlayer + UMediaTexture wrapper whose playhead is owned by the MatchInput
// state machine rather than by wall-clock time.
//
// THE SYNC MODEL
//
// UInputReplayComponent already maintains exactly the clock we need: MatchClockSeconds advances in
// real time while an interval is counting down and *freezes* the moment a cue becomes due, staying
// frozen until the player reproduces the input. That is precisely the behaviour asked of the video, so
// the video does not get its own timing logic - it follows that clock.
//
// Two mechanisms, and they are complementary rather than redundant:
//
//   1. Events (driven by UInputRecordingSubsystem).
//      OnMatchCuePresented -> PauseVideo(). OnMatchInputMatched -> ResumeVideo(). This is what makes
//      the pause land on the exact frame the cue is due, with no polling latency, and it is what keeps
//      the video paused through any number of wrong inputs - nothing resumes it but a correct one.
//
//   2. Drift correction (SyncToMatchClock, called once per frame).
//      A media player's clock is its own; over a few minutes it will not stay in step with a clock
//      that keeps stopping and starting. Once the error passes ResyncThresholdSeconds we seek. The
//      threshold matters: seeking every frame would stutter, seeking never would drift.
//
// Nothing here knows about the recording system's types, so this class is reusable as-is for any
// "video that follows a gameplay clock" problem.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "InputRecordingVideoPlayer.generated.h"

class UMediaPlayer;
class UMediaTexture;

/** The .mp4 finished opening (or failed to). Bind to swap the UI between video and a placeholder. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInputRecordingVideoOpened, bool, bSuccess, const FString&, VideoPath);

UCLASS(BlueprintType)
class UNREALINPUTRECORDING_API UInputRecordingVideoPlayer : public UObject
{
	GENERATED_BODY()

public:
	// -----------------------------------------------------------------------------------------
	// Transport
	// -----------------------------------------------------------------------------------------

	/**
	 * Opens <ProjectSaved>/InputRecordings/<RecordingName>.mp4, paused at the first frame.
	 *
	 * Opening is asynchronous: this returning true only means the request was accepted. Bind
	 * OnVideoOpened, or poll IsVideoReady(), before trusting GetDurationSeconds().
	 *
	 * @return false if the file does not exist, in which case nothing is opened and the caller should
	 *         run MatchInput without video.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Video")
	bool OpenRecordingVideo(const FString& RecordingName);

	/** Opens an arbitrary path. OpenRecordingVideo is the one you normally want. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Video")
	bool OpenVideoFile(const FString& AbsolutePath);

	UFUNCTION(BlueprintCallable, Category = "Input Recording|Video")
	void Close();

	/** Freeze on the current frame. Idempotent. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Video")
	void PauseVideo();

	/** Resume at 1x. Idempotent. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Video")
	void ResumeVideo();

	UFUNCTION(BlueprintCallable, Category = "Input Recording|Video")
	void SeekToSeconds(float Seconds);

	/** Rewind to zero and pause. Used when a MatchInput session restarts or loops. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Video")
	void RestartFromBeginning();

	// -----------------------------------------------------------------------------------------
	// The sync entry point
	// -----------------------------------------------------------------------------------------

	/**
	 * Drives the playhead from the MatchInput clock. Call once per frame while MatchInput is running.
	 *
	 * @param MatchClockSeconds  UInputReplayComponent's position along the recorded timeline.
	 * @param bAwaitingInput     True while the system is blocked waiting for the player. The video is
	 *                           held on the cue's frame for exactly as long as this is true, however
	 *                           many wrong inputs arrive in the meantime.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Video")
	void SyncToMatchClock(float MatchClockSeconds, bool bAwaitingInput);

	// -----------------------------------------------------------------------------------------
	// Queries
	// -----------------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	bool IsVideoOpen() const;

	/** True once the player has actually parsed the file and can be seeked. */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	bool IsVideoReady() const;

	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	float GetPositionSeconds() const;

	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	float GetDurationSeconds() const;

	/** 0..1 along the video. Falls back to 0 when nothing is open. */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	float GetProgress() const;

	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	bool IsVideoPaused() const;

	/** Bind this to a UImage brush, or to a material's texture parameter. */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	UMediaTexture* GetMediaTexture();

	/** For Blueprint that needs the raw player (track selection, audio, custom transport). */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	UMediaPlayer* GetMediaPlayer();

	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	FString GetOpenVideoPath() const { return OpenPath; }

	// -----------------------------------------------------------------------------------------
	// Tuning
	// -----------------------------------------------------------------------------------------

	/**
	 * Added to MatchClockSeconds before it is used as a video position.
	 *
	 * Capture timestamps are taken on the render thread, which trails the game thread that starts the
	 * .ghost clock by a frame or two. That is a constant offset of roughly 16-33 ms - below the resync
	 * threshold, so it never triggers a seek, but it is real. Nudge this if the video consistently
	 * shows the input landing slightly before or after the cue fires.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording|Video")
	float VideoTimeOffsetSeconds = 0.0f;

	/**
	 * How far the video may drift from the match clock before we seek.
	 *
	 * Too small and the video stutters as it is repeatedly yanked back; too large and the picture
	 * visibly lags the cue. A quarter of a second is comfortably below "noticeable" while being well
	 * clear of normal decode jitter.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording|Video", meta = (ClampMin = "0.05"))
	float ResyncThresholdSeconds = 0.25f;

	/** Turn off to let the video free-run between cues and only pause/resume on events. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording|Video")
	bool bResyncToMatchClock = true;

	UPROPERTY(BlueprintAssignable, Category = "Input Recording|Video")
	FOnInputRecordingVideoOpened OnVideoOpened;

	//~ Begin UObject interface
	virtual void BeginDestroy() override;
	//~ End UObject interface

private:
	/** Creates the player and texture on first use; both live for this object's lifetime. */
	void EnsurePlayerCreated();

	UFUNCTION() void HandleMediaOpened(FString OpenedUrl);
	UFUNCTION() void HandleMediaOpenFailed(FString FailedUrl);
	UFUNCTION() void HandleMediaClosed();

	UPROPERTY(Transient)
	TObjectPtr<UMediaPlayer> MediaPlayer;

	UPROPERTY(Transient)
	TObjectPtr<UMediaTexture> MediaTexture;

	FString OpenPath;

	/**
	 * A seek issued before the player has finished opening is silently dropped, so remember where we
	 * were asked to go and apply it in HandleMediaOpened.
	 */
	float PendingSeekSeconds = -1.0f;

	bool bOpenRequested = false;
};
