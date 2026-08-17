// Copyright (c) Your Studio. All Rights Reserved.

#include "InputRecordingSubsystem.h"

#include "EngineUtils.h"                            // TActorIterator
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputRecordingSettings.h"
#include "InputReplay/InputMatchCue.h"              // LogInputMatch
#include "InputReplay/InputRecordingAssetTools.h"
#include "InputReplay/InputRecordingDataAsset.h"
#include "InputReplay/InputReplaySerializer.h"

// ---------------------------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------------------------

void UInputRecordingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (const UInputRecordingSettings* Settings = UInputRecordingSettings::Get())
	{
		ActiveRecordingName = Settings->DefaultRecordingName;
	}

	// No component resolution here on purpose: a GameInstance subsystem initialises before there is
	// a world or a local player. Resolution is lazy, on first use.
	UE_LOG(LogInputReplay, Log, TEXT("InputRecordingSubsystem ready (default recording '%s')."), *ActiveRecordingName);
}

void UInputRecordingSubsystem::Deinitialize()
{
	if (UInputReplayComponent* Component = CachedReplayComponent.Get())
	{
		UnbindFromComponent(Component);
	}
	CachedReplayComponent.Reset();

	Super::Deinitialize();
}

// ---------------------------------------------------------------------------------------------
// Component discovery
// ---------------------------------------------------------------------------------------------

UInputReplayComponent* UInputRecordingSubsystem::FindReplayComponent() const
{
	// 1. The one we already know about, if its owner is still alive.
	if (UInputReplayComponent* Cached = CachedReplayComponent.Get())
	{
		const AActor* Owner = Cached->GetOwner();
		if (IsValid(Owner) && !Owner->IsActorBeingDestroyed())
		{
			return Cached;
		}
	}

	const UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		return nullptr;
	}

	// 2. The PlayerController - the correct home for the component, because that is where the
	//    PreProcessInput / PostProcessInput hooks live (see AReplayPlayerController).
	APlayerController* PlayerController = GameInstance->GetFirstLocalPlayerController();
	if (PlayerController)
	{
		if (UInputReplayComponent* OnController = PlayerController->FindComponentByClass<UInputReplayComponent>())
		{
			return OnController;
		}

		// 3. The pawn - supported because the component's GetOwningPlayerController() walks up from
		//    a pawn owner, though input hooks will fall back to TickComponent.
		if (APawn* Pawn = PlayerController->GetPawn())
		{
			if (UInputReplayComponent* OnPawn = Pawn->FindComponentByClass<UInputReplayComponent>())
			{
				return OnPawn;
			}
		}
	}

	// 4. Anywhere in the world. Covers a dedicated "replay manager" actor placed in the level.
	if (UWorld* World = GameInstance->GetWorld())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (UInputReplayComponent* Found = It->FindComponentByClass<UInputReplayComponent>())
			{
				return Found;
			}
		}
	}

	return nullptr;
}

UInputReplayComponent* UInputRecordingSubsystem::GetReplayComponent()
{
	UInputReplayComponent* Component = FindReplayComponent();

	// 5. Nothing out there - make one, if the project allows it.
	if (!Component)
	{
		const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();
		const bool bMayCreate = !Settings || Settings->bAutoCreateReplayComponent;

		if (bMayCreate)
		{
			if (const UGameInstance* GameInstance = GetGameInstance())
			{
				Component = CreateReplayComponentOn(GameInstance->GetFirstLocalPlayerController());
			}
		}
	}

	if (Component && Component != CachedReplayComponent.Get())
	{
		if (UInputReplayComponent* Previous = CachedReplayComponent.Get())
		{
			UnbindFromComponent(Previous);
		}

		CachedReplayComponent = Component;
		BindToComponent(Component);

		// Project defaults must not stomp a component a designer configured on the controller, so
		// non-created components only get the fields they left empty.
		if (const UInputRecordingSettings* Settings = UInputRecordingSettings::Get())
		{
			Settings->ApplyDefaultsTo(Component, /*bForce=*/false);
		}
	}

	if (!Component)
	{
		UE_LOG(LogInputReplay, Error,
			TEXT("No UInputReplayComponent found. Use AReplayPlayerController (or reparent your ")
			TEXT("PlayerController Blueprint to it), add the component to your pawn, or enable ")
			TEXT("'Auto Create Replay Component' in Project Settings > Game > Input Recording."));
	}

	return Component;
}

