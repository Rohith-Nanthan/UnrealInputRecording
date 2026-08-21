// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ControlRecapWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "InputRecordingLog.h"
#include "Settings/InputIconMapping.h"
#include "Settings/InputRecordingSettings.h"
#include "Subsystem/InputRecordingSubsystem.h"
#include "UI/MatchCueMarkerWidget.h"
#include "UI/VideoSurfaceWidget.h"
#include "UI/WrongInputRowWidget.h"

void UControlRecapWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (CancelButton)
	{
		CancelButton->OnClicked.AddUniqueDynamic(this, &UControlRecapWidget::HandleCancelClicked);
	}
}

void UControlRecapWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BindSubsystemEvents();
}

void UControlRecapWidget::NativeDestruct()
{
	UnbindSubsystemEvents();
	Super::NativeDestruct();
}

void UControlRecapWidget::CollectMissingBindings(TArray<FString>& OutMissing) const
{
	if (!SessionLabelText)
	{
		OutMissing.Add(TEXT("SessionLabelText"));
	}
	if (!CancelButton)
	{
		OutMissing.Add(TEXT("CancelButton"));
	}
	if (!VideoSizeBox)
	{
		OutMissing.Add(TEXT("VideoSizeBox"));
	}
	if (!VideoSurface)
	{
		OutMissing.Add(TEXT("VideoSurface"));
	}
	if (!CueCounterText)
	{
		OutMissing.Add(TEXT("CueCounterText"));
	}
	if (!TrackProgressBar)
	{
		OutMissing.Add(TEXT("TrackProgressBar"));
	}
	if (!MarkerCanvas)
	{
		OutMissing.Add(TEXT("MarkerCanvas"));
	}
	if (!ExpectedInputText)
	{
		OutMissing.Add(TEXT("ExpectedInputText"));
	}
	if (!WrongInputContainer)
	{
		OutMissing.Add(TEXT("WrongInputContainer"));
	}
}

void UControlRecapWidget::BindSubsystemEvents()
{
	UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	if (!Subsystem || bBoundToSubsystem)
	{
		return;
	}

	Subsystem->OnMatchCuePresented.AddUniqueDynamic(this, &UControlRecapWidget::HandleCuePresented);
	Subsystem->OnMatchInputMatched.AddUniqueDynamic(this, &UControlRecapWidget::HandleCueMatched);
	Subsystem->OnMatchInputMismatched.AddUniqueDynamic(this, &UControlRecapWidget::HandleCueMismatched);
	Subsystem->OnMatchInputFinished.AddUniqueDynamic(this, &UControlRecapWidget::HandleMatchFinished);
	bBoundToSubsystem = true;
}

void UControlRecapWidget::UnbindSubsystemEvents()
{
	UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	if (!Subsystem || !bBoundToSubsystem)
	{
		bBoundToSubsystem = false;
		return;
	}

	Subsystem->OnMatchCuePresented.RemoveDynamic(this, &UControlRecapWidget::HandleCuePresented);
	Subsystem->OnMatchInputMatched.RemoveDynamic(this, &UControlRecapWidget::HandleCueMatched);
	Subsystem->OnMatchInputMismatched.RemoveDynamic(this, &UControlRecapWidget::HandleCueMismatched);
	Subsystem->OnMatchInputFinished.RemoveDynamic(this, &UControlRecapWidget::HandleMatchFinished);
	bBoundToSubsystem = false;
}

// -------------------------------------------------------------------------------------------
// Review lifecycle
// -------------------------------------------------------------------------------------------

