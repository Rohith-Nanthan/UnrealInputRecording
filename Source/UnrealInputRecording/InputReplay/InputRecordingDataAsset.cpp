// Copyright (c) Your Studio. All Rights Reserved.

#include "InputRecordingDataAsset.h"

#include "InputAction.h"
#include "InputMatchCue.h"
#include "InputReplaySerializer.h"
#include "Misc/DateTime.h"
#include "UObject/UnrealType.h"

void UInputRecordingDataAsset::ReimportFromSourceFile()
{
	if (SourceFileName.IsEmpty())
	{
		LastImportError = TEXT("Source File Name is empty. Set it to a recording name such as 'Lap01'.");
		UE_LOG(LogInputMatch, Error, TEXT("%s: %s"), *GetName(), *LastImportError);
		return;
	}

	ImportFromFile(SourceFileName, bSourceIsJson);
}

bool UInputRecordingDataAsset::ImportFromFile(const FString& FileName, bool bJson)
{
	SourceFileName     = FileName;
	bSourceIsJson      = bJson;
	ResolvedSourcePath = UInputReplaySerializer::ResolveRecordingPath(FileName, bJson);

	FInputRecording Loaded;
	FString Error;
	if (!UInputReplaySerializer::Load(Loaded, FileName, bJson, Error))
	{
		LastImportError = Error;
		UE_LOG(LogInputMatch, Error, TEXT("%s: import failed - %s"), *GetName(), *Error);
		return false;
	}

	LastImportError.Reset();
	PopulateFromRecording(Loaded);

	UE_LOG(LogInputMatch, Log, TEXT("%s: imported '%s' (%d sample(s), %d cue(s), %.2fs)."),
		*GetName(), *ResolvedSourcePath, Timeline.Num(), MatchInputCues.Num(), DurationSeconds);

	MarkPackageDirty();
	return true;
}

void UInputRecordingDataAsset::PopulateFromRecording(const FInputRecording& InRecording)
{
	// ---- Raw copies (the authoritative cache) -------------------------------------------------
	Header            = InRecording.Header;
	RawFrames         = InRecording.Frames;
	SyncPoints        = InRecording.SyncPoints;
	FrameDeltaSeconds = InRecording.FrameDeltaSeconds;

	DurationSeconds    = InRecording.GetDurationSeconds();
	TotalLogicalFrames = InRecording.Header.TotalFrames;
	SampleCount        = InRecording.Frames.Num();
	SyncPointCount     = InRecording.SyncPoints.Num();
	ImportedAtUtc      = FDateTime::UtcNow();

	const int32 NumActions = Header.ActionPaths.Num();

	// Which registry indices are per-frame deltas (mouse) rather than held rates.
	TArray<bool> bIsDelta;
	bIsDelta.Init(false, NumActions);
	for (const int32 DeltaIndex : Header.FrameDeltaActionIndices)
	{
		if (bIsDelta.IsValidIndex(DeltaIndex))
		{
			bIsDelta[DeltaIndex] = true;
		}
	}

	// ---- Per-action rollup -------------------------------------------------------------------
	ActionSummaries.Reset(NumActions);
	for (int32 Index = 0; Index < NumActions; ++Index)
	{
		const FSoftObjectPath Path(Header.ActionPaths[Index]);

		FInputRecordingActionSummary& Summary = ActionSummaries.AddDefaulted_GetRef();
		Summary.ActionIndex   = Index;
		Summary.ActionName    = UInputMatchLibrary::GetActionShortName(Header.ActionPaths[Index]);
		Summary.Action        = TSoftObjectPtr<UInputAction>(Path);
		Summary.bIsFrameDelta = bIsDelta[Index];

		// Synchronous load is acceptable here: this only runs on import / reimport, and knowing
		// whether the recording's asset references still resolve is the whole point of the check.
		Summary.bResolved = (Cast<UInputAction>(Path.TryLoad()) != nullptr);
	}

	// ---- Readable timeline -------------------------------------------------------------------
	Timeline.Reset(InRecording.Frames.Num());
	for (const FRecordedInputFrame& Frame : InRecording.Frames)
	{
		const bool bKnownAction = Header.ActionPaths.IsValidIndex(Frame.ActionIndex);
		const FString ActionPath = bKnownAction ? Header.ActionPaths[Frame.ActionIndex] : FString();
		const FVector Value(Frame.Value);

		FInputRecordingTimelineEntry& Entry = Timeline.AddDefaulted_GetRef();
		Entry.TimeSeconds   = UInputMatchLibrary::GetFrameTimeSeconds(InRecording, Frame);
		Entry.FrameIndex    = Frame.FrameIndex;
		Entry.ActionName    = bKnownAction ? UInputMatchLibrary::GetActionShortName(ActionPath)
										   : FString::Printf(TEXT("<unknown index %d>"), Frame.ActionIndex);
		Entry.Action        = bKnownAction ? TSoftObjectPtr<UInputAction>(FSoftObjectPath(ActionPath))
										   : TSoftObjectPtr<UInputAction>();
		Entry.TriggerEvent  = UInputMatchLibrary::DescribeTriggerEvent(Frame.TriggerEvent);
		Entry.ValueType     = UInputMatchLibrary::DescribeValueType(Frame.ValueType);
		Entry.Value         = Value;
		Entry.bIsFrameDelta = bIsDelta.IsValidIndex(Frame.ActionIndex) && bIsDelta[Frame.ActionIndex];
		Entry.Description   = UInputMatchLibrary::DescribeInputValue(Entry.ActionName, Frame.ValueType, Value);

		if (ActionSummaries.IsValidIndex(Frame.ActionIndex))
		{
			++ActionSummaries[Frame.ActionIndex].SampleCount;
		}
	}

	RebuildMatchInputCues();
}

