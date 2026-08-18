// Copyright (c) Your Studio. All Rights Reserved.

#include "Video/VideoEncoderBackend.h"

#include "Containers/Queue.h"
#include "HAL/Event.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Video/InputRecordingVideoTypes.h"

#include <atomic>

#if PLATFORM_WINDOWS

#include "Microsoft/COMPointer.h"

// Note: everything the Windows headers define is scoped to this block. HideWindowsPlatformTypes.h
// undefines the BOOL/TRUE/FALSE macros on the way out, so use literals in the code below.
#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
	#include <mfapi.h>
	#include <mfidl.h>
	#include <mfreadwrite.h>
	#include <mferror.h>
	#include <codecapi.h>
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"

namespace
{
	/**
	 * One frame in flight between the render thread and the encoder thread.
	 *
	 * Pixels are stored BOTTOM-UP: Media Foundation's MFVideoFormat_RGB32 with a positive
	 * MF_MT_DEFAULT_STRIDE means "bottom-up DIB", which is the historical Windows convention. The
	 * alternative - declaring a negative stride so MF reads top-down - works too, but the sign
	 * semantics are easy to get subtly wrong and produce a silently upside-down video. We already
	 * have to copy row by row (readback buffers are padded), so walking the destination rows backwards
	 * costs nothing and is unambiguous.
	 */
	struct FEncoderFrame
	{
		TArray<uint8> Pixels;
		int32 Width = 0;
		int32 Height = 0;
		double TimeSeconds = 0.0;
	};

	/** Seconds -> Media Foundation's 100-nanosecond ticks. */
	FORCEINLINE LONGLONG ToMfTime(double Seconds)
	{
		return static_cast<LONGLONG>(Seconds * 10000000.0);
	}
}

/**
 * Media Foundation sink-writer backend.
 *
 * All Media Foundation work - MFStartup, sink writer creation, WriteSample, Finalize, MFShutdown -
 * happens on the worker thread. That is deliberate: UE initialises COM on the main thread in an
 * apartment we do not control, and handing an MF object created there to another thread would rely on
 * marshalling that may or may not be set up. Confining the whole MF lifetime to one thread we own
 * sidesteps the question entirely, at the cost of a short handshake in Initialize().
 */
class FMediaFoundationVideoEncoder final : public IInputRecordingVideoEncoder, public FRunnable
{
public:
	virtual ~FMediaFoundationVideoEncoder() override
	{
		Finalize();
	}

	//~ IInputRecordingVideoEncoder ---------------------------------------------------------------

	virtual bool Initialize(const FInputRecordingVideoEncoderConfig& InConfig, FString& OutError) override
	{
		Config = InConfig;

		if (Config.Width <= 0 || Config.Height <= 0 || (Config.Width & 1) || (Config.Height & 1))
		{
			OutError = FString::Printf(
				TEXT("H.264 requires positive, even dimensions; got %dx%d."), Config.Width, Config.Height);
			return false;
		}

		const FString Directory = FPaths::GetPath(Config.OutputPath);
		if (!Directory.IsEmpty() && !IFileManager::Get().DirectoryExists(*Directory))
		{
			IFileManager::Get().MakeDirectory(*Directory, /*Tree=*/true);
		}

		FrameStride = Config.Width * 4;
		FrameBytes  = FrameStride * Config.Height;

		InitCompleteEvent = FPlatformProcess::GetSynchEventFromPool(/*bIsManualReset=*/true);
		WorkAvailableEvent = FPlatformProcess::GetSynchEventFromPool(/*bIsManualReset=*/false);

		Thread.Reset(FRunnableThread::Create(this, TEXT("InputRecordingVideoEncoder"),
			0, TPri_BelowNormal));

		if (!Thread)
		{
			OutError = TEXT("Could not create the encoder thread.");
			ReleaseEvents();
			return false;
		}

		// Wait for the worker to report whether the sink writer came up. Sink writer creation is fast
		// (a few milliseconds); the generous timeout is only there so a wedged media stack cannot hang
		// the game thread forever.
		InitCompleteEvent->Wait(FTimespan::FromSeconds(5.0));

		if (!bInitialised.load(std::memory_order_acquire))
		{
			OutError = GetLastError();
			if (OutError.IsEmpty())
			{
				OutError = TEXT("Timed out waiting for the Media Foundation sink writer to initialise.");
			}

			// Tear the half-started worker back down so we do not leak a thread on failure.
			ShutdownWorker();
			return false;
		}

		return true;
	}

