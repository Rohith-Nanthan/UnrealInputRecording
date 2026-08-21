// Copyright Epic Games, Inc. All Rights Reserved.

#include "InputReplay/InputRecordingSerializer.h"

#include "InputRecordingLog.h"
#include "HAL/PlatformFileManager.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

FArchive& operator<<(FArchive& Ar, FRecordedInputSample& Sample)
{
	Ar << Sample.ActionName;
	Ar << Sample.ActionIndex;
	Ar << Sample.FrameIndex;
	Ar << Sample.TimeSeconds;
	Ar << Sample.TriggerEvent;
	Ar << Sample.ValueType;
	Ar << Sample.Value;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FInputRecordingHeader& Header)
{
	Ar << Header.RecordingId;
	Ar << Header.DisplayName;

	// FDateTime has no archive operator of its own; its tick count is the whole state.
	int64 Ticks = Header.RecordedAtUtc.GetTicks();
	Ar << Ticks;
	if (Ar.IsLoading())
	{
		Header.RecordedAtUtc = FDateTime(Ticks);
	}

	Ar << Header.LevelName;
	Ar << Header.EngineVersion;

	uint8 TimeMode = static_cast<uint8>(Header.TimeMode);
	Ar << TimeMode;
	if (Ar.IsLoading())
	{
		Header.TimeMode = static_cast<EInputReplayTimeMode>(TimeMode);
	}

	Ar << Header.LogicalTicksPerSecond;
	Ar << Header.TotalFrames;
	Ar << Header.RandomSeed;
	Ar << Header.ActionPaths;
	Ar << Header.FrameDeltaActionIndices;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FInputRecording& Recording)
{
	Ar << Recording.Header;
	Ar << Recording.Samples;
	Ar << Recording.FrameDeltaSeconds;
	return Ar;
}

FString UInputRecordingSerializer::GetGhostPath(const FString& AbsoluteBasePath)
{
	return AbsoluteBasePath + TEXT(".ghost");
}

FString UInputRecordingSerializer::GetGhostJsonPath(const FString& AbsoluteBasePath)
{
	return AbsoluteBasePath + TEXT(".ghost.json");
}

bool UInputRecordingSerializer::SaveRecording(const FInputRecording& Recording, const FString& AbsoluteBasePath, bool bAlsoExportJson)
{
	if (AbsoluteBasePath.IsEmpty())
	{
		UE_LOG(LogInputRecording, Error, TEXT("SaveRecording called with an empty base path."));
		return false;
	}

	TArray<uint8> Bytes;
	{
		FMemoryWriter Writer(Bytes, /*bIsPersistent=*/true);

		uint32 Magic = GInputRecordingGhostMagic;
		uint32 Version = GInputRecordingGhostVersion;
		Writer << Magic;
		Writer << Version;

		// const_cast is safe here: the archive is write-only, so nothing is mutated.
		FInputRecording& MutableRecording = const_cast<FInputRecording&>(Recording);
		Writer << MutableRecording;
	}

	const FString GhostPath = GetGhostPath(AbsoluteBasePath);
	if (!FFileHelper::SaveArrayToFile(Bytes, *GhostPath))
	{
		UE_LOG(LogInputRecording, Error, TEXT("Failed to write ghost file %s."), *GhostPath);
		return false;
	}

	UE_LOG(LogInputRecording, Log, TEXT("Saved recording %s (%d samples, %.2fs) to %s."),
		*Recording.Header.DisplayName, Recording.Samples.Num(), Recording.GetDurationSeconds(), *GhostPath);

	if (bAlsoExportJson && !SaveRecordingAsJson(Recording, AbsoluteBasePath))
	{
		// A missing JSON copy is a debugging inconvenience, never a lost take.
		UE_LOG(LogInputRecording, Warning, TEXT("Ghost saved but the JSON copy failed for %s."), *AbsoluteBasePath);
	}

	return true;
}

bool UInputRecordingSerializer::LoadRecording(const FString& AbsoluteBasePath, FInputRecording& OutRecording)
{
	OutRecording.Reset();

	const FString GhostPath = GetGhostPath(AbsoluteBasePath);

	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *GhostPath))
	{
		UE_LOG(LogInputRecording, Warning, TEXT("No ghost file at %s."), *GhostPath);
		return false;
	}

	FMemoryReader Reader(Bytes, /*bIsPersistent=*/true);

	uint32 Magic = 0;
	uint32 Version = 0;
	Reader << Magic;
	Reader << Version;

	if (Magic != GInputRecordingGhostMagic)
	{
		UE_LOG(LogInputRecording, Error, TEXT("%s is not a ghost file (magic 0x%08X)."), *GhostPath, Magic);
		return false;
	}

	if (Version > GInputRecordingGhostVersion)
	{
		UE_LOG(LogInputRecording, Error,
			TEXT("%s was written by a newer build (file version %u, this build reads up to %u). Refusing to load."),
			*GhostPath, Version, GInputRecordingGhostVersion);
		return false;
	}

	Reader << OutRecording;

	if (Reader.IsError())
	{
		UE_LOG(LogInputRecording, Error, TEXT("Ghost file %s is truncated or corrupt."), *GhostPath);
		OutRecording.Reset();
		return false;
	}

	UE_LOG(LogInputRecording, Log, TEXT("Loaded recording %s (%d samples, %d actions, %.2fs)."),
		*OutRecording.Header.DisplayName, OutRecording.Samples.Num(),
		OutRecording.Header.ActionPaths.Num(), OutRecording.GetDurationSeconds());

	return true;
}

bool UInputRecordingSerializer::SaveRecordingAsJson(const FInputRecording& Recording, const FString& AbsoluteBasePath)
{
	FString Json;
	if (!FJsonObjectConverter::UStructToJsonObjectString(Recording, Json, 0, 0, 0, nullptr, /*bPrettyPrint=*/true))
	{
		UE_LOG(LogInputRecording, Warning, TEXT("Could not convert recording to JSON for %s."), *AbsoluteBasePath);
		return false;
	}

	const FString JsonPath = GetGhostJsonPath(AbsoluteBasePath);
	if (!FFileHelper::SaveStringToFile(Json, *JsonPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogInputRecording, Warning, TEXT("Failed to write JSON copy %s."), *JsonPath);
		return false;
	}

	return true;
}

bool UInputRecordingSerializer::DoesRecordingExist(const FString& AbsoluteBasePath)
{
	return FPlatformFileManager::Get().GetPlatformFile().FileExists(*GetGhostPath(AbsoluteBasePath));
}
