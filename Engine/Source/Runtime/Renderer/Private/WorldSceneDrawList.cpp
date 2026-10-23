// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Renderer/WorldSceneDrawList.h"
#include "Geometry/StaticMesh.h"
#include "Foliage/Foliage.h"
#include <algorithm>
#include <cmath>
namespace
{
// sqrt(||M||1 * ||M||inf) bounds the largest singular value, including shear.
float BoundScale(const Matrix44& M)
{
	float MaxRow = 0, MaxCol = 0;
	for (int I = 0; I < 3; I++)
	{
		float Row = 0, Col = 0;
		for (int J = 0; J < 3; J++)
		{
			Row += std::abs(M.GetMatrix(I, J));
			Col += std::abs(M.GetMatrix(J, I));
		}
		MaxRow = std::max(MaxRow, Row);
		MaxCol = std::max(MaxCol, Col);
	}
	return std::sqrt(MaxRow * MaxCol);
}
bool MeshVisible(const StaticMesh& Mesh, const Matrix44& T, const FSceneViewVolume& Volume)
{
	Vector3d Low, High, Corners[8];
	Mesh.GetBounds(Low, High);
	for (int I = 0; I < 8; I++)
		T.Transform(Corners[I], Vector3d((I & 1) ? High.GetX() : Low.GetX(), (I & 2) ? High.GetY() : Low.GetY(),
										 (I & 4) ? High.GetZ() : Low.GetZ()));
	return Volume.BoundBox(Corners) >= 0;
}
} // namespace
bool FWorldSceneDrawList::Build(const FWorldSceneSnapshot& Scene, const Matrix44& WorldToView,
								const FSceneViewVolume& Volume, const FProjectedSceneView& View, float Detail,
								FString& Error)
{
	if (!Volume.IsValid() || !Volume.Perspective || !View.Width || !View.Height ||
		!std::isfinite(View.ProjectionScale) || View.ProjectionScale <= 0)
	{
		Error = "Runtime scene drawing requires a valid perspective view.";
		return false;
	}
	for (int R = 0; R < 4; R++)
		for (int C = 0; C < 4; C++)
			if (!std::isfinite(WorldToView.GetMatrix(R, C)))
			{
				Error = "Non-finite view transform.";
				return false;
			}
	if (WorldToView.GetMatrix(3, 0) != 0 || WorldToView.GetMatrix(3, 1) != 0 || WorldToView.GetMatrix(3, 2) != 0 ||
		WorldToView.GetMatrix(3, 3) != 1)
	{
		Error = "View transform must be affine.";
		return false;
	}
	FWorldSceneDrawList Out;
	for (const auto& Item : Scene.GetItems())
	{
		Matrix44 ToView;
		ToView.Multiply(WorldToView, Item.ToWorld);
		if (Item.Mesh && !Item.Mesh->Data.Mesh)
		{
			Error = "Missing runtime mesh geometry.";
			return false;
		}
		if (Item.Mesh && MeshVisible(*Item.Mesh->Data.Mesh, ToView, Volume) &&
			!Out.Meshes.Append(Item.Mesh, ToView, Error))
			return false;
		if (Item.Terrain)
		{
			Vector3d Origin, Axes[3];
			Item.ToWorld.Transform(Origin, Vector3d::origVec);
			for (int I = 0; I < 3; I++)
			{
				Vector3d V(I == 0 ? 1.0f : 0.0f, I == 1 ? 1.0f : 0.0f, I == 2 ? 1.0f : 0.0f);
				Item.ToWorld.Transform(Axes[I], V);
				Axes[I].SubVector(Origin);
			}
			FLandscapeDrawStats Stats;
			AppendProjectedLandscape(Item.Terrain, Item.RenderKey, ToView, Item.ToWorld,
									 Vector3d(Axes[0].GetMag(), Axes[1].GetMag(), Axes[2].GetMag()), Volume, View,
									 Detail, Out.Vertices, Out.Batches, Stats);
			Out.TerrainComponents += Stats.Components;
			Out.TerrainTriangles += Stats.Triangles;
			for (const auto& Mesh : Item.SplineMeshes)
				if (MeshVisible(*Mesh->Data.Mesh, ToView, Volume) && !Out.Meshes.Append(Mesh, ToView, Error))
					return false;
		}
		if (Item.Plants)
			Out.FoliageInstances +=
				BuildFoliageDrawList(*Item.Plants, ToView, BoundScale(Item.ToWorld), Volume, Out.Instances);
	}
	*this = std::move(Out);
	Error.Empty();
	return true;
}
