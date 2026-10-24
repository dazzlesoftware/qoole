// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "World/WorldCompositionStreaming.h"
#include "World/WorldLocation.h"

// A region of the game world that is loaded now.
struct FGameRegion
{
	FString Id, Name;
	std::shared_ptr<const FWorldSceneSnapshot> Scene;
	Matrix44 ToRender; // region map space to render space
};

struct FGameWorldChanges
{
	TArray<FString> Loaded, Unloaded, Failed;
	bool bRebased = false;
};

// The world a game plays in, as Unreal's UWorld with its streaming levels:
// one level, or a world composition streamed around the player. Positions are
// region-relative (FWorldLocation). Rendering happens relative to an origin
// cell near the player, rebased as the player travels, so render-space values
// stay small however far the world reaches.
class FGameWorld
{
public:
	// Rebase once the viewer is this many cells from the render origin.
	static constexpr double RebaseCells = 2.0;
	// Opens a Genesis.World level or a Genesis.WorldComposition by reference.
	static TUniquePtr<FGameWorld> Open(
	    std::shared_ptr<FAssetRegistry> Registry, const FAssetReference& World, FString& Error);
	// A level that need not be saved in the registry (Play-in-Editor's current level).
	static TUniquePtr<FGameWorld> OpenLevel(
	    std::shared_ptr<FAssetRegistry> Registry, const FAssetPackage& Level, const FString& Name, FString& Error);

	bool IsComposition() const
	{
		return bComposition;
	}
	const FAssetReference& GetReference() const
	{
		return Reference;
	}
	// A level is one region at the origin; its ID is the level's asset ID.
	const FWorldComposition& GetComposition() const
	{
		return Composition;
	}
	bool SetStreamingSettings(const FWorldCompositionStreamingSettings& Settings, FString& Error);
	// Streams regions around the viewer and rebases the render origin when needed.
	void Update(const FWorldLocation& Viewer, FGameWorldChanges* Changes = nullptr);
	void GetLoadedRegions(TArray<FGameRegion>& Out) const;
	bool IsLoaded(const FString& RegionId) const;

	// Conversions. Absolute positions are world units from the world's origin,
	// in double precision; render positions are relative to the render origin.
	bool ToAbsolute(const FWorldLocation& Location, double& X, double& Y, double& Z) const;
	// The region containing the point, or the nearest one when none does.
	bool FromAbsolute(double X, double Y, double Z, FWorldLocation& Out) const;
	bool ToRender(const FWorldLocation& Location, Vector3d& Out) const;
	// The same place expressed in the region that contains it now (after moving).
	FWorldLocation Normalize(const FWorldLocation& Location) const;
	// The render origin's grid cell.
	int64 GetOriginCellX() const
	{
		return OriginX;
	}
	int64 GetOriginCellY() const
	{
		return OriginY;
	}

	// The highest loaded ground under a location, in its region's space.
	bool SampleGround(const FWorldLocation& Location, float& Height) const;
	// Where a new game starts: the first info_player_start found in a loaded
	// region, else the centre of the first region, placed on the ground.
	bool FindPlayerStart(FWorldLocation& Out) const;

private:
	FGameWorld() = default;
	bool StartLevel(const FAssetPackage& Package, const FString& Name, FString& Error);
	bool RegionCentre(const FString& RegionId, double& X, double& Y) const;
	std::shared_ptr<FAssetRegistry> Registry;
	FAssetReference Reference;
	bool bComposition = false;
	FWorldComposition Composition;
	TUniquePtr<FLocalWorldTileSource> Source;
	TUniquePtr<FWorldCompositionStreaming> Streaming;
	std::shared_ptr<const FWorldSceneSnapshot> Level; // the single level
	int64 OriginX = 0, OriginY = 0;
	bool bOriginSet = false;
};
