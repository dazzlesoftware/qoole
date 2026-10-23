// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "DrawShowcase.h"
#include "Containers/ContainerAliases.h"
#include "Templates/SharedPointer.h"

namespace
{
	// Palette.
	constexpr FColor Background = FColor(24, 26, 31);
	constexpr FColor PanelFill = FColor(36, 39, 46);
	constexpr FColor PanelOutline = FColor(58, 63, 74);
	constexpr FColor HeaderText = FColor(230, 233, 240);
	constexpr FColor BodyText = FColor(196, 201, 212);
	constexpr FColor DimText = FColor(130, 137, 152);
	constexpr FColor Accent = FColor(86, 156, 255);
	constexpr FColor Accent2 = FColor(255, 166, 77);
	constexpr FColor Accent3 = FColor(112, 214, 140);
	constexpr FColor Accent4 = FColor(232, 98, 120);

	TSharedRef<FSlateTextureData> MakeImage(
	    int32 W, int32 H, const TFunction<FColor(int32, int32)>& Pixel, const char* Name)
	{
		TArray<uint8> Pixels(SIZE_T(W) * H * 4);
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const FColor C = Pixel(X, Y);
				uint8* P = &Pixels[(SIZE_T(Y) * W + X) * 4];
				P[0] = C.R;
				P[1] = C.G;
				P[2] = C.B;
				P[3] = C.A;
			}
		}
		return MakeShared<FSlateTextureData>(W, H, std::move(Pixels), FName(Name));
	}

	FColor Lerp(FColor A, FColor B, float T)
	{
		const TFunction<uint8(uint8, uint8)> L = [T](uint8 X, uint8 Y)
		{ return static_cast<uint8>(FMath::RoundToInt(FMath::Lerp(float(X), float(Y), FMath::Clamp(T, 0.0f, 1.0f)))); };
		return FColor(L(A.R, B.R), L(A.G, B.G), L(A.B, B.B), L(A.A, B.A));
	}

	float Pulse(double Time, float Speed)
	{
		return 0.5f + 0.5f * FMath::Sin(static_cast<float>(Time) * Speed);
	}
} // namespace

FDrawShowcase::FDrawShowcase(FSlateFontCache& InFontCache, FShowcaseFonts InFonts)
    : FontCache(InFontCache), Fonts(std::move(InFonts))
{
	// A bevelled 24x24 frame with 6px corners for 9-slicing: light top-left
	// edge, dark bottom-right edge, rounded corners, gradient center.
	FrameImage = MakeImage(24, 24, [](int32 X, int32 Y)
	{
		const float CX = X + 0.5f, CY = Y + 0.5f;
		const float DX = FMath::Max(0.0f, FMath::Max(6.0f - CX, CX - 18.0f));
		const float DY = FMath::Max(0.0f, FMath::Max(6.0f - CY, CY - 18.0f));
		const float Dist = FMath::Sqrt(DX * DX + DY * DY);
		if (Dist > 6.0f)
			return FColor::Transparent();
		const uint8 Edge = static_cast<uint8>(FMath::Clamp((6.0f - Dist) * 255.0f, 0.0f, 255.0f));
		if (Dist > 4.5f)
			return FColor(200, 210, 230, Edge); // rim
		if (X < 3 || Y < 3)
			return FColor(120, 150, 210); // highlight
		if (X > 20 || Y > 20)
			return FColor(30, 40, 70); // shadow
		return Lerp(FColor(70, 95, 150), FColor(45, 60, 105), Y / 23.0f);
	}, "Frame");

	CheckerImage = MakeImage(16, 16,
	    [](int32 X, int32 Y) { return ((X / 8) + (Y / 8)) % 2 ? FColor(70, 76, 90) : FColor(52, 56, 66); }, "Checker");

	GradientImage = MakeImage(128, 8, [](int32 X, int32)
	{
		const float T = X / 127.0f;
		return T < 0.5f ? Lerp(Accent4, Accent2, T * 2.0f) : Lerp(Accent2, Accent3, (T - 0.5f) * 2.0f);
	}, "Gradient");

	HairlineImage = FSlateTextureData::CreateSolid(3, 3, FColor::White(), FName("Hairline")).ToSharedPtr();

	// A 16x16 "G" icon.
	IconImage = MakeImage(16, 16, [](int32 X, int32 Y)
	{
		const float DX = X - 7.5f, DY = Y - 7.5f;
		const float R = FMath::Sqrt(DX * DX + DY * DY);
		const bool bRing = R > 4.5f && R < 7.5f && !(DX > 0.0f && DY < 0.0f && DY > -4.0f);
		const bool bBar = Y >= 7 && Y <= 9 && X >= 8 && X <= 13;
		return (bRing || bBar) ? Accent : FColor::Transparent();
	}, "Icon");
}

