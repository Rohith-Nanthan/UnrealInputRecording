// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ControlRecapGameMode.generated.h"

/**
 * Game mode for the standalone review map.
 *
 * The review surface needs its own game mode rather than a widget pushed over the gameplay HUD
 * because it has controller-level concerns - input mode, pawn state, level travel on exit - that
 * a widget cannot own cleanly.
 *
 * Create BP_ControlRecapGameMode as a child of this and set that as the map's game mode override.
 */
UCLASS(Blueprintable)
class INPUTRECORDER_API AControlRecapGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AControlRecapGameMode();

	/** Where Cancel goes. Falls back to the project setting's gameplay map, then the engine default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control Recap", meta = (AllowedClasses = "/Script/Engine.World"))
	FSoftObjectPath TargetOnCancelMap;

	/**
	 * Pins this level to one take instead of "most recent". A command-line flag deliberately
	 * outranks this: the pin is a design-time choice baked into a map, and somebody typing a
	 * flag into a terminal is overriding it on purpose for this run.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control Recap")
	FString ForcedSessionFolder;
};
