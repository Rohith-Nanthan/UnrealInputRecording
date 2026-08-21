// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

/**
 * Empty primary game module. The input recording system lives in Plugins/InputRecorder and is
 * pulled in by the .uproject's plugin list, not by a dependency here - depending on it from the
 * game module would defeat the point of the plugin being self-contained.
 */
public class UnrealInputRecording : ModuleRules
{
	public UnrealInputRecording(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});
	}
}
