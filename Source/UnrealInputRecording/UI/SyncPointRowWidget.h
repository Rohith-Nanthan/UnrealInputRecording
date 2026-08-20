// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/InputRecordingWidgetBase.h"
#include "SyncPointRowWidget.generated.h"

class UTextBlock;

/** One row in the corner overlay's sync-point history. */
UCLASS(Blueprintable, BlueprintType)
class UNREALINPUTRECORDING_API USyncPointRowWidget : public UInputRecordingWidgetBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ActionText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TimeText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ValueText;

	UFUNCTION(BlueprintCallable, Category = "Input Recording|UI")
	void SetSyncPoint(FName ActionName, float TimeSeconds, const FVector& Value);

	UFUNCTION(BlueprintImplementableEvent, Category = "Input Recording|UI", meta = (DisplayName = "On Sync Point Set"))
	void K2_OnSyncPointSet(FName ActionName, float TimeSeconds, const FVector& Value);

protected:
	virtual void CollectMissingBindings(TArray<FString>& OutMissing) const override;
};
