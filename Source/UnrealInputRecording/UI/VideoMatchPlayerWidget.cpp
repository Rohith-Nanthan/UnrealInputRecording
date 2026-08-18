// Copyright (c) Your Studio. All Rights Reserved.

#include "UI/VideoMatchPlayerWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "InputRecordingSubsystem.h"
#include "InputReplay/InputRecordingDataAsset.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MediaTexture.h"
#include "UI/InputActionIconMappingDataAsset.h"
#include "Video/InputRecordingVideoPlayer.h"
#include "Widgets/Layout/Anchors.h"                 // FAnchors - lives in Slate, not SlateCore

// ---------------------------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------------------------

UInputRecordingSubsystem* UVideoMatchPlayerWidget::GetRecordingSubsystem() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UInputRecordingSubsystem>() : nullptr;
}

void UVideoMatchPlayerWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem())
	{
		Subsystem->OnModeChanged.AddUniqueDynamic(this, &UVideoMatchPlayerWidget::HandleModeChanged);
		Subsystem->OnMatchCuePresented.AddUniqueDynamic(this, &UVideoMatchPlayerWidget::HandleCuePresented);
		Subsystem->OnMatchInputMatched.AddUniqueDynamic(this, &UVideoMatchPlayerWidget::HandleCueMatched);
		Subsystem->OnMatchInputMismatch.AddUniqueDynamic(this, &UVideoMatchPlayerWidget::HandleMismatch);
		Subsystem->OnMatchInputFinished.AddUniqueDynamic(this, &UVideoMatchPlayerWidget::HandleMatchFinished);

		if (UInputRecordingVideoPlayer* VideoPlayer = Subsystem->GetVideoPlayer())
		{
			VideoPlayer->OnVideoOpened.AddUniqueDynamic(this, &UVideoMatchPlayerWidget::HandleVideoOpened);
		}
	}

	if (!TimelineCanvas)
	{
		UE_LOG(LogInputMatch, Warning,
			TEXT("%s has no widget named 'TimelineCanvas'. Cue icons will not be shown - add a Canvas ")
			TEXT("Panel with that exact name, overlaid on the progress bar."),
			*GetName());
	}

	BindVideoSurface();

	// A session may already be running (this widget was added mid-tutorial), so prefer live data and
	// fall back to the preview asset only when there is nothing to show.
	if (GetRecordingSubsystem() && GetRecordingSubsystem()->GetMatchCueCount() > 0)
	{
		BuildTimelineFromSubsystem();
	}
	else if (PreviewRecordingAsset && PreviewRecordingAsset->MatchInputCues.Num() > 0)
	{
		BuildTimeline(PreviewRecordingAsset->MatchInputCues, PreviewRecordingAsset->DurationSeconds);
	}
	else
	{
		// Nothing loaded: clear whatever placeholder the designer left in the prompt widgets.
		ShowExpectedInput(INDEX_NONE);
	}

	RefreshDisplay();
}

void UVideoMatchPlayerWidget::NativeDestruct()
{
	if (UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem())
	{
		Subsystem->OnModeChanged.RemoveAll(this);
		Subsystem->OnMatchCuePresented.RemoveAll(this);
		Subsystem->OnMatchInputMatched.RemoveAll(this);
		Subsystem->OnMatchInputMismatch.RemoveAll(this);
		Subsystem->OnMatchInputFinished.RemoveAll(this);

		if (UInputRecordingVideoPlayer* VideoPlayer = Subsystem->GetVideoPlayer())
		{
			VideoPlayer->OnVideoOpened.RemoveAll(this);
		}
	}

	Super::NativeDestruct();
}

void UVideoMatchPlayerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	if (!Subsystem)
	{
		return;
	}

	if (bDriveVideoPlayhead && Subsystem->IsMatchingInput())
	{
		if (UInputRecordingVideoPlayer* VideoPlayer = Subsystem->GetVideoPlayer())
		{
			VideoPlayer->SyncToMatchClock(Subsystem->GetMatchClockSeconds(), Subsystem->IsAwaitingMatchInput());
		}
	}

	RefreshDisplay();
}

