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

	/**
	 * Writes one BGRA8 frame to a PNG, exactly as the encoder received it.
	 *
	 * The orientation harness behind ir.video.dumpframe. PNG rather than BMP on purpose: BMP's own
	 * row order is bottom-up by convention, so dumping to one would reproduce the very ambiguity this
	 * is meant to resolve. If the PNG is upright, the encoder is being fed an upright frame and any
	 * inversion in the .mp4 came from the encoder; if the PNG is inverted, the capture side is at
	 * fault. Blocking, and expected to be called at most once per take from a worker thread.
	 *
	 * @param Bgra    Tightly packed rows, Width * 4 bytes each.
	 * @param PngPath Absolute destination. Parent directory must already exist.
	 */
	UNREALINPUTRECORDING_API void DumpBgraFrameToPng(const uint8* Bgra, int32 Width, int32 Height, const FString& PngPath);
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
 * Row order handed to the encoder.
 *
 * Named rather than boolean because "flip" only means something once you know what it is flipping
 * from, and every bug in this area has come from two pieces of code each assuming the other's
 * convention. See FInputRecordingVideoOptions::Orientation.
 */
UENUM(BlueprintType)
enum class EInputRecordingCaptureOrientation : uint8
{
	/** Whatever the platform backend considers native. On Windows this is a straight row-for-row copy. */
	Auto		UMETA(DisplayName = "Auto"),

	/** Row 0 of the encoder's buffer is the top of the image. */
	TopDown		UMETA(DisplayName = "Top-down"),

	/** Row 0 of the encoder's buffer is the bottom of the image - the legacy RGB DIB convention. */
	BottomUp	UMETA(DisplayName = "Bottom-up")
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
	 * Capture is always at the viewport's native resolution - there is deliberately no scale factor.
	 *
	 * A downscale is the cheapest way to buy back frame time on a high-resolution display, but it
	 * also quietly changes what the recording is evidence *of*, and a control recap is meant to show
	 * the game as it actually looked. Cost is managed with BitRateKbps instead, which trades file
	 * size without touching the picture. The size is snapped down to even on both axes because H.264
	 * macroblocks require it and the encoder refuses an odd dimension outright.
	 *
	 * Ignore the viewport entirely and pin the output size with the two properties below.
	 */
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
	 * Which way up the encoder is fed.
	 *
	 * This exists as an explicit setting rather than a hidden constant because the two halves of the
	 * pipeline disagree about the convention and neither one announces it: UMediaCapture's CPU
	 * readback hands over the viewport top-down, while Media Foundation's uncompressed RGB surfaces
	 * follow the legacy DIB convention where the first row in memory is the *bottom* of the image.
	 * Whether that mismatch actually inverts the file depends on which conversion the sink writer
	 * inserts between RGB32 and the encoder's native format, and that varies by machine.
	 *
	 * Auto is what the current pipeline does today. If a capture comes out inverted, set BottomUp -
	 * the flip costs nothing, since the frame is already being copied row by row. Confirm which way
	 * is correct with ir.video.dumpframe rather than by re-encoding and eyeballing a video player.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video", AdvancedDisplay)
	EInputRecordingCaptureOrientation Orientation = EInputRecordingCaptureOrientation::Auto;
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
