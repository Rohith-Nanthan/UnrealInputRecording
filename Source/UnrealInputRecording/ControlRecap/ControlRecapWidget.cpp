// Copyright (c) Your Studio. All Rights Reserved.

#include "ControlRecap/ControlRecapWidget.h"

#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "InputRecordingSettings.h"
#include "InputRecordingSubsystem.h"
#include "MediaTexture.h"
#include "Storage/RecordingStore.h"
#include "UI/InputActionIconMappingDataAsset.h"
#include "UI/MatchCueMarkerWidget.h"
#include "Video/InputRecordingVideoPlayer.h"

// ---------------------------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------------------------

void UControlRecapWidget::NativeConstruct()
{
	Super::NativeConstruct();

	ValidateBindings();

	if (CancelButton && !CancelButton->OnClicked.IsAlreadyBound(this, &UControlRecapWidget::HandleCancelClicked))
	{
		CancelButton->OnClicked.AddDynamic(this, &UControlRecapWidget::HandleCancelClicked);
	}

	// Font sizes are pushed from C++ so the size relationship the design depends on survives however
	// the Blueprint was authored. Colours too - a red that is not actually red defeats the point of
	// the mismatch line.
	if (bOverrideFontSizes)
	{
		if (ExpectedInputText)
		{
			FSlateFontInfo Font = ExpectedInputText->GetFont();
			Font.Size = ExpectedInputFontSize;
			ExpectedInputText->SetFont(Font);
			ExpectedInputText->SetColorAndOpacity(FSlateColor(ExpectedInputColor));
		}

		if (WrongInputText)
		{
			FSlateFontInfo Font = WrongInputText->GetFont();
			Font.Size = WrongInputFontSize;
			WrongInputText->SetFont(Font);
			WrongInputText->SetColorAndOpacity(FSlateColor(WrongInputColor));
		}

		if (CueCounterText)
		{
			FSlateFontInfo Font = CueCounterText->GetFont();
			Font.Size = CueCounterFontSize;
			CueCounterText->SetFont(Font);
		}
	}

	if (UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem())
	{
		Subsystem->OnMatchCuePresented.AddDynamic(this, &UControlRecapWidget::HandleMatchCuePresented);
		Subsystem->OnMatchInputMatched.AddDynamic(this, &UControlRecapWidget::HandleMatchInputMatched);
		Subsystem->OnMatchInputMismatch.AddDynamic(this, &UControlRecapWidget::HandleMatchInputMismatch);
		Subsystem->OnMatchInputFinished.AddDynamic(this, &UControlRecapWidget::HandleMatchInputFinished);

		// The media texture object is stable across opens, but binding on the event covers the case
		// where this widget is constructed before the player exists.
		if (UInputRecordingVideoPlayer* VideoPlayer = Subsystem->GetVideoPlayer())
		{
			VideoPlayer->OnVideoOpened.AddUniqueDynamic(this, &UControlRecapWidget::HandleVideoOpened);
		}
	}

	RefreshVideoBinding();

	HidePrompt();
	ClearWrongInput();

	// Focus starts on Cancel so a pad has somewhere to be from the first frame. Without this the
	// screen accepts navigation input but has nothing focused, which reads as unresponsive.
	if (CancelButton)
	{
		CancelButton->SetKeyboardFocus();
	}

	OnRecapConstructed();
}

