// Copyright (c) Your Studio. All Rights Reserved.
//
// RecordingUIInputConfig.h
//
// One asset holding the Enhanced Input setup for both recording UIs.
//
// WHY THIS IS SEPARATE FROM UMG NAVIGATION
//   These two layers are routinely confused and the confusion produces UI that looks controller-ready
//   and is completely dead on a pad:
//
//     * Moving focus between buttons is Slate's job. It comes from FNavigationConfig and the
//       UWidgetNavigation links on each widget, and it works whether or not Enhanced Input exists.
//     * Semantic actions - accept, back, scrub the timeline, toggle recording - are Enhanced Input's
//       job, and they are what this asset configures.
//
//   Binding "navigate up" as an Enhanced Input action and calling SetFocus by hand is the usual
//   mistake. It fights Slate's own navigation, breaks the moment a widget is added, and never quite
//   matches mouse focus behaviour. Let Slate navigate; use this for verbs.
//
//   ApplyTo() sets up both halves, which is the only reason it knows about navigation at all.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "RecordingUIInputConfig.generated.h"

class APlayerController;
class UInputAction;
class UInputMappingContext;

UCLASS(BlueprintType, meta = (DisplayName = "Recording UI Input Config"))
class UNREALINPUTRECORDING_API URecordingUIInputConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Context pushed while either recording UI is up.
	 *
	 * Keep this narrow - it should map the actions below and nothing else. It is pushed at a high
	 * priority so it wins over gameplay bindings for shared keys, which is exactly what you want when
	 * a full-screen review UI is on top, and harmless for the small overlay because the actions do not
	 * overlap gameplay ones.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI Input")
	TSoftObjectPtr<UInputMappingContext> MappingContext;

	/**
	 * Priority for the pushed context. Higher wins.
	 *
	 * Above any gameplay context, because a UI that cannot take Back while the pawn can still jump is
	 * worse than a pawn that briefly cannot jump.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI Input")
	int32 ContextPriority = 100;

	//~ Actions -------------------------------------------------------------------------------------

	/** Confirm the focused control. Gamepad face button bottom, Enter, Space. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI Input|Actions")
	TSoftObjectPtr<UInputAction> Accept;

	/** Cancel, close, go back. Gamepad face button right, Escape. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI Input|Actions")
	TSoftObjectPtr<UInputAction> Back;

	/** Start or stop recording without reaching for the mouse. Bound on the overlay only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI Input|Actions")
	TSoftObjectPtr<UInputAction> ToggleRecord;

	/** Stop and jump straight to the control recap map. Bound on the overlay only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI Input|Actions")
	TSoftObjectPtr<UInputAction> Test;

	//~ API -----------------------------------------------------------------------------------------

	/**
	 * Pushes the mapping context and turns on analog-stick UI navigation.
	 *
	 * @param bAllowAnalogNavigation  Let the left stick move focus as well as the d-pad. On for the
	 *                                full-screen recap, off for the overlay, where a stick that moves
	 *                                focus while the player is still driving the game is a nuisance.
	 */
	void ApplyTo(APlayerController* PlayerController, bool bAllowAnalogNavigation) const;

	/** Removes the mapping context and restores the previous navigation config. Balanced with ApplyTo. */
	void RemoveFrom(APlayerController* PlayerController) const;

	/** Loads and returns the context, or null. Logs once if the soft reference will not resolve. */
	UInputMappingContext* LoadMappingContext() const;
};