// ---------------------------------------------------------------------------------------------
// Video surface
// ---------------------------------------------------------------------------------------------

void UVideoMatchPlayerWidget::BindVideoSurface()
{
	if (!VideoImage)
	{
		return;
	}

	UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	UInputRecordingVideoPlayer* VideoPlayer = Subsystem ? Subsystem->GetVideoPlayer() : nullptr;
	UMediaTexture* MediaTexture = VideoPlayer ? VideoPlayer->GetMediaTexture() : nullptr;

	if (!MediaTexture)
	{
		return;
	}

	if (bUseMaterialForVideo && VideoMaterial)
	{
		if (!VideoMaterialInstance)
		{
			VideoMaterialInstance = UMaterialInstanceDynamic::Create(VideoMaterial, this);
		}

		if (VideoMaterialInstance)
		{
			VideoMaterialInstance->SetTextureParameterValue(VideoMaterialTextureParameter, MediaTexture);
			VideoImage->SetBrushFromMaterial(VideoMaterialInstance);
		}
		return;
	}

	// SetBrushResourceObject rather than SetBrushFromTexture: a UMediaTexture is a UTexture but not a
	// UTexture2D, so the typed setter would refuse it.
	VideoImage->SetBrushResourceObject(MediaTexture);
}

// ---------------------------------------------------------------------------------------------
// Timeline construction
// ---------------------------------------------------------------------------------------------

void UVideoMatchPlayerWidget::BuildTimelineFromSubsystem()
{
	UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	if (!Subsystem)
	{
		return;
	}

	BuildTimeline(Subsystem->GetMatchCues(), Subsystem->GetRecordingDurationSeconds());
}

