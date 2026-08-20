// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputReplay/InputReplayTypes.h"
#include "UI/InputRecordingWidgetBase.h"
#include "InputRecorderOverlayWidget.generated.h"

class UButton;
class UPanelWidget;
class USizeBox;
class USyncPointRowWidget;
class UTextBlock;

/**
 * Corner overlay pinned bottom-right during normal gameplay.
 *
 * Start/Stop toggle, status pill, live current-input read-out, scrolling sync-point history -
 * and deliberately no Test button. Testing is a console or terminal action (ir.record.test,
 * -IR=1), not a control on a gameplay HUD.
 */
UCLASS(Blueprintable, BlueprintType)
class UNREALINPUTRECORDING_API UInputRecorderOverlayWidget : public UInputRecordingWidgetBase
{
	GENERATED_BODY()

public:
	/** Wrap the whole panel in this. C++ writes its Max sizes every frame - see §9.4. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> RootSizeBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> ToggleRecordButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ToggleRecordLabel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LiveInputText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DurationText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> HistoryContainer;

	/** Per-widget override, resolved as "override if set, else the project setting" - see §15.7. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording|UI")
	TSubclassOf<USyncPointRowWidget> SyncPointRowClassOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording|UI", meta = (ClampMin = "1", ClampMax = "50"))
	int32 MaxHistoryRows = 8;

	UFUNCTION(BlueprintCallable, Category = "Input Recording|UI")
	void ToggleRecording();

	UFUNCTION(BlueprintCallable, Category = "Input Recording|UI")
	void ClearHistory();

	UFUNCTION(BlueprintImplementableEvent, Category = "Input Recording|UI", meta = (DisplayName = "On Recording State Changed"))
	void K2_OnRecordingStateChanged(EInputReplayMode NewMode);

	UFUNCTION(BlueprintImplementableEvent, Category = "Input Recording|UI", meta = (DisplayName = "On Sync Point Row Added"))
	void K2_OnSyncPointRowAdded(USyncPointRowWidget* Row);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void CollectMissingBindings(TArray<FString>& OutMissing) const override;

private:
	UFUNCTION()
	void HandleToggleClicked();

	UFUNCTION()
	void HandleModeChanged(EInputReplayMode NewMode);

	UFUNCTION()
	void HandleSampleRecorded(FName ActionName, float TimeSeconds, FVector Value);

	void BindSubsystemEvents();
	void UnbindSubsystemEvents();

	/**
	 * Writes MaxDesiredWidth / MaxDesiredHeight - max, never fixed, so a panel with little in it
	 * stays small. The cap is a fraction of screen *area*: capping width and height
	 * independently makes the two constraints disagree at unusual aspect ratios and the panel
	 * ends up eating an ultrawide screen.
	 */
	void UpdateSizeCap();

	TSubclassOf<USyncPointRowWidget> ResolveSyncPointRowClass() const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USyncPointRowWidget>> HistoryRows;

	bool bBoundToSubsystem = false;
};
