// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Materials/Material.h"
struct FMaterialParameterOverride
{
	FString Name;
	uint8 Width = 1;
	float Value[4] = {};
};
struct FMaterialTextureParameterOverride
{
	FString Name;
	FAssetReference Texture;
};
struct FMaterialInstanceAsset
{
	FAssetReference Parent;
	TArray<FMaterialParameterOverride> Parameters;
	TArray<FMaterialTextureParameterOverride> Textures;
};
class FMaterialInstance
{
  public:
	// Resolves named overrides into a copy; never alters the parent or its cooked shaders.
	static bool Resolve(const FMaterialAsset& Parent, const FMaterialInstanceAsset&, FMaterialAsset& Out,
						FString& Error);
};
