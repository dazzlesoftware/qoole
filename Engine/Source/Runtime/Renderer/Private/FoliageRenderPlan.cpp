// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Renderer/FoliageRenderPlan.h"
#include "Foliage/Foliage.h"
#include <cmath>
// Where an instance is in view space and whether it is worth drawing:
// in front, in the view's cone, nearer than its cull distance. Clip is
// set when it crosses the view's edges (its triangles need cutting).
bool IsFoliageInstanceVisible(const Foliage& foliage, const FoliageInstance& instance, const Matrix44& trans,
							  float objectScale, const FSceneViewVolume& clip, Matrix44& out, bool& needsClip)
{
	needsClip = false;
	if (instance.type < 0 || instance.type >= foliage.GetNumTypes() || !std::isfinite(instance.scale) ||
		instance.scale <= 0 || !std::isfinite(objectScale) || objectScale <= 0)
		return false;
	const FoliageType& type = foliage.GetTypes()[instance.type];
	const std::shared_ptr<const StaticMesh>& mesh = foliage.GetMesh(instance.type);
	if (!mesh)
		return false;
	out.Multiply(trans, foliage.GetInstanceMatrix(instance));
	Vector3d p;
	out.Transform(p, Vector3d::origVec);
	// Its pivot is at its foot: the sphere around its middle.
	Vector3d low, high;
	mesh->GetBounds(low, high);
	Vector3d middle(0.5f * (low.GetX() + high.GetX()), 0.5f * (low.GetY() + high.GetY()),
					0.5f * (low.GetZ() + high.GetZ()));
	out.Transform(p, middle);
	const float r = mesh->GetBoundRadius() * instance.scale * objectScale;
	if (!std::isfinite(r) || !std::isfinite(p.GetX()) || !std::isfinite(p.GetY()) || !std::isfinite(p.GetZ()))
		return false;
	const Vector3d& near = clip.Minimum;
	const Vector3d& far = clip.Maximum;
	if (!clip.Perspective)
	{
		needsClip = p.GetX() - r < near.GetX() || p.GetX() + r > far.GetX() || p.GetZ() - r < near.GetZ() ||
					p.GetZ() + r > far.GetZ();
		return !(p.GetX() + r < near.GetX() || p.GetX() - r > far.GetX() || p.GetZ() + r < near.GetZ() ||
				 p.GetZ() - r > far.GetZ());
	}
	if (p.GetY() + r < near.GetY() || p.GetY() - r > far.GetY())
		return false;
	if (type.cullDistance > 0.0f && p.GetMag() - r > type.cullDistance * objectScale)
		return false;
	const float leftSlack = r * std::sqrt(1 + near.GetX() * near.GetX());
	const float rightSlack = r * std::sqrt(1 + far.GetX() * far.GetX());
	const float bottomSlack = r * std::sqrt(1 + near.GetZ() * near.GetZ());
	const float topSlack = r * std::sqrt(1 + far.GetZ() * far.GetZ());
	if (p.GetX() + leftSlack < p.GetY() * near.GetX() || p.GetX() - rightSlack > p.GetY() * far.GetX() ||
		p.GetZ() + bottomSlack < p.GetY() * near.GetZ() || p.GetZ() - topSlack > p.GetY() * far.GetZ())
		return false;
	needsClip = p.GetY() - r < near.GetY() || p.GetY() + r > far.GetY() ||
				p.GetX() - leftSlack < p.GetY() * near.GetX() || p.GetX() + rightSlack > p.GetY() * far.GetX() ||
				p.GetZ() - bottomSlack < p.GetY() * near.GetZ() || p.GetZ() + topSlack > p.GetY() * far.GetZ();
	return true;
}

uint32 BuildFoliageDrawList(const Foliage& foliage, const Matrix44& transform, float scale,
							const FSceneViewVolume& view, TArray<FMeshInstanceBatch>& out)
{
	uint32 count = 0;
	Matrix44 placed;
	for (const auto& instance : foliage.GetInstances())
	{
		bool clip = false;
		if (IsFoliageInstanceVisible(foliage, instance, transform, scale, view, placed, clip) &&
			AppendMeshInstance(out, foliage.GetMesh(instance.type), placed))
			++count;
	}
	return count;
}
