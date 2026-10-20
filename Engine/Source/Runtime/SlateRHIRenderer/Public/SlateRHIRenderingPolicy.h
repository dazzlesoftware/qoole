// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Draws FDrawElementLists through the RHI (docs/GenesisUI.md §5.2).
//
// Per frame, between FRHIDevice::BeginFrame and EndFrame:
//
//   FRHICommandList& Cmd = Device.BeginFrame();
//   Renderer.BeginFrame();
//   Renderer.Render(Cmd, SwapChain, Elements, ClearColor);   // per window
//   Device.EndFrame();
//
// The renderer owns the GPU side of UI resources: image atlas pages (small
// images, packed on first use), standalone textures for large images, glyph
// atlas pages mirrored from the FSlateFontCache, and per-frame vertex/index
// buffers.

#pragma once

#include "ElementBatcher.h"
#include "RHI.h"

// An RHI render target that UI can show with a Viewport element.
class FSlateRenderTargetRHI final : public ISlateRenderTarget
{
public:
	explicit FSlateRenderTargetRHI(TSharedPtr<FRHITexture> InTexture) : Texture(std::move(InTexture)) {}
	FIntPoint GetSize() const override { return FIntPoint(static_cast<int32>(Texture->GetWidth()), static_cast<int32>(Texture->GetHeight())); }
	FRHITexture& GetTexture() const { return *Texture; }

private:
	TSharedPtr<FRHITexture> Texture;
};

struct FSlateRenderStats
{
	int32 Elements = 0;
	int32 Batches = 0;
	int32 Vertices = 0;
	int32 Indices = 0;
};

class FSlateRHIRenderingPolicy final : public FSlateShaderResourceManager
{
public:
	FSlateRHIRenderingPolicy(FRHIDevice& InDevice, FSlateFontCache& InFontCache);
	~FSlateRHIRenderingPolicy() override;

	bool Initialize();

	void BeginFrame();

	// Draws a list into a swapchain (or a render target). The pass begins and
	// ends inside; ClearColor clears first.
	void Render(FRHICommandList& CommandList, FRHISwapChain& Target, const FSlateWindowElementList& Elements, const TOptional<FLinearColor>& ClearColor);
	void Render(FRHICommandList& CommandList, FRHITexture& Target, const FSlateWindowElementList& Elements, const TOptional<FLinearColor>& ClearColor);

	TSharedRef<FSlateRenderTargetRHI> CreateRenderTarget(int32 Width, int32 Height, const char* DebugName = nullptr);

	const FSlateRenderStats& GetLastStats() const { return LastStats; }
	FSlateElementBatcher& GetBatcher() { return Batcher; }

	// FSlateShaderResourceManager
	FSlateTextureId GetWhiteTexture() override { return WhiteTextureId; }
	FSlateResolvedTexture ResolveImage(const FSlateTextureData& Image) override;
	FSlateResolvedTexture ResolveRenderTarget(ISlateRenderTarget& Target) override;
	FSlateTextureId GetGlyphPageTexture(int32 PageIndex) override;

	// Images up to this size (each side) go into shared atlas pages.
	static constexpr int32 MaxAtlasedImageSize = 256;
	static constexpr int32 ImageAtlasPageSize = 1024;

private:
	struct FImageEntry
	{
		uint32 Revision = 0;
		FSlateTextureId Texture = 0;
		FRect UVRect;
		FIntRect AtlasSlot;  // in its page, when atlased
		int32 AtlasPage = -1;
	};

	struct FImagePage
	{
		TSharedPtr<FRHITexture> Texture;
		FSlateTextureAtlas Packer{ ImageAtlasPageSize, ImageAtlasPageSize, 1 };
		FSlateTextureId Id = 0;
	};

	struct FFrameBuffers
	{
		TSharedPtr<FRHIBuffer> Vertices;
		TSharedPtr<FRHIBuffer> Indices;
		uint64 VertexCursor = 0; // bytes used this frame
		uint64 IndexCursor = 0;
	};

	void RenderBatches(FRHICommandList& CommandList, EPixelFormat Format, FIntPoint TargetSize);
	void SyncGlyphPages();
	FRHIGraphicsPipeline* GetPipeline(ESlateShader Shader, ESlateBlendMode Blend, EPixelFormat Format);
	FSlateTextureId RegisterTexture(TSharedPtr<FRHITexture> Texture);
	void UploadImage(const FSlateTextureData& Image, FImageEntry& Entry);
	bool EnsureCapacity(TSharedPtr<FRHIBuffer>& Buffer, uint64 Required, EBufferUsage Usage, const char* Name);

	FRHIDevice& Device;
	FSlateFontCache& FontCache;
	FSlateElementBatcher Batcher;
	FBatchedElements Batched;
	FSlateRenderStats LastStats;

	TMap<FSlateTextureId, TSharedPtr<FRHITexture>> Textures;
	FSlateTextureId NextTextureId = 1;
	FSlateTextureId WhiteTextureId = 0;

	TMap<uint64, FImageEntry> Images;
	TArray<FImagePage> ImagePages;
	TArray<FSlateTextureId> GlyphPageTextures;     // by font atlas page index
	TArray<uint32> GlyphPageRevisions;
	TMap<const ISlateRenderTarget*, FSlateTextureId> RenderTargetIds;

	FFrameBuffers Frames[FRHIDevice::MaxFramesInFlight];
	TMap<uint32, TSharedPtr<FRHIGraphicsPipeline>> Pipelines;
};
