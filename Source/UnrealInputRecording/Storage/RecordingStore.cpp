// Copyright (c) Your Studio. All Rights Reserved.

#include "Storage/RecordingStore.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "InputReplay/InputReplayTypes.h"
#include "JsonObjectConverter.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Video/InputRecordingVideoTypes.h"

namespace
{
	/** Sums every file under a folder, including anything this system did not write. */
	class FFolderSizeVisitor final : public IPlatformFile::FDirectoryStatVisitor
	{
	public:
		int64 TotalBytes = 0;

		virtual bool Visit(const TCHAR* FilenameOrDirectory, const FFileStatData& StatData) override
		{
			if (!StatData.bIsDirectory)
			{
				TotalBytes += FMath::Max<int64>(0, StatData.FileSize);
			}
			return true;
		}
	};

	/** "ghost+json+mp4", or "empty" - what the log table prints in its contents column. */
	FString DescribeContents(const FRecordingSessionInfo& Session)
	{
		TArray<FString> Parts;
		if (Session.bHasGhost) { Parts.Add(TEXT("ghost")); }
		if (Session.bHasJson)  { Parts.Add(TEXT("json"));  }
		if (Session.bHasVideo) { Parts.Add(TEXT("mp4"));   }

		return Parts.Num() > 0 ? FString::Join(Parts, TEXT("+")) : FString(TEXT("empty"));
	}
}

// ---------------------------------------------------------------------------------------------
// Root resolution
// ---------------------------------------------------------------------------------------------

FString URecordingStore::ResolveRootDirectory()
{
	// An explicit override wins everywhere, including on console. This is how a QA run points at a
	// scratch drive without a rebuild, and how an automated test gets an empty store per run.
	FString Override;
	if (FParse::Value(FCommandLine::Get(), TEXT("RecordingRoot="), Override) && !Override.IsEmpty())
	{
		return FPaths::ConvertRelativePathToFull(Override);
	}

#if PLATFORM_DESKTOP
	const FString Base = FPaths::ProjectSavedDir();
#else
	// Console. See the header for why this is not ProjectSavedDir().
	const FString Base = FPaths::ProjectPersistentDownloadDir();
#endif

	return FPaths::ConvertRelativePathToFull(FPaths::Combine(Base, RecordingStore::RootFolderName));
}

// ---------------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------------

void URecordingStore::Initialize(int64 InQuotaBytes)
{
	QuotaBytes = FMath::Max<int64>(RecordingStore::BytesPerMegabyte, InQuotaBytes);
	RootDirectory = ResolveRootDirectory();

	IFileManager::Get().MakeDirectory(*RootDirectory, /*Tree=*/true);

	ReadIndexFile();
	MigrateLegacyLayout();
	Rescan();

	// The index file is authoritative, but a folder that outlives it - restored from a backup, copied
	// in by hand - must not have its name reused. Whichever is higher wins.
	for (const FRecordingSessionInfo& Session : Sessions)
	{
		NextIndex = FMath::Max(NextIndex, Session.Index + 1);
	}
	WriteIndexFile();

	bInitialised = true;

	LogInventory(TEXT("boot scan"));

	// A store that is already over quota - the setting was lowered, or files were copied in - gets
	// brought back into line before anything else runs.
	TrimToQuota(0);
}

void URecordingStore::Rescan()
{
	if (RootDirectory.IsEmpty())
	{
		RootDirectory = ResolveRootDirectory();
	}

	Sessions.Reset();

	TArray<FString> FolderNames;
	IFileManager::Get().FindFiles(FolderNames, *(RootDirectory / TEXT("*")), /*Files=*/false, /*Directories=*/true);

	for (const FString& FolderName : FolderNames)
	{
		FRecordingSessionInfo Session;
		if (ScanSession(FolderName, Session))
		{
			Sessions.Add(MoveTemp(Session));
		}
	}

	Sessions.Sort([](const FRecordingSessionInfo& A, const FRecordingSessionInfo& B)
	{
		return A.Index < B.Index;
	});
}

