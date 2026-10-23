// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Assets/RenderAssets.h"

struct FRenderTextureMipChain
{
	TArray<uint8> Pixels; // levels packed in RHI order
	uint32 Levels = 0;
	// Color maps filter in linear light when marked sRGB. Normal map mip
	// averages are renormalized. Odd dimensions include all source texels.
	static bool Build(const FTextureAsset&, bool bNormalMap, FRenderTextureMipChain&, FString& Error);
};
