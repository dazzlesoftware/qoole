// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Renderer/ProjectedSceneRenderer.h"
#include "Containers/ContainerAliases.h"
#include "Templates/SharedPointer.h"
#include "Geometry/StaticMesh.h"
#include <cmath>
#include <cstring>
#include <limits>
#include "SceneShaders_VSMain_DXIL.h"
#include "Shaders/SceneShaders_VSMain_SPIRV.h"
#include "SceneShaders_VSFoliage_DXIL.h"
#include "Shaders/SceneShaders_VSFoliage_SPIRV.h"
#include "SceneShaders_PSTextured_DXIL.h"
#include "Shaders/SceneShaders_PSTextured_SPIRV.h"
#include "SceneShaders_PSLandscape_DXIL.h"
#include "Shaders/SceneShaders_PSLandscape_SPIRV.h"
FRHIGraphicsPipeline* FProjectedSceneRenderer::GetSolidPipeline(EPixelFormat TargetFormat)
{
	if (SolidPipeline && SolidFormat == TargetFormat)
	{
		return SolidPipeline.get();
	}
	FRHIGraphicsPipelineDesc Desc;
	const bool bVulkan = Device.GetBackend() == ERHIBackend::Vulkan;
	Desc.VertexShader = bVulkan ? FRHIShaderBytecode{g_SceneShaders_VSMain_SPIRV, sizeof(g_SceneShaders_VSMain_SPIRV)}
	                            : FRHIShaderBytecode{g_SceneShaders_VSMain_DXIL, sizeof(g_SceneShaders_VSMain_DXIL)};
	Desc.PixelShader = bVulkan
	    ? FRHIShaderBytecode{g_SceneShaders_PSTextured_SPIRV, sizeof(g_SceneShaders_PSTextured_SPIRV)}
	    : FRHIShaderBytecode{g_SceneShaders_PSTextured_DXIL, sizeof(g_SceneShaders_PSTextured_DXIL)};
	Desc.VertexAttributes = {
	    {"POSITION", 0, EVertexFormat::Float3, offsetof(FProjectedVertex, X)},
	    {"TEXCOORD", 0, EVertexFormat::Float2, offsetof(FProjectedVertex, U)},
	    {"COLOR", 0, EVertexFormat::UByte4N, offsetof(FProjectedVertex, Color)},
	};
	Desc.VertexStride = sizeof(FProjectedVertex);
	Desc.BlendMode = EBlendMode::Opaque;
	Desc.RenderTargetFormat = TargetFormat;
	Desc.DepthFormat = EPixelFormat::D32_FLOAT;
	// The shaders' constants are 64 bytes (SceneShaders.hlsl), faces use 16.
	Desc.PushConstantSize = 64;
	Desc.DebugName = "Projected scene geometry";
	if (SolidPipeline)
		Device.DeferRelease(SolidPipeline);
	SolidPipeline = Device.CreateGraphicsPipeline(Desc);
	SolidFormat = TargetFormat;
	return SolidPipeline.get();
}

FRHIGraphicsPipeline* FProjectedSceneRenderer::GetLandscapePipeline(EPixelFormat TargetFormat)
{
	if (LandscapePipeline && LandscapeFormat == TargetFormat)
	{
		return LandscapePipeline.get();
	}
	FRHIGraphicsPipeline* Solid = GetSolidPipeline(TargetFormat);
	if (!Solid)
	{
		return nullptr;
	}
	FRHIGraphicsPipelineDesc Desc = Solid->GetDesc();
	const bool bVulkan = Device.GetBackend() == ERHIBackend::Vulkan;
	Desc.PixelShader = bVulkan
	    ? FRHIShaderBytecode{g_SceneShaders_PSLandscape_SPIRV, sizeof(g_SceneShaders_PSLandscape_SPIRV)}
	    : FRHIShaderBytecode{g_SceneShaders_PSLandscape_DXIL, sizeof(g_SceneShaders_PSLandscape_DXIL)};
	// Slot 0 unused, then the weights and the layers' textures (which repeat).
	Desc.NumTextures = 3;
	Desc.SamplerAddress = ESamplerAddress::Wrap;
	Desc.DebugName = "Landscape layer blending";
	if (LandscapePipeline)
		Device.DeferRelease(LandscapePipeline);
	LandscapePipeline = Device.CreateGraphicsPipeline(Desc);
	LandscapeFormat = TargetFormat;
	return LandscapePipeline.get();
}

