// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SyncPointRowWidget.h"

#include "Components/TextBlock.h"
#include "Library/InputRecordingFormatLibrary.h"

void USyncPointRowWidget::CollectMissingBindings(TArray<FString>& OutMissing) const
{
	if (!ActionText)
	{
		OutMissing.Add(TEXT("ActionText"));
	}
	if (!TimeText)
	{
		OutMissing.Add(TEXT("TimeText"));
	}
	if (!ValueText)
	{
		OutMissing.Add(TEXT("ValueText"));
	}
}

void USyncPointRowWidget::SetSyncPoint(FName ActionName, float TimeSeconds, const FVector& Value)
{
	if (ActionText)
	{
		ActionText->SetText(FText::FromName(ActionName));
	}

	if (TimeText)
	{
		TimeText->SetText(FText::FromString(UInputRecordingFormatLibrary::FormatDurationClock(TimeSeconds)));
	}

	if (ValueText)
	{
		ValueText->SetText(FText::FromString(FString::Printf(TEXT("%+.2f %+.2f %+.2f"), Value.X, Value.Y, Value.Z)));
	}

	K2_OnSyncPointSet(ActionName, TimeSeconds, Value);
}
