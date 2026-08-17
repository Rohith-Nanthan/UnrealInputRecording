// Copyright (c) Your Studio. All Rights Reserved.
//
// InputReplaySerializer.h
//
// Custom file I/O for input recordings.
//
//  * Binary (.ghost)      - FMemoryWriter/FMemoryReader + FFileHelper. Compact, exact, shipping.
//  * JSON   (.ghost.json) - FJsonObjectConverter. Human-readable, diffable, for debugging only:
//                           JSON float round-tripping is not bit-exact, so a JSON recording is
//                           not guaranteed to reproduce a binary recording tick for tick.
//
// Requires "Json" and "JsonUtilities" in your Build.cs PrivateDependencyModuleNames.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "InputReplayTypes.h"

#include "InputReplaySerializer.generated.h"

UCLASS()
class UInputReplaySerializer : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** <ProjectSaved>/InputRecordings/ - created on demand. */
	UFUNCTION(BlueprintPure, Category = "Input Replay|IO")
	static FString GetRecordingDirectory();

	/**
	 * Resolves a bare file name ("Lap01") to a full path, adding the right extension.
	 * An absolute path or a name that already has the extension is passed through untouched.
	 */
	UFUNCTION(BlueprintPure, Category = "Input Replay|IO")
	static FString ResolveRecordingPath(const FString& FileName, bool bJson);

	/** Lists recordings found on disk (bare names, no extension). */
	UFUNCTION(BlueprintCallable, Category = "Input Replay|IO")
	static TArray<FString> ListRecordings(bool bJson = false);

	static bool SaveBinary(const FInputRecording& Recording, const FString& FileName, FString& OutError);
	static bool LoadBinary(FInputRecording& OutRecording, const FString& FileName, FString& OutError);

	static bool SaveJson(const FInputRecording& Recording, const FString& FileName, FString& OutError);
	static bool LoadJson(FInputRecording& OutRecording, const FString& FileName, FString& OutError);

	/** Dispatches to the binary or JSON implementation. */
	static bool Save(const FInputRecording& Recording, const FString& FileName, bool bJson, FString& OutError);
	static bool Load(FInputRecording& OutRecording, const FString& FileName, bool bJson, FString& OutError);
};
