// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "InputRecordingFormatLibrary.generated.h"

/**
 * Shared formatting. Every surface that prints an age, a size or a duration goes through here -
 * the console table, the in-game list and the review header all have to agree, and they only do
 * that if there is exactly one implementation.
 */
UCLASS()
class UNREALINPUTRECORDING_API UInputRecordingFormatLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * "1 day 5 minutes 10 seconds ago".
	 *
	 * Emits only the non-zero components, in day / hour / minute / second order, each correctly
	 * pluralised. A zero component in the middle is dropped, never printed as "0 hours".
	 * Under a second reads "just now"; a future timestamp reads "in ..." rather than going
	 * negative when a clock is skewed.
	 */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Format")
	static FString FormatRelativeTime(const FDateTime& UtcTimestamp);

	/** The same rules against an already-computed span. Positive means "ago". */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Format")
	static FString FormatRelativeTimespan(const FTimespan& Elapsed);

	/** "141.2 MB". */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Format")
	static FString FormatByteSize(int64 Bytes);

	/** "0:47", or "1:02:03" once a take passes an hour. */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Format")
	static FString FormatDurationClock(float Seconds);
};
