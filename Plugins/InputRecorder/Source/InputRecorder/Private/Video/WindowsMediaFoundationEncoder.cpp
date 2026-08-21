// Copyright Epic Games, Inc. All Rights Reserved.

#include "Video/WindowsMediaFoundationEncoder.h"

#if PLATFORM_WINDOWS

#include "InputRecordingLog.h"
#include "Misc/Paths.h"

#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"

namespace WindowsMediaFoundationEncoderPrivate
{
	template <typename T>
	void SafeRelease(T*& Pointer)
	{
		if (Pointer)
		{
			Pointer->Release();
			Pointer = nullptr;
		}
	}

	bool Check(HRESULT Result, const TCHAR* What)
	{
		if (FAILED(Result))
		{
			UE_LOG(LogRecordingVideo, Error, TEXT("Media Foundation: %s failed (hr 0x%08X)."), What, static_cast<uint32>(Result));
			return false;
		}
		return true;
	}
}

struct FWindowsMediaFoundationEncoder::FImpl
{
	IMFSinkWriter* SinkWriter = nullptr;
	DWORD StreamIndex = 0;
	bool bMediaFoundationStarted = false;
};

FWindowsMediaFoundationEncoder::~FWindowsMediaFoundationEncoder()
{
	Finalize();
	ReleaseResources();
}

bool FWindowsMediaFoundationEncoder::Initialize(const FVideoEncoderInitParams& Params)
{
	using namespace WindowsMediaFoundationEncoderPrivate;

	if (bInitialized)
	{
		return true;
	}

	if (Params.Width <= 0 || Params.Height <= 0)
	{
		UE_LOG(LogRecordingVideo, Error, TEXT("Encoder refused a %dx%d frame size."), Params.Width, Params.Height);
		return false;
	}

	// H.264 refuses odd dimensions outright, so both axes are snapped down to even before
	// anything else touches them.
	Config = Params;
	Config.Width = Params.Width & ~1;
	Config.Height = Params.Height & ~1;

	Impl = MakeUnique<FImpl>();

	if (!Check(MFStartup(MF_VERSION, MFSTARTUP_LITE), TEXT("MFStartup")))
	{
		Impl.Reset();
		return false;
	}
	Impl->bMediaFoundationStarted = true;

	const FString AbsolutePath = FPaths::ConvertRelativePathToFull(Config.OutputPath);

	IMFAttributes* Attributes = nullptr;
	if (!Check(MFCreateAttributes(&Attributes, 2), TEXT("MFCreateAttributes")))
	{
		ReleaseResources();
		return false;
	}

	// Hardware transforms when the machine has them, and low latency so Finalize does not have
	// to drain a deep lookahead buffer at the end of every take.
	Attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, 1);
	Attributes->SetUINT32(MF_LOW_LATENCY, 1);

	const bool bWriterCreated = Check(
		MFCreateSinkWriterFromURL(*AbsolutePath, nullptr, Attributes, &Impl->SinkWriter),
		TEXT("MFCreateSinkWriterFromURL"));
	SafeRelease(Attributes);

	if (!bWriterCreated)
	{
		ReleaseResources();
		return false;
	}

	// Output: H.264 in MP4.
	IMFMediaType* OutputType = nullptr;
	bool bOk = Check(MFCreateMediaType(&OutputType), TEXT("MFCreateMediaType (output)"));
	if (bOk)
	{
		bOk &= Check(OutputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), TEXT("output major type"));
		bOk &= Check(OutputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264), TEXT("output subtype"));
		bOk &= Check(OutputType->SetUINT32(MF_MT_AVG_BITRATE, static_cast<UINT32>(Config.BitRate)), TEXT("output bitrate"));
		bOk &= Check(OutputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive), TEXT("output interlace mode"));
		bOk &= Check(MFSetAttributeSize(OutputType, MF_MT_FRAME_SIZE, Config.Width, Config.Height), TEXT("output frame size"));
		bOk &= Check(MFSetAttributeRatio(OutputType, MF_MT_FRAME_RATE, Config.FrameRate, 1), TEXT("output frame rate"));
		bOk &= Check(MFSetAttributeRatio(OutputType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1), TEXT("output pixel aspect ratio"));
	}
	if (bOk)
	{
		bOk = Check(Impl->SinkWriter->AddStream(OutputType, &Impl->StreamIndex), TEXT("AddStream"));
	}
	SafeRelease(OutputType);

	if (!bOk)
	{
		ReleaseResources();
		return false;
	}

	// Input: 32-bit BGRA, which is what UMediaCapture's CPU readback hands us.
	IMFMediaType* InputType = nullptr;
	bOk = Check(MFCreateMediaType(&InputType), TEXT("MFCreateMediaType (input)"));
	if (bOk)
	{
		bOk &= Check(InputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), TEXT("input major type"));
		bOk &= Check(InputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32), TEXT("input subtype"));
		bOk &= Check(InputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive), TEXT("input interlace mode"));
		bOk &= Check(MFSetAttributeSize(InputType, MF_MT_FRAME_SIZE, Config.Width, Config.Height), TEXT("input frame size"));
		bOk &= Check(MFSetAttributeRatio(InputType, MF_MT_FRAME_RATE, Config.FrameRate, 1), TEXT("input frame rate"));
		bOk &= Check(MFSetAttributeRatio(InputType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1), TEXT("input pixel aspect ratio"));

		// Declaring a positive default stride tells Media Foundation the buffer is top-down.
		// Without it MF assumes the legacy DIB bottom-up convention for RGB32 and the video
		// comes out upside down on some machines and not others.
		bOk &= Check(InputType->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(Config.Width * 4)), TEXT("input default stride"));
	}
	if (bOk)
	{
		bOk = Check(Impl->SinkWriter->SetInputMediaType(Impl->StreamIndex, InputType, nullptr), TEXT("SetInputMediaType"));
	}
	SafeRelease(InputType);

	if (!bOk)
	{
		ReleaseResources();
		return false;
	}

	if (!Check(Impl->SinkWriter->BeginWriting(), TEXT("BeginWriting")))
	{
		ReleaseResources();
		return false;
	}

	bInitialized = true;
	bFinalized = false;
	FramesWritten = 0;

	UE_LOG(LogRecordingVideo, Log, TEXT("Encoder ready: %dx%d @ %d fps, %d kbps, source is %s, writing %s."),
		Config.Width, Config.Height, Config.FrameRate, Config.BitRate / 1000,
		Config.bSourceIsBottomUp ? TEXT("bottom-up") : TEXT("top-down"), *AbsolutePath);

	return true;
}

