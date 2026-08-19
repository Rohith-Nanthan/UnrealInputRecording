// Copyright (c) Your Studio. All Rights Reserved.

#include "Video/InputRecordingScreenRecorder.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Video/InputRecordingMediaOutput.h"
#include "Video/VideoEncoderBackend.h"

UInputRecordingScreenRecorder::UInputRecordingScreenRecorder()
	: bCaptureIncludingUI(false)
{
}

void UInputRecordingScreenRecorder::BeginDestroy()
{
	// A capture left running past the owning subsystem's lifetime would keep issuing render commands
	// against a viewport that is going away. Stop it here rather than relying on the caller.
	StopCapture();

	Super::BeginDestroy();
}

FIntPoint UInputRecordingScreenRecorder::ResolveCaptureResolution(UObject* WorldContext) const
{
	FIntPoint Size = Options.ForcedResolution;

	if (!Options.bOverrideResolution)
	{
		// Native viewport resolution, 1:1, no scale factor. See FInputRecordingVideoOptions for why
		// there is no downscale knob here any more.
		Size = FIntPoint(1920, 1080);

		if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
		{
			Size = GEngine->GameViewport->Viewport->GetSizeXY();
		}
	}

	// H.264 chroma is subsampled 2x2, so both axes have to be even. Rounding down rather than up keeps
	// us inside the source rectangle, which matters when the resize pass is doing the scaling.
	Size.X = FMath::Max(64, Size.X & ~1);
	Size.Y = FMath::Max(64, Size.Y & ~1);

	return Size;
}

bool UInputRecordingScreenRecorder::StartCapture(UObject* WorldContext, const FString& RecordingName)
{
	LastError.Reset();

	if (IsCapturing())
	{
		UE_LOG(LogInputRecordingVideo, Warning, TEXT("StartCapture: already capturing '%s'."), *OutputPath);
		return false;
	}

	if (RecordingName.IsEmpty())
	{
		Fail(TEXT("StartCapture was given an empty recording name."));
		return false;
	}

	if (!IInputRecordingVideoEncoder::IsSupportedOnThisPlatform())
	{
		Fail(TEXT("No video encoder backend on this platform - continuing without video capture."));
		return false;
	}

	State = EInputRecordingVideoState::Starting;
	CurrentRecordingName = RecordingName;
	OutputPath = UInputRecordingVideoLibrary::ResolveVideoPath(RecordingName);
	CaptureResolution = ResolveCaptureResolution(WorldContext);

	// A stale file from a previous take under the same name would otherwise sit there if capture fails
	// halfway, and the match-input flow would happily open it and desynchronise against the new .ghost.
	if (IFileManager::Get().FileExists(*OutputPath))
	{
		IFileManager::Get().Delete(*OutputPath, /*RequireExists=*/false, /*EvenReadOnly=*/true);
	}

	MediaOutput = NewObject<UInputRecordingMediaOutput>(this);
	MediaOutput->OutputFilePath = OutputPath;
	MediaOutput->Resolution = CaptureResolution;
	MediaOutput->EncoderOptions = Options;

	// Consumed here, not when the frame is written, so a dump request can never survive into a second
	// take if this capture fails to start.
	MediaOutput->bDumpFirstFrame = bDumpNextFrame;
	bDumpNextFrame = false;

	FString ValidationError;
	if (!MediaOutput->Validate(ValidationError))
	{
		Fail(ValidationError);
		MediaOutput = nullptr;
		return false;
	}

	MediaCapture = Cast<UInputRecordingMediaCapture>(MediaOutput->CreateMediaCapture());
	if (!MediaCapture)
	{
		Fail(TEXT("UMediaOutput::CreateMediaCapture returned null."));
		MediaOutput = nullptr;
		return false;
	}

	FMediaCaptureOptions CaptureOptions;

	// Skip rather than Flush: falling behind should cost a frame of video, never a hitch in the game
	// the player is being recorded performing.
	CaptureOptions.OverrunAction = EMediaCaptureOverrunAction::Skip;

	// The viewport is whatever size the window happens to be; the encoder needs the exact size it was
	// configured with. ResizeInRenderPass scales on the GPU during capture and leaves the viewport -
	// and therefore the player's actual view - untouched.
	CaptureOptions.ResizeMethod = EMediaCaptureResizeMethod::ResizeInRenderPass;

	CaptureOptions.CapturePhase = bCaptureIncludingUI
		? EMediaCapturePhase::BackBufferReady
		: EMediaCapturePhase::EndFrame;

	// Viewport resolution is fixed for the duration of a take; if the window is resized mid-recording
	// the resize pass keeps the output size constant, which is what the encoder requires.
	CaptureOptions.bAutoRestartOnSourceSizeChange = false;

	if (!MediaCapture->CaptureActiveSceneViewport(CaptureOptions))
	{
		// This is the one failure mode users hit routinely, so name the fix rather than the symptom.
		Fail(TEXT("Could not resolve an active scene viewport. UMediaCapture can only find one when ")
			 TEXT("the game owns its own window - set Editor Preferences > Level Editor > Play > ")
			 TEXT("Play In to 'New Editor Window', or launch Standalone. Continuing without video ")
			 TEXT("capture; the .ghost recording is unaffected."));

		MediaCapture = nullptr;
		MediaOutput = nullptr;
		return false;
	}

	State = EInputRecordingVideoState::Recording;

	UE_LOG(LogInputRecordingVideo, Log, TEXT("Screen capture started: %dx%d -> '%s'."),
		CaptureResolution.X, CaptureResolution.Y, *OutputPath);

	return true;
}

