// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class UWorld;

/**
 * Plugin runtime module.
 *
 * Exists as a real class rather than FDefaultModuleImpl for one reason: the -IR / -ControlRecap
 * map override has to happen in StartupModule, before the engine picks a map at all. That is
 * also why the descriptor asks for the PreDefault loading phase.
 */
class FInputRecorderModule : public IModuleInterface
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
