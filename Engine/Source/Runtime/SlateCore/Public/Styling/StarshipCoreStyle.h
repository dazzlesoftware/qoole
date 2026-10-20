// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// The default style set ("Genesis"): the dark palette, Noto Sans, and a style
// for every built-in widget (docs/GenesisUI.md §8).

#pragma once

#include "Styling/SlateStyle.h"

struct FStarshipCoreFonts
{
	TSharedPtr<FCompositeFont> Regular; // Noto Sans + Noto Sans Devanagari fallback
	TSharedPtr<FCompositeFont> Bold;

	// Loads the fonts from FontDirectory (default: <Content>/Slate/Fonts/).
	static FStarshipCoreFonts Load(FSlateFontCache& FontCache, FString FontDirectory = FString());
	bool IsValid() const { return Regular && Bold; }
};

class FStarshipCoreStyle
{
public:
	static constexpr const char* SetName = "Genesis";

	static TSharedRef<FSlateStyleSet> Create(const FStarshipCoreFonts& Fonts);
};
