// Copyright (c) Your Studio. All Rights Reserved.
//
// MatchVideoPlayerWidget.h
//
// The full-screen "Test" surface: the recorded video at ~80%, a sync timeline along the bottom, and a
// Cancel button that is always available. Opened by URecordingControllerWidget when Test is pressed.
//
// The whole tree is built in C++ (RebuildWidget). A Blueprint subclass restyles through the Style
// properties and can reach the built panels (exposed BlueprintReadOnly) from OnPlayerConstructed.
//
// INPUT LOCKOUT
//   On open, the pawn is frozen with SetIgnoreMoveInput/SetIgnoreLookInput and the input mode is set to
//   Game-and-UI with the cursor shown. The Enhanced Input contexts stay live on purpose: the matching
//   system reads action values straight from the EI subsystem, so freezing the pawn stops it wandering
//   the level while the player's presses still register as cue matches. Balanced restore on close.
//
// LIFECYCLE
//   Cancel  -> StopMatchInputMode(), which fires OnMatchInputFinished(false).
//   Finish  -> the component fires OnMatchInputFinished(true) on its own.
//   Both land in HandleMatchFinished, so there is exactly one teardown path: unbind, restore input,
//   broadcast OnClosed, remove from parent. The controller listens to OnClosed to show itself again.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "InputReplay/InputMatchCue.h"

#include "MatchVideoPlayerWidget.generated.h"

class UBorder;
class UButton;
class UCanvasPanel;
class UHorizontalBox;
class UImage;
class UInputActionIconMappingDataAsset;
class UInputRecordingSubsystem;
class UMatchCueMarkerWidget;
class UOverlay;
class UProgressBar;
class USizeBox;
class UTextBlock;
class UVerticalBox;
class UVideoSurfaceWidget;

/** Fired when the player closes, either way. bCompletedAllCues is false on Cancel / early stop. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMatchVideoPlayerClosed, bool, bCompletedAllCues);

UCLASS(Blueprintable, meta = (DisplayName = "Match Video Player Widget"))
class UNREALINPUTRECORDING_API UMatchVideoPlayerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	//~ Setup - assign before AddToViewport, or set as class defaults on a Blueprint subclass ----

	/** Resolves the icon for each cue on the timeline and the "waiting for" prompt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match Video Player|Setup")
	TObjectPtr<UInputActionIconMappingDataAsset> IconMapping;

	/** Widget spawned per cue. Defaults to UMatchCueMarkerWidget when left null. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match Video Player|Setup")
	TSubclassOf<UMatchCueMarkerWidget> CueMarkerClass;

	/** The embedded video widget class. Defaults to UVideoSurfaceWidget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match Video Player|Setup")
	TSubclassOf<UVideoSurfaceWidget> VideoSurfaceClass;

	//~ Style hooks -------------------------------------------------------------------------------

	/** Padding between the screen edge and the content - the "nice padding around the borders". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match Video Player|Style")
	FMargin BorderPadding = FMargin(48.f);

	/** Full-screen backing colour behind the content. Dark and near-opaque by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match Video Player|Style")
	FLinearColor ScreenColor = FLinearColor(0.02f, 0.03f, 0.04f, 0.96f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match Video Player|Style")
	FLinearColor ProgressFillColor = FLinearColor(0.24f, 0.55f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match Video Player|Style")
	FLinearColor PromptBackground = FLinearColor(0.04f, 0.05f, 0.06f, 0.82f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match Video Player|Style")
	FLinearColor CancelButtonColor = FLinearColor(0.12f, 0.13f, 0.15f, 1.0f);

	/** Height of the timeline strip (icons above, dots + bar below). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match Video Player|Style", meta = (ClampMin = "40"))
	float TimelineHeight = 72.f;

	/** Per-marker column width on the timeline canvas. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match Video Player|Style", meta = (ClampMin = "16"))
	float MarkerColumnWidth = 44.f;

	//~ Events ------------------------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "Match Video Player")
	FOnMatchVideoPlayerClosed OnClosed;

	/** Fires after the tree and timeline are built, for extra Blueprint styling. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Match Video Player")
	void OnPlayerConstructed();

	//~ Exposed panels (for a Blueprint subclass that wants to reach in) --------------------------

	UPROPERTY(BlueprintReadOnly, Category = "Match Video Player|Widgets") TObjectPtr<UBorder> ScreenBorder;
	UPROPERTY(BlueprintReadOnly, Category = "Match Video Player|Widgets") TObjectPtr<UVideoSurfaceWidget> VideoSurface;
	UPROPERTY(BlueprintReadOnly, Category = "Match Video Player|Widgets") TObjectPtr<UProgressBar> ProgressBar;
	UPROPERTY(BlueprintReadOnly, Category = "Match Video Player|Widgets") TObjectPtr<UCanvasPanel> MarkerCanvas;
	UPROPERTY(BlueprintReadOnly, Category = "Match Video Player|Widgets") TObjectPtr<UButton> CancelButton;

protected:
	//~ Begin UUserWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	//~ End UUserWidget interface

	UInputRecordingSubsystem* GetRecordingSubsystem() const;

	/** Builds the static chrome (border, overlay, timeline, cancel). Runs once. */
	void BuildTree();

	/** Spawns the video surface and the per-cue markers. Needs a world, so runs in NativeConstruct. */
	void BuildDynamicContent();

	/** Repositions marker states, the fill, the prompt and the time label from the live match clock. */
	void RefreshFromMatchClock();

	void ApplyInputLock(bool bLock);

	/** The single teardown path. bCompleted distinguishes finish from cancel. */
	void ClosePlayer(bool bCompletedAllCues);

private:
	UFUNCTION() void HandleCancelClicked();
	UFUNCTION() void HandleMatchCuePresented(int32 CueIndex, int32 TotalCues, const FString& ExpectedInput);
	UFUNCTION() void HandleMatchInputMatched(int32 CueIndex, int32 TotalCues);
	UFUNCTION() void HandleMatchInputFinished(bool bCompletedAllCues);

	void RefreshMarkerStates(int32 ActiveCueIndex);
	void ShowPrompt(int32 CueIndex);
	static FString FormatTime(float Seconds);

	//~ Built widgets not already exposed above ---------------------------------------------------
	UPROPERTY(Transient) TObjectPtr<UBorder> VideoContainer;
	UPROPERTY(Transient) TObjectPtr<UBorder> PromptBorder;
	UPROPERTY(Transient) TObjectPtr<UImage> PromptIcon;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PromptText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CueCountText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TimeText;

	UPROPERTY(Transient) TArray<TObjectPtr<UMatchCueMarkerWidget>> Markers;
	UPROPERTY(Transient) TArray<FMatchInputCue> Cues;

	float TotalDurationSeconds = 0.f;
	int32 LastAppliedCueIndex = INDEX_NONE;
	bool bInputLocked = false;
	bool bClosing = false;
};
