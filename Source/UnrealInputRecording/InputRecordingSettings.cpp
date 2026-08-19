// Copyright (c) Your Studio. All Rights Reserved.

#include "InputRecordingSettings.h"

#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputReplay/InputReplayComponent.h"
#include "Storage/RecordingSessionTypes.h"
#include "Blueprint/UserWidget.h"
#include "UI/InputActionIconMappingDataAsset.h"

UInputRecordingSettings::UInputRecordingSettings()
{
	// Sensible starting point for the Third Person template: ignore the two look actions so camera
	// movement never counts as a wrong answer during MatchInput.
	MatchCueOptions.IgnoredActions = { TEXT("IA_Look"), TEXT("IA_MouseLook") };
}

const UInputRecordingSettings* UInputRecordingSettings::Get()
{
	// GetDefault is the correct accessor for a UDeveloperSettings CDO - it is where the ini values
	// are loaded into.
	return GetDefault<UInputRecordingSettings>();
}

UInputActionIconMappingDataAsset* UInputRecordingSettings::LoadIconMapping() const
{
	if (IconMapping.IsNull())
	{
		return nullptr;
	}

	UInputActionIconMappingDataAsset* Loaded = IconMapping.LoadSynchronous();

	if (!Loaded)
	{
		// Worth a log rather than a silent null: the symptom downstream is "no icons anywhere", which
		// reads as a broken sprite pipeline instead of an unresolvable asset reference.
		UE_LOG(LogInputReplay, Warning,
			TEXT("Input Action Icon Mapping ('%s') will not load. Recording UIs will draw no icons."),
			*IconMapping.ToString());
	}

	return Loaded;
}

UClass* UInputRecordingSettings::LoadWidgetClass(const FSoftClassPath& ConfiguredClass, UClass* FallbackClass, const TCHAR* ContextName)
{
	if (ConfiguredClass.IsValid())
	{
		if (UClass* Loaded = ConfiguredClass.TryLoadClass<UUserWidget>())
		{
			return Loaded;
		}

		UE_LOG(LogInputReplay, Error,
			TEXT("%s widget class '%s' will not load. Falling back to the C++ class, which builds no ")
			TEXT("layout and will render nothing."),
			ContextName, *ConfiguredClass.ToString());

		return FallbackClass;
	}

	// Loud rather than silent: the C++ classes are pure logic now, so an unset class is the difference
	// between a working screen and a blank one, and a blank screen gives no hint why.
	UE_LOG(LogInputReplay, Error,
		TEXT("No %s widget class is set in Project Settings > Game > Input Recording > UI. ")
		TEXT("That UI will render nothing until a Blueprint is assigned."),
		ContextName);

	return FallbackClass;
}

int64 UInputRecordingSettings::GetQuotaBytes() const
{
	return static_cast<int64>(QuotaMegabytes) * RecordingStore::BytesPerMegabyte;
}

int64 UInputRecordingSettings::GetReserveBytesPerTake() const
{
	return static_cast<int64>(ReserveMegabytesPerTake) * RecordingStore::BytesPerMegabyte;
}

void UInputRecordingSettings::ApplyDefaultsTo(UInputReplayComponent* Component, bool bForce) const
{
	if (!Component)
	{
		return;
	}

	// ---- Recorded contexts -------------------------------------------------------------------
	if (bForce || Component->RecordedContexts.Num() == 0)
	{
		TArray<TObjectPtr<UInputMappingContext>> Resolved;
		for (const TSoftObjectPtr<UInputMappingContext>& Soft : RecordedContexts)
		{
			// Synchronous load: these are tiny data assets and we need them before the first sample.
			if (UInputMappingContext* Context = Soft.LoadSynchronous())
			{
				Resolved.Add(Context);
			}
			else if (!Soft.IsNull())
			{
				UE_LOG(LogInputReplay, Warning,
					TEXT("Input Recording settings reference a mapping context that will not load: %s"),
					*Soft.ToString());
			}
		}

		if (Resolved.Num() > 0 || bForce)
		{
			Component->RecordedContexts = MoveTemp(Resolved);
		}
	}

	// ---- Additional actions ------------------------------------------------------------------
	if (bForce || Component->AdditionalActions.Num() == 0)
	{
		TArray<TObjectPtr<UInputAction>> Resolved;
		for (const TSoftObjectPtr<UInputAction>& Soft : AdditionalActions)
		{
			if (UInputAction* Action = Soft.LoadSynchronous())
			{
				Resolved.Add(Action);
			}
		}

		if (Resolved.Num() > 0 || bForce)
		{
			Component->AdditionalActions = MoveTemp(Resolved);
		}
	}

	// ---- Frame-delta actions -----------------------------------------------------------------
	// Getting this set wrong is the classic "the ghost turns further than I did" bug, so never clear
	// a hand-authored set: only add to it.
	if (bForce || Component->FrameDeltaActions.Num() == 0)
	{
		for (const TSoftObjectPtr<UInputAction>& Soft : FrameDeltaActions)
		{
			if (UInputAction* Action = Soft.LoadSynchronous())
			{
				Component->FrameDeltaActions.Add(Action);
			}
		}
	}

	// ---- Recording filter --------------------------------------------------------------------
	// A component left at RecordAll is at its default and has not been configured by hand, so the
	// project setting is free to fill it in. One that already says WhitelistOnly was set deliberately
	// and keeps its own list.
	if (bForce || Component->RecordingFilterMode == EInputRecordingFilterMode::RecordAll)
	{
		Component->RecordingFilterMode = RecordingFilterMode;

		if (RecordingFilterMode == EInputRecordingFilterMode::WhitelistOnly)
		{
			TArray<TObjectPtr<UInputAction>> Resolved;
			for (const TSoftObjectPtr<UInputAction>& Soft : RecordedActionWhitelist)
			{
				if (UInputAction* Action = Soft.LoadSynchronous())
				{
					Resolved.Add(Action);
				}
				else if (!Soft.IsNull())
				{
					// Silently dropping a whitelist entry would look identical to the action simply not
					// firing, which is a miserable thing to debug during a take.
					UE_LOG(LogInputReplay, Warning,
						TEXT("Recording whitelist references an action that will not load: %s"),
						*Soft.ToString());
				}
			}

			Component->RecordedActionWhitelist = MoveTemp(Resolved);
		}
	}

	// ---- Determinism -------------------------------------------------------------------------
	if (bForce)
	{
		Component->TimeMode = TimeMode;
		Component->LogicalTicksPerSecond = LogicalTicksPerSecond;
	}

	// ---- MatchInput --------------------------------------------------------------------------
	if (bForce || Component->MatchCueOptions.IgnoredActions.Num() == 0)
	{
		Component->MatchCueOptions = MatchCueOptions;
		Component->MatchDirectionTolerance = MatchDirectionTolerance;
	}
}
