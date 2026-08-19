// Copyright (c) Your Studio. All Rights Reserved.
//
// ControlRecapPlayerController.h
//
// Drives ControlRecapLevel: picks the session, raises the recap widget, and locks everything the
// player could otherwise do.
//
// INPUT LOCKOUT, AND WHY IT IS NOT UI-ONLY
//   The obvious move is FInputModeUIOnly, and it is wrong here. MatchInput reads live Enhanced Input
//   action values to decide whether the player pressed the right thing, so gameplay input has to keep
//   being evaluated - UI-only stops it reaching the input stack at all and every cue would hang
//   forever. Instead the pawn is frozen with SetIgnoreMoveInput / SetIgnoreLookInput, which stops
//   movement while leaving action evaluation intact, and the input mode stays Game-and-UI.
//
// SESSION RESOLUTION
//   1. -IR=1, which forces the most recently updated session and skips everything below it
//   2. -ControlRecap=<folder> from the command line
//   3. AControlRecapGameMode::ForcedSessionFolder, if the level pins one
//   4. the most recently updated playable session
//   Nothing found means the widget comes up with an explanatory message rather than a black screen.
//
//   The command line outranks the level's own pin on purpose: the pin is a design-time choice baked
//   into a map, and someone typing a flag into a terminal is deliberately overriding it for this run.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Storage/RecordingSessionTypes.h"

#include "ControlRecapPlayerController.generated.h"

class UControlRecapWidget;
class UInputReplayComponent;
class URecordingUIInputConfig;

UCLASS(Blueprintable, meta = (DisplayName = "Control Recap Player Controller"))
class UNREALINPUTRECORDING_API AControlRecapPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AControlRecapPlayerController();

	/** Widget class raised on possession. A Blueprint subclass here is the styling hook. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control Recap")
	TSoftClassPtr<UControlRecapWidget> RecapWidgetClass;

	/**
	 * Leave the recap map for another one.
	 *
	 * Cancel calls this. Resolution order is the game mode's TargetOnCancelMap, then the project's
	 * Gameplay Map setting, then the engine default map - so it always goes somewhere.
	 */
	UFUNCTION(BlueprintCallable, Category = "Control Recap")
	void LeaveRecap();

	/** The session this level is reviewing. Invalid when the store had nothing playable. */
	UFUNCTION(BlueprintPure, Category = "Control Recap")
	const FRecordingSessionInfo& GetReviewedSession() const { return ReviewedSession; }

	//~ Begin APlayerController interface
	//
	// Forwarded to the replay component for the same reason AReplayPlayerController forwards them:
	// PreProcessInput runs before Enhanced Input evaluates the stack and PostProcessInput after, so
	// injection lands in the same frame and sampling reads post-modifier values. Without these the
	// component falls back to TickComponent, where every cue is judged against input that is one
	// frame stale - and judging input is the only thing this map does.
	//
	// The component guards itself against being stepped twice in a frame, so the fallback path
	// simply stops firing once these are in place.
	virtual void PreProcessInput(const float DeltaTime, const bool bGamePaused) override;
	virtual void PostProcessInput(const float DeltaTime, const bool bGamePaused) override;
	//~ End APlayerController interface

protected:
	//~ Begin AActor interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End AActor interface

	/**
	 * The replay component the subsystem put on this controller, resolved lazily.
	 *
	 * Lazily, because the subsystem auto-creates it when the widget first asks for a recording -
	 * which is after BeginPlay - so caching it any earlier would cache a null.
	 */
	UInputReplayComponent* ResolveReplayComponent();

	/** Applies the pawn freeze and the Game-and-UI input mode. See the header note on lockout. */
	void ApplyRecapInputLock();

	/** Runs the resolution order in the header comment. */
	bool ResolveSessionToReview(FRecordingSessionInfo& OutSession) const;

private:
	UFUNCTION() void HandleRecapClosed(bool bCompletedAllCues);

	URecordingUIInputConfig* LoadUIInputConfig() const;

	UPROPERTY(Transient)
	TObjectPtr<UControlRecapWidget> RecapWidget;

	UPROPERTY(Transient)
	TObjectPtr<URecordingUIInputConfig> UIInputConfig;

	UPROPERTY(Transient)
	TObjectPtr<UInputReplayComponent> CachedReplayComponent;

	FRecordingSessionInfo ReviewedSession;
};
