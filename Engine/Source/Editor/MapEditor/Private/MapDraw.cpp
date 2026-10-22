// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "MapDraw.h"

#include "MapEditorPrivate.h"
#include "MapLandscapeTextures.h"
#include "Assets/RenderAssets.h"

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
	Batches.clear();
	InstanceBatches.clear();
	MaterialVertices.clear();
	MaterialBatches.clear();
}

bool FMapDraw::ZBufMaterialMesh(const Object& Obj, const Matrix44& ToView)
{
	const auto Asset = Obj.GetMeshAsset();
	if (!bMaterials || !Asset)
		return false;
	const auto& Mesh = *Asset->Data.Mesh;
	const bool Translucent = Asset->Material && Asset->Material->BlendMode == EMaterialBlendMode::Translucent;
	Vector3d Origin, Axis[3], Cofactor[3];
	ToView.Transform(Origin, Vector3d());
	for (int I = 0; I < 3; ++I)
	{
		Vector3d Unit(I == 0 ? 1.0f : 0.0f, I == 1 ? 1.0f : 0.0f, I == 2 ? 1.0f : 0.0f);
		ToView.Transform(Axis[I], Unit);
		Axis[I].SubVector(Origin);
	}
	Cofactor[0].CrossVector(Axis[1], Axis[2]);
	Cofactor[1].CrossVector(Axis[2], Axis[0]);
	Cofactor[2].CrossVector(Axis[0], Axis[1]);
	const float Det = Axis[0].DotVector(Cofactor[0]);
	if (!std::isfinite(Det) || std::abs(Det) < 1e-30f)
		return false;
	FMapMaterialBatch Batch;
	Batch.Mesh = &Obj;
	Batch.FirstVertex = static_cast<uint32>(MaterialVertices.size());
	for (const auto& T : Mesh.GetTriangles())
	{
		Vector3d P[3], A, B, Face;
		for (int K = 0; K < 3; ++K)
			ToView.Transform(P[K], Mesh.GetVertex(T.v[K]));
		A.SubVector(P[1], P[0]);
		B.SubVector(P[2], P[0]);
		Face.CrossVector(A, B);
		const float Facing = Face.DotVector(P[0]);
		if (Det < 0 ? Facing <= 0 : Facing >= 0)
			continue;
		const uint32 TriangleStart = static_cast<uint32>(MaterialVertices.size());
		for (int K = 0; K < 3; ++K)
		{
			const auto& N = T.norm[K];
			Vector3d Normal;
			Normal.NewVector(
				(Cofactor[0].GetX() * N.GetX() + Cofactor[1].GetX() * N.GetY() + Cofactor[2].GetX() * N.GetZ()) / Det,
				(Cofactor[0].GetY() * N.GetX() + Cofactor[1].GetY() * N.GetY() + Cofactor[2].GetY() * N.GetZ()) / Det,
				(Cofactor[0].GetZ() * N.GetX() + Cofactor[1].GetZ() * N.GetY() + Cofactor[2].GetZ() * N.GetZ()) / Det);
			const float Length = Normal.GetMag();
			if (Length > 1e-20f)
				Normal.MultVector(1 / Length);
			FMapMaterialVertex V = {{P[K].GetX(), P[K].GetY(), P[K].GetZ()},
									{Normal.GetX(), Normal.GetY(), Normal.GetZ()},
									{T.uv[K][0], T.uv[K][1]},
									Mesh.HasVertexColors() ? Mesh.GetVertexColor(T.v[K]) : 0xffffffffu};
			MaterialVertices.push_back(V);
		}
		if (Translucent)
			MaterialBatches.push_back({&Obj, TriangleStart, 3, (P[0].GetY() + P[1].GetY() + P[2].GetY()) / 3});
	}
	Batch.NumVertices = static_cast<uint32>(MaterialVertices.size()) - Batch.FirstVertex;
	if (Batch.NumVertices && !Translucent)
		MaterialBatches.push_back(Batch);
	return true;
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

void FMapDraw::FlushLines(FSlateWindowElementList& Out, int32 Layer)
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

FColor FMapDraw::ObjectColor(float Shade) const
{
	// Landscapes in Unreal's neutral grey-green, not a color of their own.
	if (zbufObject && zbufObject->HasLandscape())
	{
		const auto Channel = [Shade](float V) { return static_cast<uint8>(FMath::Clamp(V * Shade * 255.0f + 0.5f, 0.0f, 255.0f)); };
		return FColor(Channel(0.62f), Channel(0.66f), Channel(0.58f));
	}
	// FDraw::ZBufSetup made zbufColor from the object and the side; take the
	// side out again so every face of an object has the same hue.
	const int Axis = FMath::Clamp(axisAlign, 0, 5);
	const uint32 ObjectKey = static_cast<uint32>((zbufColor - Axis * 10 + 255 * 4) % 255);
	const float Hue = static_cast<float>((ObjectKey * 37u) % 360u);
	return HsvToColor(Hue, 0.28f, 0.82f, Shade);
}

void FMapDraw::AddVertex(const FMapVertex& Vertex, const Object* Textured)
{
	if (Batches.empty() || Batches.back().Landscape != Textured)
	{
		FMapTriangleBatch Batch;
		Batch.Landscape = Textured;
		Batch.FirstVertex = static_cast<uint32>(Triangles.size());
		Batches.push_back(Batch);
	}
	Triangles.push_back(Vertex);
	++Batches.back().NumVertices;
}

void FMapDraw::PushTriangle(const ZPoint Points[3], const uint32 Colors[3], const Object* Textured)
{
	for (int Index = 0; Index < 3; ++Index)
	{
		FMapVertex Vertex;
		Vertex.X = Points[Index].x + static_cast<float>(orgX);
		Vertex.Y = static_cast<float>(orgY) - Points[Index].y;
		Vertex.Q = 1.0f / FMath::Max(Points[Index].z, 1e-4f);
		Vertex.U = Textured ? Points[Index].s : 0.0f;
		Vertex.V = Textured ? Points[Index].t : 0.0f;
		Vertex.Color = Colors[Index];
		AddVertex(Vertex, Textured);
	}
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
		// A shade per side, so the faces of a brush read as a solid.
		Color = ObjectColor(bShaded ? FaceShade[Axis] : 0.86f);
	}
	const uint32 Packed = PackRGBA(Color);
	const uint32 Colors[3] = { Packed, Packed, Packed };
	PushTriangle(Points, Colors);
}

