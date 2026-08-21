// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRecap/ControlRecapPawn.h"

#include "Components/SceneComponent.h"

AControlRecapPawn::AControlRecapPawn()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// No movement component, no default bindings, no camera manipulation. The review widget is
	// the entire experience of this level.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
}