void UInputRecordingScreenRecorder::StopCapture()
{
	if (!MediaCapture)
	{
		if (State == EInputRecordingVideoState::Recording || State == EInputRecordingVideoState::Starting)
		{
			State = EInputRecordingVideoState::Idle;
		}
		return;
	}

	// true = let the frames already in the readback pipeline finish. Those are the last second or so of
	// the take; dropping them would leave the video short against the .ghost it is paired with.
	MediaCapture->StopCapture(/*bAllowPendingFrameToBeProcess=*/true);

	const FString EncoderError = MediaCapture->GetEncoderError();
	if (!EncoderError.IsEmpty())
	{
		LastError = EncoderError;
		State = EInputRecordingVideoState::Failed;
	}
	else
	{
		State = EInputRecordingVideoState::Idle;
	}

	MediaCapture = nullptr;
	MediaOutput = nullptr;

	if (State != EInputRecordingVideoState::Failed)
	{
		const int64 FileSize = IFileManager::Get().FileSize(*OutputPath);
		UE_LOG(LogInputRecordingVideo, Log, TEXT("Screen capture finished: '%s' (%.1f MB)."),
			*OutputPath, FileSize > 0 ? FileSize / (1024.0 * 1024.0) : 0.0);
	}
}

bool UInputRecordingScreenRecorder::RenameCapturedVideo(const FString& NewRecordingName)
{
	if (NewRecordingName.IsEmpty() || NewRecordingName == CurrentRecordingName)
	{
		return IFileManager::Get().FileExists(*OutputPath);
	}

	if (OutputPath.IsEmpty() || !IFileManager::Get().FileExists(*OutputPath))
	{
		return false;
	}

	const FString NewPath = UInputRecordingVideoLibrary::ResolveVideoPath(NewRecordingName);
	if (NewPath.IsEmpty() || NewPath == OutputPath)
	{
		return true;
	}

	if (!IFileManager::Get().Move(*NewPath, *OutputPath, /*bReplace=*/true))
	{
		UE_LOG(LogInputRecordingVideo, Warning,
			TEXT("Could not move '%s' to '%s'. The .mp4 will not pair with the saved .ghost."),
			*OutputPath, *NewPath);
		return false;
	}

	UE_LOG(LogInputRecordingVideo, Log, TEXT("Video renamed to match the saved recording: '%s'."), *NewPath);

	OutputPath = NewPath;
	CurrentRecordingName = NewRecordingName;
	return true;
}

void UInputRecordingScreenRecorder::Fail(const FString& Reason)
{
	LastError = Reason;
	State = EInputRecordingVideoState::Failed;
	UE_LOG(LogInputRecordingVideo, Warning, TEXT("%s"), *Reason);
}
