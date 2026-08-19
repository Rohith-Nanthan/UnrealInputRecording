// Copyright (c) Your Studio. All Rights Reserved.

#include "UI/SyncPointRowWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Styling/CoreStyle.h"

TSharedRef<SWidget> USyncPointRowWidget::RebuildWidget()
{
	if (!RootBorder)
	{
		BuildTree();
	}
	return Super::RebuildWidget();
}

void USyncPointRowWidget::BuildTree()
{
	RootBorder = WidgetTree->ConstructWidget<UBorder>();
	RootBorder->SetPadding(RowPadding);

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	RootBorder->SetContent(Row);

	IconImage = WidgetTree->ConstructWidget<UImage>();
	IconImage->SetDesiredSizeOverride(IconSize);
	if (UHorizontalBoxSlot* IconSlot = Row->AddChildToHorizontalBox(IconImage))
	{
		IconSlot->SetVerticalAlignment(VAlign_Center);
		IconSlot->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
	}

	NameText = WidgetTree->ConstructWidget<UTextBlock>();
	NameText->SetFont(FCoreStyle::GetDefaultFontStyle("Mono", FontSize));
	if (UHorizontalBoxSlot* NameSlot = Row->AddChildToHorizontalBox(NameText))
	{
		NameSlot->SetVerticalAlignment(VAlign_Center);
		NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	TimeText = WidgetTree->ConstructWidget<UTextBlock>();
	TimeText->SetFont(FCoreStyle::GetDefaultFontStyle("Mono", FontSize));
	TimeText->SetJustification(ETextJustify::Right);
	if (UHorizontalBoxSlot* TimeSlot = Row->AddChildToHorizontalBox(TimeText))
	{
		TimeSlot->SetVerticalAlignment(VAlign_Center);
		TimeSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	}

	WidgetTree->RootWidget = RootBorder;

	ApplyHighlight();
}

void USyncPointRowWidget::SetSyncPoint(const FSlateBrush& Icon, const FText& ActionLabel, float TimeSeconds)
{
	if (!RootBorder)
	{
		TakeWidget();
	}

	if (IconImage)
	{
		IconImage->SetBrush(Icon);
		IconImage->SetDesiredSizeOverride(IconSize);
	}
	if (NameText)
	{
		NameText->SetText(ActionLabel);
	}
	if (TimeText)
	{
		TimeText->SetText(FText::FromString(FString::Printf(TEXT("t=%.2fs"), TimeSeconds)));
	}
}

void USyncPointRowWidget::SetHighlighted(bool bInHighlighted)
{
	bHighlighted = bInHighlighted;
	ApplyHighlight();
}

void USyncPointRowWidget::ApplyHighlight()
{
	if (RootBorder)
	{
		RootBorder->SetBrushColor(bHighlighted ? HighlightBackground : NormalBackground);
	}

	const FSlateColor TextColor(bHighlighted ? HighlightText : FadedText);
	if (NameText) { NameText->SetColorAndOpacity(TextColor); }
	if (TimeText) { TimeText->SetColorAndOpacity(TextColor); }
	if (IconImage) { IconImage->SetColorAndOpacity(bHighlighted ? FLinearColor::White : FadedText); }
}
