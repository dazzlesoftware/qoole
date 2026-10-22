// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "AssetRegistry.h"
#include <memory>
class StaticMesh;

struct FTextureAsset
{
	int32 Width = 0, Height = 0;
	bool bSRGB = true;
	TArray<uint8> Pixels; // tightly packed RGBA8, rows from the top
};

enum class EMaterialBlendMode
{
	Opaque,
	Masked,
	Translucent
};

struct FMaterialAsset
{
	float BaseColor[4] = {1, 1, 1, 1};
	float Roughness = 0.5f, Metallic = 0;
	EMaterialBlendMode BlendMode = EMaterialBlendMode::Opaque;
	float OpacityMaskClip = 0.333f;
	FAssetReference BaseColorTexture;
	FAssetReference NormalTexture; // tangent-space RGB; always sampled as linear data
};

struct FStaticMeshAsset
{
	std::shared_ptr<const StaticMesh> Mesh;
	FAssetReference Material;
};

// Typed codecs never read image files or depend on editor/graphics libraries.
// Encoders preserve Package.Id and replace its payload only on success.
class FRenderAssetCodec
{
  public:
	static bool EncodeTexture(const FTextureAsset&, FAssetPackage&, FString& Error);
	static bool DecodeTexture(const FAssetPackage&, FTextureAsset&, FString& Error);
	static bool EncodeMaterial(const FMaterialAsset&, FAssetPackage&, FString& Error);
	static bool DecodeMaterial(const FAssetPackage&, FMaterialAsset&, FString& Error);
	static bool EncodeMesh(const FStaticMeshAsset&, FAssetPackage&, FString& Error);
	static bool DecodeMesh(const FAssetPackage&, FStaticMeshAsset&, FString& Error);
};

struct FLoadedMeshAsset
{
	FAssetReference Source, MaterialOverride;
	FStaticMeshAsset Data;
	TOptional<FMaterialAsset> Material;
	TOptional<FTextureAsset> Texture;
	TOptional<FTextureAsset> NormalTexture;
	std::shared_ptr<const StaticMesh> DisplayMesh;
};

class FRenderAssetLoader
{
  public:
	explicit FRenderAssetLoader(const FAssetRegistry& InRegistry) : Registry(InRegistry)
	{
	}
	bool LoadTexture(const FAssetReference&, FTextureAsset&, FString& Error) const;
	bool LoadMaterial(const FAssetReference&, FMaterialAsset&, FString& Error) const;
	bool LoadMesh(const FAssetReference&, FLoadedMeshAsset&, FString& Error,
				  const FAssetReference& MaterialOverride = {}) const;

  private:
	const FAssetRegistry& Registry;
};