UInputReplayComponent* UInputRecordingSubsystem::CreateReplayComponentOn(APlayerController* PlayerController)
{
	if (!PlayerController)
	{
		return nullptr;
	}

	UInputReplayComponent* Component = NewObject<UInputReplayComponent>(
		PlayerController, UInputReplayComponent::StaticClass(), TEXT("InputReplayComponent_Runtime"));

	if (!Component)
	{
		return nullptr;
	}

	PlayerController->AddInstanceComponent(Component);
	Component->RegisterComponent();

	// A code-created component has no designer-authored setup, so the project settings are the only
	// place its recorded contexts can come from - force them on.
	if (const UInputRecordingSettings* Settings = UInputRecordingSettings::Get())
	{
		Settings->ApplyDefaultsTo(Component, /*bForce=*/true);
	}

	if (!bWarnedAboutAutoCreatedComponent)
	{
		bWarnedAboutAutoCreatedComponent = true;

		// Worth stating plainly: without the controller hooks the component runs from TickComponent,
		// which is fine for MatchInput (it only reads live input) but adds a frame of latency to
		// ghost playback.
		UE_LOG(LogInputReplay, Warning,
			TEXT("Created a UInputReplayComponent on '%s' at runtime. It will run from TickComponent ")
			TEXT("because this controller does not forward PreProcessInput/PostProcessInput. ")
			TEXT("MatchInput and recording work; for frame-accurate ghost playback, use ")
			TEXT("AReplayPlayerController instead."),
			*PlayerController->GetName());
	}

	UE_LOG(LogInputReplay, Log, TEXT("Auto-created UInputReplayComponent on '%s'."), *PlayerController->GetName());
	return Component;
}

void UInputRecordingSubsystem::SetReplayComponent(UInputReplayComponent* Component)
{
	if (Component == CachedReplayComponent.Get())
	{
		return;
	}

	if (UInputReplayComponent* Previous = CachedReplayComponent.Get())
	{
		UnbindFromComponent(Previous);
	}

	CachedReplayComponent = Component;

	if (Component)
	{
		BindToComponent(Component);
	}
}

void UInputRecordingSubsystem::BindToComponent(UInputReplayComponent* Component)
{
	if (!Component)
	{
		return;
	}

	// AddUniqueDynamic rather than AddDynamic: re-resolving the same component (a respawn that keeps
	// the controller, for instance) must not double up the relays.
	Component->OnRecordingStarted.AddUniqueDynamic(this, &UInputRecordingSubsystem::HandleRecordingStarted);
	Component->OnRecordingStopped.AddUniqueDynamic(this, &UInputRecordingSubsystem::HandleRecordingStopped);
	Component->OnPlaybackStarted.AddUniqueDynamic(this, &UInputRecordingSubsystem::HandlePlaybackStarted);
	Component->OnPlaybackFinished.AddUniqueDynamic(this, &UInputRecordingSubsystem::HandlePlaybackFinished);
	Component->OnMatchInputStarted.AddUniqueDynamic(this, &UInputRecordingSubsystem::HandleMatchInputStarted);
	Component->OnMatchInputCuePresented.AddUniqueDynamic(this, &UInputRecordingSubsystem::HandleMatchCuePresented);
	Component->OnMatchInputMatched.AddUniqueDynamic(this, &UInputRecordingSubsystem::HandleMatchInputMatched);
	Component->OnMatchInputMismatch.AddUniqueDynamic(this, &UInputRecordingSubsystem::HandleMatchInputMismatch);
	Component->OnMatchInputFinished.AddUniqueDynamic(this, &UInputRecordingSubsystem::HandleMatchInputFinished);
}

void UInputRecordingSubsystem::UnbindFromComponent(UInputReplayComponent* Component)
{
	if (!Component)
	{
		return;
	}

	Component->OnRecordingStarted.RemoveAll(this);
	Component->OnRecordingStopped.RemoveAll(this);
	Component->OnPlaybackStarted.RemoveAll(this);
	Component->OnPlaybackFinished.RemoveAll(this);
	Component->OnMatchInputStarted.RemoveAll(this);
	Component->OnMatchInputCuePresented.RemoveAll(this);
	Component->OnMatchInputMatched.RemoveAll(this);
	Component->OnMatchInputMismatch.RemoveAll(this);
	Component->OnMatchInputFinished.RemoveAll(this);
}

// ---------------------------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------------------------