bool URecordingStore::ScanSession(const FString& FolderName, FRecordingSessionInfo& OutSession) const
{
	const int32 Index = FRecordingSessionInfo::ParseFolderName(FolderName);
	if (Index == INDEX_NONE)
	{
		// Some other folder living in the recording root. Not ours, not counted, not deleted.
		return false;
	}

	OutSession = FRecordingSessionInfo();
	OutSession.Index = Index;
	OutSession.FolderName = FolderName;
	OutSession.AbsolutePath = FPaths::Combine(RootDirectory, FolderName);

	IFileManager& FileManager = IFileManager::Get();

	OutSession.bHasGhost = FileManager.FileExists(*OutSession.GetGhostPath());
	OutSession.bHasJson  = FileManager.FileExists(*OutSession.GetJsonPath());
	OutSession.bHasVideo = FileManager.FileExists(*OutSession.GetVideoPath());
	OutSession.TotalBytes = ComputeFolderBytes(OutSession.AbsolutePath);

	// Filesystem timestamps first, so a session with no manifest is still fully ordered for LRU. The
	// manifest then overwrites what it legitimately knows.
	const FString GhostPath = OutSession.GetGhostPath();
	const FDateTime FileTime = OutSession.bHasGhost
		? FileManager.GetTimeStamp(*GhostPath)
		: FileManager.GetTimeStamp(*OutSession.AbsolutePath);

	OutSession.CreatedUtc = FileTime;
	OutSession.UpdatedUtc = FileTime;
	OutSession.DisplayName = FolderName;

	FRecordingSessionInfo Manifest;
	if (ReadManifest(OutSession.GetManifestPath(), Manifest))
	{
		// Only the fields the folder itself cannot answer. Sizes and file flags stay as scanned, so a
		// manifest left behind by a half-written take cannot claim files that are not there.
		OutSession.DisplayName     = Manifest.DisplayName.IsEmpty() ? OutSession.DisplayName : Manifest.DisplayName;
		OutSession.DurationSeconds = Manifest.DurationSeconds;
		OutSession.CueCount        = Manifest.CueCount;
		OutSession.MapName         = Manifest.MapName;

		if (Manifest.CreatedUtc.GetTicks() > 0) { OutSession.CreatedUtc = Manifest.CreatedUtc; }
		if (Manifest.UpdatedUtc.GetTicks() > 0) { OutSession.UpdatedUtc = Manifest.UpdatedUtc; }
	}

	return true;
}

int64 URecordingStore::ComputeFolderBytes(const FString& FolderPath)
{
	FFolderSizeVisitor Visitor;
	FPlatformFileManager::Get().GetPlatformFile().IterateDirectoryStatRecursively(*FolderPath, Visitor);
	return Visitor.TotalBytes;
}

// ---------------------------------------------------------------------------------------------
// Manifest and index files
// ---------------------------------------------------------------------------------------------

bool URecordingStore::WriteManifest(const FRecordingSessionInfo& Session) const
{
	const FString ManifestPath = Session.GetManifestPath();
	if (ManifestPath.IsEmpty())
	{
		return false;
	}

	FString Json;
	if (!FJsonObjectConverter::UStructToJsonObjectString(Session, Json))
	{
		UE_LOG(LogRecordingStore, Warning, TEXT("Could not serialise the manifest for %s."), *Session.FolderName);
		return false;
	}

	if (!FFileHelper::SaveStringToFile(Json, *ManifestPath))
	{
		UE_LOG(LogRecordingStore, Warning, TEXT("Could not write '%s'."), *ManifestPath);
		return false;
	}

	return true;
}

bool URecordingStore::ReadManifest(const FString& ManifestPath, FRecordingSessionInfo& OutSession) const
{
	if (ManifestPath.IsEmpty() || !IFileManager::Get().FileExists(*ManifestPath))
	{
		return false;
	}

	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *ManifestPath))
	{
		return false;
	}

	// A corrupt manifest is not an error worth stopping for: everything in it can be re-derived from
	// the folder, and the next commit rewrites it. Say so once at Verbose and move on.
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &OutSession, /*CheckFlags=*/0, /*SkipFlags=*/0))
	{
		UE_LOG(LogRecordingStore, Verbose,
			TEXT("Ignoring unreadable manifest '%s'; falling back to the folder contents."), *ManifestPath);
		return false;
	}

	return true;
}

