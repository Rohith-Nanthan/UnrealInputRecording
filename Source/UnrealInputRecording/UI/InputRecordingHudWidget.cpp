// Copyright (c) Your Studio. All Rights Reserved.

#include "InputRecordingHudWidget.h"

#include "Components/Button.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "InputRecordingSubsystem.h"
#include "InputReplay/InputReplayComponent.h"       // LogInputReplay

UInputRecordingSubsystem* UInputRecordingHudWidget::GetRecordingSubsystem() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UInputRecordingSubsystem>() : nullptr;
}

// ---------------------------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------------------------

void UInputRecordingHudWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (RecordButton)		{ RecordButton->OnClicked.AddUniqueDynamic(this, &UInputRecordingHudWidget::HandleRecordClicked); }
	if (StopButton)			{ StopButton->OnClicked.AddUniqueDynamic(this, &UInputRecordingHudWidget::HandleStopClicked); }
	if (MatchInputButton)	{ MatchInputButton->OnClicked.AddUniqueDynamic(this, &UInputRecordingHudWidget::HandleMatchInputClicked); }

	// Loud about missing bindings: a mistyped widget name is otherwise completely silent.
	if (!RecordButton || !StopButton || !MatchInputButton)
	{
		UE_LOG(LogInputReplay, Warning,
			TEXT("%s: button bindings missing (Record=%s Stop=%s MatchInput=%s). The Widget Blueprint ")
			TEXT("must contain Buttons named exactly RecordButton, StopButton and MatchInputButton."),
			*GetName(),
			RecordButton ? TEXT("ok") : TEXT("MISSING"),
			StopButton ? TEXT("ok") : TEXT("MISSING"),
			MatchInputButton ? TEXT("ok") : TEXT("MISSING"));
	}

	if (UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem())
	{
		Subsystem->OnModeChanged.AddUniqueDynamic(this, &UInputRecordingHudWidget::OnModeChanged);
		Subsystem->OnMatchCuePresented.AddUniqueDynamic(this, &UInputRecordingHudWidget::OnCuePresented);
		Subsystem->OnMatchInputMatched.AddUniqueDynamic(this, &UInputRecordingHudWidget::OnCueMatched);
		Subsystem->OnMatchInputMismatch.AddUniqueDynamic(this, &UInputRecordingHudWidget::OnMismatch);
		Subsystem->OnMatchInputFinished.AddUniqueDynamic(this, &UInputRecordingHudWidget::OnMatchFinished);

		if (!RecordingFileName.IsEmpty())
		{
			Subsystem->ActiveRecordingName = RecordingFileName;
		}
		Subsystem->bUseJsonFormat = bUseJsonFormat;

		// Resolve the component now rather than on the first button press. This is what makes the
		// subsystem attach its relays early, so cues started from elsewhere (the console exec
		// commands, a level Blueprint) still reach this widget.
		Subsystem->GetReplayComponent();
	}

	// Game-and-UI, never UI-only: MatchInput has to keep receiving gameplay input.
	if (bManagePlayerInputMode)
	{
		if (APlayerController* PlayerController = GetOwningPlayer())
		{
			FInputModeGameAndUI InputMode;
			InputMode.SetHideCursorDuringCapture(false);
			PlayerController->SetInputMode(InputMode);
			PlayerController->SetShowMouseCursor(true);
			bPushedInputMode = true;
		}
	}

	RefreshDisplay();
	UpdateButtonStates();
}

void UInputRecordingHudWidget::NativeDestruct()
{
	if (RecordButton)		{ RecordButton->OnClicked.RemoveAll(this); }
	if (StopButton)			{ StopButton->OnClicked.RemoveAll(this); }
	if (MatchInputButton)	{ MatchInputButton->OnClicked.RemoveAll(this); }

	if (UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem())
	{
		Subsystem->OnModeChanged.RemoveAll(this);
		Subsystem->OnMatchCuePresented.RemoveAll(this);
		Subsystem->OnMatchInputMatched.RemoveAll(this);
		Subsystem->OnMatchInputMismatch.RemoveAll(this);
		Subsystem->OnMatchInputFinished.RemoveAll(this);
	}

	if (bPushedInputMode)
	{
		if (APlayerController* PlayerController = GetOwningPlayer())
		{
			PlayerController->SetShowMouseCursor(false);
			PlayerController->SetInputMode(FInputModeGameOnly());
		}
		bPushedInputMode = false;
	}

	Super::NativeDestruct();
}

void UInputRecordingHudWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (MismatchMessageTimer > 0.0f)
	{
		MismatchMessageTimer -= InDeltaTime;
		if (MismatchMessageTimer <= 0.0f && MismatchText)
		{
			MismatchText->SetText(FText::GetEmpty());
		}
	}

	// 20 Hz is plenty for a countdown read-out and keeps us out of the per-frame string-building
	// business. Cue transitions come through delegates, so nothing latency-sensitive waits on this.
	RefreshTimer -= InDeltaTime;
	if (RefreshTimer <= 0.0f)
	{
		RefreshTimer = 0.05f;
		RefreshDisplay();
	}
}

// ---------------------------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------------------------

void UInputRecordingHudWidget::HandleRecordClicked()
{
	UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	if (!Subsystem)
	{
		return;
	}

	if (!RecordingFileName.IsEmpty())
	{
		Subsystem->ActiveRecordingName = RecordingFileName;
	}
	Subsystem->bUseJsonFormat = bUseJsonFormat;

	Subsystem->StartRecording(RecordingDisplayName);
	UpdateButtonStates();
	RefreshDisplay();
}

