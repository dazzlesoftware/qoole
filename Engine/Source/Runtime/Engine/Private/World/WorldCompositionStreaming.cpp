// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "World/WorldCompositionStreaming.h"
#include "Internationalization/TextLocalization.h"
#include <cmath>

namespace
{
	bool SameReference(const FAssetReference& A, const FAssetReference& B)
	{
		return A.Id == B.Id && A.Path == B.Path;
	}
	Matrix44 TileToWorld(const FWorldComposition& Composition, const FWorldCompositionTile& Tile)
	{
		FTransform3D Transform;
		Transform.Location = Composition.GetTileOrigin(Tile);
		return Transform.ToMatrix();
	}
} // namespace

bool FLocalWorldTileSource::LoadTile(const FWorldCompositionTile& Tile, FWorldSceneSnapshot& Snapshot, FString& Error)
{
	if (!Tile.Host.IsEmpty())
	{
		Error = FTextLocalization::Format(
		    "World tile {} is hosted by a region server, which this process cannot load.", Tile.Name);
		return false;
	}
	FAssetPackage Package;
	if (!Registry.Load(Tile.Map, Package, Error, "Genesis.World"))
		return false;
	return FWorldSceneSnapshot::Load(Package, &Registry, Snapshot, Error);
}

bool FWorldCompositionStreamingSettings::IsValid() const
{
	return std::isfinite(LoadDistance) && std::isfinite(UnloadDistance) && LoadDistance >= 0 &&
	    UnloadDistance >= LoadDistance && UnloadDistance <= WorldCompositionLimits::MaxWorldExtent &&
	    MaxLoadsPerUpdate >= 1 && MaxLoadsPerUpdate <= WorldCompositionLimits::MaxTiles;
}

bool FWorldCompositionStreaming::SetSettings(const FWorldCompositionStreamingSettings& NewSettings, FString& Error)
{
	if (!NewSettings.IsValid())
	{
		Error = GENESIS_TEXT("Invalid world streaming distances or load budget.");
		return false;
	}
	Settings = NewSettings;
	Error.Empty();
	return true;
}

void FWorldCompositionStreaming::Update(
    const FWorldComposition& Composition, const Vector3d& ViewLocation, FWorldCompositionStreamingChanges* Changes)
{
	if (Changes)
		*Changes = {};
	const double X = ViewLocation.GetX(), Y = ViewLocation.GetY();
	if (!std::isfinite(X) || !std::isfinite(Y))
		return;

	// Release tiles that left the composition, changed what they load or fell
	// out of range. Failed tiles are forgotten too, so returning retries them.
	TArray<FStreamedWorldTile> Kept;
	Kept.reserve(Tiles.size());
	for (FStreamedWorldTile& Streamed : Tiles)
	{
		const FWorldCompositionTile* Tile = Composition.FindTile(Streamed.TileId);
		const bool bKeep = Tile && SameReference(Tile->Map, Streamed.Map) && Tile->Host == Streamed.Host &&
		    Composition.GetTileBounds(*Tile).GetDistance(X, Y) <= Settings.UnloadDistance;
		if (!bKeep)
		{
			if (Changes && Streamed.State == EWorldTileStreamingState::Loaded)
				Changes->Unloaded.push_back(Streamed.TileId);
			continue;
		}
		Streamed.ToWorld = TileToWorld(Composition, *Tile);
		Kept.push_back(std::move(Streamed));
	}
	Tiles = std::move(Kept);

	TArray<const FWorldCompositionTile*> Nearby;
	Composition.GetTilesWithinDistance(X, Y, Settings.LoadDistance, Nearby);
	int32 Loads = 0;
	for (const FWorldCompositionTile* Tile : Nearby)
	{
		if (Loads >= Settings.MaxLoadsPerUpdate)
			break;
		if (FindTile(Tile->Id))
			continue;
		++Loads;
		FStreamedWorldTile Streamed;
		Streamed.TileId = Tile->Id;
		Streamed.Map = Tile->Map;
		Streamed.Host = Tile->Host;
		Streamed.ToWorld = TileToWorld(Composition, *Tile);
		std::shared_ptr<FWorldSceneSnapshot> Snapshot = std::make_shared<FWorldSceneSnapshot>();
		if (Source.LoadTile(*Tile, *Snapshot, Streamed.Error))
		{
			Streamed.Snapshot = std::move(Snapshot);
			Streamed.Error.Empty();
			if (Changes)
				Changes->Loaded.push_back(Tile->Id);
		}
		else
		{
			Streamed.State = EWorldTileStreamingState::Failed;
			if (Changes)
				Changes->Failed.push_back(Tile->Id);
		}
		Tiles.push_back(std::move(Streamed));
	}
}

const FStreamedWorldTile* FWorldCompositionStreaming::FindTile(const FString& TileId) const
{
	for (const FStreamedWorldTile& Streamed : Tiles)
		if (Streamed.TileId == TileId)
			return &Streamed;
	return nullptr;
}
bool FWorldCompositionStreaming::IsLoaded(const FString& TileId) const
{
	const FStreamedWorldTile* Streamed = FindTile(TileId);
	return Streamed && Streamed->State == EWorldTileStreamingState::Loaded;
}
void FWorldCompositionStreaming::Retry(const FString& TileId)
{
	Tiles.erase(std::remove_if(Tiles.begin(), Tiles.end(), [&TileId](const FStreamedWorldTile& Streamed)
	{ return Streamed.TileId == TileId && Streamed.State == EWorldTileStreamingState::Failed; }),
	    Tiles.end());
}
void FWorldCompositionStreaming::UnloadAll()
{
	Tiles.clear();
}