void URecordingStore::ReadIndexFile()
{
	const FString IndexPath = FPaths::Combine(RootDirectory, RecordingStore::IndexFileName);

	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *IndexPath))
	{
		return;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return;
	}

	int32 Stored = 1;
	if (Root->TryGetNumberField(TEXT("NextIndex"), Stored))
	{
		NextIndex = FMath::Max(1, Stored);
	}
}

void URecordingStore::WriteIndexFile() const
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("NextIndex"), NextIndex);

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Root, Writer);

	const FString IndexPath = FPaths::Combine(RootDirectory, RecordingStore::IndexFileName);
	FFileHelper::SaveStringToFile(Json, *IndexPath);
}

// ---------------------------------------------------------------------------------------------
// Legacy migration
// ---------------------------------------------------------------------------------------------

void URecordingStore::MigrateLegacyLayout()
{
	// The legacy directory always lived under Saved, even on platforms where the store itself now
	// does not, because that is where the old build wrote it.
	const FString LegacyDir = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), RecordingStore::LegacyFolderName));

	IFileManager& FileManager = IFileManager::Get();
	if (!FileManager.DirectoryExists(*LegacyDir))
	{
		return;
	}

	TArray<FString> GhostFiles;
	FileManager.FindFiles(GhostFiles, *(LegacyDir / TEXT("*") + InputReplay::BinaryExtension), /*Files=*/true, /*Directories=*/false);

	if (GhostFiles.Num() == 0)
	{
		return;
	}

	UE_LOG(LogRecordingStore, Log, TEXT("Migrating %d recording(s) from the flat layout in '%s'."),
		GhostFiles.Num(), *LegacyDir);

	for (const FString& GhostFile : GhostFiles)
	{
		FString BareName = GhostFile;
		BareName.RemoveFromEnd(InputReplay::BinaryExtension);

		const int32 Index = NextIndex++;
		const FString FolderName = FRecordingSessionInfo::MakeFolderName(Index);
		const FString FolderPath = FPaths::Combine(RootDirectory, FolderName);

		if (!FileManager.MakeDirectory(*FolderPath, /*Tree=*/true))
		{
			UE_LOG(LogRecordingStore, Error, TEXT("Could not create '%s'; leaving '%s' where it is."),
				*FolderPath, *BareName);
			--NextIndex;
			continue;
		}

		// Each of the three is optional except the .ghost, which is why the trio is keyed off it.
		const TCHAR* Extensions[] = {
			InputReplay::BinaryExtension,
			InputReplay::JsonExtension,
			InputRecordingVideo::VideoExtension
		};

		int32 MovedCount = 0;
		for (const TCHAR* Extension : Extensions)
		{
			const FString From = FPaths::Combine(LegacyDir, BareName + Extension);
			const FString To   = FPaths::Combine(FolderPath, FolderName + Extension);

			if (!FileManager.FileExists(*From))
			{
				continue;
			}

			if (FileManager.Move(*To, *From, /*bReplace=*/true))
			{
				++MovedCount;
			}
			else
			{
				UE_LOG(LogRecordingStore, Warning, TEXT("Could not move '%s' to '%s'."), *From, *To);
			}
		}

		FRecordingSessionInfo Migrated;
		Migrated.Index = Index;
		Migrated.FolderName = FolderName;
		Migrated.AbsolutePath = FolderPath;

		// The original name is the only thing the flat layout carried that the folder name loses, so
		// it becomes the display name rather than being thrown away.
		Migrated.DisplayName = BareName;
		Migrated.CreatedUtc = FileManager.GetTimeStamp(*Migrated.GetGhostPath());
		Migrated.UpdatedUtc = Migrated.CreatedUtc;

		WriteManifest(Migrated);

		UE_LOG(LogRecordingStore, Log, TEXT("  '%s' -> %s (%d file(s))"), *BareName, *FolderName, MovedCount);
	}

	WriteIndexFile();
}

