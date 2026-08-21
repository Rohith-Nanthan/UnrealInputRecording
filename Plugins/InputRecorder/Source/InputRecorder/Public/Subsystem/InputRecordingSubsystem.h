// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "InputReplay/InputReplayComponent.h"
#include "Store/RecordingSessionTypes.h"
#include "InputRecordingSubsystem.generated.h"

class UInputRecorderOverlayWidget;
class UInputRecordingVideoCapture;
class UInputRecordingVideoPlayer;
class URecordingListWidget;
class URecordingStore;
class URecordingToastWidget;
class UUserWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnRecordingSaved, bool, bSuccess, const FString&, SessionPath, bool, bQuotaStopped);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRecordingVideoSaved, bool, bSuccess, const FString&, VideoPath);

/**
 * The one object that owns everything: replay component lookup, video capture, video playback,
 * the session store and every widget.
 *
 * Widgets and Blueprints bind to *this*, never directly to the UInputReplayComponent. That
 * component lives on a controller or pawn and dies on every respawn and level travel; anything
 * holding a raw pointer to it holds a dangling pointer within one map change. The subsystem
 * outlives all of it and re-resolves the component lazily.
 */
UCLASS(BlueprintType)
class INPUTRECORDER_API UInputRecordingSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// FTickableGameObject. One tick for everything - the video sync and the quota poll both ride
	// on it rather than each adding a ticker of their own.
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableInEditor() const override { return false; }
	virtual TStatId GetStatId() const override;

	// --- Events. Relayed from the component so Blueprints only ever bind here. ---------------

	UPROPERTY(BlueprintAssignable, Category = "Input Recording|Events")
	FOnInputReplayModeChanged OnModeChanged;

	UPROPERTY(BlueprintAssignable, Category = "Input Recording|Events")
	FOnInputSampleRecorded OnSyncPointRecorded;

	UPROPERTY(BlueprintAssignable, Category = "Input Recording|Events")
	FOnRecordingVideoSaved OnVideoSaved;

	UPROPERTY(BlueprintAssignable, Category = "Input Recording|Events")
	FOnRecordingSaved OnRecordingSaved;

	UPROPERTY(BlueprintAssignable, Category = "Input Recording|Events")
	FOnMatchCuePresented OnMatchCuePresented;

	UPROPERTY(BlueprintAssignable, Category = "Input Recording|Events")
	FOnMatchInputMatched OnMatchInputMatched;

	UPROPERTY(BlueprintAssignable, Category = "Input Recording|Events")
	FOnMatchInputMismatched OnMatchInputMismatched;

	UPROPERTY(BlueprintAssignable, Category = "Input Recording|Events")
	FOnMatchInputFinished OnMatchInputFinished;

	// --- Component resolution -----------------------------------------------------------------

	/**
	 * Cached, then the local PlayerController, then its pawn, then any actor in the world
	 * carrying one, then auto-created on the PlayerController when settings allow it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input Recording")
	UInputReplayComponent* ResolveReplayComponent();

	// --- Recording control --------------------------------------------------------------------

	/** Empty name falls back to the project setting's default. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording")
	bool StartRecording(const FString& DisplayName);

	/** Stops without writing anything. The session folder and in-memory take both survive. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording")
	void StopRecordingWithoutSaving();

	UFUNCTION(BlueprintCallable, Category = "Input Recording")
	bool StopRecordingAndSave(const FString& DisplayName, bool bAlsoExportJson);

	/** Convenience wrapper that takes its arguments from project settings. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording")
	bool StopRecording();

	/** Abandons the take and deletes its folder - distinct from stopping without saving. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording")
	void CancelRecording();

	// --- Review -------------------------------------------------------------------------------

	/**
	 * Stops and saves any take in progress, then travels to the review map.
	 *
	 * @param SessionSpecifier empty for the most recent playable session, or a folder name, a
	 *        bare index, or a display name. A named session that does not exist is an error that
	 *        names what was asked for and lists what is available - never a silent fallback to
	 *        the most recent, which would review a different take than the one requested.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input Recording")
	bool RunControlRecapTest(const FString& SessionSpecifier);

	UFUNCTION(BlueprintCallable, Category = "Input Recording")
	bool StartMatchInputFromSession(const FRecordingSessionInfo& Session);

	UFUNCTION(BlueprintCallable, Category = "Input Recording")
	void StopMatchInput(bool bCompleted);

	/** What the review map should open, set by RunControlRecapTest before travelling. */
	UFUNCTION(BlueprintPure, Category = "Input Recording")
	FString GetPendingReviewSpecifier() const { return PendingReviewSpecifier; }

	UFUNCTION(BlueprintCallable, Category = "Input Recording")
	void ClearPendingReviewSpecifier() { PendingReviewSpecifier.Reset(); }

	// --- Owned objects -------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Input Recording")
	URecordingStore* GetStore() const { return Store; }

	UFUNCTION(BlueprintPure, Category = "Input Recording")
	UInputRecordingVideoPlayer* GetVideoPlayer() const { return VideoPlayer; }

	UFUNCTION(BlueprintPure, Category = "Input Recording")
	UInputRecordingVideoCapture* GetVideoCapture() const { return VideoCapture; }

	// --- Widgets --------------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Input Recording|UI")
	UInputRecorderOverlayWidget* ShowOverlay();

	UFUNCTION(BlueprintCallable, Category = "Input Recording|UI")
	void HideOverlay();

	UFUNCTION(BlueprintPure, Category = "Input Recording|UI")
	bool IsOverlayVisible() const;

	UFUNCTION(BlueprintCallable, Category = "Input Recording|UI")
	URecordingListWidget* ShowRecordingList();

	UFUNCTION(BlueprintCallable, Category = "Input Recording|UI")
	void HideRecordingList();

	UFUNCTION(BlueprintCallable, Category = "Input Recording|UI")
	void ShowToast(const FText& Message, const FText& Detail);

	/**
	 * Creates a widget from a project-settings class path, falling back to the C++ class so the
	 * caller never gets null. Shared with the review map's PlayerController, which creates its
	 * own full-screen surface.
	 */
	UUserWidget* CreateWidgetFromSettingsClass(const FSoftClassPath& Path, UClass* FallbackClass, const TCHAR* SettingName);

	// --- State ----------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Input Recording")
	EInputReplayMode GetMode() const;

	UFUNCTION(BlueprintPure, Category = "Input Recording")
	bool IsRecording() const { return GetMode() == EInputReplayMode::Recording; }

	UFUNCTION(BlueprintPure, Category = "Input Recording")
	bool IsMatchingInput() const { return GetMode() == EInputReplayMode::MatchingInput; }

	UFUNCTION(BlueprintPure, Category = "Input Recording")
	bool IsIdle() const { return GetMode() == EInputReplayMode::Idle; }

	UFUNCTION(BlueprintPure, Category = "Input Recording")
	bool IsAwaitingMatchInput() const;

	UFUNCTION(BlueprintPure, Category = "Input Recording")
	FString GetExpectedInputDescription() const;

	/** 0..1 across the cue list. */
	UFUNCTION(BlueprintPure, Category = "Input Recording")
	float GetMatchProgress() const;

	UFUNCTION(BlueprintPure, Category = "Input Recording")
	int32 GetMatchCueCount() const;

	UFUNCTION(BlueprintPure, Category = "Input Recording")
	int32 GetCurrentMatchCueIndex() const;

	UFUNCTION(BlueprintPure, Category = "Input Recording")
	float GetMatchClockSeconds() const;

	UFUNCTION(BlueprintPure, Category = "Input Recording")
	TArray<FMatchInputCue> GetMatchCues() const;

	UFUNCTION(BlueprintPure, Category = "Input Recording")
	int32 GetMismatchCount() const;

	UFUNCTION(BlueprintPure, Category = "Input Recording")
	FString GetLastMismatchDescription() const;

	UFUNCTION(BlueprintPure, Category = "Input Recording")
	float GetRecordingDurationSeconds() const;

	/** Duration of the take currently under review, for the timeline's denominator. */
	UFUNCTION(BlueprintPure, Category = "Input Recording")
	float GetReviewedRecordingDurationSeconds() const;

	UFUNCTION(BlueprintPure, Category = "Input Recording")
	FString GetStatusText() const;

	UFUNCTION(BlueprintPure, Category = "Input Recording")
	bool GetLiveInputSnapshot(FString& OutActionName, FVector& OutValue) const;

	/** True when the last take was cut short by the storage quota rather than stopped normally. */
	UFUNCTION(BlueprintPure, Category = "Input Recording")
	bool WasLastRecordingQuotaStopped() const { return bLastRecordingQuotaStopped; }

