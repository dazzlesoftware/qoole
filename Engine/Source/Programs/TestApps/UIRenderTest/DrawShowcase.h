// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// The draw-element showcase of UIRenderTest (milestone 2 of
// Engine/Documentation/Source/GenesisUI.md): boxes, 9-slices, rounded boxes, lines, clipping,
// shaped text and a render-target viewport, built directly from draw
// elements, without widgets.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/DrawElements.h"
#include "SlateRHIRenderingPolicy.h"

struct FShowcaseFonts
{
	TSharedPtr<FCompositeFont> Regular; // Noto Sans + Noto Sans Devanagari fallback
	TSharedPtr<FCompositeFont> Bold;
};

struct FShowcaseFrame
{
	double Time = 0.0;
	float Scale = 1.0f;         // pixels per logical unit (DPI x app scale)
	float DpiScale = 1.0f;
	float AppScale = 1.0f;
	FVector2 Origin;            // top-left of the drawing area in the window, logical units
	FVector2 LogicalSize;       // size of the drawing area, logical units
	FIntPoint PixelSize;
	FVector2 MouseLogical{ -1.0f, -1.0f }; // relative to Origin
	FString BackendName;
	FString AdapterName;
	float FramesPerSecond = 0.0f;
	FSlateRenderStats Stats;
	float ListScroll = 0.0f;    // mouse-wheel offset of the clipped list
};

class FDrawShowcase
{
public:
	FDrawShowcase(FSlateFontCache& InFontCache, FShowcaseFonts InFonts);

	// The showcase, filling Frame.LogicalSize at Frame.Origin.
	void PaintMain(FSlateWindowElementList& List, const FShowcaseFrame& Frame, const TSharedPtr<FSlateRenderTargetRHI>& ViewportTarget);
	// The scene shown through the Viewport element, drawn into its render target.
	void PaintViewportScene(FSlateWindowElementList& List, const FShowcaseFrame& Frame, FIntPoint TargetSize);

private:
	struct FPanel
	{
		FRect Rect; // logical
		FString Title;
	};

	FPaintGeometry Geo(const FShowcaseFrame& Frame, FRect LogicalRect) const;
	// Draws text with its line box at Position; returns its logical size.
	FVector2 Text(FSlateWindowElementList& List, int32 Layer, const FShowcaseFrame& Frame, FVector2 Position, std::string_view String,
		const TSharedPtr<FCompositeFont>& Font, float Size, FColor Color);
	FRect Panel(FSlateWindowElementList& List, const FShowcaseFrame& Frame, const FRect& Rect, std::string_view Title);

	void PaintBoxes(FSlateWindowElementList& List, const FShowcaseFrame& Frame, FRect Area);
	void PaintRoundedBoxes(FSlateWindowElementList& List, const FShowcaseFrame& Frame, FRect Area);
	void PaintLines(FSlateWindowElementList& List, const FShowcaseFrame& Frame, FRect Area);
	void PaintClipping(FSlateWindowElementList& List, const FShowcaseFrame& Frame, FRect Area);
	void PaintText(FSlateWindowElementList& List, const FShowcaseFrame& Frame, FRect Area);
	void PaintViewport(FSlateWindowElementList& List, const FShowcaseFrame& Frame, FRect Area, const TSharedPtr<FSlateRenderTargetRHI>& ViewportTarget);

	FSlateFontCache& FontCache;
	FShowcaseFonts Fonts;
	TSharedPtr<FSlateTextureData> FrameImage;   // 9-slice source: bevelled frame
	TSharedPtr<FSlateTextureData> CheckerImage; // tiled pattern
	TSharedPtr<FSlateTextureData> GradientImage;
	TSharedPtr<FSlateTextureData> IconImage;    // small atlas-packed icon
	TSharedPtr<FSlateTextureData> HairlineImage;
};
