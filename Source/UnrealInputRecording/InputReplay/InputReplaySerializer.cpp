// Copyright (c) Your Studio. All Rights Reserved.

#include "InputReplaySerializer.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogInputReplayIO, Log, All);

FString UInputReplaySerializer::GetRecordingDirectory()
{
	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), InputReplay::RecordingSubDir);

	// Cheap and idempotent; MakeDirectory with bTree=true is a no-op if it already exists.
	IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);
	return Dir;
}

FString UInputReplaySerializer::ResolveRecordingPath(const FString& FileName, bool bJson)
{
	const FString Extension = bJson ? InputReplay::JsonExtension : InputReplay::BinaryExtension;

	// Absolute path supplied by a tool or automation harness - respect it verbatim.
	if (FPaths::IsRelative(FileName) == false)
	{
		return FileName.EndsWith(Extension) ? FileName : FileName + Extension;
	}

	FString Sanitised = FPaths::GetCleanFilename(FileName);
	Sanitised = FPaths::MakeValidFileName(Sanitised, TEXT('_'));

	if (!Sanitised.EndsWith(Extension))
	{
		// Strip a wrong-format extension the caller may have typed.
		Sanitised.RemoveFromEnd(InputReplay::BinaryExtension);
		Sanitised.RemoveFromEnd(InputReplay::JsonExtension);
		Sanitised += Extension;
	}

	return FPaths::Combine(GetRecordingDirectory(), Sanitised);
}

TArray<FString> UInputReplaySerializer::ListRecordings(bool bJson)
{
	TArray<FString> Found;
	const FString Extension = bJson ? InputReplay::JsonExtension : InputReplay::BinaryExtension;

	IFileManager::Get().FindFiles(Found, *(GetRecordingDirectory() / TEXT("*") + Extension), /*Files=*/true, /*Directories=*/false);

	for (FString& Entry : Found)
	{
		Entry.RemoveFromEnd(Extension);
	}
	return Found;
}

bool UInputReplaySerializer::SaveBinary(const FInputRecording& Recording, const FString& FileName, FString& OutError)
{
	const FString Path = ResolveRecordingPath(FileName, /*bJson=*/false);

	TArray<uint8> Buffer;

	// FMemoryWriter over an owned buffer. (FBufferArchive works identically - it simply *is* a
	// TArray<uint8> - but FMemoryWriter keeps the buffer separate from the archive, which is
	// tidier when you later want to pipe it through FArchiveSaveCompressedProxy.)
	FMemoryWriter Writer(Buffer, /*bIsPersistent=*/true);

	uint32 Magic = InputReplay::FileMagic;
	uint32 Version = InputReplay::FileVersion;
	Writer << Magic;
	Writer << Version;

	// const_cast is the standard idiom here: FArchive operators are bidirectional by design and
	// this archive is write-only (Writer.IsSaving() == true).
	FInputRecording& Mutable = const_cast<FInputRecording&>(Recording);
	Writer << Mutable;

	Writer.FlushCache();
	Writer.Close();

	if (!FFileHelper::SaveArrayToFile(Buffer, *Path))
	{
		OutError = FString::Printf(TEXT("Failed to write '%s' (disk full or path not writable?)"), *Path);
		return false;
	}

	UE_LOG(LogInputReplayIO, Log, TEXT("Saved recording: %s (%d frames, %d bytes)"),
		*Path, Recording.Frames.Num(), Buffer.Num());
	return true;
}

bool UInputReplaySerializer::LoadBinary(FInputRecording& OutRecording, const FString& FileName, FString& OutError)
{
	const FString Path = ResolveRecordingPath(FileName, /*bJson=*/false);

	TArray<uint8> Buffer;
	if (!FFileHelper::LoadFileToArray(Buffer, *Path))
	{
		OutError = FString::Printf(TEXT("Recording not found: '%s'"), *Path);
		return false;
	}

	FMemoryReader Reader(Buffer, /*bIsPersistent=*/true);
	Reader.Seek(0);

	uint32 Magic = 0;
	uint32 Version = 0;
	Reader << Magic;
	Reader << Version;

	if (Magic != InputReplay::FileMagic)
	{
		OutError = FString::Printf(TEXT("'%s' is not an input recording (bad magic 0x%08X)"), *Path, Magic);
		return false;
	}
	if (Version > InputReplay::FileVersion)
	{
		OutError = FString::Printf(TEXT("'%s' was written by a newer build (file v%u, runtime v%u)"),
			*Path, Version, InputReplay::FileVersion);
		return false;
	}

	OutRecording.Reset();
	Reader << OutRecording;

	if (Reader.IsError())
	{
		OutError = FString::Printf(TEXT("Truncated or corrupt recording: '%s'"), *Path);
		OutRecording.Reset();
		return false;
	}

	Reader.Close();

	UE_LOG(LogInputReplayIO, Log, TEXT("Loaded recording: %s (%d frames, %.2fs)"),
		*Path, OutRecording.Frames.Num(), OutRecording.GetDurationSeconds());
	return true;
}

bool UInputReplaySerializer::SaveJson(const FInputRecording& Recording, const FString& FileName, FString& OutError)
{
	const FString Path = ResolveRecordingPath(FileName, /*bJson=*/true);

	FString Json;
	if (!FJsonObjectConverter::UStructToJsonObjectString(Recording, Json, /*CheckFlags=*/0, /*SkipFlags=*/0))
	{
		OutError = TEXT("FJsonObjectConverter failed to serialise the recording.");
		return false;
	}

	if (!FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("Failed to write '%s'"), *Path);
		return false;
	}

	UE_LOG(LogInputReplayIO, Log, TEXT("Saved JSON recording: %s"), *Path);
	return true;
}

bool UInputReplaySerializer::LoadJson(FInputRecording& OutRecording, const FString& FileName, FString& OutError)
{
	const FString Path = ResolveRecordingPath(FileName, /*bJson=*/true);

	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		OutError = FString::Printf(TEXT("Recording not found: '%s'"), *Path);
		return false;
	}

	OutRecording.Reset();
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &OutRecording, /*CheckFlags=*/0, /*SkipFlags=*/0))
	{
		OutError = FString::Printf(TEXT("Malformed JSON recording: '%s'"), *Path);
		OutRecording.Reset();
		return false;
	}

	return true;
}

bool UInputReplaySerializer::Save(const FInputRecording& Recording, const FString& FileName, bool bJson, FString& OutError)
{
	return bJson ? SaveJson(Recording, FileName, OutError) : SaveBinary(Recording, FileName, OutError);
}

bool UInputReplaySerializer::Load(FInputRecording& OutRecording, const FString& FileName, bool bJson, FString& OutError)
{
	return bJson ? LoadJson(OutRecording, FileName, OutError) : LoadBinary(OutRecording, FileName, OutError);
}