FString UInputRecordingSubsystem::ResolveRecordingName(const FString& Requested) const
{
	if (!Requested.IsEmpty())
	{
		return Requested;
	}
	if (!ActiveRecordingName.IsEmpty())
	{
		return ActiveRecordingName;
	}

	const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();
	const FString Fallback = Settings ? Settings->DefaultRecordingName : FString();
	return Fallback.IsEmpty() ? TEXT("Recording01") : Fallback;
}

bool UInputRecordingSubsystem::StartRecording(const FString& DisplayName)
{
	UInputReplayComponent* Component = GetReplayComponent();
	if (!Component)
	{
		return false;
	}

	if (Component->GetMode() != EInputReplayMode::Idle)
	{
		UE_LOG(LogInputReplay, Warning, TEXT("StartRecording: already busy (mode=%d) - stopping first."),
			static_cast<int32>(Component->GetMode()));
		StopAll();
	}

	Component->StartRecording(DisplayName);

	const bool bStarted = Component->IsRecording();
	if (!bStarted)
	{
		// StartRecording only refuses for one reason: an empty action registry.
		UE_LOG(LogInputReplay, Error,
			TEXT("StartRecording failed - the component has no tracked actions. Assign Recorded ")
			TEXT("Contexts on the component, or set them in Project Settings > Game > Input Recording."));
	}

	BroadcastModeChanged();
	return bStarted;
}

bool UInputRecordingSubsystem::StopRecording()
{
	return StopRecordingAndSave(ActiveRecordingName, bUseJsonFormat);
}

bool UInputRecordingSubsystem::StopRecordingAndSave(const FString& FileName, bool bAsJson)
{
	UInputReplayComponent* Component = GetReplayComponent();
	if (!Component)
	{
		return false;
	}

	if (!Component->IsRecording())
	{
		UE_LOG(LogInputReplay, Warning, TEXT("StopRecordingAndSave: not recording."));
		return false;
	}

	Component->StopRecording();

	const FString Name = ResolveRecordingName(FileName);
	ActiveRecordingName = Name;

	const bool bSaved = Component->SaveRecordingToFile(Name, bAsJson);

	// Writing the readable companion copy by default is what makes the Data Asset workflow painless:
	// the binary stays authoritative for playback, the JSON is there to read and to import.
	const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();
	if (bSaved && !bAsJson && Settings && Settings->bAlsoExportJsonOnSave)
	{
		Component->SaveRecordingToFile(Name, /*bAsJson=*/true);
	}

	if (bSaved)
	{
		UE_LOG(LogInputReplay, Log, TEXT("Recording saved as '%s' in %s"),
			*Name, *UInputReplaySerializer::GetRecordingDirectory());
	}

	BroadcastModeChanged();
	return bSaved;
}

void UInputRecordingSubsystem::StopRecordingWithoutSaving()
{
	if (UInputReplayComponent* Component = FindReplayComponent())
	{
		Component->StopRecording();
		BroadcastModeChanged();
	}
}

// ---------------------------------------------------------------------------------------------
// MatchInput
// ---------------------------------------------------------------------------------------------

bool UInputRecordingSubsystem::StartMatchInputMode(const FString& FileName, bool bJson)
{
	UInputReplayComponent* Component = GetReplayComponent();
	if (!Component)
	{
		return false;
	}

	if (Component->GetMode() != EInputReplayMode::Idle)
	{
		StopAll();
	}

	const FString Name = ResolveRecordingName(FileName);
	const bool bPreferJson = bJson || bUseJsonFormat;

	if (!Component->LoadRecordingFromFile(Name, bPreferJson))
	{
		// Saving writes both formats by default, so try the other one before giving up rather than
		// reporting "not found" for a file that is sitting right there in the other format.
		if (!Component->LoadRecordingFromFile(Name, !bPreferJson))
		{
			UE_LOG(LogInputMatch, Error,
				TEXT("StartMatchInputMode: could not load recording '%s' from '%s'. ")
				TEXT("Record and save one first (console: InputReplay.ListRecordings)."),
				*Name, *UInputReplaySerializer::GetRecordingDirectory());
			return false;
		}
	}

	ActiveRecordingName = Name;
	MismatchCount = 0;
	LastMismatchDescription.Reset();

	const bool bStarted = Component->StartMatchInput();
	BroadcastModeChanged();
	return bStarted;
}

