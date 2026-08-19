// Copyright (c) Your Studio. All Rights Reserved.
//
// RecordingControllerWidget.h
//
// The recording control panel: a Start/Stop toggle, a Test button, a "current input" read-out, and a
// scrolling sync-point history. Built entirely in C++ (RebuildWidget); a Blueprint subclass restyles
// through the Style properties and can reach the built panels from OnControllerConstructed.
//
// A POP-UP, NOT A HUD ELEMENT
//   The subsystem owns this widget and raises it from StartRecording, whichever route that came in by
//   - console command, gameplay code, or the button on this panel. StopRecording takes it away and
//   replaces it with the save confirmation. It is pinned to the bottom-right corner and capped at a
//   share of the screen area so it never becomes the thing the player is looking at.
//
// Data sources (all on UInputRecordingSubsystem):
//   Current input   - GetLiveInputSnapshot(), polled each tick.
//   Sync history    - OnInputSyncPointRecorded, one row appended per onset. The last few rows stay
//                     bright (HistoryHighlightCount) while older ones fade, and the list auto-scrolls
//                     to the newest.
//   Test            - RunControlRecapTest(), which saves the take and travels to ControlRecapLevel.
//                     This widget does not survive that travel, and does not need to.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "InputReplay/InputReplayTypes.h"

#include "RecordingControllerWidget.generated.h"

class UBorder;
class UButton;
class UCanvasPanel;
class UImage;
class UInputActionIconMappingDataAsset;
class UInputRecordingSubsystem;
class UScrollBox;
class USizeBox;
class USyncPointRowWidget;
class UTextBlock;
class UVerticalBox;

UCLASS(Blueprintable, meta = (DisplayName = "Recording Controller Widget"))
class UNREALINPUTRECORDING_API URecordingControllerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	//~ Setup -------------------------------------------------------------------------------------

	/** Recording name for Record / Test. Empty falls back to the project's Default Recording Name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Controller|Setup")
	FString RecordingFileName;

	/** Label stored in the recording header. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Controller|Setup")
	FString RecordingDisplayName = TEXT("Tutorial Take");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Controller|Setup")
	bool bUseJsonFormat = false;

	/** Icons for the current-input read-out and the history rows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Controller|Setup")
	TObjectPtr<UInputActionIconMappingDataAsset> IconMapping;

	/** Row widget for the history list. Defaults to USyncPointRowWidget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Controller|Setup")
	TSubclassOf<USyncPointRowWidget> SyncRowClass;

	/** How many of the most recent history rows stay highlighted. The rest fade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Controller|Setup", meta = (ClampMin = "1"))
	int32 HistoryHighlightCount = 5;

	/**
	 * Show the cursor and use Game-and-UI input while the controller is up so the buttons are clickable.
	 * Game-and-UI, never UI-only: matching has to keep receiving gameplay input.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Controller|Setup")
	bool bManagePlayerInputMode = true;

	//~ Screen footprint --------------------------------------------------------------------------

	/**
	 * Largest share of the screen *area* the panel may occupy.
	 *
	 * Area rather than a width and a height, because those two constraints disagree at unusual aspect
	 * ratios: 26% wide by 46% tall is a reasonable 12% on a 16:9 display and a very different thing on
	 * an ultrawide. The panel is laid out from MaxWidthFraction and MaxHeightFraction, then scaled down
	 * uniformly if their product would exceed this.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Controller|Layout", meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float MaxScreenAreaFraction = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Controller|Layout", meta = (ClampMin = "0.1", ClampMax = "0.9"))
	float MaxWidthFraction = 0.26f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Controller|Layout", meta = (ClampMin = "0.1", ClampMax = "0.9"))
	float MaxHeightFraction = 0.46f;

	/** Gap between the panel and the bottom-right corner of the viewport. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Controller|Layout")
	FVector2D CornerMargin = FVector2D(24.0, 24.0);

	//~ Style hooks -------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Controller|Style")
	FLinearColor PanelColor = FLinearColor(0.05f, 0.06f, 0.07f, 0.94f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Controller|Style")
	FLinearColor CardColor = FLinearColor(0.09f, 0.10f, 0.12f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Controller|Style")
	FLinearColor AccentColor = FLinearColor(0.24f, 0.55f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Controller|Style")
	FLinearColor DangerColor = FLinearColor(0.90f, 0.29f, 0.29f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Controller|Style")
	FLinearColor MutedColor = FLinearColor(0.6f, 0.65f, 0.7f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Controller|Style")
	FString PanelTitle = TEXT("Recording controller");

	//~ Button handlers (also callable from a Blueprint that wires its own buttons) ---------------

	UFUNCTION(BlueprintCallable, Category = "Recording Controller")
	void ToggleRecording();

	UFUNCTION(BlueprintCallable, Category = "Recording Controller")
	void StartTest();

	/** Empties the sync-point history. Called automatically when a new recording starts. */
	UFUNCTION(BlueprintCallable, Category = "Recording Controller")
	void ClearHistory();

	/** Fires after the tree is built, for extra Blueprint styling. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Recording Controller")
	void OnControllerConstructed();

	//~ Exposed panels ----------------------------------------------------------------------------

	UPROPERTY(BlueprintReadOnly, Category = "Recording Controller|Widgets") TObjectPtr<UBorder> RootBorder;
	UPROPERTY(BlueprintReadOnly, Category = "Recording Controller|Widgets") TObjectPtr<UButton> RecordToggleButton;
	UPROPERTY(BlueprintReadOnly, Category = "Recording Controller|Widgets") TObjectPtr<UButton> TestButton;
	UPROPERTY(BlueprintReadOnly, Category = "Recording Controller|Widgets") TObjectPtr<UScrollBox> HistoryScroll;

protected:
	//~ Begin UUserWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	//~ End UUserWidget interface

	UInputRecordingSubsystem* GetRecordingSubsystem() const;

	void BuildTree();
	void RefreshControls();
	void RefreshCurrentInput();

private:
	UFUNCTION() void HandleModeChanged(EInputReplayMode NewMode);
	UFUNCTION() void HandleSyncPointRecorded(FName ActionName, float TimeSeconds, FVector Value);
	UFUNCTION() void HandleRecordClicked();
	UFUNCTION() void HandleTestClicked();

	/** Applies MaxScreenAreaFraction to ClampBox from the current viewport size. Called each tick. */
	void ApplyScreenClamp(const FGeometry& MyGeometry);

	/** Re-applies the last-N highlight window after a row is added. */
	void UpdateHistoryHighlights();

	static FString FormatClock(float Seconds);

	//~ Built widgets ----------------------------------------------------------------------------
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleText;
	UPROPERTY(Transient) TObjectPtr<UBorder> StatusPill;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusPillText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RecordButtonLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TestButtonLabel;
	UPROPERTY(Transient) TObjectPtr<UImage> CurrentInputIcon;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CurrentInputName;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CurrentInputSub;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HistoryCountBadge;

	UPROPERTY(Transient) TArray<TObjectPtr<USyncPointRowWidget>> HistoryRows;

	/** Wraps the panel and caps its size. Between the root canvas and RootBorder. */
	UPROPERTY(Transient) TObjectPtr<USizeBox> ClampBox;

	int32 SyncPointCount = 0;
	float RecordStartWorldTime = 0.f;
	bool bPushedInputMode = false;
};