namespace
{
	// A corner of a mesh's triangle for the instanced pipeline.
	struct FInstancedVertex
	{
		float Position[3];
		float Normal[3];
		uint32 Color; // RGBA8, R in the low byte
	};
} // namespace

FRHIGraphicsPipeline* FProjectedSceneRenderer::GetInstancedPipeline(EPixelFormat TargetFormat, bool Mirrored)
{
	if (Mirrored)
	{
		FRHIGraphicsPipeline* Base = GetInstancedPipeline(TargetFormat, false);
		if (!Base)
			return nullptr;
		if (!MirroredInstancedPipeline)
		{
			FRHIGraphicsPipelineDesc Desc = Base->GetDesc();
			Desc.CullMode = ECullMode::Back;
			Desc.DebugName = "Mirrored instanced scene meshes";
			MirroredInstancedPipeline = Device.CreateGraphicsPipeline(Desc);
		}
		return MirroredInstancedPipeline.get();
	}
	if (InstancedPipeline && InstancedFormat == TargetFormat)
	{
		return InstancedPipeline.get();
	}
	FRHIGraphicsPipeline* Solid = GetSolidPipeline(TargetFormat);
	if (!Solid)
	{
		return nullptr;
	}
	FRHIGraphicsPipelineDesc Desc = Solid->GetDesc();
	const bool bVulkan = Device.GetBackend() == ERHIBackend::Vulkan;
	Desc.VertexShader = bVulkan
	    ? FRHIShaderBytecode{g_SceneShaders_VSFoliage_SPIRV, sizeof(g_SceneShaders_VSFoliage_SPIRV)}
	    : FRHIShaderBytecode{g_SceneShaders_VSFoliage_DXIL, sizeof(g_SceneShaders_VSFoliage_DXIL)};
	// The mesh's corners, then each copy's object-to-view rows.
	Desc.VertexAttributes = {
	    {"POSITION", 0, EVertexFormat::Float3, offsetof(FInstancedVertex, Position), 0},
	    {"NORMAL", 0, EVertexFormat::Float3, offsetof(FInstancedVertex, Normal), 0},
	    {"COLOR", 0, EVertexFormat::UByte4N, offsetof(FInstancedVertex, Color), 0},
	    {"TEXCOORD", 1, EVertexFormat::Float4, 0, 1},
	    {"TEXCOORD", 2, EVertexFormat::Float4, 16, 1},
	    {"TEXCOORD", 3, EVertexFormat::Float4, 32, 1},
	};
	Desc.VertexStride = sizeof(FInstancedVertex);
	Desc.InstanceStride = 12 * sizeof(float);
	// Closed meshes (and two-sided blades): back faces need not be drawn.
	// Counterclockwise from outside in map space, their outsides are
	// counterclockwise on screen (y flipped): "front" to the RHI.
	Desc.CullMode = ECullMode::Front;
	Desc.DebugName = "Instanced scene meshes";
	if (InstancedPipeline)
		Device.DeferRelease(InstancedPipeline);
	if (MirroredInstancedPipeline)
	{
		Device.DeferRelease(MirroredInstancedPipeline);
		MirroredInstancedPipeline.reset();
	}
	InstancedPipeline = Device.CreateGraphicsPipeline(Desc);
	InstancedFormat = TargetFormat;
	return InstancedPipeline.get();
}