FPaintGeometry FDrawShowcase::Geo(const FShowcaseFrame& Frame, FRect R) const
{
	return FPaintGeometry::Make(R.GetTopLeft() + Frame.Origin, R.GetSize(), Frame.Scale);
}

FVector2 FDrawShowcase::Text(FSlateWindowElementList& List, int32 Layer, const FShowcaseFrame& Frame, FVector2 Position,
    std::string_view String, const TSharedPtr<FCompositeFont>& Font, float Size, FColor Color)
{
	const TSharedRef<const FShapedGlyphSequence> Shaped =
	    FontCache.ShapeText(String, FSlateFontInfo(Font, Size), Frame.Scale);
	List.AddText(
	    Layer, FPaintGeometry::Make(Position + Frame.Origin, Shaped->GetLogicalSize(), Frame.Scale), Shaped, Color);
	return Shaped->GetLogicalSize();
}

FRect FDrawShowcase::Panel(
    FSlateWindowElementList& List, const FShowcaseFrame& Frame, const FRect& Rect, std::string_view Title)
{
	const bool bHovered = Rect.Contains(Frame.MouseLogical);
	List.AddRoundedBox(1, Geo(Frame, Rect), PanelFill, 8.0f, bHovered ? Accent : PanelOutline, 1.0f);
	Text(List, 2, Frame, Rect.GetTopLeft() + FVector2(14.0f, 10.0f), Title, Fonts.Bold, 14.0f, HeaderText);
	List.AddSolidBox(
	    2, Geo(Frame, FRect(Rect.Left + 14.0f, Rect.Top + 34.0f, Rect.Right - 14.0f, Rect.Top + 35.0f)), PanelOutline);
	return FRect(Rect.Left + 14.0f, Rect.Top + 44.0f, Rect.Right - 14.0f, Rect.Bottom - 12.0f);
}

