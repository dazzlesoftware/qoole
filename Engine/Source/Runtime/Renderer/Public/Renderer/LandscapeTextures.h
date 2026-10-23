// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Runtime landscape layer images, procedural textures and incremental baking.
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
class FAssetRegistry;

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
	// Called when a project/world asset context changes; weak ownership avoids
	// holding closed projects open. Built-in/file textures need no registry.
	static void SetAssetRegistry(const std::shared_ptr<FAssetRegistry>&);
	static std::string RevisionKey();

	static const TArray<FString>& GetBuiltInNames();
	static FString BuiltIn(const FString& Name)
	{
		return "builtin:" + Name;
	}
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
	std::string NativeRevision;
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

	int32 GetWidth() const
	{
		return Image.Width;
	}
	int32 GetHeight() const
	{
		return Image.Height;
	}
	const TArray<uint8>& GetPixels() const
	{
		return Image.Pixels;
	}
	// The texel's color (tests).
	FColor GetTexel(int32 X, int32 Y) const;

  private:
	void Bake(const Landscape& Land, const FIntRect& Texels);

	std::shared_ptr<const Landscape> Baked;
	std::string NativeRevision;
	FLandscapeImage Image;
};

// A landscape's layers for the GPU to blend per pixel (the map views'
// landscape shader, PSLandscape), sharper than a bake: their weights at the
// grid points, in two RGBA slices (layers 0-3, then 4-7), and their
// textures, all one size, a slice each with their mip levels (built-in
// patterns tinted with the layer's color, plain layers their color).
class FLandscapeLayerImages
{
  public:
	static constexpr int32 MaxLayers = 8;
	static constexpr int32 MaxTextureSize = 1024;
	// Up to MaxLayers layers, one textured at least (else it is drawn with
	// colors at its grid points).
	static bool CanBlend(const Landscape& Land);

	// The weights brought up to this landscape: all of them the first time
	// or when its size or layer count changed, else the grid points whose
	// weights changed. Returns those points (empty if none).
	FIntRect UpdateWeights(const std::shared_ptr<const Landscape>& Land);
	int32 GetPointsX() const
	{
		return PointsX;
	}
	int32 GetPointsY() const
	{
		return PointsY;
	}
	// Slice 0 then slice 1, each PointsX x PointsY RGBA8.
	const TArray<uint8>& GetWeights() const
	{
		return Weights;
	}

	// The textures remade if a layer's look (texture, color) changed since;
	// true if they were.
	bool UpdateTextures(const Landscape& Land);
	int32 GetTextureSize() const
	{
		return TextureSize;
	}
	int32 GetMipLevels() const
	{
		return MipLevels;
	}
	int32 GetNumSlices() const
	{
		return Slices;
	}
	// Each slice's mip levels in turn, tightly packed (RHI's layout).
	const TArray<uint8>& GetTextures() const
	{
		return Textures;
	}

	// The landscape shader's constants after the view's scale: the size in
	// units, the layer count, and each layer's repeats a unit (1 / tiling).
	static void GetConstants(const Landscape& Land, float Out[12]);

  private:
	std::shared_ptr<const Landscape> WeightsOf;
	int32 PointsX = 0;
	int32 PointsY = 0;
	int32 Layers = 0;
	TArray<uint8> Weights;

	std::string Look; // what the textures were made from
	int32 TextureSize = 0;
	int32 MipLevels = 0;
	int32 Slices = 0;
	TArray<uint8> Textures;
};
