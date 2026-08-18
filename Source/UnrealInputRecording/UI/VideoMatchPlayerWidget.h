// Copyright (c) Your Studio. All Rights Reserved.
//
// VideoMatchPlayerWidget.h
//
// The video surface, the timeline and the "press this" prompt, as one embeddable widget.
//
// Meant to be dropped inside WBP_InputRecordingHUD (or any other panel) rather than shown on its own -
// it drives nothing, it only reflects UInputRecordingSubsystem. The subsystem starts and stops
// sessions; this widget draws whatever is happening.
//
// -------------------------------------------------------------------------------------------------
// WIDGET BLUEPRINT SETUP
//
// Create a Widget Blueprint parented to this class, then add widgets with these exact names. Every
// binding is optional, so start with two or three and add the rest as you need them.
//
//   VideoImage          Image          The video surface. See "Video surface" below.
//   TimelineBar         Progress Bar   Fills from 0 to 1 as the match clock advances.
//   TimelineCanvas      Canvas Panel   Cue icons are spawned into this. MUST be the same width as
//                                      TimelineBar - put both in an Overlay so they share bounds.
//   ExpectedInputIcon   Image          Icon of the cue currently being waited on.
//   ExpectedInputLabel  Text Block     "Jump", or the cue's description if no icon mapping matches.
//   TimeLabel           Text Block     "0:04 / 0:31".
//   WaitingIndicator    (any widget)   Made visible only while the system is blocked on the player.
//
// The recommended hierarchy for the timeline, because it is the one thing that is easy to get wrong:
//
//   Overlay  (Size To Content = false, fill horizontally)
//   |- TimelineBar       Horizontal/Vertical Alignment = Fill
//   \- TimelineCanvas    Horizontal/Vertical Alignment = Fill
//
// Markers are positioned with normalised *anchors*, not pixels, so they stay correct at any resolution
// and through any resize - but only if the canvas spans exactly the same rectangle as the bar.
//
// -------------------------------------------------------------------------------------------------
// VIDEO SURFACE
//
// Two ways to get the picture into VideoImage, and the widget supports both:
//
//   Direct   (bUseMaterialForVideo = false, the default)
//            The UMediaTexture is set as the image brush's resource object. One click, no assets.
//            Good enough for a debug HUD or a small inset player.
//
//   Material (bUseMaterialForVideo = true, assign VideoMaterial)
//            A Material with Material Domain = User Interface and Blend Mode = Opaque, sampling a
//            Texture Sample Parameter 2D named by VideoMaterialTextureParameter. This is what you want
//            for anything shipping: it is where letterboxing, colour grading and rounded corners live.
//            The widget builds a dynamic instance and pushes the media texture into that parameter.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "InputReplay/InputMatchCue.h"
#include "InputReplay/InputReplayTypes.h"
#include "UI/MatchCueMarkerWidget.h"

#include "VideoMatchPlayerWidget.generated.h"

class UCanvasPanel;
class UImage;
class UInputActionIconMappingDataAsset;
class UInputRecordingDataAsset;
class UInputRecordingSubsystem;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProgressBar;
class UTextBlock;

