// Copyright (c) Your Studio. All Rights Reserved.
//
// InputRecordingScreenRecorder.h
//
// The subsystem-facing face of screen capture. Owns the UMediaOutput / UMediaCapture pair for one
// take and nothing else, so the subsystem's recording path stays readable:
//
//     StartRecording()  -> Component->StartRecording()  +  ScreenRecorder->StartCapture()
//     StopRecording()   -> Component->SaveRecording()   +  ScreenRecorder->StopCapture()
//
// Failure here is deliberately non-fatal. If the viewport cannot be resolved, or the platform has no
// encoder, StartCapture logs why and returns false - and the input recording carries on regardless.
// A .ghost with no .mp4 is a usable recording; a lost .ghost is a take the user has to re-perform.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Video/InputRecordingVideoTypes.h"

#include "InputRecordingScreenRecorder.generated.h"

class UInputRecordingMediaCapture;
class UInputRecordingMediaOutput;

UCLASS(BlueprintType)
class UNREALINPUTRECORDING_API UInputRecordingScreenRecorder : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Begins capturing the active game viewport to <ProjectSaved>/InputRecordings/<RecordingName>.mp4.
	 *
	 * @param WorldContext   Any object with a world; used to find the game viewport.
	 * @param RecordingName  Bare name, matching the .ghost this take will produce.
	 * @return false if capture could not start. Check the log; the caller should carry on either way.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Video", meta = (WorldContext = "WorldContext"))
	bool StartCapture(UObject* WorldContext, const FString& RecordingName);

	/** Stops capture and finalises the .mp4. Blocks briefly while the encoder drains. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Video")
	void StopCapture();

	/**
	 * Renames the finished .mp4 so it pairs with a .ghost that was saved under a different name.
	 * Called by the subsystem when StopRecordingAndSave is given a name other than the one capture
	 * started with. No-op when the names already agree.
	 *
	 * @return true if the file now sits at the expected path.
	 */
	bool RenameCapturedVideo(const FString& NewRecordingName);

	/**
	 * Ask the next capture to write its first frame out as a PNG beside the .mp4.
	 *
	 * The orientation harness. Cleared as soon as a capture consumes it, so it never affects more than
	 * one take and cannot be left on by accident.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Video")
	void RequestFrameDump() { bDumpNextFrame = true; }

	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	bool IsFrameDumpPending() const { return bDumpNextFrame; }

	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	bool IsCapturing() const { return State == EInputRecordingVideoState::Recording; }

	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	EInputRecordingVideoState GetState() const { return State; }

	/** Absolute path of the .mp4 for the current or most recent take. Empty if there is none. */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	FString GetOutputPath() const { return OutputPath; }

	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	FString GetRecordingName() const { return CurrentRecordingName; }

	/** Empty unless the last StartCapture or the encoder failed. */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	FString GetLastError() const { return LastError; }

	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	FIntPoint GetCaptureResolution() const { return CaptureResolution; }

	/** Encoder tuning for the next StartCapture. Seeded from the project settings by the subsystem. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording|Video")
	FInputRecordingVideoOptions Options;

	/**
	 * Which point in the frame to grab.
	 *
	 * EndFrame captures the rendered viewport. Switch to BackBufferReady if you want the HUD and any
	 * other Slate drawn on top to appear in the video - useful when the recording is going to be shown
	 * back to a player as a tutorial, less useful when you want a clean picture of the game itself.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording|Video")
	uint8 bCaptureIncludingUI : 1;

	//~ Begin UObject interface
	virtual void BeginDestroy() override;
	//~ End UObject interface

	UInputRecordingScreenRecorder();

private:
	/** Viewport size with ResolutionScale applied, snapped down to even on both axes. */
	FIntPoint ResolveCaptureResolution(UObject* WorldContext) const;

	void Fail(const FString& Reason);

	UPROPERTY(Transient)
	TObjectPtr<UInputRecordingMediaOutput> MediaOutput;

	UPROPERTY(Transient)
	TObjectPtr<UInputRecordingMediaCapture> MediaCapture;

	EInputRecordingVideoState State = EInputRecordingVideoState::Idle;

	FString OutputPath;
	FString CurrentRecordingName;
	FString LastError;
	FIntPoint CaptureResolution = FIntPoint::ZeroValue;

	/** One-shot, consumed by the next StartCapture. See RequestFrameDump. */
	bool bDumpNextFrame = false;
};
