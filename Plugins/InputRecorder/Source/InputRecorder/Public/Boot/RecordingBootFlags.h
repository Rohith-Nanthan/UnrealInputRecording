// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RecordingBootFlags.generated.h"

UENUM(BlueprintType)
enum class ERecordingBootMode : uint8
{
	/** Normal boot into the gameplay map. */
	Normal			UMETA(DisplayName = "Normal"),
	/** Boot straight into the standalone review map. */
	ControlRecap	UMETA(DisplayName = "Control Recap")
};

/** Everything the command line asked for, parsed once at module startup. */
struct FRecordingBootFlags
{
	ERecordingBootMode Mode = ERecordingBootMode::Normal;

	/** Session folder name or bare index the command line named, empty for "most recent". */
	FString RequestedSession;

	/** -RecordingRoot=<path>, empty when not supplied. */
	FString RecordingRootOverride;

	/** True when -IR was present with any value, parseable or not. */
	bool bSawIRSwitch = false;

	/** True when -IR=0 was supplied, which also vetoes -ControlRecap. */
	bool bIRExplicitlyDisabled = false;

	bool bSawControlRecapSwitch = false;

	/** What the map override actually did, for the log and for DescribeBootFlags. */
	FString ResolvedMapPath;
	bool bRewroteDefaultMap = false;
};

namespace RecordingBootFlags
{
	/**
	 * Whole-token command-line lookup.
	 *
	 * FParse::Value substring-matches, so asking it for "IR=" also finds the tail of
	 * "-SomeDir=..." and hands back that path. With a two-letter switch that is a real
	 * collision, not a theoretical one. This walks whole tokens, strips a leading - or /,
	 * splits on the first =, and compares the key exactly.
	 *
	 * @param bOutHadValue false for a bare switch such as "-ControlRecap" with no "=".
	 */
	INPUTRECORDER_API bool FindSwitchValue(const TCHAR* CommandLine, const TCHAR* Key, FString& OutValue, bool& bOutHadValue);

	/** Convenience wrapper for callers that do not care whether a value was attached. */
	INPUTRECORDER_API bool HasSwitch(const TCHAR* CommandLine, const TCHAR* Key);

	/** Parses without applying anything. Exposed so tests and logging can reuse it. */
	INPUTRECORDER_API FRecordingBootFlags Parse(const TCHAR* CommandLine);

	/** The flags this process booted with. */
	INPUTRECORDER_API const FRecordingBootFlags& Get();

	/** Called once from the game module's StartupModule. */
	INPUTRECORDER_API void InitializeFromCommandLine();

	/** Human-readable one-liner for logs and menus. */
	INPUTRECORDER_API FString Describe();
}

/** Blueprint view of the boot flags, so a menu can offer "Review last recording" only when asked for. */
UCLASS()
class INPUTRECORDER_API URecordingBootLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Input Recording|Boot")
	static ERecordingBootMode GetBootMode();

	UFUNCTION(BlueprintPure, Category = "Input Recording|Boot")
	static bool IsControlRecapBoot();

	/** Empty means "the most recently updated playable session". */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Boot")
	static FString GetRequestedSessionFolder();

	UFUNCTION(BlueprintPure, Category = "Input Recording|Boot")
	static FString GetRecordingRootOverride();

	UFUNCTION(BlueprintPure, Category = "Input Recording|Boot")
	static FString DescribeBootFlags();
};
