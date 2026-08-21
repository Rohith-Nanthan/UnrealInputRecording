// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Templates/SharedPointer.h"
#include "Video/InputRecordingVideoTypes.h"
#include "InputRecordingVideoCapture.generated.h"

class FVideoEncoderPipeline;
class UInputRecordingMediaCapture;
class UInputRecordingMediaOutput;

/**
 * The Unreal-facing half of video capture: owns the media output, the capture object and the
 * encoder pipeline, and hides all three behind start / stop.
 *
 * Every failure here is non-fatal by design. No viewport, no encoder, no platform support - log
 * it and let input recording continue. A .ghost with no .mp4 is a usable recording; a lost
 * .ghost is a take somebody has to re-perform.
 */
UCLASS(BlueprintType)
class INPUTRECORDER_API UInputRecordingVideoCapture : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Begins capturing the active game viewport into AbsoluteVideoPath.
	 * Returns false when capture could not start; the caller keeps recording input regardless.
	 */
	bool StartCapture(const FString& AbsoluteVideoPath, const FInputRecordingVideoOptions& Options);

	/** Stops capture and blocks until the queue drains and the file closes. */
	void StopCapture();

	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	bool IsCapturing() const { return bCapturing; }

	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	FString GetCurrentVideoPath() const { return CurrentVideoPath; }

	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	FIntPoint GetCaptureResolution() const { return CaptureResolution; }

	/** Bytes on disk for the file being written right now, for the live quota poll. */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	int64 GetCurrentVideoBytesOnDisk() const;

	/**
	 * Arms the one-shot first-frame PNG dump next to the .mp4. Build this and check it on the
	 * target machine before trusting any orientation setting - see EInputRecordingVideoOrientation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Video")
	bool ArmFrameDump();

	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	int64 GetSubmittedFrameCount() const;

	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	int64 GetDroppedFrameCount() const;

	/** True when this build has an encoder backend at all. */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	static bool IsVideoCaptureSupported();

private:
	/** Native viewport size, snapped down to even because H.264 refuses odd dimensions. */
	bool ResolveCaptureResolution(const FInputRecordingVideoOptions& Options, FIntPoint& OutResolution) const;

	UPROPERTY(Transient)
	TObjectPtr<UInputRecordingMediaOutput> MediaOutput;

	UPROPERTY(Transient)
	TObjectPtr<UInputRecordingMediaCapture> MediaCapture;

	TSharedPtr<FVideoEncoderPipeline, ESPMode::ThreadSafe> Pipeline;

	FString CurrentVideoPath;
	FIntPoint CaptureResolution = FIntPoint::ZeroValue;
	bool bCapturing = false;
};
