// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Landscape layer textures, as the editor draws them.
//
// A paint layer's texture (LandscapeLayer::texture) is one of
//   ""                  its plain color
//   "builtin:<Name>"    one of the editor's own patterns (GetBuiltInNames),
//                       tinted with the layer's color, so a preset layer
//                       looks like ground without any asset
//   a file's path       an image (PNG, BMP, JPEG, TIFF), drawn as it is
// repeating every LandscapeLayer::tiling units of the landscape.
//
// FLandscapeTextureLibrary keeps the textures (with smaller copies, so far
// views do not shimmer). FLandscapeBake blends a landscape's layers by
// their weights into one image, a few texels per quad, which the map views
// draw the landscape with: the GPU sees one texture per landscape. When a
// stroke changes the weights, only the part of the image under it is baked
// again (Update says which).

#pragma once

#include "CoreMinimal.h"
#include "Math/Rect.h"

#include <map>
#include <memory>
#include <string>

class Landscape;

// RGBA8 rows from the top.
struct FLandscapeImage
{
	int32 Width = 0;
	int32 Height = 0;
	TArray<uint8> Pixels;
};

class FLandscapeTexture
{
public:
	// Mip 0 is the image; each after it half the size, down to 1 x 1.
	TArray<FLandscapeImage> Mips;
	// Built-in patterns are shades of grey (128 is the layer's own color)
	// that tint the layer's color; images are drawn as they are.
	bool bTint = false;

	// Interpolated and wrapping, at U, V in repeats, from the mip whose
	// texels are about Footprint repeats wide. RGB 0 to 255.
	void Sample(float U, float V, float Footprint, float Out[3]) const;
};

class FLandscapeTextureLibrary
{
public:
	static FLandscapeTextureLibrary& Get();

	static const TArray<FString>& GetBuiltInNames();
	static FString BuiltIn(const FString& Name) { return "builtin:" + Name; }
	// "Grass" for "builtin:Grass", the file's name for a path, "None" for "".
	static FString DisplayName(const std::string& Texture);
	// Any layer of it has a texture: it is drawn with a baked image.
	static bool IsTextured(const Landscape& Land);

	// Null for "" or a file that cannot be read (that is logged once).
	const FLandscapeTexture* Find(const std::string& Texture);
	// Reads an image file as RGBA8.
	static bool LoadImageFile(const FString& Path, FLandscapeImage& Out, FString& Error);

private:
	std::map<std::string, std::unique_ptr<FLandscapeTexture>> Textures;
	std::map<std::string, bool> Failed;
};

class FLandscapeBake
{
public:
	// The baked image's texels per quad: as many as fit 2048 on a side,
	// 1 to 16.
	static int32 GetTexelsPerQuad(const Landscape& Land);
	static constexpr int32 MaxSize = 2048;

	// Brings the image up to this landscape: all of it the first time, or
	// when its size or a layer's look changed; otherwise just where the
	// weights changed. Returns the texels baked (empty if none).
	FIntRect Update(const std::shared_ptr<const Landscape>& Land);

	int32 GetWidth() const { return Image.Width; }
	int32 GetHeight() const { return Image.Height; }
	const TArray<uint8>& GetPixels() const { return Image.Pixels; }
	// The texel's color (tests).
	FColor GetTexel(int32 X, int32 Y) const;

private:
	void Bake(const Landscape& Land, const FIntRect& Texels);

	std::shared_ptr<const Landscape> Baked;
	FLandscapeImage Image;
};
