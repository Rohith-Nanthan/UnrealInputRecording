// Copyright (c) Your Studio. All Rights Reserved.
//
// MatchCueMarkerWidget.h
//
// One icon on the timeline track. Optional: UVideoMatchPlayerWidget spawns a plain UImage when no
// marker class is set, so you get a working timeline with zero extra Blueprints.
//
// It exists for the moment you want more than an icon - a key-cap frame, a hit flash, a "you are here"
// pulse - because all of that is animation and layout, which belongs in a Widget Blueprint, not in C++.
// Derive a WBP from this, add an Image named IconImage, and the base class fills it in for you.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "InputReplay/InputMatchCue.h"
#include "Styling/SlateBrush.h"

#include "MatchCueMarkerWidget.generated.h"

class UImage;

/** Where a cue sits relative to the player's progress through the sequence. */
UENUM(BlueprintType)
enum class EMatchCueMarkerState : uint8
{
	/** Not reached yet. */
	Pending		UMETA(DisplayName = "Pending"),

	/** This is the cue the system is currently waiting on. */
	Active		UMETA(DisplayName = "Active"),

	/** Already matched. */
	Completed	UMETA(DisplayName = "Completed")
};

UCLASS(Abstract, meta = (DisplayName = "Match Cue Marker Widget"))
class UNREALINPUTRECORDING_API UMatchCueMarkerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Called once by the timeline widget right after this marker is added to the canvas. */
	void InitialiseMarker(int32 InCueIndex, const FMatchInputCue& InCue, const FSlateBrush& InIcon);

	/** Set up your own visuals here - the icon and cue data are already populated when this fires. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Match Input")
	void OnMarkerInitialised();

	/**
	 * Drives the marker's appearance as the player advances.
	 *
	 * The native implementation tints IconImage: dim for pending, full white for active, half-faded for
	 * completed. Override in Blueprint (call the parent or not, your choice) to animate instead.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Match Input")
	void SetMarkerState(EMatchCueMarkerState NewState);
	virtual void SetMarkerState_Implementation(EMatchCueMarkerState NewState);

	UFUNCTION(BlueprintPure, Category = "Match Input")
	EMatchCueMarkerState GetMarkerState() const { return MarkerState; }

	/** Position of this cue in the sequence. Matches UInputRecordingSubsystem::GetCurrentMatchCueIndex. */
	UPROPERTY(BlueprintReadOnly, Category = "Match Input")
	int32 CueIndex = INDEX_NONE;

	/** The cue itself: timestamp, action name, expected value, pre-formatted description. */
	UPROPERTY(BlueprintReadOnly, Category = "Match Input")
	FMatchInputCue Cue;

	/** Tints used by the default SetMarkerState implementation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match Input|Appearance")
	FLinearColor PendingTint = FLinearColor(1.0f, 1.0f, 1.0f, 0.35f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match Input|Appearance")
	FLinearColor ActiveTint = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match Input|Appearance")
	FLinearColor CompletedTint = FLinearColor(0.35f, 0.85f, 0.4f, 0.8f);

protected:
	/** Add a UImage named exactly "IconImage" to your WBP and the cue's icon lands in it. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Match Input|Widgets")
	TObjectPtr<UImage> IconImage;

	EMatchCueMarkerState MarkerState = EMatchCueMarkerState::Pending;
};
