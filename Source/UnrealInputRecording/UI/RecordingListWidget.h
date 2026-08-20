// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Store/RecordingSessionTypes.h"
#include "UI/InputRecordingWidgetBase.h"
#include "RecordingListWidget.generated.h"

class UButton;
class UPanelWidget;
class URecordingListRowWidget;
class UTextBlock;

/**
 * In-game session browser, raised over whatever is on screen by ir.store.list.ui.
 *
 * Consumes the same FRecordingListEntry array the console printer does. Formatting a row here
 * as well would guarantee the two eventually disagree.
 */
UCLASS(Blueprintable, BlueprintType)
class UNREALINPUTRECORDING_API URecordingListWidget : public UInputRecordingWidgetBase
{
	GENERATED_BODY()

public:
	/** Rows are added here, most recently updated first. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> RowContainer;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TotalsText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> EmptyStateText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> CloseButton;

	/**
	 * Per-widget override for the row class, resolved as "override if set, else the project
	 * setting". Any widget that a C++ path can instantiate directly needs both paths - a field
	 * only ever filled in by a Blueprint subclass is silently null in a C++-created instance,
	 * and the symptom looks exactly like a misconfigured data asset.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording|UI")
	TSubclassOf<URecordingListRowWidget> RowWidgetClassOverride;

	UFUNCTION(BlueprintCallable, Category = "Input Recording|UI")
	void RefreshList();

	UFUNCTION(BlueprintCallable, Category = "Input Recording|UI")
	void CloseList();

	UFUNCTION(BlueprintImplementableEvent, Category = "Input Recording|UI", meta = (DisplayName = "On List Refreshed"))
	void K2_OnListRefreshed(int32 RowCount);

protected:
	virtual void NativeOnInitialized() override;
	virtual void CollectMissingBindings(TArray<FString>& OutMissing) const override;

private:
	UFUNCTION()
	void HandleCloseClicked();

	UFUNCTION()
	void HandleRowClicked(const FString& FolderName);

	TSubclassOf<URecordingListRowWidget> ResolveRowWidgetClass() const;
};
