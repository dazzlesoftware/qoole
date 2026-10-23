// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "MapLandscapeMode.h"

#include "MapCoreHeaders.h"
#include "MapTerrainAttachments.h"
#include "MapLandscapeTextures.h"

#include <algorithm>
#include <cmath>


const char* ToString(ELandscapeTool Tool)
{
	switch (Tool)
	{
	case ELandscapeTool::Sculpt: return "Sculpt";
	case ELandscapeTool::Smooth: return "Smooth";
	case ELandscapeTool::Flatten: return "Flatten";
	case ELandscapeTool::Ramp: return "Ramp";
	case ELandscapeTool::Erosion: return "Erosion";
	case ELandscapeTool::Noise: return "Noise";
	case ELandscapeTool::Splines: return "Splines";
	case ELandscapeTool::Paint: return "Paint";
	}
	return "?";
}

const char* ToString(ELandscapeFlattenMode Mode)
{
	switch (Mode)
	{
	case ELandscapeFlattenMode::Both: return "Both";
	case ELandscapeFlattenMode::Raise: return "Raise";
	case ELandscapeFlattenMode::Lower: return "Lower";
	}
	return "?";
}

float FLandscapeAutoPaint::WeightAt(const Landscape& Land, int32 X, int32 Y) const
{
	// The slope across the point, as the landscape's normals have it.
	const float Size = Land.GetQuadSize();
	const float Dx = (Land.GetHeight(X + 1, Y) - Land.GetHeight(X - 1, Y)) / ((FMath::Min(X + 1, Land.GetQuadsX()) - FMath::Max(X - 1, 0)) * Size);
	const float Dy = (Land.GetHeight(X, Y + 1) - Land.GetHeight(X, Y - 1)) / ((FMath::Min(Y + 1, Land.GetQuadsY()) - FMath::Max(Y - 1, 0)) * Size);
	const float Slope = std::atan(std::sqrt(Dx * Dx + Dy * Dy)) * 57.29578f;
	const float Height = Land.GetHeight(X, Y);
	// 1 inside each range, fading to 0 over its fade outside it.
	const auto Band = [](float V, float Lo, float Hi, float Fade)
	{
		const float Outside = FMath::Max(FMath::Max(Lo - V, V - Hi), 0.0f);
		return Fade > 0.0f ? FMath::Clamp(1.0f - Outside / Fade, 0.0f, 1.0f) : (Outside > 0.0f ? 0.0f : 1.0f);
	};
	return Band(Slope, MinSlope, MaxSlope, SlopeFade) * Band(Height, MinHeight, MaxHeight, HeightFade);
}

namespace
{
	// After a layer's weight at a point changed: the others share what is
	// left, so the weights add up to 1 at most.
	void GiveWay(std::vector<LandscapeLayer>& Layers, SIZE_T Kept, SIZE_T Point)
	{
		float Others = 0.0f;
		for (SIZE_T L = 0; L < Layers.size(); ++L)
		{
			Others += L == Kept ? 0.0f : Layers[L].weights[Point];
		}
		const float Room = 1.0f - Layers[Kept].weights[Point];
		if (Others > Room && Others > 0.0f)
		{
			const float Keep = Room / Others;
			for (SIZE_T L = 0; L < Layers.size(); ++L)
			{
				if (L != Kept)
				{
					Layers[L].weights[Point] *= Keep;
				}
			}
		}
	}

	constexpr float SculptSpeed = 256.0f; // units a second at full strength
	constexpr float NoiseSpeed = 128.0f;
	constexpr float BlendRate = 8.0f;     // Smooth and Flatten, a second at full strength
	constexpr float StrokeStep = 1.0f / 60.0f; // what a click alone does
	constexpr float PaintRate = 4.0f;          // weight a second at full strength
	constexpr float ErosionRate = 6.0f;        // of the excess slope a second

	struct FLayerPreset
	{
		const char* Name;
		FColor Color;
	};
	const FLayerPreset LayerPresets[] = {
		{ "Grass", FColor(92, 146, 62) },
		{ "Rock", FColor(122, 118, 112) },
		{ "Dirt", FColor(122, 92, 62) },
		{ "Sand", FColor(206, 188, 134) },
		{ "Snow", FColor(236, 240, 244) },
	};

	Matrix44 ToScope(const Object& Obj)
	{
		Matrix44 M;
		Obj.CalTransSpaceMatrix(M.SetIdentity());
		return M;
	}

	Matrix44 ToLocal(const Object& Obj)
	{
		Matrix44 M;
		Obj.CalInvTransSpaceMatrix(M.SetIdentity());
		return M;
	}

	// The brush's radius in the landscape's own units.
	float LocalRadius(const Object& Land, float Radius)
	{
		const Vector3d& Scale = Land.GetScale();
		return Radius / FMath::Max(FMath::Max(std::fabs(Scale.GetX()), std::fabs(Scale.GetY())), 1e-3f);
	}

	// 1 inside the inner part of the radius, fading to 0 at its edge.
	float Weight(float Distance, float Radius, float Falloff)
	{
		if (Distance >= Radius)
		{
			return 0.0f;
		}
		const float Inner = Radius * (1.0f - FMath::Clamp(Falloff, 0.0f, 1.0f));
		if (Distance <= Inner)
		{
			return 1.0f;
		}
		const float T = (Radius - Distance) / FMath::Max(Radius - Inner, 1e-4f);
		return T * T * (3.0f - 2.0f * T);
	}

	// -1 to 1, fixed per grid point and seed.
	float Hash(int32 X, int32 Y, uint32 Seed)
	{
		uint32 H = static_cast<uint32>(X) * 374761393u + static_cast<uint32>(Y) * 668265263u + Seed * 2246822519u;
		H = (H ^ (H >> 13)) * 1274126177u;
		H ^= H >> 16;
		return static_cast<float>(H & 0xFFFFu) / 32767.5f - 1.0f;
	}

	bool IsInScope(const FMapDocument& Doc, const Object* Obj)
	{
		const TArray<Object*> Objects = Doc.GetObjects();
		return Obj && std::find(Objects.begin(), Objects.end(), Obj) != Objects.end();
	}
}

void FMapLandscapeMode::SetDocument(FMapDocument* InDocument)
{
	EndStroke();
	Document = InDocument;
	HoverLand = nullptr;
	HoverView = nullptr;
}

Object* FMapLandscapeMode::CreateLandscape(const Vector3d& Center)
{
	return Document ? Document->AddLandscape(NewQuadsX, NewQuadsY, NewQuadSize, Center) : nullptr;
}

Object* FMapLandscapeMode::ImportLandscape(const FString& Path, const Vector3d& Center, FString& Error)
{
	if (!Document)
	{
		Error = "No map is open";
		return nullptr;
	}
	FHeightmap Map;
	if (!FHeightmap::Load(Path, Map, Error))
	{
		return nullptr;
	}
	// One point per pixel, or fewer over the same extent if that's too many.
	const int32 PixelQuads = FMath::Max(Map.Width, Map.Height) - 1;
	const float Shrink = PixelQuads > Landscape::MaxQuads ? static_cast<float>(Landscape::MaxQuads) / static_cast<float>(PixelQuads) : 1.0f;
	const int32 QuadsX = FMath::Clamp(static_cast<int32>(std::lround((Map.Width - 1) * Shrink)), 1, Landscape::MaxQuads);
	const int32 QuadsY = FMath::Clamp(static_cast<int32>(std::lround((Map.Height - 1) * Shrink)), 1, Landscape::MaxQuads);
	const float QuadSize = FMath::Max(NewQuadSize, 1.0f) / Shrink;
	std::shared_ptr<const Landscape> Land = Landscape::Create(QuadsX, QuadsY, QuadSize, Map.ToHeights(QuadsX + 1, QuadsY + 1, HeightmapZRange));
	if (!Land)
	{
		Error = "Can't make a landscape of that size";
		return nullptr;
	}
	return Document->AddLandscape(std::move(Land), Center, "Import Landscape");
}

