// Copyright (c) Your Studio. All Rights Reserved.

#include "Video/InputRecordingVideoPlayer.h"

#include "HAL/FileManager.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "Video/InputRecordingVideoTypes.h"

void UInputRecordingVideoPlayer::EnsurePlayerCreated()
{
	if (MediaPlayer && MediaTexture)
	{
		return;
	}

	if (!MediaPlayer)
	{
		MediaPlayer = NewObject<UMediaPlayer>(this, NAME_None, RF_Transient);

		// The whole point is that the match system decides when the video moves. Auto-play on open
		// would start it running before the first cue is even presented.
		MediaPlayer->PlayOnOpen = false;
		MediaPlayer->SetLooping(false);

		MediaPlayer->OnMediaOpened.AddDynamic(this, &UInputRecordingVideoPlayer::HandleMediaOpened);
		MediaPlayer->OnMediaOpenFailed.AddDynamic(this, &UInputRecordingVideoPlayer::HandleMediaOpenFailed);
		MediaPlayer->OnMediaClosed.AddDynamic(this, &UInputRecordingVideoPlayer::HandleMediaClosed);
	}

	if (!MediaTexture)
	{
		MediaTexture = NewObject<UMediaTexture>(this, NAME_None, RF_Transient);

		// Without AutoClear the texture keeps showing the last decoded frame after Close(), which reads
		// as "the video is still there" long after the session ended.
		MediaTexture->AutoClear = true;
		MediaTexture->SetMediaPlayer(MediaPlayer);
		MediaTexture->UpdateResource();
	}
}

// ---------------------------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------------------------

bool UInputRecordingVideoPlayer::OpenRecordingVideo(const FString& RecordingName)
{
	const FString Path = UInputRecordingVideoLibrary::ResolveVideoPath(RecordingName);

	if (Path.IsEmpty() || !IFileManager::Get().FileExists(*Path))
	{
		UE_LOG(LogInputRecordingVideo, Log,
			TEXT("No paired video for recording '%s' (looked for '%s'). Match Input will run without it."),
			*RecordingName, *Path);

		OnVideoOpened.Broadcast(/*bSuccess=*/false, Path);
		return false;
	}

	return OpenVideoFile(Path);
}

bool UInputRecordingVideoPlayer::OpenVideoFile(const FString& AbsolutePath)
{
	EnsurePlayerCreated();

	if (AbsolutePath.IsEmpty())
	{
		return false;
	}

	Close();

	OpenPath = AbsolutePath;
	bOpenRequested = true;
	PendingSeekSeconds = 0.0f;

	if (!MediaPlayer->OpenFile(AbsolutePath))
	{
		UE_LOG(LogInputRecordingVideo, Error,
			TEXT("UMediaPlayer::OpenFile('%s') was rejected. On Windows this normally means no media ")
			TEXT("player plugin claimed the file - check that WmfMedia (or ElectraPlayer) is enabled."),
			*AbsolutePath);

		bOpenRequested = false;
		OpenPath.Reset();
		OnVideoOpened.Broadcast(/*bSuccess=*/false, AbsolutePath);
		return false;
	}

	UE_LOG(LogInputRecordingVideo, Log, TEXT("Opening video '%s'."), *AbsolutePath);
	return true;
}

void UInputRecordingVideoPlayer::Close()
{
	bOpenRequested = false;
	PendingSeekSeconds = -1.0f;
	OpenPath.Reset();

	if (MediaPlayer)
	{
		MediaPlayer->Close();
	}
}

void UInputRecordingVideoPlayer::PauseVideo()
{
	if (!IsVideoReady() || MediaPlayer->IsPaused())
	{
		return;
	}

	// SetRate(0) rather than Pause(): every backend that can play can also hold at rate zero, whereas
	// CanPause() is false for some sources and Pause() then silently does nothing.
	MediaPlayer->SetRate(0.0f);
}

void UInputRecordingVideoPlayer::ResumeVideo()
{
	if (!IsVideoReady() || MediaPlayer->IsPlaying())
	{
		return;
	}

	MediaPlayer->SetRate(1.0f);
}

void UInputRecordingVideoPlayer::SeekToSeconds(float Seconds)
{
	if (!MediaPlayer)
	{
		return;
	}

	const float Clamped = FMath::Max(0.0f, Seconds);

	if (!IsVideoReady())
	{
		// Remember it; HandleMediaOpened will apply it once the file is actually parsed.
		PendingSeekSeconds = Clamped;
		return;
	}

	if (!MediaPlayer->SupportsSeeking())
	{
		return;
	}

	const float Duration = GetDurationSeconds();
	const float Target = (Duration > 0.0f) ? FMath::Min(Clamped, Duration) : Clamped;

	MediaPlayer->Seek(FTimespan::FromSeconds(Target));
}

