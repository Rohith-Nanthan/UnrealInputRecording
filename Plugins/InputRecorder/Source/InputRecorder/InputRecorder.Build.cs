// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class InputRecorder : ModuleRules
{
	public InputRecorder(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// UBT adds these two by convention, but they are named explicitly because every include
		// in this module is module-root-relative ("InputReplay/InputReplayTypes.h" rather than
		// a sibling-relative path). That only resolves while the module roots are on the include
		// path, so the requirement is worth stating rather than inheriting silently.
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private"));

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			// Public rather than private: UInputReplayComponent's header exposes
			// TSoftObjectPtr<UInputMappingContext> and UInputAction to anything including it.
			"EnhancedInput",
			"UMG",
			"Slate",
			"SlateCore",
			"DeveloperSettings",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// UGameMapsSettings lives in EngineSettings. A monolithic game target links it in
			// anyway; the modular editor DLL does not, so omitting this fails the editor build only.
			"EngineSettings",
			"ApplicationCore",
			"Projects",
			"RenderCore",
			"RHI",
			"Json",
			"JsonUtilities",
			// UMediaOutput / UMediaCapture come from MediaIOCore; UMediaPlayer / UMediaTexture
			// come from MediaAssets. They are different modules and both are needed.
			"Media",
			"MediaAssets",
			"MediaIOCore",
			"MediaUtils",
			"ImageWrapper",
		});

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// Media Foundation sink writer: H.264 encode and MP4 mux in one object, no plugin needed.
			PublicSystemLibraries.AddRange(new string[]
			{
				"mfplat.lib",
				"mfreadwrite.lib",
				"mfuuid.lib",
				"ole32.lib",
			});
		}
	}
}
