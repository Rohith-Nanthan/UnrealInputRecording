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

	/**
	 * Exact "-Switch=Value" lookup.
	 *
	 * FParse::Value would be the one-liner, but it substring-matches: asking it for "IR=" also finds
	 * the tail of "-SomeDir=..." and hands back that path. A two-letter switch name makes that a real
	 * collision rather than a theoretical one, so this walks whole tokens and compares the key.
	 */
	bool FindSwitchValue(const TCHAR* CmdLine, const TCHAR* SwitchName, FString& OutValue)
	{
		const TCHAR* Cursor = CmdLine;

		FString Token;
		while (FParse::Token(Cursor, Token, /*bUseEscape=*/false))
		{
			Token.TrimStartAndEndInline();
			if (Token.IsEmpty())
			{
				continue;
			}

			if (Token[0] == TEXT('-') || Token[0] == TEXT('/'))
			{
				Token = Token.RightChop(1);
			}

			FString Key;
			FString Value;
			if (Token.Split(TEXT("="), &Key, &Value) && Key.TrimStartAndEnd().Equals(SwitchName, ESearchCase::IgnoreCase))
			{
				// Trailing quotes survive tokenisation of -IR="1"; nobody writes that, but a batch
				// file that builds the command line from a variable does.
				OutValue = Value.TrimStartAndEnd().TrimQuotes();
				return true;
			}
		}

		return false;
	}

	/** Everything the command line said about booting, parsed once. */
	struct FBootFlagState
	{
		ERecordingBootMode Mode = ERecordingBootMode::Normal;
		bool bModeSpecified = false;
		bool bControlRecapSwitch = false;
		FString RequestedFolder;
	};

	FBootFlagState ParseBootFlags()
	{
		FBootFlagState State;
		const TCHAR* CmdLine = FCommandLine::Get();

		// -IR=<n>, with a bare -IR reading as -IR=1. Anything unparseable is a typo in a shortcut
		// somebody will stare at for ten minutes, so it warns rather than silently booting normally.
		FString RawMode;
		if (FindSwitchValue(CmdLine, TEXT("IR"), RawMode))
		{
			State.bModeSpecified = true;

			if (RawMode.IsEmpty() || RawMode.Equals(TEXT("1")) || RawMode.Equals(TEXT("true"), ESearchCase::IgnoreCase)
				|| RawMode.Equals(TEXT("recap"), ESearchCase::IgnoreCase))
			{
				State.Mode = ERecordingBootMode::ControlRecap;
			}
			else if (RawMode.Equals(TEXT("0")) || RawMode.Equals(TEXT("false"), ESearchCase::IgnoreCase))
			{
				State.Mode = ERecordingBootMode::Normal;
			}
			else
			{
				UE_LOG(LogRecordingStore, Warning,
					TEXT("-IR=%s is not a boot mode. Use -IR=0 for the gameplay map or -IR=1 for the ")
					TEXT("control recap map. Booting normally."), *RawMode);

				State.bModeSpecified = false;
			}
		}
		else if (FParse::Param(CmdLine, TEXT("IR")))
		{
			State.Mode = ERecordingBootMode::ControlRecap;
			State.bModeSpecified = true;
		}

		// -ControlRecap, bare or valued. FParse::Param only matches the bare switch, so the valued
		// form needs the token walk - and the long name is distinctive enough that it could have used
		// FParse::Value, but there is no reason for the two flags to parse differently.
		State.bControlRecapSwitch = FParse::Param(CmdLine, TEXT("ControlRecap"));

		FString RequestedFolder;
		if (FindSwitchValue(CmdLine, TEXT("ControlRecap"), RequestedFolder))
		{
			State.bControlRecapSwitch = true;

			if (!RequestedFolder.IsEmpty())
			{
				// Accept a bare index so "-ControlRecap=5" does the obvious thing.
				State.RequestedFolder = (FRecordingSessionInfo::ParseFolderName(RequestedFolder) == INDEX_NONE && RequestedFolder.IsNumeric())
					? FRecordingSessionInfo::MakeFolderName(FCString::Atoi(*RequestedFolder))
					: RequestedFolder;
			}
		}

		return State;
	}

	const FBootFlagState& GetBootFlagState()
	{
		// The command line cannot change after startup, and every caller here runs long after
		// FCommandLine is set, so one parse is enough.
		static const FBootFlagState State = ParseBootFlags();
		return State;
	}
}

ERecordingBootMode RecordingBootFlags::GetBootMode()
{
	return GetBootFlagState().Mode;
}

bool RecordingBootFlags::WasBootModeSpecified()
{
	return GetBootFlagState().bModeSpecified;
}

bool RecordingBootFlags::IsControlRecapRequested()
{
	const FBootFlagState& State = GetBootFlagState();

	// An explicit -IR=0 is the one thing that can veto -ControlRecap: it is how a launcher that
	// appends -IR=<n> to a fixed shortcut turns review mode back off without editing the shortcut.
	if (State.bModeSpecified && State.Mode == ERecordingBootMode::Normal)
	{
		return false;
	}

	return State.Mode == ERecordingBootMode::ControlRecap || State.bControlRecapSwitch;
}

FString RecordingBootFlags::GetRequestedSessionFolder()
{
	return GetBootFlagState().RequestedFolder;
}

bool RecordingBootFlags::ShouldForceMostRecentSession()
{
	const FBootFlagState& State = GetBootFlagState();

	// Only the short flag forces. -ControlRecap on its own already falls through to "most recent" as
	// its last resort, but it lets a level pin a take first; -IR=1 is the terminal-driven "show me
	// what I just recorded" path, and a pinned level would defeat the entire point of it.
	return State.Mode == ERecordingBootMode::ControlRecap
		&& State.bModeSpecified
		&& State.RequestedFolder.IsEmpty();
}

FString RecordingBootFlags::DescribeBootFlags()
{
	const FBootFlagState& State = GetBootFlagState();

	if (!IsControlRecapRequested())
	{
		return State.bModeSpecified
			? TEXT("-IR=0: booting the gameplay map.")
			: TEXT("No boot flags: booting the gameplay map.");
	}

	const TCHAR* Flag = State.bModeSpecified ? TEXT("-IR=1") : TEXT("-ControlRecap");

	return State.RequestedFolder.IsEmpty()
		? FString::Printf(TEXT("%s: reviewing the most recent session."), Flag)
		: FString::Printf(TEXT("%s: reviewing '%s'."), Flag, *State.RequestedFolder);
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
			TEXT("%s was passed but no Control Recap Map is set in ")
			TEXT("Project Settings > Game > Input Recording. Booting normally instead."),
			WasBootModeSpecified() ? TEXT("-IR=1") : TEXT("-ControlRecap"));
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

	UE_LOG(LogRecordingStore, Log, TEXT("Booting into '%s'. %s"), *MapPath, *DescribeBootFlags());

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
			TEXT("Boot override missed its window and '%s' loaded instead. Travelling to '%s' now."),
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
