// Copyright (c) Your Studio. All Rights Reserved.
//
// MatchCueMarkerWidget.h
//
// One sync point on the video timeline, drawn as a full-height column so a single widget carries both
// layers the design calls for at the same X position:
//
//     [ icon ]      <- above the bar: which input is expected here
//     [ "next" ]    <- shown only on the active cue
//     (   fill  )
//     [  dot  ]     <- on the bar: state at a glance (passed / next / upcoming)
//
// The column is anchored to the full height of the timeline canvas by UMatchVideoPlayerWidget, so the
// icon rides at the top and the dot lands on the progress bar at the bottom - they can never drift
// apart because they are the same widget on the same anchor.
//
// The whole tree is built in C++ (RebuildWidget); a Blueprint subclass customises the look through the
// Style properties, not by rebuilding the tree.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "InputReplay/InputMatchCue.h"
#include "Styling/SlateBrush.h"

#include "MatchCueMarkerWidget.generated.h"

class UImage;
class UTextBlock;
class UVerticalBox;

/** Where a cue sits relative to the player's progress. Drives both the icon tint and the dot colour. */
UENUM(BlueprintType)
enum class EMatchCueMarkerState : uint8
{
	Pending		UMETA(DisplayName = "Pending"),
	Active		UMETA(DisplayName = "Active"),
	Completed	UMETA(DisplayName = "Completed")
};

UCLASS(Blueprintable, meta = (DisplayName = "Match Cue Marker Widget"))
class UNREALINPUTRECORDING_API UMatchCueMarkerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UMatchCueMarkerWidget(const FObjectInitializer& ObjectInitializer);

	/** Called once by the timeline right after the marker is added to the canvas. */
	void InitialiseMarker(int32 InCueIndex, const FMatchInputCue& InCue, const FSlateBrush& InIcon);

	/** Cross-fade the marker between the three states. Cheap; safe to call every frame. */
	UFUNCTION(BlueprintCallable, Category = "Match Cue Marker")
	void SetMarkerState(EMatchCueMarkerState NewState);

	UFUNCTION(BlueprintPure, Category = "Match Cue Marker")
	EMatchCueMarkerState GetMarkerState() const { return MarkerState; }

	/** Fires after the icon and cue data are populated, for extra Blueprint styling. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Match Cue Marker")
	void OnMarkerInitialised();

	//~ Read-only cue data -----------------------------------------------------------------------

	UPROPERTY(BlueprintReadOnly, Category = "Match Cue Marker")
	int32 CueIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Match Cue Marker")
	FMatchInputCue Cue;

	//~ Style hooks - safe to override in a Blueprint subclass ------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match Cue Marker|Style")
	FLinearColor IconPendingTint = FLinearColor(1.f, 1.f, 1.f, 0.35f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match Cue Marker|Style")
	FLinearColor IconActiveTint = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match Cue Marker|Style")
	FLinearColor IconCompletedTint = FLinearColor(0.35f, 0.85f, 0.4f, 0.85f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match Cue Marker|Style")
	FLinearColor DotPendingColor = FLinearColor(0.42f, 0.46f, 0.5f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match Cue Marker|Style")
	FLinearColor DotActiveColor = FLinearColor(0.5f, 0.7f, 1.f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match Cue Marker|Style")
	FLinearColor DotCompletedColor = FLinearColor(0.79f, 0.82f, 0.87f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match Cue Marker|Style")
	FVector2D IconSize = FVector2D(28.0, 28.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match Cue Marker|Style")
	FVector2D DotSize = FVector2D(12.0, 12.0);

	/** Grown slightly on the active cue so the "next" dot reads as the biggest one on the bar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match Cue Marker|Style")
	float ActiveDotScale = 1.3f;

protected:
	//~ Begin UUserWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	//~ End UUserWidget interface

	/** Constructs the column into WidgetTree. Runs once, guarded on RootColumn. */
	void BuildTree();

	/** Pushes MarkerState onto the icon tint, the dot colour/size, and the "next" label. */
	void ApplyState();

	UPROPERTY(Transient) TObjectPtr<UVerticalBox> RootColumn;
	UPROPERTY(Transient) TObjectPtr<UImage> IconImage;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NextLabel;
	UPROPERTY(Transient) TObjectPtr<UImage> DotImage;

	/** The dot's shape - a rounded box rounded to a circle. Tinted per state at runtime. */
	FSlateBrush DotBrush;

	EMatchCueMarkerState MarkerState = EMatchCueMarkerState::Pending;
};