void FDrawShowcase::PaintMain(
    FSlateWindowElementList& List, const FShowcaseFrame& Frame, const TSharedPtr<FSlateRenderTargetRHI>& ViewportTarget)
{
	const FVector2 Size = Frame.LogicalSize;
	List.AddSolidBox(0, Geo(Frame, FRect(FVector2(), Size)), Background);

	// Header.
	List.AddBox(1, Geo(Frame, FRect(16.0f, 16.0f, 32.0f, 32.0f)), FBrush::FromImage(IconImage));
	Text(List, 1, Frame, {40.0f, 12.0f}, "Genesis UI draw elements", Fonts.Bold, 20.0f, HeaderText);
	const FString Info = FString::Format("{} on {}   ·   DPI {:.0f}%   ·   app scale {:.0f}%   ·   {}x{} px   ·   "
	                                     "{:.0f} fps   ·   {} batches, {} vertices",
	    Frame.BackendName, Frame.AdapterName, Frame.DpiScale * 100.0f, Frame.AppScale * 100.0f, Frame.PixelSize.X,
	    Frame.PixelSize.Y, Frame.FramesPerSecond, Frame.Stats.Batches, Frame.Stats.Vertices);
	Text(List, 1, Frame, {16.0f, 42.0f}, Info.View(), Fonts.Regular, 12.0f, DimText);

	// Animated progress bar under the header.
	const float BarWidth = Size.X - 32.0f;
	const float T = static_cast<float>(FMath::Fmod(static_cast<float>(Frame.Time) * 0.25f, 1.0f));
	List.AddRoundedBox(1, Geo(Frame, FRect(16.0f, 64.0f, 16.0f + BarWidth, 68.0f)), PanelFill, 2.0f);
	List.AddRoundedBox(2, Geo(Frame, FRect(16.0f, 64.0f, 16.0f + BarWidth * T, 68.0f)), Accent, 2.0f);

	// Three columns by two rows of panels, laid out in logical units.
	const float Top = 80.0f;
	const float Gap = 12.0f;
	const float ColumnWidth = (Size.X - 32.0f - Gap * 2.0f) / 3.0f;
	const float RowHeight = (Size.Y - Top - 16.0f - Gap) / 2.0f;
	const TFunction<FRect(int32, int32)> Cell = [&](int32 Col, int32 Row)
	{
		const float X = 16.0f + Col * (ColumnWidth + Gap);
		const float Y = Top + Row * (RowHeight + Gap);
		return FRect(X, Y, X + ColumnWidth, Y + RowHeight);
	};
	if (ColumnWidth < 120.0f || RowHeight < 120.0f)
	{
		Text(List, 1, Frame, {16.0f, Top}, "Make the window larger to see the panels.", Fonts.Regular, 14.0f, BodyText);
		return;
	}

	PaintBoxes(List, Frame, Panel(List, Frame, Cell(0, 0), "Boxes, images and 9-slices"));
	PaintRoundedBoxes(List, Frame, Panel(List, Frame, Cell(1, 0), "Rounded boxes (SDF)"));
	PaintLines(List, Frame, Panel(List, Frame, Cell(2, 0), "Anti-aliased lines"));
	PaintClipping(List, Frame, Panel(List, Frame, Cell(0, 1), "Clipping (scissor stack)"));
	PaintText(List, Frame, Panel(List, Frame, Cell(1, 1), "Shaped text"));
	PaintViewport(List, Frame, Panel(List, Frame, Cell(2, 1), "Viewport (render target)"), ViewportTarget);
}

