// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/InputRecorderOverlayWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Library/InputRecordingFormatLibrary.h"
#include "Settings/InputRecordingSettings.h"
#include "Subsystem/InputRecordingSubsystem.h"
#include "UI/SyncPointRowWidget.h"

void UInputRecorderOverlayWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (ToggleRecordButton)
	{
		ToggleRecordButton->OnClicked.AddUniqueDynamic(this, &UInputRecorderOverlayWidget::HandleToggleClicked);
	}
}

void UInputRecorderOverlayWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BindSubsystemEvents();

	if (const UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem())
	{
		HandleModeChanged(Subsystem->GetMode());
	}
}

void UInputRecorderOverlayWidget::NativeDestruct()
{
	UnbindSubsystemEvents();
	Super::NativeDestruct();
}

void UInputRecorderOverlayWidget::CollectMissingBindings(TArray<FString>& OutMissing) const
{
	if (!RootSizeBox)
	{
		OutMissing.Add(TEXT("RootSizeBox"));
	}
	if (!ToggleRecordButton)
	{
		OutMissing.Add(TEXT("ToggleRecordButton"));
	}
	if (!ToggleRecordLabel)
	{
		OutMissing.Add(TEXT("ToggleRecordLabel"));
	}
	if (!StatusText)
	{
		OutMissing.Add(TEXT("StatusText"));
	}
	if (!LiveInputText)
	{
		OutMissing.Add(TEXT("LiveInputText"));
	}
	if (!HistoryContainer)
	{
		OutMissing.Add(TEXT("HistoryContainer"));
	}
}

void UInputRecorderOverlayWidget::BindSubsystemEvents()
{
	UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	if (!Subsystem || bBoundToSubsystem)
	{
		return;
	}

	// Bound to the subsystem, never to the replay component: the component dies on every respawn
	// and level travel, and a widget holding a pointer to it holds a dangling one within a map
	// change.
	Subsystem->OnModeChanged.AddUniqueDynamic(this, &UInputRecorderOverlayWidget::HandleModeChanged);
	Subsystem->OnSyncPointRecorded.AddUniqueDynamic(this, &UInputRecorderOverlayWidget::HandleSampleRecorded);
	bBoundToSubsystem = true;
}

void UInputRecorderOverlayWidget::UnbindSubsystemEvents()
{
	UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	if (!Subsystem || !bBoundToSubsystem)
	{
		bBoundToSubsystem = false;
		return;
	}

	Subsystem->OnModeChanged.RemoveDynamic(this, &UInputRecorderOverlayWidget::HandleModeChanged);
	Subsystem->OnSyncPointRecorded.RemoveDynamic(this, &UInputRecorderOverlayWidget::HandleSampleRecorded);
	bBoundToSubsystem = false;
}

void UInputRecorderOverlayWidget::HandleToggleClicked()
{
	ToggleRecording();
}

void UInputRecorderOverlayWidget::ToggleRecording()
{
	UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	if (!Subsystem)
	{
		return;
	}

	if (Subsystem->IsRecording())
	{
		Subsystem->StopRecordingAndSave(FString(), UInputRecordingSettings::Get()->bAlsoExportJsonOnSave);
	}
	else
	{
		ClearHistory();
		Subsystem->StartRecording(FString());
	}
}

void UInputRecorderOverlayWidget::HandleModeChanged(EInputReplayMode NewMode)
{
	const bool bRecording = NewMode == EInputReplayMode::Recording;

	if (ToggleRecordLabel)
	{
		ToggleRecordLabel->SetText(FText::FromString(bRecording ? TEXT("Stop") : TEXT("Record")));
	}

	if (StatusText)
	{
		if (const UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem())
		{
			StatusText->SetText(FText::FromString(Subsystem->GetStatusText()));
		}
	}

	K2_OnRecordingStateChanged(NewMode);
}

void UInputRecorderOverlayWidget::HandleSampleRecorded(FName ActionName, float TimeSeconds, FVector Value)
{
	if (!HistoryContainer)
	{
		return;
	}

	USyncPointRowWidget* Row = CreateWidget<USyncPointRowWidget>(GetOwningPlayer(), ResolveSyncPointRowClass());
	if (!Row)
	{
		return;
	}

	Row->SetSyncPoint(ActionName, TimeSeconds, Value);
	HistoryContainer->AddChild(Row);
	HistoryRows.Add(Row);

	// Oldest out of the front, so the newest row is always visible without scrolling.
	while (HistoryRows.Num() > MaxHistoryRows)
	{
		if (USyncPointRowWidget* Oldest = HistoryRows[0])
		{
			Oldest->RemoveFromParent();
		}
		HistoryRows.RemoveAt(0);
	}

	K2_OnSyncPointRowAdded(Row);
}

