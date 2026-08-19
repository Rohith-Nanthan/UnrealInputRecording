// Copyright (c) Your Studio. All Rights Reserved.

#include "UI/MatchCueMarkerWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"

UMatchCueMarkerWidget::UMatchCueMarkerWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// A rounded box rounded to a circle: HalfHeightRadius makes a square draw as a dot.
	DotBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
	DotBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
	DotBrush.TintColor = FSlateColor(FLinearColor::White);
}

TSharedRef<SWidget> UMatchCueMarkerWidget::RebuildWidget()
{
	if (!RootColumn)
	{
		BuildTree();
	}
	return Super::RebuildWidget();
}

void UMatchCueMarkerWidget::BuildTree()
{
	RootColumn = WidgetTree->ConstructWidget<UVerticalBox>();
	IconImage  = WidgetTree->ConstructWidget<UImage>();
	NextLabel  = WidgetTree->ConstructWidget<UTextBlock>();
	DotImage   = WidgetTree->ConstructWidget<UImage>();

	// Icon rides at the top of the column.
	IconImage->SetDesiredSizeOverride(IconSize);
	if (UVerticalBoxSlot* IconSlot = RootColumn->AddChildToVerticalBox(IconImage))
	{
		IconSlot->SetHorizontalAlignment(HAlign_Center);
		IconSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	}

	// "next" hint, only visible on the active cue.
	NextLabel->SetText(NSLOCTEXT("InputRecording", "NextCue", "next"));
	NextLabel->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 9));
	NextLabel->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* LabelSlot = RootColumn->AddChildToVerticalBox(NextLabel))
	{
		LabelSlot->SetHorizontalAlignment(HAlign_Center);
		LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	}

	// The fill spacer is what pushes the dot down onto the bar, whatever the column's height.
	if (UVerticalBoxSlot* FillSlot = RootColumn->AddChildToVerticalBox(WidgetTree->ConstructWidget<USpacer>()))
	{
		FillSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	// Dot sits at the bottom, on the progress bar.
	DotImage->SetBrush(DotBrush);
	DotImage->SetDesiredSizeOverride(DotSize);
	if (UVerticalBoxSlot* DotSlot = RootColumn->AddChildToVerticalBox(DotImage))
	{
		DotSlot->SetHorizontalAlignment(HAlign_Center);
		DotSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	}

	WidgetTree->RootWidget = RootColumn;

	ApplyState();
}

void UMatchCueMarkerWidget::InitialiseMarker(int32 InCueIndex, const FMatchInputCue& InCue, const FSlateBrush& InIcon)
{
	CueIndex = InCueIndex;
	Cue = InCue;

	if (!RootColumn)
	{
		// Force the tree up front so the icon can be assigned before the first paint.
		TakeWidget();
	}

	if (IconImage)
	{
		IconImage->SetBrush(InIcon);
		// SetBrush replaced the size; restore the designer-controlled desired size.
		IconImage->SetDesiredSizeOverride(IconSize);
	}

	SetMarkerState(EMatchCueMarkerState::Pending);
	OnMarkerInitialised();
}

void UMatchCueMarkerWidget::SetMarkerState(EMatchCueMarkerState NewState)
{
	MarkerState = NewState;
	ApplyState();
}

void UMatchCueMarkerWidget::ApplyState()
{
	if (!IconImage || !DotImage)
	{
		return;
	}

	FLinearColor IconTint = IconPendingTint;
	FLinearColor DotColor = DotPendingColor;
	float DotScale = 1.0f;
	bool bShowNext = false;

	switch (MarkerState)
	{
	case EMatchCueMarkerState::Active:
		IconTint = IconActiveTint;
		DotColor = DotActiveColor;
		DotScale = ActiveDotScale;
		bShowNext = true;
		break;

	case EMatchCueMarkerState::Completed:
		IconTint = IconCompletedTint;
		DotColor = DotCompletedColor;
		break;

	case EMatchCueMarkerState::Pending:
	default:
		break;
	}

	IconImage->SetColorAndOpacity(IconTint);
	DotImage->SetColorAndOpacity(DotColor);
	DotImage->SetDesiredSizeOverride(DotSize * DotScale);

	if (NextLabel)
	{
		NextLabel->SetColorAndOpacity(FSlateColor(DotActiveColor));
		NextLabel->SetVisibility(bShowNext ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}
