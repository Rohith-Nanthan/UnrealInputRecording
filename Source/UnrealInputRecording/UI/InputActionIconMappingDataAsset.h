// Copyright (c) Your Studio. All Rights Reserved.
//
// InputActionIconMappingDataAsset.h
//
// "Which picture means IA_Jump?" - answered once, in an asset, instead of in a switch statement in
// every widget that needs to draw a prompt.
//
// WHY LOOKUP IS BY *TWO* KEYS
//
// A FMatchInputCue carries both a TSoftObjectPtr<UInputAction> and a short ActionName string, because
// a recording stores soft paths and those paths can go stale - rename or move IA_Jump and every
// recording made before the move still refers to the old path. The soft path is the precise key; the
// short name is the one that survives a reorganisation. GetIconForCue tries the path first and falls
// back to the name, so old recordings keep showing the right icon.
//
// Icons are FSlateBrush rather than UTexture2D. A brush costs nothing extra to author (drop a texture
// in and you are done), but it also lets an entry point at a material, set a draw-as mode, or carry
// its own tint and image size - all of which you will want the first time a designer asks for a
// nine-sliced key-cap background instead of a flat icon.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Styling/SlateBrush.h"
#include "UObject/SoftObjectPtr.h"

#include "InputActionIconMappingDataAsset.generated.h"

class UInputAction;
struct FMatchInputCue;

/** One action's presentation. */
USTRUCT(BlueprintType)
struct FInputActionIconEntry
{
	GENERATED_BODY()

	/**
	 * The action this entry describes. Soft, so a mapping asset referencing fifty actions does not
	 * drag fifty UInputAction assets into memory just to draw one icon.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icon")
	TSoftObjectPtr<UInputAction> Action;

	/**
	 * Short asset name ("IA_Jump"), used as the fallback key. Filled in automatically from Action when
	 * you set it in the editor; set it by hand for actions that do not exist as assets yet.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icon")
	FName ActionName;

	/** Drop a texture into the brush's Image field and set its Image Size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icon")
	FSlateBrush Icon;

	/** Player-facing label, e.g. "Jump" for IA_Jump. Falls back to ActionName when empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icon")
	FText DisplayName;
};

/**
 * Create one at Content Browser > Miscellaneous > Data Asset > Input Action Icon Mapping, add an entry
 * per action, and assign it on UVideoMatchPlayerWidget.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Input Action Icon Mapping"))
class UNREALINPUTRECORDING_API UInputActionIconMappingDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons")
	TArray<FInputActionIconEntry> Entries;

	/** Drawn for any action with no entry, so an unmapped action shows a placeholder, not a hole. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons")
	FSlateBrush DefaultIcon;

	/** Size the UI uses for timeline markers when a brush does not specify its own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons")
	FVector2D DefaultIconSize = FVector2D(48.0, 48.0);

	// -----------------------------------------------------------------------------------------
	// Lookup
	// -----------------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Input Recording|Icons")
	FSlateBrush GetIconForAction(const UInputAction* Action) const;

	UFUNCTION(BlueprintPure, Category = "Input Recording|Icons")
	FSlateBrush GetIconForActionName(FName ActionName) const;

	UFUNCTION(BlueprintPure, Category = "Input Recording|Icons")
	FSlateBrush GetIconForSoftAction(const TSoftObjectPtr<UInputAction>& Action) const;

	UFUNCTION(BlueprintPure, Category = "Input Recording|Icons")
	FText GetDisplayNameForActionName(FName ActionName) const;

	UFUNCTION(BlueprintPure, Category = "Input Recording|Icons")
	bool HasEntryForActionName(FName ActionName) const;

	/**
	 * The one the UI actually calls: soft path first, short name second, DefaultIcon last.
	 * C++ only because FMatchInputCue is not a Blueprint-exposed parameter type on this asset.
	 */
	FSlateBrush GetIconForCue(const FMatchInputCue& Cue) const;

	FText GetDisplayNameForCue(const FMatchInputCue& Cue) const;

	/** Discards the lookup caches. Call after editing Entries at runtime. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Icons")
	void InvalidateLookup();

#if WITH_EDITOR
	//~ Begin UObject interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject interface
#endif

private:
	/** Index of the entry matching this key, or INDEX_NONE. Builds the caches on first use. */
	int32 FindEntryIndexByPath(const FSoftObjectPath& Path) const;
	int32 FindEntryIndexByName(FName ActionName) const;

	void BuildLookupIfNeeded() const;

	/**
	 * mutable because lookup happens from BlueprintPure const functions and building the map on demand
	 * beats forcing every caller to remember to prime it.
	 */
	mutable TMap<FSoftObjectPath, int32> PathToEntry;
	mutable TMap<FName, int32> NameToEntry;
	mutable bool bLookupBuilt = false;
};
