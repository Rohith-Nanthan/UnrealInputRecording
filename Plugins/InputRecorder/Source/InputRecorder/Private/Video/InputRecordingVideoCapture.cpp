// Copyright Epic Games, Inc. All Rights Reserved.

#include "Video/InputRecordingVideoCapture.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "HAL/PlatformFileManager.h"
#include "InputRecordingLog.h"
#include "MediaCapture.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Video/InputRecordingMediaOutput.h"
#include "Video/IVideoEncoderBackend.h"
#include "Video/VideoEncoderPipeline.h"

bool UInputRecordingVideoCapture::IsVideoCaptureSupported()
{
	// Creating and discarding a backend is the only honest test: a platform can have the headers
	// and still have no working encoder.
	return VideoEncoderBackend::Create().IsValid();
}

bool UInputRecordingVideoCapture::ResolveCaptureResolution(const FInputRecordingVideoOptions& Options, FIntPoint& OutResolution) const
{
	FIntPoint Resolution = FIntPoint::ZeroValue;

	if (Options.bOverrideResolution)
	{
		Resolution = Options.ForcedResolution;
	}
	else if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		Resolution = GEngine->GameViewport->Viewport->GetSizeXY();
	}

	if (Resolution.X < 2 || Resolution.Y < 2)
	{
		return false;
	}

	// H.264 refuses odd dimensions. Snapping down rather than up guarantees the encoder never
	// reads past the end of a captured row.
	OutResolution = FIntPoint(Resolution.X & ~1, Resolution.Y & ~1);
	return true;
}

bool UInputRecordingVideoCapture::StartCapture(const FString& AbsoluteVideoPath, const FInputRecordingVideoOptions& Options)
{
	if (bCapturing)
	{
		UE_LOG(LogRecordingVideo, Warning, TEXT("StartCapture ignored - already capturing to %s."), *CurrentVideoPath);
		return false;
	}

	if (AbsoluteVideoPath.IsEmpty())
	{
		UE_LOG(LogRecordingVideo, Error, TEXT("StartCapture called with an empty output path."));
		return false;
	}

	FIntPoint Resolution;
	if (!ResolveCaptureResolution(Options, Resolution))
	{
		UE_LOG(LogRecordingVideo, Warning,
			TEXT("No usable game viewport, so this take will have no video. Input recording continues."));
		return false;
	}

	TUniquePtr<IVideoEncoderBackend> Backend = VideoEncoderBackend::Create();
	if (!Backend)
	{
		UE_LOG(LogRecordingVideo, Warning,
			TEXT("No video encoder backend on this platform, so this take will have no video. Input recording continues."));
		return false;
	}

	FVideoEncoderInitParams Params;
	Params.Width = Resolution.X;
	Params.Height = Resolution.Y;
	Params.FrameRate = FMath::Max(1, Options.TargetFrameRate);
	Params.BitRate = Options.GetBitrateBitsPerSecond();
	Params.OutputPath = AbsoluteVideoPath;

	// Auto resolves here, once, so no backend ever has to reason about it. TopDown is the
	// documented default because UMediaCapture's CPU readback delivers frames top-down and the
	// sink writer is told so through MF_MT_DEFAULT_STRIDE - but see ir.video.dumpframe before
	// trusting that on an unfamiliar machine.
	Params.bSourceIsBottomUp = Options.Orientation == EInputRecordingVideoOrientation::BottomUp;

	TSharedPtr<FVideoEncoderPipeline, ESPMode::ThreadSafe> NewPipeline = MakeShared<FVideoEncoderPipeline, ESPMode::ThreadSafe>();
	if (!NewPipeline->Initialize(MoveTemp(Backend), Params, Options.MaxQueuedFrames))
	{
		UE_LOG(LogRecordingVideo, Warning,
			TEXT("Encoder refused to start for %s, so this take will have no video. Input recording continues."),
			*AbsoluteVideoPath);
		return false;
	}

	MediaOutput = NewObject<UInputRecordingMediaOutput>(this);
	MediaOutput->OverrideSize = Options.bOverrideResolution ? Resolution : FIntPoint::ZeroValue;
	MediaOutput->NumberOfTextureBuffers = FMath::Clamp(Options.MaxQueuedFrames, 2, 8);

	MediaCapture = Cast<UInputRecordingMediaCapture>(MediaOutput->CreateMediaCapture());
	if (!MediaCapture)
	{
		UE_LOG(LogRecordingVideo, Warning, TEXT("Media output could not create a capture object; no video this take."));
		NewPipeline->Finalize();
		MediaOutput = nullptr;
		return false;
	}

	MediaCapture->SetEncoderPipeline(NewPipeline);
	MediaCapture->SetCaptureStartTime(FPlatformTime::Seconds());
	MediaCapture->SetTargetFrameInterval(1.0 / static_cast<double>(Params.FrameRate));

	FMediaCaptureOptions CaptureOptions;
	CaptureOptions.bAutoRestartOnSourceSizeChange = false;
	CaptureOptions.OverrunAction = EMediaCaptureOverrunAction::Skip;

	if (!MediaCapture->CaptureActiveSceneViewport(CaptureOptions))
	{
		UE_LOG(LogRecordingVideo, Warning,
			TEXT("CaptureActiveSceneViewport failed, so this take will have no video. Input recording continues."));
		MediaCapture->SetEncoderPipeline(nullptr);
		NewPipeline->Finalize();
		MediaCapture = nullptr;
		MediaOutput = nullptr;
		return false;
	}

	Pipeline = NewPipeline;
	CurrentVideoPath = AbsoluteVideoPath;
	CaptureResolution = Resolution;
	bCapturing = true;

	UE_LOG(LogRecordingVideo, Log, TEXT("Video capture started: %dx%d @ %d fps into %s."),
		Resolution.X, Resolution.Y, Params.FrameRate, *AbsoluteVideoPath);

	return true;
}

