// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "World/GameWorld.h"
#include "World/WorldCompositionLimits.h"
#include "Internationalization/TextLocalization.h"
#include "Landscape/Landscape.h"
#include <cmath>
#include <filesystem>
#include <limits>

namespace
{
	constexpr const char* LevelSchema = "Genesis.World";
	constexpr const char* CompositionSchema = "Genesis.WorldComposition";
	constexpr const char* PlayerStartClass = "info_player_start";

	Matrix44 Translation(double X, double Y, double Z)
	{
		Matrix44 M;
		M.SetIdentity();
		M.SetTranslation(Vector3d(static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z)));
		return M;
	}
	// The inverse of an affine transform; false when it is singular.
	bool InvertAffine(const Matrix44& M, Matrix44& Out)
	{
		const double A = M.GetMatrix(0, 0), B = M.GetMatrix(0, 1), C = M.GetMatrix(0, 2);
		const double D = M.GetMatrix(1, 0), E = M.GetMatrix(1, 1), F = M.GetMatrix(1, 2);
		const double G = M.GetMatrix(2, 0), H = M.GetMatrix(2, 1), I = M.GetMatrix(2, 2);
		const double Det = A * (E * I - F * H) - B * (D * I - F * G) + C * (D * H - E * G);
		if (!std::isfinite(Det) || std::abs(Det) < 1e-12)
			return false;
		const double R[3][3] = {{(E * I - F * H) / Det, (C * H - B * I) / Det, (B * F - C * E) / Det},
		    {(F * G - D * I) / Det, (A * I - C * G) / Det, (C * D - A * F) / Det},
		    {(D * H - E * G) / Det, (B * G - A * H) / Det, (A * E - B * D) / Det}};
		Out.SetIdentity();
		for (int Row = 0; Row < 3; ++Row)
		{
			double T = 0;
			for (int Column = 0; Column < 3; ++Column)
			{
				Out.SetMatrix(Row, Column, static_cast<float>(R[Row][Column]));
				T -= R[Row][Column] * M.GetMatrix(Column, 3);
			}
			Out.SetMatrix(Row, 3, static_cast<float>(T));
		}
		return true;
	}
} // namespace

TUniquePtr<FGameWorld> FGameWorld::Open(
    std::shared_ptr<FAssetRegistry> Registry, const FAssetReference& World, FString& Error)
{
	if (!Registry)
	{
		Error = GENESIS_TEXT("A game world needs an asset registry.");
		return {};
	}
	const FAssetData* Data = Registry->Resolve(World, Error);
	if (!Data)
		return {};
	TUniquePtr<FGameWorld> Result(new FGameWorld());
	Result->Reference = Data->GetReference();
	FAssetPackage Package;
	if (Data->Metadata.Schema == CompositionSchema)
	{
		if (!Registry->Load(Result->Reference, Package, Error, CompositionSchema) ||
		    !FWorldComposition::FromPackage(Package, Result->Composition, Error))
			return {};
		if (Result->Composition.GetTiles().empty())
		{
			Error = GENESIS_TEXT("The world composition has no regions.");
			return {};
		}
		Result->bComposition = true;
		Result->Source = MakeUnique<FLocalWorldTileSource>(*Registry);
		Result->Streaming = MakeUnique<FWorldCompositionStreaming>(*Result->Source);
	}
	else if (Data->Metadata.Schema == LevelSchema && Data->Metadata.Kind == EAssetPackageKind::Map)
	{
		Result->Registry = Registry;
		if (!Registry->Load(Result->Reference, Package, Error, LevelSchema) ||
		    !Result->StartLevel(
		        Package, FString::FromWide(std::filesystem::path(Data->FilePath.ToWide()).stem().wstring()), Error))
			return {};
	}
	else
	{
		Error = GENESIS_TEXT("The startup world must be a level or a world composition.");
		return {};
	}
	Result->Registry = std::move(Registry);
	Error.Empty();
	return Result;
}

TUniquePtr<FGameWorld> FGameWorld::OpenLevel(
    std::shared_ptr<FAssetRegistry> Registry, const FAssetPackage& Level, const FString& Name, FString& Error)
{
	if (!Registry || Level.Schema != LevelSchema || Level.Kind != EAssetPackageKind::Map ||
	    !FAssetId::IsValid(Level.Id))
	{
		Error = GENESIS_TEXT("Only a saved-identity world level can be played.");
		return {};
	}
	TUniquePtr<FGameWorld> Result(new FGameWorld());
	Result->Reference = {Level.Id, {}};
	Result->Registry = Registry;
	if (!Result->StartLevel(Level, Name, Error))
		return {};
	Error.Empty();
	return Result;
}

