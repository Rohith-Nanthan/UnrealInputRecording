// Copyright (c) Your Studio. All Rights Reserved.

#include "InputRecordingAssetTools.h"

#include "InputMatchCue.h"                 // LogInputMatch
#include "InputRecordingDataAsset.h"
#include "InputReplaySerializer.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

#if WITH_EDITOR
	#include "AssetRegistry/AssetRegistryModule.h"
	#include "Editor.h"                    // GEditor
	#include "Subsystems/AssetEditorSubsystem.h"
	#include "UObject/SavePackage.h"
#endif

const TCHAR* UInputRecordingAssetTools::DefaultPackagePath = TEXT("/Game/InputRecordings");

TArray<FString> UInputRecordingAssetTools::GetAvailableRecordings(bool bJson)
{
	return UInputReplaySerializer::ListRecordings(bJson);
}

#if WITH_EDITOR

namespace
{
	/** Strip anything that cannot appear in an asset name and guarantee a usable prefix. */
	FString MakeAssetName(const FString& FileName)
	{
		FString Base = FPaths::GetBaseFilename(FileName);

		// "Lap01.ghost.json" -> GetBaseFilename leaves "Lap01.ghost", so trim again.
		Base = FPaths::GetBaseFilename(Base);
		Base = FPaths::MakeValidFileName(Base, TEXT('_'));

		// Asset names also reject a few characters that are legal in file names.
		Base.ReplaceInline(TEXT(" "), TEXT("_"));
		Base.ReplaceInline(TEXT("."), TEXT("_"));

		if (Base.IsEmpty())
		{
			Base = TEXT("Recording");
		}

		return Base.StartsWith(TEXT("DA_")) ? Base : TEXT("DA_") + Base;
	}

	FString NormalisePackagePath(const FString& InPath)
	{
		FString Path = InPath.TrimStartAndEnd();
		if (Path.IsEmpty())
		{
			Path = UInputRecordingAssetTools::DefaultPackagePath;
		}
		if (!Path.StartsWith(TEXT("/")))
		{
			Path = TEXT("/Game/") + Path;
		}
		Path.RemoveFromEnd(TEXT("/"));
		return Path;
	}
}

