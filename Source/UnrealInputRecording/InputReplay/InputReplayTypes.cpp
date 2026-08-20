// Copyright Epic Games, Inc. All Rights Reserved.

#include "InputReplay/InputReplayTypes.h"

FString FInputRecordingHeader::GetActionShortName(int32 ActionIndex) const
{
	if (!ActionPaths.IsValidIndex(ActionIndex))
	{
		return FString();
	}

	// ActionPaths hold full soft object paths ("/Game/Input/Actions/IA_Jump.IA_Jump"); every
	// consumer wants the readable tail.
	const FString& Path = ActionPaths[ActionIndex];
	int32 DotIndex = INDEX_NONE;
	if (Path.FindLastChar(TEXT('.'), DotIndex))
	{
		return Path.Mid(DotIndex + 1);
	}

	int32 SlashIndex = INDEX_NONE;
	if (Path.FindLastChar(TEXT('/'), SlashIndex))
	{
		return Path.Mid(SlashIndex + 1);
	}

	return Path;
}

bool FInputRecording::IsValidRecording() const
{
	return Header.RecordingId.IsValid() && Header.ActionPaths.Num() > 0;
}

float FInputRecording::GetDurationSeconds() const
{
	// Prefer the frame count: it is authoritative, and the trailing sample's float timestamp
	// stops short of the real end of the take whenever the last input was released early.
	if (Header.TotalFrames > 0 && Header.LogicalTicksPerSecond > 0)
	{
		return static_cast<float>(Header.TotalFrames) / static_cast<float>(Header.LogicalTicksPerSecond);
	}

	return Samples.Num() > 0 ? Samples.Last().TimeSeconds : 0.0f;
}

void FInputRecording::Reset()
{
	Header = FInputRecordingHeader();
	Samples.Reset();
	FrameDeltaSeconds.Reset();
}
