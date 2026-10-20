// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Style colors (Engine/Documentation/Source/GenesisUI.md §8).
//
// Colors go through a semantic palette: a style says "Panel" or "Foreground"
// and the active FStyleTheme decides the actual color, so a theme swaps the
// palette, not the widgets.

#pragma once

#include "CoreMinimal.h"

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

class FStyleTheme
{
public:
	FStyleTheme();
	FColor Get(EStyleColor Key) const { return Colors[static_cast<int32>(Key)]; }
	void Set(EStyleColor Key, FColor Color) { Colors[static_cast<int32>(Key)] = Color; }

	// The dark theme the editor uses by default.
	static FStyleTheme MakeDark();

	// The palette FSlateColor resolves against.
	static const FStyleTheme& GetActive();
	static void SetActive(const FStyleTheme& Palette);

private:
	TStaticArray<FColor, static_cast<SIZE_T>(EStyleColor::Count)> Colors{};
};

// A palette entry or a fixed color, with an optional opacity.
struct FSlateColor
{
	TOptional<EStyleColor> Key;
	FColor Color = FColor::White();
	uint8 Alpha = 255; // multiplies the palette color's alpha

	FSlateColor() = default;
	FSlateColor(EStyleColor InKey, uint8 InAlpha = 255) : Key(InKey), Alpha(InAlpha) {}
	FSlateColor(FColor InColor) : Color(InColor) {}

	FColor Resolve() const
	{
		if (!Key)
		{
			return Color;
		}
		FColor C = FStyleTheme::GetActive().Get(*Key);
		C.A = uint8((C.A * Alpha + 127) / 255);
		return C;
	}

	bool operator==(const FSlateColor&) const = default;
};