bool FMapLandscapeMode::ImportHeightmap(Object& Land, const FString& Path, FString& Error)
{
	if (!Document || !Land.HasLandscape())
	{
		Error = "No landscape to import into";
		return false;
	}
	FHeightmap Map;
	if (!FHeightmap::Load(Path, Map, Error))
	{
		return false;
	}
	const Landscape& Current = Land.GetLandscape();
	std::shared_ptr<const Landscape> Updated = Current.WithHeights(Map.ToHeights(Current.GetPointsX(), Current.GetPointsY(), HeightmapZRange));
	if (!Updated)
	{
		Error = "Can't fit the heightmap to the landscape";
		return false;
	}
	const std::shared_ptr<const Landscape> Before = Land.GetLandscapePtr();
	Document->SetLandscape(Land, std::move(Updated), "Import Heightmap");
	SnapFoliageToLandscape(*Document, Land, *Before);
	return true;
}

bool FMapLandscapeMode::ExportHeightmap(const Object& Land, const FString& Path, FString& Error) const
{
	if (!Land.HasLandscape())
	{
		Error = "No landscape to export";
		return false;
	}
	return FHeightmap::FromLandscape(Land.GetLandscape(), HeightmapZRange).Save(Path, Error);
}

bool FMapLandscapeMode::ImportWeightmap(Object& Land, int32 Layer, const FString& Path, FString& Error)
{
	if (!Document || !Land.HasLandscape() || Layer < 0 || Layer >= Land.GetLandscape().GetNumLayers())
	{
		Error = "No paint layer to import into";
		return false;
	}
	FHeightmap Map;
	if (!FHeightmap::Load(Path, Map, Error))
	{
		return false;
	}
	const Landscape& Current = Land.GetLandscape();
	std::vector<LandscapeLayer> Layers = Current.GetLayers();
	const SIZE_T Target = static_cast<SIZE_T>(Layer);
	Layers[Target].weights = Map.ToWeights(Current.GetPointsX(), Current.GetPointsY());
	// The others share what is left at each point, as when painting.
	for (SIZE_T Point = 0; Point < Layers[Target].weights.size(); ++Point)
	{
		float Others = 0.0f;
		for (SIZE_T Index = 0; Index < Layers.size(); ++Index)
		{
			Others += Index == Target ? 0.0f : Layers[Index].weights[Point];
		}
		const float Room = 1.0f - Layers[Target].weights[Point];
		if (Others > Room)
		{
			const float Scale = Room / Others;
			for (SIZE_T Index = 0; Index < Layers.size(); ++Index)
			{
				Layers[Index].weights[Point] *= Index == Target ? 1.0f : Scale;
			}
		}
	}
	std::shared_ptr<const Landscape> Updated = Current.WithLayers(std::move(Layers));
	if (!Updated)
	{
		Error = "Can't fit the weightmap to the landscape";
		return false;
	}
	Document->SetLandscape(Land, std::move(Updated), "Import Weightmap");
	return true;
}

bool FMapLandscapeMode::ExportWeightmap(const Object& Land, int32 Layer, const FString& Path, FString& Error) const
{
	if (!Land.HasLandscape() || Layer < 0 || Layer >= Land.GetLandscape().GetNumLayers())
	{
		Error = "No paint layer to export";
		return false;
	}
	return FHeightmap::FromWeights(Land.GetLandscape(), Layer).Save(Path, Error, 8);
}

Object* FMapLandscapeMode::GetTargetLandscape() const
{
	if (!Document)
	{
		return nullptr;
	}
	for (Object* Obj : Document->GetSelection())
	{
		if (Obj->HasLandscape())
		{
			return Obj;
		}
	}
	for (Object* Obj : Document->GetObjects())
	{
		if (Obj->HasLandscape())
		{
			return Obj;
		}
	}
	return nullptr;
}

bool FMapLandscapeMode::AddLayer(Object& Land)
{
	if (!Land.HasLandscape())
	{
		return false;
	}
	const int32 Count = Land.GetLandscape().GetNumLayers();
	if (Count < static_cast<int32>(std::size(LayerPresets)))
	{
		return AddLayer(Land, LayerPresets[Count].Name, LayerPresets[Count].Color, FLandscapeTextureLibrary::BuiltIn(LayerPresets[Count].Name));
	}
	return AddLayer(Land, FString::Format("Layer {}", Count + 1), FColor(static_cast<uint8>(60 + (Count * 67) % 180), static_cast<uint8>(60 + (Count * 113) % 180), static_cast<uint8>(60 + (Count * 41) % 180)));
}

bool FMapLandscapeMode::AddLayer(Object& Land, const FString& Name, FColor Color, const FString& Texture)
{
	if (!Document || !Land.HasLandscape())
	{
		return false;
	}
	const Landscape& Field = Land.GetLandscape();
	std::vector<LandscapeLayer> Layers = Field.GetLayers();
	Layers.push_back(Field.MakeLayer(Name.ToAnsi().c_str(), Color.R, Color.G, Color.B));
	Layers.back().texture = std::string(Texture.View());
	Document->SetLandscape(Land, Field.WithLayers(std::move(Layers)), "Add Paint Layer");
	SelectedLayer = Land.GetLandscape().GetNumLayers() - 1;
	return true;
}

bool FMapLandscapeMode::RemoveLayer(Object& Land, int32 Layer)
{
	if (!Document || !Land.HasLandscape() || Layer < 0 || Layer >= Land.GetLandscape().GetNumLayers())
	{
		return false;
	}
	std::vector<LandscapeLayer> Layers = Land.GetLandscape().GetLayers();
	Layers.erase(Layers.begin() + Layer);
	Document->SetLandscape(Land, Land.GetLandscape().WithLayers(std::move(Layers)), "Remove Paint Layer");
	SelectedLayer = FMath::Clamp(SelectedLayer, 0, FMath::Max(0, Land.GetLandscape().GetNumLayers() - 1));
	return true;
}

bool FMapLandscapeMode::RenameLayer(Object& Land, int32 Layer, const FString& Name)
{
	if (!Document || !Land.HasLandscape() || Layer < 0 || Layer >= Land.GetLandscape().GetNumLayers()
		|| Land.GetLandscape().GetLayers()[static_cast<SIZE_T>(Layer)].name == Name.ToAnsi())
	{
		return false;
	}
	std::vector<LandscapeLayer> Layers = Land.GetLandscape().GetLayers();
	Layers[static_cast<SIZE_T>(Layer)].name = Name.ToAnsi();
	Document->SetLandscape(Land, Land.GetLandscape().WithLayers(std::move(Layers)), "Rename Paint Layer");
	return true;
}

bool FMapLandscapeMode::SetLayerTexture(Object& Land, int32 Layer, const FString& Texture)
{
	if (!Document || !Land.HasLandscape() || Layer < 0 || Layer >= Land.GetLandscape().GetNumLayers()
		|| Land.GetLandscape().GetLayers()[static_cast<SIZE_T>(Layer)].texture == Texture.View())
	{
		return false;
	}
	std::vector<LandscapeLayer> Layers = Land.GetLandscape().GetLayers();
	Layers[static_cast<SIZE_T>(Layer)].texture = std::string(Texture.View());
	Document->SetLandscape(Land, Land.GetLandscape().WithLayers(std::move(Layers)), "Change Paint Layer Texture");
	return true;
}

