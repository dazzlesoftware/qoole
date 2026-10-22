// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "TextureAssetEditor.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/RenderingCommon.h"
class STextureAssetPreview final : public SLeafWidget
{
  public:
	SLATE_BEGIN_ARGS(STextureAssetPreview) SLATE_END_ARGS() void Construct(const FArguments&)
	{
		SetClipping(EWidgetClipping::ClipToBounds);
	}
	bool Update(const FTextureAsset&, ETexturePreviewChannel, FString& Error);
	void Fit()
	{
		Zoom = 1;
		Pan = FVector2();
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	FReply OnMouseWheel(const FGeometry&, const FPointerEvent&) override;
	FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent&) override;
	FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override;
	FReply OnMouseMove(const FGeometry&, const FPointerEvent&) override;
	void OnMouseCaptureLost() override
	{
		bPanning = false;
	}

  protected:
	FVector2 ComputeDesiredSize(float) const override
	{
		return FVector2(512, 512);
	}
	int32 OnPaint(const FPaintArgs&, const FGeometry&, const FRect&, FSlateWindowElementList&, int32,
				  const FWidgetStyle&) const override;

  private:
	TSharedPtr<FSlateTextureData> Image;
	float Zoom = 1;
	FVector2 Pan, Last;
	bool bPanning = false;
};
