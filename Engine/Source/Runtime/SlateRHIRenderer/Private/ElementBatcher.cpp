// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "ElementBatcher.h"

const char* ToString(ESlateShader Shader)
{
	switch (Shader)
	{
	case ESlateShader::Default: return "Default";
	case ESlateShader::RoundedBox: return "RoundedBox";
	case ESlateShader::Line: return "Line";
	case ESlateShader::Count: break;
	}
	return "?";
}

void FBatchedElements::Reset()
{
	Vertices.clear();
	Indices.clear();
	Batches.clear();
	ClipRects.clear();
	ViewportSize = FIntPoint(0, 0);
}

namespace
{
	FSlateVertex MakeVertex(FVector2 Position, FVector2 UV, FColor Color)
	{
		FSlateVertex V{};
		V.Position = Position;
		V.UV = UV;
		V.Color = Color;
		return V;
	}

	// Maps a normalized coordinate of an image into its place in a texture.
	FVector2 MapUV(const FRect& Rect, FVector2 UV)
	{
		return FVector2(Rect.Left + UV.X * Rect.Width(), Rect.Top + UV.Y * Rect.Height());
	}
}

void FSlateElementBatcher::Batch(const FSlateWindowElementList& Elements, FSlateFontCache* InFontCache, FSlateShaderResourceManager& InResolver, FBatchedElements& InOut)
{
	Out = &InOut;
	FontCache = InFontCache;
	Resolver = &InResolver;
	Out->Reset();
	Out->ClipRects = Elements.GetClipRects();
	Out->ViewportSize = Elements.GetViewportSize();

	const TArray<FSlateDrawElement>& List = Elements.GetElements();
	TArray<int32> Order(List.size());
	for (int32 I = 0; I < static_cast<int32>(Order.size()); ++I)
	{
		Order[I] = I;
	}
	std::stable_sort(Order.begin(), Order.end(), [&List](int32 A, int32 B) { return List[A].Layer < List[B].Layer; });

	for (int32 Index : Order)
	{
		const FSlateDrawElement& Element = List[Index];
		CurrentElement = &Element;
		bNewElement = true;
		switch (Element.Type)
		{
		case EElementType::Box: AddBox(Element); break;
		case EElementType::RoundedBox: AddRoundedBox(Element); break;
		case EElementType::Text: AddText(Element); break;
		case EElementType::Lines: AddLines(Element); break;
		case EElementType::CustomVerts: AddCustomVerts(Element); break;
		case EElementType::Viewport: AddViewport(Element); break;
		}
	}

	// Drop batches that ended up empty (e.g. text of only spaces).
	std::erase_if(Out->Batches, [](const FSlateRenderBatch& B) { return B.NumIndices == 0; });
	CurrentElement = nullptr;
	Out = nullptr;
}

void FSlateElementBatcher::UseKey(const FKey& Key)
{
	TArray<FSlateRenderBatch>& Batches = Out->Batches;
	const bool bSame = !Batches.empty() && Batches.back().Shader == Key.Shader && Batches.back().Blend == Key.Blend
		&& Batches.back().Texture == Key.Texture && Batches.back().ClipIndex == Key.ClipIndex;
	if (!bSame)
	{
		FSlateRenderBatch& Batch = Batches.emplace_back();
		Batch.Layer = CurrentElement->Layer;
		Batch.Shader = Key.Shader;
		Batch.Blend = Key.Blend;
		Batch.Texture = Key.Texture;
		Batch.ClipIndex = Key.ClipIndex;
		Batch.FirstIndex = static_cast<uint32>(Out->Indices.size());
		Batch.NumElements = 1;
		bNewElement = false;
	}
	else if (bNewElement)
	{
		++Batches.back().NumElements;
		bNewElement = false;
	}
}

uint32 FSlateElementBatcher::AddVertex(const FSlateVertex& Vertex)
{
	Out->Vertices.push_back(Vertex);
	return static_cast<uint32>(Out->Vertices.size() - 1);
}

void FSlateElementBatcher::AddQuad(uint32 V0)
{
	const uint32 Quad[6] = { V0, V0 + 1, V0 + 2, V0, V0 + 2, V0 + 3 };
	Out->Indices.insert(Out->Indices.end(), std::begin(Quad), std::end(Quad));
	Out->Batches.back().NumIndices += 6;
}

