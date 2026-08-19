// Copyright (c) Your Studio. All Rights Reserved.

#include "UI/MatchVideoPlayerWidget.h"

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
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "InputRecordingSubsystem.h"
#include "Styling/CoreStyle.h"
#include "UI/InputActionIconMappingDataAsset.h"
#include "UI/MatchCueMarkerWidget.h"
#include "UI/VideoSurfaceWidget.h"
#include "Widgets/Layout/Anchors.h"

UInputRecordingSubsystem* UMatchVideoPlayerWidget::GetRecordingSubsystem() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UInputRecordingSubsystem>() : nullptr;
}

// ---------------------------------------------------------------------------------------------
// Tree construction
// ---------------------------------------------------------------------------------------------

TSharedRef<SWidget> UMatchVideoPlayerWidget::RebuildWidget()
{
	if (!ScreenBorder)
	{
		BuildTree();
	}
	return Super::RebuildWidget();
}

void UMatchVideoPlayerWidget::BuildTree()
{
	// Full-screen backing + padding.
	ScreenBorder = WidgetTree->ConstructWidget<UBorder>();
	ScreenBorder->SetBrushColor(ScreenColor);
	ScreenBorder->SetPadding(BorderPadding);
	ScreenBorder->SetHorizontalAlignment(HAlign_Fill);
	ScreenBorder->SetVerticalAlignment(VAlign_Fill);

	UOverlay* RootOverlay = WidgetTree->ConstructWidget<UOverlay>();
	ScreenBorder->SetContent(RootOverlay);

	// --- content column: video (fills) + timeline (fixed height) -----------------------------
	UVerticalBox* ContentVBox = WidgetTree->ConstructWidget<UVerticalBox>();
	if (UOverlaySlot* ContentSlot = RootOverlay->AddChildToOverlay(ContentVBox))
	{
		ContentSlot->SetHorizontalAlignment(HAlign_Fill);
		ContentSlot->SetVerticalAlignment(VAlign_Fill);
	}

	// Video container fills the remaining space above the timeline - the ~80% share.
	VideoContainer = WidgetTree->ConstructWidget<UBorder>();
	VideoContainer->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
	VideoContainer->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));
	if (UVerticalBoxSlot* VideoSlot = ContentVBox->AddChildToVerticalBox(VideoContainer))
	{
		VideoSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		VideoSlot->SetHorizontalAlignment(HAlign_Fill);
		VideoSlot->SetVerticalAlignment(VAlign_Fill);
	}

	// Timeline strip: fixed height so the video keeps the rest.
	USizeBox* TimelineSizeBox = WidgetTree->ConstructWidget<USizeBox>();
	TimelineSizeBox->SetHeightOverride(TimelineHeight);
	if (UVerticalBoxSlot* TimelineSlot = ContentVBox->AddChildToVerticalBox(TimelineSizeBox))
	{
		TimelineSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		TimelineSlot->SetHorizontalAlignment(HAlign_Fill);
	}

	UOverlay* TimelinePanel = WidgetTree->ConstructWidget<UOverlay>();
	TimelineSizeBox->SetContent(TimelinePanel);

	// The bar sits in a thin strip at the bottom of the timeline panel.
	USizeBox* BarSizeBox = WidgetTree->ConstructWidget<USizeBox>();
	BarSizeBox->SetHeightOverride(10.f);
	ProgressBar = WidgetTree->ConstructWidget<UProgressBar>();
	ProgressBar->SetPercent(0.f);
	ProgressBar->SetFillColorAndOpacity(ProgressFillColor);
	BarSizeBox->SetContent(ProgressBar);
	if (UOverlaySlot* BarSlot = TimelinePanel->AddChildToOverlay(BarSizeBox))
	{
		BarSlot->SetHorizontalAlignment(HAlign_Fill);
		BarSlot->SetVerticalAlignment(VAlign_Bottom);
	}

	// Marker canvas spans the whole strip; each marker is a full-height column (icon top, dot bottom).
	MarkerCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	if (UOverlaySlot* CanvasSlot = TimelinePanel->AddChildToOverlay(MarkerCanvas))
	{
		CanvasSlot->SetHorizontalAlignment(HAlign_Fill);
		CanvasSlot->SetVerticalAlignment(VAlign_Fill);
	}

	// Elapsed / total, bottom-right just above the bar.
	TimeText = WidgetTree->ConstructWidget<UTextBlock>();
	TimeText->SetFont(FCoreStyle::GetDefaultFontStyle("Mono", 11));
	TimeText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.5f)));
	if (UOverlaySlot* TimeSlot = TimelinePanel->AddChildToOverlay(TimeText))
	{
		TimeSlot->SetHorizontalAlignment(HAlign_Right);
		TimeSlot->SetVerticalAlignment(VAlign_Bottom);
		TimeSlot->SetPadding(FMargin(0.f, 0.f, 4.f, 12.f));
	}

	// --- waiting prompt: floats just above the timeline, centred -----------------------------
	PromptBorder = WidgetTree->ConstructWidget<UBorder>();
	PromptBorder->SetBrushColor(PromptBackground);
	PromptBorder->SetPadding(FMargin(16.f, 8.f));
	PromptBorder->SetVisibility(ESlateVisibility::Collapsed);
	{
		UHorizontalBox* PromptRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		PromptBorder->SetContent(PromptRow);

		UTextBlock* WaitingLabel = WidgetTree->ConstructWidget<UTextBlock>();
		WaitingLabel->SetText(NSLOCTEXT("InputRecording", "WaitingFor", "waiting for"));
		WaitingLabel->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 12));
		WaitingLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.65f, 0.7f, 1.f)));
		if (UHorizontalBoxSlot* S = PromptRow->AddChildToHorizontalBox(WaitingLabel))
		{
			S->SetVerticalAlignment(VAlign_Center);
			S->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
		}

		PromptIcon = WidgetTree->ConstructWidget<UImage>();
		PromptIcon->SetDesiredSizeOverride(FVector2D(24.0, 24.0));
		if (UHorizontalBoxSlot* S = PromptRow->AddChildToHorizontalBox(PromptIcon))
		{
			S->SetVerticalAlignment(VAlign_Center);
			S->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		}

		PromptText = WidgetTree->ConstructWidget<UTextBlock>();
		PromptText->SetFont(FCoreStyle::GetDefaultFontStyle("Mono", 15));
		if (UHorizontalBoxSlot* S = PromptRow->AddChildToHorizontalBox(PromptText))
		{
			S->SetVerticalAlignment(VAlign_Center);
		}
	}
	if (UOverlaySlot* PromptSlot = RootOverlay->AddChildToOverlay(PromptBorder))
	{
		PromptSlot->SetHorizontalAlignment(HAlign_Center);
		PromptSlot->SetVerticalAlignment(VAlign_Bottom);
		PromptSlot->SetPadding(FMargin(0.f, 0.f, 0.f, TimelineHeight + 24.f));
	}

	// --- cue counter, top-left ---------------------------------------------------------------
	CueCountText = WidgetTree->ConstructWidget<UTextBlock>();
	CueCountText->SetFont(FCoreStyle::GetDefaultFontStyle("Mono", 12));
	CueCountText->SetColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.65f, 0.7f, 1.f)));
	if (UOverlaySlot* CountSlot = RootOverlay->AddChildToOverlay(CueCountText))
	{
		CountSlot->SetHorizontalAlignment(HAlign_Left);
		CountSlot->SetVerticalAlignment(VAlign_Top);
	}

	// --- cancel button, top-right (always available) -----------------------------------------
	CancelButton = WidgetTree->ConstructWidget<UButton>();
	CancelButton->SetBackgroundColor(CancelButtonColor);
	CancelButton->OnClicked.AddDynamic(this, &UMatchVideoPlayerWidget::HandleCancelClicked);
	{
		UTextBlock* CancelText = WidgetTree->ConstructWidget<UTextBlock>();
		CancelText->SetText(NSLOCTEXT("InputRecording", "Cancel", "Cancel"));
		CancelText->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 13));
		CancelButton->SetContent(CancelText);
	}
	if (UOverlaySlot* CancelSlot = RootOverlay->AddChildToOverlay(CancelButton))
	{
		CancelSlot->SetHorizontalAlignment(HAlign_Right);
		CancelSlot->SetVerticalAlignment(VAlign_Top);
	}

	WidgetTree->RootWidget = ScreenBorder;
}

