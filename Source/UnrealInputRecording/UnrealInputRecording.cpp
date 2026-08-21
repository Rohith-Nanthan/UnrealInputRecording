// Copyright Epic Games, Inc. All Rights Reserved.

#include "UnrealInputRecording.h"

#include "Modules/ModuleManager.h"

// The recorder's own startup work (command-line boot flags, the map override) moved to
// FInputRecorderModule in Plugins/InputRecorder. This module exists only because a game project
// must declare a primary game module and a plugin module cannot serve as one.
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, UnrealInputRecording, "UnrealInputRecording");
