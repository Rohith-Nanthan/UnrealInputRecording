// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/RecordingListRowWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"

void URecordingListRowWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (SelectButton)
	{
		SelectButton->OnClicked.AddUniqueDynamic(this, &URecordingListRowWidget::HandleSelectClicked);
	}
}

void URecordingListRowWidget::CollectMissingBindings(TArray<FString>& OutMissing) const
{
	if (!FolderText)
	{
		OutMissing.Add(TEXT("FolderText"));
	}
	if (!DisplayNameText)
	{
		OutMissing.Add(TEXT("DisplayNameText"));
	}
	if (!SizeText)
	{
		OutMissing.Add(TEXT("SizeText"));
	}
	if (!LastUpdatedText)
	{
		OutMissing.Add(TEXT("LastUpdatedText"));
	}
	if (!SelectButton)
	{
		OutMissing.Add(TEXT("SelectButton"));
	}
}

void URecordingListRowWidget::SetEntry(const FRecordingListEntry& InEntry)
{
	Entry = InEntry;

	if (FolderText)
	{
		FolderText->SetText(FText::FromString(Entry.FolderName));
	}
	if (DisplayNameText)
	{
		DisplayNameText->SetText(FText::FromString(Entry.DisplayName));
	}
	if (SizeText)
	{
		SizeText->SetText(FText::FromString(Entry.SizeText));
	}
	if (LastUpdatedText)
	{
		LastUpdatedText->SetText(FText::FromString(Entry.LastUpdatedText));
	}
	if (DurationText)
	{
		DurationText->SetText(FText::FromString(Entry.DurationText));
	}
	if (CuesText)
	{
		CuesText->SetText(FText::AsNumber(Entry.CueCount));
	}
	if (ContentsText)
	{
		ContentsText->SetText(FText::FromString(Entry.ContentsText));
	}
	if (PlayableText)
	{
		PlayableText->SetText(FText::FromString(Entry.bPlayable ? TEXT("yes") : TEXT("no")));
	}

	if (SelectButton)
	{
		// An unplayable folder has no ghost to review, so the row says so rather than opening a
		// review map that would come up empty.
		SelectButton->SetIsEnabled(Entry.bPlayable);
	}

	K2_OnEntrySet(Entry);
}

void URecordingListRowWidget::HandleSelectClicked()
{
	OnRowClicked.Broadcast(Entry.FolderName);
}
