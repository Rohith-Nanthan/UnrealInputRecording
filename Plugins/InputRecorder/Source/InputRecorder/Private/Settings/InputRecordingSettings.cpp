// Copyright Epic Games, Inc. All Rights Reserved.

#include "Settings/InputRecordingSettings.h"

#include "Blueprint/UserWidget.h"
#include "InputRecordingLog.h"
#include "InputReplay/InputReplayComponent.h"

UInputRecordingSettings::UInputRecordingSettings()
{
	// Sensible out-of-the-box paths. Everything under /Game/RecordingFolder, nowhere else.
	ControlRecapMap = FSoftObjectPath(TEXT("/Game/RecordingFolder/Maps/ControlRecapLevel.ControlRecapLevel"));
	GameplayMap = FSoftObjectPath(TEXT("/Game/ThirdPerson/Lvl_ThirdPerson.Lvl_ThirdPerson"));
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
