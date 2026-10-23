// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Materials/MaterialGraph.h"

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
	TOptional<FMaterialGraph> Graph;
	// Cooked target programs. Shipped runtimes never invoke a shader compiler.
	TArray<uint8> GraphDXIL, GraphSPIRV;
	FString CookedGraphSource;
};

// Includes material instances; usable by pickers without loading asset payloads.
bool IsMaterialAsset(const FAssetPackageMetadata&);
