// Copyright (c) Your Studio. All Rights Reserved.
//
// RecordingStore.h
//
// Owns the recording folder: what is in it, how big it is, which take is next, and what gets deleted
// when the quota is reached.
//
// Everything that touches the recording directory goes through here. That is the whole design goal -
// before this existed the serializer built its own paths, the screen recorder built its own paths,
// and nothing knew the total size of anything. One object owning the directory is what makes the
// quota enforceable, because there is exactly one place that knows what is on disk.
//
// LIFETIME
//   Created and owned by UInputRecordingSubsystem, so it outlives level travel. Initialize() runs
//   once at subsystem startup: scan, migrate the legacy flat layout if present, reconcile the index
//   counter, log an inventory, and trim if the folder is already over quota.
//
// THE QUOTA RULE
//   Eviction is least-recently-updated first, and it happens at exactly two moments: before a take
//   starts (reserving headroom) and after one commits. It deliberately does NOT happen during a take
//   - a recording in progress that suddenly deletes files is a recording that can lose the thing it
//   is about to write. Mid-take the store only reports; the subsystem stops the take instead.

#pragma once

#include "CoreMinimal.h"
#include "Storage/RecordingSessionTypes.h"
#include "UObject/Object.h"

#include "RecordingStore.generated.h"

UCLASS(BlueprintType)
class UNREALINPUTRECORDING_API URecordingStore : public UObject
{
	GENERATED_BODY()

public:
	// -----------------------------------------------------------------------------------------
	// Root resolution
	// -----------------------------------------------------------------------------------------

	/**
	 * Where sessions live.
	 *
	 *   Desktop / editor : <ProjectSaved>/Recordings
	 *   Console          : <PersistentDownloadDir>/Recordings
	 *
	 * The split is not cosmetic. ProjectSavedDir() is not a reliably writable location in a packaged
	 * console title, and 900 MB of video has no business in user save data even where it is - save
	 * data is small, user-visible and bound to a profile. The persistent download area is the
	 * platform's answer for exactly this: large, app-owned, disposable.
	 *
	 * Overridable for a test run with -RecordingRoot=D:/Takes.
	 */
	UFUNCTION(BlueprintPure, Category = "Recording Store")
	static FString ResolveRootDirectory();

	// -----------------------------------------------------------------------------------------
	// Lifecycle
	// -----------------------------------------------------------------------------------------

	/** Scan, migrate, reconcile, log, trim. Safe to call again; Rescan() is the cheaper repeat. */
	void Initialize(int64 InQuotaBytes);

	/** Re-reads the directory from scratch. Call after anything outside this object touched it. */
	UFUNCTION(BlueprintCallable, Category = "Recording Store")
	void Rescan();

	// -----------------------------------------------------------------------------------------
	// Queries
	// -----------------------------------------------------------------------------------------

	const TArray<FRecordingSessionInfo>& GetSessions() const { return Sessions; }

	UFUNCTION(BlueprintPure, Category = "Recording Store")
	FRecordingStoreStats GetStats() const;

	UFUNCTION(BlueprintCallable, Category = "Recording Store")
	bool FindSession(int32 Index, FRecordingSessionInfo& OutSession) const;

	UFUNCTION(BlueprintCallable, Category = "Recording Store")
	bool FindSessionByFolder(const FString& FolderName, FRecordingSessionInfo& OutSession) const;

	/**
	 * The take with the newest UpdatedUtc that is actually playable.
	 *
	 * This is what -ControlRecap boots into, so "playable" matters: a folder whose .ghost never
	 * finished writing is newer than everything else and would otherwise win every time.
	 */
	UFUNCTION(BlueprintCallable, Category = "Recording Store")
	bool GetMostRecentSession(FRecordingSessionInfo& OutSession) const;

	UFUNCTION(BlueprintPure, Category = "Recording Store")
	int64 GetTotalBytes() const;

	UFUNCTION(BlueprintPure, Category = "Recording Store")
	int64 GetQuotaBytes() const { return QuotaBytes; }

	/** Would ExtraBytes fit without eviction? */
	UFUNCTION(BlueprintPure, Category = "Recording Store")
	bool HasHeadroom(int64 ExtraBytes) const;

	UFUNCTION(BlueprintPure, Category = "Recording Store")
	int32 GetNextIndex() const { return NextIndex; }

	// -----------------------------------------------------------------------------------------
	// Session lifecycle
	// -----------------------------------------------------------------------------------------

	/**
	 * Claims the next index, evicts until ReserveBytes fits, and creates the folder.
	 *
	 * @return an invalid session if the folder could not be created, or if ReserveBytes cannot be
	 *         made to fit even with everything evictable gone. Recording must not start either way.
	 */
	FRecordingSessionInfo BeginSession(const FString& DisplayName, const FString& MapName, int64 ReserveBytes);

	/** Writes the manifest, refreshes sizes, unpins, then trims. Call once the files are on disk. */
	bool CommitSession(int32 Index, float DurationSeconds, int32 CueCount);

	/** Deletes the folder outright. For a take that failed before producing anything usable. */
	void AbortSession(int32 Index);

	/** Bumps UpdatedUtc so replaying a take protects it from eviction. */
	void TouchSession(int32 Index);

	// -----------------------------------------------------------------------------------------
	// Quota
	// -----------------------------------------------------------------------------------------

	/**
	 * Evicts least-recently-updated sessions until total + ExtraReservation fits under the quota.
	 * Pinned sessions are skipped, so this can return having freed less than asked for.
	 *
	 * @return how many sessions were deleted.
	 */
	int32 TrimToQuota(int64 ExtraReservation);

	/** A pinned session is never evicted: the take being written, and the one being replayed. */
	void PinSession(int32 Index);
	void UnpinSession(int32 Index);
	bool IsPinned(int32 Index) const { return PinnedIndices.Contains(Index); }

	// -----------------------------------------------------------------------------------------
	// Logging
	// -----------------------------------------------------------------------------------------

	/** Prints the whole inventory as an aligned table. Reason heads the block, e.g. "boot scan". */
	void LogInventory(const TCHAR* Reason) const;

private:
	/** Reads one session folder from disk. Manifest values are used only where the folder agrees. */
	bool ScanSession(const FString& FolderName, FRecordingSessionInfo& OutSession) const;

	bool WriteManifest(const FRecordingSessionInfo& Session) const;
	bool ReadManifest(const FString& ManifestPath, FRecordingSessionInfo& OutSession) const;

	void ReadIndexFile();
	void WriteIndexFile() const;

	/**
	 * Moves a pre-folder <ProjectSaved>/InputRecordings/<Name>.{ghost,ghost.json,mp4} trio into a
	 * session folder. Runs once - after it succeeds the legacy directory is empty and never re-read.
	 */
	void MigrateLegacyLayout();

	static int64 ComputeFolderBytes(const FString& FolderPath);

	/** Session indices sorted oldest-updated first: the eviction order. Skips pinned entries. */
	TArray<int32> BuildEvictionOrder() const;

	UPROPERTY(Transient)
	TArray<FRecordingSessionInfo> Sessions;

	/** Cached root, so a mid-session -RecordingRoot change cannot split a take across two folders. */
	FString RootDirectory;

	int64 QuotaBytes = 900 * RecordingStore::BytesPerMegabyte;

	/**
	 * Next folder index. Monotonic and never reused, even after the folder it named is evicted - a
	 * log line naming Recording_12 should never be ambiguous about which take it meant.
	 */
	int32 NextIndex = 1;

	TSet<int32> PinnedIndices;

	bool bInitialised = false;
};