void FDrawShowcase::PaintBoxes(FSlateWindowElementList& List, const FShowcaseFrame& Frame, FRect Area)
{
	// Solid boxes with an animated hue cycle.
	const FColor Colors[] = {Accent, Accent2, Accent3, Accent4};
	for (int32 I = 0; I < 4; ++I)
	{
		const float Phase = Pulse(Frame.Time + I * 0.4, 2.0f);
		const FRect R(Area.Left + I * 30.0f, Area.Top, Area.Left + I * 30.0f + 24.0f, Area.Top + 24.0f);
		List.AddSolidBox(3, Geo(Frame, R), Lerp(Colors[I], FColor::White(), Phase * 0.35f));
	}
	// A 1-logical-unit hairline frame: crisp at every scale thanks to snapping.
	List.AddBox(3, Geo(Frame, FRect(Area.Left + 124.0f, Area.Top, Area.Left + 148.0f, Area.Top + 24.0f)),
	    FBrush::NineSlice(HairlineImage, FMargin(1.0f), BodyText, false));
	List.AddBox(3, Geo(Frame, FRect(Area.Left + 156.0f, Area.Top + 4.0f, Area.Left + 172.0f, Area.Top + 20.0f)),
	    FBrush::FromImage(IconImage));

	// 9-slice at three sizes, plus one tinted, and a border-only one.
	const float Y = Area.Top + 36.0f;
	List.AddBox(
	    3, Geo(Frame, FRect(Area.Left, Y, Area.Left + 48.0f, Y + 32.0f)), FBrush::NineSlice(FrameImage, FMargin(6.0f)));
	List.AddBox(3, Geo(Frame, FRect(Area.Left + 56.0f, Y, Area.Left + 156.0f, Y + 32.0f)),
	    FBrush::NineSlice(FrameImage, FMargin(6.0f)));
	const float Grow = 40.0f * Pulse(Frame.Time, 1.3f);
	List.AddBox(3, Geo(Frame, FRect(Area.Left, Y + 40.0f, Area.Left + 120.0f + Grow, Y + 84.0f)),
	    FBrush::NineSlice(FrameImage, FMargin(6.0f)));
	Text(List, 4, Frame, {Area.Left + 12.0f, Y + 52.0f}, "Stretchy 9-slice", Fonts.Regular, 13.0f, HeaderText);
	List.AddBox(3, Geo(Frame, FRect(Area.Left + 172.0f, Y, Area.Left + 220.0f, Y + 32.0f)),
	    FBrush::NineSlice(FrameImage, FMargin(6.0f), Accent2));
	List.AddBox(3, Geo(Frame, FRect(Area.Left + 172.0f, Y + 40.0f, Area.Left + 220.0f, Y + 84.0f)),
	    FBrush::NineSlice(FrameImage, FMargin(6.0f), FColor::White(), false));

	// Tiled checker and a stretched gradient image.
	const float Y2 = Y + 96.0f;
	if (Y2 + 52.0f < Area.Bottom)
	{
		FBrush Tiled = FBrush::FromImage(CheckerImage);
		Tiled.Tiling = EBrushTiling::Both;
		List.AddBox(3, Geo(Frame, FRect(Area.Left, Y2, Area.Right, Y2 + 36.0f)), Tiled);
		List.AddBox(
		    4, Geo(Frame, FRect(Area.Left, Y2 + 42.0f, Area.Right, Y2 + 52.0f)), FBrush::FromImage(GradientImage));
	}
}

void FDrawShowcase::PaintRoundedBoxes(FSlateWindowElementList& List, const FShowcaseFrame& Frame, FRect Area)
{
	const float W = (Area.Width() - 24.0f) / 3.0f;
	const float H = 44.0f;
	const float Radii[] = {0.0f, 6.0f, 22.0f};
	for (int32 I = 0; I < 3; ++I)
	{
		const FRect R(Area.Left + I * (W + 12.0f), Area.Top, Area.Left + I * (W + 12.0f) + W, Area.Top + H);
		List.AddRoundedBox(3, Geo(Frame, R), Accent, Radii[I]);
	}
	// Outlines of 1, 2 and an animated width.
	const float Y = Area.Top + H + 12.0f;
	const float Widths[] = {1.0f, 2.0f, 1.0f + 5.0f * Pulse(Frame.Time, 2.5f)};
	for (int32 I = 0; I < 3; ++I)
	{
		const FRect R(Area.Left + I * (W + 12.0f), Y, Area.Left + I * (W + 12.0f) + W, Y + H);
		List.AddRoundedBox(3, Geo(Frame, R), PanelFill, 10.0f, Accent2, Widths[I]);
	}
	// Per-corner radii, a pill, a circle and a translucent overlap.
	const float Y2 = Y + H + 12.0f;
	List.AddRoundedBox(3, Geo(Frame, FRect(Area.Left, Y2, Area.Left + W, Y2 + H)), Accent3, {20.0f, 2.0f, 20.0f, 2.0f},
	    FColor::White(), 1.5f);
	List.AddRoundedBox(3, Geo(Frame, FRect(Area.Left + W + 12.0f, Y2 + 8.0f, Area.Left + 2.0f * W + 12.0f, Y2 + 36.0f)),
	    Accent4, 14.0f, FColor(255, 255, 255, 160), 1.0f);
	const FVector2 C(Area.Left + 2.5f * W + 24.0f, Y2 + H * 0.5f);
	List.AddRoundedBox(3, Geo(Frame, FRect(C.X - 22.0f, C.Y - 22.0f, C.X + 22.0f, C.Y + 22.0f)), Accent2, 22.0f);
	if (Y2 + H + 60.0f < Area.Bottom)
	{
		const float Y3 = Y2 + H + 14.0f;
		const float Slide = 60.0f * Pulse(Frame.Time, 1.0f);
		List.AddRoundedBox(
		    3, Geo(Frame, FRect(Area.Left, Y3, Area.Left + 140.0f, Y3 + 40.0f)), FColor(86, 156, 255, 160), 12.0f);
		List.AddRoundedBox(4,
		    Geo(Frame, FRect(Area.Left + 40.0f + Slide, Y3 + 8.0f, Area.Left + 200.0f + Slide, Y3 + 48.0f)),
		    FColor(255, 166, 77, 160), 12.0f, FColor::White(), 1.0f);
	}
}