bool UInputRecordingSubsystem::StartMatchInputModeFromAsset(UInputRecordingDataAsset* RecordingAsset)
{
	if (!RecordingAsset || !RecordingAsset->HasValidRecording())
	{
		UE_LOG(LogInputMatch, Error,
			TEXT("StartMatchInputModeFromAsset: asset is null or has never been imported."));
		return false;
	}

	UInputReplayComponent* Component = GetReplayComponent();
	if (!Component)
	{
		return false;
	}

	if (Component->GetMode() != EInputReplayMode::Idle)
	{
		StopAll();
	}

	Component->SetRecording(RecordingAsset->BuildRecording());

	// Adopt the asset's cue tuning so the sequence the player is asked for is exactly the one
	// previewed in the asset's details panel.
	Component->MatchCueOptions = RecordingAsset->CueOptions;

	ActiveRecordingName = RecordingAsset->SourceFileName;
	MismatchCount = 0;
	LastMismatchDescription.Reset();

	const bool bStarted = Component->StartMatchInput();
	BroadcastModeChanged();
	return bStarted;
}

void UInputRecordingSubsystem::StopMatchInputMode()
{
	if (UInputReplayComponent* Component = FindReplayComponent())
	{
		Component->StopMatchInput();
		BroadcastModeChanged();
	}
}

// ---------------------------------------------------------------------------------------------
// Ghost playback
// ---------------------------------------------------------------------------------------------

bool UInputRecordingSubsystem::StartPlayback(const FString& FileName, bool bJson)
{
	UInputReplayComponent* Component = GetReplayComponent();
	if (!Component)
	{
		return false;
	}

	if (Component->GetMode() != EInputReplayMode::Idle)
	{
		StopAll();
	}

	const FString Name = ResolveRecordingName(FileName);
	const bool bPreferJson = bJson || bUseJsonFormat;

	if (!Component->LoadRecordingFromFile(Name, bPreferJson) &&
		!Component->LoadRecordingFromFile(Name, !bPreferJson))
	{
		UE_LOG(LogInputReplay, Error, TEXT("StartPlayback: could not load recording '%s'."), *Name);
		return false;
	}

	ActiveRecordingName = Name;

	const bool bStarted = Component->StartPlayback();
	BroadcastModeChanged();
	return bStarted;
}

void UInputRecordingSubsystem::StopPlayback()
{
	if (UInputReplayComponent* Component = FindReplayComponent())
	{
		Component->StopPlayback();
		BroadcastModeChanged();
	}
}

void UInputRecordingSubsystem::StopAll()
{
	UInputReplayComponent* Component = FindReplayComponent();
	if (!Component)
	{
		return;
	}

	switch (Component->GetMode())
	{
	case EInputReplayMode::Recording:
		// Stop *and save*: a recording the user has to re-perform because a button stopped it
		// without writing anything is the worst possible outcome here.
		StopRecordingAndSave(ActiveRecordingName, bUseJsonFormat);
		break;

	case EInputReplayMode::Playing:
		Component->StopPlayback();
		break;

	case EInputReplayMode::MatchInput:
		Component->StopMatchInput();
		break;

	default:
		break;
	}

	BroadcastModeChanged();
}

// ---------------------------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------------------------

EInputReplayMode UInputRecordingSubsystem::GetMode() const
{
	const UInputReplayComponent* Component = FindReplayComponent();
	return Component ? Component->GetMode() : EInputReplayMode::Idle;
}

bool UInputRecordingSubsystem::IsAwaitingMatchInput() const
{
	const UInputReplayComponent* Component = FindReplayComponent();
	return Component && Component->IsAwaitingMatchInput();
}

FString UInputRecordingSubsystem::GetExpectedInputDescription() const
{
	const UInputReplayComponent* Component = FindReplayComponent();
	return Component ? Component->GetExpectedInputDescription() : FString();
}

float UInputRecordingSubsystem::GetTimeUntilNextCue() const
{
	const UInputReplayComponent* Component = FindReplayComponent();
	return Component ? Component->GetTimeUntilNextCue() : 0.0f;
}

float UInputRecordingSubsystem::GetMatchProgress() const
{
	const UInputReplayComponent* Component = FindReplayComponent();
	return Component ? Component->GetMatchProgress() : 0.0f;
}

float UInputRecordingSubsystem::GetPlaybackProgress() const
{
	const UInputReplayComponent* Component = FindReplayComponent();
	return Component ? Component->GetPlaybackProgress() : 0.0f;
}

int32 UInputRecordingSubsystem::GetMatchCueCount() const
{
	const UInputReplayComponent* Component = FindReplayComponent();
	return Component ? Component->GetMatchCueCount() : 0;
}

int32 UInputRecordingSubsystem::GetCurrentMatchCueIndex() const
{
	const UInputReplayComponent* Component = FindReplayComponent();
	return Component ? Component->GetCurrentMatchCueIndex() : 0;
}

