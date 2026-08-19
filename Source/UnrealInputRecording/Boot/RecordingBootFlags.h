// Copyright (c) Your Studio. All Rights Reserved.
//
// RecordingBootFlags.h
//
// Command-line entry into the control recap map.
//
//     UnrealInputRecording.exe -ControlRecap                 most recently updated session
//     UnrealInputRecording.exe -ControlRecap=Recording_5     that session
//     UnrealInputRecording.exe -RecordingRoot=D:/Takes       read sessions from somewhere else
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

class UWorld;

namespace RecordingBootFlags
{
	/** True when -ControlRecap was passed, with or without a session name. */
	UNREALINPUTRECORDING_API bool IsControlRecapRequested();

	/**
	 * Session folder named by -ControlRecap=<name>, or empty for "whichever was updated last".
	 * Accepts either "Recording_5" or a bare "5".
	 */
	UNREALINPUTRECORDING_API FString GetRequestedSessionFolder();

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