void UVideoMatchPlayerWidget::BuildTimeline(const TArray<FMatchInputCue>& InCues, float InTotalDurationSeconds)
{
	ClearTimeline();

	Cues = InCues;
	TotalDurationSeconds = InTotalDurationSeconds;

	// The recording's duration is the authority for horizontal scale, but a hand-built cue list may not
	// come with one. Falling back to the last cue's timestamp keeps the track usable; without this the
	// division below would collapse every marker onto the left edge.
	if (TotalDurationSeconds <= KINDA_SMALL_NUMBER && Cues.Num() > 0)
	{
		TotalDurationSeconds = Cues.Last().TimeSeconds;
	}

	if (!TimelineCanvas || Cues.Num() == 0 || TotalDurationSeconds <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	CueMarkers.Reserve(Cues.Num());

	for (int32 Index = 0; Index < Cues.Num(); ++Index)
	{
		UWidget* Marker = SpawnCueMarker(Index, Cues[Index]);
		CueMarkers.Add(Marker);

		if (Marker)
		{
			OnCueMarkerCreated(Marker, Index, Cues[Index]);
		}
	}

	// A session may already be in progress, so seed both the marker states and the prompt from where the
	// player actually is rather than from cue zero.
	const UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	const int32 ActiveCueIndex = (Subsystem && Subsystem->IsMatchingInput())
		? Subsystem->GetCurrentMatchCueIndex()
		: INDEX_NONE;

	LastAppliedCueIndex = INDEX_NONE;
	RefreshMarkerStates(ActiveCueIndex);
	ShowExpectedInput(ActiveCueIndex);

	UE_LOG(LogInputMatch, Log, TEXT("%s: built a %d-cue timeline over %.2fs."),
		*GetName(), Cues.Num(), TotalDurationSeconds);
}

UWidget* UVideoMatchPlayerWidget::SpawnCueMarker(int32 CueIndex, const FMatchInputCue& Cue)
{
	const FSlateBrush Icon = IconMapping
		? IconMapping->GetIconForCue(Cue)
		: FSlateBrush();

	UWidget* Marker = nullptr;

	if (CueMarkerClass)
	{
		UMatchCueMarkerWidget* MarkerWidget = CreateWidget<UMatchCueMarkerWidget>(this, CueMarkerClass);
		if (MarkerWidget)
		{
			MarkerWidget->InitialiseMarker(CueIndex, Cue, Icon);
			Marker = MarkerWidget;
		}
	}
	else
	{
		// No marker class: a bare image is enough to make the track work, and it means the timeline is
		// functional with nothing but this C++ class and an icon mapping asset.
		UImage* Image = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		if (Image)
		{
			Image->SetBrush(Icon);
			Marker = Image;
		}
	}

	if (!Marker)
	{
		return nullptr;
	}

	UCanvasPanelSlot* CanvasSlot = TimelineCanvas->AddChildToCanvas(Marker);
	if (!CanvasSlot)
	{
		return Marker;
	}

	const float Fraction = FMath::Clamp(Cue.TimeSeconds / TotalDurationSeconds, 0.0f, 1.0f);

	// Anchors are normalised to the canvas, so a marker placed at 0.42 stays at 42% of the bar's width
	// through every resize and at every resolution - no pixel arithmetic, no re-layout on window change.
	// This is the reason TimelineCanvas has to span exactly the same rectangle as TimelineBar.
	CanvasSlot->SetAnchors(FAnchors(Fraction, 0.5f, Fraction, 0.5f));

	// Centre the icon on its anchor rather than hanging it off to the right of the timestamp.
	CanvasSlot->SetAlignment(FVector2D(0.5, 0.5));

	CanvasSlot->SetAutoSize(false);

	// A brush that carries its own image size wins - that is the designer being explicit about how big
	// this particular icon should be.
	const FVector2D BrushSize(Icon.ImageSize.X, Icon.ImageSize.Y);
	CanvasSlot->SetSize(BrushSize.IsNearlyZero() ? CueMarkerSize : BrushSize);

	CanvasSlot->SetPosition(CueMarkerOffset);
	CanvasSlot->SetZOrder(1);

	return Marker;
}

void UVideoMatchPlayerWidget::ClearTimeline()
{
	for (UWidget* Marker : CueMarkers)
	{
		if (Marker)
		{
			Marker->RemoveFromParent();
		}
	}

	CueMarkers.Reset();
	Cues.Reset();
	TotalDurationSeconds = 0.0f;
	LastAppliedCueIndex = INDEX_NONE;
}

// ---------------------------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------------------------

void UVideoMatchPlayerWidget::RefreshDisplay()
{
	UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	if (!Subsystem)
	{
		return;
	}

	const bool bMatching = Subsystem->IsMatchingInput();
	const float ClockSeconds = Subsystem->GetMatchClockSeconds();

	if (TimelineBar)
	{
		// Position along the *recorded* timeline, not the fraction of cues completed: that is what the
		// markers are anchored against, so the fill and the icons agree.
		const float Percent = (TotalDurationSeconds > KINDA_SMALL_NUMBER)
			? FMath::Clamp(ClockSeconds / TotalDurationSeconds, 0.0f, 1.0f)
			: 0.0f;

		TimelineBar->SetPercent(Percent);
	}

	if (TimeLabel)
	{
		const int32 CurrentSeconds = FMath::FloorToInt(ClockSeconds);
		const int32 TotalSeconds = FMath::FloorToInt(TotalDurationSeconds);

		TimeLabel->SetText(FText::FromString(FString::Printf(TEXT("%d:%02d / %d:%02d"),
			CurrentSeconds / 60, CurrentSeconds % 60,
			TotalSeconds / 60, TotalSeconds % 60)));
	}

	if (WaitingIndicator)
	{
		WaitingIndicator->SetVisibility(Subsystem->IsAwaitingMatchInput()
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Collapsed);
	}

	const int32 ActiveCueIndex = bMatching ? Subsystem->GetCurrentMatchCueIndex() : INDEX_NONE;
	if (ActiveCueIndex != LastAppliedCueIndex)
	{
		RefreshMarkerStates(ActiveCueIndex);
		ShowExpectedInput(ActiveCueIndex);
	}
}

void UVideoMatchPlayerWidget::RefreshMarkerStates(int32 ActiveCueIndex)
{
	LastAppliedCueIndex = ActiveCueIndex;

	for (int32 Index = 0; Index < CueMarkers.Num(); ++Index)
	{
		UWidget* Marker = CueMarkers[Index];
		if (!Marker)
		{
			continue;
		}

		EMatchCueMarkerState State = EMatchCueMarkerState::Pending;
		if (ActiveCueIndex != INDEX_NONE)
		{
			if (Index < ActiveCueIndex)      { State = EMatchCueMarkerState::Completed; }
			else if (Index == ActiveCueIndex) { State = EMatchCueMarkerState::Active; }
		}

		if (UMatchCueMarkerWidget* MarkerWidget = Cast<UMatchCueMarkerWidget>(Marker))
		{
			MarkerWidget->SetMarkerState(State);
			continue;
		}

		// Plain-UImage fallback: same three-state read-out, just without the Blueprint hook.
		if (UImage* Image = Cast<UImage>(Marker))
		{
			switch (State)
			{
			case EMatchCueMarkerState::Active:
				Image->SetColorAndOpacity(FLinearColor::White);
				break;
			case EMatchCueMarkerState::Completed:
				Image->SetColorAndOpacity(FLinearColor(0.35f, 0.85f, 0.4f, 0.8f));
				break;
			default:
				Image->SetColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.35f));
				break;
			}
		}
	}
}

