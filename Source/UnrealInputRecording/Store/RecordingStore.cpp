// Copyright Epic Games, Inc. All Rights Reserved.

#include "Store/RecordingStore.h"

#include "Boot/RecordingBootFlags.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "InputRecordingLog.h"
#include "JsonObjectConverter.h"
#include "Library/InputRecordingFormatLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace RecordingStorePrivate
{
	const TCHAR* IndexFileName = TEXT("RecordingIndex.json");
	const TCHAR* RecordingsFolderName = TEXT("Recordings");

	/** Sums every file under a directory tree. */
	int64 ComputeDirectorySize(const FString& Directory)
	{
		class FSizeVisitor : public IPlatformFile::FDirectoryStatVisitor
		{
		public:
			int64 Total = 0;

			virtual bool Visit(const TCHAR*, const FFileStatData& StatData) override
			{
				if (!StatData.bIsDirectory && StatData.FileSize > 0)
				{
					Total += StatData.FileSize;
				}
				return true;
			}
		};

		FSizeVisitor Visitor;
		FPlatformFileManager::Get().GetPlatformFile().IterateDirectoryStatRecursively(*Directory, Visitor);
		return Visitor.Total;
	}
}

// -------------------------------------------------------------------------------------------
// Root and index
// -------------------------------------------------------------------------------------------

FString URecordingStore::ResolveRoot() const
{
	const FString Override = RecordingBootFlags::Get().RecordingRootOverride;
	if (!Override.IsEmpty())
	{
		return FPaths::ConvertRelativePathToFull(Override);
	}

#if PLATFORM_DESKTOP
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), RecordingStorePrivate::RecordingsFolderName));
#else
	// Not cosmetic: ProjectSavedDir is not reliably writable in a packaged console title, and
	// hundreds of megabytes of video has no business in user save data even where it works.
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPlatformMisc::GamePersistentDownloadDir(), RecordingStorePrivate::RecordingsFolderName));
#endif
}

FString URecordingStore::GetIndexFilePath() const
{
	return FPaths::Combine(ResolvedRoot, RecordingStorePrivate::IndexFileName);
}

void URecordingStore::LoadIndexCounter()
{
	NextIndex = 1;

	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *GetIndexFilePath()))
	{
		return;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
	{
		int32 Stored = 1;
		if (Root->TryGetNumberField(TEXT("NextIndex"), Stored))
		{
			NextIndex = FMath::Max(1, Stored);
		}
	}
}

void URecordingStore::SaveIndexCounter() const
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("NextIndex"), NextIndex);

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Root, Writer);

	if (!FFileHelper::SaveStringToFile(Json, *GetIndexFilePath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogRecordingStore, Error, TEXT("Could not write the index counter to %s."), *GetIndexFilePath());
	}
}

// -------------------------------------------------------------------------------------------
// Manifests
// -------------------------------------------------------------------------------------------

bool URecordingStore::ReadManifest(FRecordingSessionInfo& Session) const
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Session.GetManifestPath()))
	{
		return false;
	}

	// Only the persisted block is read back. The derived block is always recomputed from the
	// folder, because the manifest is metadata and never truth.
	FRecordingSessionInfo Persisted;
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Persisted, 0, 0))
	{
		UE_LOG(LogRecordingStore, Warning, TEXT("Manifest %s is unreadable; deriving what we can from the folder."),
			*Session.GetManifestPath());
		return false;
	}

	Session.DisplayName = Persisted.DisplayName;
	Session.CreatedUtc = Persisted.CreatedUtc;
	Session.UpdatedUtc = Persisted.UpdatedUtc;
	Session.DurationSeconds = Persisted.DurationSeconds;
	Session.CueCount = Persisted.CueCount;
	Session.MapName = Persisted.MapName;

	return true;
}

