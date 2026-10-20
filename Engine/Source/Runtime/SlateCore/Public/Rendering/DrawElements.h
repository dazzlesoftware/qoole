// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Draw elements (docs/GenesisUI.md §5.1): what widgets emit when they paint.
// The renderer turns an FSlateWindowElementList into GPU batches.
//
// Geometry is given in logical units plus a transform to window pixels
// (DPI scale and layout offset), so the renderer can snap to pixels and
// rasterize text at its final size. Every element has a layer id (draw
// order; ties keep submission order) and the clip state that was current
// when it was added. Clipping is axis-aligned scissor rectangles only.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/FontCache.h"
#include "Rendering/RenderingCommon.h"

#include <variant>

// Where to draw: a local rectangle (0,0)-(LocalSize) and its transform to
// window pixels.
struct FPaintGeometry
{
	FTransform2D Transform;
	FVector2 LocalSize;

	FPaintGeometry() = default;
	FPaintGeometry(const FTransform2D& InTransform, FVector2 InLocalSize) : Transform(InTransform), LocalSize(InLocalSize) {}

	// A rectangle at a logical position in a window with the given DPI scale.
	static FPaintGeometry Make(FVector2 LogicalPosition, FVector2 LogicalSize, float Scale)
	{
		return FPaintGeometry(FTransform2D(Scale, LogicalPosition * Scale), LogicalSize);
	}

	// A child rectangle at Offset (local units) inside this one.
	FPaintGeometry MakeChild(FVector2 Offset, FVector2 Size) const
	{
		return FPaintGeometry(FTransform2D::FromTranslation(Offset).Concatenate(Transform), Size);
	}

	// Applies an extra transform (e.g. a render transform) in local space.
	FPaintGeometry WithLocalTransform(const FTransform2D& Local) const
	{
		return FPaintGeometry(Local.Concatenate(Transform), LocalSize);
	}

	// Pixels per local unit (assumes uniform scale).
	float GetScale() const { return FMath::Sqrt(FMath::Abs(Transform.Determinant())); }

	// Axis-aligned bounds in window pixels.
	FRect GetPixelBounds() const;
};

enum class EElementType : uint8
{
	Box,
	RoundedBox,
	Text,
	Lines,
	CustomVerts,
	Viewport,
};

const char* ToString(EElementType Type);

struct FSlateBoxElement
{
	FBrush Brush;
};

struct FSlateRoundedBoxElement
{
	FColor OutlineColor = FColor::Transparent();
	float OutlineWidth = 0.0f;            // logical units, drawn inside the rectangle
	TStaticArray<float, 4> CornerRadii{}; // logical units: top-left, top-right, bottom-right, bottom-left
};

struct FSlateTextElement
{
	TSharedPtr<const FShapedGlyphSequence> Text;
};

struct FSlateLineElement
{
	TArray<FVector2> Points; // local units
	float Thickness = 1.0f;  // logical units
	bool bClosed = false;
};

struct FSlateVertex2D
{
	FVector2 Position; // local units
	FVector2 UV;
	FColor Color;
};

struct FSlateCustomVertsElement
{
	TSharedPtr<FSlateTextureData> Image; // nullptr: untextured
	TArray<FSlateVertex2D> Vertices;
	TArray<uint32> Indices;
};

struct FSlateViewportElement
{
	TSharedPtr<ISlateRenderTarget> RenderTarget;
	bool bAlphaBlend = false; // false: the target's alpha is ignored
};

struct FSlateDrawElement
{
	EElementType Type = EElementType::Box;
	int32 Layer = 0;
	int32 ClipIndex = INDEX_NONE; // into FSlateWindowElementList::GetClipRects; INDEX_NONE = unclipped
	FPaintGeometry Geometry;
	FColor Color = FColor::White(); // fill / tint / text color
	std::variant<FSlateBoxElement, FSlateRoundedBoxElement, FSlateTextElement, FSlateLineElement, FSlateCustomVertsElement, FSlateViewportElement> Data;

	template <typename T> const T& Get() const { return std::get<T>(Data); }
};

class FSlateWindowElementList
{
public:
	void Reset();

	// Size of the target in pixels; the unclipped region.
	void SetViewportSize(FIntPoint Size) { ViewportSize = Size; }
	FIntPoint GetViewportSize() const { return ViewportSize; }

	void AddBox(int32 Layer, const FPaintGeometry& Geometry, const FBrush& Brush);
	void AddSolidBox(int32 Layer, const FPaintGeometry& Geometry, FColor Color) { AddBox(Layer, Geometry, FBrush::Solid(Color)); }
	void AddRoundedBox(int32 Layer, const FPaintGeometry& Geometry, FColor Fill, float CornerRadius, FColor OutlineColor = FColor::Transparent(), float OutlineWidth = 0.0f);
	void AddRoundedBox(int32 Layer, const FPaintGeometry& Geometry, FColor Fill, const TStaticArray<float, 4>& CornerRadii, FColor OutlineColor, float OutlineWidth);
	// Geometry's top-left is the top-left of the line box (baseline at
	// Text->GetLogicalBaseline()). The text must have been shaped for this
	// geometry's scale.
	void AddText(int32 Layer, const FPaintGeometry& Geometry, const TSharedRef<const FShapedGlyphSequence>& Text, FColor Color);
	void AddLines(int32 Layer, const FPaintGeometry& Geometry, TArray<FVector2> Points, FColor Color, float Thickness = 1.0f, bool bClosed = false);
	void AddCustomVerts(int32 Layer, const FPaintGeometry& Geometry, TSharedPtr<FSlateTextureData> Image, TArray<FSlateVertex2D> Vertices, TArray<uint32> Indices);
	void AddViewport(int32 Layer, const FPaintGeometry& Geometry, TSharedPtr<ISlateRenderTarget> RenderTarget, bool bAlphaBlend = false, FColor Tint = FColor::White());

	// Clips everything added until the matching PopClip to the geometry's
	// rectangle, intersected with the current clip. The geometry must be
	// axis-aligned (rotated content that needs clipping renders to a texture).
	void PushClip(const FPaintGeometry& Geometry);
	void PushClipPixels(const FIntRect& PixelRect);
	void PopClip();
	int32 GetClipDepth() const { return static_cast<int32>(ClipStack.size()); }
	// The current scissor in pixels, or nothing when unclipped.
	TOptional<FIntRect> GetCurrentClip() const;

	const TArray<FSlateDrawElement>& GetElements() const { return Elements; }
	const TArray<FIntRect>& GetClipRects() const { return ClipRects; }
	int32 GetCulledCount() const { return CulledCount; }

private:
	FSlateDrawElement& Add(EElementType Type, int32 Layer, const FPaintGeometry& Geometry, FColor Color);
	// True when the element would be invisible under the current clip.
	bool IsCulled(const FRect& PixelBounds);

	TArray<FSlateDrawElement> Elements;
	TArray<FIntRect> ClipRects;   // every clip state ever pushed this frame
	TArray<int32> ClipStack;      // indices into ClipRects
	FIntPoint ViewportSize{ 0, 0 };
	int32 CulledCount = 0;
};
