// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Renderer/LandscapeRenderPlan.h"
struct FLandscapeTriangleVertex
{
	Vector3d Position; // view coordinates: X right, Y forward, Z up
	float U = 0, V = 0, Shade = 1;
	unsigned int Color = 0xFFFFFFFFu;
};
struct FLandscapeTriangle
{
	FLandscapeTriangleVertex Vertices[3];
	bool bColored = false;
};
struct FLandscapeDrawStats
{
	int Components = 0, Triangles = 0;
};
class ILandscapeTriangleSink
{
  public:
	virtual ~ILandscapeTriangleSink() = default;
	virtual void OnTriangle(const FLandscapeTriangle&) = 0;
};
// Emits visible, seam-stitched and frustum-clipped triangles. UVs/light follow
// clipping; color clipping preserves the existing renderer's mean-color fallback.
// MeshToWorld and Scale retain the editor's current preview-light convention.
// Scene GPU projection and editor selection/line drawing remain separate callers.
void BuildLandscapeTriangles(const Landscape&, const Matrix44& MeshToView, const Matrix44& MeshToWorld,
							 const Vector3d& Scale, const FSceneViewVolume&, float projectionScale, float targetPixels,
							 ILandscapeTriangleSink&, FLandscapeDrawStats&);
