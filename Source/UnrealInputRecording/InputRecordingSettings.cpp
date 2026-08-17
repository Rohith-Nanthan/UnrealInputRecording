// Copyright (c) Your Studio. All Rights Reserved.

#include "InputRecordingSettings.h"

#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputReplay/InputReplayComponent.h"

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