bool FMapLandscapeMode::SetLayerTiling(Object& Land, int32 Layer, float Tiling)
{
	Tiling = FMath::Clamp(Tiling, 1.0f, 65536.0f);
	if (!Document || !Land.HasLandscape() || Layer < 0 || Layer >= Land.GetLandscape().GetNumLayers()
		|| Land.GetLandscape().GetLayers()[static_cast<SIZE_T>(Layer)].tiling == Tiling)
	{
		return false;
	}
	std::vector<LandscapeLayer> Layers = Land.GetLandscape().GetLayers();
	Layers[static_cast<SIZE_T>(Layer)].tiling = Tiling;
	Document->SetLandscape(Land, Land.GetLandscape().WithLayers(std::move(Layers)), "Change Paint Layer Tiling");
	return true;
}

bool FMapLandscapeMode::SetLayerColor(Object& Land, int32 Layer, FColor Color)
{
	if (!Document || !Land.HasLandscape() || Layer < 0 || Layer >= Land.GetLandscape().GetNumLayers())
	{
		return false;
	}
	std::vector<LandscapeLayer> Layers = Land.GetLandscape().GetLayers();
	LandscapeLayer& Target = Layers[static_cast<SIZE_T>(Layer)];
	if (Target.r == Color.R && Target.g == Color.G && Target.b == Color.B)
	{
		return false;
	}
	Target.r = Color.R;
	Target.g = Color.G;
	Target.b = Color.B;
	Document->SetLandscape(Land, Land.GetLandscape().WithLayers(std::move(Layers)), "Change Paint Layer Color");
	return true;
}

Object* FMapLandscapeMode::TraceLandscape(const Vector3d& Origin, const Vector3d& Direction, Vector3d& OutHit) const
{
	if (!Document)
	{
		return nullptr;
	}
	Object* Best = nullptr;
	float BestDistance = 0.0f;
	for (Object* Obj : Document->GetObjects())
	{
		if (!Obj->HasLandscape())
		{
			continue;
		}
		// The ray in the landscape's own space.
		const Matrix44 Inverse = ToLocal(*Obj);
		Vector3d LocalOrigin, LocalEnd;
		Inverse.Transform(LocalOrigin, Origin);
		Vector3d End(Origin);
		End.AddVector(Direction);
		Inverse.Transform(LocalEnd, End);
		Vector3d LocalDirection(LocalEnd);
		LocalDirection.SubVector(LocalOrigin);
		const float Length = LocalDirection.GetMag();
		if (Length < 1e-6f)
		{
			continue;
		}
		LocalDirection.MultVector(1.0f / Length);
		float Depth = 0.0f;
		if (!Obj->GetLandscape().Raycast(Line(LocalOrigin, LocalDirection), 0.0f, 1.0e7f, Depth))
		{
			continue;
		}
		Vector3d LocalHit(LocalDirection);
		LocalHit.MultVector(Depth).AddVector(LocalOrigin);
		Vector3d Hit;
		ToScope(*Obj).Transform(Hit, LocalHit);
		Vector3d Offset(Hit);
		const float Distance = Offset.SubVector(Origin).GetMag();
		if (!Best || Distance < BestDistance)
		{
			Best = Obj;
			BestDistance = Distance;
			OutHit = Hit;
		}
	}
	return Best;
}

bool FMapLandscapeMode::ApplyAt(Object& Land, const Vector3d& ScopePoint, float DeltaSeconds, bool bInvertTool)
{
	if (!Document || !Land.HasLandscape() || DeltaSeconds <= 0.0f)
	{
		return false;
	}
	const Landscape& Field = Land.GetLandscape();
	Vector3d Center;
	ToLocal(Land).Transform(Center, ScopePoint);
	const float Radius = LocalRadius(Land, Brush.Radius);
	const float Size = Field.GetQuadSize();
	const float HalfX = Field.GetSizeX() * 0.5f;
	const float HalfY = Field.GetSizeY() * 0.5f;
	const int32 X0 = FMath::Max(0, static_cast<int32>(std::floor((Center.GetX() - Radius + HalfX) / Size)));
	const int32 X1 = FMath::Min(Field.GetQuadsX(), static_cast<int32>(std::ceil((Center.GetX() + Radius + HalfX) / Size)));
	const int32 Y0 = FMath::Max(0, static_cast<int32>(std::floor((Center.GetY() - Radius + HalfY) / Size)));
	const int32 Y1 = FMath::Min(Field.GetQuadsY(), static_cast<int32>(std::ceil((Center.GetY() + Radius + HalfY) / Size)));
	if (X0 > X1 || Y0 > Y1)
	{
		return false;
	}

	const float Strength = FMath::Clamp(Brush.Strength, 0.0f, 1.0f);
	const float Sign = bInvertTool ? -1.0f : 1.0f;

	if (Tool == ELandscapeTool::Paint)
	{
		// Weight-blended: the layer gains, the others give way.
		if (SelectedLayer < 0 || SelectedLayer >= Field.GetNumLayers())
		{
			return false;
		}
		std::vector<LandscapeLayer> Layers = Field.GetLayers();
		const SIZE_T Painted = static_cast<SIZE_T>(SelectedLayer);
		const int32 PointsX = Field.GetPointsX();
		bool bPainted = false;
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				const Vector3d Point = Field.GetPoint(X, Y);
				const float Dx = Point.GetX() - Center.GetX();
				const float Dy = Point.GetY() - Center.GetY();
				const float W = Weight(std::sqrt(Dx * Dx + Dy * Dy), Radius, Brush.Falloff);
				if (W <= 0.0f)
				{
					continue;
				}
				const SIZE_T I = static_cast<SIZE_T>(Y * PointsX + X);
				float& Own = Layers[Painted].weights[I];
				const float Before = Own;
				// Toward the target weight (Shift: toward none), never past it.
				const float Goal = bInvertTool ? 0.0f : FMath::Clamp(PaintTarget, 0.0f, 1.0f);
				const float Step = Strength * PaintRate * DeltaSeconds * W;
				Own = Own < Goal ? FMath::Min(Own + Step, Goal) : FMath::Max(Own - Step, Goal);
				if (Own == Before)
				{
					continue;
				}
				bPainted = true;
				GiveWay(Layers, Painted, I);
			}
		}
		if (!bPainted)
		{
			return false;
		}
		Document->SetLandscape(Land, Field.WithLayers(std::move(Layers)), "Landscape Paint");
		return true;
	}
	if (Tool == ELandscapeTool::Ramp || Tool == ELandscapeTool::Splines)
	{
		return false; // a drag and a release (AddRamp), or points (splines)
	}
	const float Blend = FMath::Min(1.0f, Strength * BlendRate * DeltaSeconds);
	const std::vector<float>& Old = Field.GetHeights();
	std::vector<float> Heights = Old;
	const int32 Stride = Field.GetPointsX();
	bool bChanged = false;

	if (Tool == ELandscapeTool::Erosion)
	{
		// Thermal erosion: where the ground drops more than the talus angle
		// allows to a neighbour, half the excess slides down to it.
		const float MaxDrop = std::tan(FMath::Clamp(ErosionTalus, 1.0f, 89.0f) / 57.29578f) * Size;
		const float Rate = FMath::Min(1.0f, Strength * ErosionRate * DeltaSeconds);
		std::vector<float> Delta(Heights.size(), 0.0f);
		const int32 Offsets[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				const Vector3d Point = Field.GetPoint(X, Y);
				const float Dx = Point.GetX() - Center.GetX();
				const float Dy = Point.GetY() - Center.GetY();
				const float W = Weight(std::sqrt(Dx * Dx + Dy * Dy), Radius, Brush.Falloff);
				if (W <= 0.0f)
				{
					continue;
				}
				const SIZE_T I = static_cast<SIZE_T>(Y * Stride + X);
				for (const auto& Offset : Offsets)
				{
					const int32 Nx = X + Offset[0];
					const int32 Ny = Y + Offset[1];
					if (Nx < 0 || Ny < 0 || Nx > Field.GetQuadsX() || Ny > Field.GetQuadsY())
					{
						continue;
					}
					const SIZE_T N = static_cast<SIZE_T>(Ny * Stride + Nx);
					const float Excess = Old[I] - Old[N] - MaxDrop;
					if (Excess > 0.0f)
					{
						// A quarter each way at most, so a point never gives
						// more than it has above its neighbours.
						const float Moved = Excess * 0.25f * Rate * W;
						Delta[I] -= Moved;
						Delta[N] += Moved;
					}
				}
			}
		}
		for (SIZE_T I = 0; I < Heights.size(); ++I)
		{
			if (Delta[I] != 0.0f)
			{
				Heights[I] += Delta[I];
				bChanged = true;
			}
		}
		if (!bChanged)
		{
			return false;
		}
		const std::shared_ptr<const Landscape> Before = Land.GetLandscapePtr();
		Document->SetLandscape(Land, Field.WithHeights(std::move(Heights)));
		SnapFoliageToLandscape(*Document, Land, *Before);
		return true;
	}

	const float FlattenTo = bFlattenToTarget ? FlattenTarget : FlattenHeight;
	for (int32 Y = Y0; Y <= Y1; ++Y)
	{
		for (int32 X = X0; X <= X1; ++X)
		{
			const Vector3d Point = Field.GetPoint(X, Y);
			const float Dx = Point.GetX() - Center.GetX();
			const float Dy = Point.GetY() - Center.GetY();
			const float W = Weight(std::sqrt(Dx * Dx + Dy * Dy), Radius, Brush.Falloff);
			if (W <= 0.0f)
			{
				continue;
			}
			float& H = Heights[static_cast<SIZE_T>(Y * Stride + X)];
			const float Before = H;
			switch (Tool)
			{
			case ELandscapeTool::Sculpt:
				H += Sign * Strength * SculptSpeed * DeltaSeconds * W;
				break;
			case ELandscapeTool::Smooth:
			{
				float Sum = 0.0f;
				int32 Count = 0;
				for (int32 Ny = FMath::Max(0, Y - 1); Ny <= FMath::Min(Field.GetQuadsY(), Y + 1); ++Ny)
				{
					for (int32 Nx = FMath::Max(0, X - 1); Nx <= FMath::Min(Field.GetQuadsX(), X + 1); ++Nx)
					{
						Sum += Old[static_cast<SIZE_T>(Ny * Stride + Nx)];
						++Count;
					}
				}
				H += (Sum / static_cast<float>(Count) - H) * Blend * W;
				break;
			}
			case ELandscapeTool::Flatten:
			{
				const float Moved = H + (FlattenTo - H) * Blend * W;
				if ((FlattenMode == ELandscapeFlattenMode::Raise && Moved < H) || (FlattenMode == ELandscapeFlattenMode::Lower && Moved > H))
				{
					break;
				}
				H = Moved;
				break;
			}
			case ELandscapeTool::Noise:
				H += Hash(X, Y, NoiseSeed) * Strength * NoiseSpeed * DeltaSeconds * W;
				break;
			default:
				break;
			}
			bChanged |= H != Before;
		}
	}
	if (!bChanged)
	{
		return false;
	}
	const std::shared_ptr<const Landscape> Before = Land.GetLandscapePtr();
	Document->SetLandscape(Land, Field.WithHeights(std::move(Heights)));
	SnapFoliageToLandscape(*Document, Land, *Before);
	return true;
}

