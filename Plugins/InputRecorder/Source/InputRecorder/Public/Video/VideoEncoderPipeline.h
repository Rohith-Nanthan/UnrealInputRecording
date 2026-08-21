// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/CriticalSection.h"
#include "Video/IVideoEncoderBackend.h"

class FRunnableThread;

/**
 * Bounded frame queue in front of a platform encoder backend.
 *
 * Threading contract, and it is not optional:
 *
 *  - Initialize()            creating thread. Blocks briefly while the backend opens the file.
 *  - SubmitFrame_AnyThread() render thread, once per captured frame. Copies the pixels into a
 *                            pooled buffer and returns. It NEVER encodes - encoding on the
 *                            render thread destroys frame time.
 *  - Finalize()              creating thread. Blocks until the queue drains and the file closes.
 *
 * The caller must not submit concurrently with finalize; SubmitFinalizeLock enforces it.
 */
class FVideoEncoderPipeline final : public FRunnable
{
public:
	FVideoEncoderPipeline() = default;
	virtual ~FVideoEncoderPipeline() override;

	/** Takes ownership of the backend. False when the backend refuses these parameters. */
	bool Initialize(TUniquePtr<IVideoEncoderBackend> InBackend, const FVideoEncoderInitParams& Params, int32 InMaxQueuedFrames);

	/**
	 * Copy one BGRA8 frame in. Returns false when the frame was dropped because the queue was
	 * full - dropping is correct here, stalling the render thread is not.
	 */
	bool SubmitFrame_AnyThread(const void* Pixels, int32 Width, int32 Height, int32 Stride, int64 TimestampMicroseconds);

	/** Drains, closes the file and joins the worker. Safe to call more than once. */
	void Finalize();

	bool IsRunning() const { return bRunning; }
	int64 GetSubmittedFrameCount() const { return SubmittedFrames; }
	int64 GetDroppedFrameCount() const { return DroppedFrames; }

	// FRunnable
	virtual uint32 Run() override;
	virtual void Stop() override;

private:
	struct FQueuedFrame
	{
		TArray<uint8> Pixels;
		int32 Stride = 0;
		int64 TimestampMicroseconds = 0;
	};

	/** Pops from the pool, or allocates when the pool is empty. Caller owns the result. */
	TUniquePtr<FQueuedFrame> AcquireFrameBuffer(int32 RequiredBytes);
	void ReleaseFrameBuffer(TUniquePtr<FQueuedFrame>&& Frame);

	TUniquePtr<IVideoEncoderBackend> Backend;
	FRunnableThread* WorkerThread = nullptr;
	FEvent* FrameAvailableEvent = nullptr;

	mutable FCriticalSection QueueLock;
	TArray<TUniquePtr<FQueuedFrame>> PendingFrames;

	/**
	 * Reusing buffers rather than allocating per frame: at 1080p each is about 8 MB, and
	 * thirty allocations a second of that size visibly thrashes the allocator.
	 */
	mutable FCriticalSection PoolLock;
	TArray<TUniquePtr<FQueuedFrame>> BufferPool;

	FCriticalSection SubmitFinalizeLock;

	int32 MaxQueuedFrames = 6;
	std::atomic<bool> bRunning{ false };
	std::atomic<bool> bStopRequested{ false };
	std::atomic<int64> SubmittedFrames{ 0 };
	std::atomic<int64> DroppedFrames{ 0 };
};