void UInputRecordingVideoPlayer::RestartFromBeginning()
{
	SeekToSeconds(0.0f);
	PauseVideo();
}

// ---------------------------------------------------------------------------------------------
// Sync
// ---------------------------------------------------------------------------------------------

void UInputRecordingVideoPlayer::SyncToMatchClock(float MatchClockSeconds, bool bAwaitingInput)
{
	if (!IsVideoReady())
	{
		return;
	}

	const float Duration = GetDurationSeconds();
	float Target = MatchClockSeconds + VideoTimeOffsetSeconds;
	Target = (Duration > 0.0f) ? FMath::Clamp(Target, 0.0f, Duration) : FMath::Max(0.0f, Target);

	const float Position = GetPositionSeconds();
	const float Error = FMath::Abs(Position - Target);

	if (bAwaitingInput)
	{
		// Blocked on the player. The clock is frozen at the cue's timestamp, so the video is too - and
		// stays there through any number of wrong inputs, because nothing in this branch resumes it.
		PauseVideo();

		// A pause never lands on the exact frame (the decoder was mid-frame when the cue fired), so
		// pull it onto the cue once. The threshold stops this re-firing every frame while paused.
		if (Error > ResyncThresholdSeconds)
		{
			SeekToSeconds(Target);
		}

		return;
	}

	// Counting down an interval: the video should be running.
	ResumeVideo();

	if (bResyncToMatchClock && Error > ResyncThresholdSeconds)
	{
		SeekToSeconds(Target);
	}
}

// ---------------------------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------------------------

bool UInputRecordingVideoPlayer::IsVideoOpen() const
{
	return MediaPlayer != nullptr && (bOpenRequested || MediaPlayer->IsReady());
}

bool UInputRecordingVideoPlayer::IsVideoReady() const
{
	return MediaPlayer != nullptr && MediaPlayer->IsReady();
}

float UInputRecordingVideoPlayer::GetPositionSeconds() const
{
	return MediaPlayer ? static_cast<float>(MediaPlayer->GetTime().GetTotalSeconds()) : 0.0f;
}

float UInputRecordingVideoPlayer::GetDurationSeconds() const
{
	return MediaPlayer ? static_cast<float>(MediaPlayer->GetDuration().GetTotalSeconds()) : 0.0f;
}

float UInputRecordingVideoPlayer::GetProgress() const
{
	const float Duration = GetDurationSeconds();
	return (Duration > KINDA_SMALL_NUMBER)
		? FMath::Clamp(GetPositionSeconds() / Duration, 0.0f, 1.0f)
		: 0.0f;
}

bool UInputRecordingVideoPlayer::IsVideoPaused() const
{
	return MediaPlayer != nullptr && MediaPlayer->IsPaused();
}

UMediaTexture* UInputRecordingVideoPlayer::GetMediaTexture()
{
	EnsurePlayerCreated();
	return MediaTexture;
}

UMediaPlayer* UInputRecordingVideoPlayer::GetMediaPlayer()
{
	EnsurePlayerCreated();
	return MediaPlayer;
}

// ---------------------------------------------------------------------------------------------
// Media player events
// ---------------------------------------------------------------------------------------------

void UInputRecordingVideoPlayer::HandleMediaOpened(FString OpenedUrl)
{
	bOpenRequested = false;

	UE_LOG(LogInputRecordingVideo, Log, TEXT("Video ready: '%s' (%.2fs)."),
		*OpenedUrl, GetDurationSeconds());

	if (PendingSeekSeconds >= 0.0f)
	{
		const float Seek = PendingSeekSeconds;
		PendingSeekSeconds = -1.0f;
		SeekToSeconds(Seek);
	}

	// Held on the first frame until MatchInput says otherwise.
	PauseVideo();

	OnVideoOpened.Broadcast(/*bSuccess=*/true, OpenPath);
}

void UInputRecordingVideoPlayer::HandleMediaOpenFailed(FString FailedUrl)
{
	bOpenRequested = false;
	PendingSeekSeconds = -1.0f;

	UE_LOG(LogInputRecordingVideo, Error,
		TEXT("Failed to open video '%s'. If the file exists and is non-zero, the take probably ended ")
		TEXT("before the encoder wrote the index - check the log for encoder errors."),
		*FailedUrl);

	OnVideoOpened.Broadcast(/*bSuccess=*/false, FailedUrl);
}

void UInputRecordingVideoPlayer::HandleMediaClosed()
{
	bOpenRequested = false;
	PendingSeekSeconds = -1.0f;
}

void UInputRecordingVideoPlayer::BeginDestroy()
{
	if (MediaPlayer)
	{
		MediaPlayer->OnMediaOpened.RemoveAll(this);
		MediaPlayer->OnMediaOpenFailed.RemoveAll(this);
		MediaPlayer->OnMediaClosed.RemoveAll(this);
		MediaPlayer->Close();
	}

	Super::BeginDestroy();
}
