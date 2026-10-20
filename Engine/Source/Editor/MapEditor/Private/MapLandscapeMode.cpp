// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "MapLandscapeMode.h"

#include "MapEditorPrivate.h"
#include "MapLandscapeTextures.h"

#include <algorithm>
#include <cmath>

using namespace MapEditorPrivate;

const char* ToString(ELandscapeTool Tool)
{
	switch (Tool)
	{
	case ELandscapeTool::Sculpt: return "Sculpt";
	case ELandscapeTool::Smooth: return "Smooth";
	case ELandscapeTool::Flatten: return "Flatten";
	case ELandscapeTool::Noise: return "Noise";
	case ELandscapeTool::Paint: return "Paint";
	}
	return "?";
}

namespace
{
	constexpr float SculptSpeed = 256.0f; // units a second at full strength
	constexpr float NoiseSpeed = 128.0f;
	constexpr float BlendRate = 8.0f;     // Smooth and Flatten, a second at full strength
	constexpr float StrokeStep = 1.0f / 60.0f; // what a click alone does
	constexpr float PaintRate = 4.0f;          // weight a second at full strength

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
	Document->SetLandscape(Land, std::move(Updated), "Import Heightmap");
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
		if (!Obj->GetMesh().HitMesh(Line(LocalOrigin, LocalDirection), 0.0f, 1.0e7f, Depth))
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
				Own = FMath::Clamp(Own + Sign * Strength * PaintRate * DeltaSeconds * W, 0.0f, 1.0f);
				if (Own == Before)
				{
					continue;
				}
				bPainted = true;
				float Others = 0.0f;
				for (SIZE_T L = 0; L < Layers.size(); ++L)
				{
					Others += L == Painted ? 0.0f : Layers[L].weights[I];
				}
				const float Room = 1.0f - Own;
				if (Others > Room && Others > 0.0f)
				{
					const float Keep = Room / Others;
					for (SIZE_T L = 0; L < Layers.size(); ++L)
					{
						if (L != Painted)
						{
							Layers[L].weights[I] *= Keep;
						}
					}
				}
			}
		}
		if (!bPainted)
		{
			return false;
		}
		Document->SetLandscape(Land, Field.WithLayers(std::move(Layers)), "Landscape Paint");
		return true;
	}
	const float Blend = FMath::Min(1.0f, Strength * BlendRate * DeltaSeconds);
	const std::vector<float>& Old = Field.GetHeights();
	std::vector<float> Heights = Old;
	const int32 Stride = Field.GetPointsX();
	bool bChanged = false;
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
				H += (FlattenHeight - H) * Blend * W;
				break;
			case ELandscapeTool::Noise:
				H += Hash(X, Y, NoiseSeed) * Strength * NoiseSpeed * DeltaSeconds * W;
				break;
			}
			bChanged |= H != Before;
		}
	}
	if (!bChanged)
	{
		return false;
	}
	Document->SetLandscape(Land, Field.WithHeights(std::move(Heights)));
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
	bHeld = true;
	bInvert = Event.IsShiftDown();
	if (UpdateHover(View, Local))
	{
		BeginStroke(*HoverLand, HoverPoint, bInvert);
	}
	return true;
}

void FMapLandscapeMode::InputRelease(FMapViewportClient&)
{
	bHeld = false;
	EndStroke();
}

void FMapLandscapeMode::MouseMove(FMapViewportClient& View, FVector2 Local, const FPointerEvent& Event)
{
	UpdateHover(View, Local);
	bInvert = Event.IsShiftDown();
	// A press that began off the landscape strokes once the cursor is on it.
	if (bHeld && !StrokeTarget && HoverLand)
	{
		BeginStroke(*HoverLand, HoverPoint, bInvert);
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

void FMapLandscapeMode::DrawOverlay(const FMapViewportClient& View, FSlateWindowElementList& Out, float DpiScale) const
{
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
}

FString FMapLandscapeMode::GetHint() const
{
	switch (Tool)
	{
	case ELandscapeTool::Sculpt: return "Landscape: Sculpt (Shift lowers)";
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
