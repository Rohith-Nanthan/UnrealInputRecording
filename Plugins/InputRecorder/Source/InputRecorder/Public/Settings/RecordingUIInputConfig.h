// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RecordingUIInputConfig.generated.h"

class UInputAction;
class UInputMappingContext;

/**
 * Semantic UI verbs - Accept, Back, toggle record - and the context they live in.
 *
 * These are Enhanced Input's job. Moving focus between widgets is NOT: that is Slate's, through
 * FNavigationConfig and UWidgetNavigation. Binding "navigate up/down" as an input action and
 * calling SetFocus by hand fights Slate's own navigation, breaks the instant a widget is added
 * or reordered, and never matches how mouse focus already behaves.
 */
UCLASS(BlueprintType)
class INPUTRECORDER_API URecordingUIInputConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Dedicated context, pushed above gameplay so a UI verb wins over a gameplay binding on the same key. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recording UI Input")
	TSoftObjectPtr<UInputMappingContext> UIMappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recording UI Input")
	int32 PushPriority = 100;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recording UI Input")
	TSoftObjectPtr<UInputAction> AcceptAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recording UI Input")
	TSoftObjectPtr<UInputAction> BackAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recording UI Input")
	TSoftObjectPtr<UInputAction> ToggleRecordAction;
};
