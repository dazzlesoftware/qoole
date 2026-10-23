// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "World/WorldComposition.h"
#include "World/WorldSceneSnapshot.h"
#include "World/WorldCompositionLimits.h"

// Supplies a tile's contents. The local source reads map packages; a region
// server connection implements the same contract for hosted tiles.
class IWorldTileSource
{
public:
	virtual ~IWorldTileSource() = default;
	// On failure Snapshot is unchanged and Error holds a localized diagnostic.
	virtual bool LoadTile(const FWorldCompositionTile& Tile, FWorldSceneSnapshot& Snapshot, FString& Error) = 0;
};

// Loads tile maps through an asset registry in this process.
class FLocalWorldTileSource final : public IWorldTileSource
{
public:
	explicit FLocalWorldTileSource(const FAssetRegistry& InRegistry) : Registry(InRegistry) {}
	bool LoadTile(const FWorldCompositionTile& Tile, FWorldSceneSnapshot& Snapshot, FString& Error) override;

private:
	const FAssetRegistry& Registry;
};

struct FWorldCompositionStreamingSettings
{
	double LoadDistance = WorldCompositionLimits::DefaultLoadDistance;
	// At least LoadDistance, so a viewer near a boundary does not reload tiles.
	double UnloadDistance = WorldCompositionLimits::DefaultUnloadDistance;
	int32 MaxLoadsPerUpdate = WorldCompositionLimits::DefaultMaxLoadsPerUpdate;
	bool IsValid() const;
};

enum class EWorldTileStreamingState
{
	Loaded,
	Failed
};

struct FStreamedWorldTile
{
	FString TileId;
	EWorldTileStreamingState State = EWorldTileStreamingState::Loaded;
	std::shared_ptr<const FWorldSceneSnapshot> Snapshot; // null when failed
	// Tile map space to world space; snapshot item transforms are in map space.
	Matrix44 ToWorld;
	FString Error;
	// What was loaded, so a re-registered map or host is loaded again.
	FAssetReference Map;
	FString Host;
};

struct FWorldCompositionStreamingChanges
{
	TArray<FString> Loaded, Unloaded, Failed;
};

// Distance-based tile streaming around one viewer. Synchronous: each Update
// unloads tiles beyond UnloadDistance, then loads up to MaxLoadsPerUpdate of
// the nearest unloaded tiles within LoadDistance. A failed tile is not retried
// until Retry, its map or host registration changes, or the viewer moves
// beyond UnloadDistance and returns.
class FWorldCompositionStreaming
{
public:
	explicit FWorldCompositionStreaming(IWorldTileSource& InSource) : Source(InSource) {}
	const FWorldCompositionStreamingSettings& GetSettings() const
	{
		return Settings;
	}
	bool SetSettings(const FWorldCompositionStreamingSettings& NewSettings, FString& Error);
	void Update(const FWorldComposition& Composition, const Vector3d& ViewLocation,
	    FWorldCompositionStreamingChanges* Changes = nullptr);
	// Loaded and failed tiles; pointers remain valid until the next Update.
	const TArray<FStreamedWorldTile>& GetTiles() const
	{
		return Tiles;
	}
	const FStreamedWorldTile* FindTile(const FString& TileId) const;
	bool IsLoaded(const FString& TileId) const;
	// Forgets a failure so the next Update may load the tile again.
	void Retry(const FString& TileId);
	void UnloadAll();

private:
	IWorldTileSource& Source;
	FWorldCompositionStreamingSettings Settings;
	TArray<FStreamedWorldTile> Tiles;
};
