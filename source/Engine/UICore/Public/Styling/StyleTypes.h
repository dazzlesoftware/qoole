// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Style building blocks (docs/GenesisUI.md §8).
//
// Colors go through a semantic palette: a style says "Panel" or "Foreground"
// and the active FStylePalette decides the actual color, so a theme swaps the
// palette, not the widgets. FStyleBrush describes how a rectangle is filled:
// nothing, a solid color, a rounded box with an outline, or an image
// (stretched or 9-sliced).

#pragma once

#include "CoreMinimal.h"
#include "Rendering/DrawElements.h"

enum class EStyleColor : uint8
{
	Background,          // window background
	Recessed,            // wells, list backgrounds
	Panel,               // panels, cards
	Header,              // headers, title bars
	Input,               // text box fill
	Dropdown,            // menus, popups, tooltips
	Hover,               // hovered controls
	Pressed,             // pressed controls
	Border,              // outlines, separators
	Foreground,          // text and icons
	ForegroundHover,
	ForegroundSecondary, // hints, captions
	ForegroundDisabled,
	ForegroundInverted,  // text on Primary
	Primary,             // accent: focus rings, checked boxes, progress
	PrimaryHover,
	PrimaryPressed,
	Select,              // selected rows, text selection (focused)
	SelectInactive,      // selection without focus
	SelectHover,
	Error,
	Warning,
	Success,
	White,
	Black,
	Transparent,
	Count,
};

const char* ToString(EStyleColor Color);

class FStylePalette
{
public:
	FStylePalette();
	FColor Get(EStyleColor Key) const { return Colors[static_cast<int32>(Key)]; }
	void Set(EStyleColor Key, FColor Color) { Colors[static_cast<int32>(Key)] = Color; }

	// The dark theme the editor uses by default.
	static FStylePalette MakeDark();

	// The palette FStyleColor resolves against.
	static const FStylePalette& GetActive();
	static void SetActive(const FStylePalette& Palette);

private:
	TStaticArray<FColor, static_cast<SIZE_T>(EStyleColor::Count)> Colors{};
};

// A palette entry or a fixed color, with an optional opacity.
struct FStyleColor
{
	TOptional<EStyleColor> Key;
	FColor Color = FColor::White();
	uint8 Alpha = 255; // multiplies the palette color's alpha

	FStyleColor() = default;
	FStyleColor(EStyleColor InKey, uint8 InAlpha = 255) : Key(InKey), Alpha(InAlpha) {}
	FStyleColor(FColor InColor) : Color(InColor) {}

	FColor Resolve() const
	{
		if (!Key)
		{
			return Color;
		}
		FColor C = FStylePalette::GetActive().Get(*Key);
		C.A = uint8((C.A * Alpha + 127) / 255);
		return C;
	}

	bool operator==(const FStyleColor&) const = default;
};

enum class EStyleBrushType : uint8
{
	None,
	Solid,      // filled rectangle
	RoundedBox, // rounded rectangle, optional outline (SDF shader)
	Image,      // stretched image
	NineSlice,  // corners fixed, edges and center stretched
};

struct FStyleBrush
{
	EStyleBrushType Type = EStyleBrushType::None;
	FStyleColor Fill = FStyleColor(FColor::White());
	FStyleColor Outline = FStyleColor(FColor::Transparent());
	float OutlineWidth = 0.0f;              // logical units
	TStaticArray<float, 4> CornerRadii{};   // top-left, top-right, bottom-right, bottom-left
	TSharedPtr<FUIImage> Image;
	FMargin Margin;                         // 9-slice insets (image pixels)
	FVector2 ImageSize;                     // logical size; zero = image pixels

	static FStyleBrush None() { return FStyleBrush(); }
	static FStyleBrush Solid(FStyleColor InFill)
	{
		FStyleBrush B;
		B.Type = EStyleBrushType::Solid;
		B.Fill = InFill;
		return B;
	}
	static FStyleBrush Rounded(FStyleColor InFill, float Radius, FStyleColor InOutline = FStyleColor(FColor::Transparent()), float InOutlineWidth = 0.0f)
	{
		FStyleBrush B;
		B.Type = EStyleBrushType::RoundedBox;
		B.Fill = InFill;
		B.Outline = InOutline;
		B.OutlineWidth = InOutlineWidth;
		B.CornerRadii = { Radius, Radius, Radius, Radius };
		return B;
	}
	static FStyleBrush FromImage(TSharedPtr<FUIImage> InImage, FVector2 InSize = FVector2::Zero(), FStyleColor Tint = FStyleColor(FColor::White()))
	{
		FStyleBrush B;
		B.Type = EStyleBrushType::Image;
		B.Image = std::move(InImage);
		B.ImageSize = InSize;
		B.Fill = Tint;
		return B;
	}

	bool IsVisible() const { return Type != EStyleBrushType::None; }
	// Natural size for images (logical units); zero for color brushes.
	FVector2 GetImageSize() const;

	// Draws the brush over Geometry; Tint multiplies the fill and outline.
	void Paint(FDrawElementList& Out, int32 Layer, const FPaintGeometry& Geometry, FColor Tint = FColor::White()) const;
};
