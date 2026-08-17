// Copyright (c) Your Studio. All Rights Reserved.
//
// InputReplayTypes.h
//
// Core POD types for the deterministic Enhanced Input recording / playback system.
//
// NOTE: Replace INPUTREPLAY_API with your own module's API macro (e.g. MYGAME_API)
//       if you are dropping these files straight into a game module.

#pragma once

#include "CoreMinimal.h"
#include "InputActionValue.h"   // FInputActionValue, EInputActionValueType
#include "InputTriggers.h"      // ETriggerEvent
#include "Misc/DateTime.h"
#include "Misc/Guid.h"

#include "InputReplayTypes.generated.h"

class UInputAction;

namespace InputReplay
{
	/** 'IRPL' little-endian. First 4 bytes of every binary recording. */
	static constexpr uint32 FileMagic = 0x4C505249;

	/** Bump whenever the on-disk layout changes. Loader refuses newer files. */
	static constexpr uint32 FileVersion = 1;

	inline const TCHAR* BinaryExtension = TEXT(".ghost");
	inline const TCHAR* JsonExtension   = TEXT(".ghost.json");

	/** Subfolder under FPaths::ProjectSavedDir(). */
	inline const TCHAR* RecordingSubDir = TEXT("InputRecordings");
}

/** What the manager component is currently doing. */
UENUM(BlueprintType)
enum class EInputReplayMode : uint8
{
	Idle		UMETA(DisplayName = "Idle"),
	Recording	UMETA(DisplayName = "Recording"),
	Playing		UMETA(DisplayName = "Playing")
};

/**
 * How recorded time maps onto playback time. See README section "Determinism".
 */
UENUM(BlueprintType)
enum class EInputReplayTimeMode : uint8
{
	/**
	 * Default. Input is quantised onto a fixed logical tick (e.g. 60 Hz) using an accumulator.
	 * Playback consumes the same logical ticks, aggregating several of them into one engine
	 * frame when the framerate drops. Robust, shipping-safe, ~frame-accurate.
	 */
	FixedLogicalStep	UMETA(DisplayName = "Fixed Logical Step (recommended)"),

	/**
	 * Highest fidelity. One logical frame per engine frame, and the exact DeltaSeconds of every
	 * recorded frame is stored. Playback forces FApp::SetUseFixedTimeStep + SetFixedDeltaTime so
	 * gameplay code observes a bit-identical delta time sequence. Use for automated tests,
	 * regression capture and validation runs; it decouples the sim from the wall clock.
	 */
	RecordedDeltas		UMETA(DisplayName = "Replay Recorded Deltas (most deterministic)"),

	/**
	 * Simplest: dispatch purely by comparing wall-clock elapsed time against timestamps.
	 * Provided for reference / debugging only - it WILL drift.
	 */
	FreeRun				UMETA(DisplayName = "Free Run (debug only)")
};

/**
 * A single recorded input sample.
 *
 * Storage is delta-compressed: a frame is only written when the value or trigger event for that
 * action actually changed. Playback reconstructs the dense per-tick stream by holding the last
 * known value (see UInputReplayComponent::AdvanceStateTo).
 *
 * We deliberately do NOT store a UInputAction* or an FInputActionValue directly:
 *  - Raw object pointers are meaningless across sessions.
 *  - FInputActionValue's members are private and not UPROPERTY-reflected, so neither the
 *    reflection system nor FJsonObjectConverter can round-trip it. We store the raw vector and
 *    the value type instead, which is lossless.
 */
USTRUCT(BlueprintType)
struct FRecordedInputFrame
{
	GENERATED_BODY()

	/**
	 * Authoritative time reference: the logical tick this sample belongs to.
	 * Integer indices cannot accumulate float error, which is why playback keys off this and
	 * not off TimeSeconds.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay")
	int32 FrameIndex = 0;

	/**
	 * Elapsed seconds since recording started. Derived from FrameIndex for fixed-step recordings.
	 * Kept for tooling, JSON readability and scrubbing UI - NOT used to drive playback.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay")
	float TimeSeconds = 0.0f;

	/** Index into FInputRecordingHeader::ActionPaths. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay")
	int32 ActionIndex = INDEX_NONE;

	/** ETriggerEvent at the moment of capture. Debug / tooling only - see README. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay")
	uint8 TriggerEvent = 0;

	/** EInputActionValueType (Boolean / Axis1D / Axis2D / Axis3D). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay")
	uint8 ValueType = 0;

	/**
	 * Raw value. Stored as float32 (not the engine's FVector3d) so the on-disk representation is
	 * platform-stable and half the size. Boolean packs into X as 0.0/1.0, Axis1D into X, etc.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay")
	FVector3f Value = FVector3f::ZeroVector;

	FRecordedInputFrame() = default;

	FRecordedInputFrame(int32 InFrameIndex, float InTimeSeconds, int32 InActionIndex,
						ETriggerEvent InTriggerEvent, const FInputActionValue& InValue)
		: FrameIndex(InFrameIndex)
		, TimeSeconds(InTimeSeconds)
		, ActionIndex(InActionIndex)
		, TriggerEvent(static_cast<uint8>(InTriggerEvent))
		, ValueType(static_cast<uint8>(InValue.GetValueType()))
	{
		const FVector Raw = InValue.Get<FVector>();
		Value = FVector3f(static_cast<float>(Raw.X), static_cast<float>(Raw.Y), static_cast<float>(Raw.Z));
	}

	/** Rebuild an engine-native FInputActionValue for injection. */
	FInputActionValue ToActionValue() const
	{
		return FInputActionValue(
			static_cast<EInputActionValueType>(ValueType),
			FVector(static_cast<double>(Value.X), static_cast<double>(Value.Y), static_cast<double>(Value.Z)));
	}

