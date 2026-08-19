// Copyright (c) Your Studio. All Rights Reserved.
//
// RecordingToastWidget.h
//
// The "recording successful, saved to ..." confirmation.
//
// WHY THIS IS A WIDGET AND NOT AddOnScreenDebugMessage
//   On-screen debug messages are gated behind GAreScreenMessagesEnabled, which is off in Shipping.
//   A confirmation that silently stops appearing in the build you ship to a console is worse than no
//   confirmation at all, because nobody notices until someone asks why the artist thinks recording is
//   broken. Real UMG costs one small widget and works everywhere.
//
// Styling is a Blueprint subclass's job: set it on UInputRecordingSubsystem::ToastWidgetClass and
// override the colours, or rebuild the tree entirely from OnToastConstructed.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"

#include "RecordingToastWidget.generated.h"

class UBorder;
class UTextBlock;

UCLASS(Blueprintable, meta = (DisplayName = "Recording Toast Widget"))
class UNREALINPUTRECORDING_API URecordingToastWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Shows a message, replacing whatever is already up and restarting the timer.
	 *
	 * Replacing rather than queueing is deliberate: these messages are status, not a transcript, and
	 * the most recent one is always the one worth reading.
	 */
	UFUNCTION(BlueprintCallable, Category = "Recording Toast")
	void ShowMessage(const FString& Message, float DurationSeconds = 4.f);

	UFUNCTION(BlueprintCallable, Category = "Recording Toast")
	void Dismiss();

	//~ Style hooks -------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Toast|Style")
	FLinearColor BackgroundColor = FLinearColor(0.05f, 0.06f, 0.07f, 0.92f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Toast|Style")
	FLinearColor TextColor = FLinearColor(0.90f, 0.92f, 0.95f, 1.0f);

	/** Distance from the bottom of the screen, in slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Toast|Style")
	float BottomMargin = 96.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording Toast|Style")
	int32 ToastZOrder = 1100;

	/** Fires after the tree is built, for extra Blueprint styling. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Recording Toast")
	void OnToastConstructed();

	UPROPERTY(BlueprintReadOnly, Category = "Recording Toast|Widgets") TObjectPtr<UBorder> ToastBorder;
	UPROPERTY(BlueprintReadOnly, Category = "Recording Toast|Widgets") TObjectPtr<UTextBlock> MessageText;

protected:
	//~ Begin UUserWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeDestruct() override;
	//~ End UUserWidget interface

private:
	FTimerHandle DismissTimerHandle;
};
