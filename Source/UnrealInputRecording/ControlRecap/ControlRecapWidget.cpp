// Copyright (c) Your Studio. All Rights Reserved.

#include "ControlRecap/ControlRecapWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "InputRecordingSubsystem.h"
#include "Storage/RecordingStore.h"
#include "UI/InputActionIconMappingDataAsset.h"
#include "UI/MatchCueMarkerWidget.h"
#include "UI/VideoSurfaceWidget.h"

namespace
{
	/** A circle, as far as Slate is concerned: a rounded box with its radius pinned to half its size. */
	FSlateBrush MakeDotBrush(const FLinearColor& Color, float Diameter)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.OutlineSettings.CornerRadii = FVector4(Diameter * 0.5f, Diameter * 0.5f, Diameter * 0.5f, Diameter * 0.5f);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		Brush.TintColor = FSlateColor(Color);
		Brush.ImageSize = FVector2D(Diameter, Diameter);
		return Brush;
	}
}

// ---------------------------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------------------------

TSharedRef<SWidget> UControlRecapWidget::RebuildWidget()
{
	// A Blueprint subclass that laid out its own tree keeps it. Only build the C++ layout when the
	// designer left the canvas empty, which is the normal case.
	if (!WidgetTree->RootWidget)
	{
		BuildTree();
	}

	const TSharedRef<SWidget> Result = Super::RebuildWidget();

	OnRecapConstructed();

	return Result;
}