bool FWindowsMediaFoundationEncoder::EncodeFrame(const uint8* Pixels, int32 Stride, int64 TimestampMicroseconds)
{
	using namespace WindowsMediaFoundationEncoderPrivate;

	if (!bInitialized || bFinalized || !Impl || !Impl->SinkWriter || !Pixels)
	{
		return false;
	}

	const LONG DestStride = static_cast<LONG>(Config.Width * 4);
	const DWORD BufferSize = static_cast<DWORD>(DestStride) * static_cast<DWORD>(Config.Height);

	IMFMediaBuffer* Buffer = nullptr;
	if (!Check(MFCreateMemoryBuffer(BufferSize, &Buffer), TEXT("MFCreateMemoryBuffer")))
	{
		return false;
	}

	BYTE* Destination = nullptr;
	if (!Check(Buffer->Lock(&Destination, nullptr, nullptr), TEXT("IMFMediaBuffer::Lock")))
	{
		SafeRelease(Buffer);
		return false;
	}

	// A negative source stride starting from the last row is how MFCopyImage is told to flip.
	const BYTE* Source = reinterpret_cast<const BYTE*>(Pixels);
	LONG SourceStride = static_cast<LONG>(Stride);
	if (Config.bSourceIsBottomUp)
	{
		Source += static_cast<SIZE_T>(Stride) * static_cast<SIZE_T>(Config.Height - 1);
		SourceStride = -SourceStride;
	}

	const HRESULT CopyResult = MFCopyImage(Destination, DestStride, Source, SourceStride, DestStride, Config.Height);

	Buffer->Unlock();

	if (!Check(CopyResult, TEXT("MFCopyImage")))
	{
		SafeRelease(Buffer);
		return false;
	}

	Buffer->SetCurrentLength(BufferSize);

	IMFSample* Sample = nullptr;
	bool bOk = Check(MFCreateSample(&Sample), TEXT("MFCreateSample"));
	if (bOk)
	{
		bOk = Check(Sample->AddBuffer(Buffer), TEXT("IMFSample::AddBuffer"));
	}

	if (bOk)
	{
		// Media Foundation timestamps are 100-nanosecond units.
		const LONGLONG SampleTime = static_cast<LONGLONG>(TimestampMicroseconds) * 10;
		const LONGLONG SampleDuration = static_cast<LONGLONG>(10000000.0 / FMath::Max(1, Config.FrameRate));

		bOk = Check(Sample->SetSampleTime(SampleTime), TEXT("SetSampleTime"));
		bOk &= Check(Sample->SetSampleDuration(SampleDuration), TEXT("SetSampleDuration"));
	}

	if (bOk)
	{
		bOk = Check(Impl->SinkWriter->WriteSample(Impl->StreamIndex, Sample), TEXT("WriteSample"));
	}

	SafeRelease(Sample);
	SafeRelease(Buffer);

	if (bOk)
	{
		++FramesWritten;
	}

	return bOk;
}

void FWindowsMediaFoundationEncoder::Finalize()
{
	using namespace WindowsMediaFoundationEncoderPrivate;

	if (!bInitialized || bFinalized || !Impl)
	{
		return;
	}

	bFinalized = true;

	if (Impl->SinkWriter)
	{
		if (FramesWritten == 0)
		{
			// Finalizing a stream that never received a sample produces a zero-byte file that
			// looks like a real recording until somebody tries to play it.
			UE_LOG(LogRecordingVideo, Warning, TEXT("Encoder finalized without a single frame; %s will be unplayable."),
				*Config.OutputPath);
		}

		Check(Impl->SinkWriter->Finalize(), TEXT("IMFSinkWriter::Finalize"));
	}

	UE_LOG(LogRecordingVideo, Log, TEXT("Encoder finalized after %lld frame(s): %s"), FramesWritten, *Config.OutputPath);

	ReleaseResources();
}

void FWindowsMediaFoundationEncoder::ReleaseResources()
{
	using namespace WindowsMediaFoundationEncoderPrivate;

	if (!Impl)
	{
		bInitialized = false;
		return;
	}

	SafeRelease(Impl->SinkWriter);

	if (Impl->bMediaFoundationStarted)
	{
		MFShutdown();
		Impl->bMediaFoundationStarted = false;
	}

	Impl.Reset();
	bInitialized = false;
}

#endif // PLATFORM_WINDOWS

// The factory lives beside the only backend that currently exists so that adding a platform
// means adding a file and one branch here, and touching nothing else.
#include "Video/IVideoEncoderBackend.h"

TUniquePtr<IVideoEncoderBackend> VideoEncoderBackend::Create()
{
#if PLATFORM_WINDOWS
	return MakeUnique<FWindowsMediaFoundationEncoder>();
#else
	// No backend on this platform. Not an error: capture is skipped and input recording carries
	// on, because a .ghost with no .mp4 is a usable take and a lost .ghost is a re-performance.
	return nullptr;
#endif
}
