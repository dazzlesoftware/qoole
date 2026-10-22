// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Assets/RenderAssets.h"
class FMapAssetImport
{
  public:
	// Geometry-only OBJ: triangle/convex polygon faces, normals and UVs.
	// Materials are assigned as native assets; OBJ/MTL materials are not imported.
	static bool ReadObj(const FString &File, FStaticMeshAsset &, FString &Error);
	static bool ImportTexture(const FString &File, const FString &Target, FString &Error);
	static bool ImportMesh(const FString &File, const FString &Target, FString &Error);
};
