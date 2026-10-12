// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// The milestone 2 showcase: boxes, 9-slices, rounded boxes, lines, clipping,
// shaped text and a render-target viewport, built directly from draw
// elements. The widget gallery shows it as its "Draw elements" page.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/DrawElements.h"
#include "UIRenderer.h"

struct FSandboxFonts
{
	TSharedPtr<FCompositeFont> Regular; // Noto Sans + Noto Sans Devanagari fallback
	TSharedPtr<FCompositeFont> Bold;
};

struct FSandboxFrameInfo
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
	FUIRenderStats Stats;
	float ListScroll = 0.0f;    // mouse-wheel offset of the clipped list
};

class FSandboxContent
{
public:
	FSandboxContent(FFontCache& InFontCache, FSandboxFonts InFonts);

	// The showcase, filling Frame.LogicalSize at Frame.Origin.
	void PaintMain(FDrawElementList& List, const FSandboxFrameInfo& Frame, const TSharedPtr<FUIRenderTarget>& ViewportTarget);
	// The scene shown through the Viewport element, drawn into its render target.
	void PaintViewportScene(FDrawElementList& List, const FSandboxFrameInfo& Frame, FIntPoint TargetSize);

private:
	struct FPanel
	{
		FRect Rect; // logical
		FString Title;
	};

	FPaintGeometry Geo(const FSandboxFrameInfo& Frame, FRect LogicalRect) const;
	// Draws text with its line box at Position; returns its logical size.
	FVector2 Text(FDrawElementList& List, int32 Layer, const FSandboxFrameInfo& Frame, FVector2 Position, std::string_view String,
		const TSharedPtr<FCompositeFont>& Font, float Size, FColor Color);
	FRect Panel(FDrawElementList& List, const FSandboxFrameInfo& Frame, const FRect& Rect, std::string_view Title);

	void PaintBoxes(FDrawElementList& List, const FSandboxFrameInfo& Frame, FRect Area);
	void PaintRoundedBoxes(FDrawElementList& List, const FSandboxFrameInfo& Frame, FRect Area);
	void PaintLines(FDrawElementList& List, const FSandboxFrameInfo& Frame, FRect Area);
	void PaintClipping(FDrawElementList& List, const FSandboxFrameInfo& Frame, FRect Area);
	void PaintText(FDrawElementList& List, const FSandboxFrameInfo& Frame, FRect Area);
	void PaintViewport(FDrawElementList& List, const FSandboxFrameInfo& Frame, FRect Area, const TSharedPtr<FUIRenderTarget>& ViewportTarget);

	FFontCache& FontCache;
	FSandboxFonts Fonts;
	TSharedPtr<FUIImage> FrameImage;   // 9-slice source: bevelled frame
	TSharedPtr<FUIImage> CheckerImage; // tiled pattern
	TSharedPtr<FUIImage> GradientImage;
	TSharedPtr<FUIImage> IconImage;    // small atlas-packed icon
	TSharedPtr<FUIImage> HairlineImage;
};