FVector2 FSlateElementBatcher::Snap(FVector2 Pixel) const
{
	return bPixelSnapping ? Pixel.Round() : Pixel;
}

void FSlateElementBatcher::AddImageQuad(const FTransform2D& Transform, const FRect& LocalRect, const FRect& UV, FColor Color, bool bSnap)
{
	FVector2 P[4];
	if (bSnap && Transform.IsAxisAligned())
	{
		// Snapping both corners keeps shared edges of neighbouring quads
		// (9-slice cells, tiles) on the same pixel.
		const FVector2 A = Snap(Transform.TransformPoint(LocalRect.GetTopLeft()));
		const FVector2 B = Snap(Transform.TransformPoint(LocalRect.GetBottomRight()));
		P[0] = A;
		P[1] = FVector2(B.X, A.Y);
		P[2] = B;
		P[3] = FVector2(A.X, B.Y);
	}
	else
	{
		P[0] = Transform.TransformPoint({ LocalRect.Left, LocalRect.Top });
		P[1] = Transform.TransformPoint({ LocalRect.Right, LocalRect.Top });
		P[2] = Transform.TransformPoint({ LocalRect.Right, LocalRect.Bottom });
		P[3] = Transform.TransformPoint({ LocalRect.Left, LocalRect.Bottom });
	}
	const uint32 V0 = AddVertex(MakeVertex(P[0], { UV.Left, UV.Top }, Color));
	AddVertex(MakeVertex(P[1], { UV.Right, UV.Top }, Color));
	AddVertex(MakeVertex(P[2], { UV.Right, UV.Bottom }, Color));
	AddVertex(MakeVertex(P[3], { UV.Left, UV.Bottom }, Color));
	AddQuad(V0);
}

void FSlateElementBatcher::AddBox(const FSlateDrawElement& Element)
{
	const FBrush& Brush = Element.Get<FSlateBoxElement>().Brush;
	const FPaintGeometry& Geometry = Element.Geometry;
	const FVector2 Size = Geometry.LocalSize;

	FSlateResolvedTexture Texture;
	if (Brush.Image)
	{
		Texture = Resolver->ResolveImage(*Brush.Image);
	}
	else
	{
		Texture.Id = Resolver->GetWhiteTexture();
	}
	UseKey({ ESlateShader::Default, ESlateBlendMode::Alpha, Texture.Id, Element.ClipIndex });

	// The part of the texture the brush uses.
	const FRect Region(MapUV(Texture.UVRect, Brush.UVRegion.GetTopLeft()), MapUV(Texture.UVRect, Brush.UVRegion.GetBottomRight()));
	const FColor Color = Element.Color;

	if (!Brush.Image || Brush.DrawType == EBrushDrawType::Image)
	{
		if (!Brush.Image || Brush.Tiling == EBrushTiling::NoTile)
		{
			AddImageQuad(Geometry.Transform, FRect(FVector2(), Size), Region, Color, true);
			return;
		}
		// Tiles are one image pixel per logical unit. Atlas textures cannot
		// use wrap sampling, so tiles are separate quads.
		const FVector2 TileSize(Brush.Image->GetWidth() * Brush.UVRegion.Width(), Brush.Image->GetHeight() * Brush.UVRegion.Height());
		const bool bTileX = Brush.Tiling == EBrushTiling::Horizontal || Brush.Tiling == EBrushTiling::Both;
		const bool bTileY = Brush.Tiling == EBrushTiling::Vertical || Brush.Tiling == EBrushTiling::Both;
		const float StepX = bTileX ? TileSize.X : Size.X;
		const float StepY = bTileY ? TileSize.Y : Size.Y;
		constexpr int32 MaxTiles = 4096;
		int32 Count = 0;
		for (float Y = 0.0f; Y < Size.Y && Count < MaxTiles; Y += StepY)
		{
			const float H = FMath::Min(StepY, Size.Y - Y);
			for (float X = 0.0f; X < Size.X && Count < MaxTiles; X += StepX, ++Count)
			{
				const float W = FMath::Min(StepX, Size.X - X);
				const FRect UV(Region.Left, Region.Top,
					bTileX ? Region.Left + Region.Width() * (W / StepX) : Region.Right,
					bTileY ? Region.Top + Region.Height() * (H / StepY) : Region.Bottom);
				AddImageQuad(Geometry.Transform, FRect(X, Y, X + W, Y + H), UV, Color, true);
			}
		}
		return;
	}

	// 9-slice. Margins are in image pixels and drawn at one logical unit
	// per pixel; when the box is too small they shrink proportionally.
	const FMargin& M = Brush.Margin;
	const float ImageW = static_cast<float>(Brush.Image->GetWidth()) * Brush.UVRegion.Width();
	const float ImageH = static_cast<float>(Brush.Image->GetHeight()) * Brush.UVRegion.Height();
	const float FitX = (M.Left + M.Right) > Size.X && (M.Left + M.Right) > 0.0f ? Size.X / (M.Left + M.Right) : 1.0f;
	const float FitY = (M.Top + M.Bottom) > Size.Y && (M.Top + M.Bottom) > 0.0f ? Size.Y / (M.Top + M.Bottom) : 1.0f;

	const float Xs[4] = { 0.0f, M.Left * FitX, Size.X - M.Right * FitX, Size.X };
	const float Ys[4] = { 0.0f, M.Top * FitY, Size.Y - M.Bottom * FitY, Size.Y };
	const float Us[4] = { Region.Left, Region.Left + Region.Width() * (M.Left / ImageW), Region.Right - Region.Width() * (M.Right / ImageW), Region.Right };
	const float Vs[4] = { Region.Top, Region.Top + Region.Height() * (M.Top / ImageH), Region.Bottom - Region.Height() * (M.Bottom / ImageH), Region.Bottom };

	for (int32 Row = 0; Row < 3; ++Row)
	{
		for (int32 Col = 0; Col < 3; ++Col)
		{
			if (Row == 1 && Col == 1 && Brush.DrawType == EBrushDrawType::Border)
			{
				continue;
			}
			if (Xs[Col + 1] <= Xs[Col] || Ys[Row + 1] <= Ys[Row])
			{
				continue;
			}
			AddImageQuad(Geometry.Transform, FRect(Xs[Col], Ys[Row], Xs[Col + 1], Ys[Row + 1]),
				FRect(Us[Col], Vs[Row], Us[Col + 1], Vs[Row + 1]), Color, true);
		}
	}
}