bool FMapLandscapeMode::AddRamp(Object& Land, const Vector3d& From, const Vector3d& To)
{
	if (!Document || !Land.HasLandscape())
	{
		return false;
	}
	const Landscape& Field = Land.GetLandscape();
	Vector3d A, B;
	ToLocal(Land).Transform(A, From);
	ToLocal(Land).Transform(B, To);
	const float Ax = A.GetX(), Ay = A.GetY(), Ah = Field.SampleHeight(Ax, Ay);
	const float Bx = B.GetX(), By = B.GetY(), Bh = Field.SampleHeight(Bx, By);
	const float Lx = Bx - Ax, Ly = By - Ay;
	const float Length2 = Lx * Lx + Ly * Ly;
	const float Radius = LocalRadius(Land, Brush.Radius);
	if (Length2 < 1.0f)
	{
		return false;
	}
	std::vector<float> Heights = Field.GetHeights();
	const int32 Stride = Field.GetPointsX();
	bool bChanged = false;
	for (int32 Y = 0; Y <= Field.GetQuadsY(); ++Y)
	{
		for (int32 X = 0; X <= Field.GetQuadsX(); ++X)
		{
			const Vector3d Point = Field.GetPoint(X, Y);
			// Along the ramp (0 at From, 1 at To) and how far to its side.
			const float T = ((Point.GetX() - Ax) * Lx + (Point.GetY() - Ay) * Ly) / Length2;
			if (T < 0.0f || T > 1.0f)
			{
				continue;
			}
			const float Sx = Point.GetX() - (Ax + Lx * T);
			const float Sy = Point.GetY() - (Ay + Ly * T);
			const float W = Weight(std::sqrt(Sx * Sx + Sy * Sy), Radius, Brush.Falloff);
			if (W <= 0.0f)
			{
				continue;
			}
			float& H = Heights[static_cast<SIZE_T>(Y * Stride + X)];
			const float Before = H;
			H += (Ah + (Bh - Ah) * T - H) * W;
			bChanged |= H != Before;
		}
	}
	if (!bChanged)
	{
		return false;
	}
	const std::shared_ptr<const Landscape> Before = Land.GetLandscapePtr();
	Document->SetLandscape(Land, Field.WithHeights(std::move(Heights)), "Landscape Ramp");
	SnapFoliageToLandscape(*Document, Land, *Before);
	return true;
}

bool FMapLandscapeMode::HasSelectedSplinePoint() const
{
	return GetSelectedSpline() && SelectedPoint >= 0 && SelectedPoint < static_cast<int32>(GetSelectedSpline()->points.size());
}

const LandscapeSpline* FMapLandscapeMode::GetSelectedSpline() const
{
	if (!Document || !SplineLand || !IsInScope(*Document, SplineLand) || !SplineLand->HasLandscape())
	{
		return nullptr;
	}
	const std::vector<LandscapeSpline>& Splines = SplineLand->GetLandscape().GetSplines();
	return SelectedSpline >= 0 && SelectedSpline < static_cast<int32>(Splines.size()) ? &Splines[static_cast<SIZE_T>(SelectedSpline)] : nullptr;
}

bool FMapLandscapeMode::SetSpline(Object& Land, int32 Spline, const LandscapeSpline& NewSpline, const FString& Description)
{
	if (!Document || !Land.HasLandscape() || Spline < 0 || Spline >= static_cast<int32>(Land.GetLandscape().GetSplines().size()))
	{
		return false;
	}
	std::vector<LandscapeSpline> Splines = Land.GetLandscape().GetSplines();
	Splines[static_cast<SIZE_T>(Spline)] = NewSpline;
	Document->SetLandscape(Land, Land.GetLandscape().WithSplines(std::move(Splines)), Description);
	return true;
}

bool FMapLandscapeMode::AddSplinePoint(Object& Land, const Vector3d& ScopePoint)
{
	if (!Document || !Land.HasLandscape())
	{
		return false;
	}
	LandscapeSplinePoint Point;
	ToLocal(Land).Transform(Point.position, ScopePoint);
	std::vector<LandscapeSpline> Splines = Land.GetLandscape().GetSplines();
	const bool bExtend = SplineLand == &Land && HasSelectedSplinePoint();
	if (bExtend)
	{
		// After the selected point, its width and falloff; at the start of
		// a spline, before it.
		LandscapeSpline& Spline = Splines[static_cast<SIZE_T>(SelectedSpline)];
		const LandscapeSplinePoint& From = Spline.points[static_cast<SIZE_T>(SelectedPoint)];
		Point.width = From.width;
		Point.falloff = From.falloff;
		const bool bAtStart = SelectedPoint == 0 && Spline.points.size() > 1 && !Spline.closed;
		const int32 At = bAtStart ? 0 : SelectedPoint + 1;
		Spline.points.insert(Spline.points.begin() + At, Point);
		SelectedPoint = At;
	}
	else
	{
		LandscapeSpline Spline;
		Spline.points.push_back(Point);
		Splines.push_back(Spline);
		SplineLand = &Land;
		SelectedSpline = static_cast<int32>(Splines.size()) - 1;
		SelectedPoint = 0;
	}
	Document->SetLandscape(Land, Land.GetLandscape().WithSplines(std::move(Splines)), "Add Spline Point");
	return true;
}

