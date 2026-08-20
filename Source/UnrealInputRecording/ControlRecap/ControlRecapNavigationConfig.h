// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/NavigationConfig.h"

/**
 * Slate navigation config for the review map, with analog-stick navigation turned on.
 *
 * Moving focus between widgets is Slate's job, through FNavigationConfig and UWidgetNavigation,
 * and it works with zero Enhanced Input involvement. Stock Slate only responds to the d-pad and
 * the arrow keys, so a full-screen quiz that ignores the stick reads as broken on a pad.
 *
 * The semantic verbs - Accept, Back, toggle record - are the other system entirely: those are
 * Enhanced Input actions configured through DA_RecordingUIInput. Do not bind "navigate up/down"
 * as an input action and call SetFocus by hand; it fights Slate's own navigation, breaks the
 * instant a widget is added or reordered, and never matches how mouse focus already behaves.
 *
 * Deliberately scoped to this map. A stick that also moves UI focus during normal gameplay is a
 * nuisance rather than a feature, so the corner overlay never installs this.
 */
class FControlRecapNavigationConfig : public FNavigationConfig
{
public:
	FControlRecapNavigationConfig()
	{
		bTabNavigation = true;
		bKeyNavigation = true;
		bAnalogNavigation = true;

		// A stick has to travel most of the way before it counts as a deliberate move; otherwise
		// resting drift walks the focus around on its own.
		AnalogNavigationHorizontalThreshold = 0.55f;
		AnalogNavigationVerticalThreshold = 0.55f;
	}
};
