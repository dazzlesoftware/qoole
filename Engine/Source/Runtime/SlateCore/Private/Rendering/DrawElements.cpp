// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Rendering/DrawElements.h"

const char* ToString(EDrawElementType Type)
{
	switch (Type)
	{
	case EDrawElementType::Box: return "Box";
	case EDrawElementType::RoundedBox: return "RoundedBox";
	case EDrawElementType::Text: return "Text";
	case EDrawElementType::Lines: return "Lines";
	case EDrawElementType::CustomVerts: return "CustomVerts";
	case EDrawElementType::Viewport: return "Viewport";
	}
	return "?";
}

FRect FPaintGeometry::GetPixelBounds() const
{
	const FVector2 Corners[4] = {
		Transform.TransformPoint({ 0.0f, 0.0f }),
		Transform.TransformPoint({ LocalSize.X, 0.0f }),
		Transform.TransformPoint({ 0.0f, LocalSize.Y }),
		Transform.TransformPoint(LocalSize),
	};
	FVector2 Min = Corners[0], Max = Corners[0];
	for (const FVector2& C : Corners)
	{
		Min = FVector2::Min(Min, C);
		Max = FVector2::Max(Max, C);
	}
	return FRect(Min, Max);
}

// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------

void FDrawElementList::Reset()
{
	Elements.clear();
	ClipRects.clear();
	ClipStack.clear();
	CulledCount = 0;
}

bool FDrawElementList::IsCulled(const FRect& PixelBounds)
{
	if (PixelBounds.Right < PixelBounds.Left || PixelBounds.Bottom < PixelBounds.Top)
	{
		++CulledCount;
		return true;
	}
	if (const TOptional<FIntRect> Clip = GetCurrentClip())
	{
		const FRect ClipRect(float(Clip->Left), float(Clip->Top), float(Clip->Right), float(Clip->Bottom));
		if (Clip->IsEmpty() || !ClipRect.Intersects(PixelBounds))
		{
			++CulledCount;
			return true;
		}
	}
	return false;
}

FDrawElement& FDrawElementList::Add(EDrawElementType Type, int32 Layer, const FPaintGeometry& Geometry, FColor Color)
{
	FDrawElement& Element = Elements.emplace_back();
	Element.Type = Type;
	Element.Layer = Layer;
	Element.ClipIndex = ClipStack.empty() ? INDEX_NONE : ClipStack.back();
	Element.Geometry = Geometry;
	Element.Color = Color;
	return Element;
}

void FDrawElementList::AddBox(int32 Layer, const FPaintGeometry& Geometry, const FBrush& Brush)
{
	if (Brush.DrawType == EBrushDrawType::NoDrawType || Brush.Tint.A == 0 || IsCulled(Geometry.GetPixelBounds()))
	{
		return;
	}
	Add(EDrawElementType::Box, Layer, Geometry, Brush.Tint).Data = FBoxElement{ Brush };
}

void FDrawElementList::AddRoundedBox(int32 Layer, const FPaintGeometry& Geometry, FColor Fill, float CornerRadius, FColor OutlineColor, float OutlineWidth)
{
	AddRoundedBox(Layer, Geometry, Fill, { CornerRadius, CornerRadius, CornerRadius, CornerRadius }, OutlineColor, OutlineWidth);
}

void FDrawElementList::AddRoundedBox(int32 Layer, const FPaintGeometry& Geometry, FColor Fill, const TStaticArray<float, 4>& CornerRadii, FColor OutlineColor, float OutlineWidth)
{
	if ((Fill.A == 0 && (OutlineColor.A == 0 || OutlineWidth <= 0.0f)) || IsCulled(Geometry.GetPixelBounds()))
	{
		return;
	}
	FRoundedBoxElement Data;
	Data.OutlineColor = OutlineColor;
	Data.OutlineWidth = OutlineWidth;
	Data.CornerRadii = CornerRadii;
	Add(EDrawElementType::RoundedBox, Layer, Geometry, Fill).Data = Data;
}

