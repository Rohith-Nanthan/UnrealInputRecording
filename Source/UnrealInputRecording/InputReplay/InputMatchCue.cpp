// Copyright (c) Your Studio. All Rights Reserved.

#include "InputMatchCue.h"

#include "InputTriggers.h"      // ETriggerEvent

DEFINE_LOG_CATEGORY(LogInputMatch);

namespace
{
	/**
	 * Cardinal label for a 2D axis value.
	 *
	 * Convention note: X is treated as the right/left axis and Y as the forward/back axis, which is
	 * how every Enhanced Input template (including the Third Person one) wires up its Move action.
	 * Purely cosmetic - matching itself is done on the raw vector.
	 */
	FString DescribeCardinal2D(const FVector& Value, double DeadZone)
	{
		FString Label;

		if (Value.Y > DeadZone)			{ Label = TEXT("Fwd"); }
		else if (Value.Y < -DeadZone)	{ Label = TEXT("Back"); }

		if (Value.X > DeadZone)
		{
			if (!Label.IsEmpty()) { Label += TEXT("-"); }
			Label += TEXT("Right");
		}
		else if (Value.X < -DeadZone)
		{
			if (!Label.IsEmpty()) { Label += TEXT("-"); }
			Label += TEXT("Left");
		}

		return Label.IsEmpty() ? FString(TEXT("Centre")) : Label;
	}
}

FString UInputMatchLibrary::GetActionShortName(const FString& ActionPath)
{
	if (ActionPath.IsEmpty())
	{
		return TEXT("<none>");
	}

	// Paths look like "/Game/Input/Actions/IA_Jump.IA_Jump" - take everything after the last
	// separator, whichever kind it is.
	FString Result = ActionPath;

	int32 DotIndex = INDEX_NONE;
	if (Result.FindLastChar(TEXT('.'), DotIndex))
	{
		Result = Result.Mid(DotIndex + 1);
	}

	int32 SlashIndex = INDEX_NONE;
	if (Result.FindLastChar(TEXT('/'), SlashIndex))
	{
		Result = Result.Mid(SlashIndex + 1);
	}

	// Trailing ":Default__" style suffixes never appear on data assets, but a subobject path can.
	int32 ColonIndex = INDEX_NONE;
	if (Result.FindLastChar(TEXT(':'), ColonIndex))
	{
		Result = Result.Mid(ColonIndex + 1);
	}

	return Result.IsEmpty() ? ActionPath : Result;
}

FString UInputMatchLibrary::DescribeValueType(uint8 ValueType)
{
	switch (static_cast<EInputActionValueType>(ValueType))
	{
	case EInputActionValueType::Boolean:	return TEXT("Boolean");
	case EInputActionValueType::Axis1D:		return TEXT("Axis1D");
	case EInputActionValueType::Axis2D:		return TEXT("Axis2D");
	case EInputActionValueType::Axis3D:		return TEXT("Axis3D");
	default:								return FString::Printf(TEXT("Unknown(%u)"), ValueType);
	}
}

FString UInputMatchLibrary::DescribeTriggerEvent(uint8 TriggerEvent)
{
	// Written out by hand rather than through StaticEnum<ETriggerEvent>(): ETriggerEvent has changed
	// between a plain enum and a bitflag enum across engine versions, and a log line is not worth a
	// compile break.
	switch (static_cast<ETriggerEvent>(TriggerEvent))
	{
	case ETriggerEvent::None:		return TEXT("None");
	case ETriggerEvent::Triggered:	return TEXT("Triggered");
	case ETriggerEvent::Started:	return TEXT("Started");
	case ETriggerEvent::Ongoing:	return TEXT("Ongoing");
	case ETriggerEvent::Canceled:	return TEXT("Canceled");
	case ETriggerEvent::Completed:	return TEXT("Completed");
	default:						return FString::Printf(TEXT("Event(%u)"), TriggerEvent);
	}
}