// ---------------------------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------------------------

FRecordingStoreStats URecordingStore::GetStats() const
{
	FRecordingStoreStats Stats;
	Stats.SessionCount = Sessions.Num();
	Stats.TotalBytes = GetTotalBytes();
	Stats.QuotaBytes = QuotaBytes;
	Stats.NextIndex = NextIndex;
	return Stats;
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
	return GetTotalBytes() + FMath::Max<int64>(0, ExtraBytes) <= QuotaBytes;
}

bool URecordingStore::FindSession(int32 Index, FRecordingSessionInfo& OutSession) const
{
	if (const FRecordingSessionInfo* Found = Sessions.FindByPredicate(
		[Index](const FRecordingSessionInfo& Session) { return Session.Index == Index; }))
	{
		OutSession = *Found;
		return true;
	}
	return false;
}

bool URecordingStore::FindSessionByFolder(const FString& FolderName, FRecordingSessionInfo& OutSession) const
{
	return FindSession(FRecordingSessionInfo::ParseFolderName(FolderName), OutSession);
}

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

// ---------------------------------------------------------------------------------------------
// Session lifecycle
// ---------------------------------------------------------------------------------------------

FRecordingSessionInfo URecordingStore::BeginSession(const FString& DisplayName, const FString& MapName, int64 ReserveBytes)
{
	if (!bInitialised)
	{
		Initialize(QuotaBytes);
	}

	const int64 Reservation = FMath::Max<int64>(0, ReserveBytes);

	// Make room before claiming an index, so a refusal costs nothing and leaves no gap in the numbering.
	TrimToQuota(Reservation);

	if (!HasHeadroom(Reservation))
	{
		UE_LOG(LogRecordingStore, Error,
			TEXT("Refusing to start a take: %s needed, %s free of %s, and everything else is pinned ")
			TEXT("or in use. Stop the active session or raise the quota."),
			*RecordingStore::FormatBytes(Reservation),
			*RecordingStore::FormatBytes(GetStats().GetFreeBytes()),
			*RecordingStore::FormatBytes(QuotaBytes));

		return FRecordingSessionInfo();
	}

	const int32 Index = NextIndex;
	const FString FolderName = FRecordingSessionInfo::MakeFolderName(Index);
	const FString FolderPath = FPaths::Combine(RootDirectory, FolderName);

	if (!IFileManager::Get().MakeDirectory(*FolderPath, /*Tree=*/true))
	{
		UE_LOG(LogRecordingStore, Error, TEXT("Could not create the session folder '%s'."), *FolderPath);
		return FRecordingSessionInfo();
	}

	++NextIndex;
	WriteIndexFile();

	FRecordingSessionInfo Session;
	Session.Index = Index;
	Session.FolderName = FolderName;
	Session.AbsolutePath = FolderPath;
	Session.DisplayName = DisplayName.IsEmpty() ? FolderName : DisplayName;
	Session.MapName = MapName;
	Session.CreatedUtc = FDateTime::UtcNow();
	Session.UpdatedUtc = Session.CreatedUtc;

	Sessions.Add(Session);
	PinSession(Index);

	// Written up front so an interrupted take - a crash, a pulled cable - still leaves a folder that
	// scans as a real session rather than as an anonymous directory.
	WriteManifest(Session);

	UE_LOG(LogRecordingStore, Log, TEXT("Session %s opened, reserving %s. %s free of %s."),
		*FolderName,
		*RecordingStore::FormatBytes(Reservation),
		*RecordingStore::FormatBytes(GetStats().GetFreeBytes()),
		*RecordingStore::FormatBytes(QuotaBytes));

	return Session;
}

