// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/InputRecordingWidgetBase.h"
#include "RecordingToastWidget.generated.h"

class UTextBlock;

/** Save / cancel confirmation. Dismisses itself. */
UCLASS(Blueprintable, BlueprintType)
class UNREALINPUTRECORDING_API URecordingToastWidget : public UInputRecordingWidgetBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MessageText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DetailText;

	UFUNCTION(BlueprintCallable, Category = "Input Recording|UI")
	void ShowToast(const FText& Message, const FText& Detail, float DurationSeconds = 4.0f);

	UFUNCTION(BlueprintImplementableEvent, Category = "Input Recording|UI", meta = (DisplayName = "On Toast Shown"))
	void K2_OnToastShown(const FText& Message, const FText& Detail, float DurationSeconds);

	UFUNCTION(BlueprintImplementableEvent, Category = "Input Recording|UI", meta = (DisplayName = "On Toast Expired"))
	void K2_OnToastExpired();

protected:
	virtual void CollectMissingBindings(TArray<FString>& OutMissing) const override;
	virtual void NativeDestruct() override;

private:
	void HandleToastExpired();

	FTimerHandle DismissTimerHandle;
};