bool URecordingStore::WriteManifest(const FRecordingSessionInfo& Session) const
{
	FString Json;
	if (!FJsonObjectConverter::UStructToJsonObjectString(Session, Json, 0, 0, 0, nullptr, /*bPrettyPrint=*/true))
	{
		UE_LOG(LogRecordingStore, Error, TEXT("Could not serialise the manifest for %s."), *Session.FolderName);
		return false;
	}

	if (!FFileHelper::SaveStringToFile(Json, *Session.GetManifestPath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogRecordingStore, Error, TEXT("Could not write the manifest %s."), *Session.GetManifestPath());
		return false;
	}

	UE_LOG(LogRecordingStore, Verbose, TEXT("Manifest written: %s"), *Session.GetManifestPath());
	return true;
}

void URecordingStore::RefreshDerivedFields(FRecordingSessionInfo& Session) const
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	Session.bHasGhost = PlatformFile.FileExists(*Session.GetGhostPath());
	Session.bHasVideo = PlatformFile.FileExists(*Session.GetVideoPath());
	Session.bHasJson = PlatformFile.FileExists(*Session.GetJsonPath());
	Session.TotalBytes = RecordingStorePrivate::ComputeDirectorySize(Session.AbsolutePath);

	// A folder with no manifest is still a usable session. Fall back to the file system's own
	// timestamps rather than leaving it looking like it was made at the epoch and evicting it
	// first.
	if (Session.CreatedUtc.GetTicks() == 0 || Session.UpdatedUtc.GetTicks() == 0)
	{
		const FFileStatData Stat = PlatformFile.GetStatData(*Session.AbsolutePath);
		if (Stat.bIsValid)
		{
			if (Session.CreatedUtc.GetTicks() == 0)
			{
				Session.CreatedUtc = Stat.CreationTime;
			}
			if (Session.UpdatedUtc.GetTicks() == 0)
			{
				Session.UpdatedUtc = Stat.ModificationTime;
			}
		}
	}

	if (Session.DisplayName.IsEmpty())
	{
		Session.DisplayName = Session.FolderName;
	}
}

// -------------------------------------------------------------------------------------------
// Scanning
// -------------------------------------------------------------------------------------------

void URecordingStore::Initialize(int64 InQuotaBytes)
{
	QuotaBytes = FMath::Max<int64>(0, InQuotaBytes);
	ResolvedRoot = ResolveRoot();

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*ResolvedRoot))
	{
		PlatformFile.CreateDirectoryTree(*ResolvedRoot);
	}

	bInitialized = true;

	LoadIndexCounter();
	Rescan();
	LogInventory(TEXT("boot"));

	// Trim on boot: the quota may have been lowered since the last run, or a previous run may
	// have been killed mid-take.
	TrimToQuota(0);
}

void URecordingStore::Rescan()
{
	Sessions.Reset();

	if (ResolvedRoot.IsEmpty())
	{
		ResolvedRoot = ResolveRoot();
	}

	TArray<FString> FolderNames;
	IFileManager::Get().FindFiles(FolderNames, *(ResolvedRoot / TEXT("*")), /*Files=*/false, /*Directories=*/true);

	int32 HighestIndex = 0;

	for (const FString& FolderName : FolderNames)
	{
		int32 ParsedIndex = INDEX_NONE;
		if (!FRecordingSessionInfo::ParseFolderName(FolderName, ParsedIndex))
		{
			continue;
		}

		FRecordingSessionInfo Session;
		Session.Index = ParsedIndex;
		Session.FolderName = FolderName;
		Session.AbsolutePath = FPaths::Combine(ResolvedRoot, FolderName);

		ReadManifest(Session);
		RefreshDerivedFields(Session);

		Sessions.Add(MoveTemp(Session));
		HighestIndex = FMath::Max(HighestIndex, ParsedIndex);
	}

	// Indices are never reused, so the counter only ever moves forward. A counter file that
	// went missing is reconstructed from the highest folder on disk.
	const int32 ReconciledNext = FMath::Max(NextIndex, HighestIndex + 1);
	if (ReconciledNext != NextIndex)
	{
		UE_LOG(LogRecordingStore, Log, TEXT("Index counter reconciled from %d to %d against the folders on disk."),
			NextIndex, ReconciledNext);
		NextIndex = ReconciledNext;
		SaveIndexCounter();
	}

	Sessions.Sort([](const FRecordingSessionInfo& A, const FRecordingSessionInfo& B)
	{
		return A.UpdatedUtc > B.UpdatedUtc;
	});
}

