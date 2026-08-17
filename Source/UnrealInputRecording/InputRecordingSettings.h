// Copyright (c) Your Studio. All Rights Reserved.
//
// InputRecordingSettings.h
//
// Project-wide defaults for the recording system, surfaced at
//     Project Settings > Game > Input Recording
// and stored in DefaultGame.ini.
//
// Why this exists: UInputRecordingSubsystem can create a UInputReplayComponent on the fly for
// projects whose PlayerController is not AReplayPlayerController. A component created in code has no
// designer to fill in RecordedContexts, so it has to get those from somewhere - here.
//
// A component that was configured by hand in the editor always wins; see ApplyDefaultsTo.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "InputReplay/InputMatchCue.h"
#include "InputReplay/InputReplayTypes.h"

#include "InputRecordingSettings.generated.h"

class UInputAction;
class UInputMappingContext;
class UInputReplayComponent;

UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Input Recording"))
class UNREALINPUTRECORDING_API UInputRecordingSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UInputRecordingSettings();

	/** Puts the page under the "Game" heading in Project Settings. */
	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }

	//~ Recording -------------------------------------------------------------------------------

	/**
	 * Mapping contexts whose actions get recorded. For the Third Person template this is IMC_Default
	 * (and IMC_MouseLook if you use it).
	 */
	UPROPERTY(config, EditAnywhere, Category = "Recording")
	TArray<TSoftObjectPtr<UInputMappingContext>> RecordedContexts;

	/** Actions to record that no context references (injected-only actions, debug actions). */
	UPROPERTY(config, EditAnywhere, Category = "Recording")
	TArray<TSoftObjectPtr<UInputAction>> AdditionalActions;

	/**
	 * Actions whose value is a per-frame *delta* rather than a rate: mouse XY, scroll wheel.
	 * For the Third Person template this is IA_MouseLook.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Recording")
	TArray<TSoftObjectPtr<UInputAction>> FrameDeltaActions;

	UPROPERTY(config, EditAnywhere, Category = "Recording")
	EInputReplayTimeMode TimeMode = EInputReplayTimeMode::FixedLogicalStep;

	UPROPERTY(config, EditAnywhere, Category = "Recording", meta = (ClampMin = "10", ClampMax = "480"))
	int32 LogicalTicksPerSecond = 60;

	/** File name used when the UI or subsystem is not given one explicitly. */
	UPROPERTY(config, EditAnywhere, Category = "Recording")
	FString DefaultRecordingName = TEXT("MatchTutorial");

	/**
	 * Also write a .ghost.json alongside the binary .ghost on save. The binary file stays the one
	 * used for playback (JSON floats do not round-trip bit-exactly); the JSON is for reading,
	 * diffing and importing into a Data Asset.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Recording")
	bool bAlsoExportJsonOnSave = true;

	//~ MatchInput ------------------------------------------------------------------------------

	/**
	 * Cue extraction defaults. Add your analog look actions to IgnoredActions - for the Third Person
	 * template, IA_Look and IA_MouseLook - or camera movement will register as wrong input.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Match Input")
	FMatchInputCueBuildOptions MatchCueOptions;

	/** Axis direction agreement required to satisfy a cue. Dot product; 0.7 ~= within 45 degrees. */
	UPROPERTY(config, EditAnywhere, Category = "Match Input", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float MatchDirectionTolerance = 0.7f;

	//~ Subsystem -------------------------------------------------------------------------------

	/**
	 * Let UInputRecordingSubsystem add a UInputReplayComponent to the PlayerController when it
	 * cannot find one. Turn this off if you always use AReplayPlayerController.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Subsystem")
	bool bAutoCreateReplayComponent = true;

	//~ Editor ----------------------------------------------------------------------------------

	/** Where generated UInputRecordingDataAssets are written. */
	UPROPERTY(config, EditAnywhere, Category = "Editor")
	FString DataAssetPackagePath = TEXT("/Game/InputRecordings");

	//~ API -------------------------------------------------------------------------------------

	static const UInputRecordingSettings* Get();

	/**
	 * Pushes these defaults onto a component.
	 *
	 * @param bForce  true  - overwrite everything (used for components the subsystem itself created).
	 *                false - only fill in fields the component leaves empty, so a component set up
	 *                        by hand on AReplayPlayerController keeps its own configuration.
	 */
	void ApplyDefaultsTo(UInputReplayComponent* Component, bool bForce) const;
};
