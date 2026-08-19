// Copyright (c) Your Studio. All Rights Reserved.

#include "UI/RecordingToastWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "TimerManager.h"

TSharedRef<SWidget> URecordingToastWidget::RebuildWidget()
{
	// A Blueprint subclass that designed its own tree in the editor keeps it - only build the default
	// one when there is nothing there. Same contract as the other widgets in this module.
	if (WidgetTree->RootWidget)
	{
		return Super::RebuildWidget();
	}

	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	WidgetTree->RootWidget = RootCanvas;

	ToastBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ToastBorder"));
	ToastBorder->SetBrushColor(BackgroundColor);
	ToastBorder->SetPadding(FMargin(20.f, 12.f));

	MessageText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MessageText"));
	MessageText->SetColorAndOpacity(FSlateColor(TextColor));
	MessageText->SetAutoWrapText(true);
	ToastBorder->AddChild(MessageText);

	if (UCanvasPanelSlot* BorderSlot = Cast<UCanvasPanelSlot>(RootCanvas->AddChild(ToastBorder)))
	{
		// Anchored to the bottom centre and sized to its content, so a long save path grows the box
		// outwards from the middle rather than pushing it off screen.
		BorderSlot->SetAnchors(FAnchors(0.5f, 1.0f, 0.5f, 1.0f));
		BorderSlot->SetAlignment(FVector2D(0.5f, 1.0f));
		BorderSlot->SetPosition(FVector2D(0.f, -BottomMargin));
		BorderSlot->SetAutoSize(true);
	}

	const TSharedRef<SWidget> Result = Super::RebuildWidget();

	OnToastConstructed();

	return Result;
}

void URecordingToastWidget::ShowMessage(const FString& Message, float DurationSeconds)
{
	if (MessageText)
	{
		MessageText->SetText(FText::FromString(Message));
	}

	if (!IsInViewport())
	{
		AddToViewport(ToastZOrder);
	}

	SetVisibility(ESlateVisibility::HitTestInvisible);

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Restarting the timer rather than stacking one per message is what makes a burst of messages
	// behave: the last one gets its full duration instead of inheriting the remains of an earlier one.
	World->GetTimerManager().ClearTimer(DismissTimerHandle);
	World->GetTimerManager().SetTimer(
		DismissTimerHandle, this, &URecordingToastWidget::Dismiss,
		FMath::Max(0.5f, DurationSeconds), /*bLoop=*/false);
}

void URecordingToastWidget::Dismiss()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DismissTimerHandle);
	}

	if (IsInViewport())
	{
		RemoveFromParent();
	}
}

void URecordingToastWidget::NativeDestruct()
{
	// The timer holds a raw this pointer; leaving it live past teardown is a crash waiting for a
	// level change to happen at the wrong moment.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DismissTimerHandle);
	}

	Super::NativeDestruct();
}
