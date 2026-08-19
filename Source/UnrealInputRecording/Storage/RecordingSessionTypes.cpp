// Copyright (c) Your Studio. All Rights Reserved.

#include "Storage/RecordingSessionTypes.h"

#include "InputReplay/InputReplayTypes.h"
#include "Misc/Paths.h"
#include "Video/InputRecordingVideoTypes.h"

DEFINE_LOG_CATEGORY(LogRecordingStore);

namespace RecordingStore
{
	FString FormatBytes(int64 Bytes)
	{
		if (Bytes < BytesPerMegabyte)
		{
			// Sub-megabyte files are almost always a .ghost or a manifest, where the exact size is
			// more useful than a rounded fraction of a megabyte.
			return FString::Printf(TEXT("%.1f KB"), Bytes / 1024.0);
		}

		return FString::Printf(TEXT("%.1f MB"), Bytes / static_cast<double>(BytesPerMegabyte));
	}
}

FString FRecordingSessionInfo::MakeFolderName(int32 InIndex)
{
	return FString::Printf(TEXT("%s%d"), RecordingStore::SessionFolderPrefix, InIndex);
}

int32 FRecordingSessionInfo::ParseFolderName(const FString& InFolderName)
{
	if (!InFolderName.StartsWith(RecordingStore::SessionFolderPrefix, ESearchCase::CaseSensitive))
	{
		return INDEX_NONE;
	}

	const FString Suffix = InFolderName.RightChop(FCString::Strlen(RecordingStore::SessionFolderPrefix));

	// IsNumeric would accept "+7", "-7" and "7.0". A session index is a run of digits and nothing
	// else, so anything looser risks two folder names parsing to the same index.
	if (Suffix.IsEmpty())
	{
		return INDEX_NONE;
	}

	for (const TCHAR Character : Suffix)
	{
		if (!FChar::IsDigit(Character))
		{
			return INDEX_NONE;
		}
	}

	return FCString::Atoi(*Suffix);
}

FString FRecordingSessionInfo::GetBasePath() const
{
	if (AbsolutePath.IsEmpty() || FolderName.IsEmpty())
	{
		return FString();
	}

	return FPaths::Combine(AbsolutePath, FolderName);
}

FString FRecordingSessionInfo::GetGhostPath() const
{
	const FString Base = GetBasePath();
	return Base.IsEmpty() ? Base : Base + InputReplay::BinaryExtension;
}

FString FRecordingSessionInfo::GetJsonPath() const
{
	const FString Base = GetBasePath();
	return Base.IsEmpty() ? Base : Base + InputReplay::JsonExtension;
}

FString FRecordingSessionInfo::GetVideoPath() const
{
	const FString Base = GetBasePath();
	return Base.IsEmpty() ? Base : Base + InputRecordingVideo::VideoExtension;
}

FString FRecordingSessionInfo::GetManifestPath() const
{
	return AbsolutePath.IsEmpty()
		? FString()
		: FPaths::Combine(AbsolutePath, RecordingStore::ManifestFileName);
}
