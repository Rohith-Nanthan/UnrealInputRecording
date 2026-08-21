// Copyright Epic Games, Inc. All Rights Reserved.

#include "Video/VideoEncoderPipeline.h"

#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/RunnableThread.h"
#include "InputRecordingLog.h"

FVideoEncoderPipeline::~FVideoEncoderPipeline()
{
	Finalize();
}

bool FVideoEncoderPipeline::Initialize(TUniquePtr<IVideoEncoderBackend> InBackend, const FVideoEncoderInitParams& Params, int32 InMaxQueuedFrames)
{
	if (!InBackend)
	{
		return false;
	}

	if (!InBackend->Initialize(Params))
	{
		return false;
	}

	Backend = MoveTemp(InBackend);
	MaxQueuedFrames = FMath::Max(1, InMaxQueuedFrames);
	bStopRequested = false;
	SubmittedFrames = 0;
	DroppedFrames = 0;

	FrameAvailableEvent = FPlatformProcess::GetSynchEventFromPool(/*bIsManualReset=*/false);

	bRunning = true;
	WorkerThread = FRunnableThread::Create(this, TEXT("InputRecordingVideoEncoder"), 0, TPri_BelowNormal);

	if (!WorkerThread)
	{
		UE_LOG(LogRecordingVideo, Error, TEXT("Could not start the video encoder thread."));
		bRunning = false;
		FPlatformProcess::ReturnSynchEventToPool(FrameAvailableEvent);
		FrameAvailableEvent = nullptr;
		Backend->Finalize();
		Backend.Reset();
		return false;
	}

	UE_LOG(LogRecordingVideo, Log, TEXT("Encoder pipeline running on %s, queue depth %d."),
		*Backend->GetBackendName(), MaxQueuedFrames);

	return true;
}

TUniquePtr<FVideoEncoderPipeline::FQueuedFrame> FVideoEncoderPipeline::AcquireFrameBuffer(int32 RequiredBytes)
{
	{
		FScopeLock Lock(&PoolLock);
		if (BufferPool.Num() > 0)
		{
			TUniquePtr<FQueuedFrame> Recycled = MoveTemp(BufferPool.Last());
			BufferPool.Pop(EAllowShrinking::No);
			Recycled->Pixels.SetNumUninitialized(RequiredBytes, EAllowShrinking::No);
			return Recycled;
		}
	}

	TUniquePtr<FQueuedFrame> Fresh = MakeUnique<FQueuedFrame>();
	Fresh->Pixels.SetNumUninitialized(RequiredBytes, EAllowShrinking::No);
	return Fresh;
}

void FVideoEncoderPipeline::ReleaseFrameBuffer(TUniquePtr<FQueuedFrame>&& Frame)
{
	if (!Frame)
	{
		return;
	}

	FScopeLock Lock(&PoolLock);

	// Cap the pool at the queue depth; anything beyond that is memory nobody is going to use.
	if (BufferPool.Num() < MaxQueuedFrames)
	{
		BufferPool.Add(MoveTemp(Frame));
	}
}

bool FVideoEncoderPipeline::SubmitFrame_AnyThread(const void* Pixels, int32 Width, int32 Height, int32 Stride, int64 TimestampMicroseconds)
{
	if (!bRunning || bStopRequested || !Pixels || Width <= 0 || Height <= 0 || Stride <= 0)
	{
		return false;
	}

	FScopeLock SubmitLock(&SubmitFinalizeLock);

	if (!bRunning || bStopRequested)
	{
		return false;
	}

	{
		FScopeLock Lock(&QueueLock);
		if (PendingFrames.Num() >= MaxQueuedFrames)
		{
			// Drop rather than block. A stalled render thread costs every frame in the game;
			// a dropped frame costs one frame in the review video.
			++DroppedFrames;
			return false;
		}
	}

	const int32 RequiredBytes = Stride * Height;
	TUniquePtr<FQueuedFrame> Frame = AcquireFrameBuffer(RequiredBytes);
	Frame->Stride = Stride;
	Frame->TimestampMicroseconds = TimestampMicroseconds;
	FMemory::Memcpy(Frame->Pixels.GetData(), Pixels, RequiredBytes);

	{
		FScopeLock Lock(&QueueLock);
		PendingFrames.Add(MoveTemp(Frame));
	}

	++SubmittedFrames;

	if (FrameAvailableEvent)
	{
		FrameAvailableEvent->Trigger();
	}

	return true;
}

uint32 FVideoEncoderPipeline::Run()
{
	while (!bStopRequested)
	{
		TUniquePtr<FQueuedFrame> Frame;

		{
			FScopeLock Lock(&QueueLock);
			if (PendingFrames.Num() > 0)
			{
				Frame = MoveTemp(PendingFrames[0]);
				PendingFrames.RemoveAt(0, EAllowShrinking::No);
			}
		}

		if (!Frame)
		{
			if (FrameAvailableEvent)
			{
				// Bounded wait so a missed trigger cannot wedge the thread for the whole take.
				FrameAvailableEvent->Wait(FTimespan::FromMilliseconds(50));
			}
			continue;
		}

		if (Backend)
		{
			Backend->EncodeFrame(Frame->Pixels.GetData(), Frame->Stride, Frame->TimestampMicroseconds);
		}

		ReleaseFrameBuffer(MoveTemp(Frame));
	}

	// Drain whatever arrived between the stop request and the loop noticing it, so Finalize
	// really does mean "every submitted frame is in the file".
	for (;;)
	{
		TUniquePtr<FQueuedFrame> Frame;
		{
			FScopeLock Lock(&QueueLock);
			if (PendingFrames.Num() == 0)
			{
				break;
			}
			Frame = MoveTemp(PendingFrames[0]);
			PendingFrames.RemoveAt(0, EAllowShrinking::No);
		}

		if (Backend && Frame)
		{
			Backend->EncodeFrame(Frame->Pixels.GetData(), Frame->Stride, Frame->TimestampMicroseconds);
		}
	}

	return 0;
}

void FVideoEncoderPipeline::Stop()
{
	bStopRequested = true;
	if (FrameAvailableEvent)
	{
		FrameAvailableEvent->Trigger();
	}
}

void FVideoEncoderPipeline::Finalize()
{
	if (!bRunning)
	{
		return;
	}

	// Held across the whole teardown so a render-thread submit cannot land on a queue that is
	// being drained or on a backend that has already closed its file.
	FScopeLock SubmitLock(&SubmitFinalizeLock);

	if (!bRunning)
	{
		return;
	}

	Stop();

	if (WorkerThread)
	{
		WorkerThread->WaitForCompletion();
		delete WorkerThread;
		WorkerThread = nullptr;
	}

	if (Backend)
	{
		Backend->Finalize();
		Backend.Reset();
	}

	if (FrameAvailableEvent)
	{
		FPlatformProcess::ReturnSynchEventToPool(FrameAvailableEvent);
		FrameAvailableEvent = nullptr;
	}

	{
		FScopeLock Lock(&QueueLock);
		PendingFrames.Reset();
	}
	{
		FScopeLock Lock(&PoolLock);
		BufferPool.Reset();
	}

	bRunning = false;

	if (DroppedFrames > 0)
	{
		UE_LOG(LogRecordingVideo, Warning, TEXT("Encoder finished: %lld frame(s) encoded, %lld dropped to protect frame time."),
			SubmittedFrames.load(), DroppedFrames.load());
	}
	else
	{
		UE_LOG(LogRecordingVideo, Log, TEXT("Encoder finished: %lld frame(s) encoded, none dropped."), SubmittedFrames.load());
	}
}
