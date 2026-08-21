// Copyright Epic Games, Inc. All Rights Reserved.

#include "Store/RecordingSessionTypes.h"

#include "Misc/Paths.h"

namespace RecordingSessionTypesPrivate
{
	const TCHAR* FolderPrefix = TEXT("Recording_");
	const TCHAR* ManifestFileName = TEXT("Session.json");
}

FString FRecordingSessionInfo::GetBasePath() const
{
	if (AbsolutePath.IsEmpty() || FolderName.IsEmpty())
	{
		return FString();
	}

	// Every file inside is named after the folder, so a session copied to a desk somewhere is
	// still self-describing.
	return FPaths::Combine(AbsolutePath, FolderName);
}

FString FRecordingSessionInfo::GetGhostPath() const
{
	const FString Base = GetBasePath();
	return Base.IsEmpty() ? Base : Base + TEXT(".ghost");
}

FString FRecordingSessionInfo::GetJsonPath() const
{
	const FString Base = GetBasePath();
	return Base.IsEmpty() ? Base : Base + TEXT(".ghost.json");
}

FString FRecordingSessionInfo::GetVideoPath() const
{
	const FString Base = GetBasePath();
	return Base.IsEmpty() ? Base : Base + TEXT(".mp4");
}

FString FRecordingSessionInfo::GetManifestPath() const
{
	return AbsolutePath.IsEmpty() ? FString() : FPaths::Combine(AbsolutePath, RecordingSessionTypesPrivate::ManifestFileName);
}

FString FRecordingSessionInfo::MakeFolderName(int32 InIndex)
{
	return FString::Printf(TEXT("%s%d"), RecordingSessionTypesPrivate::FolderPrefix, InIndex);
}

bool FRecordingSessionInfo::ParseFolderName(const FString& Name, int32& OutIndex)
{
	OutIndex = INDEX_NONE;

	if (!Name.StartsWith(RecordingSessionTypesPrivate::FolderPrefix, ESearchCase::CaseSensitive))
	{
		return false;
	}

	const FString Suffix = Name.Mid(FCString::Strlen(RecordingSessionTypesPrivate::FolderPrefix));
	if (Suffix.IsEmpty() || !Suffix.IsNumeric())
	{
		return false;
	}

	OutIndex = FCString::Atoi(*Suffix);
	return OutIndex >= 0;
}