TArray<FString> UInputRecordingSubsystem::GetAvailableRecordings(bool bJson) const
{
	return UInputReplaySerializer::ListRecordings(bJson);
}

FString UInputRecordingSubsystem::GetStatusText() const
{
	const UInputReplayComponent* Component = FindReplayComponent();
	if (!Component)
	{
		return TEXT("No replay component");
	}

	switch (Component->GetMode())
	{
	case EInputReplayMode::Recording:
		return FString::Printf(TEXT("Recording '%s' - tick %d"),
			*ActiveRecordingName, Component->GetCurrentFrameIndex());

	case EInputReplayMode::Playing:
		return FString::Printf(TEXT("Replaying '%s' - %.0f%%"),
			*ActiveRecordingName, Component->GetPlaybackProgress() * 100.0f);

	case EInputReplayMode::MatchInput:
		if (Component->IsAwaitingMatchInput())
		{
			return FString::Printf(TEXT("Match Input %d/%d - press %s"),
				Component->GetCurrentMatchCueIndex() + 1, Component->GetMatchCueCount(),
				*Component->GetExpectedInputDescription());
		}
		return FString::Printf(TEXT("Match Input %d/%d - next cue in %.1fs"),
			Component->GetCurrentMatchCueIndex() + 1, Component->GetMatchCueCount(),
			Component->GetTimeUntilNextCue());

	default:
		return FString::Printf(TEXT("Idle - active recording '%s'"), *ActiveRecordingName);
	}
}

// ---------------------------------------------------------------------------------------------
// Editor tooling
// ---------------------------------------------------------------------------------------------

UInputRecordingDataAsset* UInputRecordingSubsystem::GenerateDataAssetFromFile(const FString& FileName, bool bJson)
{
	const UInputRecordingSettings* Settings = UInputRecordingSettings::Get();
	const FString Destination = (Settings && !Settings->DataAssetPackagePath.IsEmpty())
		? Settings->DataAssetPackagePath
		: FString(UInputRecordingAssetTools::DefaultPackagePath);

	return UInputRecordingAssetTools::GenerateRecordingDataAsset(
		ResolveRecordingName(FileName), bJson, Destination, TEXT(""), /*bOpenInEditor=*/true);
}

UInputRecordingDataAsset* UInputRecordingSubsystem::GenerateDataAssetFromLastRecording()
{
	return GenerateDataAssetFromFile(ActiveRecordingName, bUseJsonFormat);
}

// ---------------------------------------------------------------------------------------------
// Component event relays
// ---------------------------------------------------------------------------------------------

void UInputRecordingSubsystem::BroadcastModeChanged()
{
	const EInputReplayMode Mode = GetMode();
	if (Mode == LastBroadcastMode)
	{
		return;
	}

	LastBroadcastMode = Mode;
	OnModeChanged.Broadcast(Mode);
}

void UInputRecordingSubsystem::HandleRecordingStarted()	{ BroadcastModeChanged(); }
void UInputRecordingSubsystem::HandleRecordingStopped()	{ BroadcastModeChanged(); }
void UInputRecordingSubsystem::HandlePlaybackStarted()	{ BroadcastModeChanged(); }
void UInputRecordingSubsystem::HandlePlaybackFinished()	{ BroadcastModeChanged(); }
void UInputRecordingSubsystem::HandleMatchInputStarted()	{ BroadcastModeChanged(); }

void UInputRecordingSubsystem::HandleMatchCuePresented(int32 CueIndex, int32 TotalCues, const FString& ExpectedInput)
{
	OnMatchCuePresented.Broadcast(CueIndex, TotalCues, ExpectedInput);
}

void UInputRecordingSubsystem::HandleMatchInputMatched(int32 CueIndex, int32 TotalCues)
{
	OnMatchInputMatched.Broadcast(CueIndex, TotalCues);
}

void UInputRecordingSubsystem::HandleMatchInputMismatch(const FString& ExpectedInput, const FString& ActualInput)
{
	++MismatchCount;
	LastMismatchDescription = FString::Printf(TEXT("Expected %s, got %s"), *ExpectedInput, *ActualInput);

	OnMatchInputMismatch.Broadcast(ExpectedInput, ActualInput);
}

void UInputRecordingSubsystem::HandleMatchInputFinished(bool bCompletedAllCues)
{
	OnMatchInputFinished.Broadcast(bCompletedAllCues);
	BroadcastModeChanged();
}
