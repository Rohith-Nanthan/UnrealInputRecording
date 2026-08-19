// Copyright (c) Your Studio. All Rights Reserved.
//
// ControlRecapWidget.h
//
// The full-screen review surface in ControlRecapLevel.
//
// LAYOUT
//   Padded full screen, three rows:
//     header    - "cue 3 / 12" on the left, Cancel on the right
//     video     - 80% of the available height, with the waiting-for prompt as a pill over it
//     timeline  - progress bar, one marker column per cue, elapsed/total, and a legend
//
//   Each sync point renders twice on one shared X - the action icon above the bar and a state dot on
//   it - and both halves are the same UMatchCueMarkerWidget on the same canvas anchor. That is the
//   reason they cannot drift: there is no second rail doing the same arithmetic and rounding it
//   differently.
//
// WHY THIS IS NOT UMatchVideoPlayerWidget
//   That widget is an overlay pushed on top of a live gameplay map, and it still works that way for
//   anyone using it. This one owns a whole map, is driven by its own player controller, and answers
//   Cancel by travelling rather than by closing. Sharing a base class would have meant one of the two
//   constantly checking which situation it was in.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "InputReplay/InputMatchCue.h"
#include "Storage/RecordingSessionTypes.h"

#include "ControlRecapWidget.generated.h"

class UBorder;
class UButton;
class UCanvasPanel;
class UHorizontalBox;
class UImage;
class UInputActionIconMappingDataAsset;
class UInputRecordingSubsystem;
class UMatchCueMarkerWidget;
class UProgressBar;
class USizeBox;
class UTextBlock;
class UVerticalBox;
class UVideoSurfaceWidget;

/** Fired when the recap ends. bCompletedAllCues is false on Cancel. The controller travels on this. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnControlRecapClosed, bool, bCompletedAllCues);

UCLASS(Blueprintable, meta = (DisplayName = "Control Recap Widget"))
class UNREALINPUTRECORDING_API UControlRecapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Loads the session and starts MatchInput against it. Builds the timeline from its cue list. */
	UFUNCTION(BlueprintCallable, Category = "Control Recap")
	void BeginReview(const FRecordingSessionInfo& Session);

	/** Replaces the video area with a message. For "the store is empty" rather than a black screen. */
	UFUNCTION(BlueprintCallable, Category = "Control Recap")
	void ShowEmptyState(const FString& Message);

	/** Aborts playback and fires OnClosed(false). What Cancel and the Back action both call. */
	UFUNCTION(BlueprintCallable, Category = "Control Recap")
	void CancelReview();

	UPROPERTY(BlueprintAssignable, Category = "Control Recap")
	FOnControlRecapClosed OnClosed;

	/** Fires after the tree is built, for extra Blueprint styling. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Control Recap")
	void OnRecapConstructed();

	//~ Setup -------------------------------------------------------------------------------------

	/** Resolves the icon for each cue and for the waiting-for prompt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control Recap|Setup")
	TObjectPtr<UInputActionIconMappingDataAsset> IconMapping;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control Recap|Setup")
	TSubclassOf<UMatchCueMarkerWidget> CueMarkerClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control Recap|Setup")
	TSubclassOf<UVideoSurfaceWidget> VideoSurfaceClass;

	//~ Style hooks -------------------------------------------------------------------------------

	/** Padding between the screen edge and the content. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control Recap|Style")
	FMargin ScreenPadding = FMargin(48.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control Recap|Style")
	FLinearColor ScreenColor = FLinearColor(0.03f, 0.035f, 0.047f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control Recap|Style")
	FLinearColor VideoFrameColor = FLinearColor(0.07f, 0.075f, 0.10f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control Recap|Style")
	FLinearColor AccentColor = FLinearColor(0.23f, 0.51f, 0.96f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control Recap|Style")
	FLinearColor MutedTextColor = FLinearColor(0.42f, 0.44f, 0.47f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control Recap|Style")
	FLinearColor PrimaryTextColor = FLinearColor(0.91f, 0.92f, 0.93f, 1.0f);

	/**
	 * Share of the available height given to the video.
	 *
	 * Applied as an explicit height override each frame rather than left to fill whatever the header
	 * and timeline do not use, because "80% of the canvas" is a stated requirement and letting it
	 * emerge from content sizing would quietly drift as either row gains a line.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control Recap|Style", meta = (ClampMin = "0.4", ClampMax = "0.95"))
	float VideoScreenFraction = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control Recap|Style", meta = (ClampMin = "48"))
	float TimelineHeight = 96.f;

	/** Per-marker column width on the timeline canvas. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control Recap|Style", meta = (ClampMin = "16"))
	float MarkerColumnWidth = 44.f;

	//~ Exposed panels ----------------------------------------------------------------------------

	UPROPERTY(BlueprintReadOnly, Category = "Control Recap|Widgets") TObjectPtr<UBorder> ScreenBorder;
	UPROPERTY(BlueprintReadOnly, Category = "Control Recap|Widgets") TObjectPtr<UVideoSurfaceWidget> VideoSurface;
	UPROPERTY(BlueprintReadOnly, Category = "Control Recap|Widgets") TObjectPtr<UProgressBar> ProgressBar;
	UPROPERTY(BlueprintReadOnly, Category = "Control Recap|Widgets") TObjectPtr<UCanvasPanel> MarkerCanvas;
	UPROPERTY(BlueprintReadOnly, Category = "Control Recap|Widgets") TObjectPtr<UButton> CancelButton;

protected:
	//~ Begin UUserWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	//~ End UUserWidget interface

	UInputRecordingSubsystem* GetRecordingSubsystem() const;

	/** Static chrome. Runs once, guarded on ScreenBorder. */
	void BuildTree();

	/** Spawns one marker per cue. Re-entrant: clears the canvas first. */
	void BuildTimeline();

	/** Repositions the fill, the prompt, the counter and the clock from the live match clock. */
	void RefreshFromMatchClock();

	void RefreshMarkerStates(int32 ActiveCueIndex);
	void ShowPrompt(int32 CueIndex);
	void HidePrompt();

	/** The single teardown path, whether the session finished or was cancelled. */
	void CloseRecap(bool bCompletedAllCues);

private:
	UFUNCTION() void HandleCancelClicked();
	UFUNCTION() void HandleMatchCuePresented(int32 CueIndex, int32 TotalCues, const FString& ExpectedInput);
	UFUNCTION() void HandleMatchInputMatched(int32 CueIndex, int32 TotalCues);
	UFUNCTION() void HandleMatchInputMismatch(const FString& ExpectedInput, const FString& ActualInput);
	UFUNCTION() void HandleMatchInputFinished(bool bCompletedAllCues);

	static FString FormatTime(float Seconds);

	//~ Built widgets ----------------------------------------------------------------------------
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CueCounterText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SessionLabel;
	UPROPERTY(Transient) TObjectPtr<USizeBox> VideoBox;
	UPROPERTY(Transient) TObjectPtr<UBorder> VideoFrame;
	UPROPERTY(Transient) TObjectPtr<UBorder> PromptPill;
	UPROPERTY(Transient) TObjectPtr<UImage> PromptIcon;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PromptActionText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> EmptyStateText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ElapsedText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TotalText;

	UPROPERTY(Transient) TArray<TObjectPtr<UMatchCueMarkerWidget>> Markers;
	UPROPERTY(Transient) TArray<FMatchInputCue> Cues;

	FRecordingSessionInfo ReviewedSession;

	float TotalDurationSeconds = 0.f;
	int32 LastAppliedCueIndex = INDEX_NONE;
	bool bClosing = false;
};
