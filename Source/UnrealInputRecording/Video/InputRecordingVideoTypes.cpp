// Copyright (c) Your Studio. All Rights Reserved.

#include "Video/InputRecordingVideoTypes.h"

#include "HAL/FileManager.h"
#include "InputReplay/InputReplaySerializer.h"
#include "Misc/Paths.h"
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
