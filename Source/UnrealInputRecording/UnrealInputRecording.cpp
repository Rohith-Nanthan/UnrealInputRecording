// Fill out your copyright notice in the Description page of Project Settings.

#include "UnrealInputRecording.h"

#include "Boot/RecordingBootFlags.h"
#include "Modules/ModuleManager.h"

/**
 * The module exists as a real class for exactly one reason: -ControlRecap has to rewrite the default
 * map before the engine chooses one, and StartupModule is the last point at which that is still
 * possible. Everything else in this system is driven from the game instance subsystem.
 */
class FUnrealInputRecordingModule final : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		RecordingBootFlags::ApplyStartupMapOverride();
	}

	virtual void ShutdownModule() override
	{
		// The fallback registers a delegate against FCoreUObjectDelegates, which outlives this module.
		// Leaving it bound past shutdown would call into unloaded code on the next map load.
		RecordingBootFlags::UnregisterFallbackTravel();
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FUnrealInputRecordingModule, UnrealInputRecording, "UnrealInputRecording");
