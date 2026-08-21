// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "InputRecordingLog.h"
#include "Library/InputRecordingFormatLibrary.h"
#include "Settings/InputRecordingSettings.h"
#include "Store/RecordingStore.h"
#include "Subsystem/InputRecordingSubsystem.h"
#include "Video/InputRecordingVideoCapture.h"

/**
 * Console surface.
 *
 * These are module-level FAutoConsoleCommand objects rather than UFUNCTION(Exec) members on a
 * PlayerController. Exec functions only route through whatever class the console happens to
 * dispatch to, and stop working silently the moment that class changes. A console command object
 * registers once at module load and works from any world state, including before a
 * PlayerController exists at all.
 */
namespace InputRecordingConsole
{
	UInputRecordingSubsystem* GetSubsystem(UWorld* World)
	{
		// The fallback has to trigger on "this world has no game instance", not merely on "no
		// world was supplied". Typing an ir.* command into the editor's Output Log console while
		// PIE is running hands us the *editor* world, which is non-null but whose GetGameInstance
		// is always null - so guarding on (!World) alone made the command fail with "start play
		// first" at the exact moment play was already running.
		if (!World || !World->GetGameInstance())
		{
			World = GEngine ? GEngine->GetCurrentPlayWorld() : nullptr;
		}

		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		if (!GameInstance)
		{
			UE_LOG(LogInputRecording, Error, TEXT("No game instance; start play before using the ir.* commands."));
			return nullptr;
		}

		UInputRecordingSubsystem* Subsystem = GameInstance->GetSubsystem<UInputRecordingSubsystem>();
		if (!Subsystem)
		{
			UE_LOG(LogInputRecording, Error, TEXT("The input recording subsystem is not available."));
		}

		return Subsystem;
	}

	/** Console args arrive pre-split, so a quoted display name comes back in pieces. */
	FString JoinArgs(const TArray<FString>& Args)
	{
		FString Joined = FString::Join(Args, TEXT(" ")).TrimStartAndEnd();

		if (Joined.Len() >= 2 && Joined.StartsWith(TEXT("\"")) && Joined.EndsWith(TEXT("\"")))
		{
			Joined = Joined.Mid(1, Joined.Len() - 2);
		}

		return Joined;
	}

	void PrintSessionTable(URecordingStore& Store)
	{
		Store.Rescan();

		const TArray<FRecordingListEntry> Entries = Store.BuildListEntries();

		UE_LOG(LogRecordingStore, Display, TEXT("%-14s %-24s %10s  %-28s %8s %5s  %-18s %s"),
			TEXT("Folder"), TEXT("Display name"), TEXT("Size"), TEXT("Last updated"),
			TEXT("Length"), TEXT("Cues"), TEXT("Contents"), TEXT("Playable"));

		UE_LOG(LogRecordingStore, Display, TEXT("%s"), *FString::ChrN(130, TEXT('-')));

		for (const FRecordingListEntry& Entry : Entries)
		{
			UE_LOG(LogRecordingStore, Display, TEXT("%-14s %-24s %10s  %-28s %8s %5d  %-18s %s"),
				*Entry.FolderName, *Entry.DisplayName, *Entry.SizeText, *Entry.LastUpdatedText,
				*Entry.DurationText, Entry.CueCount, *Entry.ContentsText,
				Entry.bPlayable ? TEXT("yes") : TEXT("no"));
		}

		const FRecordingStoreStats Stats = Store.GetStats();
		UE_LOG(LogRecordingStore, Display, TEXT("%s"), *FString::ChrN(130, TEXT('-')));
		UE_LOG(LogRecordingStore, Display,
			TEXT("%d session(s)   %s used of %s quota (%.0f%%)   %s headroom remaining   root: %s"),
			Stats.SessionCount,
			*UInputRecordingFormatLibrary::FormatByteSize(Stats.TotalBytes),
			*UInputRecordingFormatLibrary::FormatByteSize(Stats.QuotaBytes),
			Stats.GetUsedFraction() * 100.0f,
			*UInputRecordingFormatLibrary::FormatByteSize(Stats.GetFreeBytes()),
			*Store.GetRoot());
	}
}

// -------------------------------------------------------------------------------------------
// Recording
// -------------------------------------------------------------------------------------------

