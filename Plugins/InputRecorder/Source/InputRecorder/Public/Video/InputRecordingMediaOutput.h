// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MediaCapture.h"
#include "MediaOutput.h"
#include "Templates/SharedPointer.h"
#include "InputRecordingMediaOutput.generated.h"

class FVideoEncoderPipeline;

/**
 * Minimal UMediaOutput whose only job is to hand UMediaCapture a viewport-sized BGRA target.
 *
 * There is no scale-factor knob here on purpose - capture runs at native viewport resolution.
 * The resolution escape hatch lives in FInputRecordingVideoOptions and is off by default.
 */
UCLASS(BlueprintType)
class INPUTRECORDER_API UInputRecordingMediaOutput : public UMediaOutput
{
	GENERATED_BODY()

public:
	/** Zero (the default) means "whatever the viewport is". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording")
	FIntPoint OverrideSize = FIntPoint::ZeroValue;

	virtual bool Validate(FString& OutFailureReason) const override;
	virtual FIntPoint GetRequestedSize() const override;
	virtual EPixelFormat GetRequestedPixelFormat() const override;

#if WITH_EDITOR
	virtual FString GetDescriptionString() const override;
#endif

protected:
	virtual UMediaCapture* CreateMediaCaptureImpl() override;
};

/**
 * Receives captured viewport frames and pushes them at the encoder pipeline.
 *
 * ShouldCaptureRHIResource is left false so frames arrive already read back to CPU memory,
 * which is what the sink writer wants and what makes the frame dump meaningful.
 */
UCLASS(BlueprintType)
class INPUTRECORDER_API UInputRecordingMediaCapture : public UMediaCapture
{
	GENERATED_BODY()

public:
	/**
	 * Shared rather than raw: this is read on the render thread, and the controller that owns
	 * the pipeline can be garbage collected on the game thread at any point in between.
	 */
	void SetEncoderPipeline(TSharedPtr<FVideoEncoderPipeline, ESPMode::ThreadSafe> InPipeline);

	/**
	 * Arms a one-shot PNG dump of the next captured frame - the exact bytes handed to the
	 * encoder. PNG specifically: BMP's row order is bottom-up by convention and would reproduce
	 * the very ambiguity this is meant to resolve.
	 */
	void ArmFrameDump(const FString& InAbsolutePngPath);

	/** Wall-clock start of the take, so sample timestamps line up with the input timeline. */
	void SetCaptureStartTime(double InStartTimeSeconds) { CaptureStartTimeSeconds = InStartTimeSeconds; }

	/**
	 * UMediaCapture delivers every rendered frame, so a 144 fps game would otherwise produce a
	 * 144 fps video and burn through the storage quota in a couple of takes. Frames arriving
	 * faster than this interval are discarded before they are ever copied.
	 */
	void SetTargetFrameInterval(double InSeconds) { TargetFrameIntervalSeconds = InSeconds; }

	int64 GetSubmittedFrameCount() const;
	int64 GetDroppedFrameCount() const;

protected:
	virtual bool InitializeCapture() override { return true; }
	virtual bool PostInitializeCaptureViewport(TSharedPtr<FSceneViewport>& InSceneViewport) override { return true; }
	virtual bool ShouldCaptureRHIResource() const override { return false; }

	virtual void OnFrameCaptured_RenderingThread(const FCaptureBaseData& InBaseData,
		TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData,
		void* InBuffer, int32 Width, int32 Height, int32 BytesPerRow) override;

private:
	void WriteFrameDump_RenderingThread(const void* Pixels, int32 Width, int32 Height, int32 BytesPerRow);

	TSharedPtr<FVideoEncoderPipeline, ESPMode::ThreadSafe> Pipeline;

	double CaptureStartTimeSeconds = 0.0;
	double TargetFrameIntervalSeconds = 0.0;
	double LastAcceptedFrameTimeSeconds = -1.0;

	std::atomic<bool> bFrameDumpArmed{ false };
	FCriticalSection FrameDumpLock;
	FString FrameDumpPath;
};
