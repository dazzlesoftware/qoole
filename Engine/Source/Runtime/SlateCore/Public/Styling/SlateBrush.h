// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Style brushes (docs/GenesisUI.md §8). FSlateBrush describes how a
// rectangle is filled: nothing, a solid color, a rounded box with an
// outline, or an image (stretched or 9-sliced).

#pragma once

#include "CoreMinimal.h"
#include "Rendering/DrawElements.h"
#include "Styling/SlateColor.h"

enum class ESlateBrushDrawType : uint8
{
	None,
	Solid,      // filled rectangle
	RoundedBox, // rounded rectangle, optional outline (SDF shader)
	Image,      // stretched image
	NineSlice,  // corners fixed, edges and center stretched
};

struct FSlateBrush
{
	ESlateBrushDrawType Type = ESlateBrushDrawType::None;
	FSlateColor Fill = FSlateColor(FColor::White());
	FSlateColor Outline = FSlateColor(FColor::Transparent());
	float OutlineWidth = 0.0f;              // logical units
	TStaticArray<float, 4> CornerRadii{};   // top-left, top-right, bottom-right, bottom-left
	TSharedPtr<FSlateTextureData> Image;
	FMargin Margin;                         // 9-slice insets (image pixels)
	FVector2 ImageSize;                     // logical size; zero = image pixels

	static FSlateBrush None() { return FSlateBrush(); }
	static FSlateBrush Solid(FSlateColor InFill)
	{
		FSlateBrush B;
		B.Type = ESlateBrushDrawType::Solid;
		B.Fill = InFill;
		return B;
	}
	static FSlateBrush Rounded(FSlateColor InFill, float Radius, FSlateColor InOutline = FSlateColor(FColor::Transparent()), float InOutlineWidth = 0.0f)
	{
		FSlateBrush B;
		B.Type = ESlateBrushDrawType::RoundedBox;
		B.Fill = InFill;
		B.Outline = InOutline;
		B.OutlineWidth = InOutlineWidth;
		B.CornerRadii = { Radius, Radius, Radius, Radius };
		return B;
	}
	static FSlateBrush FromImage(TSharedPtr<FSlateTextureData> InImage, FVector2 InSize = FVector2::Zero(), FSlateColor Tint = FSlateColor(FColor::White()))
	{
		FSlateBrush B;
		B.Type = ESlateBrushDrawType::Image;
		B.Image = std::move(InImage);
		B.ImageSize = InSize;
		B.Fill = Tint;
		return B;
	}

	bool IsVisible() const { return Type != ESlateBrushDrawType::None; }
	// Natural size for images (logical units); zero for color brushes.
	FVector2 GetImageSize() const;

	// Draws the brush over Geometry; Tint multiplies the fill and outline.
	void Paint(FSlateWindowElementList& Out, int32 Layer, const FPaintGeometry& Geometry, FColor Tint = FColor::White()) const;
};
