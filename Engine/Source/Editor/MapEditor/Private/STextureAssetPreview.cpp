// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "STextureAssetPreview.h"
#include "Rendering/DrawElements.h"
#include <cmath>
bool STextureAssetPreview::Update(const FTextureAsset& Texture, ETexturePreviewChannel Channel, FString& Error)
{
	FTextureAsset Pixels;
	if (!BuildTextureAssetPreview(Texture, Channel, Pixels, Error))
		return false;
	if (!Image || Image->GetWidth() != Pixels.Width || Image->GetHeight() != Pixels.Height)
		Image = MakeShared<FSlateTextureData>(Pixels.Width, Pixels.Height, std::move(Pixels.Pixels));
	else
		Image->UpdatePixels(std::move(Pixels.Pixels));
	Invalidate(EInvalidateWidgetReason::Paint);
	return true;
}
int32 STextureAssetPreview::OnPaint(const FPaintArgs&, const FGeometry& G, const FRect&, FSlateWindowElementList& Out,
									int32 Layer, const FWidgetStyle&) const
{
	Out.AddSolidBox(Layer, G.ToPaintGeometry(), FColor(24, 26, 30));
	if (Image)
	{
		const FVector2 Size(static_cast<float>(Image->GetWidth()), static_cast<float>(Image->GetHeight()));
		const auto Area = G.GetLocalSize();
		const float Scale = FMath::Min(Area.X / Size.X, Area.Y / Size.Y) * Zoom;
		const auto Draw = Size * Scale;
		Out.AddBox(Layer + 1, G.ToPaintGeometry((Area - Draw) * 0.5f + Pan, Draw), FBrush::FromImage(Image));
	}
	return Layer + 1;
}
FReply STextureAssetPreview::OnMouseWheel(const FGeometry&, const FPointerEvent& E)
{
	Zoom = FMath::Clamp(Zoom * std::pow(1.2f, E.GetWheelDelta()), 0.125f, 16.0f);
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled();
}
FReply STextureAssetPreview::OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E)
{
	if (E.GetEffectingButton() != EMouseButton::Middle)
		return FReply::Unhandled();
	bPanning = true;
	Last = G.AbsoluteToLocal(E.GetAbsolutePosition());
	return FReply::Handled().CaptureMouse(SharedThis(this));
}
FReply STextureAssetPreview::OnMouseMove(const FGeometry& G, const FPointerEvent& E)
{
	if (!bPanning)
		return FReply::Unhandled();
	const auto Point = G.AbsoluteToLocal(E.GetAbsolutePosition());
	Pan += Point - Last;
	Last = Point;
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled();
}
FReply STextureAssetPreview::OnMouseButtonUp(const FGeometry&, const FPointerEvent& E)
{
	if (!bPanning || E.GetEffectingButton() != EMouseButton::Middle)
		return FReply::Unhandled();
	bPanning = false;
	return FReply::Handled().ReleaseMouseCapture();
}
