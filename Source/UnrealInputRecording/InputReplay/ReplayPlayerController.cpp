// Copyright (c) Your Studio. All Rights Reserved.

#include "ReplayPlayerController.h"

#include "Engine/GameInstance.h"
#include "InputRecordingSubsystem.h"
#include "InputReplayComponent.h"

AReplayPlayerController::AReplayPlayerController()
{
	ReplayComponent = CreateDefaultSubobject<UInputReplayComponent>(TEXT("InputReplayComponent"));
}

void AReplayPlayerController::PreProcessInput(const float DeltaTime, const bool bGamePaused)
{
	Super::PreProcessInput(DeltaTime, bGamePaused);

	// Inject BEFORE ProcessInputStack so the values land in this frame's evaluation.
	if (ReplayComponent)
	{
		ReplayComponent->TickPreInput(DeltaTime, bGamePaused);
	}
}

void AReplayPlayerController::PostProcessInput(const float DeltaTime, const bool bGamePaused)
{
	Super::PostProcessInput(DeltaTime, bGamePaused);

	// Sample AFTER ProcessInputStack so modifiers and triggers have been applied.
	if (ReplayComponent)
	{
		ReplayComponent->TickPostInput(DeltaTime, bGamePaused);
	}
}

void AReplayPlayerController::ReplayRecord()
{
	if (ReplayComponent)
	{
		ReplayComponent->StartRecording(TEXT("Console Recording"));
	}
}

void AReplayPlayerController::ReplayStopAndSave(const FString& FileName)
{
	if (ReplayComponent)
	{
		ReplayComponent->StopRecording();
		ReplayComponent->SaveRecordingToFile(FileName, /*bAsJson=*/false);
	}
}

void AReplayPlayerController::ReplayLoadAndPlay(const FString& FileName)
{
	if (ReplayComponent && ReplayComponent->LoadRecordingFromFile(FileName, /*bAsJson=*/false))
	{
		ReplayComponent->StartPlayback();
	}
}

void AReplayPlayerController::ReplayMatchInput(const FString& FileName)
{
	UGameInstance* GameInstance = GetGameInstance();
	UInputRecordingSubsystem* Subsystem = GameInstance ? GameInstance->GetSubsystem<UInputRecordingSubsystem>() : nullptr;

	if (Subsystem)
	{
		// Make sure the subsystem is pointed at *this* controller's component before it resolves one
		// by search - relevant with splitscreen or a spare component elsewhere in the level.
		if (ReplayComponent)
		{
			Subsystem->SetReplayComponent(ReplayComponent);
		}

		Subsystem->StartMatchInputMode(FileName);
		return;
	}

	// Subsystem unavailable (very early startup): fall back to driving the component directly.
	if (ReplayComponent && ReplayComponent->LoadRecordingFromFile(FileName, /*bAsJson=*/false))
	{
		ReplayComponent->StartMatchInput();
	}
}

void AReplayPlayerController::ReplayStop()
{
	if (!ReplayComponent)
	{
		return;
	}

	if (ReplayComponent->IsPlaying())
	{
		ReplayComponent->StopPlayback();
	}
	else if (ReplayComponent->IsRecording())
	{
		ReplayComponent->StopRecording();
	}
	else if (ReplayComponent->IsMatchingInput())
	{
		ReplayComponent->StopMatchInput();
	}
}

void AReplayPlayerController::ReplayExportJson(const FString& FileName)
{
	if (ReplayComponent)
	{
		ReplayComponent->SaveRecordingToFile(FileName, /*bAsJson=*/true);
	}
}
