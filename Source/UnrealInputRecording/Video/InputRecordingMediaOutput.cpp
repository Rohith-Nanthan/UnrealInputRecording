// Copyright (c) Your Studio. All Rights Reserved.

#include "Video/InputRecordingMediaOutput.h"

#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Video/VideoEncoderBackend.h"

// ---------------------------------------------------------------------------------------------
// UInputRecordingMediaOutput
// ---------------------------------------------------------------------------------------------

UInputRecordingMediaOutput::UInputRecordingMediaOutput()
{
	// Three readback buffers: enough that the GPU is never waited on for the frame it just finished,
	// while keeping the capture within one or two frames of the game thread's clock. More buffers
	// would only add latency between what happened and what the .mp4 says happened.
	NumberOfTextureBuffers = 3;
}

bool UInputRecordingMediaOutput::Validate(FString& OutFailureReason) const
{
	if (!Super::Validate(OutFailureReason))
	{
		return false;
	}

	if (OutputFilePath.IsEmpty())
	{
		OutFailureReason = TEXT("No output file path was set on the media output.");
		return false;
	}

	if (Resolution.X <= 0 || Resolution.Y <= 0)
	{
		OutFailureReason = FString::Printf(TEXT("Invalid capture resolution %dx%d."), Resolution.X, Resolution.Y);
		return false;
	}

	if ((Resolution.X & 1) || (Resolution.Y & 1))
	{
		OutFailureReason = FString::Printf(
			TEXT("H.264 requires even dimensions; %dx%d has an odd axis."), Resolution.X, Resolution.Y);
		return false;
	}

	if (!IInputRecordingVideoEncoder::IsSupportedOnThisPlatform())
	{
		OutFailureReason = TEXT("No video encoder backend exists for this platform.");
		return false;
	}

	return true;
}

UMediaCapture* UInputRecordingMediaOutput::CreateMediaCaptureImpl()
{
	UInputRecordingMediaCapture* Capture = NewObject<UInputRecordingMediaCapture>();
	if (Capture)
	{
		Capture->SetMediaOutput(this);
	}
	return Capture;
}

#if WITH_EDITOR
FString UInputRecordingMediaOutput::GetDescriptionString() const
{
	return FString::Printf(TEXT("%dx%d H.264 -> %s"),
		Resolution.X, Resolution.Y, *FPaths::GetCleanFilename(OutputFilePath));
}
#endif

// ---------------------------------------------------------------------------------------------
// UInputRecordingMediaCapture
// ---------------------------------------------------------------------------------------------

bool UInputRecordingMediaCapture::InitializeCapture()
{
	UInputRecordingMediaOutput* Output = Cast<UInputRecordingMediaOutput>(MediaOutput);
	if (!Output)
	{
		UE_LOG(LogInputRecordingVideo, Error,
			TEXT("UInputRecordingMediaCapture was created from a '%s' rather than a ")
			TEXT("UInputRecordingMediaOutput."),
			MediaOutput ? *MediaOutput->GetClass()->GetName() : TEXT("null"));

		SetState(EMediaCaptureState::Error);
		return false;
	}

	// GetDesiredSize() is the size MediaCapture actually settled on after applying the output's
	// requested size and any resize pass. Trusting it rather than Output->Resolution means a mismatch
	// surfaces here as a clear error instead of as a corrupt file later.
	const FIntPoint Size = GetDesiredSize();

	FInputRecordingVideoEncoderConfig Config;
	Config.OutputPath      = Output->OutputFilePath;
	Config.Width           = Size.X;
	Config.Height          = Size.Y;
	Config.FrameRate       = FMath::Max(1, Output->EncoderOptions.TargetFrameRate);
	Config.BitRate         = FMath::Max(1, Output->EncoderOptions.BitRateKbps) * 1000;
	Config.MaxQueuedFrames = FMath::Max(1, Output->EncoderOptions.MaxQueuedFrames);

	TUniquePtr<IInputRecordingVideoEncoder> NewEncoder = IInputRecordingVideoEncoder::Create();
	if (!NewEncoder.IsValid())
	{
		SetState(EMediaCaptureState::Error);
		return false;
	}

	FString Error;
	if (!NewEncoder->Initialize(Config, Error))
	{
		UE_LOG(LogInputRecordingVideo, Error, TEXT("Could not start the video encoder: %s"), *Error);
		SetState(EMediaCaptureState::Error);
		return false;
	}

	{
		FScopeLock Lock(&EncoderLock);
		Encoder = MoveTemp(NewEncoder);
	}

	StartTimeSeconds = FPlatformTime::Seconds();
	FinalEncodedFrames = 0;
	FinalDroppedFrames = 0;

	SetState(EMediaCaptureState::Capturing);
	return true;
}

