// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Assets/RenderAssets.h"
struct FMaterialShaderCacheStats
{
	uint64 Hits = 0, Misses = 0, Bytes = 0;
	int32 Entries = 0;
};
class FMaterialEditingLibrary
{
  public:
	static FMaterialGraph CreateGraph(const FMaterialAsset&);
	static bool CompileMaterial(FMaterialAsset&, FString& Error);
	static bool CompileMaterialInstance(const FMaterialAsset&, FMaterialInstanceAsset&, FString& Error);
	static bool RecompileDependents(FAssetRegistry&, const FAssetReference&, int32& Updated, FString& Error);
	static FMaterialShaderCacheStats GetShaderCacheStats();
	static void ClearShaderCache();
};
