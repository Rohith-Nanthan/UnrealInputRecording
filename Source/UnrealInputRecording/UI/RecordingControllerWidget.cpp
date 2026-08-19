// Copyright (c) Your Studio. All Rights Reserved.

#include "UI/RecordingControllerWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputRecordingSubsystem.h"
#include "Styling/CoreStyle.h"
#include "UI/InputActionIconMappingDataAsset.h"
#include "UI/MatchVideoPlayerWidget.h"
#include "UI/SyncPointRowWidget.h"

namespace
{
	/** A small helper: a text block with the default mono/regular font at a given size. */
	UTextBlock* MakeText(UWidgetTree* Tree, const FString& InText, int32 Size, const FLinearColor& Color,
						  bool bMono = false)
	{
		UTextBlock* Text = Tree->ConstructWidget<UTextBlock>();
		Text->SetText(FText::FromString(InText));
		Text->SetFont(FCoreStyle::GetDefaultFontStyle(bMono ? "Mono" : "Regular", Size));
		Text->SetColorAndOpacity(FSlateColor(Color));
		return Text;
	}
}

UInputRecordingSubsystem* URecordingControllerWidget::GetRecordingSubsystem() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UInputRecordingSubsystem>() : nullptr;
}

// ---------------------------------------------------------------------------------------------
// Tree
// ---------------------------------------------------------------------------------------------

TSharedRef<SWidget> URecordingControllerWidget::RebuildWidget()
{
	if (!RootBorder)
	{
		BuildTree();
	}
	return Super::RebuildWidget();
}

void URecordingControllerWidget::BuildTree()
{
	RootBorder = WidgetTree->ConstructWidget<UBorder>();
	RootBorder->SetBrushColor(PanelColor);
	RootBorder->SetPadding(FMargin(16.f));

	UVerticalBox* RootVBox = WidgetTree->ConstructWidget<UVerticalBox>();
	RootBorder->SetContent(RootVBox);

	// --- header: title + status pill ---------------------------------------------------------
	{
		UHorizontalBox* HeaderRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		if (UVerticalBoxSlot* S = RootVBox->AddChildToVerticalBox(HeaderRow))
		{
			S->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));
		}

		TitleText = MakeText(WidgetTree, PanelTitle, 15, FLinearColor::White);
		if (UHorizontalBoxSlot* S = HeaderRow->AddChildToHorizontalBox(TitleText))
		{
			S->SetVerticalAlignment(VAlign_Center);
			S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}

		StatusPill = WidgetTree->ConstructWidget<UBorder>();
		StatusPill->SetPadding(FMargin(10.f, 4.f));
		StatusPill->SetBrushColor(FLinearColor(1.f, 1.f, 1.f, 0.06f));
		StatusPillText = MakeText(WidgetTree, TEXT("idle"), 12, MutedColor, /*bMono=*/true);
		StatusPill->SetContent(StatusPillText);
		if (UHorizontalBoxSlot* S = HeaderRow->AddChildToHorizontalBox(StatusPill))
		{
			S->SetVerticalAlignment(VAlign_Center);
			S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		}
	}

	// --- controls: record toggle + test ------------------------------------------------------
	{
		UHorizontalBox* ControlsRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		if (UVerticalBoxSlot* S = RootVBox->AddChildToVerticalBox(ControlsRow))
		{
			S->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));
		}

		RecordToggleButton = WidgetTree->ConstructWidget<UButton>();
		RecordToggleButton->SetBackgroundColor(DangerColor);
		RecordToggleButton->OnClicked.AddDynamic(this, &URecordingControllerWidget::HandleRecordClicked);
		RecordButtonLabel = MakeText(WidgetTree, TEXT("Start recording"), 14, FLinearColor::White);
		RecordButtonLabel->SetJustification(ETextJustify::Center);
		RecordToggleButton->SetContent(RecordButtonLabel);
		if (UHorizontalBoxSlot* S = ControlsRow->AddChildToHorizontalBox(RecordToggleButton))
		{
			S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			S->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
		}

		TestButton = WidgetTree->ConstructWidget<UButton>();
		TestButton->SetBackgroundColor(FLinearColor(0.18f, 0.20f, 0.23f, 1.f));
		TestButton->OnClicked.AddDynamic(this, &URecordingControllerWidget::HandleTestClicked);
		TestButtonLabel = MakeText(WidgetTree, TEXT("Test"), 14, FLinearColor::White);
		TestButtonLabel->SetJustification(ETextJustify::Center);
		TestButton->SetContent(TestButtonLabel);
		if (UHorizontalBoxSlot* S = ControlsRow->AddChildToHorizontalBox(TestButton))
		{
			S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		}
	}

	// --- current input card ------------------------------------------------------------------
	{
		UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
		Card->SetBrushColor(CardColor);
		Card->SetPadding(FMargin(12.f));
		if (UVerticalBoxSlot* S = RootVBox->AddChildToVerticalBox(Card))
		{
			S->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));
		}

		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		Card->SetContent(Row);

		CurrentInputIcon = WidgetTree->ConstructWidget<UImage>();
		CurrentInputIcon->SetDesiredSizeOverride(FVector2D(34.0, 34.0));
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(CurrentInputIcon))
		{
			S->SetVerticalAlignment(VAlign_Center);
			S->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
		}

		UVerticalBox* TextCol = WidgetTree->ConstructWidget<UVerticalBox>();
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(TextCol))
		{
			S->SetVerticalAlignment(VAlign_Center);
			S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}

		CurrentInputName = MakeText(WidgetTree, TEXT("—"), 15, FLinearColor::White, /*bMono=*/true);
		TextCol->AddChildToVerticalBox(CurrentInputName);
		CurrentInputSub = MakeText(WidgetTree, TEXT("no input"), 12, MutedColor);
		TextCol->AddChildToVerticalBox(CurrentInputSub);
	}

	// --- sync point history ------------------------------------------------------------------
	{
		UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
		Card->SetBrushColor(CardColor);
		Card->SetPadding(FMargin(12.f));
		if (UVerticalBoxSlot* S = RootVBox->AddChildToVerticalBox(Card))
		{
			S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}

		UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
		Card->SetContent(Col);

		UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
		if (UVerticalBoxSlot* S = Col->AddChildToVerticalBox(Header))
		{
			S->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
		}

		UTextBlock* Label = MakeText(WidgetTree, TEXT("Sync point history"), 12, MutedColor);
		if (UHorizontalBoxSlot* S = Header->AddChildToHorizontalBox(Label))
		{
			S->SetVerticalAlignment(VAlign_Center);
			S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}

		UBorder* Badge = WidgetTree->ConstructWidget<UBorder>();
		Badge->SetPadding(FMargin(10.f, 2.f));
		Badge->SetBrushColor(FLinearColor(AccentColor.R, AccentColor.G, AccentColor.B, 0.16f));
		HistoryCountBadge = MakeText(WidgetTree, TEXT("0 total"), 12, AccentColor, /*bMono=*/true);
		Badge->SetContent(HistoryCountBadge);
		Header->AddChildToHorizontalBox(Badge);

		HistoryScroll = WidgetTree->ConstructWidget<UScrollBox>();
		HistoryScroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
		if (UVerticalBoxSlot* S = Col->AddChildToVerticalBox(HistoryScroll))
		{
			S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
	}

	WidgetTree->RootWidget = RootBorder;
}

