// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Assets/RenderAssets.h"
#include "World/World.h"
class Landscape;
class Foliage;
struct FWorldSceneItem
{
	FString ActorId, ComponentId;
	uint64 RenderKey = 0; // process-local resource identity, stable for this snapshot
	Matrix44 ToWorld;
	std::shared_ptr<const FLoadedMeshAsset> Mesh;
	std::shared_ptr<const Landscape> Terrain;
	std::shared_ptr<const Foliage> Plants;
	TArray<std::shared_ptr<const FLoadedMeshAsset>> SplineMeshes;
};
// Explicit package-to-runtime construction, independent of legacy object trees.
// Geometry payloads own their data after the source package is released.
// Native terrain images resolve through the renderer's active asset registry.
class FWorldSceneSnapshot
{
  public:
	const FWorld& GetScene() const
	{
		return Scene;
	}
	const TArray<FWorldSceneItem>& GetItems() const
	{
		return Items;
	}
	int32 GetMetadataComponents() const
	{
		return MetadataComponents;
	}
	static bool Load(const FAssetPackage&, const FAssetRegistry*, FWorldSceneSnapshot&, FString& Error);

  private:
	FWorld Scene;
	TArray<FWorldSceneItem> Items;
	int32 MetadataComponents = 0; // point entities (worldspawn, player starts) have no renderable payload
};
