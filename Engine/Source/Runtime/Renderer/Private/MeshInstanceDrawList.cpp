// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Renderer/MeshInstanceDrawList.h"
#include "Geometry/StaticMesh.h"
#include <cmath>
bool AppendMeshInstance(
    TArray<FMeshInstanceBatch>& Batches, const std::shared_ptr<const StaticMesh>& Mesh, const Matrix44& ToView)
{
	if (!Mesh)
		return false;
	float Rows[12];
	for (int R = 0; R < 3; R++)
		for (int C = 0; C < 4; C++)
		{
			const float V = ToView.GetMatrix(R, C);
			if (!std::isfinite(V))
				return false;
			Rows[R * 4 + C] = V;
		}
	bool Mirrored = false;
	if (!ValidateMeshInstanceTransform(Rows, Mirrored))
		return false;
	FMeshInstanceBatch* Batch = nullptr;
	for (FMeshInstanceBatch& Existing : Batches)
		if (Existing.Mesh == Mesh && Existing.bMirrored == Mirrored)
		{
			Batch = &Existing;
			break;
		}
	if (!Batch)
	{
		Batches.push_back({Mesh, {}, Mirrored});
		Batch = &Batches.back();
	}
	Batch->Rows.insert(Batch->Rows.end(), Rows, Rows + 12);
	return true;
}

bool ValidateMeshInstanceTransform(const float* Rows, bool& Mirrored)
{
	if (!Rows)
		return false;
	for (int I = 0; I < 12; I++)
		if (!std::isfinite(Rows[I]))
			return false;
	const double D = double(Rows[0]) * (double(Rows[5]) * Rows[10] - double(Rows[6]) * Rows[9]) -
	    double(Rows[1]) * (double(Rows[4]) * Rows[10] - double(Rows[6]) * Rows[8]) +
	    double(Rows[2]) * (double(Rows[4]) * Rows[9] - double(Rows[5]) * Rows[8]);
	if (!std::isfinite(D) || std::abs(D) < 1e-12)
		return false;

	Mirrored = D < 0;
	return true;
}
