// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"

/** Fixed for the lifetime of one encode session. */
struct FVideoEncoderInitParams
{
	int32 Width = 0;
	int32 Height = 0;
	int32 FrameRate = 30;
	int32 BitRate = 12000000;

	/** Absolute path of the .mp4 to write. */
	FString OutputPath;

	/**
	 * True when row 0 of the submitted buffer is the bottom of the image and the backend must
	 * flip it. Resolved from EInputRecordingVideoOrientation before it ever reaches a backend,
	 * so a backend never has to reason about Auto.
	 */
	bool bSourceIsBottomUp = false;
};

/**
 * The raw platform encoder, wrapped away from everything Unreal-facing.
 *
 * Kept as a plain C++ interface on purpose: supporting another platform means adding a backend
 * and changing nothing else. Every method here runs on the encoder thread owned by
 * FVideoEncoderPipeline - none of them is safe to call from the render thread.
 */
class IVideoEncoderBackend
{
public:
	virtual ~IVideoEncoderBackend() = default;

	virtual bool Initialize(const FVideoEncoderInitParams& Params) = 0;

	/** One BGRA8 frame. Stride is the source row pitch in bytes, which need not be Width * 4. */
	virtual bool EncodeFrame(const uint8* Pixels, int32 Stride, int64 TimestampMicroseconds) = 0;

	/** Flushes and closes the file. Safe to call more than once. */
	virtual void Finalize() = 0;

	virtual FString GetBackendName() const = 0;
};

namespace VideoEncoderBackend
{
	/**
	 * Returns null on a platform with no backend, and that is not an error condition - video
	 * capture is non-fatal and recording carries on without it.
	 */
	INPUTRECORDER_API TUniquePtr<IVideoEncoderBackend> Create();
}