	ETriggerEvent GetTriggerEvent() const { return static_cast<ETriggerEvent>(TriggerEvent); }

	friend FArchive& operator<<(FArchive& Ar, FRecordedInputFrame& Frame);
};

/**
 * A periodic snapshot of authoritative pawn state.
 *
 * Input-only replay cannot be perfectly deterministic in a general-purpose engine, so we sample
 * ground truth every N logical ticks. During playback we compare against it: this turns silent
 * drift into either a measurable error metric or an explicit correction.
 */
USTRUCT(BlueprintType)
struct FReplaySyncPoint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay")
	int32 FrameIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay")
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay")
	FVector Velocity = FVector::ZeroVector;

	/** Camera / aim rotation, which is driven by look input and drifts first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay")
	FRotator ControlRotation = FRotator::ZeroRotator;

	friend FArchive& operator<<(FArchive& Ar, FReplaySyncPoint& Point);
};

/** Everything needed to validate and resolve a recording before playing it. */
USTRUCT(BlueprintType)
struct FInputRecordingHeader
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay")
	FGuid RecordingId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay")
	FDateTime RecordedAtUtc = FDateTime(0);

	/** Level the recording was made on. Playing back on a different map is almost always a bug. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay")
	FString LevelName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay")
	FString EngineVersion;

	/** EInputReplayTimeMode the recording was captured with. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay")
	uint8 TimeMode = 0;

	/** Logical ticks per second. Meaningless in RecordedDeltas mode (deltas are explicit). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay")
	int32 LogicalTicksPerSecond = 60;

	/** Total logical ticks in the recording; playback ends here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay")
	int32 TotalFrames = 0;

	/** Seed pushed into FMath::RandInit / SRandInit on playback so gameplay RNG replays too. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay")
	int32 RandomSeed = 0;

	/**
	 * Soft object paths of every UInputAction referenced, in registry order.
	 * FRecordedInputFrame::ActionIndex indexes into this array - far more compact than repeating
	 * a path per sample, and it gives us one place to validate assets on load.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay")
	TArray<FString> ActionPaths;

	/** Indices (into ActionPaths) of actions whose value is a per-frame delta, not a rate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay")
	TArray<int32> FrameDeltaActionIndices;

	float GetFixedStepSeconds() const
	{
		return 1.0f / static_cast<float>(FMath::Max(1, LogicalTicksPerSecond));
	}

	friend FArchive& operator<<(FArchive& Ar, FInputRecordingHeader& Header);
};

/** The complete on-disk payload. */
USTRUCT(BlueprintType)
struct FInputRecording
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay")
	FInputRecordingHeader Header;

	/** Delta-compressed input samples, sorted ascending by FrameIndex. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay")
	TArray<FRecordedInputFrame> Frames;

	/**
	 * One entry per recorded engine frame. Only populated in RecordedDeltas mode, where playback
	 * feeds these straight back into FApp::SetFixedDeltaTime().
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay")
	TArray<float> FrameDeltaSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Replay")
	TArray<FReplaySyncPoint> SyncPoints;

	void Reset()
	{
		Header = FInputRecordingHeader();
		Frames.Reset();
		FrameDeltaSeconds.Reset();
		SyncPoints.Reset();
	}

	bool IsValidRecording() const { return Header.TotalFrames > 0 && Header.ActionPaths.Num() > 0; }

	float GetDurationSeconds() const
	{
		if (Header.TimeMode == static_cast<uint8>(EInputReplayTimeMode::RecordedDeltas))
		{
			float Total = 0.0f;
			for (const float Delta : FrameDeltaSeconds) { Total += Delta; }
			return Total;
		}
		return Header.TotalFrames * Header.GetFixedStepSeconds();
	}

	friend FArchive& operator<<(FArchive& Ar, FInputRecording& Recording);
};