// ---------------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------------

void UMatchVideoPlayerWidget::NativeConstruct()
{
	Super::NativeConstruct();

	BuildDynamicContent();

	if (UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem())
	{
		Subsystem->OnMatchCuePresented.AddUniqueDynamic(this, &UMatchVideoPlayerWidget::HandleMatchCuePresented);
		Subsystem->OnMatchInputMatched.AddUniqueDynamic(this, &UMatchVideoPlayerWidget::HandleMatchInputMatched);
		Subsystem->OnMatchInputFinished.AddUniqueDynamic(this, &UMatchVideoPlayerWidget::HandleMatchInputFinished);
	}

	ApplyInputLock(true);

	if (VideoSurface)
	{
		VideoSurface->RefreshBinding();
	}

	RefreshFromMatchClock();
	OnPlayerConstructed();
}

void UMatchVideoPlayerWidget::BuildDynamicContent()
{
	// Video surface.
	if (!VideoSurface && VideoContainer)
	{
		TSubclassOf<UVideoSurfaceWidget> SurfaceClass = VideoSurfaceClass;
		if (!SurfaceClass) { SurfaceClass = UVideoSurfaceWidget::StaticClass(); }

		VideoSurface = CreateWidget<UVideoSurfaceWidget>(this, SurfaceClass);
		if (VideoSurface)
		{
			VideoContainer->SetContent(VideoSurface);
		}
	}

	// Timeline markers from the loaded cue list.
	UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	if (!Subsystem || !MarkerCanvas)
	{
		return;
	}

	Cues = Subsystem->GetMatchCues();
	TotalDurationSeconds = Subsystem->GetRecordingDurationSeconds();
	if (TotalDurationSeconds <= KINDA_SMALL_NUMBER && Cues.Num() > 0)
	{
		TotalDurationSeconds = Cues.Last().TimeSeconds;
	}

	if (Cues.Num() == 0 || TotalDurationSeconds <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	TSubclassOf<UMatchCueMarkerWidget> MarkerClass = CueMarkerClass;
	if (!MarkerClass) { MarkerClass = UMatchCueMarkerWidget::StaticClass(); }

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

			// Point-anchor the top-centre of the column at (fraction, 0). A fixed column height equal to
			// the strip means the icon rides at the top and the dot lands on the bar at the bottom, and
			// the horizontal position scales with the canvas so it stays aligned with the fill at any
			// resolution.
			MarkerSlot->SetAnchors(FAnchors(Fraction, 0.f));
			MarkerSlot->SetAlignment(FVector2D(0.5, 0.0));
			MarkerSlot->SetAutoSize(false);
			MarkerSlot->SetSize(FVector2D(MarkerColumnWidth, TimelineHeight));
			MarkerSlot->SetPosition(FVector2D::ZeroVector);
		}

		Markers.Add(Marker);
	}

	LastAppliedCueIndex = INDEX_NONE;
}

