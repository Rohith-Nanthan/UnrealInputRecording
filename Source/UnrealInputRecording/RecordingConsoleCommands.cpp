// Copyright (c) Your Studio. All Rights Reserved.
//
// RecordingConsoleCommands.cpp
//
// The ir.* console surface.
//
// These are FAutoConsoleCommand objects registered by the module rather than UFUNCTION(Exec) members,
// and that difference matters. Exec functions only reach the console when they live on an object the
// console happens to route to - a PlayerController of the right class, a cheat manager, the game mode.
// AReplayPlayerController's existing Replay* commands work exactly as long as the project uses that
// controller class, and silently vanish the moment it does not, which is a miserable way to lose your
// debugging surface halfway through a bring-up.
//
// A console command object is registered once, by the module, and is reachable from anywhere for the
// life of the process. The old Exec functions still work; they simply forward here now.

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "InputRecordingSubsystem.h"
#include "Storage/RecordingSessionTypes.h"
#include "Storage/RecordingStore.h"

namespace
{
	/**
	 * Resolves the subsystem from whatever world the console handed us.
	 *
	 * Logs rather than returning silently, because "the command did nothing" is the single most
	 * confusing failure a console command can have.
	 */
	UInputRecordingSubsystem* GetSubsystem(UWorld* World)
	{
		if (!World)
		{
			// The console gives a null world when it fires before a map is up, or from a commandlet.
			// Fall back to any game world the engine knows about.
			if (GEngine)
			{
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					if (Context.World() && Context.WorldType == EWorldType::Game)
					{
						World = Context.World();
						break;
					}
				}
			}
		}

		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		if (!GameInstance)
		{
			UE_LOG(LogRecordingStore, Warning,
				TEXT("No game instance yet - run this once the game is playing."));
			return nullptr;
		}

		return GameInstance->GetSubsystem<UInputRecordingSubsystem>();
	}

	FString JoinArgs(const TArray<FString>& Args)
	{
		return Args.Num() > 0 ? FString::Join(Args, TEXT(" ")) : FString();
	}
}

// ---------------------------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------------------------

static FAutoConsoleCommandWithWorldAndArgs GRecordStartCommand(
	TEXT("ir.record.start"),
	TEXT("Start a recording and show the recording controller. Optional argument is a display name."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (UInputRecordingSubsystem* Subsystem = GetSubsystem(World))
		{
			const FString DisplayName = JoinArgs(Args);
			Subsystem->StartRecording(DisplayName.IsEmpty() ? TEXT("Console take") : DisplayName);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRecordStopCommand(
	TEXT("ir.record.stop"),
	TEXT("Stop recording, save the session, and hide the recording controller."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (UInputRecordingSubsystem* Subsystem = GetSubsystem(World))
		{
			Subsystem->StopRecording();
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRecordTestCommand(
	TEXT("ir.record.test"),
	TEXT("Stop and save the current take, then open the control recap map for it."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (UInputRecordingSubsystem* Subsystem = GetSubsystem(World))
		{
			Subsystem->RunControlRecapTest();
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRecordCancelCommand(
	TEXT("ir.record.cancel"),
	TEXT("Abandon the current take and delete its session folder."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (UInputRecordingSubsystem* Subsystem = GetSubsystem(World))
		{
			Subsystem->CancelRecording();
		}
	}));

// ---------------------------------------------------------------------------------------------
// Store
// ---------------------------------------------------------------------------------------------

static FAutoConsoleCommandWithWorldAndArgs GStoreListCommand(
	TEXT("ir.store.list"),
	TEXT("Print every recording session, its size, and the quota headroom."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (UInputRecordingSubsystem* Subsystem = GetSubsystem(World))
		{
			if (URecordingStore* Store = Subsystem->GetRecordingStore())
			{
				Store->Rescan();
				Store->LogInventory(TEXT("ir.store.list"));
			}
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GStoreTrimCommand(
	TEXT("ir.store.trim"),
	TEXT("Evict least-recently-updated sessions until the store is back under quota."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (UInputRecordingSubsystem* Subsystem = GetSubsystem(World))
		{
			if (URecordingStore* Store = Subsystem->GetRecordingStore())
			{
				Store->Rescan();

				const int32 Evicted = Store->TrimToQuota(0);
				UE_LOG(LogRecordingStore, Log, TEXT("ir.store.trim evicted %d session(s)."), Evicted);

				Store->LogInventory(TEXT("after trim"));
			}
		}
	}));

// ---------------------------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------------------------

static FAutoConsoleCommandWithWorldAndArgs GUiShowCommand(
	TEXT("ir.ui.show"),
	TEXT("Show the recording controller overlay without starting a recording."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (UInputRecordingSubsystem* Subsystem = GetSubsystem(World))
		{
			Subsystem->ShowRecordingController();
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GUiHideCommand(
	TEXT("ir.ui.hide"),
	TEXT("Hide the recording controller overlay."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (UInputRecordingSubsystem* Subsystem = GetSubsystem(World))
		{
			Subsystem->HideRecordingController();
		}
	}));

// ---------------------------------------------------------------------------------------------
// Video
// ---------------------------------------------------------------------------------------------

static FAutoConsoleCommandWithWorldAndArgs GVideoDumpFrameCommand(
	TEXT("ir.video.dumpframe"),
	TEXT("Write the first frame of the next capture to a PNG beside the .mp4, to check orientation."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UInputRecordingSubsystem* Subsystem = GetSubsystem(World);
		if (!Subsystem)
		{
			return;
		}

		Subsystem->RequestVideoFrameDump();

		UE_LOG(LogRecordingStore, Log,
			TEXT("The next take will dump its first frame as a PNG next to the .mp4. Start a ")
			TEXT("recording, then compare the PNG against what was on screen."));
	}));
