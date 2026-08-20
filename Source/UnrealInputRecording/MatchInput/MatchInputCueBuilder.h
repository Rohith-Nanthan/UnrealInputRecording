// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "InputReplay/InputReplayTypes.h"
#include "MatchInput/MatchInputTypes.h"
#include "MatchInputCueBuilder.generated.h"

/**
 * Cue extraction and the one shared description formatter.
 *
 * This lives in its own translation unit rather than inside the replay component because two
 * consumers need byte-identical results - the live MatchInput state machine and any
 * editor-side preview - and duplicating the rules guarantees they drift apart.
 */
UCLASS()
class UNREALINPUTRECORDING_API UMatchInputCueBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Walks a whole take and picks the significant press onsets - the frames where an action
	 * crossed from below the press threshold to above it - not every sample.
	 */
	UFUNCTION(BlueprintCallable, Category = "Match Input")
	static TArray<FMatchInputCue> BuildMatchInputCues(const FInputRecording& Recording, const FMatchInputCueBuildOptions& Options);

	/**
	 * The single formatter every log line and every UI label goes through, so the expected
	 * prompt and the "you pressed" line can never disagree about phrasing.
	 */
	UFUNCTION(BlueprintPure, Category = "Match Input")
	static FString FormatInputDescription(const FString& ActionName, uint8 ValueType, const FVector& Value);

	/** True when this action name appears in the ignore list, by bare name or by full path. */
	UFUNCTION(BlueprintPure, Category = "Match Input")
	static bool IsActionIgnored(const FString& ActionPathOrName, const TArray<FString>& IgnoredActions);

	/**
	 * Direction comparison used to judge an answer. Booleans need presence only; axes must
	 * point the same way within the tolerance. Magnitude never has to match.
	 */
	UFUNCTION(BlueprintPure, Category = "Match Input")
	static bool DoesValueMatch(uint8 ValueType, const FVector& Expected, const FVector& Actual, float DirectionTolerance, float PressThreshold);
};
