// Copyright Epic Games, Inc. All Rights Reserved.

#include "InputRecorder.h"

#include "Boot/RecordingBootFlags.h"
#include "Engine/World.h"
#include "InputRecordingLog.h"
#include "Kismet/GameplayStatics.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_MODULE(FInputRecorderModule, InputRecorder);

void FInputRecorderModule::StartupModule()
{
	RecordingBootFlags::InitializeFromCommandLine();

	if (RecordingBootFlags::Get().Mode == ERecordingBootMode::ControlRecap)
	{
		PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddRaw(this, &FInputRecorderModule::HandlePostLoadMap);
	}
}

void FInputRecorderModule::ShutdownModule()
{
	if (PostLoadMapHandle.IsValid())
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
		PostLoadMapHandle.Reset();
	}
}

void FInputRecorderModule::HandlePostLoadMap(UWorld* LoadedWorld)
{
	if (bFallbackConsumed || !LoadedWorld)
	{
		return;
	}

	// One shot only. Beyond the first map load this delegate would start fighting legitimate
	// level changes - including the player deliberately leaving the review map.
	bFallbackConsumed = true;

	const FRecordingBootFlags& Flags = RecordingBootFlags::Get();

	if (Flags.ResolvedMapPath.IsEmpty())
	{
		return;
	}

	const FString LoadedName = LoadedWorld->GetName();
	const FString ExpectedName = FPackageName::GetShortName(FPackageName::ObjectPathToPackageName(Flags.ResolvedMapPath));

	if (LoadedName.Equals(ExpectedName, ESearchCase::IgnoreCase))
	{
		// The map override did its job; nothing to rescue.
		if (PostLoadMapHandle.IsValid())
		{
			FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
			PostLoadMapHandle.Reset();
		}
		return;
	}

	UE_LOG(LogInputRecording, Error,
		TEXT("Boot-time map override did not take effect: expected to land in '%s' but loaded '%s'. ")
		TEXT("Travelling there now, which means a frame or two of the wrong level has already rendered. ")
		TEXT("This indicates StartupModule ran after map resolution."),
		*ExpectedName, *LoadedName);

	UGameplayStatics::OpenLevel(LoadedWorld, FName(*Flags.ResolvedMapPath));

	if (PostLoadMapHandle.IsValid())
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
		PostLoadMapHandle.Reset();
	}
}
