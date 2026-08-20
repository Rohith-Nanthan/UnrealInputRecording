// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Store/RecordingSessionTypes.h"
#include "RecordingStore.generated.h"

/**
 * Owns the recordings folder: layout, indices, quota and eviction.
 *
 * A UObject owned by the subsystem rather than a plain struct, so it outlives level travel
 * along with everything else the subsystem holds.
 *
 * Recordings live in dated, indexed session folders and never as bare files. The folder is the
 * unit of storage, eviction, listing and review:
 *
 *   <Root>/RecordingIndex.json      authoritative next-index counter, never reuses an index
 *   <Root>/Recording_1/Recording_1.ghost, .ghost.json, .mp4, Session.json
 *   <Root>/Recording_7/             indices go sparse after eviction and are never reused
 */
UCLASS(BlueprintType)
class UNREALINPUTRECORDING_API URecordingStore : public UObject
{
	GENERATED_BODY()

public:
	/** Resolves the root, scans it, reconciles the index counter, logs the inventory and trims. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Store")
	void Initialize(int64 InQuotaBytes);

	UFUNCTION(BlueprintCallable, Category = "Input Recording|Store")
	void Rescan();

	/**
	 * Reserves an index and creates the folder. Call only once input recording has definitely
	 * started - a failed start must never burn an index or leave an empty directory behind.
	 *
	 * @return the new session index, or INDEX_NONE when the quota cannot fit the reservation
	 *         even after evicting everything it is allowed to.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Store")
	int32 BeginSession(const FString& DisplayName, const FString& MapName, int64 ReserveBytes);

	/** Writes the manifest, refreshes sizes, unpins, then trims. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Store")
	bool CommitSession(int32 Index, float DurationSeconds, int32 CueCount);

	/** Abandons a take and deletes its folder. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Store")
	bool AbortSession(int32 Index);

	/**
	 * Marks a session as used. Reviewing counts as using, which is why eviction is by update
	 * time rather than creation time - a take somebody keeps coming back to should outlive one
	 * they recorded later and never watched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Store")
	bool TouchSession(int32 Index);

	/** Evicts least-recently-updated unpinned sessions until HeadroomBytes fits. Returns the count evicted. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Store")
	int32 TrimToQuota(int64 HeadroomBytes);

	/** The session being recorded and the one under review are pinned and never evicted. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Store")
	void PinSession(int32 Index);

	UFUNCTION(BlueprintCallable, Category = "Input Recording|Store")
	void UnpinSession(int32 Index);

	UFUNCTION(BlueprintPure, Category = "Input Recording|Store")
	bool IsPinned(int32 Index) const;

	/** Most recently updated *playable* session - a folder still being written must not win. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Store")
	bool GetMostRecentSession(FRecordingSessionInfo& OutSession) const;

	UFUNCTION(BlueprintCallable, Category = "Input Recording|Store")
	bool FindSession(int32 Index, FRecordingSessionInfo& OutSession) const;

	UFUNCTION(BlueprintCallable, Category = "Input Recording|Store")
	bool FindSessionByFolder(const FString& FolderName, FRecordingSessionInfo& OutSession) const;

	/**
	 * Accepts "Recording_5", a bare "5", or a display name (case-insensitive). When a display
	 * name matches more than one session the most recently updated wins and the others are
	 * named in the log.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Store")
	bool ResolveSessionSpecifier(const FString& Specifier, FRecordingSessionInfo& OutSession) const;

	UFUNCTION(BlueprintPure, Category = "Input Recording|Store")
	TArray<FRecordingSessionInfo> GetSessions() const { return Sessions; }

	UFUNCTION(BlueprintPure, Category = "Input Recording|Store")
	FRecordingStoreStats GetStats() const;

	UFUNCTION(BlueprintPure, Category = "Input Recording|Store")
	int64 GetTotalBytes() const;

	UFUNCTION(BlueprintPure, Category = "Input Recording|Store")
	bool HasHeadroom(int64 ExtraBytes) const;

	UFUNCTION(BlueprintPure, Category = "Input Recording|Store")
	FString GetRoot() const { return ResolvedRoot; }

	/** One row per session, pre-formatted, sorted most recently updated first. */
	UFUNCTION(BlueprintCallable, Category = "Input Recording|Store")
	TArray<FRecordingListEntry> BuildListEntries() const;

	UFUNCTION(BlueprintCallable, Category = "Input Recording|Store")
	void LogInventory(const FString& Reason) const;

	/** Live bytes for an in-progress take, used by the once-a-second quota poll. */
	UFUNCTION(BlueprintPure, Category = "Input Recording|Store")
	int64 GetSessionBytesOnDisk(int32 Index) const;

private:
	FString ResolveRoot() const;
	FString GetIndexFilePath() const;

	void LoadIndexCounter();
	void SaveIndexCounter() const;

	bool ReadManifest(FRecordingSessionInfo& Session) const;
	bool WriteManifest(const FRecordingSessionInfo& Session) const;

	/** Recomputes the derived block - sizes and which files exist - from the folder itself. */
	void RefreshDerivedFields(FRecordingSessionInfo& Session) const;

	int32 IndexOfSession(int32 SessionIndex) const;
	bool DeleteSessionFolder(const FRecordingSessionInfo& Session) const;

	UPROPERTY(Transient)
	TArray<FRecordingSessionInfo> Sessions;

	TSet<int32> PinnedIndices;

	/** Cached, so a mid-session command-line or config change cannot split a take across two folders. */
	FString ResolvedRoot;

	int64 QuotaBytes = 0;
	int32 NextIndex = 1;
	bool bInitialized = false;
};
