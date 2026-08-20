// Copyright Epic Games, Inc. All Rights Reserved.

#include "InputReplay/RecordingPlayerController.h"

#include "Engine/GameInstance.h"
#include "InputReplay/InputReplayComponent.h"
#include "Subsystem/InputRecordingSubsystem.h"

UInputReplayComponent* ARecordingPlayerController::ResolveReplayComponent()
{
	if (UInputReplayComponent* Cached = CachedReplayComponent.Get())
	{
		return Cached;
	}

	// Through the subsystem, so the auto-create and settings-application rules stay in one place.
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UInputRecordingSubsystem* Subsystem = GameInstance->GetSubsystem<UInputRecordingSubsystem>())
		{
			CachedReplayComponent = Subsystem->ResolveReplayComponent();
		}
	}

	return CachedReplayComponent.Get();
}

void ARecordingPlayerController::PreProcessInput(const float DeltaTime, const bool bGamePaused)
{
	Super::PreProcessInput(DeltaTime, bGamePaused);

	if (UInputReplayComponent* Component = ResolveReplayComponent())
	{
		Component->HandlePreProcessInput(DeltaTime);
	}
}

void ARecordingPlayerController::PostProcessInput(const float DeltaTime, const bool bGamePaused)
{
	Super::PostProcessInput(DeltaTime, bGamePaused);

	// After Enhanced Input has evaluated the stack, so values are read post-modifier in the same
	// frame they were produced.
	if (UInputReplayComponent* Component = ResolveReplayComponent())
	{
		Component->HandlePostProcessInput(DeltaTime);
	}
}
