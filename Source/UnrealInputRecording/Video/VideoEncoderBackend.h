// Copyright (c) Your Studio. All Rights Reserved.
//
// VideoEncoderBackend.h
//
// The one piece of this feature that is genuinely platform-specific.
//
// UE 5.8 ships everything needed to *acquire* frames (UMediaCapture) and to *play* an MP4 back
// (WmfMedia / Electra), but nothing that encodes H.264 and muxes it into an .mp4 on disk. The engine's
// own MP4Muxer plugin is a raw muxer - it wants an already-encoded, already-framed elementary stream
// plus a hand-built avcC configuration record - and AVCodecs is still Experimental.
//
// So the encode step sits behind this interface. The Windows implementation uses the Media Foundation
// sink writer (IMFSinkWriter), which does H.264 encode *and* MP4 muxing in one object, picks up a
// hardware encoder when one is present, and needs no plugin at all - just three system .libs. That is
// the same API the engine's old GameplayMediaEncoder used.
//
// Everything above this header (UInputRecordingMediaCapture, the subsystem, the UI) is
// platform-agnostic: to support another platform, add a backend in Create() and nothing else changes.
//
// THREADING CONTRACT
//   Initialize()          - creating thread (game thread in practice). Blocks briefly.
//   SubmitFrame_AnyThread - render thread, once per captured frame. Copies and returns; never encodes.
//   Finalize()            - creating thread. Blocks until the queue drains and the file is closed.
// The caller is responsible for not calling SubmitFrame_AnyThread concurrently with Finalize();
// UInputRecordingMediaCapture does that with a critical section.

#pragma once

#include "CoreMinimal.h"

/** Everything the encoder needs to know up front. Immutable for the lifetime of one capture. */
struct FInputRecordingVideoEncoderConfig
{
	/** Absolute path of the .mp4 to write. Parent directory is created if missing. */
	FString OutputPath;

	/** Must both be even - H.264 macroblocks are 16x16 and chroma is subsampled 2x2. */
	int32 Width = 0;
	int32 Height = 0;

	/** Advertised frame rate. Samples still carry real timestamps; see FInputRecordingVideoOptions. */
	int32 FrameRate = 30;

	/** Average bitrate in bits per second. */
	int32 BitRate = 12000000;

	/** Queue depth before frames are dropped rather than stalling the render thread. */
	int32 MaxQueuedFrames = 6;

	/**
	 * Whether to reverse row order while copying into the encoder's buffer.
	 *
	 * This is the *resolved* answer, not the user's setting: UInputRecordingMediaCapture turns
	 * FInputRecordingVideoOptions::Orientation into a plain bool before it gets here, so the backend
	 * never has to reason about what Auto means on this platform.
	 *
	 * The flip is free either way - the frame is already copied a row at a time to strip readback
	 * padding, so reversing the destination index costs nothing but cache locality.
	 */
	bool bFlipVertical = false;

	/**
	 * Write the first encoded frame out as a PNG beside the .mp4 and clear the flag.
	 *
	 * Orientation is the one property of this pipeline that cannot be asserted in code - it depends
	 * on which conversion the sink writer picks at runtime. A single PNG settles it in seconds, and
	 * PNG specifically because it has exactly one row order, unlike BMP.
	 */
	bool bDumpFirstFrame = false;
};

class IInputRecordingVideoEncoder
{
public:
	virtual ~IInputRecordingVideoEncoder() = default;

	/**
	 * Opens the file and starts the encoder thread.
	 * @return false and fills OutError if the backend could not start. Nothing is written in that case.
	 */
	virtual bool Initialize(const FInputRecordingVideoEncoderConfig& Config, FString& OutError) = 0;

	/**
	 * Queues one captured frame. Returns immediately after a copy - encoding happens on the worker.
	 *
	 * @param SourceBgra    Top-down BGRA8 pixels, as delivered by UMediaCapture's CPU readback.
	 * @param SourceWidth   Must match the configured width.
	 * @param SourceHeight  Must match the configured height.
	 * @param SourceStride  Bytes per row. May exceed SourceWidth * 4 - readback buffers are padded.
	 * @param TimeSeconds   Elapsed seconds since capture began. Must be non-decreasing across calls.
	 */
	virtual void SubmitFrame_AnyThread(const void* SourceBgra, int32 SourceWidth, int32 SourceHeight,
									   int32 SourceStride, double TimeSeconds) = 0;

	/** Drains the queue, closes the file and stops the worker. Safe to call twice. */
	virtual void Finalize() = 0;

	virtual int64 GetSubmittedFrameCount() const = 0;
	virtual int64 GetEncodedFrameCount() const = 0;

	/** Frames thrown away because the encoder could not keep up. Non-zero means a stuttery video. */
	virtual int64 GetDroppedFrameCount() const = 0;

	virtual bool HasFailed() const = 0;
	virtual FString GetLastError() const = 0;

	/** Creates the backend for this platform, or nullptr where there is none. */
	static TUniquePtr<IInputRecordingVideoEncoder> Create();

	/** Cheap query for UI / logging - does not create anything. */
	static bool IsSupportedOnThisPlatform();
};