void UVideoMatchPlayerWidget::ShowExpectedInput(int32 CueIndex)
{
	if (!Cues.IsValidIndex(CueIndex))
	{
		if (ExpectedInputIcon)  { ExpectedInputIcon->SetVisibility(ESlateVisibility::Hidden); }
		if (ExpectedInputLabel) { ExpectedInputLabel->SetText(FText::GetEmpty()); }
		return;
	}

	const FMatchInputCue& Cue = Cues[CueIndex];

	if (ExpectedInputIcon)
	{
		if (IconMapping)
		{
			ExpectedInputIcon->SetBrush(IconMapping->GetIconForCue(Cue));
		}
		ExpectedInputIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	if (ExpectedInputLabel)
	{
		// The mapping's display name when there is one ("Jump"), the cue's own formatted description
		// otherwise ("IA_Move [Fwd | X=+0.00 Y=+1.00]") - which is ugly but always says something true.
		ExpectedInputLabel->SetText(IconMapping
			? IconMapping->GetDisplayNameForCue(Cue)
			: FText::FromString(Cue.Description));
	}
}

// ---------------------------------------------------------------------------------------------
// Subsystem event relays
// ---------------------------------------------------------------------------------------------

void UVideoMatchPlayerWidget::HandleModeChanged(EInputReplayMode NewMode)
{
	if (NewMode == EInputReplayMode::MatchInput)
	{
		// A new session means a new cue list and, usually, a new video texture to point at.
		BuildTimelineFromSubsystem();
		BindVideoSurface();
	}
}

void UVideoMatchPlayerWidget::HandleCuePresented(int32 CueIndex, int32 TotalCues, const FString& ExpectedInput)
{
	RefreshMarkerStates(CueIndex);
	ShowExpectedInput(CueIndex);

	OnCuePresented(CueIndex, TotalCues, ExpectedInput);
}

void UVideoMatchPlayerWidget::HandleCueMatched(int32 CueIndex, int32 TotalCues)
{
	RefreshMarkerStates(CueIndex + 1);

	// Look ahead to the cue now counting down rather than leaving a completed one on screen. The
	// authoritative update still happens in HandleCuePresented; this just stops the prompt going stale
	// during the interval.
	ShowExpectedInput(CueIndex + 1);

	OnCueMatched(CueIndex, TotalCues);
}

void UVideoMatchPlayerWidget::HandleMismatch(const FString& ExpectedInput, const FString& ActualInput)
{
	OnCueMismatched(ExpectedInput, ActualInput);
}

void UVideoMatchPlayerWidget::HandleMatchFinished(bool bCompletedAllCues)
{
	RefreshMarkerStates(bCompletedAllCues ? Cues.Num() : INDEX_NONE);
	ShowExpectedInput(INDEX_NONE);

	OnSessionFinished(bCompletedAllCues);
}

void UVideoMatchPlayerWidget::HandleVideoOpened(bool bSuccess, const FString& VideoPath)
{
	if (bSuccess)
	{
		BindVideoSurface();
	}

	OnVideoReady(bSuccess, VideoPath);
}