bool FGameWorld::StartLevel(const FAssetPackage& Package, const FString& Name, FString& Error)
{
	std::shared_ptr<FWorldSceneSnapshot> Snapshot = std::make_shared<FWorldSceneSnapshot>();
	if (!FWorldSceneSnapshot::Load(Package, Registry.get(), *Snapshot, Error))
		return false;
	Level = std::move(Snapshot);
	// A level is one region centred on the world origin: a 2x2-cell tile
	// around 0, so region space and absolute space are the same.
	FWorldCompositionTile Tile =
	    FWorldCompositionTile::New(Name.IsEmpty() ? FString("Level") : Name, Reference, -1, -1);
	Tile.Id = Reference.Id;
	Tile.SizeX = Tile.SizeY = 2;
	if (!Composition.SetCellSize(WorldCompositionLimits::MaxCellSize, Error) || !Composition.AddTile(Tile, Error))
		return false;
	bOriginSet = true; // a level never rebases
	return true;
}

bool FGameWorld::SetStreamingSettings(const FWorldCompositionStreamingSettings& Settings, FString& Error)
{
	if (!Streaming)
	{
		Error.Empty();
		return true; // a level is always loaded
	}
	return Streaming->SetSettings(Settings, Error);
}

bool FGameWorld::RegionCentre(const FString& RegionId, double& X, double& Y) const
{
	const FWorldCompositionTile* Tile = Composition.FindTile(RegionId);
	if (!Tile)
		return false;
	const FWorldTileBounds Bounds = Composition.GetTileBounds(*Tile);
	X = (Bounds.MinX + Bounds.MaxX) * 0.5;
	Y = (Bounds.MinY + Bounds.MaxY) * 0.5;
	return true;
}

bool FGameWorld::ToAbsolute(const FWorldLocation& Location, double& X, double& Y, double& Z) const
{
	double CX = 0, CY = 0;
	if (!RegionCentre(Location.RegionId, CX, CY))
		return false;
	X = CX + Location.Local.GetX();
	Y = CY + Location.Local.GetY();
	Z = Location.Local.GetZ();
	return true;
}

bool FGameWorld::FromAbsolute(double X, double Y, double Z, FWorldLocation& Out) const
{
	const FWorldCompositionTile* Tile = Composition.FindTileAt(X, Y);
	if (!Tile)
	{
		double Best = std::numeric_limits<double>::max();
		for (const FWorldCompositionTile& Each : Composition.GetTiles())
		{
			const double Distance = Composition.GetTileBounds(Each).GetDistance(X, Y);
			if (Distance < Best)
			{
				Best = Distance;
				Tile = &Each;
			}
		}
	}
	double CX = 0, CY = 0;
	if (!Tile || !RegionCentre(Tile->Id, CX, CY))
		return false;
	Out.RegionId = Tile->Id;
	Out.Local = Vector3d(static_cast<float>(X - CX), static_cast<float>(Y - CY), static_cast<float>(Z));
	return true;
}

bool FGameWorld::ToRender(const FWorldLocation& Location, Vector3d& Out) const
{
	double X = 0, Y = 0, Z = 0;
	if (!ToAbsolute(Location, X, Y, Z))
		return false;
	const double Cell = Composition.GetCellSize();
	Out =
	    Vector3d(static_cast<float>(X - OriginX * Cell), static_cast<float>(Y - OriginY * Cell), static_cast<float>(Z));
	return true;
}

FWorldLocation FGameWorld::Normalize(const FWorldLocation& Location) const
{
	double X = 0, Y = 0, Z = 0;
	if (!ToAbsolute(Location, X, Y, Z))
		return Location;
	const FWorldCompositionTile* Tile = Composition.FindTileAt(X, Y);
	if (!Tile || Tile->Id == Location.RegionId)
		return Location; // still inside its region, or over the void between regions
	FWorldLocation Moved;
	return FromAbsolute(X, Y, Z, Moved) ? Moved : Location;
}

void FGameWorld::Update(const FWorldLocation& Viewer, FGameWorldChanges* Changes)
{
	if (Changes)
		*Changes = {};
	double X = 0, Y = 0, Z = 0;
	if (!bComposition || !ToAbsolute(Viewer, X, Y, Z))
		return;
	const double Cell = Composition.GetCellSize();
	const double Reach = RebaseCells * Cell;
	if (!bOriginSet || std::abs(X - (OriginX + 0.5) * Cell) > Reach || std::abs(Y - (OriginY + 0.5) * Cell) > Reach)
	{
		OriginX = static_cast<int64>(std::floor(X / Cell));
		OriginY = static_cast<int64>(std::floor(Y / Cell));
		if (Changes && bOriginSet)
			Changes->bRebased = true;
		bOriginSet = true;
	}
	FWorldCompositionStreamingChanges Streamed;
	Streaming->Update(Composition, Vector3d(static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z)),
	    Changes ? &Streamed : nullptr);
	if (Changes)
	{
		Changes->Loaded = std::move(Streamed.Loaded);
		Changes->Unloaded = std::move(Streamed.Unloaded);
		Changes->Failed = std::move(Streamed.Failed);
	}
}

