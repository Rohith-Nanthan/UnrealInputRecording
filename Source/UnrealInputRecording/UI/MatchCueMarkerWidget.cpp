// Copyright (c) Your Studio. All Rights Reserved.

#include "UI/MatchCueMarkerWidget.h"

#include "Components/Image.h"

void UMatchCueMarkerWidget::InitialiseMarker(int32 InCueIndex, const FMatchInputCue& InCue, const FSlateBrush& InIcon)
{
	CueIndex = InCueIndex;
	Cue = InCue;

	if (IconImage)
	{
		IconImage->SetBrush(InIcon);
	}

	// Populate before notifying, so a Blueprint override reads finished data.
	SetMarkerState(EMatchCueMarkerState::Pending);
	OnMarkerInitialised();
}

void UMatchCueMarkerWidget::SetMarkerState_Implementation(EMatchCueMarkerState NewState)
{
	MarkerState = NewState;

	if (!IconImage)
	{
		return;
	}

	switch (NewState)
	{
	case EMatchCueMarkerState::Active:
		IconImage->SetColorAndOpacity(ActiveTint);
		break;

	case EMatchCueMarkerState::Completed:
		IconImage->SetColorAndOpacity(CompletedTint);
		break;

	case EMatchCueMarkerState::Pending:
	default:
		IconImage->SetColorAndOpacity(PendingTint);
		break;
	}
}
