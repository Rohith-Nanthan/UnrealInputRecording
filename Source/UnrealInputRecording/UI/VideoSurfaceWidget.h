// Copyright (c) Your Studio. All Rights Reserved.
//
// VideoSurfaceWidget.h
//
// The video, and only the video. A single UImage whose brush is the subsystem's UMediaTexture, wrapped
// so nothing else has to know whether the picture is coming through a plain texture or a UI material.
//
// This is the "completely separate video player widget" from the design: it owns no timeline, no
// transport, no match logic. UMatchVideoPlayerWidget embeds one; so could a menu, a replay theatre, or
// a picture-in-picture inset. It rebinds itself whenever the subsystem opens a new take.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"

#include "VideoSurfaceWidget.generated.h"

class UImage;
class UInputRecordingSubsystem;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMediaTexture;

UCLASS(Blueprintable, meta = (DisplayName = "Video Surface Widget"))
class UNREALINPUTRECORDING_API UVideoSurfaceWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** (Re)point the surface at the subsystem's current media texture. Idempotent. */
	UFUNCTION(BlueprintCallable, Category = "Video Surface")
	void RefreshBinding();

	/** Point the surface at a specific texture, bypassing the subsystem. */
	UFUNCTION(BlueprintCallable, Category = "Video Surface")
	void SetMediaTexture(UMediaTexture* Texture);

	//~ Style hooks --------------------------------------------------------------------------------

	/**
	 * Route the texture through a UI material instead of using it as the brush directly. The material
	 * is where letterboxing, colour grading and rounded corners live. Off = the texture is the brush.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video Surface|Style")
	bool bUseMaterial = false;

	/** Material Domain = User Interface, sampling a Texture Sample Parameter 2D named below. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video Surface|Style", meta = (EditCondition = "bUseMaterial"))
	TObjectPtr<UMaterialInterface> VideoMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video Surface|Style", meta = (EditCondition = "bUseMaterial"))
	FName MaterialTextureParameter = TEXT("MediaTexture");

	/** Multiplied over the picture. Leave white; handy for fades. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video Surface|Style")
	FLinearColor Tint = FLinearColor::White;

	/** The image widget, exposed so a Blueprint subclass can reach it after construction. */
	UPROPERTY(BlueprintReadOnly, Category = "Video Surface")
	TObjectPtr<UImage> VideoImage;

protected:
	//~ Begin UUserWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	//~ End UUserWidget interface

	void BuildTree();

	UInputRecordingSubsystem* GetRecordingSubsystem() const;

private:
	UFUNCTION() void HandleVideoOpened(bool bSuccess, const FString& VideoPath);

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MaterialInstance;
};