void FDrawElementList::AddText(int32 Layer, const FPaintGeometry& Geometry, const TSharedRef<const FShapedText>& Text, FColor Color)
{
	const FPaintGeometry Bounds(Geometry.Transform, Text->GetLogicalSize());
	if (Text->Glyphs.empty() || Color.A == 0 || IsCulled(Bounds.GetPixelBounds()))
	{
		return;
	}
	ensuref(FMath::IsNearlyEqual(Geometry.GetScale(), Text->Scale, 0.01f),
		"Text shaped at scale {} drawn at scale {}; it will not be pixel-exact", Text->Scale, Geometry.GetScale());
	Add(EDrawElementType::Text, Layer, Geometry, Color).Data = FTextElement{ Text.ToSharedPtr() };
}

void FDrawElementList::AddLines(int32 Layer, const FPaintGeometry& Geometry, TArray<FVector2> Points, FColor Color, float Thickness, bool bClosed)
{
	if (Points.size() < 2 || Color.A == 0)
	{
		return;
	}
	FVector2 Min = Points[0], Max = Points[0];
	for (const FVector2& P : Points)
	{
		Min = FVector2::Min(Min, P);
		Max = FVector2::Max(Max, P);
	}
	const float Pad = Thickness * 0.5f + 1.0f;
	const FPaintGeometry Bounds(FTransform2D::FromTranslation(Min - FVector2(Pad)).Concatenate(Geometry.Transform), Max - Min + FVector2(Pad * 2.0f));
	if (IsCulled(Bounds.GetPixelBounds()))
	{
		return;
	}
	FLinesElement Data;
	Data.Points = std::move(Points);
	Data.Thickness = Thickness;
	Data.bClosed = bClosed;
	Add(EDrawElementType::Lines, Layer, Geometry, Color).Data = std::move(Data);
}

void FDrawElementList::AddCustomVerts(int32 Layer, const FPaintGeometry& Geometry, TSharedPtr<FUIImage> Image, TArray<FUIVertex2D> Vertices, TArray<uint32> Indices)
{
	if (Vertices.empty() || Indices.size() < 3)
	{
		return;
	}
	check(Indices.size() % 3 == 0);
	FCustomVertsElement Data;
	Data.Image = std::move(Image);
	Data.Vertices = std::move(Vertices);
	Data.Indices = std::move(Indices);
	Add(EDrawElementType::CustomVerts, Layer, Geometry, FColor::White()).Data = std::move(Data);
}

void FDrawElementList::AddViewport(int32 Layer, const FPaintGeometry& Geometry, TSharedPtr<IUIRenderTarget> RenderTarget, bool bAlphaBlend, FColor Tint)
{
	if (!RenderTarget || IsCulled(Geometry.GetPixelBounds()))
	{
		return;
	}
	Add(EDrawElementType::Viewport, Layer, Geometry, Tint).Data = FViewportElement{ std::move(RenderTarget), bAlphaBlend };
}

TOptional<FIntRect> FDrawElementList::GetCurrentClip() const
{
	if (ClipStack.empty())
	{
		return {};
	}
	return ClipRects[ClipStack.back()];
}

void FDrawElementList::PushClip(const FPaintGeometry& Geometry)
{
	ensuref(Geometry.Transform.IsAxisAligned(), "Clipping supports axis-aligned rectangles only");
	const FRect Bounds = Geometry.GetPixelBounds();
	// Round to the nearest pixel edge, matching how boxes are snapped.
	PushClipPixels(FIntRect(FMath::RoundToInt(Bounds.Left), FMath::RoundToInt(Bounds.Top),
		FMath::RoundToInt(Bounds.Right), FMath::RoundToInt(Bounds.Bottom)));
}

void FDrawElementList::PushClipPixels(const FIntRect& PixelRect)
{
	FIntRect Rect = PixelRect;
	if (const TOptional<FIntRect> Current = GetCurrentClip())
	{
		Rect = FIntRect(FMath::Max(Rect.Left, Current->Left), FMath::Max(Rect.Top, Current->Top),
			FMath::Min(Rect.Right, Current->Right), FMath::Min(Rect.Bottom, Current->Bottom));
	}
	// Keep empty clips well-formed (zero size), so they cull everything.
	Rect.Right = FMath::Max(Rect.Right, Rect.Left);
	Rect.Bottom = FMath::Max(Rect.Bottom, Rect.Top);
	ClipRects.push_back(Rect);
	ClipStack.push_back(static_cast<int32>(ClipRects.size()) - 1);
}

void FDrawElementList::PopClip()
{
	if (ensuref(!ClipStack.empty(), "PopClip without PushClip"))
	{
		ClipStack.pop_back();
	}
}
