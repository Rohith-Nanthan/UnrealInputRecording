// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "InputRecordingVideoPlayer.generated.h"

class UMediaPlayer;
class UMediaTexture;

/**
 * Plays a take's .mp4 back and keeps it aligned with the MatchInput virtual clock.
 *
 * The alignment is the whole point: a cue that pauses the clock has to pause the video too, or
 * the reviewer is being quizzed on something that already scrolled past.
 */
UCLASS(BlueprintType)
class UNREALINPUTRECORDING_API UInputRecordingVideoPlayer : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Video")
	bool OpenVideo(const FString& AbsoluteVideoPath);

	UFUNCTION(BlueprintCallable, Category = "Input Recording|Video")
	void CloseVideo();

	/**
	 * The texture a UImage brush should point at.
	 *
	 * Assign it with SetBrushResourceObject, never SetBrushFromTexture: UMediaTexture is a
	 * UTexture but not a UTexture2D, and the typed setter rejects it.
	 */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	UMediaTexture* GetMediaTexture() const { return MediaTexture; }

	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	bool IsVideoOpen() const { return bVideoOpen; }

	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	FString GetOpenVideoPath() const { return OpenVideoPath; }

	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	float GetPlaybackSeconds() const;

	UFUNCTION(BlueprintPure, Category = "Input Recording|Video")
	float GetDurationSeconds() const;

	/**
	 * Call once a tick with the virtual clock. Pauses when the clock is frozen and seeks only
	 * when the drift is large enough to be visible - seeking on every small discrepancy makes
	 * playback stutter far worse than the drift ever would.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Video")
	void SyncToClock(float ClockSeconds, bool bClockIsRunning);

	/** Drift beyond this many seconds triggers a seek. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording|Video", meta = (ClampMin = "0.05"))
	float ResyncThresholdSeconds = 0.35f;

private:
	void EnsureObjects();

	UPROPERTY(Transient)
	TObjectPtr<UMediaPlayer> MediaPlayer;

	UPROPERTY(Transient)
	TObjectPtr<UMediaTexture> MediaTexture;

	FString OpenVideoPath;
	bool bVideoOpen = false;
};
