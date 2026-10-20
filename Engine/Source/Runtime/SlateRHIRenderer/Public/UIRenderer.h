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
// atlas pages mirrored from the FFontCache, and per-frame vertex/index
// buffers.

#pragma once

#include "ElementBatcher.h"
#include "RHI.h"

// An RHI render target that UI can show with a Viewport element.
class FUIRenderTarget final : public IUIRenderTarget
{
public:
	explicit FUIRenderTarget(TSharedPtr<FRHITexture> InTexture) : Texture(std::move(InTexture)) {}
	FIntPoint GetSize() const override { return FIntPoint(static_cast<int32>(Texture->GetWidth()), static_cast<int32>(Texture->GetHeight())); }
	FRHITexture& GetTexture() const { return *Texture; }

private:
	TSharedPtr<FRHITexture> Texture;
};

struct FUIRenderStats
{
	int32 Elements = 0;
	int32 Batches = 0;
	int32 Vertices = 0;
	int32 Indices = 0;
};

class FUIRenderer final : public IUIResourceResolver
{
public:
	FUIRenderer(FRHIDevice& InDevice, FFontCache& InFontCache);
	~FUIRenderer() override;

	bool Initialize();

	void BeginFrame();

	// Draws a list into a swapchain (or a render target). The pass begins and
	// ends inside; ClearColor clears first.
	void Render(FRHICommandList& CommandList, FRHISwapChain& Target, const FDrawElementList& Elements, const TOptional<FLinearColor>& ClearColor);
	void Render(FRHICommandList& CommandList, FRHITexture& Target, const FDrawElementList& Elements, const TOptional<FLinearColor>& ClearColor);

	TSharedRef<FUIRenderTarget> CreateRenderTarget(int32 Width, int32 Height, const char* DebugName = nullptr);

	const FUIRenderStats& GetLastStats() const { return LastStats; }
	FElementBatcher& GetBatcher() { return Batcher; }

	// IUIResourceResolver
	FUITextureId GetWhiteTexture() override { return WhiteTextureId; }
	FUIResolvedTexture ResolveImage(const FUIImage& Image) override;
	FUIResolvedTexture ResolveRenderTarget(IUIRenderTarget& Target) override;
	FUITextureId GetGlyphPageTexture(int32 PageIndex) override;

	// Images up to this size (each side) go into shared atlas pages.
	static constexpr int32 MaxAtlasedImageSize = 256;
	static constexpr int32 ImageAtlasPageSize = 1024;

private:
	struct FImageEntry
	{
		uint32 Revision = 0;
		FUITextureId Texture = 0;
		FRect UVRect;
		FIntRect AtlasSlot;  // in its page, when atlased
		int32 AtlasPage = -1;
	};

	struct FImagePage
	{
		TSharedPtr<FRHITexture> Texture;
		FAtlasPacker Packer{ ImageAtlasPageSize, ImageAtlasPageSize, 1 };
		FUITextureId Id = 0;
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
	FRHIGraphicsPipeline* GetPipeline(EUIShader Shader, EUIBlend Blend, EPixelFormat Format);
	FUITextureId RegisterTexture(TSharedPtr<FRHITexture> Texture);
	void UploadImage(const FUIImage& Image, FImageEntry& Entry);
	bool EnsureCapacity(TSharedPtr<FRHIBuffer>& Buffer, uint64 Required, EBufferUsage Usage, const char* Name);

	FRHIDevice& Device;
	FFontCache& FontCache;
	FElementBatcher Batcher;
	FBatchedElements Batched;
	FUIRenderStats LastStats;

	TMap<FUITextureId, TSharedPtr<FRHITexture>> Textures;
	FUITextureId NextTextureId = 1;
	FUITextureId WhiteTextureId = 0;

	TMap<uint64, FImageEntry> Images;
	TArray<FImagePage> ImagePages;
	TArray<FUITextureId> GlyphPageTextures;     // by font atlas page index
	TArray<uint32> GlyphPageRevisions;
	TMap<const IUIRenderTarget*, FUITextureId> RenderTargetIds;

	FFrameBuffers Frames[FRHIDevice::MaxFramesInFlight];
	TMap<uint32, TSharedPtr<FRHIGraphicsPipeline>> Pipelines;
};
