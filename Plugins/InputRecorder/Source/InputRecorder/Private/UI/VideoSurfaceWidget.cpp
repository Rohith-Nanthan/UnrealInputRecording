// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/VideoSurfaceWidget.h"

#include "Components/Image.h"
#include "MediaTexture.h"
#include "Subsystem/InputRecordingSubsystem.h"
#include "Video/InputRecordingVideoPlayer.h"

void UVideoSurfaceWidget::CollectMissingBindings(TArray<FString>& OutMissing) const
{
	if (!VideoImage)
	{
		OutMissing.Add(TEXT("VideoImage"));
	}
}

void UVideoSurfaceWidget::SetMediaTexture(UMediaTexture* Texture)
{
	if (AppliedTexture == Texture)
	{
		return;
	}

	AppliedTexture = Texture;

	if (VideoImage)
	{
		// SetBrushResourceObject, never SetBrushFromTexture: UMediaTexture is a UTexture but not
		// a UTexture2D, and the typed setter rejects it.
		VideoImage->SetBrushResourceObject(Texture);
		VideoImage->SetVisibility(Texture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (NoVideoPanel)
	{
		NoVideoPanel->SetVisibility(Texture ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	K2_OnVideoAvailabilityChanged(Texture != nullptr);
}

void UVideoSurfaceWidget::RefreshFromSubsystem()
{
	const UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	const UInputRecordingVideoPlayer* Player = Subsystem ? Subsystem->GetVideoPlayer() : nullptr;

	SetMediaTexture(Player && Player->IsVideoOpen() ? Player->GetMediaTexture() : nullptr);
}
