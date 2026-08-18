// Copyright (c) Your Studio. All Rights Reserved.
//
// InputReplayComponent.h
//
// The manager. Lives on the PlayerController and does three jobs:
//
//   1. RECORD   - samples every tracked UInputAction once per logical tick, delta-compressed.
//   2. SERIALISE- hands the buffer to UInputReplaySerializer.
//   3. PLAY BACK- re-injects the recorded values through the Enhanced Input subsystem so that the
//                 pawn's existing input bindings fire exactly as they did live.
//
// Call order matters and is documented in AReplayPlayerController:
//   APlayerController::PreProcessInput   -> TickPreInput()  -> inject   (BEFORE the input stack)
//   APlayerController::PostProcessInput  -> TickPostInput() -> sample   (AFTER the input stack)

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputMatchCue.h"
#include "InputReplayTypes.h"

#include "InputReplayComponent.generated.h"

class APawn;
class APlayerController;
class UEnhancedInputLocalPlayerSubsystem;
class UEnhancedPlayerInput;
class UInputAction;
class UInputMappingContext;

DECLARE_LOG_CATEGORY_EXTERN(LogInputReplay, Log, All);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnInputReplayEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnInputReplayDesync, int32, FrameIndex, float, PositionErrorCm, float, RotationErrorDeg);

/** A cue has just become due; the system is now blocked until ExpectedInput is reproduced. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnMatchInputCuePresented, int32, CueIndex, int32, TotalCues, const FString&, ExpectedInput);

/** The live player reproduced the cue; the interval clock resumes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMatchInputMatched, int32, CueIndex, int32, TotalCues);

/** The live player pressed something else. Both descriptions are already formatted for display. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMatchInputMismatch, const FString&, ExpectedInput, const FString&, ActualInput);

/** MatchInput ended. bCompletedAllCues distinguishes "player finished" from "someone called Stop". */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMatchInputFinished, bool, bCompletedAllCues);

/**
 * Per-action bookkeeping used while recording. Runtime only - never serialised.
 */
struct FRecorderActionState
{
	/** Last value we actually wrote to the buffer (for delta compression). */
	FVector LastEmittedValue = FVector::ZeroVector;
	ETriggerEvent LastEmittedEvent = ETriggerEvent::None;
	bool bHasEmitted = false;

	/** Value sampled on the most recent engine frame. */
	FVector LatestValue = FVector::ZeroVector;
	ETriggerEvent LatestEvent = ETriggerEvent::None;

	/**
	 * Largest-magnitude value seen since the last emitted logical tick. Lets a one-engine-frame
	 * tap survive when the engine is running faster than the logical tick rate.
	 */
	FVector PeakValue = FVector::ZeroVector;
	bool bSawTransient = false;

	/** For frame-delta actions (mouse): summed since the last emitted tick. */
	FVector AccumulatedDelta = FVector::ZeroVector;
};

/**
 * Per-action bookkeeping used while playing back. Runtime only.
 */
struct FPlaybackActionState
{
	/** Held value for "rate" actions - persists between recorded samples. */
	FVector CurrentValue = FVector::ZeroVector;

	/** Summed delta for "frame delta" actions - consumed and cleared every injection. */
	FVector PendingDelta = FVector::ZeroVector;

	/** True once we have injected a value; lets us push a single zero so triggers see a release. */
	bool bNeedsZeroFlush = false;

	/** Largest value seen in the span of ticks folded into the current engine frame. */
	FVector SpanPeakValue = FVector::ZeroVector;

	/** A sample for this action landed in the current span. */
	bool bTouchedThisSpan = false;

	/** We held a press open past its recorded release to survive a frame hitch; release next frame. */
	bool bReleasePending = false;
};

/**
 * Per-action bookkeeping used while listening to the LIVE player in MatchInput mode. Runtime only.
 *
 * The listen set is deliberately wider than the recording's own action registry: if the player
 * presses something that was never recorded we still want to name it in the mismatch log, and for
 * that we have to be watching it.
 */
struct FMatchListenState
{
	/** The action being watched. */
	TWeakObjectPtr<const UInputAction> Action;

	/** Index into the recording's registry, or INDEX_NONE for actions the recording never saw. */
	int32 RegistryIndex = INDEX_NONE;

