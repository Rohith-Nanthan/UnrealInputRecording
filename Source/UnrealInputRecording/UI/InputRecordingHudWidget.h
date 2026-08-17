// Copyright (c) Your Studio. All Rights Reserved.
//
// InputRecordingHudWidget.h
//
// The test-level HUD: Record / Stop / Match Input, plus a status read-out.
//
// This class is Abstract because a C++ UUserWidget has no visual tree of its own. Create a Widget
// Blueprint whose parent class is InputRecordingHudWidget, then add widgets named exactly:
//
//     RecordButton      (Button)        StatusText         (Text Block)
//     StopButton        (Button)        ExpectedInputText  (Text Block)
//     MatchInputButton  (Button)        CountdownText      (Text Block)
//                                       MismatchText       (Text Block)
//                                       ProgressBar        (Progress Bar)
//
// Every binding is optional, so a widget with only the three buttons compiles and works; anything
// present is driven automatically. The names are what BindWidgetOptional matches on - a typo means
// the widget is silently not driven, so check the log line NativeConstruct emits.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "InputReplay/InputReplayTypes.h"

#include "InputRecordingHudWidget.generated.h"

class UButton;
class UInputRecordingSubsystem;
class UProgressBar;
class UTextBlock;

UCLASS(Abstract, meta = (DisplayName = "Input Recording HUD Widget"))
class UNREALINPUTRECORDING_API UInputRecordingHudWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	//~ Configuration ---------------------------------------------------------------------------

	/** Recording name used by Record / Stop / Match Input. Empty uses the project default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording")
	FString RecordingFileName;

	/** Label shown in the recording's header and in logs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording")
	FString RecordingDisplayName = TEXT("Tutorial Take");

	/** Read/write the JSON format instead of the binary one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording")
	bool bUseJsonFormat = false;

	/**
	 * Show the mouse cursor and switch to Game-and-UI input while this widget is up, so the buttons
	 * are clickable.
	 *
	 * Game-and-UI specifically, never UI-only: MatchInput has to keep receiving gameplay input, and
	 * UI-only mode would stop every action from evaluating - the player could never satisfy a cue.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording")
	bool bManagePlayerInputMode = true;

	/** How long a wrong-input message stays on screen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Recording", meta = (ClampMin = "0.0"))
	float MismatchDisplaySeconds = 3.0f;

	//~ Button handlers - also callable from Blueprint if you wire your own buttons up -----------

	UFUNCTION(BlueprintCallable, Category = "Input Recording")
	void HandleRecordClicked();

	UFUNCTION(BlueprintCallable, Category = "Input Recording")
	void HandleStopClicked();

	UFUNCTION(BlueprintCallable, Category = "Input Recording")
	void HandleMatchInputClicked();

	//~ Designer hooks --------------------------------------------------------------------------

	/** A new cue is due and the system is now waiting for ExpectedInput. Play your prompt here. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Input Recording")
	void NotifyCuePresented(const FString& ExpectedInput, int32 CueIndex, int32 TotalCues);

	/** The player got it right. Play the success flash here. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Input Recording")
	void NotifyCueMatched(int32 CueIndex, int32 TotalCues);

	/** The player pressed the wrong thing. Play the error flash / shake here. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Input Recording")
	void NotifyMismatch(const FString& ExpectedInput, const FString& ActualInput);

	/** The sequence ended. bCompletedAllCues is false when it was stopped early. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Input Recording")
	void NotifyMatchInputFinished(bool bCompletedAllCues);

protected:
	//~ Begin UUserWidget interface
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	//~ End UUserWidget interface

	UFUNCTION(BlueprintPure, Category = "Input Recording")
	UInputRecordingSubsystem* GetRecordingSubsystem() const;

	/** Push the current state into the text blocks and the progress bar. */
	void RefreshDisplay();

	/** Enable/disable the buttons for the current mode. */
	void UpdateButtonStates();

	//~ Bound widgets ---------------------------------------------------------------------------

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Input Recording|Widgets")
	TObjectPtr<UButton> RecordButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Input Recording|Widgets")
	TObjectPtr<UButton> StopButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Input Recording|Widgets")
	TObjectPtr<UButton> MatchInputButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Input Recording|Widgets")
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Input Recording|Widgets")
	TObjectPtr<UTextBlock> ExpectedInputText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Input Recording|Widgets")
	TObjectPtr<UTextBlock> CountdownText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Input Recording|Widgets")
	TObjectPtr<UTextBlock> MismatchText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Input Recording|Widgets")
	TObjectPtr<UProgressBar> ProgressBar;

private:
	//~ Subsystem event relays ------------------------------------------------------------------

	UFUNCTION() void OnModeChanged(EInputReplayMode NewMode);
	UFUNCTION() void OnCuePresented(int32 CueIndex, int32 TotalCues, const FString& ExpectedInput);
	UFUNCTION() void OnCueMatched(int32 CueIndex, int32 TotalCues);
	UFUNCTION() void OnMismatch(const FString& ExpectedInput, const FString& ActualInput);
	UFUNCTION() void OnMatchFinished(bool bCompletedAllCues);

	/** Seconds of MismatchDisplaySeconds still to run. */
	float MismatchMessageTimer = 0.0f;

	/** Throttles the per-frame text rebuild; still fast enough for a smooth countdown. */
	float RefreshTimer = 0.0f;

	/** We changed the player's input mode and owe them a restore in NativeDestruct. */
	bool bPushedInputMode = false;
};
