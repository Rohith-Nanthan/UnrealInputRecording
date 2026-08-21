// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputActionValue.h"
#include "InputReplayTypes.generated.h"

/** What the replay component is currently doing. */
UENUM(BlueprintType)
enum class EInputReplayMode : uint8
{
	Idle			UMETA(DisplayName = "Idle"),
	Recording		UMETA(DisplayName = "Recording"),
	PlayingGhost	UMETA(DisplayName = "Playing Ghost"),
	MatchingInput	UMETA(DisplayName = "Matching Input")
};

/**
 * How recording timestamps advance.
 *
 * FixedLogicalStep is the default because MatchInput needs frame-rate-independent
 * determinism: a take recorded at 144 fps must line up identically when reviewed at 30 fps.
 */
UENUM(BlueprintType)
enum class EInputReplayTimeMode : uint8
{
	/** Wall clock, straight off UWorld::GetTimeSeconds(). */
	RealTime			UMETA(DisplayName = "Real Time"),
	/** Fixed-tick accumulator, independent of frame rate. */
	FixedLogicalStep	UMETA(DisplayName = "Fixed Logical Step")
};

/** Whether the whitelist narrows what the mapping contexts already reach. */
UENUM(BlueprintType)
enum class EInputRecordingFilterMode : uint8
{
	/** Record every action reachable through the recorded mapping contexts. */
	RecordAll		UMETA(DisplayName = "Record All"),
	/** Record only whitelisted actions. Subtractive only - it never adds actions the contexts do not reach. */
	WhitelistOnly	UMETA(DisplayName = "Whitelist Only")
};

/**
 * One recorded input sample.
 *
 * Deliberately stores neither a UInputAction* (meaningless across sessions) nor an
 * FInputActionValue (its members are private and unreflected, so neither the reflection
 * system nor FJsonObjectConverter can round-trip it). The raw vector plus the type tag
 * is lossless and survives both.
 */
USTRUCT(BlueprintType)
struct INPUTRECORDER_API FRecordedInputSample
{
	GENERATED_BODY()

	/** Short asset name, e.g. IA_Jump. Survives a project reorganisation that a path would not. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input Recording")
	FName ActionName = NAME_None;

	/** Index into the header's ActionPaths - the compact key. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input Recording")
	int32 ActionIndex = INDEX_NONE;

	/** Logical tick. This drives playback; the float below never does. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input Recording")
	int32 FrameIndex = 0;

	/** Derived from FrameIndex, for tooling / scrubbing / JSON readability only. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input Recording")
	float TimeSeconds = 0.0f;

	/** ETriggerEvent at capture, kept for debugging. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input Recording")
	uint8 TriggerEvent = 0;

	/** EInputActionValueType - Boolean / Axis1D / Axis2D / Axis3D. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input Recording")
	uint8 ValueType = 0;

	/** Always a full vector regardless of the action's real dimensionality. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input Recording")
	FVector Value = FVector::ZeroVector;

	FRecordedInputSample() = default;

	FRecordedInputSample(FName InActionName, int32 InActionIndex, int32 InFrameIndex, float InTimeSeconds,
		uint8 InTriggerEvent, uint8 InValueType, const FVector& InValue)
		: ActionName(InActionName)
		, ActionIndex(InActionIndex)
		, FrameIndex(InFrameIndex)
		, TimeSeconds(InTimeSeconds)
		, TriggerEvent(InTriggerEvent)
		, ValueType(InValueType)
		, Value(InValue)
	{
	}
};

/** Everything needed to interpret the sample stream that follows it. */
USTRUCT(BlueprintType)
struct INPUTRECORDER_API FInputRecordingHeader
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input Recording")
	FGuid RecordingId;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input Recording")
	FString DisplayName;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input Recording")
	FDateTime RecordedAtUtc = FDateTime(0);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input Recording")
	FString LevelName;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input Recording")
	FString EngineVersion;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input Recording")
	EInputReplayTimeMode TimeMode = EInputReplayTimeMode::FixedLogicalStep;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input Recording")
	int32 LogicalTicksPerSecond = 60;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input Recording")
	int32 TotalFrames = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input Recording")
	int32 RandomSeed = 0;

	/** Soft object paths of every tracked action, in registry order. Sample.ActionIndex indexes this. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input Recording")
	TArray<FString> ActionPaths;

	/** Indices into ActionPaths whose values are per-frame deltas rather than rates. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input Recording")
	TArray<int32> FrameDeltaActionIndices;

	/** Short asset name for an index, or an empty string when the index is out of range. */
	FString GetActionShortName(int32 ActionIndex) const;
};

/**
 * A complete take.
 *
 * The sample array is delta-compressed: a sample exists only where an action's value or
 * trigger event actually changed. Playback reconstructs the dense stream by holding the
 * last known value for every action.
 */
USTRUCT(BlueprintType)
struct INPUTRECORDER_API FInputRecording
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input Recording")
	FInputRecordingHeader Header;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input Recording")
	TArray<FRecordedInputSample> Samples;

	/** Optional per-logical-frame delta seconds, written only in RealTime mode. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input Recording")
	TArray<float> FrameDeltaSeconds;

	bool IsValidRecording() const;
	float GetDurationSeconds() const;
	void Reset();
};
