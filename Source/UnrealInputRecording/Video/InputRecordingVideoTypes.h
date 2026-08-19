// Copyright (c) Your Studio. All Rights Reserved.
//
// InputRecordingVideoTypes.h
//
// Shared vocabulary for the screen-capture / video-playback half of the system.
//
// The whole video feature is built around one rule: a take's .mp4 lives next to its .ghost and shares
// the same bare name. UInputReplaySerializer::GetRecordingDirectory() is the single source of truth
// for where that is, so ResolveVideoPath() defers to it rather than rebuilding the path itself.
//
//     <ProjectSaved>/InputRecordings/MatchTutorial.ghost
//     <ProjectSaved>/InputRecordings/MatchTutorial.ghost.json
//     <ProjectSaved>/InputRecordings/MatchTutorial.mp4      <- this file's business
//
// Pairing by name (rather than by embedding the video path in the recording header) means the .ghost
// format does not change, old recordings keep loading, and you can delete a stray .mp4 without
// invalidating anything.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "InputRecordingVideoTypes.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogInputRecordingVideo, Log, All);

namespace InputRecordingVideo
{
	inline const TCHAR* VideoExtension = TEXT(".mp4");
}

/** What the screen recorder is doing. Mirrors EMediaCaptureState but without the MediaIO dependency. */
UENUM(BlueprintType)
enum class EInputRecordingVideoState : uint8
{
	Idle		UMETA(DisplayName = "Idle"),
	Starting	UMETA(DisplayName = "Starting"),
	Recording	UMETA(DisplayName = "Recording"),
	Failed		UMETA(DisplayName = "Failed")
};

/**
 * Encoder tuning. Lives in a struct so the project settings, the media output asset and the
 * subsystem can all pass the same set of knobs around without three parallel copies drifting apart.
 */
USTRUCT(BlueprintType)
struct FInputRecordingVideoOptions
{
	GENERATED_BODY()

	/**
	 * Nominal frame rate written into the MP4 header.
	 *
	 * This is NOT a capture rate: MediaCapture hands us one frame per rendered frame, and each sample
	 * is stamped with its real elapsed time, so the file is effectively variable-frame-rate. This
	 * value only tells the encoder what to optimise for and what to advertise to players.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video", meta = (ClampMin = "1", ClampMax = "240"))
	int32 TargetFrameRate = 30;

	/** Average H.264 bitrate. 12 Mbit is roughly "clean 1080p"; drop to ~6000 for smaller files. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video", meta = (ClampMin = "500", ClampMax = "100000"))
	int32 BitRateKbps = 12000;

	/**
	 * Scale applied to the viewport resolution before capture. 1.0 records at native resolution;
	 * 0.5 quarters the pixel count, which is the cheapest way to stop capture costing frames on a
	 * 4K display. The result is always snapped down to an even number of pixels - H.264 macroblocks
	 * require it and the encoder will refuse an odd dimension outright.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float ResolutionScale = 1.0f;

	/** Ignore the viewport size and always capture at ForcedResolution. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video", meta = (InlineEditConditionToggle))
	bool bOverrideResolution = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video", meta = (EditCondition = "bOverrideResolution"))
	FIntPoint ForcedResolution = FIntPoint(1920, 1080);

	/**
	 * How many captured frames may sit in the encoder queue before new ones are dropped.
	 *
	 * The queue exists because encoding must not happen on the render thread. A deeper queue rides
	 * out encoder hitches at the cost of memory (Width * Height * 4 bytes per slot - about 8 MB per
	 * frame at 1080p) and of dropping frames later rather than sooner when the encoder cannot keep up.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video", meta = (ClampMin = "1", ClampMax = "32"))
	int32 MaxQueuedFrames = 6;

	/**
	 * Flip the captured image vertically before encoding. Leave off - the stock pipeline is already
	 * upright. Only flip if a machine's H.264 encoder reads bottom-up and the .mp4 comes out inverted.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video", AdvancedDisplay)
	bool bFlipVerticallyOnCapture = false;
};

/** Path helpers. Kept in a library so Blueprint tooling can resolve the same paths C++ does. */
UCLASS()
class UNREALINPUTRECORDING_API UInputRecordingVideoLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Bare name ("MatchTutorial") -> "<ProjectSaved>/InputRecordings/MatchTutorial.mp4".
	 * An absolute path or a name that already ends in .mp4 is passed through untouched, matching
	 * UInputReplaySerializer::ResolveRecordingPath's behaviour.
	 */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	static FString ResolveVideoPath(const FString& RecordingName);

	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	static bool DoesVideoExist(const FString& RecordingName);

	/** True when this build can actually encode an MP4. False on platforms with no encoder backend. */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	static bool IsVideoCaptureSupported();
};