void FMapDraw::ZBufShadedTriangle(ZPoint Points[3], const float Shades[3])
{
	uint32 Colors[3];
	for (int Index = 0; Index < 3; ++Index)
	{
		Colors[Index] = PackRGBA(ObjectColor(LitShade(Shades[Index])));
	}
	PushTriangle(Points, Colors);
}

void FMapDraw::ZBufColoredTriangle(ZPoint Points[3], const float Shades[3], const unsigned int Colors[3])
{
	uint32 Packed[3];
	for (int Index = 0; Index < 3; ++Index)
	{
		const float Shade = LitShade(Shades[Index]);
		const auto Channel = [Shade](uint32 V) { return static_cast<uint8>(FMath::Clamp(static_cast<float>(V) * Shade + 0.5f, 0.0f, 255.0f)); };
		const uint32 C = Colors[Index];
		Packed[Index] = PackRGBA(FColor(Channel(C & 0xFF), Channel((C >> 8) & 0xFF), Channel((C >> 16) & 0xFF)));
	}
	PushTriangle(Points, Packed);
}

void FMapDraw::ZBufLandscapeTriangle(ZPoint Points[3], const float Shades[3], const unsigned int Colors[3])
{
	// Untextured landscapes keep their layers' colors per corner.
	if (!zbufObject || !zbufObject->HasLandscape() || !FLandscapeTextureLibrary::IsTextured(zbufObject->GetLandscape()))
	{
		if (Colors)
		{
			ZBufColoredTriangle(Points, Shades, Colors);
		}
		else
		{
			ZBufShadedTriangle(Points, Shades);
		}
		return;
	}
	// The baked image has the colors; the corners just the light.
	uint32 Packed[3];
	for (int Index = 0; Index < 3; ++Index)
	{
		const uint8 Light = static_cast<uint8>(LitShade(Shades[Index]) * 255.0f + 0.5f);
		Packed[Index] = PackRGBA(FColor(Light, Light, Light));
	}
	PushTriangle(Points, Packed, zbufObject);
}

void FMapDraw::ZBufInstanceLight(const Vector3d& Light, const Vector3d& Up, float WinWidth)
{
	InstanceLight = Light;
	InstanceUp = Up;
	InstanceWinWidth = WinWidth;
}

bool FMapDraw::ZBufMeshInstance(const StaticMesh& Mesh, const Matrix44& ToView)
{
	if (!bInstancing || !Mesh.HasVertexColors())
	{
		return false;
	}
	FMapInstanceBatch* Batch = nullptr;
	for (FMapInstanceBatch& Existing : InstanceBatches)
	{
		Batch = Existing.Mesh == &Mesh ? &Existing : Batch;
	}
	if (!Batch)
	{
		InstanceBatches.push_back(FMapInstanceBatch{ &Mesh, {} });
		Batch = &InstanceBatches.back();
	}
	for (int Row = 0; Row < 3; ++Row)
	{
		for (int Col = 0; Col < 4; ++Col)
		{
			Batch->Rows.push_back(ToView.GetMatrix(Row, Col));
		}
	}
	return true;
}

void FMapDraw::DepthLine(float X1, float Y1, float Q1, float X2, float Y2, float Q2)
{
	const FVector2 A(X1, Y1);
	const FVector2 B(X2, Y2);
	const FVector2 Delta = B - A;
	const float Length = Delta.Size();
	if (Length < 1e-3f)
	{
		return;
	}
	const FVector2 Normal = FVector2(-Delta.Y, Delta.X) * (LineThickness * 0.5f / Length);
	// A little farther than it is, so faces in its plane cover it.
	constexpr float Bias = 0.998f;
	const uint32 Packed = PackRGBA(GetPaletteColor(CurrentColor));
	const auto Vertex = [Packed](FVector2 P, float Q)
	{
		FMapVertex V;
		V.X = P.X;
		V.Y = P.Y;
		V.Q = Q * Bias;
		V.U = 0.0f;
		V.V = 0.0f;
		V.Color = Packed;
		return V;
	};
	const FMapVertex Corners[4] = { Vertex(A + Normal, Q1), Vertex(B + Normal, Q2), Vertex(B - Normal, Q2), Vertex(A - Normal, Q1) };
	for (const int32 Index : { 0, 1, 2, 0, 2, 3 })
	{
		AddVertex(Corners[Index], nullptr);
	}
}
