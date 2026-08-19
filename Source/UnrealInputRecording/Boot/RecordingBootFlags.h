// Copyright (c) Your Studio. All Rights Reserved.
//
// RecordingBootFlags.h
//
// Command-line entry into the control recap map.
//
//     UnrealInputRecording.exe                              normal boot, gameplay map
//     UnrealInputRecording.exe -IR=0                        normal boot, said explicitly
//     UnrealInputRecording.exe -IR=1                        recap map, most recent session
//     UnrealInputRecording.exe -ControlRecap                recap map, most recent session
//     UnrealInputRecording.exe -ControlRecap=Recording_5    recap map, that session
//     UnrealInputRecording.exe -IR=1 -ControlRecap=5        recap map, that session
//     UnrealInputRecording.exe -RecordingRoot=D:/Takes      read sessions from somewhere else
//
// -IR IS THE SHORT FORM, -ControlRecap THE EXPLICIT ONE
//   They mean the same thing and either alone boots the recap map. -IR exists so a desktop shortcut
//   or a CI job can flip between the two boot paths by changing one character, which is why -IR=0 is
//   spelled out as a value rather than left as "just omit the flag": a launcher that always appends
//   -IR=<n> needs a value that means "boot the game normally". An explicit -IR=0 also *suppresses*
//   -ControlRecap, so that same launcher can override a shortcut that has the long flag baked in.
//
// WHY THE DEFAULT MAP IS REWRITTEN RATHER THAN TRAVELLED TO
//   The obvious implementation - wait for the game to boot, then OpenLevel - loads the gameplay map
//   first: its actors spawn, its game mode runs, and the player sees a frame or two of a level they
//   did not ask for. Rewriting GameDefaultMap during module startup happens before the engine picks
//   a map to load at all, so the recap map is simply the map the game boots. Nothing loads twice.
//
//   ApplyStartupMapOverride() is called from the module's StartupModule for that reason, and the
//   timing is the only fragile part of it - hence the fallback travel in the same file, which does
//   nothing at all unless the override missed its window.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "RecordingBootFlags.generated.h"

class UWorld;

/** What the command line asked the game to boot into. Parsed once, from -IR=<n>. */
UENUM(BlueprintType)
enum class ERecordingBootMode : uint8
{
	/** -IR=0, or no -IR at all: the project's normal gameplay map. */
	Normal			UMETA(DisplayName = "Normal (-IR=0)"),

	/** -IR=1: the standalone review map, reviewing the most recent session. */
	ControlRecap	UMETA(DisplayName = "Control Recap (-IR=1)"),
};

namespace RecordingBootFlags
{
	/** Boot mode from -IR=<n>. Normal when the flag is absent or unparseable. */
	UNREALINPUTRECORDING_API ERecordingBootMode GetBootMode();

	/** True when -IR was actually on the command line, in either the valued or bare form. */
	UNREALINPUTRECORDING_API bool WasBootModeSpecified();

	/** True when -IR=1 or -ControlRecap was passed. An explicit -IR=0 forces this to false. */
	UNREALINPUTRECORDING_API bool IsControlRecapRequested();

	/**
	 * Session folder named by -ControlRecap=<name>, or empty for "whichever was updated last".
	 * Accepts either "Recording_5" or a bare "5".
	 */
	UNREALINPUTRECORDING_API FString GetRequestedSessionFolder();

	/**
	 * True when the recap map should ignore anything the level pinned and review the newest take:
	 * -IR=1 with no session named on the command line. This is the "just show me what I last
	 * recorded" path that -IR=1 exists for.
	 */
	UNREALINPUTRECORDING_API bool ShouldForceMostRecentSession();

	/** One line describing what the flags resolved to. For logs and for the recap widget's header. */
	UNREALINPUTRECORDING_API FString DescribeBootFlags();

	/**
	 * Points GameDefaultMap at the control recap map when the flag is set. Call once, early, from
	 * module startup - it has no effect once the engine has chosen a map.
	 */
	void ApplyStartupMapOverride();

	/**
	 * Safety net for the case where ApplyStartupMapOverride ran too late and the gameplay map loaded
	 * anyway. Registered on PostLoadMapWithWorld and unregisters itself after the first map.
	 */
	void RegisterFallbackTravel();
	void UnregisterFallbackTravel();
}

/**
 * Blueprint face of the boot flags.
 *
 * Menus need this: a main menu that shows a "Review last recording" button only when the build was
 * launched for review, or a HUD that hides itself in recap mode, cannot reach a C++ namespace.
 */
UCLASS(meta = (DisplayName = "Recording Boot Flags"))
class UNREALINPUTRECORDING_API URecordingBootLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Boot mode from -IR=<n>. Normal when the flag is absent. */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Boot")
	static ERecordingBootMode GetBootMode() { return RecordingBootFlags::GetBootMode(); }

	/** True when -IR was on the command line at all, whatever its value. */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Boot")
	static bool WasBootModeSpecified() { return RecordingBootFlags::WasBootModeSpecified(); }

	/** True when this process was launched to review a recording rather than to play. */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Boot")
	static bool IsControlRecapBoot() { return RecordingBootFlags::IsControlRecapRequested(); }

	/** Session folder asked for by name, or empty when the command line did not name one. */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Boot")
	static FString GetRequestedSessionFolder() { return RecordingBootFlags::GetRequestedSessionFolder(); }

	/** True when the newest session should be reviewed regardless of what the level pinned. */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Boot")
	static bool ShouldForceMostRecentSession() { return RecordingBootFlags::ShouldForceMostRecentSession(); }

	/** Human-readable summary of the parsed flags, for debug text on screen. */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Boot")
	static FString DescribeBootFlags() { return RecordingBootFlags::DescribeBootFlags(); }
};