bool FMapLandscapeMode::MoveSplinePoint(Object& Land, int32 Spline, int32 Point, const Vector3d& ScopePoint)
{
	if (!Document || !Land.HasLandscape() || Spline < 0 || Spline >= static_cast<int32>(Land.GetLandscape().GetSplines().size()))
	{
		return false;
	}
	std::vector<LandscapeSpline> Splines = Land.GetLandscape().GetSplines();
	if (Point < 0 || Point >= static_cast<int32>(Splines[static_cast<SIZE_T>(Spline)].points.size()))
	{
		return false;
	}
	Vector3d At;
	ToLocal(Land).Transform(At, ScopePoint);
	// The point, and the others of its junction.
	const int Joint = Splines[static_cast<SIZE_T>(Spline)].points[static_cast<SIZE_T>(Point)].joint;
	for (int32 S = 0; S < static_cast<int32>(Splines.size()); ++S)
	{
		for (int32 P = 0; P < static_cast<int32>(Splines[static_cast<SIZE_T>(S)].points.size()); ++P)
		{
			LandscapeSplinePoint& Other = Splines[static_cast<SIZE_T>(S)].points[static_cast<SIZE_T>(P)];
			if ((S == Spline && P == Point) || (Joint >= 0 && Other.joint == Joint))
			{
				Other.position = At;
			}
		}
	}
	Document->SetLandscape(Land, Land.GetLandscape().WithSplines(std::move(Splines)), "Move Spline Point");
	return true;
}

bool FMapLandscapeMode::ConnectSplinePoint(Object& Land, int32 Spline, int32 Point)
{
	if (!Document || !Land.HasLandscape() || Spline < 0 || Spline >= static_cast<int32>(Land.GetLandscape().GetSplines().size()))
	{
		return false;
	}
	std::vector<LandscapeSpline> Splines = Land.GetLandscape().GetSplines();
	if (Point < 0 || Point >= static_cast<int32>(Splines[static_cast<SIZE_T>(Spline)].points.size()))
	{
		return false;
	}
	const bool bFromSelected = SplineLand == &Land && HasSelectedSplinePoint();
	if (bFromSelected && SelectedSpline == Spline && SelectedPoint == Point)
	{
		return false;
	}
	// A junction id none of the points has yet.
	const auto NewJoint = [&Splines]()
	{
		int Highest = -1;
		for (const LandscapeSpline& Each : Splines)
		{
			for (const LandscapeSplinePoint& Other : Each.points)
			{
				Highest = FMath::Max(Highest, Other.joint);
			}
		}
		return Highest + 1;
	};
	// The target's junction, or a new one (the selected point is somewhere
	// else: its own junction stays its own).
	int Joint = Splines[static_cast<SIZE_T>(Spline)].points[static_cast<SIZE_T>(Point)].joint;
	if (Joint < 0)
	{
		Joint = NewJoint();
	}
	LandscapeSplinePoint& Target = Splines[static_cast<SIZE_T>(Spline)].points[static_cast<SIZE_T>(Point)];
	Target.joint = Joint;
	LandscapeSplinePoint Joined = Target;
	Joined.autoTangent = true;
	if (!bFromSelected)
	{
		// A branch from the point: a spline of its own, starting there.
		LandscapeSpline Branch;
		Branch.points.push_back(Joined);
		Splines.push_back(Branch);
		SplineLand = &Land;
		SelectedSpline = static_cast<int32>(Splines.size()) - 1;
		SelectedPoint = 0;
	}
	else
	{
		LandscapeSpline& From = Splines[static_cast<SIZE_T>(SelectedSpline)];
		LandscapeSplinePoint& Start = From.points[static_cast<SIZE_T>(SelectedPoint)];
		const bool bAtEnd = !From.closed && SelectedPoint == static_cast<int32>(From.points.size()) - 1;
		const bool bAtStart = !From.closed && SelectedPoint == 0 && From.points.size() > 1;
		if (bAtEnd || bAtStart)
		{
			// The spline goes on to the target.
			Joined.width = Start.width;
			Joined.falloff = Start.falloff;
			const int32 At = bAtEnd ? static_cast<int32>(From.points.size()) : 0;
			From.points.insert(From.points.begin() + At, Joined);
			SelectedPoint = At;
		}
		else
		{
			// From its middle: a new spline between the two, each end joined
			// to its point (the start in the selected point's own junction).
			if (Start.joint < 0)
			{
				Start.joint = NewJoint();
			}
			LandscapeSplinePoint Begin = Start;
			Begin.autoTangent = true;
			LandscapeSpline Link;
			Link.layer = From.layer;
			Link.mesh = From.mesh;
			Link.points = { Begin, Joined };
			Splines.push_back(Link);
			SelectedSpline = static_cast<int32>(Splines.size()) - 1;
			SelectedPoint = 1;
		}
	}
	Document->SetLandscape(Land, Land.GetLandscape().WithSplines(std::move(Splines)), "Connect Spline Points");
	return true;
}

bool FMapLandscapeMode::SetSplineTangent(Object& Land, int32 Spline, int32 Point, const Vector3d& Tangent)
{
	if (!Land.HasLandscape() || Spline < 0 || Spline >= static_cast<int32>(Land.GetLandscape().GetSplines().size()))
	{
		return false;
	}
	LandscapeSpline Changed = Land.GetLandscape().GetSplines()[static_cast<SIZE_T>(Spline)];
	if (Point < 0 || Point >= static_cast<int32>(Changed.points.size()))
	{
		return false;
	}
	Changed.points[static_cast<SIZE_T>(Point)].autoTangent = false;
	Changed.points[static_cast<SIZE_T>(Point)].tangent = Tangent;
	return SetSpline(Land, Spline, Changed, "Change Spline Tangent");
}

bool FMapLandscapeMode::SetSplineAutoTangent(Object& Land, int32 Spline, int32 Point)
{
	if (!Land.HasLandscape() || Spline < 0 || Spline >= static_cast<int32>(Land.GetLandscape().GetSplines().size()))
	{
		return false;
	}
	LandscapeSpline Changed = Land.GetLandscape().GetSplines()[static_cast<SIZE_T>(Spline)];
	if (Point < 0 || Point >= static_cast<int32>(Changed.points.size()) || Changed.points[static_cast<SIZE_T>(Point)].autoTangent)
	{
		return false;
	}
	Changed.points[static_cast<SIZE_T>(Point)].autoTangent = true;
	return SetSpline(Land, Spline, Changed, "Change Spline Tangent");
}

bool FMapLandscapeMode::BreakSelectedJunction()
{
	if (!HasSelectedSplinePoint() || GetSelectedSpline()->points[static_cast<SIZE_T>(SelectedPoint)].joint < 0)
	{
		return false;
	}
	LandscapeSpline Changed = *GetSelectedSpline();
	Changed.points[static_cast<SIZE_T>(SelectedPoint)].joint = -1;
	return SetSpline(*SplineLand, SelectedSpline, Changed, "Break Spline Junction");
}