FString UInputMatchLibrary::DescribeInputValue(const FString& ActionName, uint8 ValueType, const FVector& Value)
{
	const FString Name = ActionName.IsEmpty() ? TEXT("<unknown action>") : ActionName;

	switch (static_cast<EInputActionValueType>(ValueType))
	{
	case EInputActionValueType::Boolean:
		return FString::Printf(TEXT("%s [%s]"), *Name,
			FMath::Abs(Value.X) > KINDA_SMALL_NUMBER ? TEXT("pressed") : TEXT("released"));

	case EInputActionValueType::Axis1D:
		return FString::Printf(TEXT("%s [%s | %+.2f]"), *Name,
			Value.X >= 0.0 ? TEXT("Positive") : TEXT("Negative"), Value.X);

	case EInputActionValueType::Axis2D:
		return FString::Printf(TEXT("%s [%s | X=%+.2f Y=%+.2f]"), *Name,
			*DescribeCardinal2D(Value, 0.35), Value.X, Value.Y);

	case EInputActionValueType::Axis3D:
		return FString::Printf(TEXT("%s [X=%+.2f Y=%+.2f Z=%+.2f]"), *Name, Value.X, Value.Y, Value.Z);

	default:
		return FString::Printf(TEXT("%s [raw X=%+.2f Y=%+.2f Z=%+.2f]"), *Name, Value.X, Value.Y, Value.Z);
	}
}

float UInputMatchLibrary::GetFrameTimeSeconds(const FInputRecording& Recording, const FRecordedInputFrame& Frame)
{
	if (Frame.TimeSeconds > 0.0f)
	{
		return Frame.TimeSeconds;
	}

	// RecordedDeltas recordings have no fixed step, so reconstruct by summing the stored deltas.
	if (Recording.Header.TimeMode == static_cast<uint8>(EInputReplayTimeMode::RecordedDeltas))
	{
		const int32 Count = FMath::Min(Frame.FrameIndex, Recording.FrameDeltaSeconds.Num());
		float Total = 0.0f;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Total += Recording.FrameDeltaSeconds[Index];
		}
		return Total;
	}

	return Frame.FrameIndex * Recording.Header.GetFixedStepSeconds();
}

bool UInputMatchLibrary::DoesValueSatisfyCue(const FMatchInputCue& Cue, const FVector& LiveValue,
											 float PressThreshold, float DirectionTolerance)
{
	// Nothing counts until the live value actually clears the dead zone.
	if (LiveValue.Size() < PressThreshold)
	{
		return false;
	}

	if (static_cast<EInputActionValueType>(Cue.ValueType) == EInputActionValueType::Boolean)
	{
		// A digital action has no direction; clearing the threshold *is* the match.
		return true;
	}

	// Degenerate cue (should not happen - cues are built from onsets) - accept any press.
	if (Cue.ExpectedValue.IsNearlyZero())
	{
		return true;
	}

	// Compare direction, not magnitude: pushing the stick to 0.6 must satisfy a cue recorded at 1.0,
	// but pushing it the *other* way must not.
	const double Dot = FVector::DotProduct(Cue.ExpectedValue.GetSafeNormal(), LiveValue.GetSafeNormal());
	return Dot >= static_cast<double>(DirectionTolerance);
}

