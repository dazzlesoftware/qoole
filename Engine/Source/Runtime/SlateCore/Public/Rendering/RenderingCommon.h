// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Images and brushes as SlateCore sees them: CPU-side data only. The renderer
// uploads an image the first time it is drawn (small images into shared atlas
// pages, large ones into their own texture). Widgets never touch the GPU
// (Engine/Documentation/Source/GenesisUI.md §3).

#pragma once

#include "CoreMinimal.h"

// RGBA8 (sRGB-encoded, straight alpha) pixels with a process-unique id.
class FSlateTextureData
{
public:
	FSlateTextureData(int32 InWidth, int32 InHeight, TArray<uint8> InPixels, FName InName = NAME_None);

	static TSharedRef<FSlateTextureData> CreateSolid(int32 Width, int32 Height, FColor Color, FName Name = NAME_None);

	uint64 GetId() const { return Id; }
	FName GetName() const { return Name; }
	int32 GetWidth() const { return Width; }
	int32 GetHeight() const { return Height; }
	FIntPoint GetSize() const { return { Width, Height }; }
	const TArray<uint8>& GetPixels() const { return Pixels; }

	// Bumped by UpdatePixels so the renderer re-uploads.
	uint32 GetRevision() const { return Revision; }
	void UpdatePixels(TArray<uint8> NewPixels);

private:
	uint64 Id;
	FName Name;
	int32 Width;
	int32 Height;
	TArray<uint8> Pixels;
	uint32 Revision = 0;
};

enum class EBrushDrawType : uint8
{
	NoDrawType,
	Image,  // stretched (or tiled) image
	Box,    // 9-slice: corners fixed, edges stretched, center stretched
	Border, // 9-slice without the center
};

enum class EBrushTiling : uint8
{
	NoTile,
	Horizontal,
	Vertical,
	Both,
};

// How to draw a rectangle with an image. A brush without an image draws a
// solid color.
struct FBrush
{
	TSharedPtr<FSlateTextureData> Image;
	EBrushDrawType DrawType = EBrushDrawType::Image;
	EBrushTiling Tiling = EBrushTiling::NoTile;
	// 9-slice insets in image pixels; drawn at that many logical units.
	FMargin Margin;
	FColor Tint = FColor::White();
	// Sub-rectangle of the image to use, in normalized UVs.
	FRect UVRegion = FRect(0.0f, 0.0f, 1.0f, 1.0f);

	static FBrush Solid(FColor Color)
	{
		FBrush Brush;
		Brush.Tint = Color;
		return Brush;
	}

	static FBrush FromImage(TSharedPtr<FSlateTextureData> InImage, FColor InTint = FColor::White())
	{
		FBrush Brush;
		Brush.Image = std::move(InImage);
		Brush.Tint = InTint;
		return Brush;
	}

	static FBrush NineSlice(TSharedPtr<FSlateTextureData> InImage, const FMargin& InMargin, FColor InTint = FColor::White(), bool bDrawCenter = true)
	{
		FBrush Brush;
		Brush.Image = std::move(InImage);
		Brush.DrawType = bDrawCenter ? EBrushDrawType::Box : EBrushDrawType::Border;
		Brush.Margin = InMargin;
		Brush.Tint = InTint;
		return Brush;
	}
};

// A texture rendered elsewhere (3D views, previews, video) that the UI
// composites with a Viewport element. Implemented by the renderer side
// (SlateRHIRenderer's FSlateRenderTargetRHI); SlateCore only passes it through.
class ISlateRenderTarget
{
public:
	virtual ~ISlateRenderTarget() = default;
	virtual FIntPoint GetSize() const = 0;
};