void UInputRecorderOverlayWidget::ClearHistory()
{
	for (USyncPointRowWidget* Row : HistoryRows)
	{
		if (Row)
		{
			Row->RemoveFromParent();
		}
	}

	HistoryRows.Reset();
}

TSubclassOf<USyncPointRowWidget> UInputRecorderOverlayWidget::ResolveSyncPointRowClass() const
{
	if (SyncPointRowClassOverride)
	{
		return SyncPointRowClassOverride;
	}

	if (const UInputRecordingSettings* Settings = UInputRecordingSettings::Get())
	{
		if (UClass* Resolved = Settings->ResolveWidgetClass(Settings->SyncPointRowWidgetClass,
			USyncPointRowWidget::StaticClass(), TEXT("SyncPointRowWidgetClass")))
		{
			return Resolved;
		}
	}

	return USyncPointRowWidget::StaticClass();
}

void UInputRecorderOverlayWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	UpdateSizeCap();

	const UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	if (!Subsystem)
	{
		return;
	}

	if (LiveInputText)
	{
		FString ActionName;
		FVector Value = FVector::ZeroVector;

		if (Subsystem->GetLiveInputSnapshot(ActionName, Value))
		{
			LiveInputText->SetText(FText::FromString(FString::Printf(TEXT("%s  %+.2f %+.2f %+.2f"),
				*ActionName, Value.X, Value.Y, Value.Z)));
		}
		else
		{
			LiveInputText->SetText(FText::FromString(TEXT("-")));
		}
	}

	if (DurationText && Subsystem->IsRecording())
	{
		DurationText->SetText(FText::FromString(
			UInputRecordingFormatLibrary::FormatDurationClock(Subsystem->GetRecordingDurationSeconds())));
	}
}

void UInputRecorderOverlayWidget::UpdateSizeCap()
{
	if (!RootSizeBox)
	{
		return;
	}

	const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();
	if (!Settings)
	{
		return;
	}

	FVector2D ViewportSize = FVector2D::ZeroVector;
	if (const UWorld* World = GetWorld())
	{
		ViewportSize = UWidgetLayoutLibrary::GetViewportSize(const_cast<UWorld*>(World));
	}

	if (ViewportSize.X <= 0.0 || ViewportSize.Y <= 0.0)
	{
		return;
	}

	// GetViewportScale is the DPI scale UMG actually applies, so dividing by it converts the
	// viewport's pixel size into the widget's own local space. Falling back to the platform
	// scale keeps the cap sane in the window between viewport creation and the first layout.
	double DpiScale = UWidgetLayoutLibrary::GetViewportScale(GetWorld());
	if (DpiScale <= 0.0)
	{
		DpiScale = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(0.0f, 0.0f);
	}
	if (DpiScale <= 0.0)
	{
		DpiScale = 1.0;
	}

	const FVector2D LocalScreen = ViewportSize / DpiScale;
	const double MaxArea = LocalScreen.X * LocalScreen.Y * static_cast<double>(Settings->OverlayMaxScreenAreaFraction);

	// The *content's* desired size, not this widget's. Reading our own would feed the constraint
	// straight back into its own input: once the cap shrinks the panel, the panel's desired size
	// shrinks with it, the next frame sees it fitting comfortably, pins the max to that smaller
	// number, and the overlay ratchets down and can never grow back.
	const UWidget* Content = RootSizeBox->GetChildAt(0);
	FVector2D Desired = Content ? Content->GetDesiredSize() : FVector2D::ZeroVector;
	if (Desired.X <= 1.0 || Desired.Y <= 1.0)
	{
		return;
	}

	const double DesiredArea = Desired.X * Desired.Y;

	if (DesiredArea <= MaxArea)
	{
		// Under budget: no override at all, so a panel with little in it stays small.
		RootSizeBox->ClearMaxDesiredWidth();
		RootSizeBox->ClearMaxDesiredHeight();
		return;
	}

	// Scale both axes by the same factor so the area fits and the panel keeps its shape.
	const double Scale = FMath::Sqrt(MaxArea / DesiredArea);
	RootSizeBox->SetMaxDesiredWidth(static_cast<float>(Desired.X * Scale));
	RootSizeBox->SetMaxDesiredHeight(static_cast<float>(Desired.Y * Scale));
}
