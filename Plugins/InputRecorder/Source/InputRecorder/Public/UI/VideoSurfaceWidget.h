// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/InputRecordingWidgetBase.h"
#include "VideoSurfaceWidget.generated.h"

class UImage;
class UMediaTexture;

/**
 * Pure presentation: one UImage pointed at whatever UMediaTexture the subsystem has open.
 *
 * Knows nothing about recording or MatchInput on purpose - any screen that needs a picture
 * embeds one of these rather than growing its own video handling.
 */
UCLASS(Blueprintable, BlueprintType)
class INPUTRECORDER_API UVideoSurfaceWidget : public UInputRecordingWidgetBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UImage> VideoImage;

	/** Shown when there is no video - a take recorded without an encoder is normal, not an error. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UWidget> NoVideoPanel;

	/** Points the brush at the texture. Safe to call every frame; it only writes on a change. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|UI")
	void SetMediaTexture(UMediaTexture* Texture);

	/** Pulls the current texture off the subsystem's video player. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|UI")
	void RefreshFromSubsystem();

	UFUNCTION(BlueprintImplementableEvent, Category = "Input Recording|UI", meta = (DisplayName = "On Video Availability Changed"))
	void K2_OnVideoAvailabilityChanged(bool bHasVideo);

protected:
	virtual void CollectMissingBindings(TArray<FString>& OutMissing) const override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMediaTexture> AppliedTexture;
};