void FSlateElementBatcher::AddRoundedBox(const FSlateDrawElement& Element)
{
	const FSlateRoundedBoxElement& Data = Element.Get<FSlateRoundedBoxElement>();
	UseKey({ ESlateShader::RoundedBox, ESlateBlendMode::Premultiplied, Resolver->GetWhiteTexture(), Element.ClipIndex });

	// Work in pixels when axis-aligned (snapped edges stay crisp), else in
	// local units with LocalToPixel converting to pixels.
	const float PixelScale = Element.Geometry.GetScale();
	FTransform2D Transform = Element.Geometry.Transform;
	FRect Rect(FVector2(), Element.Geometry.LocalSize);
	float LocalToPixel = PixelScale;
	if (Transform.IsAxisAligned())
	{
		const FRect Bounds = Element.Geometry.GetPixelBounds();
		Rect = FRect(Snap(Bounds.GetTopLeft()), Snap(Bounds.GetBottomRight()));
		Transform = FTransform2D();
		LocalToPixel = 1.0f;
	}
	const FVector2 HalfPixels = Rect.GetSize() * (0.5f * LocalToPixel);
	const float MaxRadius = FMath::Min(HalfPixels.X, HalfPixels.Y);

	FSlateVertex Base{};
	Base.Color = Element.Color;
	Base.SecondaryColor = Data.OutlineColor;
	Base.Params0[0] = HalfPixels.X;
	Base.Params0[1] = HalfPixels.Y;
	Base.Params0[2] = Data.OutlineColor.A > 0 ? Data.OutlineWidth * PixelScale : 0.0f;
	for (int32 I = 0; I < 4; ++I)
	{
		Base.Params1[I] = FMath::Clamp(Data.CornerRadii[I] * PixelScale, 0.0f, MaxRadius);
	}

	// One pixel of margin for the anti-aliased edge.
	const float Margin = LocalToPixel > 0.0f ? 1.0f / LocalToPixel : 1.0f;
	const FRect Expanded = Rect.Expand(FMargin(Margin));
	const FVector2 Center = Rect.GetCenter();
	const FVector2 Corners[4] = {
		Expanded.GetTopLeft(), { Expanded.Right, Expanded.Top }, Expanded.GetBottomRight(), { Expanded.Left, Expanded.Bottom } };
	uint32 V0 = 0;
	for (int32 I = 0; I < 4; ++I)
	{
		FSlateVertex V = Base;
		V.Position = Transform.TransformPoint(Corners[I]);
		V.UV = (Corners[I] - Center) * LocalToPixel; // pixels from the center
		const uint32 Index = AddVertex(V);
		if (I == 0)
		{
			V0 = Index;
		}
	}
	AddQuad(V0);
}

