// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "InputReplay/InputReplayTypes.h"
#include "MatchInput/MatchInputTypes.h"
#include "Video/InputRecordingVideoTypes.h"
#include "InputRecordingSettings.generated.h"

class UInputAction;
class UInputIconMapping;
class UInputMappingContext;
class UInputReplayComponent;
class URecordingUIInputConfig;

namespace InputRecorderDefaults
{
	/**
	 * The review map this plugin ships, as one string in one place.
	 *
	 * Shared deliberately. The settings CDO and the boot-time map override each used to carry
	 * their own copy of this path, and they drifted: the content moved into the plugin, the CDO
	 * was updated, the boot copy was not, and -IR=1 in any project without an explicit
	 * ControlRecapMap in its DefaultGame.ini booted into a map that no longer existed.
	 */
	inline const TCHAR* ControlRecapMapPath = TEXT("/InputRecorder/Maps/ControlRecapLevel.ControlRecapLevel");

	/** Config section backing UInputRecordingSettings, for code that must read it before the CDO exists. */
	inline const TCHAR* SettingsSection = TEXT("/Script/InputRecorder.InputRecordingSettings");
}

/**
 * Everything a designer might tune, in one place and nowhere else.
 *
 * Widget classes live here as FSoftClassPath rather than on the owning object because a
 * UGameInstanceSubsystem and a PlayerController have no details panel to put them in.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Input Recorder"))
class INPUTRECORDER_API UInputRecordingSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UInputRecordingSettings();

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	static const UInputRecordingSettings* Get() { return GetDefault<UInputRecordingSettings>(); }

	// --- Recording ----------------------------------------------------------------------------

	/** Empty means "record whatever mapping contexts are applied to the player at the time". */
	UPROPERTY(config, EditAnywhere, Category = "Recording")
	TArray<TSoftObjectPtr<UInputMappingContext>> RecordedMappingContexts;

	UPROPERTY(config, EditAnywhere, Category = "Recording")
	TArray<TSoftObjectPtr<UInputAction>> AdditionalActions;

	/** Per-frame deltas rather than rates. Never a cue, never a wrong answer. */
	UPROPERTY(config, EditAnywhere, Category = "Recording")
	TArray<TSoftObjectPtr<UInputAction>> FrameDeltaActions;

	UPROPERTY(config, EditAnywhere, Category = "Recording")
	EInputRecordingFilterMode FilterMode = EInputRecordingFilterMode::RecordAll;

	/** Subtractive only - it narrows what the contexts already reach, never adds from outside them. */
	UPROPERTY(config, EditAnywhere, Category = "Recording")
	TArray<FString> RecordedActionWhitelist;

	UPROPERTY(config, EditAnywhere, Category = "Recording")
	EInputReplayTimeMode TimeMode = EInputReplayTimeMode::FixedLogicalStep;

	UPROPERTY(config, EditAnywhere, Category = "Recording", meta = (ClampMin = "1", ClampMax = "240"))
	int32 LogicalTicksPerSecond = 60;

	UPROPERTY(config, EditAnywhere, Category = "Recording")
	FString DefaultRecordingName = TEXT("Recording");

	UPROPERTY(config, EditAnywhere, Category = "Recording")
	bool bAlsoExportJsonOnSave = true;

	// --- Match Input --------------------------------------------------------------------------

	UPROPERTY(config, EditAnywhere, Category = "Match Input")
	FMatchInputCueBuildOptions CueBuildOptions;

	UPROPERTY(config, EditAnywhere, Category = "Match Input", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float MatchDirectionTolerance = 0.7f;

	// --- Video --------------------------------------------------------------------------------

	UPROPERTY(config, EditAnywhere, Category = "Video")
	bool bCaptureVideoWithRecording = true;

	UPROPERTY(config, EditAnywhere, Category = "Video")
	bool bPlayVideoDuringMatchInput = true;

	/**
	 * Viewport capture always includes whatever is drawn over it. When this is false the
	 * subsystem hides its own recorder overlay for the duration of the take, so the review
	 * video shows the game rather than the recording controls.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Video")
	bool bCaptureVideoIncludingUI = false;

	UPROPERTY(config, EditAnywhere, Category = "Video")
	FInputRecordingVideoOptions VideoOptions;

	// --- Storage ------------------------------------------------------------------------------

	UPROPERTY(config, EditAnywhere, Category = "Storage", meta = (ClampMin = "50"))
	int32 QuotaMegabytes = 900;

	UPROPERTY(config, EditAnywhere, Category = "Storage", meta = (ClampMin = "10"))
	int32 ReserveMegabytesPerTake = 150;

	/**
	 * On reaching the quota mid-take, stop the take rather than evicting something else.
	 * Trading a finished recording for an unfinished one is the wrong trade.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Storage")
	bool bStopRecordingWhenQuotaReached = true;

	// --- Control Recap ------------------------------------------------------------------------

	UPROPERTY(config, EditAnywhere, Category = "Control Recap", meta = (AllowedClasses = "/Script/Engine.World"))
	FSoftObjectPath ControlRecapMap;

	/** Where "cancel" goes when the review map's game mode does not name somewhere else. */
	UPROPERTY(config, EditAnywhere, Category = "Control Recap", meta = (AllowedClasses = "/Script/Engine.World"))
	FSoftObjectPath GameplayMap;

	UPROPERTY(config, EditAnywhere, Category = "Control Recap")
	TSoftObjectPtr<URecordingUIInputConfig> UIInputConfig;

	// --- UI -----------------------------------------------------------------------------------

	UPROPERTY(config, EditAnywhere, Category = "UI")
	TSoftObjectPtr<UInputIconMapping> IconMapping;

	UPROPERTY(config, EditAnywhere, Category = "UI", meta = (MetaClass = "/Script/InputRecorder.InputRecorderOverlayWidget"))
	FSoftClassPath OverlayWidgetClass;

	UPROPERTY(config, EditAnywhere, Category = "UI", meta = (MetaClass = "/Script/InputRecorder.SyncPointRowWidget"))
	FSoftClassPath SyncPointRowWidgetClass;

	UPROPERTY(config, EditAnywhere, Category = "UI", meta = (MetaClass = "/Script/InputRecorder.ControlRecapWidget"))
	FSoftClassPath ControlRecapWidgetClass;

	UPROPERTY(config, EditAnywhere, Category = "UI", meta = (MetaClass = "/Script/InputRecorder.VideoSurfaceWidget"))
	FSoftClassPath VideoSurfaceWidgetClass;

	UPROPERTY(config, EditAnywhere, Category = "UI", meta = (MetaClass = "/Script/InputRecorder.MatchCueMarkerWidget"))
	FSoftClassPath MatchCueMarkerWidgetClass;

	UPROPERTY(config, EditAnywhere, Category = "UI", meta = (MetaClass = "/Script/InputRecorder.WrongInputRowWidget"))
	FSoftClassPath WrongInputRowWidgetClass;

	UPROPERTY(config, EditAnywhere, Category = "UI", meta = (MetaClass = "/Script/InputRecorder.RecordingListWidget"))
	FSoftClassPath RecordingListWidgetClass;

	UPROPERTY(config, EditAnywhere, Category = "UI", meta = (MetaClass = "/Script/InputRecorder.RecordingListRowWidget"))
	FSoftClassPath RecordingListRowWidgetClass;

	UPROPERTY(config, EditAnywhere, Category = "UI", meta = (MetaClass = "/Script/InputRecorder.RecordingToastWidget"))
	FSoftClassPath RecordingToastWidgetClass;

	/** Fraction of the review screen the video occupies, driven into a SizeBox from C++. */
	UPROPERTY(config, EditAnywhere, Category = "UI", meta = (ClampMin = "0.1", ClampMax = "0.95"))
	float VideoScreenFraction = 0.65f;

	/**
	 * Cap on the corner overlay as a fraction of screen *area*. Capping width and height
	 * independently is what makes the panel eat an ultrawide screen - the two constraints
	 * disagree at unusual aspect ratios.
	 */
	UPROPERTY(config, EditAnywhere, Category = "UI", meta = (ClampMin = "0.02", ClampMax = "0.6"))
	float OverlayMaxScreenAreaFraction = 0.16f;

	// --- Subsystem ----------------------------------------------------------------------------

	UPROPERTY(config, EditAnywhere, Category = "Subsystem")
	bool bAutoCreateReplayComponent = true;

	// --- Helpers ------------------------------------------------------------------------------

	int64 GetQuotaBytes() const { return static_cast<int64>(FMath::Max(1, QuotaMegabytes)) * 1024 * 1024; }
	int64 GetReserveBytesPerTake() const { return static_cast<int64>(FMath::Max(1, ReserveMegabytesPerTake)) * 1024 * 1024; }

	/**
	 * @param bForce true overwrites everything, for a component the subsystem created itself.
	 *               false only fills in fields the component left empty, so a component that was
	 *               configured by hand in the editor keeps its own setup.
	 */
	void ApplyDefaultsTo(UInputReplayComponent* Component, bool bForce) const;

	/**
	 * Loads a widget class, falling back to the C++ base so a caller never gets null, and
	 * logging when a path is empty or will not load.
	 */
	UClass* ResolveWidgetClass(const FSoftClassPath& Path, UClass* FallbackClass, const TCHAR* SettingName) const;
};
