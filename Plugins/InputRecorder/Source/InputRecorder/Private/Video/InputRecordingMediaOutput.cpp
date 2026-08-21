// Copyright Epic Games, Inc. All Rights Reserved.

#include "Video/InputRecordingMediaOutput.h"

#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "InputRecordingLog.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "Video/VideoEncoderPipeline.h"

// -------------------------------------------------------------------------------------------
// UInputRecordingMediaOutput
// -------------------------------------------------------------------------------------------

bool UInputRecordingMediaOutput::Validate(FString& OutFailureReason) const
{
	if (!Super::Validate(OutFailureReason))
	{
		return false;
	}

	if (OverrideSize != FIntPoint::ZeroValue && (OverrideSize.X < 2 || OverrideSize.Y < 2))
	{
		OutFailureReason = FString::Printf(TEXT("Override size %dx%d is too small to encode."), OverrideSize.X, OverrideSize.Y);
		return false;
	}

	return true;
}

FIntPoint UInputRecordingMediaOutput::GetRequestedSize() const
{
	// The sentinel means "take the buffer size as the requested size", which is the native
	// viewport resolution - the default and the correct answer for a review video.
	return OverrideSize == FIntPoint::ZeroValue ? UMediaOutput::RequestCaptureSourceSize : OverrideSize;
}

EPixelFormat UInputRecordingMediaOutput::GetRequestedPixelFormat() const
{
	// BGRA8 lines up byte-for-byte with Media Foundation's RGB32, so no conversion pass is
	// needed between readback and the sink writer.
	return EPixelFormat::PF_B8G8R8A8;
}

#if WITH_EDITOR
FString UInputRecordingMediaOutput::GetDescriptionString() const
{
	return TEXT("Input Recording viewport capture");
}
#endif

UMediaCapture* UInputRecordingMediaOutput::CreateMediaCaptureImpl()
{
	UMediaCapture* Capture = NewObject<UInputRecordingMediaCapture>();
	if (Capture)
	{
		Capture->SetMediaOutput(this);
	}
	return Capture;
}

// -------------------------------------------------------------------------------------------
// UInputRecordingMediaCapture
// -------------------------------------------------------------------------------------------

void UInputRecordingMediaCapture::SetEncoderPipeline(TSharedPtr<FVideoEncoderPipeline, ESPMode::ThreadSafe> InPipeline)
{
	Pipeline = MoveTemp(InPipeline);
}

void UInputRecordingMediaCapture::ArmFrameDump(const FString& InAbsolutePngPath)
{
	FScopeLock Lock(&FrameDumpLock);
	FrameDumpPath = InAbsolutePngPath;
	bFrameDumpArmed = true;

	UE_LOG(LogRecordingVideo, Log, TEXT("Frame dump armed; the next captured frame goes to %s."), *InAbsolutePngPath);
}

int64 UInputRecordingMediaCapture::GetSubmittedFrameCount() const
{
	return Pipeline.IsValid() ? Pipeline->GetSubmittedFrameCount() : 0;
}

int64 UInputRecordingMediaCapture::GetDroppedFrameCount() const
{
	return Pipeline.IsValid() ? Pipeline->GetDroppedFrameCount() : 0;
}

void UInputRecordingMediaCapture::OnFrameCaptured_RenderingThread(const FCaptureBaseData& InBaseData,
	TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData,
	void* InBuffer, int32 Width, int32 Height, int32 BytesPerRow)
{
	if (!InBuffer || Width <= 0 || Height <= 0)
	{
		return;
	}

	if (bFrameDumpArmed.exchange(false))
	{
		WriteFrameDump_RenderingThread(InBuffer, Width, Height, BytesPerRow);
	}

	if (!Pipeline.IsValid())
	{
		return;
	}

	const double NowSeconds = FPlatformTime::Seconds();

	// Discarded before the copy, not after: a frame the target rate does not want should cost
	// nothing at all on the render thread.
	if (TargetFrameIntervalSeconds > 0.0 && LastAcceptedFrameTimeSeconds >= 0.0 &&
		(NowSeconds - LastAcceptedFrameTimeSeconds) < TargetFrameIntervalSeconds)
	{
		return;
	}
	LastAcceptedFrameTimeSeconds = NowSeconds;

	// Timestamps come from the wall clock rather than the frame counter so the video and the
	// input timeline share an origin even when frames are dropped.
	const int64 TimestampMicroseconds = static_cast<int64>((NowSeconds - CaptureStartTimeSeconds) * 1000000.0);

	// Copies and returns. Encoding here would destroy frame time.
	Pipeline->SubmitFrame_AnyThread(InBuffer, Width, Height, BytesPerRow, FMath::Max<int64>(0, TimestampMicroseconds));
}

void UInputRecordingMediaCapture::WriteFrameDump_RenderingThread(const void* Pixels, int32 Width, int32 Height, int32 BytesPerRow)
{
	FString Path;
	{
		FScopeLock Lock(&FrameDumpLock);
		Path = FrameDumpPath;
	}

	if (Path.IsEmpty())
	{
		return;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!Wrapper.IsValid())
	{
		UE_LOG(LogRecordingVideo, Error, TEXT("Could not create a PNG wrapper for the frame dump."));
		return;
	}

	// The readback buffer can be padded, and SetRaw wants tightly packed rows. Repacking here
	// keeps the dump an honest picture of the pixels rather than of the padding.
	const int32 TightRowBytes = Width * 4;
	TArray<uint8> Packed;
	Packed.SetNumUninitialized(TightRowBytes * Height);

	const uint8* Source = static_cast<const uint8*>(Pixels);
	for (int32 Row = 0; Row < Height; ++Row)
	{
		FMemory::Memcpy(Packed.GetData() + Row * TightRowBytes, Source + static_cast<SIZE_T>(Row) * BytesPerRow, TightRowBytes);
	}

	if (!Wrapper->SetRaw(Packed.GetData(), Packed.Num(), Width, Height, ERGBFormat::BGRA, 8))
	{
		UE_LOG(LogRecordingVideo, Error, TEXT("PNG wrapper rejected the %dx%d frame."), Width, Height);
		return;
	}

	const TArray64<uint8>& Png = Wrapper->GetCompressed(100);
	if (FFileHelper::SaveArrayToFile(Png, *Path))
	{
		UE_LOG(LogRecordingVideo, Log,
			TEXT("Frame dump written: %s (%dx%d, %d bytes per row). This is exactly what the encoder receives - ")
			TEXT("compare it against the .mp4 to settle the orientation question on this machine."),
			*Path, Width, Height, BytesPerRow);
	}
	else
	{
		UE_LOG(LogRecordingVideo, Error, TEXT("Could not write the frame dump to %s."), *Path);
	}
}