void UMatchVideoPlayerWidget::NativeDestruct()
{
	if (UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem())
	{
		Subsystem->OnMatchCuePresented.RemoveAll(this);
		Subsystem->OnMatchInputMatched.RemoveAll(this);
		Subsystem->OnMatchInputFinished.RemoveAll(this);
	}

	// Balanced restore in case we are torn down without going through ClosePlayer (level change, etc).
	ApplyInputLock(false);

	Super::NativeDestruct();
}

void UMatchVideoPlayerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshFromMatchClock();
}

// ---------------------------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------------------------

void UMatchVideoPlayerWidget::RefreshFromMatchClock()
{
	UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	if (!Subsystem)
	{
		return;
	}

	const float ClockSeconds = Subsystem->GetMatchClockSeconds();

	if (ProgressBar)
	{
		const float Percent = (TotalDurationSeconds > KINDA_SMALL_NUMBER)
			? FMath::Clamp(ClockSeconds / TotalDurationSeconds, 0.f, 1.f)
			: 0.f;
		ProgressBar->SetPercent(Percent);
	}

	if (TimeText)
	{
		TimeText->SetText(FText::FromString(FString::Printf(TEXT("%s / %s"),
			*FormatTime(ClockSeconds), *FormatTime(TotalDurationSeconds))));
	}

	const int32 ActiveCueIndex = Subsystem->IsMatchingInput() ? Subsystem->GetCurrentMatchCueIndex() : INDEX_NONE;

	if (CueCountText)
	{
		CueCountText->SetText(FText::FromString(FString::Printf(TEXT("cue %d / %d"),
			FMath::Clamp(ActiveCueIndex + 1, 0, Cues.Num()), Cues.Num())));
	}

	if (ActiveCueIndex != LastAppliedCueIndex)
	{
		RefreshMarkerStates(ActiveCueIndex);
	}

	// The prompt only shows while the system is actually blocked on the player.
	if (PromptBorder)
	{
		const bool bAwaiting = Subsystem->IsAwaitingMatchInput();
		if (bAwaiting)
		{
			ShowPrompt(ActiveCueIndex);
			PromptBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			PromptBorder->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void UMatchVideoPlayerWidget::RefreshMarkerStates(int32 ActiveCueIndex)
{
	LastAppliedCueIndex = ActiveCueIndex;

	for (int32 Index = 0; Index < Markers.Num(); ++Index)
	{
		if (!Markers[Index])
		{
			continue;
		}

		EMatchCueMarkerState State = EMatchCueMarkerState::Pending;
		if (ActiveCueIndex != INDEX_NONE)
		{
			if (Index < ActiveCueIndex)       { State = EMatchCueMarkerState::Completed; }
			else if (Index == ActiveCueIndex) { State = EMatchCueMarkerState::Active; }
		}
		Markers[Index]->SetMarkerState(State);
	}
}

void UMatchVideoPlayerWidget::ShowPrompt(int32 CueIndex)
{
	if (!Cues.IsValidIndex(CueIndex))
	{
		return;
	}

	const FMatchInputCue& Cue = Cues[CueIndex];

	if (PromptIcon && IconMapping)
	{
		PromptIcon->SetBrush(IconMapping->GetIconForCue(Cue));
		PromptIcon->SetDesiredSizeOverride(FVector2D(24.0, 24.0));
	}
	if (PromptText)
	{
		PromptText->SetText(IconMapping
			? IconMapping->GetDisplayNameForCue(Cue)
			: FText::FromString(Cue.Description));
	}
}

FString UMatchVideoPlayerWidget::FormatTime(float Seconds)
{
	const int32 Whole = FMath::Max(0, FMath::FloorToInt(Seconds));
	return FString::Printf(TEXT("%d:%02d"), Whole / 60, Whole % 60);
}

// ---------------------------------------------------------------------------------------------
// Input lockout
// ---------------------------------------------------------------------------------------------

void UMatchVideoPlayerWidget::ApplyInputLock(bool bLock)
{
	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return;
	}

	if (bLock && !bInputLocked)
	{
		// Freeze the pawn without touching Enhanced Input: the matching system reads action values from
		// the EI subsystem, so the player's presses still register while the pawn stops moving/looking.
		PC->SetIgnoreMoveInput(true);
		PC->SetIgnoreLookInput(true);

		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(InputMode);
		PC->SetShowMouseCursor(true);

		bInputLocked = true;
	}
	else if (!bLock && bInputLocked)
	{
		// SetIgnoreMoveInput/LookInput stack, so unlock has to balance the lock exactly once.
		PC->SetIgnoreMoveInput(false);
		PC->SetIgnoreLookInput(false);

		// Hand control back to the controller UI (Game-and-UI, cursor visible).
		FInputModeGameAndUI InputMode;
		PC->SetInputMode(InputMode);
		PC->SetShowMouseCursor(true);

		bInputLocked = false;
	}
}

// ---------------------------------------------------------------------------------------------
// Close paths
// ---------------------------------------------------------------------------------------------

void UMatchVideoPlayerWidget::HandleCancelClicked()
{
	// Route through the component so it fires OnMatchInputFinished(false) -> HandleMatchInputFinished,
	// keeping a single teardown path. Guard against the case where the session already ended.
	if (UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem(); Subsystem && Subsystem->IsMatchingInput())
	{
		Subsystem->StopMatchInputMode();
	}
	else
	{
		ClosePlayer(/*bCompletedAllCues=*/false);
	}
}

void UMatchVideoPlayerWidget::HandleMatchCuePresented(int32 CueIndex, int32 TotalCues, const FString& ExpectedInput)
{
	RefreshMarkerStates(CueIndex);
	ShowPrompt(CueIndex);
}

void UMatchVideoPlayerWidget::HandleMatchInputMatched(int32 CueIndex, int32 TotalCues)
{
	RefreshMarkerStates(CueIndex + 1);
}

void UMatchVideoPlayerWidget::HandleMatchInputFinished(bool bCompletedAllCues)
{
	ClosePlayer(bCompletedAllCues);
}

void UMatchVideoPlayerWidget::ClosePlayer(bool bCompletedAllCues)
{
	if (bClosing)
	{
		return;
	}
	bClosing = true;

	if (UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem())
	{
		Subsystem->OnMatchCuePresented.RemoveAll(this);
		Subsystem->OnMatchInputMatched.RemoveAll(this);
		Subsystem->OnMatchInputFinished.RemoveAll(this);
	}

	ApplyInputLock(false);

	OnClosed.Broadcast(bCompletedAllCues);

	RemoveFromParent();
}
