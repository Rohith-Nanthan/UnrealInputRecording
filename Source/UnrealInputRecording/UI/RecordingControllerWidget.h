// Copyright (c) Your Studio. All Rights Reserved.
//
// RecordingControllerWidget.h
//
// The recording control panel: a Start/Stop toggle, a Test button, a "current input" read-out, and a
// scrolling sync-point history.
//
// BLUEPRINT-OWNED LAYOUT
//   This class builds no widget tree. Every visual element is a BindWidget hook filled in by a
//   Blueprint child (WBP_RecordingController), so layout and styling are edited in the UMG designer
//   while the subsystem plumbing stays here.
//
//   Bindings are BindWidgetOptional, not BindWidget: strict binding fails Blueprint compilation on
//   the first name mismatch, which blocks rearranging the tree mid-design and reports one problem at
//   a time. ValidateBindings() logs every missing hook at once instead, and each is null-checked
//   before use. Swap any line to BindWidget once your layout is final if you want it enforced.
//
// A POP-UP, NOT A HUD ELEMENT
//   The subsystem owns this widget and raises it from StartRecording, whichever route that came in by
//   - console command, gameplay code, or the button on this panel. StopRecording takes it away and
//   replaces it with the save confirmation.
//
//   ClampBox caps the footprint: the Blueprint anchors it bottom-right on a canvas, and C++ sets its
//   max desired size each frame so the panel never grows past a share of the screen.
//
// Data sources (all on UInputRecordingSubsystem):
//   Current input   - GetLiveInputSnapshot(), polled each tick.
//   Sync history    - OnInputSyncPointRecorded, one row appended per onset. The last few rows stay
//                     bright (HistoryHighlightCount) while older ones fade, and the list auto-scrolls.
//   Test            - RunControlRecapTest(), which saves the take and travels to ControlRecapLevel.
//                     This widget does not survive that travel, and does not need to.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "InputReplay/InputReplayTypes.h"

#include "RecordingControllerWidget.generated.h"

class UBorder;
class UButton;
class UImage;
class UInputActionIconMappingDataAsset;
class UInputRecordingSubsystem;
class UScrollBox;
class USizeBox;
class USyncPointRowWidget;
class UTextBlock;

