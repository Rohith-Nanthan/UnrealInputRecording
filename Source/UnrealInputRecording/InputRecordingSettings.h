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
#include "Video/InputRecordingVideoTypes.h"

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

	/**
	 * Record everything the contexts reach, or only RecordedActionWhitelist.
	 *
	 * Pushed onto components the subsystem creates, and onto hand-configured ones only when they are
	 * still at the default - see ApplyDefaultsTo.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Recording")
	EInputRecordingFilterMode RecordingFilterMode = EInputRecordingFilterMode::RecordAll;

	/** The only actions recorded when RecordingFilterMode is WhitelistOnly. */
	UPROPERTY(config, EditAnywhere, Category = "Recording",
		meta = (EditCondition = "RecordingFilterMode == EInputRecordingFilterMode::WhitelistOnly"))
	TArray<TSoftObjectPtr<UInputAction>> RecordedActionWhitelist;

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

	//~ Video -----------------------------------------------------------------------------------

	/**
	 * Capture the viewport to an .mp4 alongside every .ghost.
	 *
	 * The two files share a bare name, which is the whole pairing mechanism. Turning this off costs
	 * nothing else - MatchInput runs identically, just without a video panel.
	 *
	 * Requires the Media IO Framework plugin, and a viewport UMediaCapture can resolve: run Standalone
	 * or set Play In to "New Editor Window". Capture is skipped with a warning otherwise.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Video")
	bool bCaptureVideoWithRecording = true;

	/** Open and drive the paired .mp4 when a MatchInput session starts. */
	UPROPERTY(config, EditAnywhere, Category = "Video")
	bool bPlayVideoDuringMatchInput = true;

	/**
	 * Capture the composited back buffer rather than the rendered viewport, so the HUD and any other
	 * Slate on top appears in the video. Usually what you want for a tutorial the player watches back.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Video")
	bool bCaptureVideoIncludingUI = false;

	UPROPERTY(config, EditAnywhere, Category = "Video")
	FInputRecordingVideoOptions VideoOptions;

	//~ Storage ---------------------------------------------------------------------------------

	/**
	 * Ceiling for the whole recording folder, in megabytes.
	 *
	 * Worth doing the arithmetic before changing this: at the default 12000 kbit/s and native capture
	 * resolution, 900 MB is roughly ten minutes of video across every session combined. That is a
	 * budget for the store, not for one take.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Storage", meta = (ClampMin = "50", ClampMax = "51200", DisplayName = "Quota (MB)"))
	int32 QuotaMegabytes = 900;

	/**
	 * Headroom reserved before a take is allowed to start, in megabytes.
	 *
	 * The store evicts until this much is free, so a take begins knowing it has somewhere to go. It is
	 * not a limit on the take: recording stops when the quota is actually reached, not when the
	 * reservation is used up.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Storage", meta = (ClampMin = "16", ClampMax = "8192", DisplayName = "Reserve Per Take (MB)"))
	int32 ReserveMegabytesPerTake = 150;

	/**
	 * Stop a take the moment the store reaches its quota.
	 *
	 * On by default and worth leaving on for console: the alternative is evicting other sessions
	 * mid-write, which risks losing a finished take to protect an unfinished one. The take that gets
	 * stopped is still saved - it is simply shorter than intended, and the toast says so.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Storage")
	bool bStopRecordingWhenQuotaReached = true;

	//~ Control Recap ---------------------------------------------------------------------------

	/** The standalone review map. Booted into directly by -ControlRecap, and opened by the Test button. */
	UPROPERTY(config, EditAnywhere, Category = "Control Recap", meta = (AllowedClasses = "/Script/Engine.World"))
	FSoftObjectPath ControlRecapMap;

	/**
	 * Where Cancel goes from the control recap screen.
	 *
	 * Overridable per level by AControlRecapGameMode::TargetOnCancelMap; this is the fallback when the
	 * game mode leaves it unset. Empty falls back to the project's own default map.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Control Recap", meta = (AllowedClasses = "/Script/Engine.World"))
	FSoftObjectPath GameplayMap;

	/** Mapping context pushed for UI navigation on both widgets. See URecordingUIInputConfig. */
	UPROPERTY(config, EditAnywhere, Category = "Control Recap")
	TSoftObjectPtr<class URecordingUIInputConfig> UIInputConfig;

	//~ UI ---------------------------------------------------------------------------------------

	/**
	 * Action -> sprite mapping used by every recording UI.
	 *
	 * This lives in settings rather than only on the widgets because a widget's own IconMapping is a
	 * per-Blueprint field, and any widget created straight from its C++ class - which is what the
	 * control recap map does - has nobody to fill it in. The result was silent: every icon lookup was
	 * skipped and the markers drew empty brushes, which looks identical to "the sprite is missing"
	 * rather than "the asset was never assigned".
	 *
	 * Widgets prefer their own IconMapping when one is set, and fall back to this. See
	 * UControlRecapWidget::ResolveIconMapping.
	 */
	UPROPERTY(config, EditAnywhere, Category = "UI", meta = (DisplayName = "Input Action Icon Mapping"))
	TSoftObjectPtr<class UInputActionIconMappingDataAsset> IconMapping;

	/** Loads and returns IconMapping, or null. Logs once if the reference will not resolve. */
	class UInputActionIconMappingDataAsset* LoadIconMapping() const;

	/**
	 * Blueprint widget classes for the three recording UIs.
	 *
	 * These live here because the widgets are created from a class by the subsystem and the recap
	 * player controller, not placed by hand - and a UGameInstanceSubsystem has no details panel to
	 * assign them in. Without a config home there would be no way to point the system at a Blueprint
	 * without editing C++.
	 *
	 * All three C++ classes build no widget tree of their own, so leaving one empty means that UI
	 * renders nothing. The loaders below log an error rather than failing silently.
	 */
	UPROPERTY(config, EditAnywhere, Category = "UI", meta = (MetaClass = "/Script/UnrealInputRecording.RecordingControllerWidget"))
	FSoftClassPath RecordingControllerWidgetClass;

	UPROPERTY(config, EditAnywhere, Category = "UI", meta = (MetaClass = "/Script/UnrealInputRecording.ControlRecapWidget"))
	FSoftClassPath ControlRecapWidgetClass;

	UPROPERTY(config, EditAnywhere, Category = "UI", meta = (MetaClass = "/Script/UnrealInputRecording.RecordingToastWidget"))
	FSoftClassPath ToastWidgetClass;

	/**
	 * Loads a configured widget class, falling back to FallbackClass.
	 *
	 * @param ConfiguredClass  one of the three paths above
	 * @param FallbackClass    the C++ class, used only so callers never get null
	 * @param ContextName      named in the log when the path is empty or will not load
	 */
	static UClass* LoadWidgetClass(const FSoftClassPath& ConfiguredClass, UClass* FallbackClass, const TCHAR* ContextName);

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

	/** QuotaMegabytes as bytes. The store works in bytes; the setting is in megabytes for humans. */
	int64 GetQuotaBytes() const;

	int64 GetReserveBytesPerTake() const;

	/**
	 * Pushes these defaults onto a component.
	 *
	 * @param bForce  true  - overwrite everything (used for components the subsystem itself created).
	 *                false - only fill in fields the component leaves empty, so a component set up
	 *                        by hand on AReplayPlayerController keeps its own configuration.
	 */
	void ApplyDefaultsTo(UInputReplayComponent* Component, bool bForce) const;
};
