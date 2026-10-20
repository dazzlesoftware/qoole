// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "MapDraw.h"

#include "MapEditorPrivate.h"

#include <cmath>

namespace
{
	uint32 PackRGBA(FColor Color)
	{
		return static_cast<uint32>(Color.R) | (static_cast<uint32>(Color.G) << 8) | (static_cast<uint32>(Color.B) << 16) | (static_cast<uint32>(Color.A) << 24);
	}

	// Brightness per facing direction (FDraw's axisAlign: up, down, +Y, -Y,
	// +X, -X), so the faces of a brush read as a solid without lighting.
	constexpr float FaceShade[6] = { 1.0f, 0.45f, 0.8f, 0.62f, 0.72f, 0.9f };

	// Hue in degrees, saturation and value in [0, 1], times Shade.
	FColor HsvToColor(float Hue, float Saturation, float Value, float Shade)
	{
		const float C = Value * Saturation;
		const float H = Hue / 60.0f;
		const float X = C * (1.0f - FMath::Abs(std::fmod(H, 2.0f) - 1.0f));
		float R = 0.0f, G = 0.0f, B = 0.0f;
		switch (static_cast<int>(H) % 6)
		{
		case 0: R = C; G = X; break;
		case 1: R = X; G = C; break;
		case 2: G = C; B = X; break;
		case 3: G = X; B = C; break;
		case 4: R = X; B = C; break;
		default: R = C; B = X; break;
		}
		const float M = Value - C;
		const auto Channel = [Shade](float V) { return static_cast<uint8>(FMath::Clamp((V * Shade) * 255.0f + 0.5f, 0.0f, 255.0f)); };
		return FColor(Channel(R + M), Channel(G + M), Channel(B + M));
	}
}

FMapDraw::FMapDraw()
{
	bits = 32;
	numBufs = 1;
	axisAlign = 0;
	zbufTex = nullptr;
	zbufColor = 0;
	for (int Index = 0; Index < 256; ++Index)
	{
		pal[Index].red = pal[Index].green = pal[Index].blue = 255;
		setpal[Index] = pal[Index];
	}
}

void FMapDraw::Size(int Cx, int Cy)
{
	FDraw::Size(Cx, Cy);
}

void FMapDraw::Reset()
{
	LineVertices.clear();
	LineIndices.clear();
	Triangles.clear();
}

FColor FMapDraw::GetPaletteColor(int Index) const
{
	const palette_t& P = pal[static_cast<uint8>(Index)];
	return FColor(P.red, P.green, P.blue);
}

void FMapDraw::Line(int X1, int Y1, int X2, int Y2)
{
	// A quad one line thickness wide around the segment between pixel
	// centers, extended by half the thickness at both ends so corners close.
	const FVector2 A(static_cast<float>(X1) + 0.5f, static_cast<float>(Y1) + 0.5f);
	const FVector2 B(static_cast<float>(X2) + 0.5f, static_cast<float>(Y2) + 0.5f);
	const FVector2 Delta = B - A;
	const float Length = Delta.Size();
	const float Half = LineThickness * 0.5f;
	const FVector2 Dir = Length > 1e-4f ? Delta / Length : FVector2(1.0f, 0.0f);
	const FVector2 Normal(-Dir.Y * Half, Dir.X * Half);
	const FVector2 Along = Dir * Half;
	const FColor Color = GetPaletteColor(CurrentColor);
	const uint32 Base = static_cast<uint32>(LineVertices.size());
	LineVertices.push_back({ A - Along + Normal, FVector2(0.5f, 0.5f), Color });
	LineVertices.push_back({ B + Along + Normal, FVector2(0.5f, 0.5f), Color });
	LineVertices.push_back({ B + Along - Normal, FVector2(0.5f, 0.5f), Color });
	LineVertices.push_back({ A - Along - Normal, FVector2(0.5f, 0.5f), Color });
	for (uint32 Index : { 0u, 1u, 2u, 0u, 2u, 3u })
	{
		LineIndices.push_back(Base + Index);
	}
}

void FMapDraw::FlushLines(FDrawElementList& Out, int32 Layer)
{
	if (LineVertices.empty())
	{
		return;
	}
	const FPaintGeometry Geometry = FPaintGeometry::Make(FVector2::Zero(), FVector2(static_cast<float>(width), static_cast<float>(height)), 1.0f);
	Out.AddCustomVerts(Layer, Geometry, nullptr, std::move(LineVertices), std::move(LineIndices));
	LineVertices.clear();
	LineIndices.clear();
}

void FMapDraw::ZBufTriangle(ZPoint Points[3])
{
	FColor Color;
	const int Axis = FMath::Clamp(axisAlign, 0, 5);
	if (zbufTex)
	{
		// A texture's solid color per side: a palette index in 8-bit games,
		// RGB565 otherwise.
		Game* GamePtr = Game::Get();
		if (GamePtr && GamePtr->GetPalName()[0])
		{
			const palette_t& P = GamePtr->GetPal().pal[zbufColor & 0xFF];
			Color = FColor(P.red, P.green, P.blue);
		}
		else
		{
			Color = FColor(static_cast<uint8>(((zbufColor >> 11) & 31) << 3), static_cast<uint8>(((zbufColor >> 5) & 63) << 2),
				static_cast<uint8>((zbufColor & 31) << 3));
		}
	}
	else
	{
		// FDraw::ZBufSetup made zbufColor from the object and the side; take
		// the side out again so every face of a brush has the same hue, then
		// shade it per side.
		const uint32 ObjectKey = static_cast<uint32>((zbufColor - Axis * 10 + 255 * 4) % 255);
		const float Hue = static_cast<float>((ObjectKey * 37u) % 360u);
		Color = HsvToColor(Hue, 0.28f, 0.82f, FaceShade[Axis]);
	}

	const uint32 Packed = PackRGBA(Color);
	for (int Index = 0; Index < 3; ++Index)
	{
		FMapVertex Vertex;
		Vertex.X = Points[Index].x + static_cast<float>(orgX);
		Vertex.Y = static_cast<float>(orgY) - Points[Index].y;
		Vertex.Q = 1.0f / FMath::Max(Points[Index].z, 1e-4f);
		Vertex.U = 0.0f;
		Vertex.V = 0.0f;
		Vertex.Color = Packed;
		Triangles.push_back(Vertex);
	}
}
