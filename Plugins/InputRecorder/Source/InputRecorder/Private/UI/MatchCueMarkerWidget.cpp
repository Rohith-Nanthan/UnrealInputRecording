// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/MatchCueMarkerWidget.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Settings/InputIconMapping.h"
#include "Settings/InputRecordingSettings.h"

void UMatchCueMarkerWidget::CollectMissingBindings(TArray<FString>& OutMissing) const
{
	if (!DotImage)
	{
		OutMissing.Add(TEXT("DotImage"));
	}
}

void UMatchCueMarkerWidget::SetCue(const FMatchInputCue& InCue, int32 InCueIndex)
{
	Cue = InCue;
	CueIndex = InCueIndex;

	if (LabelText)
	{
		LabelText->SetText(FText::FromString(Cue.ActionName));
	}

	if (IconImage)
	{
		UTexture2D* Icon = nullptr;

		if (const UInputRecordingSettings* Settings = UInputRecordingSettings::Get())
		{
			if (const UInputIconMapping* Mapping = Settings->IconMapping.LoadSynchronous())
			{
				Icon = Mapping->FindIcon(FName(*Cue.ActionName));
			}
		}

		// A missing icon is normal, not an error - the label carries the meaning on its own.
		IconImage->SetBrushFromTexture(Icon);
		IconImage->SetVisibility(Icon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	K2_OnCueSet(Cue, CueIndex);

	// Fired unconditionally rather than through SetMarkerState: a recycled marker is already
	// Pending, and the early-out in SetMarkerState would leave it wearing its old completed
	// styling with no event to correct it.
	MarkerState = EMatchCueMarkerState::Pending;
	K2_OnMarkerStateChanged(MarkerState);
}

void UMatchCueMarkerWidget::SetMarkerState(EMatchCueMarkerState NewState)
{
	if (MarkerState == NewState)
	{
		return;
	}

	MarkerState = NewState;
	K2_OnMarkerStateChanged(MarkerState);
}
