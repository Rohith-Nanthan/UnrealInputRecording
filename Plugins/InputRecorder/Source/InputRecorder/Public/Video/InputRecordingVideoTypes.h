// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputRecordingVideoTypes.generated.h"

/**
 * Which way up the pixels handed to the encoder are.
 *
 * Deliberately a named enum rather than a bool: "flip" means nothing until you know what it is
 * flipping from, and every orientation bug in this area came from two pieces of code each
 * assuming the other's convention. UMediaCapture's CPU readback delivers top-down; Media
 * Foundation's raw RGB surface convention is bottom-up (legacy DIB order). Which conversion the
 * sink writer inserts is machine-dependent - determine it with ir.video.dumpframe, do not guess.
 */
UENUM(BlueprintType)
enum class EInputRecordingVideoOrientation : uint8
{
	/** Use the platform backend's documented default for this machine. */
	Auto		UMETA(DisplayName = "Auto"),
	/** First row in the buffer is the top of the image. */
	TopDown		UMETA(DisplayName = "Top Down"),
	/** First row in the buffer is the bottom of the image. */
	BottomUp	UMETA(DisplayName = "Bottom Up")
};

/** Everything tunable about the capture half of a take. */
USTRUCT(BlueprintType)
struct INPUTRECORDER_API FInputRecordingVideoOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video", meta = (ClampMin = "1", ClampMax = "120"))
	int32 TargetFrameRate = 30;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video", meta = (ClampMin = "1", ClampMax = "200"))
	int32 BitrateMegabitsPerSecond = 12;

	/**
	 * Off by default, and it should stay off. Capture runs at native viewport resolution: a
	 * scale factor adds a variable that makes orientation bugs harder to diagnose, and 1x is
	 * correct for a review video anybody wants to read text in.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video")
	bool bOverrideResolution = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video", meta = (EditCondition = "bOverrideResolution"))
	FIntPoint ForcedResolution = FIntPoint(1920, 1080);

	/** Frames are dropped rather than stalling the render thread. At 1080p each slot is about 8 MB. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxQueuedFrames = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video")
	EInputRecordingVideoOrientation Orientation = EInputRecordingVideoOrientation::Auto;

	int32 GetBitrateBitsPerSecond() const { return FMath::Max(1, BitrateMegabitsPerSecond) * 1000000; }
};
