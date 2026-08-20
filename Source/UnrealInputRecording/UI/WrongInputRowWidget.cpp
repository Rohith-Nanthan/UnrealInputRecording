// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/WrongInputRowWidget.h"

#include "Components/TextBlock.h"

void UWrongInputRowWidget::CollectMissingBindings(TArray<FString>& OutMissing) const
{
	if (!DescriptionText)
	{
		OutMissing.Add(TEXT("DescriptionText"));
	}
}

void UWrongInputRowWidget::SetWrongInput(const FString& ReceivedDescription)
{
	if (DescriptionText)
	{
		DescriptionText->SetText(FText::FromString(FString::Printf(TEXT("You pressed: %s"), *ReceivedDescription)));
	}

	K2_OnWrongInputSet(ReceivedDescription);
}
