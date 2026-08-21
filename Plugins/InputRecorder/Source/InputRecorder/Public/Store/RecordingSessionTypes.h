// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RecordingSessionTypes.generated.h"

/**
 * One session folder.
 *
 * The fields split into two blocks and the split matters. Session.json is metadata, never
 * truth: every field in it must be re-derivable from the folder alone, which is exactly what
 * happens when the manifest is missing or corrupt. A folder without a manifest is still a
 * usable session, just with an unknown duration until it is rebuilt.
 */
USTRUCT(BlueprintType)
struct INPUTRECORDER_API FRecordingSessionInfo
{
	GENERATED_BODY()

	// --- Persisted in Session.json -----------------------------------------------------------

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Recording Session")
	int32 Index = INDEX_NONE;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Recording Session")
	FString DisplayName;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Recording Session")
	FDateTime CreatedUtc = FDateTime(0);

	/** Reviewing a session updates this. LRU eviction is by update time, not creation time. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Recording Session")
	FDateTime UpdatedUtc = FDateTime(0);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Recording Session")
	float DurationSeconds = 0.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Recording Session")
	int32 CueCount = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Recording Session")
	FString MapName;

	// --- Derived from the folder on every scan ------------------------------------------------

	UPROPERTY(BlueprintReadOnly, Category = "Recording Session")
	FString FolderName;

	UPROPERTY(BlueprintReadOnly, Category = "Recording Session")
	FString AbsolutePath;

	UPROPERTY(BlueprintReadOnly, Category = "Recording Session")
	int64 TotalBytes = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Recording Session")
	bool bHasGhost = false;

	UPROPERTY(BlueprintReadOnly, Category = "Recording Session")
	bool bHasVideo = false;

	UPROPERTY(BlueprintReadOnly, Category = "Recording Session")
	bool bHasJson = false;

	bool IsValid() const { return Index >= 0 && !AbsolutePath.IsEmpty(); }

	/**
	 * A folder still being written is newer than everything else and must not win "most recent",
	 * so playability is gated on the .ghost actually existing.
	 */
	bool IsPlayable() const { return IsValid() && bHasGhost; }

	/** Absolute path with no extension. Every file in the take hangs off this one string. */
	FString GetBasePath() const;
	FString GetGhostPath() const;
	FString GetJsonPath() const;
	FString GetVideoPath() const;
	FString GetManifestPath() const;

	static FString MakeFolderName(int32 InIndex);

	/** True when Name is exactly "Recording_<n>"; writes the index out. */
	static bool ParseFolderName(const FString& Name, int32& OutIndex);
};

/** Store-wide totals, for the quota bar and the console footer line. */
USTRUCT(BlueprintType)
struct INPUTRECORDER_API FRecordingStoreStats
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

	float GetUsedFraction() const
	{
		return QuotaBytes > 0 ? FMath::Clamp(static_cast<float>(static_cast<double>(TotalBytes) / static_cast<double>(QuotaBytes)), 0.0f, 1.0f) : 0.0f;
	}

	int64 GetFreeBytes() const { return FMath::Max<int64>(0, QuotaBytes - TotalBytes); }
};

/**
 * One row, pre-formatted.
 *
 * Built once on the store and consumed by both the console printer and the widget, because the
 * two must never format the same thing twice and disagree about it.
 */
USTRUCT(BlueprintType)
struct INPUTRECORDER_API FRecordingListEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Recording Store")
	int32 Index = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Recording Store")
	FString FolderName;

	UPROPERTY(BlueprintReadOnly, Category = "Recording Store")
	FString DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Recording Store")
	FString SizeText;

	UPROPERTY(BlueprintReadOnly, Category = "Recording Store")
	FString LastUpdatedText;

	UPROPERTY(BlueprintReadOnly, Category = "Recording Store")
	FString DurationText;

	UPROPERTY(BlueprintReadOnly, Category = "Recording Store")
	int32 CueCount = 0;

	/** "ghost + mp4 + json". */
	UPROPERTY(BlueprintReadOnly, Category = "Recording Store")
	FString ContentsText;

	UPROPERTY(BlueprintReadOnly, Category = "Recording Store")
	bool bPlayable = false;

	UPROPERTY(BlueprintReadOnly, Category = "Recording Store")
	int64 TotalBytes = 0;
};