void UInputRecordingHudWidget::HandleStopClicked()
{
	if (UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem())
	{
		// StopAll saves an in-progress recording rather than discarding it.
		Subsystem->StopAll();
	}

	UpdateButtonStates();
	RefreshDisplay();
}

void UInputRecordingHudWidget::HandleMatchInputClicked()
{
	UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	if (!Subsystem)
	{
		return;
	}

	if (MismatchText)
	{
		MismatchText->SetText(FText::GetEmpty());
	}
	MismatchMessageTimer = 0.0f;

	if (!Subsystem->StartMatchInputMode(RecordingFileName, bUseJsonFormat) && StatusText)
	{
		// The subsystem has already logged the specific reason; put something in front of the user
		// so a failed button press is not just silence.
		StatusText->SetText(FText::FromString(TEXT("Match Input failed - see the Output Log")));
	}

	UpdateButtonStates();
}

// ---------------------------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------------------------

void UInputRecordingHudWidget::RefreshDisplay()
{
	const UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	if (!Subsystem)
	{
		return;
	}

	if (StatusText)
	{
		StatusText->SetText(FText::FromString(Subsystem->GetStatusText()));
	}

	if (ExpectedInputText)
	{
		const FString Expected = Subsystem->IsMatchingInput() ? Subsystem->GetExpectedInputDescription() : FString();
		ExpectedInputText->SetText(Expected.IsEmpty()
			? FText::GetEmpty()
			: FText::FromString(FString::Printf(TEXT("Press: %s"), *Expected)));
	}

	if (CountdownText)
	{
		if (Subsystem->IsMatchingInput() && !Subsystem->IsAwaitingMatchInput())
		{
			CountdownText->SetText(FText::FromString(
				FString::Printf(TEXT("Next cue in %.1fs"), Subsystem->GetTimeUntilNextCue())));
		}
		else if (Subsystem->IsAwaitingMatchInput())
		{
			CountdownText->SetText(FText::FromString(TEXT("Waiting for your input...")));
		}
		else
		{
			CountdownText->SetText(FText::GetEmpty());
		}
	}

	if (ProgressBar)
	{
		// MatchInput progress is measured in cues; ghost playback in recorded ticks.
		float Percent = 0.0f;
		if (Subsystem->IsMatchingInput())
		{
			Percent = Subsystem->GetMatchProgress();
		}
		else if (Subsystem->IsPlayingBack())
		{
			Percent = Subsystem->GetPlaybackProgress();
		}
		ProgressBar->SetPercent(Percent);
	}
}

void UInputRecordingHudWidget::UpdateButtonStates()
{
	const UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	const EInputReplayMode Mode = Subsystem ? Subsystem->GetMode() : EInputReplayMode::Idle;
	const bool bIdle = (Mode == EInputReplayMode::Idle);

	if (RecordButton)		{ RecordButton->SetIsEnabled(bIdle); }
	if (MatchInputButton)	{ MatchInputButton->SetIsEnabled(bIdle); }
	if (StopButton)			{ StopButton->SetIsEnabled(!bIdle); }
}

// ---------------------------------------------------------------------------------------------
// Subsystem event relays
// ---------------------------------------------------------------------------------------------

void UInputRecordingHudWidget::OnModeChanged(EInputReplayMode NewMode)
{
	UpdateButtonStates();
	RefreshDisplay();
}

void UInputRecordingHudWidget::OnCuePresented(int32 CueIndex, int32 TotalCues, const FString& ExpectedInput)
{
	if (ExpectedInputText)
	{
		ExpectedInputText->SetText(FText::FromString(FString::Printf(TEXT("Press: %s"), *ExpectedInput)));
	}

	NotifyCuePresented(ExpectedInput, CueIndex, TotalCues);
}

void UInputRecordingHudWidget::OnCueMatched(int32 CueIndex, int32 TotalCues)
{
	if (MismatchText)
	{
		MismatchText->SetText(FText::GetEmpty());
	}
	MismatchMessageTimer = 0.0f;

	NotifyCueMatched(CueIndex, TotalCues);
	RefreshDisplay();
}

void UInputRecordingHudWidget::OnMismatch(const FString& ExpectedInput, const FString& ActualInput)
{
	if (MismatchText)
	{
		MismatchText->SetText(FText::FromString(
			FString::Printf(TEXT("Wrong input - expected %s, got %s"), *ExpectedInput, *ActualInput)));
	}
	MismatchMessageTimer = MismatchDisplaySeconds;

	NotifyMismatch(ExpectedInput, ActualInput);
}

void UInputRecordingHudWidget::OnMatchFinished(bool bCompletedAllCues)
{
	if (StatusText)
	{
		const UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
		const int32 Mismatches = Subsystem ? Subsystem->GetMismatchCount() : 0;

		StatusText->SetText(FText::FromString(bCompletedAllCues
			? FString::Printf(TEXT("Sequence complete - %d wrong input(s)"), Mismatches)
			: FString(TEXT("Match Input stopped"))));
	}

	if (ExpectedInputText)	{ ExpectedInputText->SetText(FText::GetEmpty()); }
	if (CountdownText)		{ CountdownText->SetText(FText::GetEmpty()); }

	NotifyMatchInputFinished(bCompletedAllCues);
	UpdateButtonStates();
}
