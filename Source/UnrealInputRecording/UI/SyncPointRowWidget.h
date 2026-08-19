// Copyright (c) Your Studio. All Rights Reserved.
//
// SyncPointRowWidget.h
//
// One line in the controller's sync-point history: [icon] IA_Name .......... t=3.28s
//
// Built in C++ so the controller can spawn them by the hundred without a Blueprint asset. A subclass
// can restyle via the Style properties; SetHighlighted is what the "last 5 stay visible" band uses to
// keep the most recent rows bright while older ones fade.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"

#include "SyncPointRowWidget.generated.h"

class UBorder;
class UHorizontalBox;
class UImage;
class UTextBlock;

UCLASS(Blueprintable, meta = (DisplayName = "Sync Point Row Widget"))
class UNREALINPUTRECORDING_API USyncPointRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Fill the row. Time is the recorded-timeline seconds the onset landed at. */
	UFUNCTION(BlueprintCallable, Category = "Sync Point Row")
	void SetSyncPoint(const FSlateBrush& Icon, const FText& ActionLabel, float TimeSeconds);

	/** Bright (one of the last 5) vs faded (scrolled-back history). */
	UFUNCTION(BlueprintCallable, Category = "Sync Point Row")
	void SetHighlighted(bool bHighlighted);

	//~ Style hooks --------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sync Point Row|Style")
	FLinearColor HighlightBackground = FLinearColor(0.30f, 0.55f, 1.0f, 0.14f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sync Point Row|Style")
	FLinearColor NormalBackground = FLinearColor(0.f, 0.f, 0.f, 0.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sync Point Row|Style")
	FLinearColor HighlightText = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sync Point Row|Style")
	FLinearColor FadedText = FLinearColor(1.f, 1.f, 1.f, 0.5f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sync Point Row|Style")
	FVector2D IconSize = FVector2D(20.0, 20.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sync Point Row|Style")
	int32 FontSize = 13;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sync Point Row|Style")
	FMargin RowPadding = FMargin(12.f, 6.f);

protected:
	//~ Begin UUserWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	//~ End UUserWidget interface

	void BuildTree();
	void ApplyHighlight();

	UPROPERTY(Transient) TObjectPtr<UBorder> RootBorder;
	UPROPERTY(Transient) TObjectPtr<UImage> IconImage;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NameText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TimeText;

	bool bHighlighted = true;
};
