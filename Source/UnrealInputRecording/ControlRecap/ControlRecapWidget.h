// Copyright (c) Your Studio. All Rights Reserved.
//
// ControlRecapWidget.h
//
// The full-screen review surface in ControlRecapLevel.
//
// BLUEPRINT-OWNED LAYOUT
//   This class builds no widget tree. Every visual element is a BindWidget hook filled in by a
//   Blueprint child (WBP_ControlRecap), so the layout, styling and arrangement are edited in the UMG
//   designer while the timeline maths, cue tracking and video binding stay here.
//
//   The bindings are BindWidgetOptional rather than BindWidget on purpose. Strict BindWidget fails
//   Blueprint compilation the moment a single name does not match, which blocks you from rearranging
//   the tree while you are still designing it, and it names one missing widget at a time. Optional
//   bindings never block, and ValidateBindings() logs every missing hook at once on construct. Once
//   your layout is settled, swapping any line to BindWidget makes it a hard requirement.
//
//   Required names are documented per property below, and every one is null-checked before use.
//
// LAYOUT THE BLUEPRINT IS EXPECTED TO BUILD (top to bottom)
//     header        session label + Cancel
//     video         VideoSizeBox, height driven from VideoScreenFraction
//     cue count     "cue 3 / 12", between the video and the bar
//     track         progress bar with one marker column per cue
//     expected      large "what to press now", with its icon
//     wrong input   red, smaller, only while a mismatch is being shown
//     legend        small
//
//   Each sync point renders twice on one shared X - the action icon above the bar and a state dot on
//   it - and both halves are the same UMatchCueMarkerWidget on the same canvas anchor. There is no
//   second rail doing the same arithmetic and rounding it differently, so they cannot drift.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "InputReplay/InputMatchCue.h"
#include "Storage/RecordingSessionTypes.h"

#include "ControlRecapWidget.generated.h"

class UButton;
class UCanvasPanel;
class UImage;
class UInputActionIconMappingDataAsset;
class UInputRecordingSubsystem;
class UMatchCueMarkerWidget;
class UPanelWidget;
class UProgressBar;
class USizeBox;
class UTextBlock;
class UWidget;

