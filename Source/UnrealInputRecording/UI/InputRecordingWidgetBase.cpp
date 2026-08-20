// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/InputRecordingWidgetBase.h"

#include "Engine/GameInstance.h"
#include "InputRecordingLog.h"
#include "Subsystem/InputRecordingSubsystem.h"

UInputRecordingSubsystem* UInputRecordingWidgetBase::GetRecordingSubsystem() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UInputRecordingSubsystem>() : nullptr;
}

void UInputRecordingWidgetBase::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	ValidateBindings();
}

void UInputRecordingWidgetBase::ValidateBindings()
{
	TArray<FString> Missing;
	CollectMissingBindings(Missing);

	if (Missing.Num() > 0)
	{
		// One message listing everything, so a designer fixes the whole tree in one pass instead
		// of chasing one warning per run.
		UE_LOG(LogInputRecording, Warning, TEXT("%s is missing %d widget binding(s): %s"),
			*GetClass()->GetName(), Missing.Num(), *FString::Join(Missing, TEXT(", ")));
	}

	K2_OnBindingsValidated(Missing.Num() == 0);
}