void UInputRecordingDataAsset::RebuildMatchInputCues()
{
	// Derive the cues from the cached raw data through the *same* library the runtime uses, so the
	// preview cannot drift away from what MatchInput will actually ask for.
	const FInputRecording Rebuilt = BuildRecording();
	UInputMatchLibrary::BuildMatchInputCues(Rebuilt, CueOptions, MatchInputCues);

	for (FInputRecordingActionSummary& Summary : ActionSummaries)
	{
		Summary.CueCount = 0;
	}
	for (const FMatchInputCue& Cue : MatchInputCues)
	{
		if (ActionSummaries.IsValidIndex(Cue.ActionIndex))
		{
			++ActionSummaries[Cue.ActionIndex].CueCount;
		}
	}
}

void UInputRecordingDataAsset::ExportToJson()
{
	if (!HasValidRecording())
	{
		UE_LOG(LogInputMatch, Error, TEXT("%s: nothing to export - import a recording first."), *GetName());
		return;
	}

	// Name the export after the asset rather than the source file, so exporting never overwrites the
	// recording this asset was built from.
	const FString ExportName = GetName() + TEXT("_Export");

	FString Error;
	if (!UInputReplaySerializer::SaveJson(BuildRecording(), ExportName, Error))
	{
		UE_LOG(LogInputMatch, Error, TEXT("%s: JSON export failed - %s"), *GetName(), *Error);
		return;
	}

	UE_LOG(LogInputMatch, Log, TEXT("%s: exported to '%s'."),
		*GetName(), *UInputReplaySerializer::ResolveRecordingPath(ExportName, /*bJson=*/true));
}

FInputRecording UInputRecordingDataAsset::BuildRecording() const
{
	FInputRecording Out;
	Out.Header            = Header;
	Out.Frames            = RawFrames;
	Out.FrameDeltaSeconds = FrameDeltaSeconds;
	Out.SyncPoints        = SyncPoints;
	return Out;
}

#if WITH_EDITOR
void UInputRecordingDataAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// MemberProperty (not Property) is the outer UPROPERTY on this class, so this also fires for
	// edits made to fields *inside* the CueOptions struct.
	const FName MemberName = PropertyChangedEvent.MemberProperty
		? PropertyChangedEvent.MemberProperty->GetFName()
		: NAME_None;

	if (MemberName == GET_MEMBER_NAME_CHECKED(UInputRecordingDataAsset, CueOptions))
	{
		// Deliberately no disk access: retune the threshold, see the cue list update instantly.
		RebuildMatchInputCues();
	}
}
#endif
