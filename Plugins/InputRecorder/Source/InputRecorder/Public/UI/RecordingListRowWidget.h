// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Store/RecordingSessionTypes.h"
#include "UI/InputRecordingWidgetBase.h"
#include "RecordingListRowWidget.generated.h"

class UButton;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRecordingListRowClicked, const FString&, FolderName);

/** One session row: folder, display name, size, relative age, duration, cues, contents. */
UCLASS(Blueprintable, BlueprintType)
class INPUTRECORDER_API URecordingListRowWidget : public UInputRecordingWidgetBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> FolderText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DisplayNameText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SizeText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LastUpdatedText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DurationText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CuesText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ContentsText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PlayableText;

	/** Clicking a row reviews that session. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> SelectButton;

	UPROPERTY(BlueprintAssignable, Category = "Input Recording|UI")
	FOnRecordingListRowClicked OnRowClicked;

	UFUNCTION(BlueprintCallable, Category = "Input Recording|UI")
	void SetEntry(const FRecordingListEntry& InEntry);

	UFUNCTION(BlueprintPure, Category = "Input Recording|UI")
	FRecordingListEntry GetEntry() const { return Entry; }

	UFUNCTION(BlueprintImplementableEvent, Category = "Input Recording|UI", meta = (DisplayName = "On Entry Set"))
	void K2_OnEntrySet(const FRecordingListEntry& InEntry);

protected:
	virtual void NativeOnInitialized() override;
	virtual void CollectMissingBindings(TArray<FString>& OutMissing) const override;

private:
	UFUNCTION()
	void HandleSelectClicked();

	UPROPERTY(Transient)
	FRecordingListEntry Entry;
};