/** Fired when the recap ends. bCompletedAllCues is false on Cancel. The controller travels on this. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnControlRecapClosed, bool, bCompletedAllCues);

UCLASS(Blueprintable, meta = (DisplayName = "Control Recap Widget"))
class UNREALINPUTRECORDING_API UControlRecapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// =========================================================================================
	// UMG bindings - name these exactly in the Blueprint's widget tree
	// =========================================================================================

	/** Image the video renders into. The media texture is assigned to its brush at runtime. */
	UPROPERTY(BlueprintReadOnly, Category = "Control Recap|Widgets", meta = (BindWidgetOptional))
	TObjectPtr<UImage> VideoImage;

	/**
	 * Size box wrapping the video. Its height is overridden every frame from VideoScreenFraction.
	 *
	 * Driven in C++ rather than left to a Fill slot because "the video is N% of the screen" has to
	 * stay true as the rows beneath it change height, and a Fill slot silently gives the video
	 * whatever is left over instead.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Control Recap|Widgets", meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> VideoSizeBox;

	/** Shown instead of the video when there is nothing to review. */
	UPROPERTY(BlueprintReadOnly, Category = "Control Recap|Widgets", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> EmptyStateText;

	/** "cue 3 / 12". Sits below the video and above the progress bar. */
	UPROPERTY(BlueprintReadOnly, Category = "Control Recap|Widgets", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CueCounterText;

	/** Session folder name, e.g. "Recording_4". */
	UPROPERTY(BlueprintReadOnly, Category = "Control Recap|Widgets", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SessionLabel;

	UPROPERTY(BlueprintReadOnly, Category = "Control Recap|Widgets", meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> ProgressBar;

	/**
	 * Canvas the cue markers are spawned onto. Must be a Canvas Panel: markers are placed by
	 * fractional anchor, which no other panel supports.
	 *
	 * Put it in an Overlay on top of the progress bar and let it fill, so marker X positions and the
	 * bar fill share one coordinate space.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Control Recap|Widgets", meta = (BindWidgetOptional))
	TObjectPtr<UCanvasPanel> MarkerCanvas;

	/** Large "what to press now" label, below the progress bar. */
	UPROPERTY(BlueprintReadOnly, Category = "Control Recap|Widgets", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ExpectedInputText;

	/** Sprite for the expected input, from the icon mapping. */
	UPROPERTY(BlueprintReadOnly, Category = "Control Recap|Widgets", meta = (BindWidgetOptional))
	TObjectPtr<UImage> ExpectedInputIcon;

	/** Red, smaller: what the player actually pressed when it was wrong. Hidden the rest of the time. */
	UPROPERTY(BlueprintReadOnly, Category = "Control Recap|Widgets", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> WrongInputText;

	UPROPERTY(BlueprintReadOnly, Category = "Control Recap|Widgets", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ElapsedText;

	UPROPERTY(BlueprintReadOnly, Category = "Control Recap|Widgets", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TotalText;

	UPROPERTY(BlueprintReadOnly, Category = "Control Recap|Widgets", meta = (BindWidgetOptional))
	TObjectPtr<UButton> CancelButton;

	/** Any panel holding the legend. Only used to hide the legend when there are no cues. */
	UPROPERTY(BlueprintReadOnly, Category = "Control Recap|Widgets", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> LegendPanel;

	/** Whole expected/wrong block, hidden while nothing is being awaited. */
	UPROPERTY(BlueprintReadOnly, Category = "Control Recap|Widgets", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> PromptPanel;

	// =========================================================================================
	// API
	// =========================================================================================

	/** Loads the session and starts MatchInput against it. Builds the timeline from its cue list. */
	UFUNCTION(BlueprintCallable, Category = "Control Recap")
	void BeginReview(const FRecordingSessionInfo& Session);

	/** Replaces the video with a message. For "the store is empty" rather than a black screen. */
	UFUNCTION(BlueprintCallable, Category = "Control Recap")
	void ShowEmptyState(const FString& Message);

	/** Aborts playback and fires OnClosed(false). What Cancel and the Back action both call. */
	UFUNCTION(BlueprintCallable, Category = "Control Recap")
	void CancelReview();

	UPROPERTY(BlueprintAssignable, Category = "Control Recap")
	FOnControlRecapClosed OnClosed;

	/** Fires once the widget is constructed and bindings are resolved, for extra Blueprint styling. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Control Recap")
	void OnRecapConstructed();

	/** Fires on every mismatch, if you want to add a shake, a sound, or your own styling. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Control Recap")
	void OnWrongInput(const FString& ExpectedInput, const FString& ActualInput);

	// =========================================================================================
	// Setup
	// =========================================================================================

	/**
	 * Action -> sprite mapping. Leave empty to use the project setting, which is the normal case.
	 *
	 * Only set this to override the icon set for one screen. ResolveIconMapping() prefers this and
	 * falls back to Project Settings > Game > Input Recording > UI > Input Action Icon Mapping.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control Recap|Setup")
	TObjectPtr<UInputActionIconMappingDataAsset> IconMapping;

	/** Marker column spawned onto MarkerCanvas, once per cue. Defaults to UMatchCueMarkerWidget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control Recap|Setup")
	TSubclassOf<UMatchCueMarkerWidget> CueMarkerClass;

	// =========================================================================================
	// Layout / style
	// =========================================================================================

	/**
	 * Share of the widget's height given to the video.
	 *
	 * Lower than it once was to make room for the cue count, the expected-input line and the
	 * mismatch line that now sit under the bar.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control Recap|Layout", meta = (ClampMin = "0.3", ClampMax = "0.9"))
	float VideoScreenFraction = 0.62f;

	/** Height of a marker column. Icon rides at the top, state dot lands on the bar at the bottom. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control Recap|Layout", meta = (ClampMin = "24"))
	float MarkerTrackHeight = 56.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control Recap|Layout", meta = (ClampMin = "16"))
	float MarkerColumnWidth = 44.f;

	/**
	 * Apply the font sizes below on construct.
	 *
	 * On by default so the size relationship the design depends on - expected input clearly larger
	 * than the mismatch line - holds however the Blueprint was authored. Turn it off to hand font
	 * control back to UMG entirely.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control Recap|Style")
	bool bOverrideFontSizes = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control Recap|Style", meta = (EditCondition = "bOverrideFontSizes", ClampMin = "8"))
	int32 ExpectedInputFontSize = 42;

	/** Deliberately well under ExpectedInputFontSize - the mismatch is a footnote, not the headline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control Recap|Style", meta = (EditCondition = "bOverrideFontSizes", ClampMin = "6"))
	int32 WrongInputFontSize = 18;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control Recap|Style", meta = (EditCondition = "bOverrideFontSizes", ClampMin = "6"))
	int32 CueCounterFontSize = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control Recap|Style")
	FLinearColor ExpectedInputColor = FLinearColor(0.95f, 0.96f, 0.98f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control Recap|Style")
	FLinearColor WrongInputColor = FLinearColor(0.94f, 0.27f, 0.27f, 1.0f);

	/** How long a mismatch line stays up. It also clears as soon as the cue is answered correctly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control Recap|Style", meta = (ClampMin = "0.5"))
	float WrongInputDisplaySeconds = 3.f;

protected:
	//~ Begin UUserWidget interface
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	//~ End UUserWidget interface

	UInputRecordingSubsystem* GetRecordingSubsystem() const;

	/** Widget's own IconMapping if set, otherwise the project setting. Cached after first resolve. */
	UInputActionIconMappingDataAsset* ResolveIconMapping();

	/** Spawns one marker per cue. Re-entrant: clears the canvas first. */
	void BuildTimeline();

	/** Repositions the fill and refreshes the counter, clock and prompt from the live match clock. */
	void RefreshFromMatchClock();

	void RefreshMarkerStates(int32 ActiveCueIndex);
	void ShowPrompt(int32 CueIndex);
	void HidePrompt();
	void ClearWrongInput();

	/** Points VideoImage's brush at the player's media texture. Safe to call repeatedly. */
	void RefreshVideoBinding();

	/** Logs every unbound hook in one message. Called once on construct. */
	void ValidateBindings() const;

	/** The single teardown path, whether the session finished or was cancelled. */
	void CloseRecap(bool bCompletedAllCues);

private:
	UFUNCTION() void HandleCancelClicked();
	UFUNCTION() void HandleMatchCuePresented(int32 CueIndex, int32 TotalCues, const FString& ExpectedInput);
	UFUNCTION() void HandleMatchInputMatched(int32 CueIndex, int32 TotalCues);
	UFUNCTION() void HandleMatchInputMismatch(const FString& ExpectedInput, const FString& ActualInput);
	UFUNCTION() void HandleMatchInputFinished(bool bCompletedAllCues);
	UFUNCTION() void HandleVideoOpened(bool bSuccess, const FString& VideoPath);

	static FString FormatTime(float Seconds);

	UPROPERTY(Transient) TArray<TObjectPtr<UMatchCueMarkerWidget>> Markers;
	UPROPERTY(Transient) TArray<FMatchInputCue> Cues;
	UPROPERTY(Transient) TObjectPtr<UInputActionIconMappingDataAsset> ResolvedIconMapping;

	FRecordingSessionInfo ReviewedSession;

	float TotalDurationSeconds = 0.f;
	float WrongInputTimer = 0.f;
	int32 LastAppliedCueIndex = INDEX_NONE;
	bool bClosing = false;
};
