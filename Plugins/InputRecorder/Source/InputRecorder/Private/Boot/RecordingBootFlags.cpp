// Copyright Epic Games, Inc. All Rights Reserved.

#include "Boot/RecordingBootFlags.h"

#include "GameMapsSettings.h"
#include "InputRecordingLog.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Settings/InputRecordingSettings.h"

namespace RecordingBootFlagsPrivate
{
	FRecordingBootFlags GBootFlags;
	bool bInitialized = false;

	FString StripQuotes(const FString& In)
	{
		FString Out = In.TrimStartAndEnd();

		// Nobody writes -IR="1" by hand, but a batch file building the command line out of a
		// variable does it constantly.
		if (Out.Len() >= 2 && Out.StartsWith(TEXT("\"")) && Out.EndsWith(TEXT("\"")))
		{
			Out = Out.Mid(1, Out.Len() - 2);
		}

		return Out;
	}

	/**
	 * Reads the review map out of config directly rather than through
	 * GetDefault<UInputRecordingSettings>(). This runs during StartupModule, and depending on a
	 * CDO that far up the boot sequence is exactly the kind of ordering assumption that works
	 * in the editor and fails in a packaged build.
	 */
	FString ResolveControlRecapMap()
	{
		FString MapPath;

		if (GConfig && GConfig->GetString(InputRecorderDefaults::SettingsSection, TEXT("ControlRecapMap"), MapPath, GGameIni))
		{
			// FSoftObjectPath serialises to config wrapped in its own struct syntax on some
			// paths; strip anything that is obviously not a path.
			MapPath = StripQuotes(MapPath);
		}

		// No entry in the host project's DefaultGame.ini is the normal case for a project that
		// has just had this plugin dropped into it, not an error - so the fallback has to be the
		// map the plugin itself ships. It is the same string the settings CDO defaults to.
		if (MapPath.IsEmpty())
		{
			MapPath = InputRecorderDefaults::ControlRecapMapPath;
		}

		return MapPath;
	}
}

bool RecordingBootFlags::FindSwitchValue(const TCHAR* CommandLine, const TCHAR* Key, FString& OutValue, bool& bOutHadValue)
{
	using namespace RecordingBootFlagsPrivate;

	OutValue.Reset();
	bOutHadValue = false;

	if (!CommandLine || !Key)
	{
		return false;
	}

	const TCHAR* Cursor = CommandLine;
	FString Token;

	while (FParse::Token(Cursor, Token, /*UseEscape=*/false))
	{
		FString Trimmed = Token.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			continue;
		}

		if (Trimmed.StartsWith(TEXT("-")) || Trimmed.StartsWith(TEXT("/")))
		{
			Trimmed.RightChopInline(1, EAllowShrinking::No);
		}

		FString TokenKey;
		FString TokenValue;
		if (Trimmed.Split(TEXT("="), &TokenKey, &TokenValue))
		{
			bOutHadValue = true;
		}
		else
		{
			TokenKey = Trimmed;
		}

		// Exact key comparison. This is the whole point of walking tokens.
		if (!TokenKey.Equals(Key, ESearchCase::IgnoreCase))
		{
			bOutHadValue = false;
			continue;
		}

		OutValue = StripQuotes(TokenValue);
		return true;
	}

	return false;
}

bool RecordingBootFlags::HasSwitch(const TCHAR* CommandLine, const TCHAR* Key)
{
	FString Unused;
	bool bUnusedHadValue = false;
	return FindSwitchValue(CommandLine, Key, Unused, bUnusedHadValue);
}

