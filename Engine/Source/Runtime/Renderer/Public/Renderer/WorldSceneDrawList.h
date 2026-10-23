// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Renderer/LandscapeProjection.h"
#include "Renderer/FoliageRenderPlan.h"
#include "World/WorldSceneSnapshot.h"
struct FWorldSceneDrawList
{
	FNativeMeshDrawList Meshes;
	TArray<FProjectedVertex> Vertices;
	TArray<FProjectedTriangleBatch> Batches;
	TArray<FMeshInstanceBatch> Instances;
	int32 TerrainComponents = 0, TerrainTriangles = 0;
	uint32 FoliageInstances = 0;
	// Builds a new list atomically, with view-space culling and runtime payloads.
	bool Build(const FWorldSceneSnapshot&, const Matrix44& WorldToView, const FSceneViewVolume&,
			   const FProjectedSceneView&, float terrainDetailPixels, FString& Error);
};
