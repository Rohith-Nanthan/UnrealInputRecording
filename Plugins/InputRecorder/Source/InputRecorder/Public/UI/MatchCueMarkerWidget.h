// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MatchInput/MatchInputTypes.h"
#include "UI/InputRecordingWidgetBase.h"
#include "MatchCueMarkerWidget.generated.h"

class UImage;
class UTextBlock;

UENUM(BlueprintType)
enum class EMatchCueMarkerState : uint8
{
	Pending		UMETA(DisplayName = "Pending"),
	Active		UMETA(DisplayName = "Active"),
	Completed	UMETA(DisplayName = "Completed")
};

/**
 * One cue on the review timeline.
 *
 * Carries both its above-bar icon and its on-bar dot, because they sit on one fractional anchor
 * and must share one coordinate space. Two separate rails doing the same arithmetic round it
 * differently and drift apart visibly by the end of a long take.
 */
UCLASS(Blueprintable, BlueprintType)
class INPUTRECORDER_API UMatchCueMarkerWidget : public UInputRecordingWidgetBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UImage> IconImage;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UImage> DotImage;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LabelText;

	UFUNCTION(BlueprintCallable, Category = "Input Recording|UI")
	void SetCue(const FMatchInputCue& InCue, int32 InCueIndex);

	UFUNCTION(BlueprintCallable, Category = "Input Recording|UI")
	void SetMarkerState(EMatchCueMarkerState NewState);

	UFUNCTION(BlueprintPure, Category = "Input Recording|UI")
	EMatchCueMarkerState GetMarkerState() const { return MarkerState; }

	UFUNCTION(BlueprintPure, Category = "Input Recording|UI")
	int32 GetCueIndex() const { return CueIndex; }

	/** The three visual states are a designer's job; C++ only says which one is current. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Input Recording|UI", meta = (DisplayName = "On Marker State Changed"))
	void K2_OnMarkerStateChanged(EMatchCueMarkerState NewState);

	UFUNCTION(BlueprintImplementableEvent, Category = "Input Recording|UI", meta = (DisplayName = "On Cue Set"))
	void K2_OnCueSet(const FMatchInputCue& InCue, int32 InCueIndex);

protected:
	virtual void CollectMissingBindings(TArray<FString>& OutMissing) const override;

private:
	UPROPERTY(Transient)
	FMatchInputCue Cue;

	int32 CueIndex = INDEX_NONE;
	EMatchCueMarkerState MarkerState = EMatchCueMarkerState::Pending;
};
