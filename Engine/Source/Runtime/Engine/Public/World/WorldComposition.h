// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "AssetRegistry.h"
#include "Geometry/Math3D.h"

// One region of a composed world: an independently saved Genesis.World map,
// usually holding its own landscape, registered on the world's grid. The map's
// local origin sits at the centre of the tile's footprint.
struct FWorldCompositionTile
{
	FString Id;   // permanent identity, retained when the tile moves or is renamed
	FString Name; // unique within the composition
	FAssetReference Map;
	int32 X = 0, Y = 0;         // grid cell of the footprint's -X/-Y corner
	int32 SizeX = 1, SizeY = 1; // footprint in cells
	// Empty when this process loads the tile. Reserved for region servers;
	// the local tile source rejects hosted tiles.
	FString Host;
	static FWorldCompositionTile New(const FString& Name, const FAssetReference& Map, int32 X, int32 Y);
	bool Overlaps(const FWorldCompositionTile& Other) const;
	// Shares part of an edge, not only a corner, without overlapping.
	bool IsAdjacent(const FWorldCompositionTile& Other) const;
	FJsonValue ToJson() const;
	static TOptional<FWorldCompositionTile> FromJson(const FJsonValue& Json, FString& Error);
};

struct FWorldTileBounds
{
	double MinX = 0, MinY = 0, MaxX = 0, MaxY = 0;
	// Horizontal distance from a point to the footprint; 0 inside it.
	double GetDistance(double X, double Y) const;
};

// The world's tile registry, as Unreal's world composition: tiles are placed on
// an integer grid of CellSize world units, never overlap, and each map is
// registered once. Mutations validate before committing; a rejected change
// leaves the composition unchanged.
class FWorldComposition
{
public:
	FWorldComposition();
	double GetCellSize() const
	{
		return CellSize;
	}
	// Rescales every tile's world footprint; rejected if any would leave the world bounds.
	bool SetCellSize(double NewCellSize, FString& Error);
	const TArray<FWorldCompositionTile>& GetTiles() const
	{
		return Tiles;
	}
	// Pointers remain valid until the next successful mutation.
	const FWorldCompositionTile* FindTile(const FString& Id) const;
	const FWorldCompositionTile* FindTileByName(const FString& Name) const;
	const FWorldCompositionTile* FindTileAt(double X, double Y) const;
	// Whether AddTile/UpdateTile would accept the tile, without changing anything.
	bool CanAddTile(const FWorldCompositionTile& Tile, FString& Error) const;
	bool CanUpdateTile(const FWorldCompositionTile& Tile, FString& Error) const;
	bool AddTile(FWorldCompositionTile Tile, FString& Error);
	// Replaces the tile with the same ID: moves, resizes, renames or rehosts it.
	bool UpdateTile(FWorldCompositionTile Tile, FString& Error);
	bool RemoveTile(const FString& Id, FString& Error);
	// Tiles sharing an edge with this one, for boundary matching and handoff.
	void GetNeighbours(const FString& Id, TArray<const FWorldCompositionTile*>& Out) const;
	// Tiles whose footprint lies within Distance of the point, nearest first.
	void GetTilesWithinDistance(double X, double Y, double Distance, TArray<const FWorldCompositionTile*>& Out) const;
	FWorldTileBounds GetTileBounds(const FWorldCompositionTile& Tile) const;
	// Where the tile's map origin lies in the world.
	Vector3d GetTileOrigin(const FWorldCompositionTile& Tile) const;
	FJsonValue ToJson() const;
	static TOptional<FWorldComposition> FromJson(const FJsonValue& Json, FString& Error);
	// Genesis.WorldComposition map packages; dependencies list the tile maps' paths.
	void ToPackage(FAssetPackage& Package) const;
	static bool FromPackage(const FAssetPackage& Package, FWorldComposition& Composition, FString& Error);

private:
	// Checks a tile against every other tile, ignoring the one at Skip.
	bool ValidateTile(const FWorldCompositionTile& Tile, size_t Skip, FString& Error) const;
	void RebuildIndex();
	void IndexTile(size_t TileIndex);
	TArray<FWorldCompositionTile> Tiles;
	TMap<std::string, size_t> ById, ByName, ByMapId, ByMapPath;
	// Tiles touching each MaxTileCells-square bucket of the grid.
	TMap<int64, TArray<size_t>> Buckets;
	double CellSize;
};