	/** Value magnitude cleared the press threshold on the previous frame. */
	bool bWasActive = false;

	/** Value sampled this frame, for the mismatch message. */
	FVector LatestValue = FVector::ZeroVector;

	/** Cached for formatting; avoids GetName() churn every frame. */
	FString ActionName;
	uint8 ValueType = 0;
};

UCLASS(ClassGroup = (Input), meta = (BlueprintSpawnableComponent), Blueprintable)
class UInputReplayComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInputReplayComponent();

	//~ Begin Setup ------------------------------------------------------------------------------

	/**
	 * Mapping contexts whose actions should be recorded. Every unique UInputAction referenced by
	 * these contexts is tracked. Explicit configuration beats trying to scrape the subsystem's
	 * applied-context list, which is not part of the public API and moves between engine versions.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay|Setup")
	TArray<TObjectPtr<UInputMappingContext>> RecordedContexts;

	/** Actions to track that are not reachable through RecordedContexts (injected-only, etc). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay|Setup")
	TArray<TObjectPtr<UInputAction>> AdditionalActions;

	/**
	 * CRITICAL for look/aim fidelity. Actions listed here are treated as per-frame *deltas*
	 * (mouse XY, trackpad, scroll wheel) rather than *rates* (gamepad stick, WASD).
	 *
	 * A rate is sampled and held; a delta is summed and cleared. Getting this wrong is the single
	 * most common cause of "the ghost turns further than I did at 30 fps" bugs - see README.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay|Setup")
	TSet<TObjectPtr<UInputAction>> FrameDeltaActions;

	//~ Begin Determinism ------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay|Determinism")
	EInputReplayTimeMode TimeMode = EInputReplayTimeMode::FixedLogicalStep;

	/** Logical tick rate for FixedLogicalStep mode. 60 is a good default; 120 for twitchy games. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay|Determinism", meta = (ClampMin = "10", ClampMax = "480"))
	int32 LogicalTicksPerSecond = 60;

	/**
	 * Quantise recorded axis values to this step (0 disables). Snapping analog values kills the
	 * last bits of float noise so two recordings of "the same" input compare equal.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay|Determinism", meta = (ClampMin = "0.0"))
	float ValueQuantisationStep = 0.0001f;

	/** Write a pawn-state sync point every N logical ticks. 0 disables. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay|Determinism", meta = (ClampMin = "0"))
	int32 SyncPointIntervalFrames = 30;

	/** Broadcast OnDesyncDetected when positional error at a sync point exceeds this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay|Determinism", meta = (ClampMin = "0.0"))
	float DesyncToleranceCm = 25.0f;

	/** Hard-snap the pawn back onto the sync point when it drifts past DesyncToleranceCm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay|Determinism")
	bool bCorrectDriftAtSyncPoints = false;

	/** Reseed FMath's global RNG from the recording so gameplay randomness replays identically. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay|Determinism")
	bool bReseedGlobalRandom = true;

	/**
	 * Remove RecordedContexts while playing back so the human at the keyboard cannot fight the
	 * ghost. Injection bypasses key mappings entirely, so it keeps working.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay|Playback")
	bool bSuppressLiveInputDuringPlayback = true;

	/**
	 * Priority used when re-adding suppressed contexts after playback. Enhanced Input has no
	 * public getter for the priority a context was applied with, so set this to match whatever
	 * your pawn/controller uses (usually 0).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay|Playback")
	int32 RestoreContextPriority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay|Playback")
	bool bLoopPlayback = false;

	//~ Begin MatchInput -------------------------------------------------------------------------

	/**
	 * Cue extraction rules for MatchInput mode: press threshold, de-duplication window and the
	 * actions to ignore. Keep these identical to the ones on your UInputRecordingDataAsset if you
	 * want the editor preview to match what the player is actually asked to press.
	 *
	 * For the Third Person template, add IA_Look (and IA_MouseLook if it is not already flagged as a
	 * frame-delta action) to IgnoredActions - otherwise camera movement registers as wrong input.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay|Match Input")
	FMatchInputCueBuildOptions MatchCueOptions;

	/**
	 * How closely the live axis direction has to agree with the recorded one. A dot product, so
	 * 1.0 = exact, 0.7 ~= within 45 degrees, 0.0 = any direction in the same half-plane.
	 * Ignored for Boolean actions, which have no direction.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay|Match Input", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float MatchDirectionTolerance = 0.7f;

	/**
	 * Also watch every action reachable through RecordedContexts / AdditionalActions, not just the
	 * ones in the recording. Costs nothing and makes the mismatch log able to name inputs the
	 * recording never contained ("expected IA_Jump but received IA_Crouch").
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay|Match Input")
	bool bMatchListenToUnrecordedActions = true;

	/** Restart from the first cue instead of finishing. Useful for a looping tutorial kiosk. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay|Match Input")
	bool bLoopMatchInput = false;

	/** Log every cue and every successful match, not just mismatches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay|Match Input")
	bool bVerboseMatchLogging = true;

	//~ Begin Events -----------------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "Input Replay|Events")
	FOnInputReplayEvent OnRecordingStarted;

	UPROPERTY(BlueprintAssignable, Category = "Input Replay|Events")
	FOnInputReplayEvent OnRecordingStopped;

	UPROPERTY(BlueprintAssignable, Category = "Input Replay|Events")
	FOnInputReplayEvent OnPlaybackStarted;

	UPROPERTY(BlueprintAssignable, Category = "Input Replay|Events")
	FOnInputReplayEvent OnPlaybackFinished;

	UPROPERTY(BlueprintAssignable, Category = "Input Replay|Events")
	FOnInputReplayDesync OnDesyncDetected;

	UPROPERTY(BlueprintAssignable, Category = "Input Replay|Events")
	FOnInputReplayEvent OnMatchInputStarted;

	UPROPERTY(BlueprintAssignable, Category = "Input Replay|Events")
	FOnMatchInputCuePresented OnMatchInputCuePresented;

	UPROPERTY(BlueprintAssignable, Category = "Input Replay|Events")
	FOnMatchInputMatched OnMatchInputMatched;

	UPROPERTY(BlueprintAssignable, Category = "Input Replay|Events")
	FOnMatchInputMismatch OnMatchInputMismatch;

	UPROPERTY(BlueprintAssignable, Category = "Input Replay|Events")
	FOnMatchInputFinished OnMatchInputFinished;

	//~ Begin Public API -------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Input Replay")
	void StartRecording(const FString& DisplayName = TEXT("Recording"));

	UFUNCTION(BlueprintCallable, Category = "Input Replay")
	void StopRecording();

	UFUNCTION(BlueprintCallable, Category = "Input Replay")
	bool SaveRecordingToFile(const FString& FileName, bool bAsJson = false);

	UFUNCTION(BlueprintCallable, Category = "Input Replay")
	bool LoadRecordingFromFile(const FString& FileName, bool bAsJson = false);

	UFUNCTION(BlueprintCallable, Category = "Input Replay")
	bool StartPlayback();

	UFUNCTION(BlueprintCallable, Category = "Input Replay")
	void StopPlayback();

	//~ MatchInput ------------------------------------------------------------------------------

	/**
	 * Starts interactive "MatchInput" mode against the currently loaded recording.
	 *
	 * Nothing is injected and no mapping context is suppressed: this mode drives the *player*, not
	 * the pawn. The component waits out the interval to the next recorded input, then blocks until
	 * that exact input arrives from the live controller, logging an error for anything else.
	 *
	 * @return false if the component is busy, no recording is loaded, or the recording contains no
	 *         discrete presses to match (e.g. a pure mouse-look capture).
	 */
	UFUNCTION(BlueprintCallable, Category = "Input Replay|Match Input")
	bool StartMatchInput();

	UFUNCTION(BlueprintCallable, Category = "Input Replay|Match Input")
	void StopMatchInput();

	UFUNCTION(BlueprintPure, Category = "Input Replay|Match Input")
	bool IsMatchingInput() const { return Mode == EInputReplayMode::MatchInput; }

	/** True while the interval has elapsed and we are blocked waiting on the live controller. */
	UFUNCTION(BlueprintPure, Category = "Input Replay|Match Input")
	bool IsAwaitingMatchInput() const { return Mode == EInputReplayMode::MatchInput && bMatchAwaitingInput; }

	UFUNCTION(BlueprintPure, Category = "Input Replay|Match Input")
	int32 GetMatchCueCount() const { return MatchCues.Num(); }

	UFUNCTION(BlueprintPure, Category = "Input Replay|Match Input")
	int32 GetCurrentMatchCueIndex() const { return MatchCueCursor; }

	/** "IA_Jump [pressed]", or empty when no cue is pending. */
	UFUNCTION(BlueprintPure, Category = "Input Replay|Match Input")
	FString GetExpectedInputDescription() const;

	/** Seconds still to run before the current cue becomes due. 0 while awaiting input. */
	UFUNCTION(BlueprintPure, Category = "Input Replay|Match Input")
	float GetTimeUntilNextCue() const;

	UFUNCTION(BlueprintPure, Category = "Input Replay|Match Input")
	float GetMatchProgress() const;

	/**
	 * Position along the recorded timeline, in seconds.
	 *
	 * This is the clock synchronised video playback binds to: it advances with real time while an
	 * interval counts down and freezes the instant a cue becomes due, staying frozen for however long
	 * the player takes - and through however many wrong presses. Anything that should "wait with" the
	 * MatchInput system should follow this rather than keeping its own timer.
	 */
	UFUNCTION(BlueprintPure, Category = "Input Replay|Match Input")
	float GetMatchClockSeconds() const { return MatchClockSeconds; }

	const TArray<FMatchInputCue>& GetMatchCues() const { return MatchCues; }

	//~ Queries ---------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Input Replay")
	EInputReplayMode GetMode() const { return Mode; }

	UFUNCTION(BlueprintPure, Category = "Input Replay")
	bool IsRecording() const { return Mode == EInputReplayMode::Recording; }

	UFUNCTION(BlueprintPure, Category = "Input Replay")
	bool IsPlaying() const { return Mode == EInputReplayMode::Playing; }

	UFUNCTION(BlueprintPure, Category = "Input Replay")
	bool IsIdle() const { return Mode == EInputReplayMode::Idle; }

	UFUNCTION(BlueprintPure, Category = "Input Replay")
	int32 GetCurrentFrameIndex() const { return CurrentFrameIndex; }

	UFUNCTION(BlueprintPure, Category = "Input Replay")
	float GetPlaybackProgress() const;

	const FInputRecording& GetRecording() const { return Recording; }

	/**
	 * Replaces the in-memory recording without touching the disk. Used by the subsystem to feed a
	 * UInputRecordingDataAsset straight into playback or MatchInput.
	 */
	void SetRecording(const FInputRecording& InRecording);

	//~ Begin PlayerController hooks -------------------------------------------------------------

	/** Call from APlayerController::PreProcessInput. Injects this frame's recorded input. */
	void TickPreInput(float DeltaSeconds, bool bGamePaused);

	/** Call from APlayerController::PostProcessInput. Samples input / validates sync points. */
	void TickPostInput(float DeltaSeconds, bool bGamePaused);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	//~ Helpers ----------------------------------------------------------------------------------

	APlayerController* GetOwningPlayerController() const;
	APawn* GetTargetPawn() const;
	UEnhancedPlayerInput* GetEnhancedPlayerInput() const;
	UEnhancedInputLocalPlayerSubsystem* GetEnhancedInputSubsystem() const;

	/** Flatten RecordedContexts + AdditionalActions into a stable, deduplicated registry. */
	void BuildActionRegistry();

	/** Turn Header.ActionPaths back into hard UInputAction pointers after a load. */
	bool ResolveActionRegistry(FString& OutError);

	void EmitFrame(int32 FrameIndex, int32 ActionIndex, ETriggerEvent Event, const FInputActionValue& Value);
	void CaptureSyncPoint(int32 FrameIndex);
	void ValidateSyncPoint(int32 FrameIndex);

	/** Sample every tracked action for this engine frame and emit any logical ticks it covers. */
	void SampleRecording(float DeltaSeconds);

	/** Walk the frame cursor forward to TargetFrame, folding every sample into the live state. */
	void AdvanceStateTo(int32 TargetFrame, int32 StepsConsumed);

	/** Push the current reconstructed state into Enhanced Input for this engine frame. */
	void InjectCurrentState();

	void ApplyLiveInputSuppression(bool bSuppress);
	void BeginFixedTimeStepOverride();
	void EndFixedTimeStepOverride();

	FVector Quantise(const FVector& In) const;

	//~ MatchInput internals ---------------------------------------------------------------------

	/** Derive the cue list from the loaded recording using MatchCueOptions. */
	void BuildMatchInputCues();

	/** Assemble the set of actions we sample from the live controller each frame. */
	void BuildMatchListenList();

	/** The whole MatchInput state machine; called once per frame from TickPostInput. */
	void TickMatchInput(float DeltaSeconds);

	/** Sample the live controller and refresh the press-onset edges in MatchListenStates. */
	void SampleMatchListenStates();

	/** Announce the cue we are now blocked on. */
	void PresentCurrentMatchCue();

	/** Consume the current cue and resume the interval clock at its timestamp. */
	void AdvanceMatchCue();

	/** Shared exit path. bCompletedAllCues is forwarded to OnMatchInputFinished. */
	void FinishMatchInput(bool bCompletedAllCues);

	/** Reset cursor/clock/edges back to the first cue. */
	void ResetMatchInputProgress();

	FString DescribeListenState(const FMatchListenState& State) const;

	//~ State ------------------------------------------------------------------------------------

	UPROPERTY(Transient)
	EInputReplayMode Mode = EInputReplayMode::Idle;

	/** The registry. Index in this array == FRecordedInputFrame::ActionIndex. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<const UInputAction>> TrackedActions;

	/** Parallel to TrackedActions: true if the action is a per-frame delta. */
	TArray<bool> TrackedActionIsDelta;

	UPROPERTY(Transient)
	FInputRecording Recording;

	TArray<FRecorderActionState> RecorderStates;
	TArray<FPlaybackActionState> PlaybackStates;

	/** Fixed-step accumulator, shared by record and playback. */
	float TimeAccumulator = 0.0f;

	/** Authoritative logical tick counter for whichever mode we are in. */
	int32 CurrentFrameIndex = 0;

	/** Elapsed logical seconds, for TimeSeconds bookkeeping and FreeRun mode. */
	float ElapsedSeconds = 0.0f;

	/** Read cursor into Recording.Frames during playback. */
	int32 FrameCursor = 0;

	/** Read cursor into Recording.SyncPoints during playback. */
	int32 SyncPointCursor = 0;

	/** Saved FApp state so RecordedDeltas playback can be undone cleanly. */
	bool bPushedFixedTimeStep = false;
	bool bSavedUseFixedTimeStep = false;
	double SavedFixedDeltaTime = 0.0;

	/** Contexts we removed for the duration of playback, so we can restore them verbatim. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputMappingContext>> SuppressedContexts;

	//~ MatchInput state -------------------------------------------------------------------------

	/** Ordered list of inputs the player has to reproduce. Rebuilt on every StartMatchInput. */
	UPROPERTY(Transient)
	TArray<FMatchInputCue> MatchCues;

	/** Index of the cue we are currently counting down to / blocked on. */
	int32 MatchCueCursor = 0;

	/**
	 * Position along the *recorded* timeline, in seconds. Advances with real time while counting
	 * down an interval and freezes while we are blocked on the player, which is precisely what
	 * "pause the input progression" means.
	 */
	float MatchClockSeconds = 0.0f;

	/** The interval has elapsed and we are now waiting on the live controller. */
	bool bMatchAwaitingInput = false;

	/** Live-input edge detection, one entry per watched action. */
	TArray<FMatchListenState> MatchListenStates;

	/** Indices into MatchListenStates that saw a fresh press this frame. Reused to avoid churn. */
	TArray<int32> MatchOnsetIndices;

	/**
	 * The first live sample after a reset only *primes* the edge detector. Without this, a key the
	 * player already happens to be holding when MatchInput starts would read as a brand new press.
	 */
	bool bMatchEdgesPrimed = false;

	/** GFrameCounter of the last frame the PlayerController hooks drove us (fallback detection). */
	uint64 LastHookFrameCounter = 0;

	bool bWarnedAboutMissingHooks = false;
};