UCLASS(Blueprintable, meta = (DisplayName = "Recording Controller Widget"))
class UNREALINPUTRECORDING_API URecordingControllerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// =========================================================================================
	// UMG bindings - name these exactly in the Blueprint's widget tree
	// =========================================================================================

	/**
	 * Wraps the whole panel and caps its size. Put it in a Canvas Panel anchored bottom-right.
	 *
	 * C++ sets MaxDesiredWidth/Height on it every frame - max rather than fixed, so a panel with
	 * little in it stays small. The cap is a ceiling, not an instruction to fill that much.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Recording Controller|Widgets", meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> ClampBox;

	/** Panel background. Bound only so a Blueprint can recolour it from PanelColor. */
	UPROPERTY(BlueprintReadOnly, Category = "Recording Controller|Widgets", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> RootBorder;

	UPROPERTY(BlueprintReadOnly, Category = "Recording Controller|Widgets", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	/** "idle" / "rec 0:12". Recoloured to DangerColor while recording. */
	UPROPERTY(BlueprintReadOnly, Category = "Recording Controller|Widgets", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusPillText;

	UPROPERTY(BlueprintReadOnly, Category = "Recording Controller|Widgets", meta = (BindWidgetOptional))
	TObjectPtr<UButton> RecordToggleButton;

	/** Text inside RecordToggleButton. Flips between "Start recording" and "Stop recording". */
	UPROPERTY(BlueprintReadOnly, Category = "Recording Controller|Widgets", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> RecordButtonLabel;

	UPROPERTY(BlueprintReadOnly, Category = "Recording Controller|Widgets", meta = (BindWidgetOptional))
	TObjectPtr<UButton> TestButton;

	UPROPERTY(BlueprintReadOnly, Category = "Recording Controller|Widgets", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TestButtonLabel;

	/** Sprite for whatever action is being held right now. */
	UPROPERTY(BlueprintReadOnly, Category = "Recording Controller|Widgets", meta = (BindWidgetOptional))
	TObjectPtr<UImage> CurrentInputIcon;

	UPROPERTY(BlueprintReadOnly, Category = "Recording Controller|Widgets", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CurrentInputName;

	UPROPERTY(BlueprintReadOnly, Category = "Recording Controller|Widgets", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CurrentInputSub;

	/** Sync-point rows are appended here, one per onset. */
	UPROPERTY(BlueprintReadOnly, Category = "Recording Controller|Widgets", meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> HistoryScroll;

	UPROPERTY(BlueprintReadOnly, Category = "Recording Controller|Widgets", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HistoryCountBadge;

	// =========================================================================================
	// Setup
	// =========================================================================================

	/** Recording name for Record / Test. Empty falls back to the project's Default Recording Name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Controller|Setup")
	FString RecordingFileName;

	/** Label stored in the recording header. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Controller|Setup")
	FString RecordingDisplayName = TEXT("Tutorial Take");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Controller|Setup")
	bool bUseJsonFormat = false;

	/**
	 * Action -> sprite mapping. Leave empty to use the project setting, which is the normal case.
	 *
	 * Only set this to override the icon set for one screen. See ResolveIconMapping().
	 */
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

	// =========================================================================================
	// Screen footprint
	// =========================================================================================

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

	/**
	 * Gap between the panel and the bottom-right corner.
	 *
	 * Applied to ClampBox's canvas slot when it has one, so the Blueprint only has to anchor the box;
	 * it does not also have to get the offset right.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Controller|Layout")
	FVector2D CornerMargin = FVector2D(24.0, 24.0);

	// =========================================================================================
	// Style
	// =========================================================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Controller|Style")
	FLinearColor PanelColor = FLinearColor(0.05f, 0.06f, 0.07f, 0.94f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Controller|Style")
	FLinearColor AccentColor = FLinearColor(0.24f, 0.55f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Controller|Style")
	FLinearColor DangerColor = FLinearColor(0.90f, 0.29f, 0.29f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Controller|Style")
	FLinearColor MutedColor = FLinearColor(0.6f, 0.65f, 0.7f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Controller|Style")
	FString PanelTitle = TEXT("Recording controller");

	/** Size the current-input sprite is drawn at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Controller|Style")
	FVector2D CurrentInputIconSize = FVector2D(34.0, 34.0);

	// =========================================================================================
	// API - also callable from a Blueprint that wires its own buttons
	// =========================================================================================

	UFUNCTION(BlueprintCallable, Category = "Recording Controller")
	void ToggleRecording();

	UFUNCTION(BlueprintCallable, Category = "Recording Controller")
	void StartTest();

	/** Empties the sync-point history. Called automatically when a new recording starts. */
	UFUNCTION(BlueprintCallable, Category = "Recording Controller")
	void ClearHistory();

	/** Fires once the widget is constructed and bindings are resolved, for extra Blueprint styling. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Recording Controller")
	void OnControllerConstructed();

protected:
	//~ Begin UUserWidget interface
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	//~ End UUserWidget interface

	UInputRecordingSubsystem* GetRecordingSubsystem() const;

	/** Widget's own IconMapping if set, otherwise the project setting. Cached after first resolve. */
	UInputActionIconMappingDataAsset* ResolveIconMapping();

	void RefreshControls();
	void RefreshCurrentInput();

	/** Logs every unbound hook in one message. Called once on construct. */
	void ValidateBindings() const;

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

	UPROPERTY(Transient) TArray<TObjectPtr<USyncPointRowWidget>> HistoryRows;
	UPROPERTY(Transient) TObjectPtr<UInputActionIconMappingDataAsset> ResolvedIconMapping;

	int32 SyncPointCount = 0;
	float RecordStartWorldTime = 0.f;
	bool bPushedInputMode = false;
};