const FInstanceMeshBuffers* FProjectedSceneRenderer::GetMeshBuffers(const std::shared_ptr<const StaticMesh>& Snapshot)
{
	if (!Snapshot)
		return nullptr;
	const StaticMesh& Mesh = *Snapshot;
	if (const std::map<const StaticMesh*, FInstanceMeshBuffers>::iterator Found = MeshBuffers.find(&Mesh);
	    Found != MeshBuffers.end())
	{
		Found->second.LastUsed = Frame;
		return &Found->second;
	}
	// A vertex a corner: the triangles' own normals there.
	TArray<FInstancedVertex> Corners;
	for (const StaticMesh::Triangle& Triangle : Mesh.GetTriangles())
	{
		for (int Corner = 0; Corner < 3; ++Corner)
		{
			if (Triangle.v[Corner] < 0 || Triangle.v[Corner] >= Mesh.GetNumVertices())
				return nullptr;
			const Vector3d& P = Mesh.GetVertex(Triangle.v[Corner]);
			if (!std::isfinite(P.GetX()) || !std::isfinite(P.GetY()) || !std::isfinite(P.GetZ()))
				return nullptr;

			const Vector3d& N = Triangle.norm[Corner];
			if (!std::isfinite(N.GetX()) || !std::isfinite(N.GetY()) || !std::isfinite(N.GetZ()))
				return nullptr;
			if (Mesh.HasVertexColors() && static_cast<size_t>(Triangle.v[Corner]) >= Mesh.GetVertexColors().size())
				return nullptr;
			FInstancedVertex Vertex{{P.GetX(), P.GetY(), P.GetZ()}, {N.GetX(), N.GetY(), N.GetZ()},
			    Mesh.HasVertexColors() ? Mesh.GetVertexColor(Triangle.v[Corner]) : 0xFFFFFFFFu};
			Corners.push_back(Vertex);
		}
	}
	if (Corners.empty())
	{
		return nullptr;
	}
	TArray<uint32> Order(Corners.size());
	for (SIZE_T Index = 0; Index < Order.size(); ++Index)
	{
		Order[Index] = static_cast<uint32>(Index);
	}
	FInstanceMeshBuffers Buffers;
	Buffers.Mesh = Snapshot;
	Buffers.LastUsed = Frame;
	FRHIBufferDesc VertexDesc;
	VertexDesc.Size = Corners.size() * sizeof(FInstancedVertex);
	VertexDesc.Usage = EBufferUsage::Vertex;
	VertexDesc.Memory = EBufferMemory::Default;
	VertexDesc.InitialData = Corners.data();
	VertexDesc.DebugName = "Instanced mesh";
	Buffers.Vertices = Device.CreateBuffer(VertexDesc);
	FRHIBufferDesc IndexDesc;
	IndexDesc.Size = Order.size() * sizeof(uint32);
	IndexDesc.Usage = EBufferUsage::Index;
	IndexDesc.Memory = EBufferMemory::Default;
	IndexDesc.InitialData = Order.data();
	IndexDesc.DebugName = "Instanced mesh indices";
	Buffers.Indices = Device.CreateBuffer(IndexDesc);
	Buffers.IndexCount = static_cast<uint32>(Order.size());
	if (!Buffers.Vertices || !Buffers.Indices)
	{
		if (Buffers.Vertices)
			Device.DeferRelease(Buffers.Vertices);
		if (Buffers.Indices)
			Device.DeferRelease(Buffers.Indices);
		return nullptr;
	}
	return &(MeshBuffers[&Mesh] = std::move(Buffers));
}

FProjectedSceneRenderer::~FProjectedSceneRenderer()
{
	for (std::shared_ptr<FRHIGraphicsPipeline>* P :
	    {&SolidPipeline, &LandscapePipeline, &InstancedPipeline, &MirroredInstancedPipeline})
		if (*P)
			Device.DeferRelease(*P);
	if (WhiteTexture)
		Device.DeferRelease(WhiteTexture);
	for (std::pair<const StaticMesh* const, FInstanceMeshBuffers>& MeshBinding : MeshBuffers)
	{
		FInstanceMeshBuffers& Buffers = MeshBinding.second;

		Device.DeferRelease(Buffers.Vertices);
		Device.DeferRelease(Buffers.Indices);
	}
	for (std::pair<const unsigned long long, std::unique_ptr<FProjectedSceneRenderer::FLandscapeEntry>>& LandBinding :
	    Landscapes)
	{
		std::unique_ptr<FProjectedSceneRenderer::FLandscapeEntry>& Entry = LandBinding.second;

		Release(*Entry);
	}
}

void FProjectedSceneRenderer::Release(FLandscapeEntry& Entry)
{
	for (TSharedPtr<FRHITexture>* Texture : {&Entry.Texture, &Entry.WeightTexture, &Entry.LayerTexture})
	{
		if (*Texture)
		{
			Device.DeferRelease(*Texture);
			Texture->reset();
		}
	}
}