void FGameWorld::GetLoadedRegions(TArray<FGameRegion>& Out) const
{
	Out.clear();
	const double Cell = Composition.GetCellSize();
	const TFunction<void(const FWorldCompositionTile&, std::shared_ptr<const FWorldSceneSnapshot>)> Add =
	    [&](const FWorldCompositionTile& Tile, std::shared_ptr<const FWorldSceneSnapshot> Scene)
	{
		double CX = 0, CY = 0;
		RegionCentre(Tile.Id, CX, CY);
		Out.push_back({Tile.Id, Tile.Name, std::move(Scene), Translation(CX - OriginX * Cell, CY - OriginY * Cell, 0)});
	};
	if (!bComposition)
	{
		Add(Composition.GetTiles()[0], Level);
		return;
	}
	for (const FStreamedWorldTile& Streamed : Streaming->GetTiles())
		if (Streamed.State == EWorldTileStreamingState::Loaded)
			if (const FWorldCompositionTile* Tile = Composition.FindTile(Streamed.TileId))
				Add(*Tile, Streamed.Snapshot);
}

bool FGameWorld::IsLoaded(const FString& RegionId) const
{
	if (!bComposition)
		return Composition.FindTile(RegionId) != nullptr;
	return Streaming->IsLoaded(RegionId);
}

bool FGameWorld::SampleGround(const FWorldLocation& Location, float& Height) const
{
	double X = 0, Y = 0, Z = 0;
	if (!ToAbsolute(Location, X, Y, Z))
		return false;
	TArray<FGameRegion> Regions;
	GetLoadedRegions(Regions);
	bool bFound = false;
	for (const FGameRegion& Region : Regions)
	{
		double CX = 0, CY = 0;
		if (!Region.Scene || !RegionCentre(Region.Id, CX, CY))
			continue;
		const Vector3d InRegion(static_cast<float>(X - CX), static_cast<float>(Y - CY), static_cast<float>(Z));
		for (const FWorldSceneItem& Item : Region.Scene->GetItems())
		{
			Matrix44 ToTerrain;
			if (!Item.Terrain || !InvertAffine(Item.ToWorld, ToTerrain))
				continue;
			Vector3d Point;
			ToTerrain.Transform(Point, InRegion);
			const Landscape& Terrain = *Item.Terrain;
			if (std::abs(Point.GetX()) > Terrain.GetSizeX() * 0.5f ||
			    std::abs(Point.GetY()) > Terrain.GetSizeY() * 0.5f)
				continue;
			Vector3d Ground;
			Item.ToWorld.Transform(
			    Ground, Vector3d(Point.GetX(), Point.GetY(), Terrain.SampleHeight(Point.GetX(), Point.GetY())));
			if (!bFound || Ground.GetZ() > Height)
				Height = Ground.GetZ();
			bFound = true;
		}
	}
	return bFound;
}

bool FGameWorld::FindPlayerStart(FWorldLocation& Out) const
{
	TArray<FGameRegion> Regions;
	GetLoadedRegions(Regions);
	for (const FGameRegion& Region : Regions)
	{
		if (!Region.Scene)
			continue;
		const FWorld& Scene = Region.Scene->GetScene();
		for (const FActor& Actor : Scene.GetActors())
			for (const FActorComponent& Component : Actor.Components)
			{
				if (Component.Type != "Genesis.LegacyEntity" ||
				    Component.Properties.Get("Data").Get("Class").AsString() != PlayerStartClass)
					continue;
				Matrix44 ToWorld;
				FString Error;
				if (!Scene.GetWorldTransform(Actor.Id, ToWorld, Error))
					continue;
				Out.RegionId = Region.Id;
				ToWorld.Transform(Out.Local, Vector3d());
				return true;
			}
	}
	const FWorldCompositionTile& First = Composition.GetTiles()[0];
	Out.RegionId = First.Id;
	Out.Local = Vector3d();
	float Ground = 0;
	if (SampleGround(Out, Ground))
		Out.Local = Vector3d(0, 0, Ground);
	return true;
}
