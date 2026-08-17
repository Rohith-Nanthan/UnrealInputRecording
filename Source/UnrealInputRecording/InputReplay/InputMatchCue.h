// Copyright (c) Your Studio. All Rights Reserved.
//
// InputMatchCue.h
//
// Turns a raw FInputRecording into a *cue list* - the ordered set of discrete inputs an interactive
// "MatchInput" session will ask the live player to reproduce.
//
// Why a separate concept from FRecordedInputFrame:
//   A recording is a dense, delta-compressed stream of every tracked action, including analog noise
//   from sticks and per-frame mouse deltas. A tutorial cannot ask a player to "reproduce
//   IA_Look = (0.0134, -0.0021)". A cue is therefore a *press onset*: the frame at which an action
//   crossed from below the press threshold to above it.
//
// This lives in its own translation unit (rather than inside the component) because two very
// different consumers need identical results:
//   * UInputReplayComponent  - drives the live MatchInput state machine.
//   * UInputRecordingDataAsset - previews the same cue list in the editor's details panel.
// Duplicating the extraction rules in both would guarantee they drift apart.

#pragma once

#include "CoreMinimal.h"
#include "InputActionValue.h"                  // EInputActionValueType
#include "InputReplayTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/SoftObjectPtr.h"

#include "InputMatchCue.generated.h"

class UInputAction;

DECLARE_LOG_CATEGORY_EXTERN(LogInputMatch, Log, All);

/**
 * One thing the player has to do, at one point in time.
 *
 * Everything here is resolved from the recording header, so a cue is fully self-describing: it can
 * be serialised into a UDataAsset and inspected without the original recording being loaded.
 */
USTRUCT(BlueprintType)
struct FMatchInputCue
{
	GENERATED_BODY()

	/** Index into FInputRecordingHeader::ActionPaths. The MatchInput state machine matches on this. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Match Input Cue")
	int32 ActionIndex = INDEX_NONE;

	/** Logical tick the press onset landed on. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Match Input Cue")
	int32 FrameIndex = 0;

	/** Absolute time from the start of the recording. This is the timestamp MatchInput waits for. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Match Input Cue")
	float TimeSeconds = 0.0f;

	/** Gap between this cue and the previous one - i.e. the interval MatchInput actually counts down. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Match Input Cue")
	float IntervalFromPreviousSeconds = 0.0f;

	/** Short asset name ("IA_Jump"), for logs and UI. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Match Input Cue")
	FString ActionName;

	/** Soft reference so the editor shows a clickable asset and nothing is force-loaded. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Match Input Cue")
	TSoftObjectPtr<UInputAction> Action;

	/** The value that must be reproduced. Direction matters for axis actions; magnitude does not. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Match Input Cue")
	FVector ExpectedValue = FVector::ZeroVector;

	/** EInputActionValueType of the expected value. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Match Input Cue")
	uint8 ValueType = 0;

	/** Pre-formatted "IA_Move [Fwd | X=+0.00 Y=+1.00]" for logs, UI and error messages. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Match Input Cue")
	FString Description;

	bool IsValidCue() const { return ActionIndex != INDEX_NONE; }
};

/**
 * Knobs for the cue extraction pass. Shared by the component and the DataAsset so that what you
 * preview in the editor is exactly what you will be asked to press at runtime.
 */
USTRUCT(BlueprintType)
struct FMatchInputCueBuildOptions
{
	GENERATED_BODY()

	/**
	 * Value magnitude at which an action counts as "pressed". Also used as the dead zone when
	 * listening to the live controller, so stick drift and trigger creep do not register as presses.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match Input", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float PressThreshold = 0.35f;

	/**
	 * Two onsets of the same action closer together than this collapse into one cue. Guards against
	 * the recorder's tap-preservation pass (which deliberately emits a peak *and* a settle sample)
	 * turning a single button tap into two cues.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match Input", meta = (ClampMin = "0.0"))
	float MinimumCueSpacing = 0.05f;

	/**
	 * Skip actions the recording flagged as per-frame deltas (mouse XY, scroll wheel). Their values
	 * are continuous noise and would produce hundreds of meaningless cues.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match Input")
	bool bIgnoreFrameDeltaActions = true;

	/**
	 * Extra actions to exclude. Accepts either a full object path
	 * ("/Game/Input/Actions/IA_Look.IA_Look") or just the asset name ("IA_Look").
	 * Analog *rate* actions such as a gamepad look stick are the usual candidates - they are not
	 * frame deltas, so bIgnoreFrameDeltaActions will not catch them.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match Input")
	TArray<FString> IgnoredActions;
};

/**
 * Stateless helpers shared by the runtime component and the editor-side DataAsset.
 */
UCLASS()
class UNREALINPUTRECORDING_API UInputMatchLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Extracts the cue list from a recording.
	 *
	 * @return number of cues written to OutCues (sorted ascending by TimeSeconds).
	 */
	static int32 BuildMatchInputCues(const FInputRecording& Recording,
									 const FMatchInputCueBuildOptions& Options,
									 TArray<FMatchInputCue>& OutCues);

	/** Blueprint-friendly wrapper for the above (Editor Utility Widgets, debug HUDs). */
	UFUNCTION(BlueprintCallable, Category = "Input Replay|Match Input", meta = (DisplayName = "Build Match Input Cues"))
	static TArray<FMatchInputCue> BuildMatchInputCuesFromRecording(const FInputRecording& Recording,
																  const FMatchInputCueBuildOptions& Options);

	/** "IA_Move [Fwd-Right | X=+0.71 Y=+0.71]". The single formatter used by every log and label. */
	UFUNCTION(BlueprintPure, Category = "Input Replay|Match Input")
	static FString DescribeInputValue(const FString& ActionName, uint8 ValueType, const FVector& Value);

	/** Readable ETriggerEvent name. Hand-rolled so bitflag/enum layout changes cannot break logs. */
	UFUNCTION(BlueprintPure, Category = "Input Replay|Match Input")
	static FString DescribeTriggerEvent(uint8 TriggerEvent);

	/** Readable EInputActionValueType name. */
	UFUNCTION(BlueprintPure, Category = "Input Replay|Match Input")
	static FString DescribeValueType(uint8 ValueType);

	/**
	 * Wall-clock time of a recorded sample. Prefers the stored TimeSeconds and falls back to
	 * FrameIndex * fixed step, which keeps JSON recordings (where TimeSeconds may have been trimmed
	 * to zero by a hand edit) usable.
	 */
	static float GetFrameTimeSeconds(const FInputRecording& Recording, const FRecordedInputFrame& Frame);

	/** Short asset name from a full object path. "/Game/X/IA_Jump.IA_Jump" -> "IA_Jump". */
	static FString GetActionShortName(const FString& ActionPath);

	/**
	 * Direction comparison used to decide whether a live input satisfies a cue.
	 * Booleans only need presence; axes must point the same way within DirectionTolerance
	 * (a dot product, so 0.7 ~= within 45 degrees).
	 */
	static bool DoesValueSatisfyCue(const FMatchInputCue& Cue, const FVector& LiveValue,
									float PressThreshold, float DirectionTolerance);
};
