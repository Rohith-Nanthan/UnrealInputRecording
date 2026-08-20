// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InputRecordingWidgetBase.generated.h"

class UInputRecordingSubsystem;

/**
 * Shared base for every surface in this system.
 *
 * Builds no widget tree of its own - no RebuildWidget, no WidgetTree->ConstructWidget. C++ holds
 * logic only: subsystem bindings, per-frame refresh, timeline maths. Every visual element is a
 * BindWidgetOptional property filled in by a Blueprint child.
 *
 * Bindings are optional, never strict. Strict BindWidget fails Blueprint *compilation* the
 * instant one name does not match, one problem at a time, so a partially built Blueprint cannot
 * be saved and a tree cannot be rearranged mid-design. ValidateBindings is the safety net
 * instead: it reports every missing hook at once, in one message.
 */
UCLASS(Abstract, Blueprintable, BlueprintType)
class UNREALINPUTRECORDING_API UInputRecordingWidgetBase : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Never null in practice, but always null-checked - a widget can outlive a game instance teardown. */
	UFUNCTION(BlueprintPure, Category = "Input Recording|UI")
	UInputRecordingSubsystem* GetRecordingSubsystem() const;

	/**
	 * Logs every missing binding in a single message rather than one per frame or one per
	 * compile. Called once on construct.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|UI")
	void ValidateBindings();

protected:
	virtual void NativeOnInitialized() override;

	/** Fill OutMissing with the name of every optional binding this widget actually needs. */
	virtual void CollectMissingBindings(TArray<FString>& OutMissing) const {}

	/** Blueprint hook for anything a designer wants to do once the bindings are known good. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Input Recording|UI", meta = (DisplayName = "On Bindings Validated"))
	void K2_OnBindingsValidated(bool bAllPresent);
};