int32 UInputMatchLibrary::BuildMatchInputCues(const FInputRecording& Recording,
											  const FMatchInputCueBuildOptions& Options,
											  TArray<FMatchInputCue>& OutCues)
{
	OutCues.Reset();

	const TArray<FString>& ActionPaths = Recording.Header.ActionPaths;
	const int32 NumActions = ActionPaths.Num();
	if (NumActions == 0 || Recording.Frames.Num() == 0)
	{
		return 0;
	}

	// ---- 1. Work out which action indices we care about --------------------------------------
	TArray<bool> bIsDelta;
	bIsDelta.Init(false, NumActions);
	for (const int32 DeltaIndex : Recording.Header.FrameDeltaActionIndices)
	{
		if (bIsDelta.IsValidIndex(DeltaIndex))
		{
			bIsDelta[DeltaIndex] = true;
		}
	}

	TArray<bool> bIgnored;
	bIgnored.Init(false, NumActions);
	for (int32 Index = 0; Index < NumActions; ++Index)
	{
		if (Options.bIgnoreFrameDeltaActions && bIsDelta[Index])
		{
			bIgnored[Index] = true;
			continue;
		}

		const FString ShortName = GetActionShortName(ActionPaths[Index]);
		for (const FString& Entry : Options.IgnoredActions)
		{
			// Accept either form so designers can type "IA_Look" instead of a full object path.
			if (!Entry.IsEmpty() && (Entry == ActionPaths[Index] || Entry == ShortName))
			{
				bIgnored[Index] = true;
				break;
			}
		}
	}

	// ---- 2. Walk the stream and collect press onsets -----------------------------------------
	const float Threshold = FMath::Max(KINDA_SMALL_NUMBER, Options.PressThreshold);

	// Reconstructing "was this action pressed on the previous sample" from the delta-compressed
	// stream is valid precisely because the recorder only writes a frame when the value changed:
	// between two samples the value is held, so no state is lost.
	TArray<bool> bWasActive;
	bWasActive.Init(false, NumActions);

	TArray<float> LastCueTime;
	LastCueTime.Init(-TNumericLimits<float>::Max(), NumActions);

	for (const FRecordedInputFrame& Frame : Recording.Frames)
	{
		if (!bWasActive.IsValidIndex(Frame.ActionIndex) || bIgnored[Frame.ActionIndex])
		{
			continue;
		}

		const FVector Value(Frame.Value);
		const bool bActive = Value.Size() >= Threshold;
		const bool bOnset = bActive && !bWasActive[Frame.ActionIndex];

		bWasActive[Frame.ActionIndex] = bActive;

		if (!bOnset)
		{
			continue;
		}

		const float Time = GetFrameTimeSeconds(Recording, Frame);
		if (Time - LastCueTime[Frame.ActionIndex] < Options.MinimumCueSpacing)
		{
			continue;
		}
		LastCueTime[Frame.ActionIndex] = Time;

		FMatchInputCue& Cue = OutCues.AddDefaulted_GetRef();
		Cue.ActionIndex  = Frame.ActionIndex;
		Cue.FrameIndex   = Frame.FrameIndex;
		Cue.TimeSeconds  = Time;
		Cue.ActionName   = GetActionShortName(ActionPaths[Frame.ActionIndex]);
		Cue.Action       = TSoftObjectPtr<UInputAction>(FSoftObjectPath(ActionPaths[Frame.ActionIndex]));
		Cue.ExpectedValue = Value;
		Cue.ValueType    = Frame.ValueType;
		Cue.Description  = DescribeInputValue(Cue.ActionName, Cue.ValueType, Value);
	}

	// ---- 3. Order them and fill in the per-cue interval ---------------------------------------
	// Frames are already sorted by FrameIndex, but two actions can share a tick, and RecordedDeltas
	// timestamps are reconstructed - a stable sort keeps same-tick cues in registry order.
	OutCues.StableSort([](const FMatchInputCue& A, const FMatchInputCue& B)
	{
		return A.TimeSeconds < B.TimeSeconds;
	});

	float PreviousTime = 0.0f;
	for (FMatchInputCue& Cue : OutCues)
	{
		Cue.IntervalFromPreviousSeconds = FMath::Max(0.0f, Cue.TimeSeconds - PreviousTime);
		PreviousTime = Cue.TimeSeconds;
	}

	return OutCues.Num();
}

TArray<FMatchInputCue> UInputMatchLibrary::BuildMatchInputCuesFromRecording(const FInputRecording& Recording,
																		   const FMatchInputCueBuildOptions& Options)
{
	TArray<FMatchInputCue> Cues;
	BuildMatchInputCues(Recording, Options, Cues);
	return Cues;
}
