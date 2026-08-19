// Copyright (c) Your Studio. All Rights Reserved.

#include "UI/RecordingControllerWidget.h"

#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputRecordingSettings.h"
#include "InputRecordingSubsystem.h"
#include "Storage/RecordingSessionTypes.h"
#include "UI/InputActionIconMappingDataAsset.h"
#include "UI/SyncPointRowWidget.h"

UInputRecordingSubsystem* URecordingControllerWidget::GetRecordingSubsystem() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UInputRecordingSubsystem>() : nullptr;
}

UInputActionIconMappingDataAsset* URecordingControllerWidget::ResolveIconMapping()
{
	if (ResolvedIconMapping)
	{
		return ResolvedIconMapping;
	}

	// Per-widget override first, project setting second. The setting is what makes icons appear
	// without anyone remembering to fill this field in on every Blueprint that shows one.
	if (IconMapping)
	{
		ResolvedIconMapping = IconMapping;
	}
	else if (const UInputRecordingSettings* Settings = UInputRecordingSettings::Get())
	{
		ResolvedIconMapping = Settings->LoadIconMapping();
	}

	return ResolvedIconMapping;
}

// ---------------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------------

void URecordingControllerWidget::NativeConstruct()
{
	Super::NativeConstruct();

	ValidateBindings();

	if (RootBorder)
	{
		RootBorder->SetBrushColor(PanelColor);
	}

	if (TitleText)
	{
		TitleText->SetText(FText::FromString(PanelTitle));
	}

	if (TestButtonLabel)
	{
		TestButtonLabel->SetText(FText::FromString(TEXT("Test")));
	}

	if (RecordToggleButton)
	{
		RecordToggleButton->OnClicked.AddUniqueDynamic(this, &URecordingControllerWidget::HandleRecordClicked);
	}

	if (TestButton)
	{
		TestButton->OnClicked.AddUniqueDynamic(this, &URecordingControllerWidget::HandleTestClicked);
	}

	// The Blueprint only has to anchor ClampBox bottom-right; the offset comes from here so the
	// margin stays a single tunable rather than something baked into the layout.
	if (ClampBox)
	{
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(ClampBox->Slot))
		{
			CanvasSlot->SetPosition(FVector2D(-CornerMargin.X, -CornerMargin.Y));
		}
	}

	if (UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem())
	{
		Subsystem->OnModeChanged.AddUniqueDynamic(this, &URecordingControllerWidget::HandleModeChanged);
		Subsystem->OnInputSyncPointRecorded.AddUniqueDynamic(this, &URecordingControllerWidget::HandleSyncPointRecorded);
	}

	if (bManagePlayerInputMode)
	{
		if (APlayerController* PC = GetOwningPlayer())
		{
			FInputModeGameAndUI InputMode;
			InputMode.SetHideCursorDuringCapture(false);
			PC->SetInputMode(InputMode);
			PC->SetShowMouseCursor(true);
			bPushedInputMode = true;
		}
	}

	RefreshControls();

	OnControllerConstructed();
}

void URecordingControllerWidget::NativeDestruct()
{
	if (UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem())
	{
		Subsystem->OnModeChanged.RemoveAll(this);
		Subsystem->OnInputSyncPointRecorded.RemoveAll(this);
	}

	if (bPushedInputMode)
	{
		if (APlayerController* PC = GetOwningPlayer())
		{
			PC->SetInputMode(FInputModeGameOnly());
			PC->SetShowMouseCursor(false);
		}
		bPushedInputMode = false;
	}

	Super::NativeDestruct();
}

void URecordingControllerWidget::ValidateBindings() const
{
	TArray<FString> Missing;

	auto Check = [&Missing](const UObject* Bound, const TCHAR* Name)
	{
		if (!Bound)
		{
			Missing.Add(Name);
		}
	};

	Check(ClampBox,           TEXT("ClampBox (Size Box)"));
	Check(RecordToggleButton, TEXT("RecordToggleButton (Button)"));
	Check(RecordButtonLabel,  TEXT("RecordButtonLabel (Text)"));
	Check(TestButton,         TEXT("TestButton (Button)"));
	Check(StatusPillText,     TEXT("StatusPillText (Text)"));
	Check(CurrentInputName,   TEXT("CurrentInputName (Text)"));
	Check(CurrentInputIcon,   TEXT("CurrentInputIcon (Image)"));
	Check(HistoryScroll,      TEXT("HistoryScroll (Scroll Box)"));

	if (Missing.Num() == 0)
	{
		return;
	}

	UE_LOG(LogRecordingStore, Warning,
		TEXT("%s is missing %d UMG binding(s): %s. Add widgets with these exact names to the ")
		TEXT("Blueprint's tree - see the header for the expected hierarchy."),
		*GetClass()->GetName(), Missing.Num(), *FString::Join(Missing, TEXT(", ")));
}

void URecordingControllerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// Re-applied every frame rather than once on construct: the viewport can be resized, and on
	// console it changes with the safe-area settings the player picks.
	ApplyScreenClamp(MyGeometry);

	RefreshControls();
	RefreshCurrentInput();
}

void URecordingControllerWidget::ApplyScreenClamp(const FGeometry& MyGeometry)
{
	if (!ClampBox)
	{
		return;
	}

	const FVector2D ScreenSize = MyGeometry.GetLocalSize();
	if (ScreenSize.X <= 0.f || ScreenSize.Y <= 0.f)
	{
		return;
	}

	float WidthFraction  = MaxWidthFraction;
	float HeightFraction = MaxHeightFraction;

	// Scale both axes by the same factor when the requested box would exceed the area budget. Shrinking
	// only one axis would change the panel's proportions with the display's, which is exactly the
	// behaviour the area cap exists to avoid.
	const float RequestedArea = WidthFraction * HeightFraction;
	if (RequestedArea > MaxScreenAreaFraction && RequestedArea > KINDA_SMALL_NUMBER)
	{
		const float Scale = FMath::Sqrt(MaxScreenAreaFraction / RequestedArea);
		WidthFraction  *= Scale;
		HeightFraction *= Scale;
	}

	// Max rather than fixed overrides: a panel with little in it should stay small. The cap is a
	// ceiling on how much screen this may take, not an instruction to fill that much.
	ClampBox->SetMaxDesiredWidth(ScreenSize.X * WidthFraction);
	ClampBox->SetMaxDesiredHeight(ScreenSize.Y * HeightFraction);
}

// ---------------------------------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------------------------------

void URecordingControllerWidget::RefreshControls()
{
	UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	if (!Subsystem)
	{
		return;
	}

	const bool bRecording = Subsystem->IsRecording();

	if (RecordButtonLabel)
	{
		RecordButtonLabel->SetText(FText::FromString(bRecording ? TEXT("Stop recording") : TEXT("Start recording")));
	}

	if (RecordToggleButton)
	{
		RecordToggleButton->SetBackgroundColor(bRecording ? DangerColor : AccentColor);
	}

	// Test only makes sense from idle - there has to be a finished take to play back.
	if (TestButton)
	{
		TestButton->SetIsEnabled(Subsystem->IsIdle());
	}

	if (StatusPillText)
	{
		if (bRecording)
		{
			const float Elapsed = GetWorld() ? (GetWorld()->GetTimeSeconds() - RecordStartWorldTime) : 0.f;
			StatusPillText->SetText(FText::FromString(FString::Printf(TEXT("rec %s"), *FormatClock(Elapsed))));
			StatusPillText->SetColorAndOpacity(FSlateColor(DangerColor));
		}
		else
		{
			StatusPillText->SetText(FText::FromString(TEXT("idle")));
			StatusPillText->SetColorAndOpacity(FSlateColor(MutedColor));
		}
	}
}

void URecordingControllerWidget::RefreshCurrentInput()
{
	UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	if (!Subsystem || !CurrentInputName)
	{
		return;
	}

	FString ActionName;
	FVector Value;
	const bool bHasInput = Subsystem->GetLiveInputSnapshot(ActionName, Value);

	if (bHasInput)
	{
		CurrentInputName->SetText(FText::FromString(ActionName));

		if (CurrentInputSub)
		{
			CurrentInputSub->SetText(FText::FromString(
				FString::Printf(TEXT("pressed · %.2f"), Value.Size())));
		}

		if (CurrentInputIcon)
		{
			if (UInputActionIconMappingDataAsset* Icons = ResolveIconMapping())
			{
				CurrentInputIcon->SetBrush(Icons->GetIconForActionName(FName(*ActionName)));
				CurrentInputIcon->SetDesiredSizeOverride(CurrentInputIconSize);
				CurrentInputIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
			}
		}
	}
	else
	{
		CurrentInputName->SetText(FText::FromString(TEXT("—")));

		if (CurrentInputSub) { CurrentInputSub->SetText(FText::FromString(TEXT("no input"))); }
		if (CurrentInputIcon) { CurrentInputIcon->SetVisibility(ESlateVisibility::Hidden); }
	}
}

