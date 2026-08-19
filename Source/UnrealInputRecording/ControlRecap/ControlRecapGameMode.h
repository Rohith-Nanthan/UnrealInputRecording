// Copyright (c) Your Studio. All Rights Reserved.
//
// ControlRecapGameMode.h
//
// Game mode for ControlRecapLevel: the standalone map where a recording is reviewed.
//
// It exists to make the map self-contained. The recap screen is not a HUD bolted onto gameplay - it
// is its own mode with no pawn worth controlling, no gameplay bindings, and its own idea of what
// Cancel means. Putting that in a game mode rather than in the widget means the map can be booted
// into directly by -ControlRecap without anything else in the project being involved.
//
// Subclass it in Blueprint to set TargetOnCancelMap per level.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "ControlRecapGameMode.generated.h"

UCLASS(Blueprintable, meta = (DisplayName = "Control Recap Game Mode"))
class UNREALINPUTRECORDING_API AControlRecapGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AControlRecapGameMode();

	/**
	 * Where Cancel goes.
	 *
	 * Left unset, the player controller falls back to the project's Gameplay Map setting, and then to
	 * the engine's own default map. Exposed here so a Blueprint subclass of this game mode can point
	 * one recap level at a different destination without touching project settings.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control Recap", meta = (AllowedClasses = "/Script/Engine.World"))
	FSoftObjectPath TargetOnCancelMap;

	/**
	 * Session folder to review, e.g. "Recording_5".
	 *
	 * Normally empty, which means "whichever session was updated most recently" - that is what both
	 * the Test button and a bare -ControlRecap want. Set it to pin a level at one specific take.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control Recap")
	FString ForcedSessionFolder;
};