// -------------------------------------------------------------------------------------------
// Session lifecycle
// -------------------------------------------------------------------------------------------

int32 URecordingStore::IndexOfSession(int32 SessionIndex) const
{
	return Sessions.IndexOfByPredicate([SessionIndex](const FRecordingSessionInfo& Session)
	{
		return Session.Index == SessionIndex;
	});
}

int32 URecordingStore::BeginSession(const FString& DisplayName, const FString& MapName, int64 ReserveBytes)
{
	if (!bInitialized)
	{
		UE_LOG(LogRecordingStore, Error, TEXT("BeginSession called before Initialize."));
		return INDEX_NONE;
	}

	// Eviction happens here and after a commit - never during a take.
	TrimToQuota(ReserveBytes);

	if (!HasHeadroom(ReserveBytes))
	{
		UE_LOG(LogRecordingStore, Error,
			TEXT("Refusing to start a take: %s reserved but only %s free of a %s quota, and everything else is pinned or already gone."),
			*UInputRecordingFormatLibrary::FormatByteSize(ReserveBytes),
			*UInputRecordingFormatLibrary::FormatByteSize(GetStats().GetFreeBytes()),
			*UInputRecordingFormatLibrary::FormatByteSize(QuotaBytes));
		return INDEX_NONE;
	}

	FRecordingSessionInfo Session;
	Session.Index = NextIndex;
	Session.FolderName = FRecordingSessionInfo::MakeFolderName(Session.Index);
	Session.AbsolutePath = FPaths::Combine(ResolvedRoot, Session.FolderName);
	Session.DisplayName = DisplayName.IsEmpty() ? Session.FolderName : DisplayName;
	Session.MapName = MapName;
	Session.CreatedUtc = FDateTime::UtcNow();
	Session.UpdatedUtc = Session.CreatedUtc;

	if (!FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*Session.AbsolutePath))
	{
		UE_LOG(LogRecordingStore, Error, TEXT("Could not create the session folder %s."), *Session.AbsolutePath);
		return INDEX_NONE;
	}

	++NextIndex;
	SaveIndexCounter();

	// Pinned for the duration: the take being recorded is never a candidate for eviction.
	PinSession(Session.Index);

	Sessions.Insert(MoveTemp(Session), 0);

	UE_LOG(LogRecordingStore, Log, TEXT("Session %s claimed (%s reserved)."),
		*Sessions[0].FolderName, *UInputRecordingFormatLibrary::FormatByteSize(ReserveBytes));

	return Sessions[0].Index;
}

bool URecordingStore::CommitSession(int32 Index, float DurationSeconds, int32 CueCount)
{
	const int32 ArrayIndex = IndexOfSession(Index);
	if (ArrayIndex == INDEX_NONE)
	{
		UE_LOG(LogRecordingStore, Error, TEXT("CommitSession: no session with index %d."), Index);
		return false;
	}

	FRecordingSessionInfo& Session = Sessions[ArrayIndex];
	Session.DurationSeconds = DurationSeconds;
	Session.CueCount = CueCount;
	Session.UpdatedUtc = FDateTime::UtcNow();

	RefreshDerivedFields(Session);
	WriteManifest(Session);

	UnpinSession(Index);

	UE_LOG(LogRecordingStore, Log, TEXT("Session %s committed: %s, %.2fs, %d cue(s), ghost=%s video=%s json=%s."),
		*Session.FolderName, *UInputRecordingFormatLibrary::FormatByteSize(Session.TotalBytes),
		Session.DurationSeconds, Session.CueCount,
		Session.bHasGhost ? TEXT("yes") : TEXT("no"),
		Session.bHasVideo ? TEXT("yes") : TEXT("no"),
		Session.bHasJson ? TEXT("yes") : TEXT("no"));

	TrimToQuota(0);
	return true;
}

