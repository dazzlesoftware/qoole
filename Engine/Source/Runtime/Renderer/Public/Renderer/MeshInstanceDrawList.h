// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "CoreMinimal.h"
#include <memory>
class StaticMesh;
class Matrix44;
struct FMeshInstanceBatch
{
	std::shared_ptr<const StaticMesh> Mesh;
	TArray<float> Rows; // three object-to-view rows per instance
	bool bMirrored = false;
	uint32 Count() const
	{
		return static_cast<uint32>(Rows.size() / 12);
	}
};

// Appends atomically; rejects missing meshes, non-finite or singular transforms.
bool AppendMeshInstance(TArray<FMeshInstanceBatch>&, const std::shared_ptr<const StaticMesh>&, const Matrix44&);

// Rows must point to twelve floats. Winding is derived from the affine determinant.
bool ValidateMeshInstanceTransform(const float* Rows, bool& bMirrored);