bool URecordingStore::CommitSession(int32 Index, float DurationSeconds, int32 CueCount)
{
	FRecordingSessionInfo* Session = Sessions.FindByPredicate(
		[Index](const FRecordingSessionInfo& Candidate) { return Candidate.Index == Index; });

	if (!Session)
	{
		UE_LOG(LogRecordingStore, Warning, TEXT("CommitSession: no session with index %d."), Index);
		return false;
	}

	// Re-scan rather than trust: the encoder and the serializer both wrote here after BeginSession,
	// and the sizes they produced are the whole basis of the quota accounting.
	FRecordingSessionInfo Rescanned;
	if (ScanSession(Session->FolderName, Rescanned))
	{
		Rescanned.DisplayName = Session->DisplayName;
		Rescanned.MapName = Session->MapName;
		Rescanned.CreatedUtc = Session->CreatedUtc;
		*Session = Rescanned;
	}

	Session->DurationSeconds = DurationSeconds;
	Session->CueCount = CueCount;
	Session->UpdatedUtc = FDateTime::UtcNow();

	WriteManifest(*Session);

	UE_LOG(LogRecordingStore, Log, TEXT("Session %s committed: %s, %.1fs, %d cue(s), [%s]."),
		*Session->FolderName,
		*RecordingStore::FormatBytes(Session->TotalBytes),
		DurationSeconds, CueCount, *DescribeContents(*Session));

	UnpinSession(Index);
	TrimToQuota(0);

	return true;
}

void URecordingStore::AbortSession(int32 Index)
{
	FRecordingSessionInfo Session;
	if (!FindSession(Index, Session))
	{
		return;
	}

	UnpinSession(Index);

	if (IFileManager::Get().DeleteDirectory(*Session.AbsolutePath, /*RequireExists=*/false, /*Tree=*/true))
	{
		UE_LOG(LogRecordingStore, Log, TEXT("Session %s aborted and removed (%s reclaimed)."),
			*Session.FolderName, *RecordingStore::FormatBytes(Session.TotalBytes));
	}
	else
	{
		UE_LOG(LogRecordingStore, Warning, TEXT("Could not remove the aborted session folder '%s'."),
			*Session.AbsolutePath);
	}

	Sessions.RemoveAll([Index](const FRecordingSessionInfo& Candidate) { return Candidate.Index == Index; });
}

void URecordingStore::TouchSession(int32 Index)
{
	FRecordingSessionInfo* Session = Sessions.FindByPredicate(
		[Index](const FRecordingSessionInfo& Candidate) { return Candidate.Index == Index; });

	if (!Session)
	{
		return;
	}

	Session->UpdatedUtc = FDateTime::UtcNow();
	WriteManifest(*Session);

	UE_LOG(LogRecordingStore, Verbose, TEXT("Session %s touched; it is now the most recent."),
		*Session->FolderName);
}

// ---------------------------------------------------------------------------------------------
// Quota
// ---------------------------------------------------------------------------------------------

TArray<int32> URecordingStore::BuildEvictionOrder() const
{
	TArray<int32> Order;
	Order.Reserve(Sessions.Num());

	for (const FRecordingSessionInfo& Session : Sessions)
	{
		if (!PinnedIndices.Contains(Session.Index))
		{
			Order.Add(Session.Index);
		}
	}

	// Oldest update first. Ties break on index so the order is stable run to run, which matters when
	// a migration stamps several sessions with the same timestamp.
	Order.Sort([this](int32 A, int32 B)
	{
		const FRecordingSessionInfo* SessionA = Sessions.FindByPredicate([A](const FRecordingSessionInfo& S) { return S.Index == A; });
		const FRecordingSessionInfo* SessionB = Sessions.FindByPredicate([B](const FRecordingSessionInfo& S) { return S.Index == B; });

		if (!SessionA || !SessionB)
		{
			return A < B;
		}

		if (SessionA->UpdatedUtc == SessionB->UpdatedUtc)
		{
			return SessionA->Index < SessionB->Index;
		}

		return SessionA->UpdatedUtc < SessionB->UpdatedUtc;
	});

	return Order;
}