bool URecordingStore::AbortSession(int32 Index)
{
	const int32 ArrayIndex = IndexOfSession(Index);
	if (ArrayIndex == INDEX_NONE)
	{
		return false;
	}

	const FRecordingSessionInfo Session = Sessions[ArrayIndex];

	UnpinSession(Index);
	Sessions.RemoveAt(ArrayIndex);

	const bool bDeleted = DeleteSessionFolder(Session);

	UE_LOG(LogRecordingStore, Log, TEXT("Session %s aborted and its folder %s."),
		*Session.FolderName, bDeleted ? TEXT("deleted") : TEXT("could not be deleted"));

	// The index is deliberately not returned to the pool. Indices go sparse and are never reused.
	return bDeleted;
}

bool URecordingStore::TouchSession(int32 Index)
{
	const int32 ArrayIndex = IndexOfSession(Index);
	if (ArrayIndex == INDEX_NONE)
	{
		return false;
	}

	Sessions[ArrayIndex].UpdatedUtc = FDateTime::UtcNow();
	WriteManifest(Sessions[ArrayIndex]);

	Sessions.Sort([](const FRecordingSessionInfo& A, const FRecordingSessionInfo& B)
	{
		return A.UpdatedUtc > B.UpdatedUtc;
	});

	UE_LOG(LogRecordingStore, Verbose, TEXT("Session %d touched; it now sits at the front of the LRU queue."), Index);
	return true;
}

bool URecordingStore::DeleteSessionFolder(const FRecordingSessionInfo& Session) const
{
	if (Session.AbsolutePath.IsEmpty())
	{
		return false;
	}

	return IFileManager::Get().DeleteDirectory(*Session.AbsolutePath, /*RequireExists=*/false, /*Tree=*/true);
}

// -------------------------------------------------------------------------------------------
// Quota
// -------------------------------------------------------------------------------------------

int32 URecordingStore::TrimToQuota(int64 HeadroomBytes)
{
	if (QuotaBytes <= 0)
	{
		return 0;
	}

	int32 Evicted = 0;

	for (;;)
	{
		const int64 Total = GetTotalBytes();
		if (Total + HeadroomBytes <= QuotaBytes)
		{
			break;
		}

		// Least recently *updated* first, not oldest by creation: reviewing a session counts as
		// using it and should protect it.
		int32 VictimArrayIndex = INDEX_NONE;
		FDateTime OldestUpdate = FDateTime::MaxValue();

		for (int32 ArrayIndex = 0; ArrayIndex < Sessions.Num(); ++ArrayIndex)
		{
			const FRecordingSessionInfo& Session = Sessions[ArrayIndex];

			if (PinnedIndices.Contains(Session.Index))
			{
				continue;
			}

			if (Session.UpdatedUtc < OldestUpdate)
			{
				OldestUpdate = Session.UpdatedUtc;
				VictimArrayIndex = ArrayIndex;
			}
		}

		if (VictimArrayIndex == INDEX_NONE)
		{
			UE_LOG(LogRecordingStore, Warning,
				TEXT("Over quota by %s but every remaining session is pinned; nothing more can be evicted."),
				*UInputRecordingFormatLibrary::FormatByteSize(Total + HeadroomBytes - QuotaBytes));
			break;
		}

		const FRecordingSessionInfo Victim = Sessions[VictimArrayIndex];
		Sessions.RemoveAt(VictimArrayIndex);

		DeleteSessionFolder(Victim);
		++Evicted;

		UE_LOG(LogRecordingStore, Log, TEXT("Evicted %s (%s, last updated %s) to stay under quota."),
			*Victim.FolderName,
			*UInputRecordingFormatLibrary::FormatByteSize(Victim.TotalBytes),
			*UInputRecordingFormatLibrary::FormatRelativeTime(Victim.UpdatedUtc));
	}

	if (Evicted > 0)
	{
		LogInventory(TEXT("after eviction"));
	}

	return Evicted;
}

