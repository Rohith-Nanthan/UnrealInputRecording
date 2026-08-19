// Copyright (c) Your Studio. All Rights Reserved.

#include "UI/VideoSurfaceWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Engine/GameInstance.h"
#include "InputRecordingSubsystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MediaTexture.h"
#include "Video/InputRecordingVideoPlayer.h"

UInputRecordingSubsystem* UVideoSurfaceWidget::GetRecordingSubsystem() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UInputRecordingSubsystem>() : nullptr;
}

TSharedRef<SWidget> UVideoSurfaceWidget::RebuildWidget()
{
	if (!VideoImage)
	{
		BuildTree();
	}
	return Super::RebuildWidget();
}

void UVideoSurfaceWidget::BuildTree()
{
	VideoImage = WidgetTree->ConstructWidget<UImage>();
	VideoImage->SetColorAndOpacity(Tint);
	WidgetTree->RootWidget = VideoImage;
}

void UVideoSurfaceWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Rebind when a new take opens: the media texture object is stable across opens, but binding on the
	// event covers the case where the surface is constructed before the player exists.
	if (UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem())
	{
		if (UInputRecordingVideoPlayer* VideoPlayer = Subsystem->GetVideoPlayer())
		{
			VideoPlayer->OnVideoOpened.AddUniqueDynamic(this, &UVideoSurfaceWidget::HandleVideoOpened);
		}
	}

	RefreshBinding();
}

void UVideoSurfaceWidget::RefreshBinding()
{
	UInputRecordingSubsystem* Subsystem = GetRecordingSubsystem();
	UInputRecordingVideoPlayer* VideoPlayer = Subsystem ? Subsystem->GetVideoPlayer() : nullptr;
	SetMediaTexture(VideoPlayer ? VideoPlayer->GetMediaTexture() : nullptr);
}

void UVideoSurfaceWidget::SetMediaTexture(UMediaTexture* Texture)
{
	if (!VideoImage)
	{
		// Force the tree so the brush can be assigned before first paint.
		TakeWidget();
	}

	if (!VideoImage || !Texture)
	{
		return;
	}

	VideoImage->SetColorAndOpacity(Tint);

	if (bUseMaterial && VideoMaterial)
	{
		if (!MaterialInstance)
		{
			MaterialInstance = UMaterialInstanceDynamic::Create(VideoMaterial, this);
		}

		if (MaterialInstance)
		{
			MaterialInstance->SetTextureParameterValue(MaterialTextureParameter, Texture);
			VideoImage->SetBrushFromMaterial(MaterialInstance);
		}
		return;
	}

	// SetBrushResourceObject, not SetBrushFromTexture: a UMediaTexture is a UTexture but not a
	// UTexture2D, so the typed setter would refuse it.
	VideoImage->SetBrushResourceObject(Texture);
}

void UVideoSurfaceWidget::HandleVideoOpened(bool bSuccess, const FString& VideoPath)
{
	if (bSuccess)
	{
		RefreshBinding();
	}
}
