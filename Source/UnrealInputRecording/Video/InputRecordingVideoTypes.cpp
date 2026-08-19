// Copyright (c) Your Studio. All Rights Reserved.

#include "Video/InputRecordingVideoTypes.h"

#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "InputReplay/InputReplaySerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Video/VideoEncoderBackend.h"

DEFINE_LOG_CATEGORY(LogInputRecordingVideo);

FString UInputRecordingVideoLibrary::ResolveVideoPath(const FString& RecordingName)
{
	if (RecordingName.IsEmpty())
	{
		return FString();
	}

	// Already fully specified - do not second-guess the caller.
	if (RecordingName.EndsWith(InputRecordingVideo::VideoExtension, ESearchCase::IgnoreCase))
	{
		return FPaths::IsRelative(RecordingName)
			? FPaths::Combine(UInputReplaySerializer::GetRecordingDirectory(), RecordingName)
			: RecordingName;
	}

	if (!FPaths::IsRelative(RecordingName))
	{
		return RecordingName + InputRecordingVideo::VideoExtension;
	}

	// Strip a .ghost / .ghost.json suffix if one was passed through by mistake, so callers can hand us
	// whatever name they happen to be holding.
	FString BareName = RecordingName;
	BareName.RemoveFromEnd(InputReplay::JsonExtension, ESearchCase::IgnoreCase);
	BareName.RemoveFromEnd(InputReplay::BinaryExtension, ESearchCase::IgnoreCase);

	return FPaths::Combine(
		UInputReplaySerializer::GetRecordingDirectory(),
		BareName + InputRecordingVideo::VideoExtension);
}

bool UInputRecordingVideoLibrary::DoesVideoExist(const FString& RecordingName)
{
	const FString Path = ResolveVideoPath(RecordingName);
	return !Path.IsEmpty() && IFileManager::Get().FileExists(*Path);
}

bool UInputRecordingVideoLibrary::IsVideoCaptureSupported()
{
	return IInputRecordingVideoEncoder::IsSupportedOnThisPlatform();
}

void InputRecordingVideo::DumpBgraFrameToPng(const uint8* Bgra, int32 Width, int32 Height, const FString& PngPath)
{
	if (!Bgra || Width <= 0 || Height <= 0 || PngPath.IsEmpty())
	{
		return;
	}

	IImageWrapperModule& ImageWrapperModule =
		FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));

	const TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!Wrapper.IsValid())
	{
		UE_LOG(LogInputRecordingVideo, Warning, TEXT("Could not create a PNG image wrapper for the frame dump."));
		return;
	}

	// BGRA in, BGRA declared. No swizzle and no flip: the point of the dump is to show the bytes the
	// encoder was handed, so any transformation here would defeat it.
	const int64 NumBytes = static_cast<int64>(Width) * static_cast<int64>(Height) * 4;
	if (!Wrapper->SetRaw(Bgra, NumBytes, Width, Height, ERGBFormat::BGRA, 8))
	{
		UE_LOG(LogInputRecordingVideo, Warning, TEXT("IImageWrapper::SetRaw refused a %dx%d BGRA frame."), Width, Height);
		return;
	}

	const TArray64<uint8>& PngBytes = Wrapper->GetCompressed(100);

	if (FFileHelper::SaveArrayToFile(PngBytes, *PngPath))
	{
		UE_LOG(LogInputRecordingVideo, Log,
			TEXT("Frame dumped to '%s'. Upright here means the encoder is being fed upright frames, so ")
			TEXT("an inverted .mp4 is the encoder's doing - set Orientation to Bottom-up. Inverted here ")
			TEXT("means the capture side is at fault."),
			*PngPath);
	}
	else
	{
		UE_LOG(LogInputRecordingVideo, Warning, TEXT("Could not write the frame dump to '%s'."), *PngPath);
	}
}
