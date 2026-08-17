// Copyright (c) Your Studio. All Rights Reserved.

#include "ReplayPlayerController.h"

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
}

void AReplayPlayerController::ReplayExportJson(const FString& FileName)
{
	if (ReplayComponent)
	{
		ReplayComponent->SaveRecordingToFile(FileName, /*bAsJson=*/true);
	}
}
