// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Assets/RenderAssets.h"
class Matrix44;
struct FNativeMeshVertex
{
	float Position[3]; // view coordinates: X right, Y forward, Z up
	float Normal[3];
	float UV[2];
	uint32 Color;
};
struct FNativeMeshBatch
{
	std::shared_ptr<const FLoadedMeshAsset> Asset;
	uint32 FirstVertex = 0, NumVertices = 0;
	float Depth = 0;
	uint32 Slot = 0;
};
// Runtime-owned geometry and material snapshots. No editor object pointers.
class FNativeMeshDrawList
{
  public:
	TArray<FNativeMeshVertex> Vertices;
	TArray<FNativeMeshBatch> Batches;
	void Reset()
	{
		Vertices.clear();
		Batches.clear();
	}
	// Appends view-space geometry, preserving outputs on invalid input.
	bool Append(const std::shared_ptr<const FLoadedMeshAsset>&, const Matrix44& MeshToView, FString& Error);
	TArray<size_t> GetDrawOrder(bool bTranslucent) const;
};
