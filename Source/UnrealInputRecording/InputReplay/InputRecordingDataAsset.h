// Copyright (c) Your Studio. All Rights Reserved.
//
// InputRecordingDataAsset.h
//
// An editor-inspectable mirror of a saved recording.
//
// A .ghost file is a compact binary blob and a .ghost.json is a wall of numbers; neither is pleasant
// to read when you are trying to work out why a replay diverged at second 12. This asset holds the
// same data in three views:
//
//   Timeline        - one readable row per recorded sample: time, action name, trigger event, value.
//   Match Input Cues- the discrete presses an interactive MatchInput session will ask for.
//   Raw Data        - the verbatim FRecordedInputFrame array, for byte-level debugging.
//
// The asset is a *cache*, not the source of truth: SourceFileName points at the file on disk, and
// Reimport From Source File re-parses it. It can also be handed straight back to the runtime -
// UInputRecordingSubsystem::StartMatchInputModeFromAsset feeds BuildRecording() into the component.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "InputMatchCue.h"
#include "InputReplayTypes.h"
#include "UObject/SoftObjectPtr.h"

#include "InputRecordingDataAsset.generated.h"

class UInputAction;

/**
 * One recorded sample, flattened for the details panel. Every field is read-only by design: edit the
 * recording on disk and reimport, never hand-edit the cache.
 */
USTRUCT(BlueprintType)
struct FInputRecordingTimelineEntry
{
	GENERATED_BODY()

	/** Elapsed seconds since the recording started. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Timeline")
	float TimeSeconds = 0.0f;

	/** Logical tick this sample belongs to. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Timeline")
	int32 FrameIndex = 0;

	/** Short asset name, e.g. "IA_Jump". */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Timeline")
	FString ActionName;

	/** Clickable reference to the actual Input Action asset. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Timeline")
	TSoftObjectPtr<UInputAction> Action;

	/** "Started" / "Triggered" / "Completed" ... */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Timeline")
	FString TriggerEvent;

	/** "Boolean" / "Axis1D" / "Axis2D" / "Axis3D". */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Timeline")
	FString ValueType;

	/** The recorded value, widened to double precision for display. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Timeline")
	FVector Value = FVector::ZeroVector;

	/** True for actions stored as per-frame deltas (mouse) rather than held rates. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Timeline")
	bool bIsFrameDelta = false;

	/** Pre-formatted single-line summary: "IA_Move [Fwd | X=+0.00 Y=+1.00]". */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Timeline")
	FString Description;
};