void UControlRecapWidget::BuildTree()
{
	ScreenBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ScreenBorder"));
	ScreenBorder->SetBrushColor(ScreenColor);
	ScreenBorder->SetPadding(ScreenPadding);
	WidgetTree->RootWidget = ScreenBorder;

	UVerticalBox* ScreenStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ScreenStack"));
	ScreenBorder->SetContent(ScreenStack);

	// ---- Header: cue counter left, Cancel right ----------------------------------------------
	{
		UHorizontalBox* HeaderRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("HeaderRow"));

		CueCounterText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CueCounterText"));
		CueCounterText->SetText(FText::FromString(TEXT("cue 0 / 0")));
		CueCounterText->SetColorAndOpacity(FSlateColor(PrimaryTextColor));
		HeaderRow->AddChildToHorizontalBox(CueCounterText);

		SessionLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SessionLabel"));
		SessionLabel->SetColorAndOpacity(FSlateColor(MutedTextColor));
		if (UHorizontalBoxSlot* LabelSlot = HeaderRow->AddChildToHorizontalBox(SessionLabel))
		{
			LabelSlot->SetPadding(FMargin(12.f, 0.f, 0.f, 0.f));
			LabelSlot->SetVerticalAlignment(VAlign_Center);
		}

		USpacer* HeaderSpacer = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("HeaderSpacer"));
		if (UHorizontalBoxSlot* SpacerSlot = HeaderRow->AddChildToHorizontalBox(HeaderSpacer))
		{
			SpacerSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}

		CancelButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CancelButton"));

		// Focusable by default, and the only focusable control on this screen - which is deliberate.
		// Cancel is the one thing a player must always be able to reach with a pad, and NativeConstruct
		// puts initial focus here for exactly that reason.
		UTextBlock* CancelLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CancelLabel"));
		CancelLabel->SetText(FText::FromString(TEXT("Cancel")));
		CancelLabel->SetColorAndOpacity(FSlateColor(PrimaryTextColor));
		CancelButton->AddChild(CancelLabel);

		HeaderRow->AddChildToHorizontalBox(CancelButton);

		if (UVerticalBoxSlot* HeaderSlot = ScreenStack->AddChildToVerticalBox(HeaderRow))
		{
			HeaderSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
			HeaderSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 16.f));
		}
	}

	// ---- Video: 80% of the height, prompt pill centred over it -------------------------------
	{
		VideoBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("VideoBox"));

		VideoFrame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("VideoFrame"));
		VideoFrame->SetBrushColor(VideoFrameColor);
		VideoFrame->SetPadding(FMargin(0.f));
		VideoBox->SetContent(VideoFrame);

		UOverlay* VideoOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("VideoOverlay"));
		VideoFrame->SetContent(VideoOverlay);

		// The surface itself is added in NativeConstruct - it needs a world to create.
		EmptyStateText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("EmptyStateText"));
		EmptyStateText->SetColorAndOpacity(FSlateColor(MutedTextColor));
		EmptyStateText->SetVisibility(ESlateVisibility::Collapsed);
		if (UOverlaySlot* EmptySlot = VideoOverlay->AddChildToOverlay(EmptyStateText))
		{
			EmptySlot->SetHorizontalAlignment(HAlign_Center);
			EmptySlot->SetVerticalAlignment(VAlign_Center);
		}

		PromptPill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PromptPill"));
		PromptPill->SetBrushColor(FLinearColor(0.05f, 0.08f, 0.13f, 0.88f));
		PromptPill->SetPadding(FMargin(24.f, 12.f));
		PromptPill->SetVisibility(ESlateVisibility::Collapsed);

		UHorizontalBox* PromptRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PromptRow"));
		PromptPill->SetContent(PromptRow);

		UTextBlock* WaitingLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("WaitingLabel"));
		WaitingLabel->SetText(FText::FromString(TEXT("waiting for")));
		WaitingLabel->SetColorAndOpacity(FSlateColor(MutedTextColor));
		if (UHorizontalBoxSlot* WaitingSlot = PromptRow->AddChildToHorizontalBox(WaitingLabel))
		{
			WaitingSlot->SetVerticalAlignment(VAlign_Center);
			WaitingSlot->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
		}

		PromptIcon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("PromptIcon"));
		if (UHorizontalBoxSlot* IconSlot = PromptRow->AddChildToHorizontalBox(PromptIcon))
		{
			IconSlot->SetVerticalAlignment(VAlign_Center);
			IconSlot->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
		}

		PromptActionText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PromptActionText"));
		PromptActionText->SetColorAndOpacity(FSlateColor(PrimaryTextColor));
		if (UHorizontalBoxSlot* ActionSlot = PromptRow->AddChildToHorizontalBox(PromptActionText))
		{
			ActionSlot->SetVerticalAlignment(VAlign_Center);
		}

		if (UOverlaySlot* PromptSlot = VideoOverlay->AddChildToOverlay(PromptPill))
		{
			PromptSlot->SetHorizontalAlignment(HAlign_Center);
			PromptSlot->SetVerticalAlignment(VAlign_Center);
		}

		if (UVerticalBoxSlot* VideoSlot = ScreenStack->AddChildToVerticalBox(VideoBox))
		{
			VideoSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		}
	}

	// ---- Timeline: track, then elapsed/total, then legend -------------------------------------
	{
		UVerticalBox* TimelineStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TimelineStack"));

		// The marker columns span this height: icon at the top, dot on the bar at the bottom. The two
		// text rows below sit outside it so a longer clock never squashes the track.
		const float TrackHeight = FMath::Max(40.f, TimelineHeight - 40.f);

		USizeBox* TrackBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("TrackBox"));
		TrackBox->SetHeightOverride(TrackHeight);

		UOverlay* TrackLayer = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("TrackLayer"));
		TrackBox->SetContent(TrackLayer);

		USizeBox* BarBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("BarBox"));
		BarBox->SetHeightOverride(6.f);

		ProgressBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("ProgressBar"));
		ProgressBar->SetFillColorAndOpacity(AccentColor);
		ProgressBar->SetPercent(0.f);
		BarBox->SetContent(ProgressBar);

		if (UOverlaySlot* BarSlot = TrackLayer->AddChildToOverlay(BarBox))
		{
			// Bottom-aligned so the dots land on it and the icons have the space above.
			BarSlot->SetHorizontalAlignment(HAlign_Fill);
			BarSlot->SetVerticalAlignment(VAlign_Bottom);
		}

		MarkerCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("MarkerCanvas"));
		if (UOverlaySlot* CanvasSlot = TrackLayer->AddChildToOverlay(MarkerCanvas))
		{
			CanvasSlot->SetHorizontalAlignment(HAlign_Fill);
			CanvasSlot->SetVerticalAlignment(VAlign_Fill);
		}

		if (UVerticalBoxSlot* TrackSlot = TimelineStack->AddChildToVerticalBox(TrackBox))
		{
			TrackSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		}

		// Elapsed / total.
		UHorizontalBox* TimeRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("TimeRow"));

		ElapsedText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ElapsedText"));
		ElapsedText->SetText(FText::FromString(TEXT("0:00")));
		ElapsedText->SetColorAndOpacity(FSlateColor(MutedTextColor));
		TimeRow->AddChildToHorizontalBox(ElapsedText);

		USpacer* TimeSpacer = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("TimeSpacer"));
		if (UHorizontalBoxSlot* SpacerSlot = TimeRow->AddChildToHorizontalBox(TimeSpacer))
		{
			SpacerSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}

		TotalText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TotalText"));
		TotalText->SetText(FText::FromString(TEXT("0:00")));
		TotalText->SetColorAndOpacity(FSlateColor(MutedTextColor));
		TimeRow->AddChildToHorizontalBox(TotalText);

		if (UVerticalBoxSlot* TimeSlot = TimelineStack->AddChildToVerticalBox(TimeRow))
		{
			TimeSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
			TimeSlot->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
		}

		// Legend. The dot states are the only thing on screen that is colour-coded without a label
		// attached, so they get one here rather than relying on the player working it out.
		UHorizontalBox* LegendRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("LegendRow"));

		auto AddLegendEntry = [this, LegendRow](const TCHAR* Name, const FLinearColor& Color, const TCHAR* Label)
		{
			UImage* Dot = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), Name);
			Dot->SetBrush(MakeDotBrush(Color, 12.f));
			if (UHorizontalBoxSlot* DotSlot = LegendRow->AddChildToHorizontalBox(Dot))
			{
				DotSlot->SetVerticalAlignment(VAlign_Center);
				DotSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
			}

			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			Text->SetText(FText::FromString(Label));
			Text->SetColorAndOpacity(FSlateColor(MutedTextColor));
			if (UHorizontalBoxSlot* TextSlot = LegendRow->AddChildToHorizontalBox(Text))
			{
				TextSlot->SetVerticalAlignment(VAlign_Center);
				TextSlot->SetPadding(FMargin(0.f, 0.f, 24.f, 0.f));
			}
		};

		AddLegendEntry(TEXT("LegendPassedDot"), FLinearColor(0.85f, 0.86f, 0.87f, 1.f), TEXT("passed"));
		AddLegendEntry(TEXT("LegendNextDot"), AccentColor, TEXT("next expected"));
		AddLegendEntry(TEXT("LegendUpcomingDot"), FLinearColor(0.23f, 0.25f, 0.28f, 1.f), TEXT("upcoming"));

		if (UVerticalBoxSlot* LegendSlot = TimelineStack->AddChildToVerticalBox(LegendRow))
		{
			LegendSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
			LegendSlot->SetPadding(FMargin(0.f, 12.f, 0.f, 0.f));
		}

		if (UVerticalBoxSlot* TimelineSlot = ScreenStack->AddChildToVerticalBox(TimelineStack))
		{
			TimelineSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
			TimelineSlot->SetPadding(FMargin(0.f, 20.f, 0.f, 0.f));
		}
	}
}

void UControlRecapWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (CancelButton && !CancelButton->OnClicked.IsAlreadyBound(this, &UControlRecapWidget::HandleCancelClicked))
	{
		CancelButton->OnClicked.AddDynamic(this, &UControlRecapWidget::HandleCancelClicked);
	}

	// Video surface needs a world, so it cannot be built in RebuildWidget.
	if (!VideoSurface && VideoFrame)
	{
		TSubclassOf<UVideoSurfaceWidget> SurfaceClass = VideoSurfaceClass;
		if (!SurfaceClass) { SurfaceClass = UVideoSurfaceWidget::StaticClass(); }

		VideoSurface = CreateWidget<UVideoSurfaceWidget>(this, SurfaceClass);

		if (VideoSurface)
		{
			if (UOverlay* VideoOverlay = Cast<UOverlay>(VideoFrame->GetContent()))
			{
				if (UOverlaySlot* SurfaceSlot = VideoOverlay->AddChildToOverlay(VideoSurface))
				{
					SurfaceSlot->SetHorizontalAlignment(HAlign_Fill);
					SurfaceSlot->SetVerticalAlignment(VAlign_Fill);
				}

				// Added last, so move it behind the prompt and the empty-state text.
				VideoOverlay->ShiftChild(0, VideoSurface);
			}
		}
	}

	if (UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem())
	{
		Subsystem->OnMatchCuePresented.AddDynamic(this, &UControlRecapWidget::HandleMatchCuePresented);
		Subsystem->OnMatchInputMatched.AddDynamic(this, &UControlRecapWidget::HandleMatchInputMatched);
		Subsystem->OnMatchInputMismatch.AddDynamic(this, &UControlRecapWidget::HandleMatchInputMismatch);
		Subsystem->OnMatchInputFinished.AddDynamic(this, &UControlRecapWidget::HandleMatchInputFinished);
	}

	// Focus starts on Cancel so a pad has somewhere to be from the first frame. Without this the
	// screen accepts navigation input but has nothing focused, which reads as unresponsive.
	if (CancelButton)
	{
		CancelButton->SetKeyboardFocus();
	}
}

