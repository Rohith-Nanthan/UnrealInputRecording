// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputAction.h"
#include "MatchInputTypes.generated.h"

/**
 * One thing the reviewer is asked to press.
 *
 * Carries both the soft object path and the short name on purpose: the path is precise but
 * goes stale the moment somebody renames or moves the asset, and the short name survives a
 * project reorganisation. Every consumer tries the path first and falls back to the name.
 */
USTRUCT(BlueprintType)
struct UNREALINPUTRECORDING_API FMatchInputCue
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Match Input")
	TSoftObjectPtr<UInputAction> Action;

	/** Short asset name, e.g. IA_Jump. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Match Input")
	FString ActionName;

	/** Index into the recording header's ActionPaths. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Match Input")
	int32 ActionIndex = INDEX_NONE;

	/** Logical tick of the press onset. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Match Input")
	int32 FrameIndex = 0;

	/** Absolute time from the start of the recording. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Match Input")
	float TimeSeconds = 0.0f;

	/** The gap MatchInput actually counts down before presenting this cue. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Match Input")
	float IntervalFromPreviousSeconds = 0.0f;

	/** EInputActionValueType. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Match Input")
	uint8 ValueType = 0;

	/** Direction matters for axes; magnitude does not. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Match Input")
	FVector ExpectedValue = FVector::ZeroVector;

	/** Pre-formatted through the single shared formatter, e.g. IA_Move [Fwd-Right | X=+0.71 Y=+0.71]. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Match Input")
	FString Description;
};

/** Tuning for which samples in a take become cues. */
USTRUCT(BlueprintType)
struct UNREALINPUTRECORDING_API FMatchInputCueBuildOptions
{
	GENERATED_BODY()

	/**
	 * Magnitude that counts as pressed. Doubles as the dead zone when listening to the live
	 * controller, so stick drift and trigger creep never register as an answer.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Match Input", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PressThreshold = 0.35f;

	/** Two onsets of the same action closer together than this collapse into one cue. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Match Input", meta = (ClampMin = "0.0"))
	float MinimumCueSpacing = 0.05f;

	/**
	 * Skip mouse and scroll deltas. Their values are continuous noise and would otherwise
	 * produce hundreds of meaningless cues.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Match Input")
	bool bIgnoreFrameDeltaActions = true;

	/**
	 * Full path or bare name. Defaults exclude look and camera actions - camera movement must
	 * never count as a wrong answer.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Match Input")
	TArray<FString> IgnoredActions;

	FMatchInputCueBuildOptions()
	{
		IgnoredActions.Add(TEXT("IA_Look"));
		IgnoredActions.Add(TEXT("IA_MouseLook"));
	}
};