void UControlRecapWidget::NativeDestruct()
{
	if (UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem())
	{
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

void UControlRecapWidget::ValidateBindings() const
{
	TArray<FString> Missing;

	auto Check = [&Missing](const UObject* Bound, const TCHAR* Name)
	{
		if (!Bound)
		{
			Missing.Add(Name);
		}
	};

	Check(VideoImage,         TEXT("VideoImage (Image)"));
	Check(VideoSizeBox,       TEXT("VideoSizeBox (Size Box)"));
	Check(CueCounterText,     TEXT("CueCounterText (Text)"));
	Check(ProgressBar,        TEXT("ProgressBar (Progress Bar)"));
	Check(MarkerCanvas,       TEXT("MarkerCanvas (Canvas Panel)"));
	Check(ExpectedInputText,  TEXT("ExpectedInputText (Text)"));
	Check(WrongInputText,     TEXT("WrongInputText (Text)"));
	Check(CancelButton,       TEXT("CancelButton (Button)"));

	if (Missing.Num() == 0)
	{
		return;
	}

	// One message listing everything, rather than the one-at-a-time compile errors strict BindWidget
	// would give. Warning rather than Error: a partly-built layout is a normal state to be in while
	// designing, and every one of these is null-checked before use.
	UE_LOG(LogRecordingStore, Warning,
		TEXT("%s is missing %d UMG binding(s): %s. Add widgets with these exact names to the ")
		TEXT("Blueprint's tree - see the header for the expected hierarchy."),
		*GetClass()->GetName(), Missing.Num(), *FString::Join(Missing, TEXT(", ")));
}

// ---------------------------------------------------------------------------------------------
// Icons
// ---------------------------------------------------------------------------------------------

UInputActionIconMappingDataAsset* UControlRecapWidget::ResolveIconMapping()
{
	if (ResolvedIconMapping)
	{
		return ResolvedIconMapping;
	}

	// A per-widget override wins, but the project setting is what makes icons work at all for a widget
	// nobody hand-configured - which is exactly this one, since the recap map creates it from a class
	// rather than from a placed instance. That gap is why icons were silently missing here.
	if (IconMapping)
	{
		ResolvedIconMapping = IconMapping;
	}
	else if (const UInputRecordingSettings* Settings = UInputRecordingSettings::Get())
	{
		ResolvedIconMapping = Settings->LoadIconMapping();
	}

	if (!ResolvedIconMapping)
	{
		UE_LOG(LogRecordingStore, Warning,
			TEXT("No input action icon mapping is available. Set one on this widget, or at ")
			TEXT("Project Settings > Game > Input Recording > UI > Input Action Icon Mapping. ")
			TEXT("Cue markers and the expected-input prompt will draw without sprites."));
	}

	return ResolvedIconMapping;
}

// ---------------------------------------------------------------------------------------------
// Video
// ---------------------------------------------------------------------------------------------

void UControlRecapWidget::RefreshVideoBinding()
{
	if (!VideoImage)
	{
		return;
	}

	UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	UInputRecordingVideoPlayer* VideoPlayer = Subsystem ? Subsystem->GetVideoPlayer() : nullptr;
	UMediaTexture* Texture = VideoPlayer ? VideoPlayer->GetMediaTexture() : nullptr;

	if (!Texture)
	{
		return;
	}

	// SetBrushResourceObject, not SetBrushFromTexture: a UMediaTexture is a UTexture but not a
	// UTexture2D, so the typed setter would refuse it.
	//
	// Assigned straight to the brush rather than through a material: the recap screen wants the picture
	// as captured, and a material here would be one more thing to keep in sync with the encoder's
	// orientation setting.
	VideoImage->SetBrushResourceObject(Texture);
}

void UControlRecapWidget::HandleVideoOpened(bool bSuccess, const FString& VideoPath)
{
	if (bSuccess)
	{
		RefreshVideoBinding();
	}
}

// ---------------------------------------------------------------------------------------------
// Review lifecycle
// ---------------------------------------------------------------------------------------------

void UControlRecapWidget::BeginReview(const FRecordingSessionInfo& Session)
{
	ReviewedSession = Session;

	if (SessionLabel)
	{
		SessionLabel->SetText(FText::FromString(Session.FolderName));
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

	RefreshVideoBinding();
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
	ClearWrongInput();

	if (VideoImage)
	{
		VideoImage->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (LegendPanel)
	{
		LegendPanel->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (CueCounterText)
	{
		CueCounterText->SetText(FText::FromString(TEXT("no recording")));
	}
}

void UControlRecapWidget::BuildTimeline()
{
	UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	if (!Subsystem)
	{
		return;
	}

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

	if (!MarkerCanvas)
	{
		// The rest of the screen still works; only the marker rail is missing.
		return;
	}

	MarkerCanvas->ClearChildren();
	Markers.Reset();

	TSubclassOf<UMatchCueMarkerWidget> MarkerClass = CueMarkerClass;
	if (!MarkerClass) { MarkerClass = UMatchCueMarkerWidget::StaticClass(); }

	UInputActionIconMappingDataAsset* Icons = ResolveIconMapping();

	Markers.Reserve(Cues.Num());
	for (int32 Index = 0; Index < Cues.Num(); ++Index)
	{
		UMatchCueMarkerWidget* Marker = CreateWidget<UMatchCueMarkerWidget>(this, MarkerClass);
		if (!Marker)
		{
			Markers.Add(nullptr);
			continue;
		}

		const FSlateBrush Icon = Icons ? Icons->GetIconForCue(Cues[Index]) : FSlateBrush();
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
			MarkerSlot->SetSize(FVector2D(MarkerColumnWidth, MarkerTrackHeight));
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

	if (VideoSizeBox)
	{
		const float AvailableHeight = MyGeometry.GetLocalSize().Y;
		if (AvailableHeight > 0.f)
		{
			VideoSizeBox->SetHeightOverride(AvailableHeight * VideoScreenFraction);
		}
	}

	if (WrongInputTimer > 0.f)
	{
		WrongInputTimer -= InDeltaTime;
		if (WrongInputTimer <= 0.f)
		{
			ClearWrongInput();
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

	// Driven by the awaiting flag rather than by the cue-presented event, because the clock also
	// freezes on a mismatch and the prompt has to stay up through that.
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
	if (!Cues.IsValidIndex(CueIndex))
	{
		return;
	}

	UInputActionIconMappingDataAsset* Icons = ResolveIconMapping();

	if (ExpectedInputText)
	{
		// The mapping's display name when it has one ("Jump"), the raw action name otherwise
		// ("IA_Jump"). Player-facing text should not leak asset naming conventions.
		ExpectedInputText->SetText(Icons
			? Icons->GetDisplayNameForCue(Cues[CueIndex])
			: FText::FromString(Cues[CueIndex].ActionName));
	}

	if (ExpectedInputIcon)
	{
		if (Icons)
		{
			ExpectedInputIcon->SetBrush(Icons->GetIconForCue(Cues[CueIndex]));
			ExpectedInputIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			ExpectedInputIcon->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	if (PromptPanel)
	{
		PromptPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UControlRecapWidget::HidePrompt()
{
	if (PromptPanel)
	{
		PromptPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
	else if (ExpectedInputText)
	{
		// No wrapping panel bound - hide the pieces individually so the layout does not keep a stale
		// prompt on screen between cues.
		ExpectedInputText->SetVisibility(ESlateVisibility::Collapsed);

		if (ExpectedInputIcon)
		{
			ExpectedInputIcon->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void UControlRecapWidget::ClearWrongInput()
{
	WrongInputTimer = 0.f;

	if (WrongInputText)
	{
		WrongInputText->SetText(FText::GetEmpty());
		WrongInputText->SetVisibility(ESlateVisibility::Collapsed);
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

	if (ExpectedInputText)
	{
		ExpectedInputText->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	ClearWrongInput();
	ShowPrompt(CueIndex);
}

void UControlRecapWidget::HandleMatchInputMatched(int32 CueIndex, int32 TotalCues)
{
	HidePrompt();

	// Clear on success rather than waiting the timer out: leaving the last mistake up next to a cue
	// the player just got right reads as though they got that one wrong too.
	ClearWrongInput();

	RefreshMarkerStates(CueIndex + 1);
	LastAppliedCueIndex = CueIndex + 1;
}

void UControlRecapWidget::HandleMatchInputMismatch(const FString& ExpectedInput, const FString& ActualInput)
{
	if (WrongInputText)
	{
		const FString Shown = ActualInput.IsEmpty() ? TEXT("something else") : ActualInput;

		WrongInputText->SetText(FText::FromString(FString::Printf(TEXT("you pressed %s"), *Shown)));
		WrongInputText->SetColorAndOpacity(FSlateColor(WrongInputColor));
		WrongInputText->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	WrongInputTimer = WrongInputDisplaySeconds;

	OnWrongInput(ExpectedInput, ActualInput);
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
