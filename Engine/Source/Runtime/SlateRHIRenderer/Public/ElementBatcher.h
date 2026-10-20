// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Turns an FSlateWindowElementList into batches (Engine/Documentation/Source/GenesisUI.md §5.2):
//
//   1. stable-sort elements by layer (ties keep submission order),
//   2. tessellate each into one vertex format, with a batch key
//      (shader, blend, texture, clip),
//   3. merge consecutive pieces with equal keys into one index range.
//
// Merging only joins pieces that are adjacent after the sort, so the draw
// order is exactly the sorted element order; that is why it may also merge
// across layers.
//
// The batcher knows nothing about the GPU. Textures are opaque ids handed
// out by an FSlateShaderResourceManager (the renderer, or a fake in tests).

#pragma once

#include "CoreMinimal.h"
#include "Rendering/DrawElements.h"

enum class ESlateShader : uint8
{
	Default,    // texture * vertex color (images, glyphs, solid colors, viewports)
	RoundedBox, // SDF rounded rectangle with outline
	Line,       // anti-aliased line strip
	Count,
};

enum class ESlateBlendMode : uint8
{
	Alpha,         // straight alpha
	Premultiplied, // shader outputs premultiplied color
	Opaque,
	Count,
};

const char* ToString(ESlateShader Shader);

// The single vertex format of the UI (56 bytes).
struct FSlateVertex
{
	FVector2 Position;     // window pixels
	FVector2 UV;           // texture coordinates, or shader-specific
	FColor Color;          // fill / tint (sRGB, straight alpha)
	FColor SecondaryColor; // outline color for rounded boxes
	float Params0[4];      // shader-specific (see SlateShaders.hlsl)
	float Params1[4];
};
static_assert(sizeof(FSlateVertex) == 56);

using FSlateTextureId = uint64;

struct FSlateResolvedTexture
{
	FSlateTextureId Id = 0;
	FRect UVRect = FRect(0.0f, 0.0f, 1.0f, 1.0f); // where the image lives in the texture
};

class FSlateShaderResourceManager
{
public:
	virtual ~FSlateShaderResourceManager() = default;
	virtual FSlateTextureId GetWhiteTexture() = 0;
	virtual FSlateResolvedTexture ResolveImage(const FSlateTextureData& Image) = 0;
	virtual FSlateResolvedTexture ResolveRenderTarget(ISlateRenderTarget& Target) = 0;
	virtual FSlateTextureId GetGlyphPageTexture(int32 PageIndex) = 0;
};

struct FSlateRenderBatch
{
	int32 Layer = 0; // layer of the first element in the batch
	ESlateShader Shader = ESlateShader::Default;
	ESlateBlendMode Blend = ESlateBlendMode::Alpha;
	FSlateTextureId Texture = 0;
	int32 ClipIndex = INDEX_NONE; // into FBatchedElements::ClipRects
	uint32 FirstIndex = 0;
	uint32 NumIndices = 0;
	uint32 NumElements = 0; // elements merged into this batch
};

struct FBatchedElements
{
	TArray<FSlateVertex> Vertices;
	TArray<uint32> Indices;
	TArray<FSlateRenderBatch> Batches;
	TArray<FIntRect> ClipRects;
	FIntPoint ViewportSize{ 0, 0 };

	void Reset();
};

class FSlateElementBatcher
{
public:
	// FontCache may be null when the list has no text.
	void Batch(const FSlateWindowElementList& Elements, FSlateFontCache* FontCache, FSlateShaderResourceManager& Resolver, FBatchedElements& Out);

	// Disables pixel snapping (for tests comparing exact positions).
	void SetPixelSnapping(bool bEnable) { bPixelSnapping = bEnable; }

private:
	struct FKey
	{
		ESlateShader Shader;
		ESlateBlendMode Blend;
		FSlateTextureId Texture;
		int32 ClipIndex;
		bool operator==(const FKey&) const = default;
	};

	// Starts a new batch unless the key matches the current one.
	void UseKey(const FKey& Key);
	uint32 AddVertex(const FSlateVertex& Vertex);
	void AddQuad(uint32 V0); // V0..V0+3: top-left, top-right, bottom-right, bottom-left

	void AddBox(const FSlateDrawElement& Element);
	void AddRoundedBox(const FSlateDrawElement& Element);
	void AddText(const FSlateDrawElement& Element);
	void AddLines(const FSlateDrawElement& Element);
	void AddCustomVerts(const FSlateDrawElement& Element);
	void AddViewport(const FSlateDrawElement& Element);

	void AddImageQuad(const FTransform2D& Transform, const FRect& LocalRect, const FRect& UV, FColor Color, bool bSnap);
	FVector2 Snap(FVector2 Pixel) const;

	FBatchedElements* Out = nullptr;
	FSlateFontCache* FontCache = nullptr;
	FSlateShaderResourceManager* Resolver = nullptr;
	const FSlateDrawElement* CurrentElement = nullptr;
	bool bNewElement = false;
	bool bPixelSnapping = true;
};