void FSlateElementBatcher::AddText(const FSlateDrawElement& Element)
{
	const FShapedGlyphSequence& Text = *Element.Get<FSlateTextElement>().Text;
	if (!ensuref(FontCache, "Text elements need a font cache"))
	{
		return;
	}
	const FTransform2D& Transform = Element.Geometry.Transform;
	const bool bAxisAligned = Transform.IsAxisAligned();
	// The baseline origin, on a whole pixel so glyph bitmaps map 1:1.
	FVector2 Origin = Transform.TransformPoint(FVector2(0.0f, Text.GetLogicalBaseline()));
	if (bAxisAligned)
	{
		Origin = Origin.Round();
	}
	const float InvPage = 1.0f / static_cast<float>(FFontAtlasPage::Size);

	for (const FShapedGlyphEntry& Glyph : Text.Glyphs)
	{
		const FCachedGlyph& Cached = FontCache->GetGlyph(*Glyph.Face, Text.PixelSize, Glyph.GlyphIndex);
		if (Cached.PageIndex < 0)
		{
			continue;
		}
		UseKey({ ESlateShader::Default, ESlateBlendMode::Alpha, Resolver->GetGlyphPageTexture(Cached.PageIndex), Element.ClipIndex });

		const float W = static_cast<float>(Cached.AtlasRect.Width());
		const float H = static_cast<float>(Cached.AtlasRect.Height());
		const FRect UV(Cached.AtlasRect.Left * InvPage, Cached.AtlasRect.Top * InvPage, Cached.AtlasRect.Right * InvPage, Cached.AtlasRect.Bottom * InvPage);
		if (bAxisAligned)
		{
			const FVector2 TopLeft(Origin.X + FMath::RoundToFloat(Glyph.X) + Cached.BearingX, Origin.Y + FMath::RoundToFloat(Glyph.Y) - Cached.BearingY);
			AddImageQuad(FTransform2D(), FRect(TopLeft, TopLeft + FVector2(W, H)), UV, Element.Color, false);
		}
		else
		{
			// Rotated text: place the pixel-sized quad in local space.
			const float InvScale = 1.0f / Text.Scale;
			const FVector2 TopLeft = FVector2(Glyph.X + Cached.BearingX, Text.Ascender + Glyph.Y - Cached.BearingY) * InvScale;
			AddImageQuad(Transform, FRect(TopLeft, TopLeft + FVector2(W, H) * InvScale), UV, Element.Color, false);
		}
	}
}

