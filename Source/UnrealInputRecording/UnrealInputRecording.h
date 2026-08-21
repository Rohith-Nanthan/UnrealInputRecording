// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Primary game module.
 *
 * Deliberately empty. Every UE game project must declare exactly one primary game module, and a
 * plugin module cannot be it - so this stub stays behind purely to satisfy that requirement after
 * the input recording system moved into Plugins/InputRecorder.
 *
 * Nothing should be added here. Code that belongs to the recorder belongs in the plugin, or it
 * stops travelling with it.
 */