void UControlRecapWidget::NativeDestruct()
{
	if (UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem())
	{
		Subsystem->OnMatchCuePresented.RemoveAll(this);
		Subsystem->OnMatchInputMatched.RemoveAll(this);
		Subsystem->OnMatchInputMismatch.RemoveAll(this);
		Subsystem->OnMatchInputFinished.RemoveAll(this);
	}

	Super::NativeDestruct();
}

// ---------------------------------------------------------------------------------------------
// Review lifecycle
// ---------------------------------------------------------------------------------------------

void UControlRecapWidget::BeginReview(const FRecordingSessionInfo& Session)
{
	ReviewedSession = Session;

	if (SessionLabel)
	{
		SessionLabel->SetText(FText::FromString(
			FString::Printf(TEXT("%s.mp4"), *Session.FolderName)));
	}

	UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	if (!Subsystem)
	{
		ShowEmptyState(TEXT("The recording subsystem is unavailable."));
		return;
	}

	if (!Subsystem->StartMatchInputFromSession(Session))
	{
		ShowEmptyState(FString::Printf(TEXT("Could not load %s."), *Session.FolderName));
		return;
	}

	BuildTimeline();
}

void UControlRecapWidget::ShowEmptyState(const FString& Message)
{
	if (EmptyStateText)
	{
		EmptyStateText->SetText(FText::FromString(Message));
		EmptyStateText->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	HidePrompt();

	if (VideoSurface)
	{
		VideoSurface->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (CueCounterText)
	{
		CueCounterText->SetText(FText::FromString(TEXT("no recording")));
	}
}

void UControlRecapWidget::BuildTimeline()
{
	UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	if (!Subsystem || !MarkerCanvas)
	{
		return;
	}

	MarkerCanvas->ClearChildren();
	Markers.Reset();

	Cues = Subsystem->GetMatchCues();
	TotalDurationSeconds = Subsystem->GetRecordingDurationSeconds();

	// A recording whose header lost its duration still has cues; the last one is a usable scale.
	if (TotalDurationSeconds <= KINDA_SMALL_NUMBER && Cues.Num() > 0)
	{
		TotalDurationSeconds = Cues.Last().TimeSeconds;
	}

	if (TotalText)
	{
		TotalText->SetText(FText::FromString(FormatTime(TotalDurationSeconds)));
	}

	if (Cues.Num() == 0 || TotalDurationSeconds <= KINDA_SMALL_NUMBER)
	{
		ShowEmptyState(TEXT("This recording has no sync points to match."));
		return;
	}

	TSubclassOf<UMatchCueMarkerWidget> MarkerClass = CueMarkerClass;
	if (!MarkerClass) { MarkerClass = UMatchCueMarkerWidget::StaticClass(); }

	const float TrackHeight = FMath::Max(40.f, TimelineHeight - 40.f);

	Markers.Reserve(Cues.Num());
	for (int32 Index = 0; Index < Cues.Num(); ++Index)
	{
		UMatchCueMarkerWidget* Marker = CreateWidget<UMatchCueMarkerWidget>(this, MarkerClass);
		if (!Marker)
		{
			Markers.Add(nullptr);
			continue;
		}

		const FSlateBrush Icon = IconMapping ? IconMapping->GetIconForCue(Cues[Index]) : FSlateBrush();
		Marker->InitialiseMarker(Index, Cues[Index], Icon);

		if (UCanvasPanelSlot* MarkerSlot = MarkerCanvas->AddChildToCanvas(Marker))
		{
			const float Fraction = FMath::Clamp(Cues[Index].TimeSeconds / TotalDurationSeconds, 0.f, 1.f);

			// One anchor for the whole column. The icon rides at the top and the dot lands on the bar
			// at the bottom, so both halves of the sync point share an X by construction rather than by
			// two rails agreeing on the same arithmetic.
			MarkerSlot->SetAnchors(FAnchors(Fraction, 0.f));
			MarkerSlot->SetAlignment(FVector2D(0.5, 0.0));
			MarkerSlot->SetAutoSize(false);
			MarkerSlot->SetSize(FVector2D(MarkerColumnWidth, TrackHeight));
			MarkerSlot->SetPosition(FVector2D::ZeroVector);
		}

		Markers.Add(Marker);
	}

	LastAppliedCueIndex = INDEX_NONE;
	RefreshMarkerStates(0);
}

void UControlRecapWidget::CancelReview()
{
	if (UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem())
	{
		// Stopping fires OnMatchInputFinished(false), which lands in HandleMatchInputFinished and closes
		// through the one teardown path. Calling CloseRecap here as well would double-fire OnClosed.
		Subsystem->StopMatchInputMode();
	}
	else
	{
		CloseRecap(false);
	}
}

void UControlRecapWidget::CloseRecap(bool bCompletedAllCues)
{
	if (bClosing)
	{
		return;
	}
	bClosing = true;

	OnClosed.Broadcast(bCompletedAllCues);
}

// ---------------------------------------------------------------------------------------------
// Per-frame refresh
// ---------------------------------------------------------------------------------------------

void UControlRecapWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// The video's share of the screen is applied here rather than left to fill whatever the other two
	// rows do not use, so "80% of the canvas" stays true as the header and timeline change size.
	if (VideoBox)
	{
		const float AvailableHeight = MyGeometry.GetLocalSize().Y - ScreenPadding.Top - ScreenPadding.Bottom;
		if (AvailableHeight > 0.f)
		{
			VideoBox->SetHeightOverride(AvailableHeight * VideoScreenFraction);
		}
	}

	RefreshFromMatchClock();
}

void UControlRecapWidget::RefreshFromMatchClock()
{
	UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	if (!Subsystem || Cues.Num() == 0)
	{
		return;
	}

	const float ClockSeconds = Subsystem->GetMatchClockSeconds();

	if (ProgressBar && TotalDurationSeconds > KINDA_SMALL_NUMBER)
	{
		ProgressBar->SetPercent(FMath::Clamp(ClockSeconds / TotalDurationSeconds, 0.f, 1.f));
	}

	if (ElapsedText)
	{
		ElapsedText->SetText(FText::FromString(FormatTime(ClockSeconds)));
	}

	const int32 CueIndex = Subsystem->GetCurrentMatchCueIndex();

	if (CueCounterText)
	{
		CueCounterText->SetText(FText::FromString(FString::Printf(
			TEXT("cue %d / %d"), FMath::Clamp(CueIndex + 1, 0, Cues.Num()), Cues.Num())));
	}

	if (CueIndex != LastAppliedCueIndex)
	{
		RefreshMarkerStates(CueIndex);
		LastAppliedCueIndex = CueIndex;
	}

	// The prompt is driven by the awaiting flag rather than by the cue-presented event, because the
	// clock also freezes on a mismatch and the prompt has to stay up through that.
	if (Subsystem->IsAwaitingMatchInput())
	{
		ShowPrompt(CueIndex);
	}
	else
	{
		HidePrompt();
	}
}

void UControlRecapWidget::RefreshMarkerStates(int32 ActiveCueIndex)
{
	for (int32 Index = 0; Index < Markers.Num(); ++Index)
	{
		if (!Markers[Index])
		{
			continue;
		}

		EMatchCueMarkerState State = EMatchCueMarkerState::Pending;
		if (Index < ActiveCueIndex)
		{
			State = EMatchCueMarkerState::Completed;
		}
		else if (Index == ActiveCueIndex)
		{
			State = EMatchCueMarkerState::Active;
		}

		Markers[Index]->SetMarkerState(State);
	}
}

void UControlRecapWidget::ShowPrompt(int32 CueIndex)
{
	if (!PromptPill || !Cues.IsValidIndex(CueIndex))
	{
		return;
	}

	if (PromptActionText)
	{
		PromptActionText->SetText(FText::FromString(Cues[CueIndex].ActionName));
	}

	if (PromptIcon && IconMapping)
	{
		PromptIcon->SetBrush(IconMapping->GetIconForCue(Cues[CueIndex]));
	}

	PromptPill->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UControlRecapWidget::HidePrompt()
{
	if (PromptPill)
	{
		PromptPill->SetVisibility(ESlateVisibility::Collapsed);
	}
}

// ---------------------------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------------------------

void UControlRecapWidget::HandleCancelClicked()
{
	CancelReview();
}

void UControlRecapWidget::HandleMatchCuePresented(int32 CueIndex, int32 TotalCues, const FString& ExpectedInput)
{
	RefreshMarkerStates(CueIndex);
	LastAppliedCueIndex = CueIndex;
	ShowPrompt(CueIndex);
}

void UControlRecapWidget::HandleMatchInputMatched(int32 CueIndex, int32 TotalCues)
{
	HidePrompt();
	RefreshMarkerStates(CueIndex + 1);
	LastAppliedCueIndex = CueIndex + 1;
}

void UControlRecapWidget::HandleMatchInputMismatch(const FString& ExpectedInput, const FString& ActualInput)
{
	// Feedback lands where the player is already looking rather than as a counter in the corner. The
	// running total stays in the log and on the subsystem for anyone who wants it.
	if (PromptPill)
	{
		PromptPill->SetBrushColor(FLinearColor(0.24f, 0.06f, 0.07f, 0.92f));
	}
}

void UControlRecapWidget::HandleMatchInputFinished(bool bCompletedAllCues)
{
	CloseRecap(bCompletedAllCues);
}

// ---------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------

UInputRecordingSubsystem* UControlRecapWidget::GetRecordingSubsystem() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UInputRecordingSubsystem>() : nullptr;
}

FString UControlRecapWidget::FormatTime(float Seconds)
{
	const int32 TotalSeconds = FMath::Max(0, FMath::FloorToInt(Seconds));
	return FString::Printf(TEXT("%d:%02d"), TotalSeconds / 60, TotalSeconds % 60);
}
