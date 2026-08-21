// Copyright Epic Games, Inc. All Rights Reserved.

#include "MatchInput/MatchInputCueBuilder.h"

#include "InputRecordingLog.h"
#include "InputActionValue.h"

namespace MatchInputCueBuilderPrivate
{
	/** Magnitude at which a component earns a direction word in a description. */
	constexpr float DirectionWordThreshold = 0.35f;

	/** Bare asset name out of a path, or the string unchanged when it is already bare. */
	FString ToShortName(const FString& PathOrName)
	{
		int32 DotIndex = INDEX_NONE;
		if (PathOrName.FindLastChar(TEXT('.'), DotIndex))
		{
			return PathOrName.Mid(DotIndex + 1);
		}

		int32 SlashIndex = INDEX_NONE;
		if (PathOrName.FindLastChar(TEXT('/'), SlashIndex))
		{
			return PathOrName.Mid(SlashIndex + 1);
		}

		return PathOrName;
	}

	FString DescribeAxis2D(const FVector& Value)
	{
		TArray<FString, TInlineAllocator<2>> Words;

		// Third-person convention: +Y is forward, +X is right.
		if (Value.Y >= DirectionWordThreshold)
		{
			Words.Add(TEXT("Fwd"));
		}
		else if (Value.Y <= -DirectionWordThreshold)
		{
			Words.Add(TEXT("Back"));
		}

		if (Value.X >= DirectionWordThreshold)
		{
			Words.Add(TEXT("Right"));
		}
		else if (Value.X <= -DirectionWordThreshold)
		{
			Words.Add(TEXT("Left"));
		}

		return Words.Num() > 0 ? FString::Join(Words, TEXT("-")) : TEXT("Neutral");
	}

	FString DescribeAxis3D(const FVector& Value)
	{
		FString Words = DescribeAxis2D(Value);

		if (Value.Z >= DirectionWordThreshold)
		{
			Words = (Words == TEXT("Neutral")) ? TEXT("Up") : Words + TEXT("-Up");
		}
		else if (Value.Z <= -DirectionWordThreshold)
		{
			Words = (Words == TEXT("Neutral")) ? TEXT("Down") : Words + TEXT("-Down");
		}

		return Words;
	}
}

FString UMatchInputCueBuilder::FormatInputDescription(const FString& ActionName, uint8 ValueType, const FVector& Value)
{
	using namespace MatchInputCueBuilderPrivate;

	const FString ShortName = ToShortName(ActionName);
	const EInputActionValueType Type = static_cast<EInputActionValueType>(ValueType);

	switch (Type)
	{
	case EInputActionValueType::Boolean:
		return FString::Printf(TEXT("%s [%s]"), *ShortName,
			FMath::Abs(Value.X) > KINDA_SMALL_NUMBER ? TEXT("Pressed") : TEXT("Released"));

	case EInputActionValueType::Axis1D:
		return FString::Printf(TEXT("%s [%s | X=%+.2f]"), *ShortName,
			Value.X >= 0.0f ? TEXT("Positive") : TEXT("Negative"), Value.X);

	case EInputActionValueType::Axis2D:
		return FString::Printf(TEXT("%s [%s | X=%+.2f Y=%+.2f]"), *ShortName,
			*DescribeAxis2D(Value), Value.X, Value.Y);

	case EInputActionValueType::Axis3D:
		return FString::Printf(TEXT("%s [%s | X=%+.2f Y=%+.2f Z=%+.2f]"), *ShortName,
			*DescribeAxis3D(Value), Value.X, Value.Y, Value.Z);

	default:
		return ShortName;
	}
}