private:
	void BindComponentEvents(UInputReplayComponent* Component);
	void UnbindComponentEvents(UInputReplayComponent* Component);

	UFUNCTION()
	void HandleComponentModeChanged(EInputReplayMode NewMode);

	UFUNCTION()
	void HandleComponentSampleRecorded(FName ActionName, float TimeSeconds, FVector Value);

	UFUNCTION()
	void HandleComponentCuePresented(int32 CueIndex, int32 TotalCues, const FString& ExpectedDescription);

	UFUNCTION()
	void HandleComponentCueMatched(int32 CueIndex, int32 TotalCues);

	UFUNCTION()
	void HandleComponentCueMismatched(const FString& ExpectedDescription, const FString& ReceivedDescription);

	UFUNCTION()
	void HandleComponentMatchFinished(bool bCompletedAllCues);

	APlayerController* GetLocalPlayerController() const;

	/** Rescues a take when the world goes away underneath it - app exit, or travel mid-recording. */
	void HandleWorldBeginTearDown(UWorld* World);

public:
	/**
	 * Saves a take that is still running, if there is one. Idempotent, so the several teardown
	 * paths that call it cannot double-save.
	 *
	 * Public because UInputReplayComponent::EndPlay calls it: EndPlay is the one notification
	 * guaranteed to arrive while the component still holds its samples, so it is the backstop
	 * for every teardown route that does not broadcast a world event.
	 */
	void SaveInProgressTake(const TCHAR* Reason);

