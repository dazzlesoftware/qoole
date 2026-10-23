// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Renderer/NativeMeshDrawList.h"
#include "RHI.h"
#include <map>
struct FNativeMeshView
{
	uint32 Width = 1, Height = 1;
	float ProjectionScale = 1;
	bool bLit = true;
	float Light[3] = {0, -1, 1}, Up[3] = {0, 0, 1};
};
class FMaterialParameterCollectionInstance;
class FNativeMeshRenderer;
class FNativeMeshFrame
{
  public:
	~FNativeMeshFrame();
	FNativeMeshFrame(const FNativeMeshFrame&) = delete;
	FNativeMeshFrame& operator=(const FNativeMeshFrame&) = delete;

  private:
	friend class FNativeMeshRenderer;
	explicit FNativeMeshFrame(FRHIDevice& D) : Device(D)
	{
	}
	struct FDraw
	{
		FNativeMeshBatch Batch;
		TSharedPtr<FRHITexture> Color, Normal;
		TSharedPtr<FRHIGraphicsPipeline> GraphPipeline;
		TSharedPtr<FRHIBuffer> Parameters;
		TArray<TSharedPtr<FRHITexture>> GraphTextures;
	};
	FRHIDevice& Device;
	TSharedPtr<FRHIBuffer> Vertices, Indices;
	TSharedPtr<FRHIGraphicsPipeline> Opaque, Translucent;
	TArray<FDraw> Draws;
	TArray<size_t> OpaqueOrder, TranslucentOrder;
};
// RHI-only material renderer. Caller owns render targets, pass order and camera.
class FNativeMeshRenderer
{
  public:
	explicit FNativeMeshRenderer(FRHIDevice& D) : Device(D)
	{
	}
	~FNativeMeshRenderer();
	FRHIGraphicsPipeline* GetPipeline(EPixelFormat, bool Translucent = false);
	// Uploads outside render passes. Frame destruction defers GPU buffer release.
	TUniquePtr<FNativeMeshFrame> Prepare(const FNativeMeshDrawList&, EPixelFormat, FString& Error);
	// Inside a color + D32 depth pass. Draw transparency after all opaque content.
	void Draw(FRHICommandList&, const FNativeMeshFrame&, const FNativeMeshView&, bool Translucent);
	void Trim();
	void SetParameterCollectionInstance(std::shared_ptr<FMaterialParameterCollectionInstance>);

  private:
	bool GetTextures(const std::shared_ptr<const FLoadedMeshAsset>&, uint32, TSharedPtr<FRHITexture>&,
					 TSharedPtr<FRHITexture>&);
	TArray<std::shared_ptr<FMaterialParameterCollectionInstance>> Collections;
	FRHIDevice& Device;
	struct FEntry
	{
		std::shared_ptr<const FLoadedMeshAsset> Asset;
		TSharedPtr<FRHITexture> Color, Normal;
		TArray<TSharedPtr<FRHITexture>> GraphTextures;
		uint64 LastUsed = 0;
	};
	std::map<const FLoadedMaterialSlot*, FEntry> Materials;
	TSharedPtr<FRHITexture> White;
	TSharedPtr<FRHIGraphicsPipeline> OpaquePipeline, TranslucentPipeline;
	struct FGraphPipelineEntry
	{
		FMaterialAsset Material;
		EPixelFormat Format;
		TSharedPtr<FRHIGraphicsPipeline> Pipeline;
		uint64 LastUsed = 0;
	};
	TArray<FGraphPipelineEntry> GraphPipelines;
	EPixelFormat Format = EPixelFormat::Unknown;
	uint64 Frame = 0;
};
