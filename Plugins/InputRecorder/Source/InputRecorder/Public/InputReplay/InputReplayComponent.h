// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputMappingContext.h"
#include "InputReplay/InputReplayTypes.h"
#include "MatchInput/MatchInputTypes.h"
#include "InputReplayComponent.generated.h"

class APlayerController;
class UEnhancedInputComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UEnhancedPlayerInput;
class UInputAction;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInputReplayModeChanged, EInputReplayMode, NewMode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnInputSampleRecorded, FName, ActionName, float, TimeSeconds, FVector, Value);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnMatchCuePresented, int32, CueIndex, int32, TotalCues, const FString&, ExpectedDescription);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMatchInputMatched, int32, CueIndex, int32, TotalCues);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMatchInputMismatched, const FString&, ExpectedDescription, const FString&, ReceivedDescription);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMatchInputFinished, bool, bCompletedAllCues);

/**
 * Records and replays Enhanced Input.
 *
 * Deliberately a component rather than a PlayerController subclass: it resolves onto whichever
 * controller or pawn exists and can be auto-created by the subsystem, which is what lets this
 * system drop into any project without dictating a class hierarchy.
 *
 * Nothing long-lived should hold a raw pointer to one of these. It dies on every respawn and
 * level travel - go through UInputRecordingSubsystem, which re-resolves it lazily.
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = (InputRecording), meta = (BlueprintSpawnableComponent))
class INPUTRECORDER_API UInputReplayComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInputReplayComponent();

	// ---------------------------------------------------------------------------------------
	// Configuration. Every field is also settable project-wide through UInputRecordingSettings.
	// ---------------------------------------------------------------------------------------

	/** Contexts whose actions get recorded. Empty means "whatever is currently applied to the player". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording|Actions")
	TArray<TSoftObjectPtr<UInputMappingContext>> RecordedMappingContexts;

	/** Extra actions to track that none of the recorded contexts reference. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording|Actions")
	TArray<TSoftObjectPtr<UInputAction>> AdditionalActions;

	/**
	 * Actions whose value is a per-frame delta rather than a rate - mouse look, scroll wheel.
	 * These are excluded from cue extraction and can never fail a match: nobody reproduces a
	 * prior mouse delta pixel-for-pixel.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording|Actions")
	TArray<TSoftObjectPtr<UInputAction>> FrameDeltaActions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording|Actions")
	EInputRecordingFilterMode FilterMode = EInputRecordingFilterMode::RecordAll;

	/** Subtractive only - narrows what the contexts already reach, never adds from outside them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording|Actions")
	TArray<FString> RecordedActionWhitelist;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording|Timing")
	EInputReplayTimeMode TimeMode = EInputReplayTimeMode::FixedLogicalStep;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording|Timing", meta = (ClampMin = "1", ClampMax = "240"))
	int32 LogicalTicksPerSecond = 60;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording|Match Input")
	FMatchInputCueBuildOptions CueBuildOptions;

	/** Dot product an axis answer must clear. 0.7 is roughly "within 45 degrees". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording|Match Input", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float MatchDirectionTolerance = 0.7f;

	// ---------------------------------------------------------------------------------------
	// Events. Blueprints normally bind these on the subsystem, which relays them.
	// ---------------------------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "Input Recording|Events")
	FOnInputReplayModeChanged OnModeChanged;

	UPROPERTY(BlueprintAssignable, Category = "Input Recording|Events")
	FOnInputSampleRecorded OnSampleRecorded;

	UPROPERTY(BlueprintAssignable, Category = "Input Recording|Events")
	FOnMatchCuePresented OnMatchCuePresented;

	UPROPERTY(BlueprintAssignable, Category = "Input Recording|Events")
	FOnMatchInputMatched OnMatchInputMatched;

	UPROPERTY(BlueprintAssignable, Category = "Input Recording|Events")
	FOnMatchInputMismatched OnMatchInputMismatched;

	UPROPERTY(BlueprintAssignable, Category = "Input Recording|Events")
	FOnMatchInputFinished OnMatchInputFinished;

	// ---------------------------------------------------------------------------------------
	// Recording
	// ---------------------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Input Recording")
	bool StartRecording(const FString& DisplayName);

	UFUNCTION(BlueprintCallable, Category = "Input Recording")
	void StopRecording();

	UFUNCTION(BlueprintCallable, Category = "Input Recording")
	bool SaveCurrentRecording(const FString& AbsoluteBasePath, bool bAlsoExportJson);

	UFUNCTION(BlueprintCallable, Category = "Input Recording")
	bool LoadRecordingFromFile(const FString& AbsoluteBasePath);

	// ---------------------------------------------------------------------------------------
	// Match Input
	// ---------------------------------------------------------------------------------------

	/** Builds cues from the supplied take and starts the quiz against them. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Match Input")
	bool StartMatchInput(const FInputRecording& Recording);

	/** Runs the quiz against whatever was last loaded through LoadRecordingFromFile. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Match Input")
	bool StartMatchInputFromLoaded();

	UFUNCTION(BlueprintCallable, Category = "Input Recording|Match Input")
	void StopMatchInput(bool bCompleted);

	/**
	 * Rebuilds the tracked-action list from the live input stack without starting anything.
	 * The overlay needs this to show a live read-out before a take begins.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input Recording")
	void RefreshTrackedActions();

	// ---------------------------------------------------------------------------------------
	// State
	// ---------------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Input Recording")
	EInputReplayMode GetMode() const { return Mode; }

	UFUNCTION(BlueprintPure, Category = "Input Recording")
	bool IsRecording() const { return Mode == EInputReplayMode::Recording; }

	UFUNCTION(BlueprintPure, Category = "Input Recording")
	bool IsMatchingInput() const { return Mode == EInputReplayMode::MatchingInput; }

	UFUNCTION(BlueprintPure, Category = "Input Recording")
	float GetRecordingDurationSeconds() const { return RecordingTimeSeconds; }

	/** Blueprint copy. C++ callers wanting no copy use GetCurrentRecordingRef. */
	UFUNCTION(BlueprintPure, Category = "Input Recording", meta = (DisplayName = "Get Current Recording"))
	FInputRecording GetCurrentRecording() const { return CurrentRecording; }
	const FInputRecording& GetCurrentRecordingRef() const { return CurrentRecording; }

	UFUNCTION(BlueprintPure, Category = "Input Recording|Match Input", meta = (DisplayName = "Get Match Cues"))
	TArray<FMatchInputCue> GetMatchCues() const { return MatchCues; }
	const TArray<FMatchInputCue>& GetMatchCuesRef() const { return MatchCues; }

	UFUNCTION(BlueprintPure, Category = "Input Recording|Match Input")
	int32 GetCurrentMatchCueIndex() const { return CurrentCueIndex; }

	UFUNCTION(BlueprintPure, Category = "Input Recording|Match Input")
	float GetMatchClockSeconds() const { return MatchClockSeconds; }

	UFUNCTION(BlueprintPure, Category = "Input Recording|Match Input")
	bool IsAwaitingMatchInput() const { return bAwaitingMatchInput; }

	UFUNCTION(BlueprintPure, Category = "Input Recording|Match Input")
	FString GetExpectedInputDescription() const;

	UFUNCTION(BlueprintPure, Category = "Input Recording|Match Input")
	int32 GetMismatchCount() const { return MismatchCount; }

	UFUNCTION(BlueprintPure, Category = "Input Recording|Match Input")
	FString GetLastMismatchDescription() const { return LastMismatchDescription; }

	/** Whichever tracked action is currently non-zero, for a live HUD read-out. False when nothing is active. */
	UFUNCTION(BlueprintPure, Category = "Input Recording")
	bool GetLiveInputSnapshot(FString& OutActionName, FVector& OutValue) const;

	// ---------------------------------------------------------------------------------------
	// Input plumbing, driven by the owning PlayerController.
	// ---------------------------------------------------------------------------------------

	/**
	 * Call from APlayerController::PreProcessInput / PostProcessInput.
	 *
	 * Sampling has to happen in PostProcessInput, after Enhanced Input has evaluated the stack,
	 * so values are read post-modifier in the same frame they were produced. Falling back to
	 * TickComponent makes every judged input one frame stale - and judging input is the entire
	 * job of the review map.
	 */
	void HandlePreProcessInput(float DeltaSeconds);
	void HandlePostProcessInput(float DeltaSeconds);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ---------------------------------------------------------------------------------------
	// Blueprint extension points
	// ---------------------------------------------------------------------------------------

	UFUNCTION(BlueprintImplementableEvent, Category = "Input Recording|Events", meta = (DisplayName = "On Recording Started"))
	void K2_OnRecordingStarted(const FString& DisplayName);

	UFUNCTION(BlueprintImplementableEvent, Category = "Input Recording|Events", meta = (DisplayName = "On Recording Stopped"))
	void K2_OnRecordingStopped(int32 SampleCount, float DurationSeconds);

	UFUNCTION(BlueprintImplementableEvent, Category = "Input Recording|Events", meta = (DisplayName = "On Match Input Started"))
	void K2_OnMatchInputStarted(int32 CueCount);

	/**
	 * Last word on whether an action is recorded, after the context and whitelist filters have
	 * run. Override in Blueprint instead of adding a C++ branch a designer would want to change.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Input Recording")
	bool ShouldRecordAction(const UInputAction* Action) const;
	virtual bool ShouldRecordAction_Implementation(const UInputAction* Action) const;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void SetMode(EInputReplayMode NewMode);

	/** Resolves the PlayerController that owns this component, whether it sits on the PC or its pawn. */
	APlayerController* ResolveOwningPlayerController() const;
	UEnhancedInputLocalPlayerSubsystem* ResolveEnhancedInputSubsystem() const;
	UEnhancedPlayerInput* ResolveEnhancedPlayerInput() const;
	UEnhancedInputComponent* ResolveEnhancedInputComponent() const;

	/** Fills TrackedActions from the recorded contexts, the live stack, and AdditionalActions. */
	bool BuildTrackedActionList();

	/** Copies the tracked list into the header. Only a take needs this; MatchInput does not. */
	void WriteHeaderActionPaths();

	/** Tracked index whose action matches this cue, by soft path first and short name second. */
	int32 ResolveCueTrackedIndex(const FMatchInputCue& Cue) const;

	void BindTrackedActionDelegates();
	void ReleaseTrackedActionDelegates();
	void ResetTrackingState();

	/**
	 * Turns on a component tick that runs immediately after the owning PlayerController's own
	 * tick, via a tick prerequisite.
	 *
	 * This is the path for projects that do not subclass their PlayerController and therefore
	 * cannot forward PostProcessInput. TickPlayerInput runs inside the controller's TickActor,
	 * so a prerequisite on that actor puts this component after input evaluation in the same
	 * frame - the same freshness PostProcessInput gives, without dictating a class hierarchy.
	 * When a controller does forward, the frame-counter guard makes this tick a no-op.
	 */
	void SetTickFallbackEnabled(bool bEnabled);

	/** One sampling step. Appends only where a value or trigger event actually changed. */
	void SampleTrackedActions(int32 FrameIndex, float TimeSeconds);
	void StepMatchInput(float DeltaSeconds);

	FVector ReadActionValue(int32 TrackedIndex, uint8& OutValueType, uint8& OutTriggerEvent) const;
	bool IsFrameDeltaAction(int32 TrackedIndex) const;

	UPROPERTY(Transient)
	EInputReplayMode Mode = EInputReplayMode::Idle;

	UPROPERTY(Transient)
	FInputRecording CurrentRecording;

	UPROPERTY(Transient)
	TArray<TObjectPtr<const UInputAction>> TrackedActions;

	/** Tracked indices whose values are per-frame deltas. Never judged, never a cue. */
	TSet<int32> FrameDeltaTrackedIndices;

	/** Tracked indices excluded by CueBuildOptions.IgnoredActions - camera and look, by default. */
	TSet<int32> IgnoredTrackedIndices;

	/** Last written value per tracked action - the whole of the delta compression state. */
	TArray<FVector> LastRecordedValues;
	TArray<uint8> LastRecordedTriggerEvents;
	TArray<bool> bHasRecordedValue;

	/** Trigger events observed through the bound delegates this frame, consumed by the sample pass. */
	TArray<uint8> PendingTriggerEvents;

	/**
	 * Raw binding handles rather than FInputBindingHandle values: that type has a protected
	 * default constructor, so it cannot live in a TArray, and keeping it out of this header
	 * also keeps EnhancedInputComponent.h out of everything that includes the component.
	 */
	TArray<uint32> ActionBindingHandles;

	int32 RecordingFrameIndex = 0;
	float RecordingTimeSeconds = 0.0f;
	float LogicalStepAccumulator = 0.0f;

	/**
	 * PreProcessInput and PostProcessInput can both arrive in one engine frame, and a second
	 * controller in the stack would step the clock twice. Frame counter guards against it.
	 */
	uint64 LastSteppedFrameCounter = 0;

	// Match Input state
	UPROPERTY(Transient)
	TArray<FMatchInputCue> MatchCues;

	/**
	 * Parallel to MatchCues. A cue's ActionIndex indexes the recording's header, but
	 * TrackedActions is rebuilt from the live stack at match time, so the two orderings are
	 * unrelated. Resolved once up front rather than searched every frame.
	 */
	TArray<int32> MatchCueTrackedIndices;

	int32 CurrentCueIndex = 0;
	float MatchClockSeconds = 0.0f;
	bool bAwaitingMatchInput = false;
	int32 MismatchCount = 0;
	FString LastMismatchDescription;

	/** Rising-edge detection so one held wrong button reports one mismatch, not one per frame. */
	TArray<bool> bWasPressedLastFrame;
};
