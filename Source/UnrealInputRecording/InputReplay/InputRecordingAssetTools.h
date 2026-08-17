// Copyright (c) Your Studio. All Rights Reserved.
//
// InputRecordingAssetTools.h
//
// Turns a recording file on disk into a real UInputRecordingDataAsset in the Content Browser.
//
// Three ways to drive it, all hitting the same code path:
//
//   1. Output Log console (no PIE needed):
//          InputReplay.GenerateDataAsset Lap01
//          InputReplay.GenerateDataAsset Lap01 json
//          InputReplay.GenerateAllDataAssets
//          InputReplay.ListRecordings
//
//   2. An Editor Utility Blueprint / Editor Utility Widget - these are BlueprintCallable.
//
//   3. C++ / the game module, via UInputRecordingSubsystem::GenerateDataAssetFromFile.
//
// Asset creation is inherently editor-only. In a cooked build the functions log an error and return
// null rather than silently doing nothing.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "InputRecordingAssetTools.generated.h"

class UInputRecordingDataAsset;

UCLASS()
class UNREALINPUTRECORDING_API UInputRecordingAssetTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Default destination for generated assets. */
	static const TCHAR* DefaultPackagePath;

	/**
	 * Parses a saved recording and creates (or updates in place) a Data Asset for it.
	 *
	 * @param FileName          Bare recording name ("Lap01"), or an absolute path.
	 * @param bJson             Read the .ghost.json instead of the binary .ghost.
	 * @param DestinationPath   Content Browser package path, e.g. "/Game/InputRecordings".
	 * @param AssetName         Asset name; defaults to "DA_<FileName>".
	 * @param bOpenInEditor     Open the new asset once it has been written.
	 * @return the asset, or nullptr if the file could not be parsed or the package could not be saved.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input Replay|Editor",
		meta = (AdvancedDisplay = "2", DisplayName = "Generate Input Recording Data Asset"))
	static UInputRecordingDataAsset* GenerateRecordingDataAsset(
		const FString& FileName,
		bool bJson = false,
		const FString& DestinationPath = TEXT("/Game/InputRecordings"),
		const FString& AssetName = TEXT(""),
		bool bOpenInEditor = true);

	/**
	 * Generates one Data Asset per recording found in <ProjectSaved>/InputRecordings.
	 * @return how many assets were created or updated.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input Replay|Editor",
		meta = (AdvancedDisplay = "1", DisplayName = "Generate All Input Recording Data Assets"))
	static int32 GenerateDataAssetsForAllRecordings(
		bool bJson = false,
		const FString& DestinationPath = TEXT("/Game/InputRecordings"));

	/** Bare names of the recordings currently on disk. */
	UFUNCTION(BlueprintCallable, Category = "Input Replay|Editor")
	static TArray<FString> GetAvailableRecordings(bool bJson = false);
};