// ---------------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------------

void URecordingControllerWidget::NativeConstruct()
{
	Super::NativeConstruct();

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

void URecordingControllerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshControls();
	RefreshCurrentInput();
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
		if (CurrentInputIcon && IconMapping)
		{
			CurrentInputIcon->SetBrush(IconMapping->GetIconForActionName(FName(*ActionName)));
			CurrentInputIcon->SetDesiredSizeOverride(FVector2D(34.0, 34.0));
			CurrentInputIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
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
	if (!Subsystem || !Subsystem->IsIdle() || ActiveMatchPlayer)
	{
		return;
	}

	if (!Subsystem->StartMatchInputMode(RecordingFileName, bUseJsonFormat))
	{
		UE_LOG(LogInputReplay, Warning,
			TEXT("Test: could not start MatchInput for '%s'. Is there a recording saved under that name?"),
			*RecordingFileName);
		return;
	}

	TSubclassOf<UMatchVideoPlayerWidget> PlayerClass = MatchPlayerClass;
	if (!PlayerClass) { PlayerClass = UMatchVideoPlayerWidget::StaticClass(); }

	ActiveMatchPlayer = CreateWidget<UMatchVideoPlayerWidget>(this, PlayerClass);
	if (!ActiveMatchPlayer)
	{
		Subsystem->StopMatchInputMode();
		return;
	}

	ActiveMatchPlayer->IconMapping = IconMapping;
	ActiveMatchPlayer->CueMarkerClass = CueMarkerClass;
	ActiveMatchPlayer->OnClosed.AddDynamic(this, &URecordingControllerWidget::HandlePlayerClosed);
	ActiveMatchPlayer->AddToViewport(MatchPlayerZOrder);

	// Hide the controller while the full-screen player is up; HandlePlayerClosed brings it back.
	SetVisibility(ESlateVisibility::Collapsed);
}

void URecordingControllerWidget::HandlePlayerClosed(bool bCompletedAllCues)
{
	ActiveMatchPlayer = nullptr;
	SetVisibility(ESlateVisibility::Visible);
	RefreshControls();
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

	const FSlateBrush Icon = IconMapping ? IconMapping->GetIconForActionName(ActionName) : FSlateBrush();
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
	const int32 Whole = FMath::Max(0, FMath::FloorToInt(Seconds));
	return FString::Printf(TEXT("%d:%02d"), Whole / 60, Whole % 60);
}
