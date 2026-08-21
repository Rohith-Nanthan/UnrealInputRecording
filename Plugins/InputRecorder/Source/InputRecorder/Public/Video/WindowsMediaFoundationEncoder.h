// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if PLATFORM_WINDOWS

#include "Video/IVideoEncoderBackend.h"

/**
 * H.264 through the Media Foundation sink writer.
 *
 * The sink writer is used rather than a raw encoder MFT because it does the H.264 encode and
 * the MP4 mux in one object, picks up a hardware encoder when the machine has one, and needs no
 * plugin - only mfplat / mfreadwrite / mfuuid / ole32.
 */
class FWindowsMediaFoundationEncoder final : public IVideoEncoderBackend
{
public:
	FWindowsMediaFoundationEncoder() = default;
	virtual ~FWindowsMediaFoundationEncoder() override;

	virtual bool Initialize(const FVideoEncoderInitParams& Params) override;
	virtual bool EncodeFrame(const uint8* Pixels, int32 Stride, int64 TimestampMicroseconds) override;
	virtual void Finalize() override;
	virtual FString GetBackendName() const override { return TEXT("Windows Media Foundation (H.264 sink writer)"); }

private:
	void ReleaseResources();

	/** Opaque so this header never drags <mfreadwrite.h> into anything that includes it. */
	struct FImpl;
	TUniquePtr<FImpl> Impl;

	FVideoEncoderInitParams Config;
	bool bInitialized = false;
	bool bFinalized = false;
	int64 FramesWritten = 0;
};

#endif // PLATFORM_WINDOWS