void FDrawShowcase::PaintLines(FSlateWindowElementList& List, const FShowcaseFrame& Frame, FRect Area)
{
	// Grid of hairlines (1 and 0.5 logical units).
	const FRect Graph(Area.Left, Area.Top, Area.Right, Area.Top + FMath::Min(120.0f, Area.Height() * 0.55f));
	for (float X = Graph.Left; X <= Graph.Right; X += 20.0f)
	{
		List.AddLines(
		    3, Geo(Frame, Graph), {{X - Graph.Left, 0.0f}, {X - Graph.Left, Graph.Height()}}, PanelOutline, 0.5f);
	}
	for (float Y = 0.0f; Y <= Graph.Height(); Y += 20.0f)
	{
		List.AddLines(3, Geo(Frame, Graph), {{0.0f, Y}, {Graph.Width(), Y}}, PanelOutline, 1.0f);
	}
	// Animated sine and a thick zig-zag with mitered joins.
	TArray<FVector2> Wave;
	for (float X = 0.0f; X <= Graph.Width(); X += 3.0f)
	{
		Wave.push_back({X,
		    Graph.Height() * 0.5f +
		        FMath::Sin(X * 0.05f + static_cast<float>(Frame.Time) * 3.0f) * Graph.Height() * 0.35f});
	}
	List.AddLines(4, Geo(Frame, Graph), Wave, Accent, 2.0f);
	TArray<FVector2> Zig;
	for (int32 I = 0; I < 8; ++I)
	{
		Zig.push_back({10.0f + I * (Graph.Width() - 20.0f) / 7.0f, I % 2 ? Graph.Height() - 14.0f : 14.0f});
	}
	List.AddLines(4, Geo(Frame, Graph), Zig, FColor(255, 166, 77, 190), 6.0f);

	// A spinning star (closed polyline) and a spinner of fading spokes.
	const float Y = Graph.Bottom + 12.0f;
	if (Y + 70.0f > Area.Bottom)
	{
		return;
	}
	const FVector2 Center(Area.Left + 40.0f, Y + 36.0f);
	TArray<FVector2> Star;
	const float Spin = static_cast<float>(Frame.Time) * 0.8f;
	for (int32 I = 0; I < 10; ++I)
	{
		const float Angle = Spin + I * FMath::Pi / 5.0f;
		const float R = I % 2 ? 14.0f : 34.0f;
		Star.push_back({FMath::Cos(Angle) * R, FMath::Sin(Angle) * R});
	}
	List.AddLines(4, Geo(Frame, FRect(Center, Center)), Star, Accent3, 1.5f, true);

	const FVector2 Spinner(Area.Left + 120.0f, Y + 36.0f);
	for (int32 I = 0; I < 12; ++I)
	{
		const float Angle = I * FMath::Pi / 6.0f;
		const float Fade = FMath::Fmod(12.0f + I - static_cast<float>(Frame.Time) * 12.0f, 12.0f) / 12.0f;
		const FVector2 Dir(FMath::Cos(Angle), FMath::Sin(Angle));
		List.AddLines(4, Geo(Frame, FRect(Spinner, Spinner)), {Dir * 14.0f, Dir * 30.0f},
		    FColor(230, 233, 240, static_cast<uint8>(40 + 215 * Fade)), 3.0f);
	}
	Text(List, 4, Frame, {Area.Left + 170.0f, Y + 26.0f}, "0.5, 1, 1.5, 2, 3 and 6 px", Fonts.Regular, 12.0f, DimText);
}

