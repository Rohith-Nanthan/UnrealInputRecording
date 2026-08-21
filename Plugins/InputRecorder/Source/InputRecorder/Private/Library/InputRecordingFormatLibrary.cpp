// Copyright Epic Games, Inc. All Rights Reserved.

#include "Library/InputRecordingFormatLibrary.h"

namespace InputRecordingFormatPrivate
{
	FString Pluralise(int32 Count, const TCHAR* Singular, const TCHAR* Plural)
	{
		return FString::Printf(TEXT("%d %s"), Count, Count == 1 ? Singular : Plural);
	}
}

FString UInputRecordingFormatLibrary::FormatRelativeTime(const FDateTime& UtcTimestamp)
{
	return FormatRelativeTimespan(FDateTime::UtcNow() - UtcTimestamp);
}

FString UInputRecordingFormatLibrary::FormatRelativeTimespan(const FTimespan& Elapsed)
{
	using namespace InputRecordingFormatPrivate;

	// A clock that went backwards between writing a manifest and reading it is a real thing on
	// a machine that syncs time, so the future case reads "in ..." rather than showing a
	// negative number.
	const bool bInFuture = Elapsed.GetTicks() < 0;
	const FTimespan Absolute = bInFuture ? -Elapsed : Elapsed;

	const int32 Days = static_cast<int32>(Absolute.GetDays());
	const int32 Hours = Absolute.GetHours();
	const int32 Minutes = Absolute.GetMinutes();
	const int32 Seconds = Absolute.GetSeconds();

	if (Days == 0 && Hours == 0 && Minutes == 0 && Seconds == 0)
	{
		return TEXT("just now");
	}

	TArray<FString, TInlineAllocator<4>> Parts;

	// Only the non-zero components, in order. A zero in the middle is dropped entirely, so
	// two hours and three seconds reads "2 hours 3 seconds ago", not "2 hours 0 minutes ...".
	if (Days > 0)
	{
		Parts.Add(Pluralise(Days, TEXT("day"), TEXT("days")));
	}
	if (Hours > 0)
	{
		Parts.Add(Pluralise(Hours, TEXT("hour"), TEXT("hours")));
	}
	if (Minutes > 0)
	{
		Parts.Add(Pluralise(Minutes, TEXT("minute"), TEXT("minutes")));
	}
	if (Seconds > 0)
	{
		Parts.Add(Pluralise(Seconds, TEXT("second"), TEXT("seconds")));
	}

	const FString Joined = FString::Join(Parts, TEXT(" "));
	return bInFuture ? FString::Printf(TEXT("in %s"), *Joined) : FString::Printf(TEXT("%s ago"), *Joined);
}

FString UInputRecordingFormatLibrary::FormatByteSize(int64 Bytes)
{
	if (Bytes < 0)
	{
		Bytes = 0;
	}

	constexpr int64 Kilobyte = 1024;
	constexpr int64 Megabyte = Kilobyte * 1024;
	constexpr int64 Gigabyte = Megabyte * 1024;

	if (Bytes >= Gigabyte)
	{
		return FString::Printf(TEXT("%.1f GB"), static_cast<double>(Bytes) / static_cast<double>(Gigabyte));
	}
	if (Bytes >= Megabyte)
	{
		return FString::Printf(TEXT("%.1f MB"), static_cast<double>(Bytes) / static_cast<double>(Megabyte));
	}
	if (Bytes >= Kilobyte)
	{
		return FString::Printf(TEXT("%.1f KB"), static_cast<double>(Bytes) / static_cast<double>(Kilobyte));
	}

	return FString::Printf(TEXT("%lld B"), Bytes);
}

FString UInputRecordingFormatLibrary::FormatDurationClock(float Seconds)
{
	const int32 Total = FMath::Max(0, FMath::RoundToInt(Seconds));
	const int32 Hours = Total / 3600;
	const int32 Minutes = (Total % 3600) / 60;
	const int32 RemainingSeconds = Total % 60;

	if (Hours > 0)
	{
		return FString::Printf(TEXT("%d:%02d:%02d"), Hours, Minutes, RemainingSeconds);
	}

	return FString::Printf(TEXT("%d:%02d"), Minutes, RemainingSeconds);
}