FRecordingBootFlags RecordingBootFlags::Parse(const TCHAR* CommandLine)
{
	FRecordingBootFlags Flags;

	FString Value;
	bool bHadValue = false;

	// -RecordingRoot=<path>
	if (FindSwitchValue(CommandLine, TEXT("RecordingRoot"), Value, bHadValue) && bHadValue && !Value.IsEmpty())
	{
		Flags.RecordingRootOverride = Value;
	}

	// -IR=<n>
	bool bIRRequestsRecap = false;
	if (FindSwitchValue(CommandLine, TEXT("IR"), Value, bHadValue))
	{
		Flags.bSawIRSwitch = true;

		if (!bHadValue || Value.IsEmpty())
		{
			// A bare -IR reads as "yes, review", which is the only sensible meaning.
			bIRRequestsRecap = true;
		}
		else if (Value.IsNumeric())
		{
			const int32 Numeric = FCString::Atoi(*Value);
			bIRRequestsRecap = Numeric != 0;
			Flags.bIRExplicitlyDisabled = Numeric == 0;
		}
		else
		{
			// Warn rather than fail silently. This is ten minutes somebody would otherwise
			// spend staring at a shortcut wondering why nothing happens.
			UE_LOG(LogInputRecording, Warning,
				TEXT("-IR=%s is not a number. Booting normally. Use -IR=1 to review, -IR=0 to boot normally."), *Value);
		}
	}

	// -ControlRecap[=<session>]
	if (FindSwitchValue(CommandLine, TEXT("ControlRecap"), Value, bHadValue))
	{
		Flags.bSawControlRecapSwitch = true;

		if (bHadValue && !Value.IsEmpty())
		{
			Flags.RequestedSession = Value;
		}
	}

	// -IR=0 vetoes -ControlRecap on purpose: a launcher that always appends -IR=<n> needs a
	// value meaning "boot normally" that can override a shortcut with -ControlRecap baked in.
	if (Flags.bIRExplicitlyDisabled)
	{
		Flags.Mode = ERecordingBootMode::Normal;
		Flags.RequestedSession.Reset();
	}
	else if (bIRRequestsRecap || Flags.bSawControlRecapSwitch)
	{
		Flags.Mode = ERecordingBootMode::ControlRecap;
	}

	return Flags;
}

const FRecordingBootFlags& RecordingBootFlags::Get()
{
	return RecordingBootFlagsPrivate::GBootFlags;
}

void RecordingBootFlags::InitializeFromCommandLine()
{
	using namespace RecordingBootFlagsPrivate;

	if (bInitialized)
	{
		return;
	}
	bInitialized = true;

	GBootFlags = Parse(FCommandLine::Get());

	if (GBootFlags.Mode != ERecordingBootMode::ControlRecap)
	{
		return;
	}

	GBootFlags.ResolvedMapPath = ResolveControlRecapMap();

	// Rewriting GameDefaultMap here, during module startup, is the last point in the boot
	// sequence where it is still possible - and it happens before the engine picks a map at
	// all. The obvious implementation (boot, then OpenLevel) loads the gameplay map first: its
	// actors spawn, its game mode runs, and the player sees a frame or two of a level they did
	// not ask for.
	// Captured before the overwrite, not after - this is the only moment the original value
	// still exists anywhere.
	GBootFlags.OriginalDefaultMap = UGameMapsSettings::GetGameDefaultMap();

	UGameMapsSettings::SetGameDefaultMap(GBootFlags.ResolvedMapPath);
	GBootFlags.bRewroteDefaultMap = true;

	UE_LOG(LogInputRecording, Log, TEXT("Boot flags: %s"), *Describe());
}

FString RecordingBootFlags::Describe()
{
	const FRecordingBootFlags& Flags = Get();

	TArray<FString, TInlineAllocator<5>> Parts;

	Parts.Add(Flags.Mode == ERecordingBootMode::ControlRecap ? TEXT("mode=ControlRecap") : TEXT("mode=Normal"));

	if (Flags.bIRExplicitlyDisabled)
	{
		Parts.Add(TEXT("-IR=0 (vetoes -ControlRecap)"));
	}
	if (Flags.bSawControlRecapSwitch)
	{
		Parts.Add(TEXT("-ControlRecap present"));
	}
	if (!Flags.RequestedSession.IsEmpty())
	{
		Parts.Add(FString::Printf(TEXT("session=%s"), *Flags.RequestedSession));
	}
	if (!Flags.RecordingRootOverride.IsEmpty())
	{
		Parts.Add(FString::Printf(TEXT("root=%s"), *Flags.RecordingRootOverride));
	}
	if (Flags.bRewroteDefaultMap)
	{
		Parts.Add(FString::Printf(TEXT("default map rewritten to %s"), *Flags.ResolvedMapPath));
	}

	return FString::Join(Parts, TEXT(", "));
}

// -------------------------------------------------------------------------------------------
// Blueprint access
// -------------------------------------------------------------------------------------------

ERecordingBootMode URecordingBootLibrary::GetBootMode()
{
	return RecordingBootFlags::Get().Mode;
}

bool URecordingBootLibrary::IsControlRecapBoot()
{
	return RecordingBootFlags::Get().Mode == ERecordingBootMode::ControlRecap;
}

FString URecordingBootLibrary::GetRequestedSessionFolder()
{
	return RecordingBootFlags::Get().RequestedSession;
}

FString URecordingBootLibrary::GetRecordingRootOverride()
{
	return RecordingBootFlags::Get().RecordingRootOverride;
}

FString URecordingBootLibrary::DescribeBootFlags()
{
	return RecordingBootFlags::Describe();
}