void FSlateElementBatcher::AddLines(const FSlateDrawElement& Element)
{
	const FSlateLineElement& Data = Element.Get<FSlateLineElement>();
	UseKey({ ESlateShader::Line, ESlateBlendMode::Alpha, Resolver->GetWhiteTexture(), Element.ClipIndex });

	TArray<FVector2> Points;
	Points.reserve(Data.Points.size() + 1);
	for (const FVector2& P : Data.Points)
	{
		const FVector2 Pixel = Element.Geometry.Transform.TransformPoint(P);
		if (Points.empty() || (Pixel - Points.back()).SizeSquared() > 1e-6f)
		{
			Points.push_back(Pixel);
		}
	}
	if (Data.bClosed && Points.size() > 2 && (Points.front() - Points.back()).SizeSquared() <= 1e-6f)
	{
		Points.pop_back();
	}
	const int32 Count = static_cast<int32>(Points.size());
	if (Count < 2)
	{
		return;
	}

	// Lines thinner than a pixel are drawn one pixel wide and faded.
	const float Thickness = Data.Thickness * Element.Geometry.GetScale();
	const float HalfWidth = FMath::Max(Thickness, 1.0f) * 0.5f;
	const float Fade = FMath::Clamp(Thickness, 0.0f, 1.0f);
	const float Outer = HalfWidth + 1.0f; // one pixel of anti-aliasing fringe
	constexpr float MiterLimit = 4.0f;

	const bool bClosed = Data.bClosed && Count > 2;
	const int32 NumStrip = bClosed ? Count + 1 : Count;
	uint32 Previous = 0;
	for (int32 S = 0; S < NumStrip; ++S)
	{
		const int32 I = S % Count;
		const FVector2 P = Points[I];
		const bool bHasPrev = bClosed || I > 0;
		const bool bHasNext = bClosed || I < Count - 1;
		const FVector2 DirIn = bHasPrev ? (P - Points[(I + Count - 1) % Count]).GetSafeNormal() : FVector2();
		const FVector2 DirOut = bHasNext ? (Points[(I + 1) % Count] - P).GetSafeNormal() : FVector2();

		FVector2 Normal;
		float Extent = Outer;
		if (bHasPrev && bHasNext)
		{
			const FVector2 NIn = DirIn.GetPerpendicular();
			const FVector2 NOut = DirOut.GetPerpendicular();
			FVector2 Miter = (NIn + NOut).GetSafeNormal();
			const float Cos = FVector2::Dot(Miter, NOut);
			if (Miter.SizeSquared() < 0.5f || Cos < 1.0f / MiterLimit)
			{
				// Near-reversal: fall back to the outgoing normal.
				Miter = NOut;
				Extent = Outer;
			}
			else
			{
				Extent = Outer / Cos;
			}
			Normal = Miter;
		}
		else
		{
			Normal = (bHasNext ? DirOut : DirIn).GetPerpendicular();
		}
		// Extend open ends by the fringe so caps are anti-aliased too.
		FVector2 Along;
		if (!bClosed && I == 0) Along = -DirOut * 1.0f;
		if (!bClosed && I == Count - 1) Along = DirIn * 1.0f;

		FSlateVertex V{};
		V.Color = Element.Color;
		V.Params0[0] = HalfWidth;
		V.Params0[1] = Fade;
		V.Position = P + Along + Normal * Extent;
		V.UV = FVector2(0.0f, Outer);
		const uint32 A = AddVertex(V);
		V.Position = P + Along - Normal * Extent;
		V.UV = FVector2(0.0f, -Outer);
		AddVertex(V);
		if (S > 0)
		{
			const uint32 Quad[6] = { Previous, Previous + 1, A + 1, Previous, A + 1, A };
			Out->Indices.insert(Out->Indices.end(), std::begin(Quad), std::end(Quad));
			Out->Batches.back().NumIndices += 6;
		}
		Previous = A;
	}
}

void FSlateElementBatcher::AddCustomVerts(const FSlateDrawElement& Element)
{
	const FSlateCustomVertsElement& Data = Element.Get<FSlateCustomVertsElement>();
	FSlateResolvedTexture Texture;
	if (Data.Image)
	{
		Texture = Resolver->ResolveImage(*Data.Image);
	}
	else
	{
		Texture.Id = Resolver->GetWhiteTexture();
	}
	UseKey({ ESlateShader::Default, ESlateBlendMode::Alpha, Texture.Id, Element.ClipIndex });

	const uint32 Base = static_cast<uint32>(Out->Vertices.size());
	for (const FSlateVertex2D& Vertex : Data.Vertices)
	{
		AddVertex(MakeVertex(Element.Geometry.Transform.TransformPoint(Vertex.Position), MapUV(Texture.UVRect, Vertex.UV), Vertex.Color));
	}
	for (uint32 Index : Data.Indices)
	{
		check(Index < Data.Vertices.size());
		Out->Indices.push_back(Base + Index);
	}
	Out->Batches.back().NumIndices += static_cast<uint32>(Data.Indices.size());
}

void FSlateElementBatcher::AddViewport(const FSlateDrawElement& Element)
{
	const FSlateViewportElement& Data = Element.Get<FSlateViewportElement>();
	const FSlateResolvedTexture Texture = Resolver->ResolveRenderTarget(*Data.RenderTarget);
	UseKey({ ESlateShader::Default, Data.bAlphaBlend ? ESlateBlendMode::Alpha : ESlateBlendMode::Opaque, Texture.Id, Element.ClipIndex });
	AddImageQuad(Element.Geometry.Transform, FRect(FVector2(), Element.Geometry.LocalSize), Texture.UVRect, Element.Color, true);
}