UInputRecordingDataAsset* UInputRecordingAssetTools::GenerateRecordingDataAsset(
	const FString& FileName, bool bJson, const FString& DestinationPath, const FString& AssetName, bool bOpenInEditor)
{
	if (FileName.IsEmpty())
	{
		UE_LOG(LogInputMatch, Error, TEXT("GenerateRecordingDataAsset: no file name supplied."));
		return nullptr;
	}

	// Fail before touching the asset database if the source is not readable.
	FInputRecording Probe;
	FString LoadError;
	if (!UInputReplaySerializer::Load(Probe, FileName, bJson, LoadError))
	{
		UE_LOG(LogInputMatch, Error, TEXT("GenerateRecordingDataAsset: %s"), *LoadError);
		return nullptr;
	}

	const FString FinalAssetName = AssetName.IsEmpty() ? MakeAssetName(FileName) : MakeAssetName(AssetName);
	const FString PackageName    = NormalisePackagePath(DestinationPath) / FinalAssetName;

	if (!FPackageName::IsValidLongPackageName(PackageName))
	{
		UE_LOG(LogInputMatch, Error, TEXT("GenerateRecordingDataAsset: '%s' is not a valid package name."), *PackageName);
		return nullptr;
	}

	// ---- Find an existing asset, or create a fresh one ---------------------------------------
	// Updating in place matters: regenerating after a new take keeps every reference to the asset
	// (level blueprints, tutorial data tables) intact instead of orphaning them.
	UPackage* Package = FindPackage(nullptr, *PackageName);
	if (!Package && FPackageName::DoesPackageExist(PackageName))
	{
		Package = LoadPackage(nullptr, *PackageName, LOAD_None);
	}

	UInputRecordingDataAsset* Asset = nullptr;
	bool bCreated = false;

	if (Package)
	{
		Package->FullyLoad();
		Asset = FindObject<UInputRecordingDataAsset>(Package, *FinalAssetName);
	}

	if (!Asset)
	{
		Package = CreatePackage(*PackageName);
		if (!Package)
		{
			UE_LOG(LogInputMatch, Error, TEXT("GenerateRecordingDataAsset: could not create package '%s'."), *PackageName);
			return nullptr;
		}

		Asset = NewObject<UInputRecordingDataAsset>(
			Package, UInputRecordingDataAsset::StaticClass(), *FinalAssetName,
			RF_Public | RF_Standalone | RF_Transactional);

		if (!Asset)
		{
			UE_LOG(LogInputMatch, Error, TEXT("GenerateRecordingDataAsset: could not create asset '%s'."), *FinalAssetName);
			return nullptr;
		}

		bCreated = true;
	}

	// ---- Fill it in --------------------------------------------------------------------------
	Asset->SourceFileName = FileName;
	Asset->bSourceIsJson  = bJson;
	Asset->PopulateFromRecording(Probe);
	Asset->ResolvedSourcePath = UInputReplaySerializer::ResolveRecordingPath(FileName, bJson);
	Asset->LastImportError.Reset();

	if (bCreated)
	{
		// Tell the Content Browser the asset exists before we save, so it appears even if the save
		// is deferred or the user cancels a source-control prompt.
		FAssetRegistryModule::AssetCreated(Asset);
	}

	Asset->MarkPackageDirty();
	Package->MarkPackageDirty();

	// ---- Write the .uasset -------------------------------------------------------------------
	const FString PackageFileName = FPackageName::LongPackageNameToFilename(
		PackageName, FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags     = SAVE_NoError;
	SaveArgs.Error         = GError;

	// UPackage::SavePackage returns bool; UPackage::Save is the overload that returns
	// FSavePackageResultStruct. We only need success/failure here.
	const bool bSaved = UPackage::SavePackage(Package, Asset, *PackageFileName, SaveArgs);
	if (!bSaved)
	{
		// The asset still exists in memory and in the Content Browser, so this is a warning, not a
		// hard failure - the user can save it by hand.
		UE_LOG(LogInputMatch, Warning,
			TEXT("GenerateRecordingDataAsset: '%s' was populated but could not be written to '%s'. Save it manually."),
			*FinalAssetName, *PackageFileName);
	}

	UE_LOG(LogInputMatch, Log, TEXT("%s '%s' from '%s': %d sample(s), %d cue(s), %.2fs."),
		bCreated ? TEXT("Created") : TEXT("Updated"),
		*PackageName, *Asset->ResolvedSourcePath,
		Asset->SampleCount, Asset->MatchInputCues.Num(), Asset->DurationSeconds);

	if (bOpenInEditor && GEditor)
	{
		if (UAssetEditorSubsystem* AssetEditor = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			AssetEditor->OpenEditorForAsset(Asset);
		}
	}

	return Asset;
}

int32 UInputRecordingAssetTools::GenerateDataAssetsForAllRecordings(bool bJson, const FString& DestinationPath)
{
	const TArray<FString> Recordings = UInputReplaySerializer::ListRecordings(bJson);
	if (Recordings.Num() == 0)
	{
		UE_LOG(LogInputMatch, Warning, TEXT("No %s recordings found in '%s'."),
			bJson ? TEXT("JSON") : TEXT("binary"), *UInputReplaySerializer::GetRecordingDirectory());
		return 0;
	}

	int32 Count = 0;
	for (const FString& Name : Recordings)
	{
		if (GenerateRecordingDataAsset(Name, bJson, DestinationPath, TEXT(""), /*bOpenInEditor=*/false))
		{
			++Count;
		}
	}

	UE_LOG(LogInputMatch, Log, TEXT("Generated %d of %d recording Data Asset(s) into '%s'."),
		Count, Recordings.Num(), *NormalisePackagePath(DestinationPath));
	return Count;
}

// ---------------------------------------------------------------------------------------------
// Console commands
// ---------------------------------------------------------------------------------------------
// Registered unconditionally in editor builds so they work from the Output Log's Cmd box without
// needing to be in PIE.

namespace
{
	bool ArgsRequestJson(const TArray<FString>& Args, int32 FirstFlagIndex)
	{
		for (int32 Index = FirstFlagIndex; Index < Args.Num(); ++Index)
		{
			if (Args[Index].Equals(TEXT("json"), ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	FAutoConsoleCommand GGenerateDataAssetCmd(
		TEXT("InputReplay.GenerateDataAsset"),
		TEXT("Generate an Input Recording Data Asset from a saved recording. Usage: InputReplay.GenerateDataAsset <Name> [json] [/Game/Path]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() == 0)
			{
				UE_LOG(LogInputMatch, Warning,
					TEXT("Usage: InputReplay.GenerateDataAsset <RecordingName> [json] [/Game/DestinationPath]"));
				return;
			}

			FString Destination = UInputRecordingAssetTools::DefaultPackagePath;
			for (int32 Index = 1; Index < Args.Num(); ++Index)
			{
				if (Args[Index].StartsWith(TEXT("/")))
				{
					Destination = Args[Index];
				}
			}

			UInputRecordingAssetTools::GenerateRecordingDataAsset(
				Args[0], ArgsRequestJson(Args, 1), Destination, TEXT(""), /*bOpenInEditor=*/true);
		}));

	FAutoConsoleCommand GGenerateAllDataAssetsCmd(
		TEXT("InputReplay.GenerateAllDataAssets"),
		TEXT("Generate a Data Asset for every saved recording. Usage: InputReplay.GenerateAllDataAssets [json] [/Game/Path]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			FString Destination = UInputRecordingAssetTools::DefaultPackagePath;
			for (const FString& Arg : Args)
			{
				if (Arg.StartsWith(TEXT("/")))
				{
					Destination = Arg;
				}
			}

			UInputRecordingAssetTools::GenerateDataAssetsForAllRecordings(ArgsRequestJson(Args, 0), Destination);
		}));

	FAutoConsoleCommand GListRecordingsCmd(
		TEXT("InputReplay.ListRecordings"),
		TEXT("List the input recordings on disk. Usage: InputReplay.ListRecordings [json]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const bool bJson = ArgsRequestJson(Args, 0);
			const TArray<FString> Recordings = UInputReplaySerializer::ListRecordings(bJson);

			UE_LOG(LogInputMatch, Log, TEXT("%d %s recording(s) in '%s':"),
				Recordings.Num(), bJson ? TEXT("JSON") : TEXT("binary"),
				*UInputReplaySerializer::GetRecordingDirectory());

			for (const FString& Name : Recordings)
			{
				UE_LOG(LogInputMatch, Log, TEXT("  %s"), *Name);
			}
		}));
}

#else // !WITH_EDITOR

UInputRecordingDataAsset* UInputRecordingAssetTools::GenerateRecordingDataAsset(
	const FString& FileName, bool bJson, const FString& DestinationPath, const FString& AssetName, bool bOpenInEditor)
{
	UE_LOG(LogInputMatch, Error,
		TEXT("GenerateRecordingDataAsset is editor-only: a cooked build cannot create .uasset files. ")
		TEXT("Load the recording with UInputReplayComponent::LoadRecordingFromFile instead."));
	return nullptr;
}

int32 UInputRecordingAssetTools::GenerateDataAssetsForAllRecordings(bool bJson, const FString& DestinationPath)
{
	UE_LOG(LogInputMatch, Error, TEXT("GenerateDataAssetsForAllRecordings is editor-only."));
	return 0;
}

#endif // WITH_EDITOR
