// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Store/RecordingSessionTypes.h"
#include "ControlRecapPlayerController.generated.h"

class UControlRecapWidget;
class UInputReplayComponent;

/**
 * Player controller for the standalone review map.
 *
 * This level is a video player with an input listener, and that framing decides everything here:
 * it reads every input the player produces and blocks none of it.
 *
 * Create BP_ControlRecapPlayerController as a child of this and set it on the game mode Blueprint.
 */
UCLASS(Blueprintable)
class INPUTRECORDER_API AControlRecapPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AControlRecapPlayerController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Forwarded to the replay component so sampling reads post-modifier values in the same frame. */
	virtual void PreProcessInput(const float DeltaTime, const bool bGamePaused) override;
	virtual void PostProcessInput(const float DeltaTime, const bool bGamePaused) override;

	/** Resolves a destination and travels: game mode's target, then the settings' gameplay map, then the engine default. */
	UFUNCTION(BlueprintCallable, Category = "Control Recap")
	void LeaveRecap();

	UFUNCTION(BlueprintPure, Category = "Control Recap")
	UControlRecapWidget* GetRecapWidget() const { return RecapWidget; }

	UFUNCTION(BlueprintImplementableEvent, Category = "Control Recap", meta = (DisplayName = "On Recap Session Resolved"))
	void K2_OnRecapSessionResolved(const FRecordingSessionInfo& Session, bool bFound);

protected:
	/**
	 * 1. -IR=1 forces the most recently updated playable session, skipping everything below.
	 * 2. -ControlRecap=<name> names one specifically. Accepts Recording_5 or a bare 5.
	 * 3. The game mode's ForcedSessionFolder, if the level pins one.
	 * 4. The most recently updated playable session.
	 */
	bool ResolveSessionToReview(FRecordingSessionInfo& OutSession) const;

	void SetUpReviewInputMode();

	/**
	 * Rebuilds the input stack this level has to listen to.
	 *
	 * @param Session the take about to be reviewed, or null when none resolved. Its header names
	 *        the contexts that were applied when the input was captured, which is the only source
	 *        that is right in a project the plugin has never been configured for. Project
	 *        settings are the fallback, for a take recorded before headers carried this.
	 */
	void PushGameplayMappingContexts(const FRecordingSessionInfo* Session);

private:
	UFUNCTION()
	void HandleRecapClosed(bool bCompletedAllCues);

	UPROPERTY(Transient)
	TObjectPtr<UControlRecapWidget> RecapWidget;

	UPROPERTY(Transient)
	TWeakObjectPtr<UInputReplayComponent> ReplayComponent;

	/** Kept so the previous config can be restored when this level is left. */
	TSharedPtr<class FNavigationConfig> PreviousNavigationConfig;
};
