// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/RecordingListWidget.h"

#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "InputRecordingLog.h"
#include "Library/InputRecordingFormatLibrary.h"
#include "Settings/InputRecordingSettings.h"
#include "Store/RecordingStore.h"
#include "Subsystem/InputRecordingSubsystem.h"
#include "UI/RecordingListRowWidget.h"

void URecordingListWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (CloseButton)
	{
		CloseButton->OnClicked.AddUniqueDynamic(this, &URecordingListWidget::HandleCloseClicked);
	}
}

void URecordingListWidget::CollectMissingBindings(TArray<FString>& OutMissing) const
{
	if (!RowContainer)
	{
		OutMissing.Add(TEXT("RowContainer"));
	}
	if (!CloseButton)
	{
		OutMissing.Add(TEXT("CloseButton"));
	}
	if (!TotalsText)
	{
		OutMissing.Add(TEXT("TotalsText"));
	}
}

TSubclassOf<URecordingListRowWidget> URecordingListWidget::ResolveRowWidgetClass() const
{
	if (RowWidgetClassOverride)
	{
		return RowWidgetClassOverride;
	}

	if (const UInputRecordingSettings* Settings = UInputRecordingSettings::Get())
	{
		if (UClass* Resolved = Settings->ResolveWidgetClass(Settings->RecordingListRowWidgetClass,
			URecordingListRowWidget::StaticClass(), TEXT("RecordingListRowWidgetClass")))
		{
			return Resolved;
		}
	}

	return URecordingListRowWidget::StaticClass();
}

void URecordingListWidget::RefreshList()
{
	UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	URecordingStore* Store = Subsystem ? Subsystem->GetStore() : nullptr;

	if (!Store)
	{
		UE_LOG(LogInputRecording, Warning, TEXT("Recording list has no store to read."));
		return;
	}

	Store->Rescan();
	const TArray<FRecordingListEntry> Entries = Store->BuildListEntries();

	if (RowContainer)
	{
		RowContainer->ClearChildren();

		const TSubclassOf<URecordingListRowWidget> RowClass = ResolveRowWidgetClass();

		for (const FRecordingListEntry& Entry : Entries)
		{
			URecordingListRowWidget* Row = CreateWidget<URecordingListRowWidget>(GetOwningPlayer(), RowClass);
			if (!Row)
			{
				continue;
			}

			Row->SetEntry(Entry);
			Row->OnRowClicked.AddUniqueDynamic(this, &URecordingListWidget::HandleRowClicked);
			RowContainer->AddChild(Row);
		}
	}

	if (EmptyStateText)
	{
		EmptyStateText->SetVisibility(Entries.Num() == 0 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	if (TotalsText)
	{
		const FRecordingStoreStats Stats = Store->GetStats();
		TotalsText->SetText(FText::FromString(FString::Printf(
			TEXT("%d session(s)  |  %s of %s used  |  %s free"),
			Stats.SessionCount,
			*UInputRecordingFormatLibrary::FormatByteSize(Stats.TotalBytes),
			*UInputRecordingFormatLibrary::FormatByteSize(Stats.QuotaBytes),
			*UInputRecordingFormatLibrary::FormatByteSize(Stats.GetFreeBytes()))));
	}

	K2_OnListRefreshed(Entries.Num());
}

void URecordingListWidget::CloseList()
{
	if (UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem())
	{
		Subsystem->HideRecordingList();
	}
	else
	{
		RemoveFromParent();
	}
}

void URecordingListWidget::HandleCloseClicked()
{
	CloseList();
}

void URecordingListWidget::HandleRowClicked(const FString& FolderName)
{
	UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	if (!Subsystem)
	{
		return;
	}

	// Same path as ir.record.test <session>, so a click and a console command cannot diverge.
	Subsystem->HideRecordingList();
	Subsystem->RunControlRecapTest(FolderName);
}