FProjectedSceneRenderer::FLandscapeEntry& FProjectedSceneRenderer::FindEntry(uint64 Key)
{
	std::unique_ptr<FLandscapeEntry>& Entry = Landscapes[Key];
	if (!Entry)
	{
		Entry = std::make_unique<FLandscapeEntry>();
	}
	Entry->LastUsed = Frame;
	return *Entry;
}

bool FProjectedSceneRenderer::GetLandscapeLayers(
    uint64 Key, const std::shared_ptr<const Landscape>& Land, FRHITexture*& OutWeights, FRHITexture*& OutLayers)
{
	OutWeights = OutLayers = nullptr;
	if (!bBlendLandscapeLayers || !Land || !FLandscapeLayerImages::CanBlend(*Land))
	{
		return false;
	}
	FLandscapeEntry& Entry = FindEntry(Key);
	FLandscapeLayerImages& Images = Entry.Layers;

	// The weights: a two-slice texture of the grid's size; a stroke copies
	// just the points it changed.
	const FIntRect Changed = Images.UpdateWeights(Land);
	const uint32 PointsX = static_cast<uint32>(Images.GetPointsX());
	const uint32 PointsY = static_cast<uint32>(Images.GetPointsY());
	if (!Entry.WeightTexture || Entry.WeightTexture->GetWidth() != PointsX ||
	    Entry.WeightTexture->GetHeight() != PointsY)
	{
		if (Entry.WeightTexture)
		{
			Device.DeferRelease(Entry.WeightTexture);
		}
		FRHITextureDesc Desc;
		Desc.Width = PointsX;
		Desc.Height = PointsY;
		Desc.ArraySize = 2;
		Desc.InitialData = Images.GetWeights().data();
		Desc.DebugName = "Landscape weights";
		Entry.WeightTexture = Device.CreateTexture(Desc);
	}
	else if (!Changed.IsEmpty())
	{
		const uint32 Pitch = PointsX * 4;
		for (uint32 Slice = 0; Slice < 2; ++Slice)
		{
			const uint8* First = Images.GetWeights().data() + SIZE_T(Pitch) * PointsY * Slice +
			    SIZE_T(Changed.Top) * Pitch + SIZE_T(Changed.Left) * 4;
			Device.UpdateTexture(*Entry.WeightTexture, Changed, First, Pitch, Slice);
		}
	}

	// The layers' textures: made again when a layer looks different.
	if (Images.UpdateTextures(*Land) || !Entry.LayerTexture)
	{
		if (Entry.LayerTexture)
		{
			Device.DeferRelease(Entry.LayerTexture);
		}
		FRHITextureDesc Desc;
		Desc.Width = Desc.Height = static_cast<uint32>(Images.GetTextureSize());
		Desc.MipLevels = static_cast<uint32>(Images.GetMipLevels());
		// An array of one would read as a plain texture: two at least.
		Desc.ArraySize = static_cast<uint32>(FMath::Max(Images.GetNumSlices(), 2));
		TArray<uint8> Pixels = Images.GetTextures();
		Pixels.resize(GetMipChainBytes(Desc.Format, Desc.Width, Desc.Height, Desc.MipLevels) * Desc.ArraySize, 255);
		Desc.InitialData = Pixels.data();
		Desc.DebugName = "Landscape layer textures";
		Entry.LayerTexture = Device.CreateTexture(Desc);
	}
	OutWeights = Entry.WeightTexture.get();
	OutLayers = Entry.LayerTexture.get();
	return OutWeights && OutLayers;
}