bool FMapLandscapeMode::GetTangentHandles(Vector3d& OutBefore, Vector3d& OutAfter) const
{
	if (!HasSelectedSplinePoint() || GetSelectedSpline()->points.size() < 2)
	{
		return false;
	}
	const LandscapeSpline& Spline = *GetSelectedSpline();
	const Vector3d& At = Spline.points[static_cast<SIZE_T>(SelectedPoint)].position;
	Vector3d Third = Spline.GetTangent(SelectedPoint);
	Third.MultVector(1.0f / 3.0f);
	Vector3d Before(At), After(At);
	Before.SubVector(Third);
	After.AddVector(Third);
	const Matrix44 Forward = ToScope(*SplineLand);
	Forward.Transform(OutBefore, Before);
	Forward.Transform(OutAfter, After);
	return true;
}

bool FMapLandscapeMode::DeleteSelectedSplinePoint()
{
	if (!HasSelectedSplinePoint())
	{
		return false;
	}
	std::vector<LandscapeSpline> Splines = SplineLand->GetLandscape().GetSplines();
	LandscapeSpline& Spline = Splines[static_cast<SIZE_T>(SelectedSpline)];
	const int Joint = Spline.points[static_cast<SIZE_T>(SelectedPoint)].joint;
	Spline.points.erase(Spline.points.begin() + SelectedPoint);
	if (Joint >= 0)
	{
		// A junction of one left is none.
		LandscapeSplinePoint* Last = nullptr;
		int Count = 0;
		for (LandscapeSpline& Each : Splines)
		{
			for (LandscapeSplinePoint& Other : Each.points)
			{
				if (Other.joint == Joint)
				{
					Last = &Other;
					++Count;
				}
			}
		}
		if (Count == 1)
		{
			Last->joint = -1;
		}
	}
	if (Spline.points.empty())
	{
		Splines.erase(Splines.begin() + SelectedSpline);
		SelectedSpline = -1;
		SelectedPoint = -1;
	}
	else
	{
		SelectedPoint = FMath::Min(SelectedPoint, static_cast<int32>(Spline.points.size()) - 1);
	}
	Document->SetLandscape(*SplineLand, SplineLand->GetLandscape().WithSplines(std::move(Splines)), "Delete Spline Point");
	return true;
}

bool FMapLandscapeMode::DeleteSelectedSpline()
{
	if (!GetSelectedSpline())
	{
		return false;
	}
	std::vector<LandscapeSpline> Splines = SplineLand->GetLandscape().GetSplines();
	Splines.erase(Splines.begin() + SelectedSpline);
	SelectedSpline = -1;
	SelectedPoint = -1;
	Document->SetLandscape(*SplineLand, SplineLand->GetLandscape().WithSplines(std::move(Splines)), "Delete Spline");
	return true;
}

bool FMapLandscapeMode::ApplySplines(Object& Land)
{
	if (!Document || !Land.HasLandscape())
	{
		return false;
	}
	std::shared_ptr<const Landscape> Applied = Land.GetLandscape().WithSplinesApplied();
	if (!Applied)
	{
		return false;
	}
	const std::shared_ptr<const Landscape> Before = Land.GetLandscapePtr();
	const bool bOwn = !Document->IsInTransaction();
	if (bOwn)
	{
		Document->BeginTransaction("Apply Splines to Landscape");
	}
	Document->SetLandscape(Land, std::move(Applied), "Apply Splines to Landscape");
	SnapFoliageToLandscape(*Document, Land, *Before);
	if (bOwn)
	{
		Document->EndTransaction();
	}
	return true;
}

bool FMapLandscapeMode::PickSplinePoint(const FMapViewportClient& View, FVector2 Local, float Pixels, Object*& OutLand, int32& OutSpline,
	int32& OutPoint) const
{
	if (!Document)
	{
		return false;
	}
	float Best = Pixels * Pixels;
	bool bFound = false;
	for (Object* Obj : Document->GetObjects())
	{
		if (!Obj->HasLandscape())
		{
			continue;
		}
		const Matrix44 Forward = ToScope(*Obj);
		const std::vector<LandscapeSpline>& Splines = Obj->GetLandscape().GetSplines();
		for (int32 S = 0; S < static_cast<int32>(Splines.size()); ++S)
		{
			for (int32 P = 0; P < static_cast<int32>(Splines[static_cast<SIZE_T>(S)].points.size()); ++P)
			{
				Vector3d At;
				Forward.Transform(At, Splines[static_cast<SIZE_T>(S)].points[static_cast<SIZE_T>(P)].position);
				FVector2 Screen;
				if (!View.MapToScreen(At, Screen))
				{
					continue;
				}
				const FVector2 Delta = Screen - Local;
				const float D2 = Delta.X * Delta.X + Delta.Y * Delta.Y;
				if (D2 <= Best)
				{
					Best = D2;
					OutLand = Obj;
					OutSpline = S;
					OutPoint = P;
					bFound = true;
				}
			}
		}
	}
	return bFound;
}

bool FMapLandscapeMode::ApplyAutoPaint(Object& Land, int32 Layer)
{
	if (!Document || !Land.HasLandscape() || Layer < 0 || Layer >= Land.GetLandscape().GetNumLayers())
	{
		return false;
	}
	const Landscape& Field = Land.GetLandscape();
	std::vector<LandscapeLayer> Layers = Field.GetLayers();
	const SIZE_T Painted = static_cast<SIZE_T>(Layer);
	const int32 PointsX = Field.GetPointsX();
	for (int32 Y = 0; Y < Field.GetPointsY(); ++Y)
	{
		for (int32 X = 0; X < PointsX; ++X)
		{
			const SIZE_T I = static_cast<SIZE_T>(Y * PointsX + X);
			Layers[Painted].weights[I] = AutoPaint.WeightAt(Field, X, Y);
			GiveWay(Layers, Painted, I);
		}
	}
	const FString Description = FString::Format("Auto Paint {}", Field.GetLayers()[Painted].name);
	Document->SetLandscape(Land, Field.WithLayers(std::move(Layers)), Description);
	return true;
}

bool FMapLandscapeMode::BeginStroke(Object& Land, const Vector3d& ScopePoint, bool bInvertTool)
{
	if (!Document || !Land.HasLandscape())
	{
		return false;
	}
	EndStroke();
	Document->BeginTransaction(FString::Format("Landscape {}", ToString(Tool)));
	StrokeTarget = &Land;
	StrokePoint = ScopePoint;
	bInvert = bInvertTool;
	++NoiseSeed;
	Vector3d Local;
	ToLocal(Land).Transform(Local, ScopePoint);
	FlattenHeight = Land.GetLandscape().SampleHeight(Local.GetX(), Local.GetY());
	ApplyAt(Land, ScopePoint, StrokeStep, bInvert);
	return true;
}

void FMapLandscapeMode::EndStroke()
{
	if (!StrokeTarget)
	{
		return;
	}
	StrokeTarget = nullptr;
	if (Document && Document->IsInTransaction())
	{
		Document->EndTransaction();
	}
}

bool FMapLandscapeMode::UpdateHover(FMapViewportClient& View, FVector2 Local)
{
	HoverView = &View;
	Vector3d Origin, Direction;
	HoverLand = View.GetCursorRay(Local, Origin, Direction) ? TraceLandscape(Origin, Direction, HoverPoint) : nullptr;
	return HoverLand != nullptr;
}

