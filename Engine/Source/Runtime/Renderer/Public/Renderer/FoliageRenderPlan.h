// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Renderer/MeshInstanceDrawList.h"
#include "Renderer/SceneViewVolume.h"
class Foliage;
struct FoliageInstance;
// Conservative view-space sphere/frustum and per-type distance culling.
bool IsFoliageInstanceVisible(const Foliage&, const FoliageInstance&, const Matrix44&, float objectScale,
							  const FSceneViewVolume&, Matrix44& instanceToView, bool& needsClip);
// Runtime-only traversal. Input snapshots may be released after preparation;
// each output batch owns its mesh. Returns the number of visible instances.
uint32 BuildFoliageDrawList(const Foliage&, const Matrix44&, float objectScale, const FSceneViewVolume&,
							TArray<FMeshInstanceBatch>&);
