// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRecap/ControlRecapGameMode.h"

#include "ControlRecap/ControlRecapPawn.h"
#include "ControlRecap/ControlRecapPlayerController.h"

AControlRecapGameMode::AControlRecapGameMode()
{
	PlayerControllerClass = AControlRecapPlayerController::StaticClass();

	// A plain non-interactive pawn: the player controls nothing here, they only answer prompts.
	// It is a pawn that does not respond to input, rather than a pawn whose input is being
	// filtered - suppressing input is exactly what must not happen in this level.
	DefaultPawnClass = AControlRecapPawn::StaticClass();

	// No HUD class. Everything on screen is the review widget.
	HUDClass = nullptr;
}
