// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// FHeightmap: a grayscale heightmap or weightmap file, as Unreal's landscape
// import and export take them:
//
//   .png         grayscale, 16-bit or 8-bit; any PNG, and BMP, JPEG or TIFF,
//                is read as gray (8-bit images in 256 steps)
//   .r16 / .r8   16-bit little-endian or 8-bit samples, no header; square
//   .raw         either, by its size (8-bit if the bytes make a square)
//
// Heightmaps are written 16-bit; weightmaps (a paint layer's weights, black
// none, white all) 8-bit, as Unreal writes them.
//
// As in Unreal, mid-grey (32768) is height 0 and ZRange map units span
// black to white: the default, 512, is what Unreal's Z scale of 100 gives.
// The image's top is the landscape's +Y edge (the top of the Top view).

#pragma once

#include "CoreMinimal.h"
#include "Services/FileDialogs.h"

#include <memory>
#include <vector>

class Landscape;

struct FHeightmap
{
	int32 Width = 0;
	int32 Height = 0;
	TArray<uint16> Samples; // row by row from the image's top

	static constexpr float DefaultZRange = 512.0f;

	bool IsValid() const { return Width >= 2 && Height >= 2 && Samples.size() == static_cast<SIZE_T>(Width) * Height; }
	uint16 At(int32 X, int32 Y) const { return Samples[static_cast<SIZE_T>(Y) * Width + X]; }
	// Interpolated at a fraction (0 to 1) of the way across and down.
	float Sample(float U, float V) const;

	// By the file's extension (the file dialog's filter list: FileFilters).
	static bool Load(const FString& Path, FHeightmap& Out, FString& Error);
	// Bits is 16 or 8, unless the extension says (.r16, .r8).
	bool Save(const FString& Path, FString& Error, int32 Bits = 16) const;
	static TArray<FFileTypeFilter> FileFilters();

	// The heights of a grid of PointsX x PointsY (row by row from -Y, as a
	// Landscape keeps them), the image stretched over it.
	std::vector<float> ToHeights(int32 PointsX, int32 PointsY, float ZRange) const;
	// One sample per grid point; heights outside the range are clamped.
	static FHeightmap FromLandscape(const Landscape& Land, float ZRange);

	// A paint layer's weights (0 to 1), the same ways.
	std::vector<float> ToWeights(int32 PointsX, int32 PointsY) const;
	static FHeightmap FromWeights(const Landscape& Land, int32 Layer);
};
