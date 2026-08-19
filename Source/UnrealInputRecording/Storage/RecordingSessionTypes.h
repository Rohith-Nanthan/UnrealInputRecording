// Copyright (c) Your Studio. All Rights Reserved.
//
// RecordingSessionTypes.h
//
// Vocabulary for the on-disk half of the recording system.
//
// A "session" is one take, and one take is one folder. The folder name is the identity - everything
// inside it is named after the folder, so a session copied out to a desk somewhere is still
// self-describing:
//
//     <RecordingRoot>/Recording_7/Recording_7.ghost
//     <RecordingRoot>/Recording_7/Recording_7.ghost.json    (optional, debug only)
//     <RecordingRoot>/Recording_7/Recording_7.mp4
//     <RecordingRoot>/Recording_7/Session.json              (this struct, serialised)
//
// Session.json is metadata, never truth. Every field in it can be rebuilt by looking at the folder,
// which is what happens when it is missing or corrupt - see URecordingStore::ScanSession. It exists
// so the store can answer "which take was touched most recently" and "how long is it" without
// parsing a .ghost, not because the folder needs it to be valid.

#pragma once

#include "CoreMinimal.h"

#include "RecordingSessionTypes.generated.h"

/**
 * Every file and quota operation logs here, and nothing else does.
 *
 * The point of a dedicated category is that `log LogRecordingStore Verbose` gives you the complete
 * story of what touched the disk without a single line of input or rendering noise, and
 * `log LogRecordingStore Off` silences file chatter without hiding recording errors.
 */
DECLARE_LOG_CATEGORY_EXTERN(LogRecordingStore, Log, All);

namespace RecordingStore
{
	/** Session folders are this plus a decimal index. Parsing this prefix is how sessions are found. */
	inline const TCHAR* SessionFolderPrefix = TEXT("Recording_");

	inline const TCHAR* ManifestFileName = TEXT("Session.json");

	/** Root-level counter file. Authoritative for the next index; the folder scan is the fallback. */
	inline const TCHAR* IndexFileName = TEXT("RecordingIndex.json");

	inline const TCHAR* RootFolderName = TEXT("Recordings");

	/**
	 * The flat layout that shipped before sessions became folders:
	 * <ProjectSaved>/InputRecordings/<Name>.ghost + .mp4. Migrated into a session folder on first
	 * scan, once, then never looked at again.
	 */
	inline const TCHAR* LegacyFolderName = TEXT("InputRecordings");

	inline constexpr int64 BytesPerMegabyte = 1024 * 1024;

	/** Formats a byte count the way the log prints it, e.g. "141.2 MB". */
	UNREALINPUTRECORDING_API FString FormatBytes(int64 Bytes);
}

/**
 * One take on disk.
 *
 * The first block is what Session.json stores; the second is derived by looking at the folder and is
 * deliberately not serialised, so a stale manifest can never lie about file sizes or which files
 * actually exist.
 */
USTRUCT(BlueprintType)
struct UNREALINPUTRECORDING_API FRecordingSessionInfo
{
	GENERATED_BODY()

	//~ Persisted in Session.json ------------------------------------------------------------------

	/** The n in Recording_n. INDEX_NONE means "not a real session". */
	UPROPERTY(BlueprintReadOnly, Category = "Recording Store")
	int32 Index = INDEX_NONE;

	/** Label the take was recorded under. Free text, shown in UI; not an identity. */
	UPROPERTY(BlueprintReadOnly, Category = "Recording Store")
	FString DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Recording Store")
	FDateTime CreatedUtc = FDateTime(0);

	/**
	 * Last time this session was written or replayed. The LRU key.
	 *
	 * Replaying a session touches this, which is the whole point: a take you keep testing against
	 * should outlive one you recorded later and never looked at.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Recording Store")
	FDateTime UpdatedUtc = FDateTime(0);

	UPROPERTY(BlueprintReadOnly, Category = "Recording Store")
	float DurationSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Recording Store")
	int32 CueCount = 0;

	/** Map the take was recorded in. Informational - nothing refuses to replay on a mismatch. */
	UPROPERTY(BlueprintReadOnly, Category = "Recording Store")
	FString MapName;

	//~ Derived from the folder on every scan ------------------------------------------------------

	UPROPERTY(BlueprintReadOnly, Category = "Recording Store")
	FString FolderName;

	UPROPERTY(BlueprintReadOnly, Category = "Recording Store")
	FString AbsolutePath;

	/** Sum of every file in the folder, not just the ones this system wrote. */
	UPROPERTY(BlueprintReadOnly, Category = "Recording Store")
	int64 TotalBytes = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Recording Store")
	bool bHasGhost = false;

	UPROPERTY(BlueprintReadOnly, Category = "Recording Store")
	bool bHasVideo = false;

	UPROPERTY(BlueprintReadOnly, Category = "Recording Store")
	bool bHasJson = false;

	//~ Helpers -------------------------------------------------------------------------------------

	bool IsValid() const { return Index >= 0 && !AbsolutePath.IsEmpty(); }

	/** A session with no .ghost cannot be replayed, whatever else is in the folder. */
	bool IsPlayable() const { return IsValid() && bHasGhost; }

	/**
	 * Path with no extension: "<root>/Recording_7/Recording_7".
	 *
	 * This is what gets handed to the serializer and the video path helper, both of which pass an
	 * absolute path through untouched and append their own extension. One base, three files, no
	 * string surgery at the call sites.
	 */
	FString GetBasePath() const;

	FString GetGhostPath() const;
	FString GetJsonPath() const;
	FString GetVideoPath() const;
	FString GetManifestPath() const;

	/** "Recording_7" from an index. The one place the folder naming convention is spelled out. */
	static FString MakeFolderName(int32 InIndex);

	/** Parses "Recording_7" back to 7, or INDEX_NONE if the name is not a session folder. */
	static int32 ParseFolderName(const FString& InFolderName);
};

/** Aggregate view of the store, for the log table and any UI that wants to show headroom. */
USTRUCT(BlueprintType)
struct UNREALINPUTRECORDING_API FRecordingStoreStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Recording Store")
	int32 SessionCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Recording Store")
	int64 TotalBytes = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Recording Store")
	int64 QuotaBytes = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Recording Store")
	int32 NextIndex = 1;

	/** 0..1 of the quota consumed. Clamped, so an over-quota store reads as full rather than >1. */
	float GetUsedFraction() const
	{
		return QuotaBytes > 0 ? FMath::Clamp(static_cast<float>(static_cast<double>(TotalBytes) / static_cast<double>(QuotaBytes)), 0.f, 1.f) : 0.f;
	}

	int64 GetFreeBytes() const { return FMath::Max<int64>(0, QuotaBytes - TotalBytes); }
};
