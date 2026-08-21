// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "InputIconMapping.generated.h"

class UTexture2D;

/** One action's icon, keyed by the short asset name so it survives the asset being moved. */
USTRUCT(BlueprintType)
struct INPUTRECORDER_API FInputIconEntry
{
	GENERATED_BODY()

	/** Short action name, e.g. IA_Jump. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Icons")
	FName ActionName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Icons")
	TSoftObjectPtr<UTexture2D> Icon;

	/** Shown when there is no sprite, and as the accessible label beside one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Icons")
	FText Label;
};

/** Action name to sprite. Missing entries are not an error - the UI falls back to text. */
UCLASS(BlueprintType)
class INPUTRECORDER_API UInputIconMapping : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input Icons")
	TArray<FInputIconEntry> Entries;

	UFUNCTION(BlueprintCallable, Category = "Input Icons")
	bool FindEntry(FName ActionName, FInputIconEntry& OutEntry) const;

	/** Null when the action has no mapping or its texture will not load. */
	UFUNCTION(BlueprintCallable, Category = "Input Icons")
	UTexture2D* FindIcon(FName ActionName) const;
};
