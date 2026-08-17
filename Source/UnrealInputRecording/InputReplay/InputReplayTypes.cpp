// Copyright (c) Your Studio. All Rights Reserved.
//
// InputReplayTypes.cpp
//
// Hand-rolled FArchive operators. We do not rely on reflection-driven (UStruct) serialization for
// the binary path: explicit operators give us a stable, compact, version-controlled layout that
// does not silently change when someone reorders a UPROPERTY.

#include "InputReplayTypes.h"

FArchive& operator<<(FArchive& Ar, FRecordedInputFrame& Frame)
{
	Ar << Frame.FrameIndex;
	Ar << Frame.TimeSeconds;
	Ar << Frame.ActionIndex;
	Ar << Frame.TriggerEvent;
	Ar << Frame.ValueType;
	Ar << Frame.Value;			// FVector3f has a native archive operator (3 x float32)
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FReplaySyncPoint& Point)
{
	Ar << Point.FrameIndex;
	Ar << Point.Location;
	Ar << Point.Rotation;
	Ar << Point.Velocity;
	Ar << Point.ControlRotation;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FInputRecordingHeader& Header)
{
	Ar << Header.RecordingId;
	Ar << Header.DisplayName;
	Ar << Header.RecordedAtUtc;
	Ar << Header.LevelName;
	Ar << Header.EngineVersion;
	Ar << Header.TimeMode;
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
	Ar << Recording.Frames;
	Ar << Recording.FrameDeltaSeconds;
	Ar << Recording.SyncPoints;
	return Ar;
}
