// Copyright (c) Your Studio. All Rights Reserved.

#include "Input/RecordingUIInputConfig.h"

#include "EnhancedInputSubsystems.h"
#include "Framework/Application/NavigationConfig.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "InputMappingContext.h"
#include "Storage/RecordingSessionTypes.h"

namespace
{
	/**
	 * Navigation config that also accepts the left analog stick.
	 *
	 * Slate's stock FNavigationConfig only navigates on the d-pad and arrow keys. Players reach for
	 * the stick first on a full-screen menu, and a menu that ignores it reads as broken rather than as
	 * a deliberate choice.
	 */
	class FRecordingUINavigationConfig final : public FNavigationConfig
	{
	public:
		FRecordingUINavigationConfig()
		{
			bTabNavigation = true;
			bKeyNavigation = true;
			bAnalogNavigation = true;
		}
	};

	/**
	 * The config Slate had before a recording UI replaced it.
	 *
	 * Static rather than per-widget on purpose: navigation config is global to the Slate application,
	 * so restoring it has to be global too. Both UIs are mutually exclusive - the overlay hides itself
	 * before the recap map loads - so there is only ever one to restore.
	 */
	TSharedPtr<FNavigationConfig> GPreviousNavigationConfig;
}

UInputMappingContext* URecordingUIInputConfig::LoadMappingContext() const
{
	if (MappingContext.IsNull())
	{
		return nullptr;
	}

	UInputMappingContext* Loaded = MappingContext.LoadSynchronous();

	if (!Loaded)
	{
		UE_LOG(LogRecordingStore, Warning,
			TEXT("The recording UI input config points at a mapping context that will not load: %s. ")
			TEXT("Controller navigation will fall back to Slate defaults."),
			*MappingContext.ToString());
	}

	return Loaded;
}

void URecordingUIInputConfig::ApplyTo(APlayerController* PlayerController, bool bAllowAnalogNavigation) const
{
	if (!PlayerController)
	{
		return;
	}

	if (const ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
			LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			if (UInputMappingContext* Context = LoadMappingContext())
			{
				// Not cleared first: gameplay contexts have to stay applied for the overlay, where
				// matching still reads live action values while the panel is up.
				InputSubsystem->AddMappingContext(Context, ContextPriority);
			}
		}
	}

	if (bAllowAnalogNavigation && FSlateApplication::IsInitialized())
	{
		FSlateApplication& SlateApplication = FSlateApplication::Get();

		if (!GPreviousNavigationConfig.IsValid())
		{
			GPreviousNavigationConfig = SlateApplication.GetNavigationConfig();
		}

		SlateApplication.SetNavigationConfig(MakeShared<FRecordingUINavigationConfig>());
	}
}

void URecordingUIInputConfig::RemoveFrom(APlayerController* PlayerController) const
{
	if (PlayerController)
	{
		if (const ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
				LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				if (UInputMappingContext* Context = MappingContext.Get())
				{
					InputSubsystem->RemoveMappingContext(Context);
				}
			}
		}
	}

	if (GPreviousNavigationConfig.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetNavigationConfig(GPreviousNavigationConfig.ToSharedRef());
		GPreviousNavigationConfig.Reset();
	}
}