static FAutoConsoleCommandWithWorldAndArgs GRecordStartCommand(
	TEXT("ir.record.start"),
	TEXT("Start a take and show the corner overlay. Optional display name: ir.record.start \"Jump tutorial\""),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (UInputRecordingSubsystem* Subsystem = InputRecordingConsole::GetSubsystem(World))
		{
			// Deliberately does NOT call ShowOverlay here. It used to, and StartRecording then
			// immediately hid the overlay again to keep it out of the capture - so a take that
			// started perfectly looked like it had done nothing at all. Overlay policy belongs to
			// StartRecording, which is the only code that knows whether video is being captured.
			Subsystem->StartRecording(InputRecordingConsole::JoinArgs(Args));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRecordStopCommand(
	TEXT("ir.record.stop"),
	TEXT("Stop the current take, save the session and hide the overlay."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (UInputRecordingSubsystem* Subsystem = InputRecordingConsole::GetSubsystem(World))
		{
			Subsystem->StopRecording();
			Subsystem->HideOverlay();
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRecordCancelCommand(
	TEXT("ir.record.cancel"),
	TEXT("Abandon the current take and delete its session folder."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (UInputRecordingSubsystem* Subsystem = InputRecordingConsole::GetSubsystem(World))
		{
			Subsystem->CancelRecording();
			Subsystem->HideOverlay();
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRecordTestCommand(
	TEXT("ir.record.test"),
	TEXT("Stop and save any take in progress, then open the review map. ")
	TEXT("With no argument this reviews the most recently updated playable session. ")
	TEXT("Otherwise pass a folder name (Recording_5), a bare index (5), or a display name."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (UInputRecordingSubsystem* Subsystem = InputRecordingConsole::GetSubsystem(World))
		{
			// The specifier goes through RunControlRecapTest so this path and -ControlRecap=<name>
			// resolve through exactly the same code.
			Subsystem->RunControlRecapTest(InputRecordingConsole::JoinArgs(Args));
		}
	}));

// -------------------------------------------------------------------------------------------
// Store
// -------------------------------------------------------------------------------------------

static FAutoConsoleCommandWithWorldAndArgs GStoreListCommand(
	TEXT("ir.store.list"),
	TEXT("Print every recording session as a table, followed by quota totals."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (UInputRecordingSubsystem* Subsystem = InputRecordingConsole::GetSubsystem(World))
		{
			if (URecordingStore* Store = Subsystem->GetStore())
			{
				InputRecordingConsole::PrintSessionTable(*Store);
			}
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GStoreListUiCommand(
	TEXT("ir.store.list.ui"),
	TEXT("Show the same session list in game. Rows are clickable and open that session for review."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (UInputRecordingSubsystem* Subsystem = InputRecordingConsole::GetSubsystem(World))
		{
			Subsystem->ShowRecordingList();
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GStoreTrimCommand(
	TEXT("ir.store.trim"),
	TEXT("Evict least-recently-updated sessions until the store is back under quota."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		UInputRecordingSubsystem* Subsystem = InputRecordingConsole::GetSubsystem(World);
		URecordingStore* Store = Subsystem ? Subsystem->GetStore() : nullptr;
		if (!Store)
		{
			return;
		}

		Store->Rescan();
		const int32 Evicted = Store->TrimToQuota(0);

		UE_LOG(LogRecordingStore, Display, TEXT("ir.store.trim evicted %d session(s)."), Evicted);
		InputRecordingConsole::PrintSessionTable(*Store);
	}));

// -------------------------------------------------------------------------------------------
// UI
// -------------------------------------------------------------------------------------------

static FAutoConsoleCommandWithWorldAndArgs GUiShowCommand(
	TEXT("ir.ui.show"),
	TEXT("Show the corner overlay without starting a take."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (UInputRecordingSubsystem* Subsystem = InputRecordingConsole::GetSubsystem(World))
		{
			Subsystem->ShowOverlay();
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GUiHideCommand(
	TEXT("ir.ui.hide"),
	TEXT("Hide the corner overlay."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (UInputRecordingSubsystem* Subsystem = InputRecordingConsole::GetSubsystem(World))
		{
			Subsystem->HideOverlay();
		}
	}));

// -------------------------------------------------------------------------------------------
// Video
// -------------------------------------------------------------------------------------------

static FAutoConsoleCommandWithWorldAndArgs GVideoDumpFrameCommand(
	TEXT("ir.video.dumpframe"),
	TEXT("Arm a one-shot PNG dump of the next captured frame, written beside the .mp4. ")
	TEXT("The PNG contains the exact bytes handed to the encoder - compare it against the video ")
	TEXT("to settle which way up this machine's encoder expects them."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		UInputRecordingSubsystem* Subsystem = InputRecordingConsole::GetSubsystem(World);
		UInputRecordingVideoCapture* Capture = Subsystem ? Subsystem->GetVideoCapture() : nullptr;
		if (!Capture)
		{
			return;
		}

		if (Capture->ArmFrameDump())
		{
			UE_LOG(LogRecordingVideo, Display, TEXT("Frame dump armed. The next captured frame will be written as a PNG."));
		}
	}));