void FDrawShowcase::PaintClipping(FSlateWindowElementList& List, const FShowcaseFrame& Frame, FRect Area)
{
	// A list scrolled automatically (and by the mouse wheel) inside a clip.
	const FRect ListRect(Area.Left, Area.Top, Area.Left + Area.Width() * 0.58f, Area.Bottom);
	List.AddRoundedBox(3, Geo(Frame, ListRect), Background, 6.0f, PanelOutline, 1.0f);
	const FRect Inner = ListRect.Inset(FMargin(1.0f));
	List.PushClip(Geo(Frame, Inner));
	const float RowHeight = 26.0f;
	const float Scroll = static_cast<float>(Frame.Time) * 20.0f + Frame.ListScroll;
	const int32 First = FMath::FloorToInt(Scroll / RowHeight);
	for (int32 Row = First; Row < First + static_cast<int32>(Inner.Height() / RowHeight) + 2; ++Row)
	{
		const float Y = Inner.Top + Row * RowHeight - Scroll;
		const FRect R(Inner.Left, Y, Inner.Right, Y + RowHeight);
		if (Row % 2 == 0)
		{
			List.AddSolidBox(4, Geo(Frame, R), FColor(255, 255, 255, 10));
		}
		List.AddRoundedBox(5, Geo(Frame, FRect(R.Left + 8.0f, R.Top + 8.0f, R.Left + 18.0f, R.Top + 18.0f)),
		    Row % 3 ? Accent : Accent3, 5.0f);
		Text(List, 5, Frame, {R.Left + 26.0f, R.Top + 4.0f},
		    FString::Format("Row {} — clipped to the list", Row).View(), Fonts.Regular, 13.0f, BodyText);
	}
	List.PopClip();

	// Nested clips: a big rotating-color box clipped by two overlapping rectangles.
	const FRect Outer(ListRect.Right + 12.0f, Area.Top, Area.Right, Area.Top + 80.0f);
	List.AddRoundedBox(3, Geo(Frame, Outer), FColor(0, 0, 0, 0), 4.0f, PanelOutline, 1.0f);
	List.PushClip(Geo(Frame, Outer));
	const float Slide = 40.0f * Pulse(Frame.Time, 1.7f);
	List.PushClip(
	    Geo(Frame, FRect(Outer.Left + Slide, Outer.Top + 10.0f, Outer.Left + Slide + 60.0f, Outer.Bottom + 40.0f)));
	List.AddSolidBox(4, Geo(Frame, Outer.Expand(FMargin(20.0f))), Accent4);
	Text(List, 5, Frame, {Outer.Left, Outer.Top + 30.0f}, "Nested clip", Fonts.Bold, 16.0f, FColor::White());
	List.PopClip();
	List.PopClip();
	Text(List, 4, Frame, {Outer.Left, Outer.Bottom + 8.0f}, "scroll the list", Fonts.Regular, 12.0f, DimText);
	Text(List, 4, Frame, {Outer.Left, Outer.Bottom + 24.0f}, "with the mouse wheel", Fonts.Regular, 12.0f, DimText);
}