void UInputRecordingVideoCapture::StopCapture()
{
	if (!bCapturing)
	{
		return;
	}

	bCapturing = false;

	if (MediaCapture)
	{
		// Let queued frames through: the tail of a take is usually the interesting part.
		MediaCapture->StopCapture(/*bAllowPendingFrameToBeProcess=*/true);
		MediaCapture->SetEncoderPipeline(nullptr);
		MediaCapture = nullptr;
	}

	if (Pipeline.IsValid())
	{
		// Blocks until the queue drains and the file closes, so the .mp4 and the .ghost are
		// always written together by the same stop call.
		Pipeline->Finalize();
		Pipeline.Reset();
	}

	MediaOutput = nullptr;

	UE_LOG(LogRecordingVideo, Log, TEXT("Video capture stopped: %s"), *CurrentVideoPath);
}

int64 UInputRecordingVideoCapture::GetCurrentVideoBytesOnDisk() const
{
	if (CurrentVideoPath.IsEmpty())
	{
		return 0;
	}

	const int64 Size = FPlatformFileManager::Get().GetPlatformFile().FileSize(*CurrentVideoPath);
	return Size > 0 ? Size : 0;
}

bool UInputRecordingVideoCapture::ArmFrameDump()
{
	if (!bCapturing || !MediaCapture)
	{
		UE_LOG(LogRecordingVideo, Warning, TEXT("ir.video.dumpframe needs a capture in progress; start a take first."));
		return false;
	}

	// Beside the .mp4 on purpose - the two belong together when somebody is comparing them.
	const FString DumpPath = FPaths::ChangeExtension(CurrentVideoPath, TEXT(".firstframe.png"));
	MediaCapture->ArmFrameDump(DumpPath);
	return true;
}

int64 UInputRecordingVideoCapture::GetSubmittedFrameCount() const
{
	return Pipeline.IsValid() ? Pipeline->GetSubmittedFrameCount() : 0;
}

int64 UInputRecordingVideoCapture::GetDroppedFrameCount() const
{
	return Pipeline.IsValid() ? Pipeline->GetDroppedFrameCount() : 0;
}