int32 URecordingStore::TrimToQuota(int64 ExtraReservation)
{
	const int64 Reservation = FMath::Max<int64>(0, ExtraReservation);

	if (GetTotalBytes() + Reservation <= QuotaBytes)
	{
		return 0;
	}

	const int64 StartingBytes = GetTotalBytes();
	int32 EvictedCount = 0;

	for (const int32 Index : BuildEvictionOrder())
	{
		if (GetTotalBytes() + Reservation <= QuotaBytes)
		{
			break;
		}

		FRecordingSessionInfo Session;
		if (!FindSession(Index, Session))
		{
			continue;
		}

		if (!IFileManager::Get().DeleteDirectory(*Session.AbsolutePath, /*RequireExists=*/false, /*Tree=*/true))
		{
			UE_LOG(LogRecordingStore, Warning,
				TEXT("Could not evict '%s'; it still counts against the quota."), *Session.AbsolutePath);
			continue;
		}

		UE_LOG(LogRecordingStore, Log, TEXT("Evicted %s - %s, last updated %s."),
			*Session.FolderName,
			*RecordingStore::FormatBytes(Session.TotalBytes),
			*Session.UpdatedUtc.ToString(TEXT("%Y-%m-%d %H:%M")));

		Sessions.RemoveAll([Index](const FRecordingSessionInfo& Candidate) { return Candidate.Index == Index; });
		++EvictedCount;
	}

	if (EvictedCount > 0)
	{
		UE_LOG(LogRecordingStore, Log, TEXT("Quota trim freed %s across %d session(s); now at %s of %s."),
			*RecordingStore::FormatBytes(StartingBytes - GetTotalBytes()),
			EvictedCount,
			*RecordingStore::FormatBytes(GetTotalBytes()),
			*RecordingStore::FormatBytes(QuotaBytes));
	}
	else if (GetTotalBytes() + Reservation > QuotaBytes)
	{
		UE_LOG(LogRecordingStore, Warning,
			TEXT("Over quota at %s of %s and nothing can be evicted - every session is pinned or in use."),
			*RecordingStore::FormatBytes(GetTotalBytes()),
			*RecordingStore::FormatBytes(QuotaBytes));
	}

	return EvictedCount;
}

void URecordingStore::PinSession(int32 Index)
{
	if (Index >= 0)
	{
		PinnedIndices.Add(Index);
	}
}

void URecordingStore::UnpinSession(int32 Index)
{
	PinnedIndices.Remove(Index);
}

// ---------------------------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------------------------

void URecordingStore::LogInventory(const TCHAR* Reason) const
{
	const FRecordingStoreStats Stats = GetStats();

	UE_LOG(LogRecordingStore, Log, TEXT("=== Recording store: %s ==="), Reason ? Reason : TEXT("inventory"));
	UE_LOG(LogRecordingStore, Log, TEXT("  root  %s"), *RootDirectory);

	if (Sessions.Num() == 0)
	{
		UE_LOG(LogRecordingStore, Log, TEXT("  (empty) - next take will be %s"),
			*FRecordingSessionInfo::MakeFolderName(NextIndex));
		return;
	}

	for (const FRecordingSessionInfo& Session : Sessions)
	{
		// Left-padded size and a fixed-width name keep the columns aligned in the output log, which is
		// the entire reason this is a table and not one Printf per field.
		UE_LOG(LogRecordingStore, Log, TEXT("  %-16s %10s  %s  [%s]%s"),
			*Session.FolderName,
			*RecordingStore::FormatBytes(Session.TotalBytes),
			*Session.UpdatedUtc.ToString(TEXT("%Y-%m-%d %H:%M")),
			*DescribeContents(Session),
			PinnedIndices.Contains(Session.Index) ? TEXT(" pinned") : TEXT(""));
	}

	UE_LOG(LogRecordingStore, Log, TEXT("  --- %d session(s), %s of %s (%.0f%%), next take is %s"),
		Stats.SessionCount,
		*RecordingStore::FormatBytes(Stats.TotalBytes),
		*RecordingStore::FormatBytes(Stats.QuotaBytes),
		Stats.GetUsedFraction() * 100.f,
		*FRecordingSessionInfo::MakeFolderName(NextIndex));
}