	virtual void SubmitFrame_AnyThread(const void* SourceBgra, int32 SourceWidth, int32 SourceHeight,
									   int32 SourceStride, double TimeSeconds) override
	{
		if (!SourceBgra || bFailed.load(std::memory_order_relaxed) || bStopRequested.load(std::memory_order_relaxed))
		{
			return;
		}

		if (SourceWidth != Config.Width || SourceHeight != Config.Height)
		{
			// The capture resized under us. Encoding a different size into an already-configured stream
			// would corrupt the file, so fail loudly rather than write garbage.
			SetFailed(FString::Printf(
				TEXT("Frame size changed mid-capture: expected %dx%d, received %dx%d."),
				Config.Width, Config.Height, SourceWidth, SourceHeight));
			return;
		}

		SubmittedFrames.fetch_add(1, std::memory_order_relaxed);

		if (QueueDepth.load(std::memory_order_relaxed) >= Config.MaxQueuedFrames)
		{
			// Better a dropped frame than a stalled render thread. Timestamps are real elapsed time, so
			// a drop shortens nothing - the next frame simply sits a little further along the timeline.
			DroppedFrames.fetch_add(1, std::memory_order_relaxed);
			return;
		}

		TUniquePtr<FEncoderFrame> Frame = AcquireFrame();
		Frame->Width = SourceWidth;
		Frame->Height = SourceHeight;
		Frame->TimeSeconds = TimeSeconds;

		const uint8* Source = static_cast<const uint8*>(SourceBgra);
		const int32 EffectiveStride = (SourceStride > 0) ? SourceStride : FrameStride;
		uint8* Dest = Frame->Pixels.GetData();

		// Vertical flip: source row 0 is the top of the image, destination row 0 is the bottom.
		for (int32 Row = 0; Row < SourceHeight; ++Row)
		{
			FMemory::Memcpy(
				Dest + static_cast<SIZE_T>(SourceHeight - 1 - Row) * FrameStride,
				Source + static_cast<SIZE_T>(Row) * EffectiveStride,
				FrameStride);
		}

		QueueDepth.fetch_add(1, std::memory_order_relaxed);
		PendingFrames.Enqueue(MoveTemp(Frame));
		WorkAvailableEvent->Trigger();
	}

	virtual void Finalize() override
	{
		ShutdownWorker();
	}

	virtual int64 GetSubmittedFrameCount() const override { return SubmittedFrames.load(std::memory_order_relaxed); }
	virtual int64 GetEncodedFrameCount()   const override { return EncodedFrames.load(std::memory_order_relaxed); }
	virtual int64 GetDroppedFrameCount()   const override { return DroppedFrames.load(std::memory_order_relaxed); }
	virtual bool  HasFailed()              const override { return bFailed.load(std::memory_order_relaxed); }

	virtual FString GetLastError() const override
	{
		FScopeLock Lock(&ErrorLock);
		return LastError;
	}

	//~ FRunnable ---------------------------------------------------------------------------------

	virtual uint32 Run() override
	{
		// Our own MTA apartment: MF objects created here are usable here without marshalling.
		const HRESULT CoInitResult = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		const bool bOwnsCom = SUCCEEDED(CoInitResult);

		FString Error;
		const bool bReady = CreateSinkWriter(Error);

		if (bReady)
		{
			bInitialised.store(true, std::memory_order_release);
		}
		else
		{
			SetFailed(Error);
		}

		InitCompleteEvent->Trigger();

		if (bReady)
		{
			EncodeLoop();
			CloseSinkWriter();
		}

		if (bMediaFoundationStarted)
		{
			::MFShutdown();
			bMediaFoundationStarted = false;
		}

		if (bOwnsCom)
		{
			::CoUninitialize();
		}

		return 0;
	}

	virtual void Stop() override
	{
		bStopRequested.store(true, std::memory_order_release);
		if (WorkAvailableEvent)
		{
			WorkAvailableEvent->Trigger();
		}
	}

private:
	//~ Worker-thread implementation ---------------------------------------------------------------

