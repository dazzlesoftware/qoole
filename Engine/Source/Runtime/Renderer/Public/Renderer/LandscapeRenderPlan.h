// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Landscape/Landscape.h"
#include "Renderer/SceneViewVolume.h"

struct FLandscapeRenderPlan
{
	std::vector<int> steps;
	std::vector<int> bounds;
	int cxs = 0, cys = 0;
	int Step(int cx, int cy) const
	{
		return (cx < 0 || cy < 0 || cx >= cxs || cy >= cys) ? 0 : steps[(size_t)cy * cxs + cx];
	}
};

// bounds: -1 outside, 0 intersecting, 1 inside. Step 0 means culled.
void PlanLandscape(const Landscape&, const Matrix44&, const FSceneViewVolume&, bool perspective, float projectionScale,
				   float zoom, float targetPixels, FLandscapeRenderPlan&);
struct FLandscapeComponentMesh
{
	std::vector<int> X, Y;
	std::vector<Vector3d> Points;
};
// Reuses storage across components; never constructs the full-resolution landscape mesh.
void BuildLandscapeComponent(const Landscape&, const FLandscapeRenderPlan&, int cx, int cy, const Matrix44&,
							 bool stitchEdges, FLandscapeComponentMesh&);