bool FMapLandscapeMode::InputPress(FMapViewportClient& View, FVector2 Local, const FPointerEvent& Event)
{
	// Every left press is the mode's, as in Unreal: off a landscape it does
	// nothing (no box selection), on one it starts a stroke.
	if (Tool == ELandscapeTool::Splines)
	{
		// Ctrl+click adds a point; a click picks one (and a drag moves it).
		bHeld = true;
		Object* Picked = nullptr;
		int32 Spline = -1, Point = -1;
		if (Event.IsControlDown())
		{
			// On another point: a junction (or a branch); else a new point.
			if (PickSplinePoint(View, Local, 10.0f, Picked, Spline, Point) && !(Picked == SplineLand && Spline == SelectedSpline && Point == SelectedPoint))
			{
				ConnectSplinePoint(*Picked, Spline, Point);
			}
			else if (UpdateHover(View, Local))
			{
				AddSplinePoint(*HoverLand, HoverPoint);
			}
			return true;
		}
		// The selected point's tangent handles come first.
		Vector3d Handles[2];
		if (GetTangentHandles(Handles[0], Handles[1]))
		{
			for (int32 Side = 0; Side < 2; ++Side)
			{
				FVector2 Screen;
				if (View.MapToScreen(Handles[Side], Screen) && (Screen - Local).SizeSquared() <= 100.0f)
				{
					DraggingHandle = Side == 0 ? -1 : 1;
					Document->BeginTransaction("Change Spline Tangent");
					return true;
				}
			}
		}
		if (PickSplinePoint(View, Local, 10.0f, Picked, Spline, Point))
		{
			SplineLand = Picked;
			SelectedSpline = Spline;
			SelectedPoint = Point;
			Document->BeginTransaction("Move Spline Point");
			bDraggingPoint = true;
		}
		else
		{
			SelectedSpline = SelectedPoint = -1;
		}
		return true;
	}
	bHeld = true;
	bInvert = Event.IsShiftDown();
	RampLand = nullptr;
	if (UpdateHover(View, Local))
	{
		if (Tool == ELandscapeTool::Ramp)
		{
			RampLand = HoverLand;
			RampFrom = HoverPoint;
		}
		else
		{
			BeginStroke(*HoverLand, HoverPoint, bInvert);
		}
	}
	return true;
}

void FMapLandscapeMode::InputRelease(FMapViewportClient&)
{
	if (bDraggingPoint || DraggingHandle != 0)
	{
		bDraggingPoint = false;
		DraggingHandle = 0;
		if (Document && Document->IsInTransaction())
		{
			Document->EndTransaction();
		}
	}
	bHeld = false;
	EndStroke();
	if (RampLand && HoverLand == RampLand)
	{
		AddRamp(*RampLand, RampFrom, HoverPoint);
	}
	RampLand = nullptr;
}

void FMapLandscapeMode::MouseMove(FMapViewportClient& View, FVector2 Local, const FPointerEvent& Event)
{
	UpdateHover(View, Local);
	if (Tool == ELandscapeTool::Splines)
	{
		// The point dragged over the ground of its landscape.
		if (bDraggingPoint && HoverLand == SplineLand && HasSelectedSplinePoint())
		{
			MoveSplinePoint(*SplineLand, SelectedSpline, SelectedPoint, HoverPoint);
		}
		// A handle: where the cursor's ray passes nearest it, so it moves
		// in the plane facing the view (level in a top view).
		Vector3d Handles[2], Origin, Direction;
		if (DraggingHandle != 0 && GetTangentHandles(Handles[0], Handles[1]) && View.GetCursorRay(Local, Origin, Direction))
		{
			const Vector3d& Was = Handles[DraggingHandle < 0 ? 0 : 1];
			Vector3d ToHandle(Was);
			ToHandle.SubVector(Origin);
			const float Length2 = FMath::Max(Direction.DotVector(Direction), 1e-12f);
			Vector3d Now(Direction);
			Now.MultVector(ToHandle.DotVector(Direction) / Length2).AddVector(Origin);
			Vector3d LocalNow, LocalAt;
			ToLocal(*SplineLand).Transform(LocalNow, Now);
			LocalAt = GetSelectedSpline()->points[static_cast<SIZE_T>(SelectedPoint)].position;
			Vector3d Tangent(LocalNow);
			Tangent.SubVector(LocalAt);
			Tangent.MultVector(3.0f * static_cast<float>(DraggingHandle));
			SetSplineTangent(*SplineLand, SelectedSpline, SelectedPoint, Tangent);
		}
		return;
	}
	bInvert = Event.IsShiftDown();
	// A press that began off the landscape strokes once the cursor is on it.
	if (bHeld && !StrokeTarget && HoverLand && Tool != ELandscapeTool::Ramp)
	{
		BeginStroke(*HoverLand, HoverPoint, bInvert);
	}
	if (bHeld && !RampLand && HoverLand && Tool == ELandscapeTool::Ramp)
	{
		RampLand = HoverLand;
		RampFrom = HoverPoint;
	}
	if (StrokeTarget && HoverLand == StrokeTarget)
	{
		StrokePoint = HoverPoint;
	}
}

void FMapLandscapeMode::MouseLeave(FMapViewportClient& View)
{
	if (HoverView == &View)
	{
		HoverLand = nullptr;
		HoverView = nullptr;
	}
}

void FMapLandscapeMode::Tick(FMapViewportClient&, float DeltaSeconds)
{
	// Only while the cursor is over it, as in Unreal.
	if (StrokeTarget && HoverLand == StrokeTarget)
	{
		ApplyAt(*StrokeTarget, StrokePoint, DeltaSeconds, bInvert);
	}
}

void DrawLandscapeSplines(const FMapDocument& Document, const FMapViewportClient& View, FSlateWindowElementList& Out, float DpiScale,
	const Object* SelectedLand, int32 SelectedSpline, int32 SelectedPoint)
{
	const FIntPoint Pixels = View.GetSize();
	const FPaintGeometry Whole = FPaintGeometry::Make(FVector2::Zero(), Pixels.ToVector2() / DpiScale, DpiScale);
	std::vector<LandscapeSpline::Sample> Samples;
	for (Object* Obj : Document.GetObjects())
	{
		if (!Obj->HasLandscape() || Obj->GetLandscape().GetSplines().empty())
		{
			continue;
		}
		const Matrix44 Forward = ToScope(*Obj);
		const std::vector<LandscapeSpline>& Splines = Obj->GetLandscape().GetSplines();
		for (int32 S = 0; S < static_cast<int32>(Splines.size()); ++S)
		{
			const LandscapeSpline& Spline = Splines[static_cast<SIZE_T>(S)];
			const bool bSelected = Obj == SelectedLand && S == SelectedSpline;
			const FColor Line = bSelected ? FColor(255, 200, 90) : FColor(240, 150, 70, 220);
			// The curve, in runs that break where it leaves the view.
			Spline.SampleCurve(Obj->GetLandscape().GetQuadSize(), Samples);
			TArray<FVector2> Run;
			const auto Flush = [&]
			{
				if (Run.size() > 1)
				{
					Out.AddLines(8, Whole, Run, Line, bSelected ? 2.0f : 1.5f);
				}
				Run.clear();
			};
			for (const LandscapeSpline::Sample& Sample : Samples)
			{
				Vector3d At;
				Forward.Transform(At, Sample.position);
				FVector2 Screen;
				if (View.MapToScreen(At, Screen))
				{
					Run.push_back(Screen);
				}
				else
				{
					Flush();
				}
			}
			Flush();
			// The control points.
			for (int32 P = 0; P < static_cast<int32>(Spline.points.size()); ++P)
			{
				Vector3d At;
				Forward.Transform(At, Spline.points[static_cast<SIZE_T>(P)].position);
				FVector2 Screen;
				if (!View.MapToScreen(At, Screen))
				{
					continue;
				}
				const bool bPoint = bSelected && P == SelectedPoint;
				// A junction: a bigger square, outlined.
				const bool bJoint = Spline.points[static_cast<SIZE_T>(P)].joint >= 0;
				const float Size = bJoint ? 11.0f : (bPoint ? 9.0f : 6.0f);
				Out.AddRoundedBox(9, FPaintGeometry::Make(Screen - FVector2(Size * 0.5f, Size * 0.5f), FVector2(Size, Size), DpiScale),
					bPoint ? FColor(255, 255, 255) : Line, bJoint ? 1.5f : Size * 0.5f, bJoint ? FColor(255, 255, 255) : FColor(30, 30, 30), bJoint ? 1.5f : 1.0f);
			}
		}
	}
}