	bool CreateSinkWriter(FString& OutError)
	{
		HRESULT Result = ::MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
		if (FAILED(Result))
		{
			OutError = FString::Printf(TEXT("MFStartup failed (hr=0x%08X)."), Result);
			return false;
		}
		bMediaFoundationStarted = true;

		// Container type is inferred from the .mp4 extension, but stating it explicitly means a caller
		// who passes an unusual path still gets an MP4 rather than an ASF.
		TComPtr<IMFAttributes> Attributes;
		Result = ::MFCreateAttributes(&Attributes, 3);
		if (FAILED(Result))
		{
			OutError = FString::Printf(TEXT("MFCreateAttributes failed (hr=0x%08X)."), Result);
			return false;
		}

		Attributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);

		// Literal 1 rather than TRUE: HideWindowsPlatformTypes.h undefines the Windows BOOL macros, so
		// TRUE does not exist outside the include block above.
		//
		// Without this the sink writer paces itself to the advertised frame rate, which would throttle
		// the encoder thread and back the queue up for no reason - we are writing a file, not streaming.
		Attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, 1);

		// Opt in to a hardware H.264 encoder (NVENC / QuickSync / AMF) when the machine has one.
		// Media Foundation falls back to the software encoder transparently if it does not.
		Attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, 1);

		Result = ::MFCreateSinkWriterFromURL(*Config.OutputPath, nullptr, Attributes, &SinkWriter);
		if (FAILED(Result) || !SinkWriter)
		{
			OutError = FString::Printf(TEXT("MFCreateSinkWriterFromURL('%s') failed (hr=0x%08X)."),
				*Config.OutputPath, Result);
			return false;
		}

		// ---- Output type: what lands in the file ---------------------------------------------
		TComPtr<IMFMediaType> OutputType;
		Result = ::MFCreateMediaType(&OutputType);
		if (FAILED(Result))
		{
			OutError = FString::Printf(TEXT("MFCreateMediaType (output) failed (hr=0x%08X)."), Result);
			return false;
		}

		OutputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
		OutputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
		OutputType->SetUINT32(MF_MT_AVG_BITRATE, static_cast<UINT32>(Config.BitRate));
		OutputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
		OutputType->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High);
		::MFSetAttributeSize(OutputType, MF_MT_FRAME_SIZE, Config.Width, Config.Height);
		::MFSetAttributeRatio(OutputType, MF_MT_FRAME_RATE, Config.FrameRate, 1);
		::MFSetAttributeRatio(OutputType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

		Result = SinkWriter->AddStream(OutputType, &StreamIndex);
		if (FAILED(Result))
		{
			OutError = FString::Printf(TEXT("IMFSinkWriter::AddStream failed (hr=0x%08X)."), Result);
			return false;
		}

		// ---- Input type: what we hand the encoder ---------------------------------------------
		TComPtr<IMFMediaType> InputType;
		Result = ::MFCreateMediaType(&InputType);
		if (FAILED(Result))
		{
			OutError = FString::Printf(TEXT("MFCreateMediaType (input) failed (hr=0x%08X)."), Result);
			return false;
		}

		InputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);

		// MFVideoFormat_RGB32 is B,G,R,X in memory, which is exactly PF_B8G8R8A8 - no swizzle needed.
		InputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
		InputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
		InputType->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(FrameStride));
		::MFSetAttributeSize(InputType, MF_MT_FRAME_SIZE, Config.Width, Config.Height);
		::MFSetAttributeRatio(InputType, MF_MT_FRAME_RATE, Config.FrameRate, 1);
		::MFSetAttributeRatio(InputType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

		Result = SinkWriter->SetInputMediaType(StreamIndex, InputType, nullptr);
		if (FAILED(Result))
		{
			OutError = FString::Printf(
				TEXT("IMFSinkWriter::SetInputMediaType failed (hr=0x%08X). No H.264 encoder accepted ")
				TEXT("%dx%d RGB32 input - check that the Media Feature Pack is installed on N/KN ")
				TEXT("editions of Windows."),
				Result, Config.Width, Config.Height);
			return false;
		}

		Result = SinkWriter->BeginWriting();
		if (FAILED(Result))
		{
			OutError = FString::Printf(TEXT("IMFSinkWriter::BeginWriting failed (hr=0x%08X)."), Result);
			return false;
		}

		UE_LOG(LogInputRecordingVideo, Log, TEXT("Encoding %dx%d H.264 @ %d fps, %d kbit/s -> '%s'."),
			Config.Width, Config.Height, Config.FrameRate, Config.BitRate / 1000, *Config.OutputPath);

		return true;
	}

	void EncodeLoop()
	{
		while (true)
		{
			TUniquePtr<FEncoderFrame> Frame;
			if (PendingFrames.Dequeue(Frame))
			{
				QueueDepth.fetch_sub(1, std::memory_order_relaxed);

				if (Frame.IsValid() && !bFailed.load(std::memory_order_relaxed))
				{
					WriteFrame(*Frame);
				}

				ReleaseFrame(MoveTemp(Frame));
				continue;
			}

			// Queue is empty. Only now is it safe to honour a stop request - otherwise we would drop
			// the tail of the recording on the floor.
			if (bStopRequested.load(std::memory_order_acquire))
			{
				break;
			}

			WorkAvailableEvent->Wait(FTimespan::FromMilliseconds(20));
		}
	}

	void WriteFrame(const FEncoderFrame& Frame)
	{
		TComPtr<IMFMediaBuffer> Buffer;
		HRESULT Result = ::MFCreateMemoryBuffer(static_cast<DWORD>(FrameBytes), &Buffer);
		if (FAILED(Result))
		{
			SetFailed(FString::Printf(TEXT("MFCreateMemoryBuffer failed (hr=0x%08X)."), Result));
			return;
		}

		BYTE* Destination = nullptr;
		Result = Buffer->Lock(&Destination, nullptr, nullptr);
		if (FAILED(Result))
		{
			SetFailed(FString::Printf(TEXT("IMFMediaBuffer::Lock failed (hr=0x%08X)."), Result));
			return;
		}

		FMemory::Memcpy(Destination, Frame.Pixels.GetData(), FrameBytes);
		Buffer->Unlock();
		Buffer->SetCurrentLength(static_cast<DWORD>(FrameBytes));

		TComPtr<IMFSample> Sample;
		Result = ::MFCreateSample(&Sample);
		if (FAILED(Result))
		{
			SetFailed(FString::Printf(TEXT("MFCreateSample failed (hr=0x%08X)."), Result));
			return;
		}

		Sample->AddBuffer(Buffer);

		// Sample times must strictly increase or the sink writer rejects the sample. Two captures
		// landing in the same clock tick is rare but possible, so clamp rather than trust the input.
		LONGLONG SampleTime = ToMfTime(Frame.TimeSeconds);
		if (bHasWrittenSample && SampleTime <= LastSampleTime)
		{
			SampleTime = LastSampleTime + 1;
		}

		const LONGLONG NominalDuration = 10000000LL / FMath::Max(1, Config.FrameRate);
		const LONGLONG Duration = bHasWrittenSample
			? FMath::Max(SampleTime - LastSampleTime, 1LL)
			: NominalDuration;

		Sample->SetSampleTime(SampleTime);
		Sample->SetSampleDuration(Duration);

		Result = SinkWriter->WriteSample(StreamIndex, Sample);
		if (FAILED(Result))
		{
			SetFailed(FString::Printf(TEXT("IMFSinkWriter::WriteSample failed (hr=0x%08X)."), Result));
			return;
		}

		LastSampleTime = SampleTime;
		bHasWrittenSample = true;
		EncodedFrames.fetch_add(1, std::memory_order_relaxed);
	}

	void CloseSinkWriter()
	{
		if (!SinkWriter)
		{
			return;
		}

		// Finalize writes the moov box and the index. Skipping it leaves an unplayable file, so it runs
		// even after an encode error - a truncated-but-valid MP4 beats a corrupt one.
		const HRESULT Result = SinkWriter->Finalize();
		if (FAILED(Result))
		{
			SetFailed(FString::Printf(TEXT("IMFSinkWriter::Finalize failed (hr=0x%08X)."), Result));
		}

		SinkWriter.Reset();
	}

	//~ Shared helpers ------------------------------------------------------------------------------

	void ShutdownWorker()
	{
		if (!Thread)
		{
			ReleaseEvents();
			return;
		}

		Stop();
		Thread->WaitForCompletion();
		Thread.Reset();

		ReleaseEvents();

		// Empty the pool and any frames that never made it through, so the memory goes back now rather
		// than at the next GC.
		TUniquePtr<FEncoderFrame> Discard;
		while (PendingFrames.Dequeue(Discard)) {}
		{
			FScopeLock Lock(&PoolLock);
			FramePool.Empty();
		}
		QueueDepth.store(0, std::memory_order_relaxed);

		UE_LOG(LogInputRecordingVideo, Log,
			TEXT("Encoder finished: %lld submitted, %lld encoded, %lld dropped -> '%s'."),
			GetSubmittedFrameCount(), GetEncodedFrameCount(), GetDroppedFrameCount(), *Config.OutputPath);
	}

	void ReleaseEvents()
	{
		if (InitCompleteEvent)
		{
			FPlatformProcess::ReturnSynchEventToPool(InitCompleteEvent);
			InitCompleteEvent = nullptr;
		}
		if (WorkAvailableEvent)
		{
			FPlatformProcess::ReturnSynchEventToPool(WorkAvailableEvent);
			WorkAvailableEvent = nullptr;
		}
	}

	/** Reuse a retired frame buffer if one is available - 8 MB allocations per frame add up fast. */
	TUniquePtr<FEncoderFrame> AcquireFrame()
	{
		{
			FScopeLock Lock(&PoolLock);
			if (FramePool.Num() > 0)
			{
				return FramePool.Pop(EAllowShrinking::No);
			}
		}

		TUniquePtr<FEncoderFrame> Frame = MakeUnique<FEncoderFrame>();
		Frame->Pixels.SetNumUninitialized(FrameBytes);
		return Frame;
	}

	void ReleaseFrame(TUniquePtr<FEncoderFrame>&& Frame)
	{
		if (!Frame.IsValid())
		{
			return;
		}

		FScopeLock Lock(&PoolLock);
		if (FramePool.Num() < Config.MaxQueuedFrames + 1)
		{
			FramePool.Add(MoveTemp(Frame));
		}
	}

	void SetFailed(const FString& Error)
	{
		{
			FScopeLock Lock(&ErrorLock);
			if (LastError.IsEmpty())
			{
				LastError = Error;
			}
		}

		if (!bFailed.exchange(true, std::memory_order_release))
		{
			UE_LOG(LogInputRecordingVideo, Error, TEXT("Video encoder failed: %s"), *Error);
		}
	}

	//~ State ---------------------------------------------------------------------------------------

	FInputRecordingVideoEncoderConfig Config;

	int32 FrameStride = 0;
	int32 FrameBytes = 0;

	TUniquePtr<FRunnableThread> Thread;
	FEvent* InitCompleteEvent = nullptr;
	FEvent* WorkAvailableEvent = nullptr;

	/** Single producer (render thread), single consumer (encoder thread). */
	TQueue<TUniquePtr<FEncoderFrame>, EQueueMode::Spsc> PendingFrames;
	std::atomic<int32> QueueDepth{ 0 };

	mutable FCriticalSection PoolLock;
	TArray<TUniquePtr<FEncoderFrame>> FramePool;

	mutable FCriticalSection ErrorLock;
	FString LastError;

	std::atomic<bool> bInitialised{ false };
	std::atomic<bool> bStopRequested{ false };
	std::atomic<bool> bFailed{ false };

	std::atomic<int64> SubmittedFrames{ 0 };
	std::atomic<int64> EncodedFrames{ 0 };
	std::atomic<int64> DroppedFrames{ 0 };

	//~ Worker-thread only --------------------------------------------------------------------------

	TComPtr<IMFSinkWriter> SinkWriter;
	DWORD StreamIndex = 0;
	LONGLONG LastSampleTime = 0;
	bool bHasWrittenSample = false;
	bool bMediaFoundationStarted = false;
};

#endif // PLATFORM_WINDOWS

// ---------------------------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------------------------

TUniquePtr<IInputRecordingVideoEncoder> IInputRecordingVideoEncoder::Create()
{
#if PLATFORM_WINDOWS
	return MakeUnique<FMediaFoundationVideoEncoder>();
#else
	UE_LOG(LogInputRecordingVideo, Warning,
		TEXT("No video encoder backend is implemented for this platform. Input recording will still ")
		TEXT("produce a .ghost file; no .mp4 will be written."));
	return nullptr;
#endif
}

bool IInputRecordingVideoEncoder::IsSupportedOnThisPlatform()
{
#if PLATFORM_WINDOWS
	return true;
#else
	return false;
#endif
}
