// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/InputRecordingWidgetBase.h"
#include "WrongInputRowWidget.generated.h"

class UTextBlock;

/**
 * One "you pressed X" line under the expected-input prompt.
 *
 * The text comes from the same formatter as the expected prompt, so the two read in identical
 * phrasing and can be compared at a glance.
 */
UCLASS(Blueprintable, BlueprintType)
class INPUTRECORDER_API UWrongInputRowWidget : public UInputRecordingWidgetBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DescriptionText;

	/** Style this distinctly from the expected prompt - a different colour, at minimum. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|UI")
	void SetWrongInput(const FString& ReceivedDescription);

	UFUNCTION(BlueprintImplementableEvent, Category = "Input Recording|UI", meta = (DisplayName = "On Wrong Input Set"))
	void K2_OnWrongInputSet(const FString& ReceivedDescription);

protected:
	virtual void CollectMissingBindings(TArray<FString>& OutMissing) const override;
};
