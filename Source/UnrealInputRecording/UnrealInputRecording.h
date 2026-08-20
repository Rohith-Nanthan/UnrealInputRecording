// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class UWorld;

/**
 * Primary game module.
 *
 * Exists as a real class rather than FDefaultGameModuleImpl for one reason: the -IR / -ControlRecap
 * map override has to happen in StartupModule, before the engine picks a map at all.
 */
class FUnrealInputRecordingModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/**
	 * Safety net for the case where module startup somehow ran after map resolution. Logs
	 * loudly, self-unregisters after firing once so it never fights a legitimate later level
	 * change, and unbinds on shutdown.
	 */
	void HandlePostLoadMap(UWorld* LoadedWorld);

	FDelegateHandle PostLoadMapHandle;
	bool bFallbackConsumed = false;
};
