// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

/** General recording / replay / subsystem chatter. */
DECLARE_LOG_CATEGORY_EXTERN(LogInputRecording, Log, All);

/**
 * Every file and quota operation logs here and nothing else does, so
 * `log LogRecordingStore Verbose` yields the complete story of disk activity
 * with zero input or rendering noise mixed in.
 */
DECLARE_LOG_CATEGORY_EXTERN(LogRecordingStore, Log, All);

/** Cue extraction and the MatchInput state machine. */
DECLARE_LOG_CATEGORY_EXTERN(LogMatchInput, Log, All);

/** Video capture, encoding and playback. */
DECLARE_LOG_CATEGORY_EXTERN(LogRecordingVideo, Log, All);
