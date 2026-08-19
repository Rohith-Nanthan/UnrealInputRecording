// Copyright (c) Your Studio. All Rights Reserved.

#include "Boot/RecordingBootFlags.h"

#include "Engine/World.h"
#include "GameMapsSettings.h"
#include "InputRecordingSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/Parse.h"
#include "Storage/RecordingSessionTypes.h"

namespace
{
	FDelegateHandle GFallbackTravelHandle;

	/** "/Game/Recording/ControlRecapLevel.ControlRecapLevel" -> "ControlRecapLevel". */
	FString GetMapAssetName(const FSoftObjectPath& MapPath)
	{
		const FString AssetName = MapPath.GetAssetName();
		return AssetName.IsEmpty() ? MapPath.ToString() : AssetName;
	}
}

bool RecordingBootFlags::IsControlRecapRequested()
{
	// Both spellings: -ControlRecap on its own, and -ControlRecap=Recording_5. FParse::Param only
	// matches the bare switch, so the valued form needs its own check.
	if (FParse::Param(FCommandLine::Get(), TEXT("ControlRecap")))
	{
		return true;
	}

	FString Unused;
	return FParse::Value(FCommandLine::Get(), TEXT("ControlRecap="), Unused);
}

FString RecordingBootFlags::GetRequestedSessionFolder()
{
	FString Requested;
	if (!FParse::Value(FCommandLine::Get(), TEXT("ControlRecap="), Requested) || Requested.IsEmpty())
	{
		return FString();
	}

	Requested.TrimStartAndEndInline();

	// Accept a bare index so "-ControlRecap=5" does the obvious thing.
	if (FRecordingSessionInfo::ParseFolderName(Requested) == INDEX_NONE && Requested.IsNumeric())
	{
		return FRecordingSessionInfo::MakeFolderName(FCString::Atoi(*Requested));
	}

	return Requested;
}

void RecordingBootFlags::ApplyStartupMapOverride()
{
	if (!IsControlRecapRequested())
	{
		return;
	}

	const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();
	if (!Settings || Settings->ControlRecapMap.IsNull())
	{
		UE_LOG(LogRecordingStore, Error,
			TEXT("-ControlRecap was passed but no Control Recap Map is set in ")
			TEXT("Project Settings > Game > Input Recording. Booting normally instead."));
		return;
	}

	const FString MapPath = Settings->ControlRecapMap.GetLongPackageName();
	if (MapPath.IsEmpty())
	{
		UE_LOG(LogRecordingStore, Error,
			TEXT("Control Recap Map ('%s') does not resolve to a package name. Booting normally instead."),
			*Settings->ControlRecapMap.ToString());
		return;
	}

	UGameMapsSettings::SetGameDefaultMap(MapPath);

	const FString RequestedSession = GetRequestedSessionFolder();
	UE_LOG(LogRecordingStore, Log, TEXT("-ControlRecap: booting into '%s', loading %s."),
		*MapPath,
		RequestedSession.IsEmpty() ? TEXT("the most recently updated session") : *RequestedSession);

	// The override above is the mechanism that should work. This only matters if module startup ran
	// after the engine had already resolved its startup map.
	RegisterFallbackTravel();
}

void RecordingBootFlags::RegisterFallbackTravel()
{
	if (GFallbackTravelHandle.IsValid())
	{
		return;
	}

	GFallbackTravelHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddLambda([](UWorld* LoadedWorld)
	{
		// One shot, whatever happens: a second firing would fight the player's own level changes.
		UnregisterFallbackTravel();

		if (!LoadedWorld || !IsControlRecapRequested())
		{
			return;
		}

		const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();
		if (!Settings || Settings->ControlRecapMap.IsNull())
		{
			return;
		}

		const FString TargetName = GetMapAssetName(Settings->ControlRecapMap);

		// Already where we wanted to be - the startup override did its job and there is nothing to do.
		if (LoadedWorld->GetMapName().EndsWith(TargetName, ESearchCase::IgnoreCase))
		{
			return;
		}

		UE_LOG(LogRecordingStore, Warning,
			TEXT("-ControlRecap: the startup map override missed its window and '%s' loaded instead. ")
			TEXT("Travelling to '%s' now."),
			*LoadedWorld->GetMapName(), *TargetName);

		UGameplayStatics::OpenLevelBySoftObjectPtr(LoadedWorld, TSoftObjectPtr<UWorld>(Settings->ControlRecapMap));
	});
}

void RecordingBootFlags::UnregisterFallbackTravel()
{
	if (GFallbackTravelHandle.IsValid())
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(GFallbackTravelHandle);
		GFallbackTravelHandle.Reset();
	}
}
