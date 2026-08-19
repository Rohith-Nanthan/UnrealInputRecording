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
		//     Video/InputRecordingScreenRecorder.cpp -> #include "Video/VideoEncoderBackend.h"
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",

			// UMG      - UInputRecordingHudWidget / UVideoMatchPlayerWidget derive from UUserWidget in
			//            public headers.
			// DeveloperSettings - UInputRecordingSettings derives from UDeveloperSettings.
			"UMG", "DeveloperSettings",

			// Slate/SlateCore back FSlateBrush, which UInputActionIconMappingDataAsset exposes as a
			// UPROPERTY in a public header. UMG pulls these in transitively; listing them is explicit.
			"Slate", "SlateCore",

			// MediaAssets - UMediaPlayer / UMediaTexture are TObjectPtr members of
			//               UInputRecordingVideoPlayer's public header.
			"MediaAssets",

			// MediaIOCore - UMediaOutput and UMediaCapture, which UInputRecordingMediaOutput and
			//               UInputRecordingMediaCapture derive from in a public header.
			//               ==> ships in the MediaIOFramework plugin, which is NOT enabled by default.
			//                   Add it to UnrealInputRecording.uproject or this module will not link.
			"MediaIOCore",

			// MediaCapture.h includes RHI.h and RenderGraphUtils.h, so anything including our media
			// output header needs these on its include path too.
			"RenderCore", "RHI"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			// Json/JsonUtilities - the .ghost.json companion format, and the recording store's
			//                      Session.json / RecordingIndex.json manifests.
			"Json", "JsonUtilities",

			// ImageWrapper - InputRecordingVideo::DumpBgraFrameToPng, the orientation harness behind
			//                ir.video.dumpframe. PNG specifically: it has one unambiguous row order,
			//                which is the entire point of dumping a frame to look at.
			"ImageWrapper",

			// ApplicationCore - FPlatformApplicationMisc, used to resolve the viewport DPI scale when
			//                   clamping the recording controller overlay to its share of the screen.
			"ApplicationCore",

			// EngineSettings - UGameMapsSettings::SetGameDefaultMap, which is how -ControlRecap boots
			//                  straight into the recap map (Boot/RecordingBootFlags.cpp).
			//                  A monolithic target links this implicitly; the modular editor DLL does
			//                  not, so it has to be named here or only the editor build fails.
			"EngineSettings"
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

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
		{
			// The MP4 encoder backend (Video/VideoEncoderBackend.cpp) uses the Media Foundation sink
			// writer, which does H.264 encoding and MP4 muxing in one object and picks up a hardware
			// encoder when the machine has one. These are OS libraries, not third-party dependencies -
			// nothing needs to be redistributed with the game.
			//
			//   mfplat      - MFStartup, MFCreateMediaType, MFCreateSample, MFCreateMemoryBuffer
			//   mfreadwrite - MFCreateSinkWriterFromURL, IMFSinkWriter
			//   mfuuid      - the MF_* / MFVideoFormat_* / MFTranscodeContainerType_* GUID constants
			//   ole32       - CoInitializeEx on the encoder thread
			//
			// The whole file is inside #if PLATFORM_WINDOWS; on other platforms
			// IInputRecordingVideoEncoder::Create() returns null and recording proceeds without video.
			PublicSystemLibraries.AddRange(new string[] {
				"mfplat.lib", "mfreadwrite.lib", "mfuuid.lib", "ole32.lib"
			});
		}
	}
}
