// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MatchInput/MatchInputTypes.h"
#include "Store/RecordingSessionTypes.h"
#include "UI/InputRecordingWidgetBase.h"
#include "ControlRecapWidget.generated.h"

class UButton;
class UCanvasPanel;
class UImage;
class UMatchCueMarkerWidget;
class UPanelWidget;
class UProgressBar;
class USizeBox;
class UTextBlock;
class UVideoSurfaceWidget;
class UWrongInputRowWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnControlRecapClosed, bool, bCompletedAllCues);

/**
 * The full-screen review surface.
 *
 * Layout the Blueprint builds, top to bottom: header (session label + Cancel), video, cue
 * counter, track (progress bar with one marker per cue), expected-input prompt, the wrong-input
 * list directly BELOW that prompt, then a small legend.
 */
UCLASS(Blueprintable, BlueprintType)
class UNREALINPUTRECORDING_API UControlRecapWidget : public UInputRecordingWidgetBase
{
	GENERATED_BODY()

public:
	// --- Header ---------------------------------------------------------------------------
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SessionLabelText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> CancelButton;

	// --- Video ----------------------------------------------------------------------------
	/** Height is driven from C++ so "the video is N% of the screen" stays true - see UpdateVideoHeight. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> VideoSizeBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UVideoSurfaceWidget> VideoSurface;

	// --- Track ----------------------------------------------------------------------------
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CueCounterText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> TrackProgressBar;

	/**
	 * Canvas overlaid on the progress bar. Markers are positioned by fractional anchor on this
	 * canvas so a marker's X and the bar's fill share one coordinate space and cannot drift.
	 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UCanvasPanel> MarkerCanvas;

	// --- Prompt ---------------------------------------------------------------------------
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ExpectedInputText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UImage> ExpectedInputIcon;

	/** Directly below the expected prompt. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> WrongInputContainer;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MismatchCountText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> EmptyStateText;

	// --- Per-widget overrides, resolved as "override if set, else the project setting" -----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording|UI")
	TSubclassOf<UMatchCueMarkerWidget> CueMarkerClassOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording|UI")
	TSubclassOf<UWrongInputRowWidget> WrongInputRowClassOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording|UI", meta = (ClampMin = "1", ClampMax = "20"))
	int32 MaxWrongInputRows = 5;

	UPROPERTY(BlueprintAssignable, Category = "Input Recording|UI")
	FOnControlRecapClosed OnRecapClosed;

	/** Loads the session, opens its video, builds the markers and starts the quiz. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|UI")
	void BeginReview(const FRecordingSessionInfo& Session);

	/** Comes up with an explanatory empty state rather than a black screen. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|UI")
	void ShowEmptyState(const FString& Reason);

	UFUNCTION(BlueprintCallable, Category = "Input Recording|UI")
	void CloseRecap(bool bCompletedAllCues);

	UFUNCTION(BlueprintImplementableEvent, Category = "Input Recording|UI", meta = (DisplayName = "On Review Started"))
	void K2_OnReviewStarted(const FRecordingSessionInfo& Session, int32 CueCount);

	UFUNCTION(BlueprintImplementableEvent, Category = "Input Recording|UI", meta = (DisplayName = "On Cue Presented"))
	void K2_OnCuePresented(int32 CueIndex, int32 TotalCues, const FString& ExpectedDescription);

	UFUNCTION(BlueprintImplementableEvent, Category = "Input Recording|UI", meta = (DisplayName = "On Cue Matched"))
	void K2_OnCueMatched(int32 CueIndex, int32 TotalCues);

	UFUNCTION(BlueprintImplementableEvent, Category = "Input Recording|UI", meta = (DisplayName = "On Cue Mismatched"))
	void K2_OnCueMismatched(const FString& ExpectedDescription, const FString& ReceivedDescription);

	UFUNCTION(BlueprintImplementableEvent, Category = "Input Recording|UI", meta = (DisplayName = "On Review Finished"))
	void K2_OnReviewFinished(bool bCompletedAllCues);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void CollectMissingBindings(TArray<FString>& OutMissing) const override;

private:
	UFUNCTION()
	void HandleCancelClicked();

	UFUNCTION()
	void HandleCuePresented(int32 CueIndex, int32 TotalCues, const FString& ExpectedDescription);

	UFUNCTION()
	void HandleCueMatched(int32 CueIndex, int32 TotalCues);

	UFUNCTION()
	void HandleCueMismatched(const FString& ExpectedDescription, const FString& ReceivedDescription);

	UFUNCTION()
	void HandleMatchFinished(bool bCompletedAllCues);

	void BindSubsystemEvents();
	void UnbindSubsystemEvents();

	void BuildCueMarkers(const TArray<FMatchInputCue>& Cues, float TotalDurationSeconds);
	void ClearWrongInputRows();
	void RefreshExpectedPrompt();

	/**
	 * Drives the video's height from C++ rather than leaving it to a Fill slot: "the video is
	 * N% of the screen" has to stay true as the rows beneath it change height, and a Fill slot
	 * silently gives the video whatever is left over instead.
	 */
	void UpdateVideoHeight();

	TSubclassOf<UMatchCueMarkerWidget> ResolveCueMarkerClass() const;
	TSubclassOf<UWrongInputRowWidget> ResolveWrongInputRowClass() const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMatchCueMarkerWidget>> CueMarkers;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UWrongInputRowWidget>> WrongInputRows;

	UPROPERTY(Transient)
	FRecordingSessionInfo ReviewedSession;

	float TotalDuration = 0.0f;
	bool bBoundToSubsystem = false;
	bool bReviewActive = false;
};