/** Per-action rollup, so you can see at a glance which actions a recording actually exercises. */
USTRUCT(BlueprintType)
struct FInputRecordingActionSummary
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Summary")
	FString ActionName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Summary")
	TSoftObjectPtr<UInputAction> Action;

	/** Index into FInputRecordingHeader::ActionPaths - the id used by every recorded sample. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Summary")
	int32 ActionIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Summary")
	int32 SampleCount = 0;

	/** Number of MatchInput cues this action produced. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Summary")
	int32 CueCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Summary")
	bool bIsFrameDelta = false;

	/** False when the soft path in the recording no longer resolves to an asset. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Summary")
	bool bResolved = false;
};

/**
 * Editor-visible snapshot of one input recording.
 *
 * Create it either with UInputRecordingAssetTools::GenerateRecordingDataAsset (one call, appears in
 * the Content Browser) or by hand: Content Browser > Miscellaneous > Data Asset >
 * InputRecordingDataAsset, then set Source File Name and press Reimport From Source File.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Input Recording Data Asset"))
class UNREALINPUTRECORDING_API UInputRecordingDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	//~ Source ----------------------------------------------------------------------------------

	/** Bare name ("Lap01"), a name with extension, or an absolute path. Resolved by the serializer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "01 - Source")
	FString SourceFileName;

	/** Read the .ghost.json instead of the binary .ghost. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "01 - Source")
	bool bSourceIsJson = false;

	/** Full path the last import actually read. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "01 - Source")
	FString ResolvedSourcePath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "01 - Source")
	FDateTime ImportedAtUtc = FDateTime(0);

	/** Empty on success, otherwise the reason the last import failed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "01 - Source")
	FString LastImportError;

	//~ Summary ---------------------------------------------------------------------------------

	/** Recording metadata verbatim: id, level, engine version, tick rate, seed, action registry. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "02 - Summary")
	FInputRecordingHeader Header;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "02 - Summary")
	float DurationSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "02 - Summary")
	int32 TotalLogicalFrames = 0;

	/** Number of delta-compressed samples, i.e. Timeline.Num(). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "02 - Summary")
	int32 SampleCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "02 - Summary")
	int32 SyncPointCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "02 - Summary")
	TArray<FInputRecordingActionSummary> ActionSummaries;

	//~ Timeline --------------------------------------------------------------------------------

	/** The readable view. One row per recorded sample, in time order. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "03 - Timeline")
	TArray<FInputRecordingTimelineEntry> Timeline;

	//~ MatchInput ------------------------------------------------------------------------------

	/**
	 * Cue extraction rules. Changing any of these re-derives MatchInputCues immediately - no
	 * reimport needed - so you can tune the press threshold and watch the cue list respond.
	 *
	 * Keep in sync with UInputReplayComponent::MatchCueOptions or the preview will not match
	 * what the player is asked to press.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "04 - Match Input")
	FMatchInputCueBuildOptions CueOptions;

	/** Exactly what an interactive MatchInput session will ask the player to reproduce, in order. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "04 - Match Input")
	TArray<FMatchInputCue> MatchInputCues;

	//~ Raw data --------------------------------------------------------------------------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, AdvancedDisplay, Category = "05 - Raw Data")
	TArray<FRecordedInputFrame> RawFrames;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, AdvancedDisplay, Category = "05 - Raw Data")
	TArray<FReplaySyncPoint> SyncPoints;

	/** Only populated for recordings captured in RecordedDeltas time mode. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, AdvancedDisplay, Category = "05 - Raw Data")
	TArray<float> FrameDeltaSeconds;

	//~ API -------------------------------------------------------------------------------------

	/** Parse SourceFileName from disk and rebuild every view. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Input Recording", meta = (DisplayName = "Reimport From Source File"))
	void ReimportFromSourceFile();

	/**
	 * Re-derive MatchInputCues from the data already cached in this asset. Cheap - no disk access.
	 * Called automatically when CueOptions change in the editor.
	 */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Input Recording", meta = (DisplayName = "Rebuild Match Input Cues"))
	void RebuildMatchInputCues();

	/**
	 * Write this asset's data back out as readable JSON next to the other recordings. Handy for
	 * diffing two takes of the same sequence.
	 */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Input Recording", meta = (DisplayName = "Export To JSON"))
	void ExportToJson();

	/** Parse an arbitrary file into this asset, updating SourceFileName to match. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording")
	bool ImportFromFile(const FString& FileName, bool bJson);

	/** Populate every view from an in-memory recording (used by the runtime and by the importer). */
	void PopulateFromRecording(const FInputRecording& InRecording);

	/**
	 * Rebuild a runtime FInputRecording from the cached data, so a Data Asset can be played back or
	 * used as a MatchInput sequence without touching the disk again.
	 */
	FInputRecording BuildRecording() const;

	/** Blueprint-facing wrapper for BuildRecording. */
	UFUNCTION(BlueprintPure, Category = "Input Recording", meta = (DisplayName = "Get Recording"))
	FInputRecording GetRecording() const { return BuildRecording(); }

	UFUNCTION(BlueprintPure, Category = "Input Recording")
	bool HasValidRecording() const { return Header.TotalFrames > 0 && Header.ActionPaths.Num() > 0; }

#if WITH_EDITOR
	//~ Begin UObject interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject interface
#endif
};
