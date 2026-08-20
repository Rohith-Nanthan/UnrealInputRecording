// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UnrealInputRecording : ModuleRules
{
	public UnrealInputRecording(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// This module has a flat layout (no Public/Private split), so UBT does not add the module
		// root to the include path by itself. Without this, cross-subfolder includes such as
		// #include "InputReplay/InputReplayTypes.h" fail to resolve.
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
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
