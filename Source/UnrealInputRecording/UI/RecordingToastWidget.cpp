// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/RecordingToastWidget.h"

#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "TimerManager.h"

void URecordingToastWidget::CollectMissingBindings(TArray<FString>& OutMissing) const
{
	if (!MessageText)
	{
		OutMissing.Add(TEXT("MessageText"));
	}
}

void URecordingToastWidget::ShowToast(const FText& Message, const FText& Detail, float DurationSeconds)
{
	if (MessageText)
	{
		MessageText->SetText(Message);
	}

	if (DetailText)
	{
		DetailText->SetText(Detail);
		DetailText->SetVisibility(Detail.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	SetVisibility(ESlateVisibility::HitTestInvisible);

	if (UWorld* World = GetWorld())
	{
		// Re-showing while one is already up restarts the clock rather than stacking timers.
		World->GetTimerManager().ClearTimer(DismissTimerHandle);
		World->GetTimerManager().SetTimer(DismissTimerHandle, FTimerDelegate::CreateUObject(this, &URecordingToastWidget::HandleToastExpired),
			FMath::Max(0.5f, DurationSeconds), /*bLoop=*/false);
	}

	K2_OnToastShown(Message, Detail, DurationSeconds);
}

void URecordingToastWidget::HandleToastExpired()
{
	SetVisibility(ESlateVisibility::Collapsed);
	K2_OnToastExpired();
}

void URecordingToastWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DismissTimerHandle);
	}

	Super::NativeDestruct();
}
