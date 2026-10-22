// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Assets/RenderAssets.h"
class Object;

// Transitional editor context. The shell owns the registry; no hidden scans or
// path fallback can replace a missing UUID with a different asset.
class FMapAssetLibrary
{
  public:
	static void SetRegistry(const std::shared_ptr<FAssetRegistry> &);
	static std::shared_ptr<FAssetRegistry> GetRegistry();
	static std::string RevisionKey();
	static std::shared_ptr<const FLoadedMeshAsset> LoadMesh(const FAssetReference &, const FAssetReference &Override,
															FString &Error);
	static FString TextureToken(const FAssetReference &);
	static bool ParseTextureToken(const FString &, FAssetReference &, FString &Error);
	static bool LoadTexture(const FString &Token, FTextureAsset &, FString &Error);
	static bool HasNativeAssets(const Object &);
};
