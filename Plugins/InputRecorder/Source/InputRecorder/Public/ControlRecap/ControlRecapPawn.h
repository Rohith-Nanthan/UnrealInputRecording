// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "ControlRecapPawn.generated.h"

/**
 * The pawn in the review map. It does nothing, on purpose.
 *
 * ADefaultPawn would have worked visually but it installs default movement bindings, which puts
 * this level one config change away from the player flying the camera around mid-quiz. The right
 * answer is a pawn that does not respond to input, not a pawn whose input is being filtered -
 * filtering is precisely what must never happen here, because MatchInput judges live input.
 */
UCLASS(Blueprintable, BlueprintType)
class INPUTRECORDER_API AControlRecapPawn : public APawn
{
	GENERATED_BODY()

public:
	AControlRecapPawn();
};