void URecordingStore::PinSession(int32 Index)
{
	PinnedIndices.Add(Index);
}

void URecordingStore::UnpinSession(int32 Index)
{
	PinnedIndices.Remove(Index);
}

bool URecordingStore::IsPinned(int32 Index) const
{
	return PinnedIndices.Contains(Index);
}

int64 URecordingStore::GetTotalBytes() const
{
	int64 Total = 0;
	for (const FRecordingSessionInfo& Session : Sessions)
	{
		Total += Session.TotalBytes;
	}
	return Total;
}

bool URecordingStore::HasHeadroom(int64 ExtraBytes) const
{
	return QuotaBytes <= 0 || (GetTotalBytes() + ExtraBytes) <= QuotaBytes;
}

FRecordingStoreStats URecordingStore::GetStats() const
{
	FRecordingStoreStats Stats;
	Stats.SessionCount = Sessions.Num();
	Stats.TotalBytes = GetTotalBytes();
	Stats.QuotaBytes = QuotaBytes;
	Stats.NextIndex = NextIndex;
	return Stats;
}

int64 URecordingStore::GetSessionBytesOnDisk(int32 Index) const
{
	const int32 ArrayIndex = IndexOfSession(Index);
	if (ArrayIndex == INDEX_NONE)
	{
		return 0;
	}

	// Deliberately hits the file system rather than trusting the cached size: this is the call
	// the live quota poll makes while a video file is still growing.
	return RecordingStorePrivate::ComputeDirectorySize(Sessions[ArrayIndex].AbsolutePath);
}

// -------------------------------------------------------------------------------------------
// Lookup
// -------------------------------------------------------------------------------------------

bool URecordingStore::GetMostRecentSession(FRecordingSessionInfo& OutSession) const
{
	const FRecordingSessionInfo* Best = nullptr;

	for (const FRecordingSessionInfo& Session : Sessions)
	{
		if (!Session.IsPlayable())
		{
			continue;
		}

		if (!Best || Session.UpdatedUtc > Best->UpdatedUtc)
		{
			Best = &Session;
		}
	}

	if (!Best)
	{
		return false;
	}

	OutSession = *Best;
	return true;
}

bool URecordingStore::FindSession(int32 Index, FRecordingSessionInfo& OutSession) const
{
	const int32 ArrayIndex = IndexOfSession(Index);
	if (ArrayIndex == INDEX_NONE)
	{
		return false;
	}

	OutSession = Sessions[ArrayIndex];
	return true;
}

bool URecordingStore::FindSessionByFolder(const FString& FolderName, FRecordingSessionInfo& OutSession) const
{
	for (const FRecordingSessionInfo& Session : Sessions)
	{
		if (Session.FolderName.Equals(FolderName, ESearchCase::IgnoreCase))
		{
			OutSession = Session;
			return true;
		}
	}

	return false;
}