FRHITexture* FProjectedSceneRenderer::GetLandscapeTexture(uint64 Key, const std::shared_ptr<const Landscape>& Land)
{
	if (!Land || !FLandscapeTextureLibrary::IsTextured(*Land))
	{
		return nullptr;
	}
	FLandscapeEntry* Entry = &FindEntry(Key);
	const FIntRect Baked = Entry->Bake.Update(Land);
	const uint32 Width = static_cast<uint32>(Entry->Bake.GetWidth());
	const uint32 Height = static_cast<uint32>(Entry->Bake.GetHeight());
	if (!Entry->Texture || Entry->Texture->GetWidth() != Width || Entry->Texture->GetHeight() != Height)
	{
		if (Entry->Texture)
		{
			Device.DeferRelease(Entry->Texture);
		}
		FRHITextureDesc Desc;
		Desc.Width = Width;
		Desc.Height = Height;
		Desc.InitialData = Entry->Bake.GetPixels().data();
		Desc.DebugName = "Landscape layers";
		Entry->Texture = Device.CreateTexture(Desc);
	}
	else if (!Baked.IsEmpty())
	{
		const uint32 Pitch = Width * 4;
		const uint8* First = Entry->Bake.GetPixels().data() + static_cast<SIZE_T>(Baked.Top) * Pitch +
		    static_cast<SIZE_T>(Baked.Left) * 4;
		Device.UpdateTexture(*Entry->Texture, Baked, First, Pitch);
	}
	return Entry->Texture.get();
}

void FProjectedSceneRenderer::Trim()
{
	// Several views draw each frame; a landscape none drew for a few
	// seconds (deleted, or its layers cleared) goes.
	++Frame;

	for (std::map<const StaticMesh*, FInstanceMeshBuffers>::iterator It = MeshBuffers.begin(); It != MeshBuffers.end();)
	{
		if (Frame - It->second.LastUsed > 300)
		{
			Device.DeferRelease(It->second.Vertices);
			Device.DeferRelease(It->second.Indices);
			It = MeshBuffers.erase(It);
		}
		else
			++It;
	}

	for (std::map<uint64, std::unique_ptr<FLandscapeEntry>>::iterator It = Landscapes.begin(); It != Landscapes.end();)
	{
		if (Frame - It->second->LastUsed > 300)
		{
			Release(*It->second);
			It = Landscapes.erase(It);
		}
		else
		{
			++It;
		}
	}
}

const FLandscapeBake* FProjectedSceneRenderer::FindLandscapeBake(uint64 Key) const
{
	const std::map<uint64, std::unique_ptr<FLandscapeEntry>>::const_iterator Found = Landscapes.find(Key);
	return Found != Landscapes.end() ? &Found->second->Bake : nullptr;
}

FRHITexture* FProjectedSceneRenderer::GetWhiteTexture()
{
	if (!WhiteTexture)
	{
		// The pipelines' texture slot must hold something; the faces are not textured yet.
		const uint32 White = 0xFFFFFFFFu;
		FRHITextureDesc Desc;
		Desc.InitialData = &White;
		Desc.DebugName = "Scene white";
		WhiteTexture = Device.CreateTexture(Desc);
	}
	return WhiteTexture.get();
}

FProjectedSceneFrame::~FProjectedSceneFrame()
{
	for (FProjectedSceneFrame::FDraw& Draw : Draws)
		for (std::shared_ptr<FRHITexture>* T : {&Draw.Texture, &Draw.Weights, &Draw.Layers})
			if (*T)
				Device.DeferRelease(*T);
	for (FProjectedSceneFrame::FInstanceDraw& Draw : Instances)
	{
		Device.DeferRelease(Draw.Mesh.Vertices);
		Device.DeferRelease(Draw.Mesh.Indices);
	}
	for (std::shared_ptr<FRHIBuffer>* B : {&Vertices, &Indices, &InstanceRows})
		if (*B)
			Device.DeferRelease(*B);
	for (std::shared_ptr<FRHIGraphicsPipeline>* P : {&Solid, &LandscapePipeline, &Instanced, &MirroredInstanced})
		if (*P)
			Device.DeferRelease(*P);
	if (White)
		Device.DeferRelease(White);
}

