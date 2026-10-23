// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Renderer/NativeMeshRenderer.h"
#include "Renderer/LandscapeTextures.h"
#include "Renderer/MeshInstanceDrawList.h"

// Transitional projected geometry. X/Y are target pixels, Q is reciprocal depth.
struct FProjectedVertex
{
	float X, Y, Q, U, V;
	uint32 Color;
};
struct FProjectedTriangleBatch
{
	uint64 LandscapeKey = 0;
	std::shared_ptr<const Landscape> Terrain;
	uint32 FirstVertex = 0, NumVertices = 0;
};
struct FProjectedSceneView : FNativeMeshView
{
	float CenterX = 0, CenterY = 0;
};
struct FInstanceMeshBuffers
{
	std::shared_ptr<const StaticMesh> Mesh;
	TSharedPtr<FRHIBuffer> Vertices, Indices;
	uint32 IndexCount = 0;
	uint64 LastUsed = 0;
};
class FProjectedSceneRenderer;
class FProjectedSceneFrame
{
  public:
	~FProjectedSceneFrame();
	FProjectedSceneFrame(const FProjectedSceneFrame&) = delete;
	FProjectedSceneFrame& operator=(const FProjectedSceneFrame&) = delete;

  private:
	friend class FProjectedSceneRenderer;
	explicit FProjectedSceneFrame(FRHIDevice& D) : Device(D)
	{
	}
	struct FDraw
	{
		uint32 First = 0, Count = 0;
		TSharedPtr<FRHITexture> Texture, Weights, Layers;
		float Constants[12] = {};
	};
	struct FInstanceDraw
	{
		FInstanceMeshBuffers Mesh;
		uint32 First = 0, Count = 0;
		bool bMirrored = false;
	};
	FRHIDevice& Device;
	TSharedPtr<FRHIBuffer> Vertices, Indices, InstanceRows;
	TSharedPtr<FRHIGraphicsPipeline> Solid, LandscapePipeline, Instanced, MirroredInstanced;
	TSharedPtr<FRHITexture> White;
	TArray<FDraw> Draws;
	TArray<FInstanceDraw> Instances;
};
// RHI-only projected scene renderer. Prepare outside passes; draw in a color+D32 pass.
// Render keys are caller-owned identities scoped to this renderer, never editor objects.
class FProjectedSceneRenderer
{
  public:
	explicit FProjectedSceneRenderer(FRHIDevice& D) : Device(D)
	{
	}
	~FProjectedSceneRenderer();
	bool bBlendLandscapeLayers = true;
	FRHIGraphicsPipeline* GetSolidPipeline(EPixelFormat);
	FRHIGraphicsPipeline* GetLandscapePipeline(EPixelFormat);
	FRHIGraphicsPipeline* GetInstancedPipeline(EPixelFormat, bool bMirrored = false);
	const FInstanceMeshBuffers* GetMeshBuffers(const std::shared_ptr<const StaticMesh>&);
	FRHITexture* GetWhiteTexture();
	bool GetLandscapeLayers(uint64 Key, const std::shared_ptr<const Landscape>&, FRHITexture*&, FRHITexture*&);
	FRHITexture* GetLandscapeTexture(uint64 Key, const std::shared_ptr<const Landscape>&);
	int32 GetNumLandscapeTextures() const
	{
		return static_cast<int32>(Landscapes.size());
	}
	const FLandscapeBake* FindLandscapeBake(uint64 Key) const;
	TUniquePtr<FProjectedSceneFrame> Prepare(const TArray<FProjectedVertex>&, const TArray<FProjectedTriangleBatch>&,
											 const TArray<FMeshInstanceBatch>&, EPixelFormat, FString& Error);
	void DrawGeometry(FRHICommandList&, const FProjectedSceneFrame&, const FProjectedSceneView&);
	void DrawInstances(FRHICommandList&, const FProjectedSceneFrame&, const FProjectedSceneView&);
	void Trim();

  private:
	struct FLandscapeEntry
	{
		FLandscapeBake Bake;
		TSharedPtr<FRHITexture> Texture;
		FLandscapeLayerImages Layers;
		TSharedPtr<FRHITexture> WeightTexture, LayerTexture;
		uint64 LastUsed = 0;
	};
	FLandscapeEntry& FindEntry(uint64 Key);
	void Release(FLandscapeEntry&);
	FRHIDevice& Device;
	TSharedPtr<FRHITexture> WhiteTexture;
	TSharedPtr<FRHIGraphicsPipeline> SolidPipeline, LandscapePipeline, InstancedPipeline, MirroredInstancedPipeline;
	EPixelFormat SolidFormat = EPixelFormat::Unknown, LandscapeFormat = EPixelFormat::Unknown,
				 InstancedFormat = EPixelFormat::Unknown;
	std::map<const StaticMesh*, FInstanceMeshBuffers> MeshBuffers;
	std::map<uint64, std::unique_ptr<FLandscapeEntry>> Landscapes;
	uint64 Frame = 0;
};