void FMapLandscapeMode::DrawOverlay(const FMapViewportClient& View, FSlateWindowElementList& Out, float DpiScale) const
{
	// Splines: the selected point (drawn for every view by the client, the
	// selection here); no brush ring.
	if (Tool == ELandscapeTool::Splines)
	{
		if (Document && HasSelectedSplinePoint())
		{
			const LandscapeSplinePoint& Point = GetSelectedSpline()->points[static_cast<SIZE_T>(SelectedPoint)];
			Vector3d At;
			ToScope(*SplineLand).Transform(At, Point.position);
			FVector2 Screen;
			if (View.MapToScreen(At, Screen))
			{
				Out.AddRoundedBox(10, FPaintGeometry::Make(Screen - FVector2(7.0f, 7.0f), FVector2(14.0f, 14.0f), DpiScale), FColor(0, 0, 0, 0), 7.0f,
					FColor(255, 255, 255), 2.0f);
				// Its tangent handles, on a line through it.
				Vector3d Handles[2];
				FVector2 Ends[2];
				if (GetTangentHandles(Handles[0], Handles[1]) && View.MapToScreen(Handles[0], Ends[0]) && View.MapToScreen(Handles[1], Ends[1]))
				{
					const FIntPoint Pixels = View.GetSize();
					const FPaintGeometry Whole = FPaintGeometry::Make(FVector2::Zero(), Pixels.ToVector2() / DpiScale, DpiScale);
					Out.AddLines(10, Whole, { Ends[0], Screen, Ends[1] }, FColor(120, 210, 255), 1.5f);
					for (const FVector2& End : Ends)
					{
						Out.AddRoundedBox(11, FPaintGeometry::Make(End - FVector2(4.5f, 4.5f), FVector2(9.0f, 9.0f), DpiScale), FColor(120, 210, 255), 1.5f,
							FColor(20, 40, 60), 1.0f);
					}
				}
			}
		}
		return;
	}
	if (!Document || HoverView != &View || !IsInScope(*Document, HoverLand) || !HoverLand->HasLandscape())
	{
		return;
	}
	const Landscape& Field = HoverLand->GetLandscape();
	const Matrix44 Forward = ToScope(*HoverLand);
	Vector3d Center;
	ToLocal(*HoverLand).Transform(Center, HoverPoint);
	const float Radius = LocalRadius(*HoverLand, Brush.Radius);
	const FIntPoint Pixels = View.GetSize();
	const FPaintGeometry Whole = FPaintGeometry::Make(FVector2::Zero(), Pixels.ToVector2() / DpiScale, DpiScale);

	// A ring on the ground at the radius, and one where the falloff begins.
	const auto Ring = [&](float R, FColor Color, float Thickness)
	{
		TArray<FVector2> Run;
		const auto Flush = [&]
		{
			if (Run.size() > 1)
			{
				Out.AddLines(8, Whole, Run, Color, Thickness);
			}
			Run.clear();
		};
		constexpr int32 Steps = 64;
		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			const float Angle = static_cast<float>(Step) / Steps * 6.2831853f;
			const float X = Center.GetX() + std::cos(Angle) * R;
			const float Y = Center.GetY() + std::sin(Angle) * R;
			Vector3d Point;
			Forward.Transform(Point, Vector3d(X, Y, Field.SampleHeight(X, Y)));
			FVector2 Screen;
			if (View.MapToScreen(Point, Screen))
			{
				Run.push_back(Screen);
			}
			else
			{
				Flush();
			}
		}
		Flush();
	};
	const bool bLowering = bInvert && (Tool == ELandscapeTool::Sculpt || Tool == ELandscapeTool::Paint);
	FColor RingColor = bLowering ? FColor(255, 150, 90) : FColor(110, 190, 255);
	if (Tool == ELandscapeTool::Paint && !bInvert && SelectedLayer >= 0 && SelectedLayer < Field.GetNumLayers())
	{
		const LandscapeLayer& Layer = Field.GetLayers()[static_cast<SIZE_T>(SelectedLayer)];
		RingColor = FColor(Layer.r, Layer.g, Layer.b);
	}
	Ring(Radius, RingColor, 2.0f);
	if (Brush.Falloff > 0.05f && Brush.Falloff < 0.95f)
	{
		Ring(Radius * (1.0f - Brush.Falloff), FColor(110, 190, 255, 130), 1.0f);
	}
	FVector2 Dot;
	if (View.MapToScreen(HoverPoint, Dot))
	{
		Out.AddRoundedBox(8, FPaintGeometry::Make(Dot - FVector2(2.5f, 2.5f), FVector2(5.0f, 5.0f), DpiScale), FColor(110, 190, 255), 2.5f);
	}
	// The ramp being dragged out: its middle and its two sides on the ground.
	if (RampLand == HoverLand)
	{
		Vector3d From;
		ToLocal(*HoverLand).Transform(From, RampFrom);
		const float Lx = Center.GetX() - From.GetX();
		const float Ly = Center.GetY() - From.GetY();
		const float Length = std::sqrt(Lx * Lx + Ly * Ly);
		if (Length > 1.0f)
		{
			const float Nx = -Ly / Length * Radius;
			const float Ny = Lx / Length * Radius;
			for (const float Side : { 0.0f, 1.0f, -1.0f })
			{
				TArray<FVector2> Run;
				constexpr int32 Steps = 32;
				for (int32 Step = 0; Step <= Steps; ++Step)
				{
					const float T = static_cast<float>(Step) / Steps;
					const float X = From.GetX() + Lx * T + Nx * Side;
					const float Y = From.GetY() + Ly * T + Ny * Side;
					Vector3d Point;
					Forward.Transform(Point, Vector3d(X, Y, Field.SampleHeight(X, Y)));
					FVector2 Screen;
					if (View.MapToScreen(Point, Screen))
					{
						Run.push_back(Screen);
					}
				}
				if (Run.size() > 1)
				{
					Out.AddLines(8, Whole, Run, FColor(110, 190, 255, Side == 0.0f ? 255 : 160), Side == 0.0f ? 2.0f : 1.0f);
				}
			}
		}
	}
}

FString FMapLandscapeMode::GetHint() const
{
	switch (Tool)
	{
	case ELandscapeTool::Sculpt: return "Landscape: Sculpt (Shift lowers)";
	case ELandscapeTool::Flatten:
		return bFlattenToTarget ? FString::Format("Landscape: Flatten to {:.0f}", FlattenTarget) : FString("Landscape: Flatten");
	case ELandscapeTool::Ramp: return "Landscape: Ramp (drag from the bottom to the top)";
	case ELandscapeTool::Splines: return "Landscape: Splines (Ctrl+click adds a point or joins one, drag moves one or a tangent handle, Delete removes it)";
	case ELandscapeTool::Paint:
	{
		const Object* Land = GetTargetLandscape();
		if (!Land || SelectedLayer < 0 || SelectedLayer >= Land->GetLandscape().GetNumLayers())
		{
			return "Landscape: Paint (add a paint layer first)";
		}
		return FString::Format("Landscape: Paint {} (Shift erases)", Land->GetLandscape().GetLayers()[static_cast<SIZE_T>(SelectedLayer)].name);
	}
	default: return FString::Format("Landscape: {}", ToString(Tool));
	}
}

FDelegateHandle RegisterLandscapeViewportOverlay(TFunction<const FMapDocument*()> GetDocument)
{
	return MapSceneOverlays().AddLambda([GetDocument = std::move(GetDocument)](const FMapDocument& Document, const FMapViewportClient& View,
		FSlateWindowElementList& Out, float DpiScale)
	{
		if (!GetDocument || GetDocument() == &Document) DrawLandscapeSplines(Document, View, Out, DpiScale);
	});
}