void UControlRecapWidget::BeginReview(const FRecordingSessionInfo& Session)
{
	ReviewedSession = Session;

	UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	if (!Subsystem)
	{
		ShowEmptyState(TEXT("No recording subsystem is available in this world."));
		return;
	}

	if (!Session.IsPlayable())
	{
		ShowEmptyState(FString::Printf(TEXT("Session %s has no .ghost file to review."),
			Session.FolderName.IsEmpty() ? TEXT("(none)") : *Session.FolderName));
		return;
	}

	if (EmptyStateText)
	{
		EmptyStateText->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (SessionLabelText)
	{
		SessionLabelText->SetText(FText::FromString(FString::Printf(TEXT("%s  -  %s"),
			*Session.DisplayName, *Session.FolderName)));
	}

	if (!Subsystem->StartMatchInputFromSession(Session))
	{
		ShowEmptyState(FString::Printf(TEXT("Could not start review of %s."), *Session.FolderName));
		return;
	}

	bReviewActive = true;

	const TArray<FMatchInputCue> Cues = Subsystem->GetMatchCues();
	TotalDuration = FMath::Max(0.01f, Subsystem->GetReviewedRecordingDurationSeconds());

	BuildCueMarkers(Cues, TotalDuration);
	ClearWrongInputRows();
	RefreshExpectedPrompt();

	if (VideoSurface)
	{
		VideoSurface->RefreshFromSubsystem();
	}

	K2_OnReviewStarted(Session, Cues.Num());
}

void UControlRecapWidget::ShowEmptyState(const FString& Reason)
{
	bReviewActive = false;

	// An explanatory empty state, never a black screen.
	UE_LOG(LogMatchInput, Warning, TEXT("Control Recap empty state: %s"), *Reason);

	if (EmptyStateText)
	{
		EmptyStateText->SetText(FText::FromString(Reason));
		EmptyStateText->SetVisibility(ESlateVisibility::Visible);
	}

	if (ExpectedInputText)
	{
		ExpectedInputText->SetText(FText::GetEmpty());
	}

	if (CueCounterText)
	{
		CueCounterText->SetText(FText::GetEmpty());
	}
}

void UControlRecapWidget::CloseRecap(bool bCompletedAllCues)
{
	if (UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem())
	{
		Subsystem->StopMatchInput(bCompletedAllCues);
	}

	bReviewActive = false;
	OnRecapClosed.Broadcast(bCompletedAllCues);
}

void UControlRecapWidget::HandleCancelClicked()
{
	CloseRecap(/*bCompletedAllCues=*/false);
}

// -------------------------------------------------------------------------------------------
// Markers
// -------------------------------------------------------------------------------------------

TSubclassOf<UMatchCueMarkerWidget> UControlRecapWidget::ResolveCueMarkerClass() const
{
	if (CueMarkerClassOverride)
	{
		return CueMarkerClassOverride;
	}

	if (const UInputRecordingSettings* Settings = UInputRecordingSettings::Get())
	{
		if (UClass* Resolved = Settings->ResolveWidgetClass(Settings->MatchCueMarkerWidgetClass,
			UMatchCueMarkerWidget::StaticClass(), TEXT("MatchCueMarkerWidgetClass")))
		{
			return Resolved;
		}
	}

	return UMatchCueMarkerWidget::StaticClass();
}

TSubclassOf<UWrongInputRowWidget> UControlRecapWidget::ResolveWrongInputRowClass() const
{
	if (WrongInputRowClassOverride)
	{
		return WrongInputRowClassOverride;
	}

	if (const UInputRecordingSettings* Settings = UInputRecordingSettings::Get())
	{
		if (UClass* Resolved = Settings->ResolveWidgetClass(Settings->WrongInputRowWidgetClass,
			UWrongInputRowWidget::StaticClass(), TEXT("WrongInputRowWidgetClass")))
		{
			return Resolved;
		}
	}

	return UWrongInputRowWidget::StaticClass();
}

void UControlRecapWidget::BuildCueMarkers(const TArray<FMatchInputCue>& Cues, float TotalDurationSeconds)
{
	CueMarkers.Reset();

	if (!MarkerCanvas)
	{
		return;
	}

	MarkerCanvas->ClearChildren();

	const TSubclassOf<UMatchCueMarkerWidget> MarkerClass = ResolveCueMarkerClass();

	for (int32 CueIndex = 0; CueIndex < Cues.Num(); ++CueIndex)
	{
		UMatchCueMarkerWidget* Marker = CreateWidget<UMatchCueMarkerWidget>(GetOwningPlayer(), MarkerClass);
		if (!Marker)
		{
			continue;
		}

		Marker->SetCue(Cues[CueIndex], CueIndex);

		UCanvasPanelSlot* CanvasSlot = MarkerCanvas->AddChildToCanvas(Marker);
		if (CanvasSlot)
		{
			const float Fraction = FMath::Clamp(Cues[CueIndex].TimeSeconds / TotalDurationSeconds, 0.0f, 1.0f);

			// Fractional anchor, so the marker and the progress bar's fill are expressed in the
			// same coordinate space. One widget carries both the above-bar icon and the on-bar
			// dot: a second rail doing the same arithmetic rounds it differently and drifts.
			CanvasSlot->SetAnchors(FAnchors(Fraction, 0.0f, Fraction, 1.0f));
			CanvasSlot->SetAlignment(FVector2D(0.5f, 0.0f));
			CanvasSlot->SetOffsets(FMargin(0.0f));
			CanvasSlot->SetAutoSize(true);
		}

		CueMarkers.Add(Marker);
	}
}

// -------------------------------------------------------------------------------------------
// Match events
// -------------------------------------------------------------------------------------------

void UControlRecapWidget::HandleCuePresented(int32 CueIndex, int32 TotalCues, const FString& ExpectedDescription)
{
	if (CueMarkers.IsValidIndex(CueIndex) && CueMarkers[CueIndex])
	{
		CueMarkers[CueIndex]->SetMarkerState(EMatchCueMarkerState::Active);
	}

	if (CueCounterText)
	{
		CueCounterText->SetText(FText::FromString(FString::Printf(TEXT("cue %d / %d"), CueIndex + 1, TotalCues)));
	}

	if (ExpectedInputText)
	{
		ExpectedInputText->SetText(FText::FromString(FString::Printf(TEXT("Press: %s"), *ExpectedDescription)));
	}

	RefreshExpectedPrompt();
	K2_OnCuePresented(CueIndex, TotalCues, ExpectedDescription);
}

void UControlRecapWidget::HandleCueMatched(int32 CueIndex, int32 TotalCues)
{
	if (CueMarkers.IsValidIndex(CueIndex) && CueMarkers[CueIndex])
	{
		CueMarkers[CueIndex]->SetMarkerState(EMatchCueMarkerState::Completed);
	}

	// Cleared the moment the cue is answered correctly. Leaving a stale "you got it wrong" line
	// beside a cue the player just got right reads as though that one was wrong too.
	ClearWrongInputRows();

	K2_OnCueMatched(CueIndex, TotalCues);
}

void UControlRecapWidget::HandleCueMismatched(const FString& ExpectedDescription, const FString& ReceivedDescription)
{
	if (WrongInputContainer)
	{
		UWrongInputRowWidget* Row = CreateWidget<UWrongInputRowWidget>(GetOwningPlayer(), ResolveWrongInputRowClass());
		if (Row)
		{
			Row->SetWrongInput(ReceivedDescription);
			WrongInputContainer->AddChild(Row);
			WrongInputRows.Add(Row);

			while (WrongInputRows.Num() > MaxWrongInputRows)
			{
				if (UWrongInputRowWidget* Oldest = WrongInputRows[0])
				{
					Oldest->RemoveFromParent();
				}
				WrongInputRows.RemoveAt(0);
			}
		}
	}

	if (MismatchCountText)
	{
		if (const UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem())
		{
			MismatchCountText->SetText(FText::FromString(FString::Printf(TEXT("%d wrong so far"), Subsystem->GetMismatchCount())));
		}
	}

	K2_OnCueMismatched(ExpectedDescription, ReceivedDescription);
}

void UControlRecapWidget::HandleMatchFinished(bool bCompletedAllCues)
{
	bReviewActive = false;

	for (UMatchCueMarkerWidget* Marker : CueMarkers)
	{
		if (Marker && Marker->GetMarkerState() != EMatchCueMarkerState::Completed && bCompletedAllCues)
		{
			Marker->SetMarkerState(EMatchCueMarkerState::Completed);
		}
	}

	K2_OnReviewFinished(bCompletedAllCues);
	OnRecapClosed.Broadcast(bCompletedAllCues);
}

void UControlRecapWidget::ClearWrongInputRows()
{
	for (UWrongInputRowWidget* Row : WrongInputRows)
	{
		if (Row)
		{
			Row->RemoveFromParent();
		}
	}

	WrongInputRows.Reset();

	if (MismatchCountText)
	{
		if (const UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem())
		{
			MismatchCountText->SetText(FText::FromString(FString::Printf(TEXT("%d wrong so far"), Subsystem->GetMismatchCount())));
		}
	}
}

void UControlRecapWidget::RefreshExpectedPrompt()
{
	if (!ExpectedInputIcon)
	{
		return;
	}

	const UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	if (!Subsystem)
	{
		return;
	}

	UTexture2D* Icon = nullptr;

	const TArray<FMatchInputCue> Cues = Subsystem->GetMatchCues();
	const int32 CueIndex = Subsystem->GetCurrentMatchCueIndex();

	if (Cues.IsValidIndex(CueIndex))
	{
		if (const UInputRecordingSettings* Settings = UInputRecordingSettings::Get())
		{
			if (const UInputIconMapping* Mapping = Settings->IconMapping.LoadSynchronous())
			{
				Icon = Mapping->FindIcon(FName(*Cues[CueIndex].ActionName));
			}
		}
	}

	ExpectedInputIcon->SetBrushFromTexture(Icon);
	ExpectedInputIcon->SetVisibility(Icon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

// -------------------------------------------------------------------------------------------
// Per-frame
// -------------------------------------------------------------------------------------------

void UControlRecapWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	UpdateVideoHeight();

	if (VideoSurface)
	{
		VideoSurface->RefreshFromSubsystem();
	}

	if (!bReviewActive)
	{
		return;
	}

	const UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	if (!Subsystem)
	{
		return;
	}

	if (TrackProgressBar && TotalDuration > 0.0f)
	{
		TrackProgressBar->SetPercent(FMath::Clamp(Subsystem->GetMatchClockSeconds() / TotalDuration, 0.0f, 1.0f));
	}
}

void UControlRecapWidget::UpdateVideoHeight()
{
	if (!VideoSizeBox)
	{
		return;
	}

	const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();
	if (!Settings)
	{
		return;
	}

	const FVector2D ViewportSize = UWidgetLayoutLibrary::GetViewportSize(GetWorld());
	double DpiScale = UWidgetLayoutLibrary::GetViewportScale(GetWorld());
	if (DpiScale <= 0.0)
	{
		DpiScale = 1.0;
	}

	if (ViewportSize.Y <= 0.0)
	{
		return;
	}

	const float TargetHeight = static_cast<float>((ViewportSize.Y / DpiScale) * Settings->VideoScreenFraction);
	VideoSizeBox->SetHeightOverride(TargetHeight);
}
