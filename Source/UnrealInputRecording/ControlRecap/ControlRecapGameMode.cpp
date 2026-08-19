// Copyright (c) Your Studio. All Rights Reserved.

#include "ControlRecap/ControlRecapGameMode.h"

#include "ControlRecap/ControlRecapPlayerController.h"
#include "GameFramework/DefaultPawn.h"

AControlRecapGameMode::AControlRecapGameMode()
{
	PlayerControllerClass = AControlRecapPlayerController::StaticClass();

	// A pawn still has to exist for the player controller to possess something, but it should not be
	// able to do anything. ADefaultPawn with input ignored is the cheapest thing that satisfies both -
	// the controller turns movement off in BeginPlay, so this never flies around behind the UI.
	DefaultPawnClass = ADefaultPawn::StaticClass();

	// No HUD: everything on screen is the recap widget, and a stray gameplay HUD drawing underneath it
	// is exactly the kind of thing that only shows up in a packaged build.
	HUDClass = nullptr;

	bStartPlayersAsSpectators = false;
}
