// Copyright Epic Games, Inc. All Rights Reserved.

#include "Settings/InputRecordingSettings.h"

#include "Blueprint/UserWidget.h"
#include "InputRecordingLog.h"
#include "InputReplay/InputReplayComponent.h"
#include "Settings/InputIconMapping.h"
#include "Settings/RecordingUIInputConfig.h"

UInputRecordingSettings::UInputRecordingSettings()
{
	// Every default points at content this plugin ships, so dropping it into a project that has
	// never heard of it yields a working recorder with no .ini edits at all. These are only
	// defaults: a host project overrides any of them from its own DefaultGame.ini as usual.
	//
	// Leaving the widget classes empty instead would technically "work" - the code falls back to
	// the raw C++ classes - but those build no widget tree, so the user gets blank surfaces and
	// an error in the log. Empty is not a usable default for a drag-and-drop plugin.
	ControlRecapMap = FSoftObjectPath(InputRecorderDefaults::ControlRecapMapPath);

	UIInputConfig = TSoftObjectPtr<URecordingUIInputConfig>(
		FSoftObjectPath(TEXT("/InputRecorder/DataAssets/DA_RecordingUIInput.DA_RecordingUIInput")));
	IconMapping = TSoftObjectPtr<UInputIconMapping>(
		FSoftObjectPath(TEXT("/InputRecorder/DataAssets/DA_InputIcons.DA_InputIcons")));

	OverlayWidgetClass          = FSoftClassPath(TEXT("/InputRecorder/Widgets/WBP_InputRecorderOverlay.WBP_InputRecorderOverlay_C"));
	SyncPointRowWidgetClass     = FSoftClassPath(TEXT("/InputRecorder/Widgets/WBP_SyncPointRow.WBP_SyncPointRow_C"));
	ControlRecapWidgetClass     = FSoftClassPath(TEXT("/InputRecorder/Widgets/WBP_ControlRecap.WBP_ControlRecap_C"));
	VideoSurfaceWidgetClass     = FSoftClassPath(TEXT("/InputRecorder/Widgets/WBP_VideoSurface.WBP_VideoSurface_C"));
	MatchCueMarkerWidgetClass   = FSoftClassPath(TEXT("/InputRecorder/Widgets/WBP_MatchCueMarker.WBP_MatchCueMarker_C"));
	WrongInputRowWidgetClass    = FSoftClassPath(TEXT("/InputRecorder/Widgets/WBP_WrongInputRow.WBP_WrongInputRow_C"));
	RecordingListWidgetClass    = FSoftClassPath(TEXT("/InputRecorder/Widgets/WBP_RecordingList.WBP_RecordingList_C"));
	RecordingListRowWidgetClass = FSoftClassPath(TEXT("/InputRecorder/Widgets/WBP_RecordingListRow.WBP_RecordingListRow_C"));
	RecordingToastWidgetClass   = FSoftClassPath(TEXT("/InputRecorder/Widgets/WBP_RecordingToast.WBP_RecordingToast_C"));

	// GameplayMap is deliberately left empty. It names *host* content - where "cancel" returns to
	// after a review - and the plugin has no way to guess that. Empty is handled: the review map's
	// game mode falls back to its own setting, and the recap map is reachable regardless.
	//
	// RecordedMappingContexts is likewise left empty on purpose. Empty means "record whatever
	// contexts are actually applied to this player", which is what lets the component drop into an
	// unknown project. Naming contexts here would hardcode one project's input assets.
}

void UInputRecordingSettings::ApplyDefaultsTo(UInputReplayComponent* Component, bool bForce) const
{
	if (!Component)
	{
		return;
	}

	if (bForce || Component->RecordedMappingContexts.Num() == 0)
	{
		Component->RecordedMappingContexts = RecordedMappingContexts;
	}

	if (bForce || Component->AdditionalActions.Num() == 0)
	{
		Component->AdditionalActions = AdditionalActions;
	}

	if (bForce || Component->FrameDeltaActions.Num() == 0)
	{
		Component->FrameDeltaActions = FrameDeltaActions;
	}

	if (bForce || Component->RecordedActionWhitelist.Num() == 0)
	{
		Component->RecordedActionWhitelist = RecordedActionWhitelist;
	}

	// Scalars have no "empty" state to test, so they only move under bForce. A component
	// configured by hand keeps whatever the designer set on it.
	if (bForce)
	{
		Component->FilterMode = FilterMode;
		Component->TimeMode = TimeMode;
		Component->LogicalTicksPerSecond = LogicalTicksPerSecond;
		Component->CueBuildOptions = CueBuildOptions;
		Component->MatchDirectionTolerance = MatchDirectionTolerance;
	}
}

UClass* UInputRecordingSettings::ResolveWidgetClass(const FSoftClassPath& Path, UClass* FallbackClass, const TCHAR* SettingName) const
{
	if (Path.IsValid())
	{
		if (UClass* Loaded = Path.TryLoadClass<UUserWidget>())
		{
			return Loaded;
		}

		UE_LOG(LogInputRecording, Error,
			TEXT("Widget class setting '%s' points at '%s', which will not load. Falling back to the C++ class, ")
			TEXT("which builds no widget tree - expect a blank surface until this is fixed."),
			SettingName, *Path.ToString());
	}
	else
	{
		UE_LOG(LogInputRecording, Error,
			TEXT("Widget class setting '%s' is empty. Point it at the WBP_ Blueprint child; the raw C++ class ")
			TEXT("builds no widget tree of its own."),
			SettingName);
	}

	return FallbackClass;
}