UCLASS(Abstract, meta = (DisplayName = "Video Match Player Widget"))
class UNREALINPUTRECORDING_API UVideoMatchPlayerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// -----------------------------------------------------------------------------------------
	// Setup
	// -----------------------------------------------------------------------------------------

	/** Resolves an icon for every cue. Without it the timeline draws the mapping's default brush. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video Match Player|Setup")
	TObjectPtr<UInputActionIconMappingDataAsset> IconMapping;

	/**
	 * Optional. When set, the timeline is built from this asset's cached cues at construction, so the
	 * track is populated in the designer and before any session starts. A live session overwrites it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video Match Player|Setup")
	TObjectPtr<UInputRecordingDataAsset> PreviewRecordingAsset;

	/**
	 * Widget spawned per cue. Leave null and the widget creates a plain UImage instead - the timeline
	 * works out of the box; set this when you want per-marker animation or a custom frame.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video Match Player|Setup")
	TSubclassOf<UMatchCueMarkerWidget> CueMarkerClass;

	/** Marker size in slot-local pixels. Overridden per-brush if the brush specifies an image size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video Match Player|Setup")
	FVector2D CueMarkerSize = FVector2D(48.0, 48.0);

	/** Nudge the whole marker row, e.g. (0, -32) to float the icons above the bar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video Match Player|Setup")
	FVector2D CueMarkerOffset = FVector2D::ZeroVector;

	/** See "VIDEO SURFACE" at the top of this file. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video Match Player|Video")
	bool bUseMaterialForVideo = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video Match Player|Video", meta = (EditCondition = "bUseMaterialForVideo"))
	TObjectPtr<UMaterialInterface> VideoMaterial;

	/** Name of the Texture Sample Parameter 2D in VideoMaterial that receives the media texture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video Match Player|Video", meta = (EditCondition = "bUseMaterialForVideo"))
	FName VideoMaterialTextureParameter = TEXT("MediaTexture");

	/**
	 * Drive the video's playhead from the match clock every frame.
	 *
	 * The subsystem already pauses and resumes on cue events, so leaving this off still gives correct
	 * pause behaviour - this is the drift correction on top. Turn it off only if you are driving
	 * UInputRecordingVideoPlayer::SyncToMatchClock from somewhere else.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video Match Player|Video")
	bool bDriveVideoPlayhead = true;

	// -----------------------------------------------------------------------------------------
	// Timeline
	// -----------------------------------------------------------------------------------------

	/** Rebuilds the marker track from whatever the subsystem currently has loaded. */
	UFUNCTION(BlueprintCallable, Category = "Video Match Player")
	void BuildTimelineFromSubsystem();

	/** Rebuilds the marker track from an explicit cue list. Duration sets the horizontal scale. */
	UFUNCTION(BlueprintCallable, Category = "Video Match Player")
	void BuildTimeline(const TArray<FMatchInputCue>& InCues, float InTotalDurationSeconds);

	UFUNCTION(BlueprintCallable, Category = "Video Match Player")
	void ClearTimeline();

	/** Fired for each marker as it is created, so Blueprint can style it further. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Video Match Player")
	void OnCueMarkerCreated(UWidget* Marker, int32 CueIndex, const FMatchInputCue& Cue);

	// -----------------------------------------------------------------------------------------
	// Designer hooks - mirror the subsystem's events with the icon already resolved
	// -----------------------------------------------------------------------------------------

	UFUNCTION(BlueprintImplementableEvent, Category = "Video Match Player")
	void OnCuePresented(int32 CueIndex, int32 TotalCues, const FString& ExpectedInput);

	UFUNCTION(BlueprintImplementableEvent, Category = "Video Match Player")
	void OnCueMatched(int32 CueIndex, int32 TotalCues);

	UFUNCTION(BlueprintImplementableEvent, Category = "Video Match Player")
	void OnCueMismatched(const FString& ExpectedInput, const FString& ActualInput);

	UFUNCTION(BlueprintImplementableEvent, Category = "Video Match Player")
	void OnSessionFinished(bool bCompletedAllCues);

	UFUNCTION(BlueprintImplementableEvent, Category = "Video Match Player")
	void OnVideoReady(bool bSuccess, const FString& VideoPath);

	// -----------------------------------------------------------------------------------------
	// Queries
	// -----------------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Video Match Player")
	UInputRecordingSubsystem* GetRecordingSubsystem() const;

	UFUNCTION(BlueprintPure, Category = "Video Match Player")
	float GetTotalDurationSeconds() const { return TotalDurationSeconds; }

	UFUNCTION(BlueprintPure, Category = "Video Match Player")
	int32 GetCueCount() const { return Cues.Num(); }

protected:
	//~ Begin UUserWidget interface
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	//~ End UUserWidget interface

	/** Points VideoImage at the subsystem's media texture, directly or through a material instance. */
	void BindVideoSurface();

	/** Refreshes the progress bar, the time label and the expected-input prompt. */
	void RefreshDisplay();

	/** Pending / Active / Completed across the whole marker track, from the current cue index. */
	void RefreshMarkerStates(int32 ActiveCueIndex);

	/** Sets ExpectedInputIcon and ExpectedInputLabel from a cue, or clears them for INDEX_NONE. */
	void ShowExpectedInput(int32 CueIndex);

	//~ Bound widgets -----------------------------------------------------------------------------

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Video Match Player|Widgets")
	TObjectPtr<UImage> VideoImage;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Video Match Player|Widgets")
	TObjectPtr<UProgressBar> TimelineBar;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Video Match Player|Widgets")
	TObjectPtr<UCanvasPanel> TimelineCanvas;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Video Match Player|Widgets")
	TObjectPtr<UImage> ExpectedInputIcon;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Video Match Player|Widgets")
	TObjectPtr<UTextBlock> ExpectedInputLabel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Video Match Player|Widgets")
	TObjectPtr<UTextBlock> TimeLabel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Video Match Player|Widgets")
	TObjectPtr<UWidget> WaitingIndicator;

	//~ Runtime state -----------------------------------------------------------------------------

	/** The cue list the track was built from. */
	UPROPERTY(BlueprintReadOnly, Category = "Video Match Player")
	TArray<FMatchInputCue> Cues;

	/** One entry per cue, in the same order. Either a UMatchCueMarkerWidget or a plain UImage. */
	UPROPERTY(BlueprintReadOnly, Category = "Video Match Player")
	TArray<TObjectPtr<UWidget>> CueMarkers;

	UPROPERTY(BlueprintReadOnly, Category = "Video Match Player")
	float TotalDurationSeconds = 0.0f;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> VideoMaterialInstance;

private:
	//~ Subsystem event relays ---------------------------------------------------------------------

	UFUNCTION() void HandleModeChanged(EInputReplayMode NewMode);
	UFUNCTION() void HandleCuePresented(int32 CueIndex, int32 TotalCues, const FString& ExpectedInput);
	UFUNCTION() void HandleCueMatched(int32 CueIndex, int32 TotalCues);
	UFUNCTION() void HandleMismatch(const FString& ExpectedInput, const FString& ActualInput);
	UFUNCTION() void HandleMatchFinished(bool bCompletedAllCues);
	UFUNCTION() void HandleVideoOpened(bool bSuccess, const FString& VideoPath);

	/** Creates one marker and anchors it at Cue.TimeSeconds / TotalDurationSeconds along the canvas. */
	UWidget* SpawnCueMarker(int32 CueIndex, const FMatchInputCue& Cue);

	/** Last index we pushed into the marker track, so RefreshMarkerStates only runs on a change. */
	int32 LastAppliedCueIndex = INDEX_NONE;
};
