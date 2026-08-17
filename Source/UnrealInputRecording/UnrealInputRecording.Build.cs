// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class UnrealInputRecording : ModuleRules
{
	public UnrealInputRecording(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// This module uses a flat layout (no Public/Private split), so UBT does not add the module
		// root to the include path automatically. Adding it explicitly is what lets files in one
		// subfolder include another by module-relative path, e.g.
		//     UI/InputRecordingHudWidget.h        -> #include "InputReplay/InputReplayTypes.h"
		//     InputReplay/ReplayPlayerController.cpp -> #include "InputRecordingSubsystem.h"
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",

			// UMG      - UInputRecordingHudWidget derives from UUserWidget in a public header.
			// DeveloperSettings - UInputRecordingSettings derives from UDeveloperSettings.
			"UMG", "DeveloperSettings"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Json", "JsonUtilities",

			// Slate/SlateCore back the widget's FText and Button/TextBlock usage.
			"Slate", "SlateCore"
		});

		if (Target.bBuildEditor)
		{
			// Creating .uasset files is editor-only, so UInputRecordingAssetTools' implementation is
			// wrapped in WITH_EDITOR and these modules are only linked for editor targets. A cooked
			// build keeps working; the generation functions just log and return null.
			//   UnrealEd      - GEditor, UAssetEditorSubsystem, UPackage::SavePackage.
			//   AssetRegistry - FAssetRegistryModule::AssetCreated.
			PrivateDependencyModuleNames.AddRange(new string[] {
				"UnrealEd", "AssetRegistry"
			});
		}

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
