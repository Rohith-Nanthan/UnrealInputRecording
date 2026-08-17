// Copyright (c) Your Studio. All Rights Reserved.
//
// ReplayPlayerController.h
//
// Owns the replay component and, crucially, forwards the two input hook points.
//
// Why these two functions and not Tick():
//   APlayerController::TickPlayerInput -> ProcessPlayerInput() calls
//       PreProcessInput(DT, bPaused)
//       PlayerInput->ProcessInputStack(...)     <- Enhanced Input evaluates everything here
//       PostProcessInput(DT, bPaused)
//
//   Injecting in PreProcessInput means the injected value is consumed by the SAME frame's
//   evaluation - zero latency. Injecting from an ordinary Tick almost always lands after
//   ProcessInputStack has already run, adding a one-frame delay to the entire replay.
//
//   Sampling in PostProcessInput means we read values that have already been through the
//   action's modifiers and triggers - i.e. exactly what the gameplay bindings received.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "ReplayPlayerController.generated.h"

class UInputReplayComponent;

UCLASS()
class AReplayPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AReplayPlayerController();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Input Replay")
	TObjectPtr<UInputReplayComponent> ReplayComponent;

	//~ Begin APlayerController interface
	virtual void PreProcessInput(const float DeltaTime, const bool bGamePaused) override;
	virtual void PostProcessInput(const float DeltaTime, const bool bGamePaused) override;
	//~ End APlayerController interface

	/** Console: Replay.Record  ->  "ReplayRecord" */
	UFUNCTION(Exec, BlueprintCallable, Category = "Input Replay")
	void ReplayRecord();

	/** Console: "ReplayStopAndSave Lap01" */
	UFUNCTION(Exec, BlueprintCallable, Category = "Input Replay")
	void ReplayStopAndSave(const FString& FileName = TEXT("Recording01"));

	/** Console: "ReplayLoadAndPlay Lap01" */
	UFUNCTION(Exec, BlueprintCallable, Category = "Input Replay")
	void ReplayLoadAndPlay(const FString& FileName = TEXT("Recording01"));

	/** Console: "ReplayStop" */
	UFUNCTION(Exec, BlueprintCallable, Category = "Input Replay")
	void ReplayStop();

	/** Console: "ReplayExportJson Lap01" - re-saves the in-memory recording as readable JSON. */
	UFUNCTION(Exec, BlueprintCallable, Category = "Input Replay")
	void ReplayExportJson(const FString& FileName = TEXT("Recording01"));
};