bool URecordingStore::ResolveSessionSpecifier(const FString& Specifier, FRecordingSessionInfo& OutSession) const
{
	const FString Trimmed = Specifier.TrimStartAndEnd();

	if (Trimmed.IsEmpty())
	{
		return GetMostRecentSession(OutSession);
	}

	if (FindSessionByFolder(Trimmed, OutSession))
	{
		return true;
	}

	if (Trimmed.IsNumeric() && FindSession(FCString::Atoi(*Trimmed), OutSession))
	{
		return true;
	}

	// Display name, most recently updated wins.
	const FRecordingSessionInfo* Best = nullptr;
	TArray<FString> AlsoMatched;

	for (const FRecordingSessionInfo& Session : Sessions)
	{
		if (!Session.DisplayName.Equals(Trimmed, ESearchCase::IgnoreCase))
		{
			continue;
		}

		if (Best)
		{
			AlsoMatched.Add(Best->UpdatedUtc >= Session.UpdatedUtc ? Session.FolderName : Best->FolderName);
		}

		if (!Best || Session.UpdatedUtc > Best->UpdatedUtc)
		{
			Best = &Session;
		}
	}

	if (!Best)
	{
		return false;
	}

	if (AlsoMatched.Num() > 0)
	{
		UE_LOG(LogRecordingStore, Warning, TEXT("Display name '%s' matched several sessions; picked %s. Also matched: %s."),
			*Trimmed, *Best->FolderName, *FString::Join(AlsoMatched, TEXT(", ")));
	}

	OutSession = *Best;
	return true;
}

// -------------------------------------------------------------------------------------------
// Listing
// -------------------------------------------------------------------------------------------

TArray<FRecordingListEntry> URecordingStore::BuildListEntries() const
{
	TArray<FRecordingSessionInfo> Sorted = Sessions;
	Sorted.Sort([](const FRecordingSessionInfo& A, const FRecordingSessionInfo& B)
	{
		return A.UpdatedUtc > B.UpdatedUtc;
	});

	TArray<FRecordingListEntry> Entries;
	Entries.Reserve(Sorted.Num());

	for (const FRecordingSessionInfo& Session : Sorted)
	{
		FRecordingListEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.Index = Session.Index;
		Entry.FolderName = Session.FolderName;
		Entry.DisplayName = Session.DisplayName;
		Entry.SizeText = UInputRecordingFormatLibrary::FormatByteSize(Session.TotalBytes);
		Entry.LastUpdatedText = UInputRecordingFormatLibrary::FormatRelativeTime(Session.UpdatedUtc);
		Entry.DurationText = UInputRecordingFormatLibrary::FormatDurationClock(Session.DurationSeconds);
		Entry.CueCount = Session.CueCount;
		Entry.bPlayable = Session.IsPlayable();
		Entry.TotalBytes = Session.TotalBytes;

		TArray<FString, TInlineAllocator<3>> Contents;
		if (Session.bHasGhost)
		{
			Contents.Add(TEXT("ghost"));
		}
		if (Session.bHasVideo)
		{
			Contents.Add(TEXT("mp4"));
		}
		if (Session.bHasJson)
		{
			Contents.Add(TEXT("json"));
		}
		Entry.ContentsText = Contents.Num() > 0 ? FString::Join(Contents, TEXT(" + ")) : TEXT("empty");
	}

	return Entries;
}

void URecordingStore::LogInventory(const FString& Reason) const
{
	const FRecordingStoreStats Stats = GetStats();

	UE_LOG(LogRecordingStore, Log, TEXT("Inventory (%s): %d session(s), %s of %s used (%.0f%%), next index %d, root %s."),
		*Reason, Stats.SessionCount,
		*UInputRecordingFormatLibrary::FormatByteSize(Stats.TotalBytes),
		*UInputRecordingFormatLibrary::FormatByteSize(Stats.QuotaBytes),
		Stats.GetUsedFraction() * 100.0f, Stats.NextIndex, *ResolvedRoot);

	for (const FRecordingSessionInfo& Session : Sessions)
	{
		UE_LOG(LogRecordingStore, Verbose, TEXT("  %-14s %-24s %10s  updated %s%s"),
			*Session.FolderName, *Session.DisplayName,
			*UInputRecordingFormatLibrary::FormatByteSize(Session.TotalBytes),
			*UInputRecordingFormatLibrary::FormatRelativeTime(Session.UpdatedUtc),
			PinnedIndices.Contains(Session.Index) ? TEXT("  [pinned]") : TEXT(""));
	}
}