// ---------------------------------------------------------------------------------------------
// Button handlers
// ---------------------------------------------------------------------------------------------

void URecordingControllerWidget::HandleRecordClicked() { ToggleRecording(); }
void URecordingControllerWidget::HandleTestClicked()   { StartTest(); }

void URecordingControllerWidget::ToggleRecording()
{
	UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	if (!Subsystem)
	{
		return;
	}

	if (Subsystem->IsRecording())
	{
		if (RecordingFileName.IsEmpty())
		{
			Subsystem->StopRecording();
		}
		else
		{
			Subsystem->StopRecordingAndSave(RecordingFileName, bUseJsonFormat);
		}
	}
	else if (Subsystem->IsIdle())
	{
		if (!RecordingFileName.IsEmpty())
		{
			Subsystem->ActiveRecordingName = RecordingFileName;
		}
		ClearHistory();
		Subsystem->StartRecording(RecordingDisplayName);
	}

	RefreshControls();
}

void URecordingControllerWidget::StartTest()
{
	UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	if (!Subsystem)
	{
		return;
	}

	// Test leaves the gameplay map entirely rather than pushing a full-screen widget over it.
	//
	// The review UI lives in ControlRecapLevel with its own game mode and player controller, so it can
	// lock input and own the whole screen without negotiating with whatever the gameplay map is doing.
	// The subsystem handles the rest: stop, save, commit the session, then travel. This widget is
	// destroyed by that travel, which is why there is no "bring the panel back" path.
	Subsystem->RunControlRecapTest();
}

// ---------------------------------------------------------------------------------------------
// Sync-point history
// ---------------------------------------------------------------------------------------------

void URecordingControllerWidget::HandleModeChanged(EInputReplayMode NewMode)
{
	if (NewMode == EInputReplayMode::Recording)
	{
		RecordStartWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
		ClearHistory();
	}

	RefreshControls();
}

void URecordingControllerWidget::HandleSyncPointRecorded(FName ActionName, float TimeSeconds, FVector Value)
{
	if (!HistoryScroll)
	{
		return;
	}

	TSubclassOf<USyncPointRowWidget> RowClass = SyncRowClass;
	if (!RowClass) { RowClass = USyncPointRowWidget::StaticClass(); }

	USyncPointRowWidget* Row = CreateWidget<USyncPointRowWidget>(this, RowClass);
	if (!Row)
	{
		return;
	}

	UInputActionIconMappingDataAsset* Icons = ResolveIconMapping();
	const FSlateBrush Icon = Icons ? Icons->GetIconForActionName(ActionName) : FSlateBrush();

	Row->SetSyncPoint(Icon, FText::FromName(ActionName), TimeSeconds);

	HistoryScroll->AddChild(Row);
	HistoryRows.Add(Row);

	++SyncPointCount;
	if (HistoryCountBadge)
	{
		HistoryCountBadge->SetText(FText::FromString(FString::Printf(TEXT("%d total"), SyncPointCount)));
	}

	UpdateHistoryHighlights();

	// Auto-scroll so the newest sync point is always visible - the "last 5 stay in view" requirement.
	HistoryScroll->ScrollToEnd();
}

void URecordingControllerWidget::UpdateHistoryHighlights()
{
	const int32 Threshold = HistoryRows.Num() - FMath::Max(1, HistoryHighlightCount);

	// Only the two rows on the boundary can change state per add, but a full pass is trivially cheap and
	// keeps the window correct after a ClearHistory or a resize.
	for (int32 Index = 0; Index < HistoryRows.Num(); ++Index)
	{
		if (HistoryRows[Index])
		{
			HistoryRows[Index]->SetHighlighted(Index >= Threshold);
		}
	}
}

void URecordingControllerWidget::ClearHistory()
{
	if (HistoryScroll)
	{
		HistoryScroll->ClearChildren();
	}

	HistoryRows.Reset();
	SyncPointCount = 0;

	if (HistoryCountBadge)
	{
		HistoryCountBadge->SetText(FText::FromString(TEXT("0 total")));
	}
}

FString URecordingControllerWidget::FormatClock(float Seconds)
{
	const int32 Total = FMath::Max(0, FMath::FloorToInt(Seconds));
	return FString::Printf(TEXT("%d:%02d"), Total / 60, Total % 60);
}
