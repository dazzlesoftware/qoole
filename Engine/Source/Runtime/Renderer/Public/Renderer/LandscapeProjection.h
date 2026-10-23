// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Renderer/ProjectedSceneRenderer.h"
#include "Renderer/LandscapeDrawList.h"
// Native scene-facing adapter: prepares projected geometry and owns terrain
// snapshots in batches, without invoking FDraw or any editor types.
void AppendProjectedLandscape(const std::shared_ptr<const Landscape>&, uint64 Key, const Matrix44& MeshToView,
							  const Matrix44& MeshToWorld, const Vector3d& Scale, const FSceneViewVolume&,
							  const FProjectedSceneView&, float targetPixels, TArray<FProjectedVertex>&,
							  TArray<FProjectedTriangleBatch>&, FLandscapeDrawStats&);