void FDrawShowcase::PaintText(FSlateWindowElementList& List, const FShowcaseFrame& Frame, FRect Area)
{
	List.PushClip(Geo(Frame, Area.Expand(FMargin(4.0f))));
	float Y = Area.Top;
	const float Sizes[] = {10.0f, 12.0f, 14.0f, 18.0f, 26.0f};
	for (const float Size : Sizes)
	{
		Y += Text(List, 4, Frame, {Area.Left, Y}, FString::Format("{:.0f}: The quick brown fox jumps", Size).View(),
		    Fonts.Regular, Size, BodyText)
		         .Y;
	}
	Y += 4.0f;
	Y += Text(List, 4, Frame, {Area.Left, Y}, "Bold: Genesis Editor 0123456789", Fonts.Bold, 15.0f, HeaderText).Y;
	// Non-Latin scripts: Devanagari needs shaping (conjuncts, reordered vowel
	// signs); Greek and Cyrillic come from Noto Sans itself.
	Y += Text(List, 4, Frame, {Area.Left, Y}, "हिन्दी: नमस्ते दुनिया, क्षत्रिय, प्रत्येक", Fonts.Regular, 18.0f, Accent2).Y;
	Y += Text(List, 4, Frame, {Area.Left, Y}, "Ελληνικά · Русский · Café · ﬁ ligature", Fonts.Regular, 15.0f, BodyText)
	         .Y;
	// Animated color and fractional size.
	const float Size = 16.0f + 6.0f * Pulse(Frame.Time, 1.1f);
	Text(List, 4, Frame, {Area.Left, Y + 2.0f}, "Rasterized at the final pixel size", Fonts.Regular, Size,
	    Lerp(Accent, Accent3, Pulse(Frame.Time, 0.7f)));
	List.PopClip();
}

void FDrawShowcase::PaintViewport(FSlateWindowElementList& List, const FShowcaseFrame& Frame, FRect Area,
    const TSharedPtr<FSlateRenderTargetRHI>& ViewportTarget)
{
	if (!ViewportTarget)
	{
		return;
	}
	const FIntPoint TargetSize = ViewportTarget->GetSize();
	// Show the target at one texel per pixel, centered.
	const FVector2 LogicalSize = TargetSize.ToVector2() / Frame.Scale;
	const FVector2 Pos = Area.GetCenter() - LogicalSize * 0.5f;
	List.PushClip(Geo(Frame, Area));
	List.AddViewport(3, Geo(Frame, FRect::FromPosSize(Pos, LogicalSize)), ViewportTarget);
	List.AddRoundedBox(4, Geo(Frame, FRect::FromPosSize(Pos, LogicalSize).Expand(FMargin(1.0f))), FColor::Transparent(),
	    2.0f, PanelOutline, 1.0f);
	List.PopClip();
}

void FDrawShowcase::PaintViewportScene(FSlateWindowElementList& List, const FShowcaseFrame& Frame, FIntPoint TargetSize)
{
	// A rotating fan of triangles (custom vertices) rendered into a texture
	// and composited by a Viewport element. Drawn at the window's scale so
	// the texture maps 1:1 onto screen pixels.
	const float Scale = Frame.Scale;
	const FVector2 Size = TargetSize.ToVector2() / Scale;
	List.SetViewportSize(TargetSize);
	List.AddSolidBox(0, FPaintGeometry::Make({}, Size, Scale), FColor(14, 16, 22));

	TArray<FSlateVertex2D> Vertices;
	TArray<uint32> Indices;
	const FColor Colors[] = {Accent, Accent2, Accent3, Accent4};
	const float Spin = static_cast<float>(Frame.Time) * 0.9f;
	const float Radius = FMath::Min(Size.X, Size.Y) * 0.42f;
	Vertices.push_back({FVector2(), {}, FColor::White()});
	for (int32 I = 0; I <= 24; ++I)
	{
		const float Angle = Spin + I * FMath::Pi / 12.0f;
		Vertices.push_back({FVector2(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius, {}, Colors[(I / 3) % 4]});
		if (I > 0)
		{
			Indices.insert(Indices.end(), {0u, static_cast<uint32>(I), static_cast<uint32>(I + 1)});
		}
	}
	List.AddCustomVerts(
	    1, FPaintGeometry::Make(Size * 0.5f, {}, Scale), nullptr, std::move(Vertices), std::move(Indices));
	Text(List, 2, Frame, {8.0f, 6.0f}, "render target", Fonts.Bold, 13.0f, FColor::White());
}
