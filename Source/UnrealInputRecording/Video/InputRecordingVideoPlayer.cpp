// Copyright Epic Games, Inc. All Rights Reserved.

#include "Video/InputRecordingVideoPlayer.h"

#include "HAL/PlatformFileManager.h"
#include "InputRecordingLog.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"

void UInputRecordingVideoPlayer::EnsureObjects()
{
	if (!MediaPlayer)
	{
		MediaPlayer = NewObject<UMediaPlayer>(this, NAME_None, RF_Transient);
		MediaPlayer->PlayOnOpen = false;
		MediaPlayer->SetLooping(false);
	}

	if (!MediaTexture)
	{
		MediaTexture = NewObject<UMediaTexture>(this, NAME_None, RF_Transient);

		// Without AutoClear the last decoded frame lingers after the player closes, so an empty
		// review screen shows a still from the previous session.
		MediaTexture->AutoClear = true;
		MediaTexture->SetMediaPlayer(MediaPlayer);
		MediaTexture->UpdateResource();
	}
}

bool UInputRecordingVideoPlayer::OpenVideo(const FString& AbsoluteVideoPath)
{
	CloseVideo();

	if (AbsoluteVideoPath.IsEmpty())
	{
		return false;
	}

	if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*AbsoluteVideoPath))
	{
		// Entirely normal: a take recorded on a machine with no encoder has a .ghost and no .mp4.
		UE_LOG(LogRecordingVideo, Log, TEXT("No video file at %s; review will run without picture."), *AbsoluteVideoPath);
		return false;
	}

	EnsureObjects();

	if (!MediaPlayer->OpenFile(AbsoluteVideoPath))
	{
		UE_LOG(LogRecordingVideo, Warning, TEXT("Media player refused to open %s."), *AbsoluteVideoPath);
		return false;
	}

	OpenVideoPath = AbsoluteVideoPath;
	bVideoOpen = true;

	UE_LOG(LogRecordingVideo, Log, TEXT("Opened video %s."), *AbsoluteVideoPath);
	return true;
}

void UInputRecordingVideoPlayer::CloseVideo()
{
	if (MediaPlayer)
	{
		MediaPlayer->Close();
	}

	OpenVideoPath.Reset();
	bVideoOpen = false;
}

float UInputRecordingVideoPlayer::GetPlaybackSeconds() const
{
	return MediaPlayer ? static_cast<float>(MediaPlayer->GetTime().GetTotalSeconds()) : 0.0f;
}

float UInputRecordingVideoPlayer::GetDurationSeconds() const
{
	return MediaPlayer ? static_cast<float>(MediaPlayer->GetDuration().GetTotalSeconds()) : 0.0f;
}

void UInputRecordingVideoPlayer::SyncToClock(float ClockSeconds, bool bClockIsRunning)
{
	if (!bVideoOpen || !MediaPlayer || !MediaPlayer->IsReady())
	{
		return;
	}

	const bool bIsPlaying = MediaPlayer->IsPlaying();

	if (bClockIsRunning && !bIsPlaying)
	{
		MediaPlayer->Play();
	}
	else if (!bClockIsRunning && bIsPlaying)
	{
		// The clock is frozen on a cue, so the picture freezes with it.
		MediaPlayer->Pause();
	}

	const float PlaybackSeconds = GetPlaybackSeconds();
	if (FMath::Abs(PlaybackSeconds - ClockSeconds) > ResyncThresholdSeconds && MediaPlayer->SupportsSeeking())
	{
		MediaPlayer->Seek(FTimespan::FromSeconds(ClockSeconds));
	}
}