bool UMatchInputCueBuilder::IsActionIgnored(const FString& ActionPathOrName, const TArray<FString>& IgnoredActions)
{
	using namespace MatchInputCueBuilderPrivate;

	const FString ShortName = ToShortName(ActionPathOrName);

	for (const FString& Ignored : IgnoredActions)
	{
		if (Ignored.IsEmpty())
		{
			continue;
		}

		// A pattern is matched against both spellings; a literal entry is compared against both
		// spellings exactly. Wildcards are opt-in per entry rather than a mode, so a list can
		// mix "IA_Jump" and "*Look*" and each behaves the way it reads.
		if (Ignored.Contains(TEXT("*")) || Ignored.Contains(TEXT("?")))
		{
			if (ActionPathOrName.MatchesWildcard(Ignored, ESearchCase::IgnoreCase) ||
				ShortName.MatchesWildcard(Ignored, ESearchCase::IgnoreCase))
			{
				return true;
			}

			continue;
		}

		// Accept either spelling so a designer can paste a full path or type the asset name.
		if (Ignored.Equals(ActionPathOrName, ESearchCase::IgnoreCase) ||
			ToShortName(Ignored).Equals(ShortName, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	return false;
}

bool UMatchInputCueBuilder::DoesValueMatch(uint8 ValueType, const FVector& Expected, const FVector& Actual, float DirectionTolerance, float PressThreshold)
{
	const float ActualMagnitude = Actual.Size();
	if (ActualMagnitude < PressThreshold)
	{
		return false;
	}

	const EInputActionValueType Type = static_cast<EInputActionValueType>(ValueType);
	if (Type == EInputActionValueType::Boolean)
	{
		// A boolean has no direction to get wrong; presence above the dead zone is the answer.
		return true;
	}

	const float ExpectedMagnitude = Expected.Size();
	if (ExpectedMagnitude < KINDA_SMALL_NUMBER)
	{
		// A cue with no direction cannot be judged on direction, so fall back to presence.
		return true;
	}

	// Direction only. Requiring magnitude to match would mean asking somebody to push a stick
	// exactly as far as it went last time, which nobody can do.
	const float Dot = FVector::DotProduct(Expected / ExpectedMagnitude, Actual / ActualMagnitude);
	return Dot >= DirectionTolerance;
}

TArray<FMatchInputCue> UMatchInputCueBuilder::BuildMatchInputCues(const FInputRecording& Recording, const FMatchInputCueBuildOptions& Options)
{
	TArray<FMatchInputCue> Cues;

	const int32 ActionCount = Recording.Header.ActionPaths.Num();
	if (ActionCount == 0 || Recording.Samples.Num() == 0)
	{
		return Cues;
	}

	// Precompute per-action admissibility once instead of re-testing strings for every sample.
	TArray<bool> bActionAdmitted;
	bActionAdmitted.Init(true, ActionCount);
	for (int32 Index = 0; Index < ActionCount; ++Index)
	{
		const FString& Path = Recording.Header.ActionPaths[Index];

		if (IsActionIgnored(Path, Options.IgnoredActions))
		{
			bActionAdmitted[Index] = false;
			continue;
		}

		if (Options.bIgnoreFrameDeltaActions && Recording.Header.FrameDeltaActionIndices.Contains(Index))
		{
			bActionAdmitted[Index] = false;
		}
	}

	// The sample stream is delta-compressed, so "the value before this sample" is simply the
	// last sample seen for that action. Onset detection needs exactly that one value held.
	TArray<float> LastMagnitude;
	LastMagnitude.Init(0.0f, ActionCount);

	TArray<float> LastCueTime;
	LastCueTime.Init(TNumericLimits<float>::Lowest(), ActionCount);

	for (const FRecordedInputSample& Sample : Recording.Samples)
	{
		if (!bActionAdmitted.IsValidIndex(Sample.ActionIndex) || !bActionAdmitted[Sample.ActionIndex])
		{
			continue;
		}

		const float Magnitude = Sample.Value.Size();
		const float Previous = LastMagnitude[Sample.ActionIndex];
		LastMagnitude[Sample.ActionIndex] = Magnitude;

		const bool bIsOnset = Previous < Options.PressThreshold && Magnitude >= Options.PressThreshold;
		if (!bIsOnset)
		{
			continue;
		}

		// Spacing is per action: a rapid Jump-then-Move pair is two real cues, whereas one
		// action flickering across the threshold twice in a frame or two is a single press.
		if (Sample.TimeSeconds - LastCueTime[Sample.ActionIndex] < Options.MinimumCueSpacing)
		{
			continue;
		}
		LastCueTime[Sample.ActionIndex] = Sample.TimeSeconds;

		const FString& ActionPath = Recording.Header.ActionPaths[Sample.ActionIndex];

		FMatchInputCue& Cue = Cues.AddDefaulted_GetRef();
		Cue.Action = TSoftObjectPtr<UInputAction>(FSoftObjectPath(ActionPath));
		Cue.ActionName = Recording.Header.GetActionShortName(Sample.ActionIndex);
		Cue.ActionIndex = Sample.ActionIndex;
		Cue.FrameIndex = Sample.FrameIndex;
		Cue.TimeSeconds = Sample.TimeSeconds;
		Cue.ValueType = Sample.ValueType;
		Cue.ExpectedValue = Sample.Value;
		Cue.Description = FormatInputDescription(Cue.ActionName, Cue.ValueType, Cue.ExpectedValue);
	}

	// Intervals are a second pass because a cue's gap is measured against the previous cue that
	// survived filtering, not against whatever sample happened to precede it in the stream.
	float PreviousTime = 0.0f;
	for (FMatchInputCue& Cue : Cues)
	{
		Cue.IntervalFromPreviousSeconds = FMath::Max(0.0f, Cue.TimeSeconds - PreviousTime);
		PreviousTime = Cue.TimeSeconds;
	}

	UE_LOG(LogMatchInput, Log, TEXT("Built %d cue(s) from %d sample(s) across %d action(s)."),
		Cues.Num(), Recording.Samples.Num(), ActionCount);

	return Cues;
}