void UInputRecordingMediaCapture::OnFrameCaptured_RenderingThread(
	const FCaptureBaseData& InBaseData,
	TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData,
	void* InBuffer, int32 Width, int32 Height, int32 BytesPerRow)
{
	FScopeLock Lock(&EncoderLock);

	if (!Encoder.IsValid())
	{
		return;
	}

	// Timestamped on the render thread, which trails the game thread by a frame or two. That constant
	// offset is why UInputRecordingVideoPlayer exposes VideoTimeOffsetSeconds: it is well under the
	// default resync threshold, but it is there, and it is tunable rather than hidden.
	const double ElapsedSeconds = FPlatformTime::Seconds() - StartTimeSeconds;

	Encoder->SubmitFrame_AnyThread(InBuffer, Width, Height, BytesPerRow, ElapsedSeconds);

	if (Encoder->HasFailed())
	{
		SetState(EMediaCaptureState::Error);
	}
}

void UInputRecordingMediaCapture::StopCaptureImpl(bool bAllowPendingFrameToBeProcess)
{
	TUniquePtr<IInputRecordingVideoEncoder> Retired;

	{
		// Take ownership under the lock. Any render-thread callback that is already inside the lock
		// finishes its enqueue first; any that arrives after finds a null encoder and returns.
		FScopeLock Lock(&EncoderLock);
		Retired = MoveTemp(Encoder);
	}

	if (!Retired.IsValid())
	{
		return;
	}

	// Blocks until the queue drains and the moov box is written. Typically a few tens of milliseconds
	// for the frames still in flight - worth paying synchronously so that by the time StopRecording
	// returns, the .mp4 on disk is complete and pairs with the .ghost that was just saved.
	Retired->Finalize();

	FinalEncodedFrames = Retired->GetEncodedFrameCount();
	FinalDroppedFrames = Retired->GetDroppedFrameCount();

	if (FinalDroppedFrames > 0)
	{
		UE_LOG(LogInputRecordingVideo, Warning,
			TEXT("Dropped %lld of %lld captured frames - the encoder could not keep up. Lower ")
			TEXT("Resolution Scale or Bit Rate, or raise Max Queued Frames."),
			FinalDroppedFrames, Retired->GetSubmittedFrameCount());
	}

	Retired.Reset();
}

int32 UInputRecordingMediaCapture::GetEncodedFrameCount() const
{
	FScopeLock Lock(&EncoderLock);
	return static_cast<int32>(Encoder.IsValid() ? Encoder->GetEncodedFrameCount() : FinalEncodedFrames);
}

int32 UInputRecordingMediaCapture::GetDroppedFrameCount() const
{
	FScopeLock Lock(&EncoderLock);
	return static_cast<int32>(Encoder.IsValid() ? Encoder->GetDroppedFrameCount() : FinalDroppedFrames);
}

FString UInputRecordingMediaCapture::GetEncoderError() const
{
	FScopeLock Lock(&EncoderLock);
	return Encoder.IsValid() ? Encoder->GetLastError() : FString();
}
