// Copyright (c) Your Studio. All Rights Reserved.

#include "UI/RecordingControllerWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputRecordingSubsystem.h"
#include "Styling/CoreStyle.h"
#include "UI/InputActionIconMappingDataAsset.h"
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

		// UButton is focusable by default, so a gamepad can already reach both of these. Focus movement
		// between them is Slate's navigation config, not Enhanced Input - see URecordingUIInputConfig
		// for why that distinction matters.
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

	// The panel is pinned to the bottom-right corner and capped at a share of the screen, so it sits
	// out of the way of whatever the player is actually doing. A canvas is the only panel that can
	// anchor to a corner without stretching, and the size box between it and the border is what caps
	// the footprint - see ApplyScreenClamp.
	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));

	ClampBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ClampBox"));
	ClampBox->SetContent(RootBorder);

	if (UCanvasPanelSlot* ClampSlot = RootCanvas->AddChildToCanvas(ClampBox))
	{
		ClampSlot->SetAnchors(FAnchors(1.f, 1.f));
		ClampSlot->SetAlignment(FVector2D(1.0, 1.0));
		ClampSlot->SetPosition(FVector2D(-CornerMargin.X, -CornerMargin.Y));
		ClampSlot->SetAutoSize(true);
	}

	WidgetTree->RootWidget = RootCanvas;
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

	// Re-applied every frame rather than once on construct: the viewport can be resized, and on
	// console it changes with the safe-area settings the player picks.
	ApplyScreenClamp(MyGeometry);

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
	if (!Subsystem)
	{
		return;
	}

	// Test now leaves the gameplay map entirely rather than pushing a full-screen widget over it.
	//
	// The review UI lives in ControlRecapLevel with its own game mode and player controller, so it can
	// lock input and own the whole screen without negotiating with whatever the gameplay map is doing.
	// The subsystem handles the rest: stop, save, commit the session, then travel. This widget is
	// destroyed by that travel, which is why there is no "bring the panel back" path any more.
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
