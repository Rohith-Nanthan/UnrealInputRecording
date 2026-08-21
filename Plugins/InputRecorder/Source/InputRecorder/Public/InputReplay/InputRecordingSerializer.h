// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "InputReplay/InputReplayTypes.h"
#include "InputRecordingSerializer.generated.h"

/** Binary .ghost file magic - 'GHST' little-endian. */
inline constexpr uint32 GInputRecordingGhostMagic = 0x54534847u;

/** Bump when the on-disk layout changes. Loading refuses anything newer than this. */
inline constexpr uint32 GInputRecordingGhostVersion = 1u;

INPUTRECORDER_API FArchive& operator<<(FArchive& Ar, FRecordedInputSample& Sample);
INPUTRECORDER_API FArchive& operator<<(FArchive& Ar, FInputRecordingHeader& Header);
INPUTRECORDER_API FArchive& operator<<(FArchive& Ar, FInputRecording& Recording);

/**
 * Reads and writes takes.
 *
 * Every entry point takes an absolute base path with no extension and appends its own
 * suffix. One base path therefore produces every file belonging to a take, which is what
 * keeps the session-folder layout free of string surgery at the call sites.
 */
UCLASS()
class INPUTRECORDER_API UInputRecordingSerializer : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Input Recording|Serialization")
	static FString GetGhostPath(const FString& AbsoluteBasePath);

	UFUNCTION(BlueprintPure, Category = "Input Recording|Serialization")
	static FString GetGhostJsonPath(const FString& AbsoluteBasePath);

	/** Writes the binary .ghost, and optionally a .ghost.json beside it. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Serialization")
	static bool SaveRecording(const FInputRecording& Recording, const FString& AbsoluteBasePath, bool bAlsoExportJson);

	/** Reads the binary .ghost. The JSON copy is never read back - see SaveRecordingAsJson. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Serialization")
	static bool LoadRecording(const FString& AbsoluteBasePath, FInputRecording& OutRecording);

	/**
	 * Human-readable copy, for reading and diffing only.
	 * JSON float round-tripping is not bit-exact, so this is never the load path.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Serialization")
	static bool SaveRecordingAsJson(const FInputRecording& Recording, const FString& AbsoluteBasePath);

	/** True when a loadable .ghost exists at this base path. */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Serialization")
	static bool DoesRecordingExist(const FString& AbsoluteBasePath);
};