private:

	FDelegateHandle WorldTearDownHandle;

	/** Stops video, finalises the take on disk and commits or abandons the session folder. */
	bool FinishRecording(const FString& DisplayName, bool bAlsoExportJson, bool bSave, bool bDeleteFolder);

	/** Once a second while recording only. Stops the take rather than evicting anything. */
	void PollQuotaWhileRecording();

	UPROPERTY(Transient)
	TObjectPtr<URecordingStore> Store;

	UPROPERTY(Transient)
	TObjectPtr<UInputRecordingVideoCapture> VideoCapture;

	UPROPERTY(Transient)
	TObjectPtr<UInputRecordingVideoPlayer> VideoPlayer;

	UPROPERTY(Transient)
	TWeakObjectPtr<UInputReplayComponent> CachedComponent;

	UPROPERTY(Transient)
	TObjectPtr<UInputRecorderOverlayWidget> OverlayWidget;

	UPROPERTY(Transient)
	TObjectPtr<URecordingListWidget> RecordingListWidget;

	UPROPERTY(Transient)
	TObjectPtr<URecordingToastWidget> ToastWidget;

	UPROPERTY(Transient)
	FRecordingSessionInfo ActiveSession;

	UPROPERTY(Transient)
	FRecordingSessionInfo ReviewedSession;

	FString PendingReviewSpecifier;

	float QuotaPollAccumulator = 0.0f;
	bool bLastRecordingQuotaStopped = false;
	bool bOverlayHiddenForCapture = false;
};
