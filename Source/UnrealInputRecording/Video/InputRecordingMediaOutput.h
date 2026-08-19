// Copyright (c) Your Studio. All Rights Reserved.
//
// InputRecordingMediaOutput.h
//
// The MediaIO half of screen capture: a UMediaOutput / UMediaCapture pair that takes the game
// viewport's back buffer and feeds it to an IInputRecordingVideoEncoder.
//
// Why go through MediaCapture at all rather than reading the back buffer by hand:
//   * It already owns the double/triple-buffered GPU->CPU readback, so the game thread never blocks
//     waiting on a fence.
//   * It handles the resize/crop/pixel-format conversion passes on the GPU.
//   * It survives viewport resizes and PIE teardown without leaving a dangling render command.
// Reimplementing that correctly is a week of work; subclassing it is two small overrides.
//
// The split is the framework's own convention: the *Output is the configuration (a UObject you could
// serialise as an asset), the *Capture is the live session created from it.

#pragma once

#include "CoreMinimal.h"
#include "MediaCapture.h"
#include "MediaOutput.h"
#include "Video/InputRecordingVideoTypes.h"

// Included rather than forward declared: UInputRecordingMediaCapture holds a
// TUniquePtr<IInputRecordingVideoEncoder>, and the compiler-generated destructor - which UHT emits into
// the .gen.cpp, where only this header is visible - needs the complete type to call delete on it.
#include "Video/VideoEncoderBackend.h"

#include "InputRecordingMediaOutput.generated.h"

/**
 * Capture settings for one take. Created transiently by UInputRecordingScreenRecorder; there is no
 * need to make an asset out of it, though nothing stops you.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Input Recording Media Output"))
class UNREALINPUTRECORDING_API UInputRecordingMediaOutput : public UMediaOutput
{
	GENERATED_BODY()

public:
	UInputRecordingMediaOutput();

	/** Absolute path of the .mp4 to write. Set by the screen recorder from the recording's name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording|Video")
	FString OutputFilePath;

	/**
	 * Capture resolution. Always even on both axes - the recorder snaps it before assigning, and the
	 * encoder refuses odd dimensions outright.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording|Video")
	FIntPoint Resolution = FIntPoint(1920, 1080);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording|Video")
	FInputRecordingVideoOptions EncoderOptions;

	/**
	 * Write the first frame of this capture out as a PNG next to the .mp4.
	 *
	 * A one-shot, set by ir.video.dumpframe before the next take starts. It lives here rather than in
	 * EncoderOptions because it is a debugging action for one capture, not a setting that should
	 * persist into DefaultGame.ini.
	 */
	UPROPERTY(Transient, BlueprintReadWrite, Category = "Input Recording|Video")
	bool bDumpFirstFrame = false;

	//~ Begin UMediaOutput interface
	virtual bool Validate(FString& OutFailureReason) const override;
	virtual FIntPoint GetRequestedSize() const override { return Resolution; }

	/**
	 * B8G8R8A8 specifically. It is the viewport's native format on Windows, so no conversion pass is
	 * needed, and it maps one-to-one onto Media Foundation's MFVideoFormat_RGB32 with no swizzle.
	 */
	virtual EPixelFormat GetRequestedPixelFormat() const override { return EPixelFormat::PF_B8G8R8A8; }

	virtual EMediaCaptureConversionOperation GetConversionOperation(EMediaCaptureSourceType InSourceType) const override
	{
		return EMediaCaptureConversionOperation::NONE;
	}

#if WITH_EDITOR
	virtual FString GetDescriptionString() const override;
#endif

protected:
	virtual UMediaCapture* CreateMediaCaptureImpl() override;
	//~ End UMediaOutput interface
};

/**
 * The live capture session. One per recording take.
 *
 * Frames arrive on the render thread in OnFrameCaptured_RenderingThread and are handed straight to the
 * encoder, which copies them and returns - no encoding happens on the render thread.
 */
UCLASS(BlueprintType)
class UNREALINPUTRECORDING_API UInputRecordingMediaCapture : public UMediaCapture
{
	GENERATED_BODY()

public:
	/** FPlatformTime::Seconds() at the moment capture began. The video timeline's origin. */
	double GetCaptureStartTimeSeconds() const { return StartTimeSeconds; }

	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	int32 GetEncodedFrameCount() const;

	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	int32 GetDroppedFrameCount() const;

	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	FString GetEncoderError() const;

protected:
	//~ Begin UMediaCapture interface
	virtual bool InitializeCapture() override;
	virtual void StopCaptureImpl(bool bAllowPendingFrameToBeProcess) override;

	/** false = give us a CPU-side buffer. The encoder needs system memory, not an RHI texture. */
	virtual bool ShouldCaptureRHIResource() const override { return false; }

	virtual void OnFrameCaptured_RenderingThread(const FCaptureBaseData& InBaseData,
												 TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData,
												 void* InBuffer, int32 Width, int32 Height, int32 BytesPerRow) override;
	//~ End UMediaCapture interface

private:
	/**
	 * Guards Encoder against the one real race here: StopCaptureImpl runs on the game thread while
	 * OnFrameCaptured_RenderingThread may still be mid-flight on the render thread. Held only around
	 * the enqueue (a memcpy), never around an encode, so the render thread is not exposed to encoder
	 * latency.
	 */
	mutable FCriticalSection EncoderLock;

	TUniquePtr<IInputRecordingVideoEncoder> Encoder;

	double StartTimeSeconds = 0.0;

	/** Cached across the encoder's destruction so the stats survive into the completion log. */
	int64 FinalEncodedFrames = 0;
	int64 FinalDroppedFrames = 0;
};
