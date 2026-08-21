// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "RecordingPlayerController.generated.h"

class UInputReplayComponent;

/**
 * Optional gameplay PlayerController base that forwards the input hooks to the replay component.
 *
 * Reparent an existing controller Blueprint to this to get exact same-frame sampling. It is not
 * required: UInputReplayComponent falls back to a tick with a prerequisite on the controller,
 * which lands in the same place in the frame. This class exists because forwarding is the
 * clearer expression of the intent when a project is free to choose its own base.
 */
UCLASS(Blueprintable)
class INPUTRECORDER_API ARecordingPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void PreProcessInput(const float DeltaTime, const bool bGamePaused) override;
	virtual void PostProcessInput(const float DeltaTime, const bool bGamePaused) override;

protected:
	/** Re-resolved rather than cached across possessions - the component dies with its owner. */
	UInputReplayComponent* ResolveReplayComponent();

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<UInputReplayComponent> CachedReplayComponent;
};