TUniquePtr<FProjectedSceneFrame> FProjectedSceneRenderer::Prepare(const TArray<FProjectedVertex>& Vertices,
    const TArray<FProjectedTriangleBatch>& Batches, const TArray<FMeshInstanceBatch>& Instances, EPixelFormat Format,
    FString& Error)
{
	Error.Empty();
	const TFunction<TUniquePtr<FProjectedSceneFrame>(const char*)> Fail =
	    [&](const char* Why) -> TUniquePtr<FProjectedSceneFrame>
	{
		Error = Why;
		return {};
	};
	if (Vertices.size() > std::numeric_limits<uint32>::max())
		return Fail("Too many projected vertices.");
	for (const FProjectedVertex& V : Vertices)
		if (!std::isfinite(V.X) || !std::isfinite(V.Y) || !std::isfinite(V.Q) || V.Q <= 0 || !std::isfinite(V.U) ||
		    !std::isfinite(V.V))
			return Fail("Invalid projected vertex.");
	for (const FProjectedTriangleBatch& B : Batches)
		if (B.FirstVertex > Vertices.size() || B.NumVertices > Vertices.size() - B.FirstVertex ||
		    B.NumVertices % 3 != 0)
			return Fail("Invalid projected triangle range.");
	size_t Total = 0;
	for (const FMeshInstanceBatch& B : Instances)
	{
		if (!B.Mesh || B.Rows.size() % 12 != 0 || B.Rows.size() / 12 > std::numeric_limits<uint32>::max() - Total)
			return Fail("Invalid mesh instance batch.");
		for (size_t I = 0; I < B.Rows.size(); I += 12)
		{
			bool Mirrored = false;
			if (!ValidateMeshInstanceTransform(B.Rows.data() + I, Mirrored) || Mirrored != B.bMirrored)
				return Fail("Invalid or mixed-winding instance transform; use AppendMeshInstance.");
		}
		Total += B.Rows.size() / 12;
	}
	TUniquePtr<FProjectedSceneFrame> Out = TUniquePtr<FProjectedSceneFrame>(new FProjectedSceneFrame(Device));
	GetSolidPipeline(Format);
	Out->Solid = SolidPipeline;
	if (!Out->Solid)
		return Fail("Projected scene pipeline is unavailable.");
	GetWhiteTexture();
	Out->White = WhiteTexture;
	if (!Out->White)
		return Fail("Projected scene white texture is unavailable.");
	if (!Vertices.empty())
	{
		FRHIBufferDesc D;
		D.Size = Vertices.size() * sizeof(FProjectedVertex);
		D.Usage = EBufferUsage::Vertex;
		D.InitialData = Vertices.data();
		D.DebugName = "Projected scene vertices";
		Out->Vertices = Device.CreateBuffer(D);
		TArray<uint32> Indices(Vertices.size());
		for (size_t I = 0; I < Indices.size(); I++)
			Indices[I] = static_cast<uint32>(I);
		D.Size = Indices.size() * sizeof(uint32);
		D.Usage = EBufferUsage::Index;
		D.InitialData = Indices.data();
		D.DebugName = "Projected scene indices";
		Out->Indices = Device.CreateBuffer(D);
		if (!Out->Vertices || !Out->Indices)
			return Fail("Projected scene buffer creation failed.");
	}
	for (const FProjectedTriangleBatch& B : Batches)
	{
		if (!B.NumVertices)
			continue;
		FProjectedSceneFrame::FDraw Draw;
		Draw.First = B.FirstVertex;
		Draw.Count = B.NumVertices;
		if (B.Terrain)
		{
			FRHITexture* Weights = nullptr;
			FRHITexture* Layers = nullptr;
			if (GetLandscapePipeline(Format) && GetLandscapeLayers(B.LandscapeKey, B.Terrain, Weights, Layers))
			{
				FProjectedSceneRenderer::FLandscapeEntry const& E = *Landscapes.at(B.LandscapeKey);
				Draw.Weights = E.WeightTexture;
				Draw.Layers = E.LayerTexture;
				FLandscapeLayerImages::GetConstants(*B.Terrain, Draw.Constants);
				Out->LandscapePipeline = LandscapePipeline;
			}
			else if (GetLandscapeTexture(B.LandscapeKey, B.Terrain))
				Draw.Texture = Landscapes.at(B.LandscapeKey)->Texture;
		}
		Out->Draws.push_back(std::move(Draw));
	}
	if (Total)
	{
		if (!GetInstancedPipeline(Format))
			return Fail("Instance pipeline is unavailable.");
		Out->Instanced = InstancedPipeline;
		TArray<float> Rows;
		Rows.reserve(Total * 12);
		uint32 First = 0;
		for (const FMeshInstanceBatch& B : Instances)
		{
			if (!B.Count())
				continue;
			const FInstanceMeshBuffers* Mesh = GetMeshBuffers(B.Mesh);
			if (!Mesh)
				return Fail("Instance mesh buffers could not be created.");
			if (B.bMirrored)
			{
				if (!GetInstancedPipeline(Format, true))
					return Fail("Mirrored instance pipeline is unavailable.");
				Out->MirroredInstanced = MirroredInstancedPipeline;
			}
			Out->Instances.push_back({*Mesh, First, B.Count(), B.bMirrored});
			Rows.insert(Rows.end(), B.Rows.begin(), B.Rows.end());
			First += B.Count();
		}
		FRHIBufferDesc D;
		D.Size = Rows.size() * sizeof(float);
		D.Usage = EBufferUsage::Vertex;
		D.InitialData = Rows.data();
		D.DebugName = "Scene instance rows";
		Out->InstanceRows = Device.CreateBuffer(D);
		if (!Out->InstanceRows)
			return Fail("Instance row buffer could not be created.");
	}
	return Out;
}
void FProjectedSceneRenderer::DrawGeometry(
    FRHICommandList& Cmd, const FProjectedSceneFrame& Prepared, const FProjectedSceneView& View)
{
	if (Prepared.Draws.empty() || !View.Width || !View.Height)
		return;
	float Constants[16] = {2.0f / View.Width, 2.0f / View.Height};
	Cmd.SetVertexBuffer(*Prepared.Vertices, 0, sizeof(FProjectedVertex));
	Cmd.SetIndexBuffer(*Prepared.Indices, 0, EIndexFormat::UInt32);
	for (const FProjectedSceneFrame::FDraw& Draw : Prepared.Draws)
	{
		Cmd.SetPipeline(Draw.Layers ? *Prepared.LandscapePipeline : *Prepared.Solid);
		if (Draw.Layers)
		{
			std::memcpy(Constants + 4, Draw.Constants, sizeof(Draw.Constants));
			Cmd.SetPushConstants(Constants, sizeof(Constants));
			Cmd.SetTexture(*Prepared.White, 0);
			Cmd.SetTexture(*Draw.Weights, 1);
			Cmd.SetTexture(*Draw.Layers, 2);
		}
		else
		{
			Cmd.SetPushConstants(Constants, sizeof(Constants));
			Cmd.SetTexture(Draw.Texture ? *Draw.Texture : *Prepared.White, 0);
		}
		Cmd.DrawIndexed(Draw.Count, Draw.First, 0);
	}
}
void FProjectedSceneRenderer::DrawInstances(
    FRHICommandList& Cmd, const FProjectedSceneFrame& Prepared, const FProjectedSceneView& View)
{
	if (Prepared.Instances.empty() || !View.Width || !View.Height)
		return;
	const float C[16] = {2.0f / View.Width, 2.0f / View.Height, 0, 0, View.CenterX, View.CenterY, View.ProjectionScale,
	    View.bLit ? 1.0f : 0.0f, View.Light[0], View.Light[1], View.Light[2], 0, View.Up[0], View.Up[1], View.Up[2], 0};
	FRHIGraphicsPipeline* Bound = nullptr;
	for (const FProjectedSceneFrame::FInstanceDraw& Draw : Prepared.Instances)
	{
		FRHIGraphicsPipeline* Wanted = Draw.bMirrored ? Prepared.MirroredInstanced.get() : Prepared.Instanced.get();
		if (Wanted != Bound)
		{
			Cmd.SetPipeline(*Wanted);
			Cmd.SetPushConstants(C, sizeof(C));
			Cmd.SetTexture(*Prepared.White, 0);
			Bound = Wanted;
		}
		Cmd.SetVertexBuffer(*Draw.Mesh.Vertices, 0, sizeof(FInstancedVertex), 0);
		Cmd.SetVertexBuffer(*Prepared.InstanceRows, 0, 12 * sizeof(float), 1);
		Cmd.SetIndexBuffer(*Draw.Mesh.Indices, 0, EIndexFormat::UInt32);
		Cmd.DrawIndexedInstanced(Draw.Mesh.IndexCount, Draw.Count, 0, 0, Draw.First);
	}
}
