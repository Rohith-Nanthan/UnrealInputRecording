// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "InputReplay/InputReplayTypes.h"
#include "InputRecordingSerializer.generated.h"

/** Binary .ghost file magic - 'GHST' little-endian. */
inline constexpr uint32 GInputRecordingGhostMagic = 0x54534847u;

/** Every layout this build understands. Loading refuses anything newer than the last one. */
namespace InputRecordingGhostVersions
{
	inline constexpr uint32 Initial = 1u;

	/** Header gained MappingContextPaths and MappingContextPriorities. */
	inline constexpr uint32 MappingContexts = 2u;
}

/** Bump when the on-disk layout changes. Loading refuses anything newer than this. */
inline constexpr uint32 GInputRecordingGhostVersion = InputRecordingGhostVersions::MappingContexts;

/**
 * Carries the file version into the nested operator<< calls.
 *
 * The file's own magic-and-version preamble stays the source of truth; this is never written
 * into the file. It exists because a struct's operator<< has no other way to learn which layout
 * it is reading, and threading a version argument through every one of them would change three
 * signatures to solve it in one place.
 */
INPUTRECORDER_API extern const FGuid GInputRecordingGhostVersionGuid;

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
	 * Reads the header block and stops.
	 *
	 * The review map has to restore the take's mapping contexts before MatchInput starts
	 * listening, which is well before it wants the samples - and on a long take the samples are
	 * the overwhelming majority of the file.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Serialization")
	static bool LoadRecordingHeader(const FString& AbsoluteBasePath, FInputRecordingHeader& OutHeader);

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
