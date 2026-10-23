// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Renderer/NativeMeshRenderer.h"
#include "Containers/ContainerAliases.h"
#include "Templates/SharedPointer.h"
#include "Internationalization/TextLocalization.h"
#include "Materials/MaterialParameterCollection.h"
#include "Renderer/RenderTextureMips.h"
#include "MeshMaterial_VSMaterial_DXIL.h"
#include "MeshMaterial_PSMaterial_DXIL.h"
#include "Shaders/MeshMaterial_VSMaterial_SPIRV.h"
#include "Shaders/MeshMaterial_PSMaterial_SPIRV.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>
FRHIGraphicsPipeline* FNativeMeshRenderer::GetPipeline(EPixelFormat TargetFormat, bool Translucent)
{
	if (Translucent)
	{
		const FRHIGraphicsPipeline* Opaque = GetPipeline(TargetFormat);
		if (!Opaque)
			return nullptr;
		if (!TranslucentPipeline)
		{
			FRHIGraphicsPipelineDesc Desc = Opaque->GetDesc();
			Desc.BlendMode = EBlendMode::AlphaBlend;
			Desc.bDepthWrite = false;
			Desc.DebugName = "Translucent mesh materials";
			TranslucentPipeline = Device.CreateGraphicsPipeline(Desc);
		}
		return TranslucentPipeline.get();
	}
	if (OpaquePipeline && Format == TargetFormat)
		return OpaquePipeline.get();
	FRHIGraphicsPipelineDesc Desc;
	Desc.RenderTargetFormat = TargetFormat;
	Desc.DepthFormat = EPixelFormat::D32_FLOAT;
	Desc.PushConstantSize = 64;
	const bool Vulkan = Device.GetBackend() == ERHIBackend::Vulkan;
	Desc.VertexShader = Vulkan
	    ? FRHIShaderBytecode{g_MeshMaterial_VSMaterial_SPIRV, sizeof(g_MeshMaterial_VSMaterial_SPIRV)}
	    : FRHIShaderBytecode{g_MeshMaterial_VSMaterial_DXIL, sizeof(g_MeshMaterial_VSMaterial_DXIL)};
	Desc.PixelShader = Vulkan
	    ? FRHIShaderBytecode{g_MeshMaterial_PSMaterial_SPIRV, sizeof(g_MeshMaterial_PSMaterial_SPIRV)}
	    : FRHIShaderBytecode{g_MeshMaterial_PSMaterial_DXIL, sizeof(g_MeshMaterial_PSMaterial_DXIL)};
	Desc.VertexAttributes = {{"POSITION", 0, EVertexFormat::Float3, offsetof(FNativeMeshVertex, Position)},
	    {"NORMAL", 0, EVertexFormat::Float3, offsetof(FNativeMeshVertex, Normal)},
	    {"TEXCOORD", 0, EVertexFormat::Float2, offsetof(FNativeMeshVertex, UV)},
	    {"COLOR", 0, EVertexFormat::UByte4N, offsetof(FNativeMeshVertex, Color)}};
	Desc.VertexStride = sizeof(FNativeMeshVertex);
	Desc.NumTextures = 2;
	Desc.SamplerAddress = ESamplerAddress::Wrap;
	Desc.DebugName = "Native mesh materials";
	TranslucentPipeline.reset();
	OpaquePipeline = Device.CreateGraphicsPipeline(Desc);
	Format = TargetFormat;
	return OpaquePipeline.get();
}

bool FNativeMeshRenderer::GetTextures(const std::shared_ptr<const FLoadedMeshAsset>& Asset, uint32 SlotIndex,
    TSharedPtr<FRHITexture>& Color, TSharedPtr<FRHITexture>& Normal)
{
	if (!White)
	{
		const uint32 Pixel = 0xffffffffu;
		FRHITextureDesc Desc;
		Desc.Width = Desc.Height = 1;
		Desc.InitialData = &Pixel;
		Desc.DebugName = "Native mesh white";
		White = Device.CreateTexture(Desc);
	}
	if (!White)
		return false;
	Color = Normal = White;
	if (!Asset)
		return false;
	const FLoadedMaterialSlot& Slot = Asset->GetSlot(SlotIndex);
	if (std::map<const FLoadedMaterialSlot*, FEntry>::iterator It = Materials.find(&Slot); It != Materials.end())
	{
		It->second.LastUsed = Frame;
		if (It->second.Color)
			Color = It->second.Color;
		if (It->second.Normal)
			Normal = It->second.Normal;
		return true;
	}
	FEntry Entry;
	Entry.Asset = Asset;
	Entry.LastUsed = Frame;
	const TFunction<bool(const FTextureAsset&, bool, TSharedPtr<FRHITexture>&)> Upload =
	    [&](const FTextureAsset& Image, bool IsNormal, TSharedPtr<FRHITexture>& Out)
	{
		FRenderTextureMipChain Mips;
		FString Error;
		if (!FRenderTextureMipChain::Build(Image, IsNormal, Mips, Error))
			return false;
		FRHITextureDesc Desc;
		Desc.Width = Image.Width;
		Desc.Height = Image.Height;
		Desc.MipLevels = Mips.Levels;
		Desc.Format = !IsNormal && Image.bSRGB ? EPixelFormat::R8G8B8A8_SRGB : EPixelFormat::R8G8B8A8_UNORM;
		Desc.InitialData = Mips.Pixels.data();
		Desc.DebugName = IsNormal ? "Mesh normal map" : "Mesh base color";
		Out = Device.CreateTexture(Desc);
		return Out != nullptr;
	};
	if ((Slot.Texture && !Upload(*Slot.Texture, false, Entry.Color)) ||
	    (Slot.NormalTexture && !Upload(*Slot.NormalTexture, true, Entry.Normal)))
	{
		if (Entry.Color)
			Device.DeferRelease(Entry.Color);
		if (Entry.Normal)
			Device.DeferRelease(Entry.Normal);
		return false;
	}
	if (Slot.Material && Slot.Material->Graph)
	{
		if (Slot.GraphTextureNormals.size() != Slot.GraphTextures.size())
			return false;
		for (size_t I = 0; I < Slot.GraphTextures.size(); ++I)
		{
			TSharedPtr<FRHITexture> Texture;
			if (!Upload(Slot.GraphTextures[I], Slot.GraphTextureNormals[I], Texture))
			{
				for (const std::shared_ptr<FRHITexture>& T : Entry.GraphTextures)
					Device.DeferRelease(T);
				if (Entry.Color)
					Device.DeferRelease(Entry.Color);
				if (Entry.Normal)
					Device.DeferRelease(Entry.Normal);
				return false;
			}
			Entry.GraphTextures.push_back(Texture);
		}
	}
	if (Entry.Color)
		Color = Entry.Color;
	if (Entry.Normal)
		Normal = Entry.Normal;
	Materials[&Slot] = std::move(Entry);
	return true;
}

FNativeMeshFrame::~FNativeMeshFrame()
{
	if (Vertices)
		Device.DeferRelease(Vertices);
	if (Indices)
		Device.DeferRelease(Indices);
	if (Opaque)
		Device.DeferRelease(Opaque);
	if (Translucent)
		Device.DeferRelease(Translucent);
	for (const FNativeMeshFrame::FDraw& Draw : Draws)
	{
		if (Draw.Color)
			Device.DeferRelease(Draw.Color);
		if (Draw.Normal)
			Device.DeferRelease(Draw.Normal);
		if (Draw.GraphPipeline)
			Device.DeferRelease(Draw.GraphPipeline);
		if (Draw.Parameters)
			Device.DeferRelease(Draw.Parameters);
		for (const std::shared_ptr<FRHITexture>& T : Draw.GraphTextures)
			Device.DeferRelease(T);
	}
}
FNativeMeshRenderer::~FNativeMeshRenderer()
{
	for (const FNativeMeshRenderer::FGraphPipelineEntry& P : GraphPipelines)
		Device.DeferRelease(P.Pipeline);
	for (std::pair<const FLoadedMaterialSlot* const, FNativeMeshRenderer::FEntry>& KeyBinding : Materials)
	{
		FNativeMeshRenderer::FEntry& Entry = KeyBinding.second;

		for (const std::shared_ptr<FRHITexture>& T : Entry.GraphTextures)
			Device.DeferRelease(T);
		if (Entry.Color)
			Device.DeferRelease(Entry.Color);
		if (Entry.Normal)
			Device.DeferRelease(Entry.Normal);
	}
	if (White)
		Device.DeferRelease(White);
}
void FNativeMeshRenderer::Trim()
{
	++Frame;
	std::erase_if(GraphPipelines, [&](TArray<FGraphPipelineEntry>::value_type& P)
	{
		if (Frame - P.LastUsed <= 300)
			return false;
		Device.DeferRelease(P.Pipeline);
		return true;
	});
	for (std::map<const FLoadedMaterialSlot*, FEntry>::iterator It = Materials.begin(); It != Materials.end();)
	{
		if (Frame - It->second.LastUsed > 300)
		{
			for (const std::shared_ptr<FRHITexture>& T : It->second.GraphTextures)
				Device.DeferRelease(T);
			if (It->second.Color)
				Device.DeferRelease(It->second.Color);
			if (It->second.Normal)
				Device.DeferRelease(It->second.Normal);
			It = Materials.erase(It);
		}
		else
			++It;
	}
}
TUniquePtr<FNativeMeshFrame> FNativeMeshRenderer::Prepare(
    const FNativeMeshDrawList& List, EPixelFormat Target, FString& Error)
{
	if (List.Vertices.empty())
	{
		Error.Empty();
		return {};
	}

	if (List.Vertices.size() > std::numeric_limits<uint32>::max())
	{
		Error = GENESIS_TEXT("Too many native mesh vertices.");
		return {};
	}
	for (const FNativeMeshVertex& V : List.Vertices)
	{
		for (float X : V.Position)
			if (!std::isfinite(X))
			{
				Error = GENESIS_TEXT("Non-finite native mesh position.");
				return {};
			}
		for (float X : V.Normal)
			if (!std::isfinite(X))
			{
				Error = GENESIS_TEXT("Non-finite native mesh normal.");
				return {};
			}
		for (float X : V.UV)
			if (!std::isfinite(X))
			{
				Error = GENESIS_TEXT("Non-finite native mesh UV.");
				return {};
			}
	}
	for (const FNativeMeshBatch& Batch : List.Batches)
	{
		if (!Batch.Asset || Batch.Slot >= static_cast<uint32>(Batch.Asset->Data.GetNumMaterials()) ||
		    (Batch.Slot > 0 && Batch.Slot > Batch.Asset->AdditionalSlots.size()) || Batch.NumVertices % 3 != 0 ||
		    Batch.FirstVertex > List.Vertices.size() || Batch.NumVertices > List.Vertices.size() - Batch.FirstVertex ||
		    !std::isfinite(Batch.Depth))
		{
			Error = GENESIS_TEXT("Invalid native draw batch range or depth.");
			return {};
		}
	}
	if (!GetPipeline(Target) || !GetPipeline(Target, true))
	{
		Error = GENESIS_TEXT("Cannot create native material pipeline.");
		return {};
	}
	TUniquePtr<FNativeMeshFrame> Prepared = TUniquePtr<FNativeMeshFrame>(new FNativeMeshFrame(Device));
	Prepared->Opaque = OpaquePipeline;
	Prepared->Translucent = TranslucentPipeline;
	FRHIBufferDesc Desc;
	Desc.Size = List.Vertices.size() * sizeof(FNativeMeshVertex);
	Desc.Usage = EBufferUsage::Vertex;
	Desc.Memory = EBufferMemory::Upload;
	Prepared->Vertices = Device.CreateBuffer(Desc);
	Desc.Size = List.Vertices.size() * sizeof(uint32);
	Desc.Usage = EBufferUsage::Index;
	Prepared->Indices = Device.CreateBuffer(Desc);
	if (!Prepared->Vertices || !Prepared->Indices)
	{
		Error = GENESIS_TEXT("Cannot allocate native mesh buffers.");
		return {};
	}
	std::memcpy(
	    Prepared->Vertices->GetMappedData(), List.Vertices.data(), List.Vertices.size() * sizeof(FNativeMeshVertex));
	uint32* Indices = static_cast<uint32*>(Prepared->Indices->GetMappedData());
	for (size_t I = 0; I < List.Vertices.size(); ++I)
		Indices[I] = static_cast<uint32>(I);
	for (const FNativeMeshBatch& Batch : List.Batches)
	{
		FNativeMeshFrame::FDraw Draw;
		Draw.Batch = Batch;
		if (!GetTextures(Batch.Asset, Batch.Slot, Draw.Color, Draw.Normal))
		{
			Error = GENESIS_TEXT("Cannot upload native mesh material textures.");
			return {};
		}
		const FLoadedMaterialSlot& Slot = Batch.Asset->GetSlot(Batch.Slot);
		if (Slot.Material && Slot.Material->Graph)
		{
			FMaterialAsset const& M = *Slot.Material;
			FMaterialGraphProgram Program;
			if (!FMaterialGraphCompiler::Compile(*M.Graph, Program, Error))
				return {};
			if (!Program.Parameters.empty())
			{
				TArray<float> Values;
				for (const FMaterialGraphParameter& P : Program.Parameters)
				{
					float Value[4];
					std::copy(std::begin(P.Value), std::end(P.Value), Value);
					if (!P.Collection.IsNull())
						for (const std::shared_ptr<FMaterialParameterCollectionInstance>& Instance : Collections)
						{
							const FAssetReference& Ref = Instance->GetReference();
							const bool Match =
							    P.Collection.Id.IsEmpty() ? Ref.Path == P.Collection.Path : Ref.Id == P.Collection.Id;
							if (Match && !Instance->GetValue(P.CollectionParameter, P.Width, Value, Error))
								return {};
						}
					for (float V : Value)
						Values.push_back(V);
				}
				FRHIBufferDesc BD;
				BD.Size = Values.size() * sizeof(float);
				BD.Usage = EBufferUsage::Uniform;
				BD.Memory = EBufferMemory::Default;
				BD.InitialData = Values.data();
				BD.DebugName = "Material parameter snapshot";
				Draw.Parameters = Device.CreateBuffer(BD);
				if (!Draw.Parameters)
				{
					Error = GENESIS_TEXT("Cannot create material parameter buffer.");
					return {};
				}
			}
			if (M.CookedGraphSource != Program.PixelShaderSource ||
			    Slot.GraphTextures.size() != Slot.GraphTextureNormals.size() ||
			    Slot.GraphTextures.size() > FMaterialGraphCompiler::MaxTextures)
			{
				if (Error.IsEmpty())
					Error = GENESIS_TEXT("Stale or incomplete graph material.");
				return {};
			}
			for (FNativeMeshRenderer::FGraphPipelineEntry& P : GraphPipelines)
				if (P.Format == Target && P.Material.BlendMode == M.BlendMode &&
				    P.Material.CookedGraphSource == M.CookedGraphSource)
				{
					Draw.GraphPipeline = P.Pipeline;
					P.LastUsed = Frame;
					break;
				}
			if (!Draw.GraphPipeline)
			{
				FGraphPipelineEntry Entry{M, Target, {}, Frame};
				FRHIGraphicsPipelineDesc GraphDesc =
				    (M.BlendMode == EMaterialBlendMode::Translucent ? TranslucentPipeline : OpaquePipeline)->GetDesc();
				const TArray<uint8>& Bytes =
				    Device.GetBackend() == ERHIBackend::Vulkan ? Entry.Material.GraphSPIRV : Entry.Material.GraphDXIL;
				if (Bytes.empty())
				{
					Error = GENESIS_TEXT("Missing graph shader for this RHI.");
					return {};
				}
				GraphDesc.PixelShader = {Bytes.data(), Bytes.size()};
				GraphDesc.NumTextures = std::max(1u, static_cast<uint32>(Slot.GraphTextures.size()));
				GraphDesc.bUniformBuffer = !Program.Parameters.empty();
				GraphDesc.DebugName = "Compiled material graph";
				Draw.GraphPipeline = Device.CreateGraphicsPipeline(GraphDesc);
				if (!Draw.GraphPipeline)
				{
					Error = GENESIS_TEXT("Cannot create graph pipeline.");
					return {};
				}
				Entry.Pipeline = Draw.GraphPipeline;
				GraphPipelines.push_back(std::move(Entry));
			}
			Draw.GraphTextures = Materials.at(&Slot).GraphTextures;
		}
		Prepared->Draws.push_back(std::move(Draw));
	}
	Prepared->OpaqueOrder = List.GetDrawOrder(false);
	Prepared->TranslucentOrder = List.GetDrawOrder(true);
	Error.Empty();
	return Prepared;
}
void FNativeMeshRenderer::Draw(
    FRHICommandList& Cmd, const FNativeMeshFrame& Prepared, const FNativeMeshView& View, bool Transparent)
{
	if (View.Width == 0 || View.Height == 0)
		return;
	const TArray<size_t>& Order = Transparent ? Prepared.TranslucentOrder : Prepared.OpaqueOrder;
	if (Order.empty())
		return;
	Cmd.SetPipeline(Transparent ? *Prepared.Translucent : *Prepared.Opaque);
	Cmd.SetVertexBuffer(*Prepared.Vertices, 0, sizeof(FNativeMeshVertex));
	Cmd.SetIndexBuffer(*Prepared.Indices, 0, EIndexFormat::UInt32);
	for (size_t I : Order)
	{
		FNativeMeshFrame::FDraw const& Draw = Prepared.Draws[I];
		Cmd.SetPipeline(Draw.GraphPipeline ? *Draw.GraphPipeline
		        : Transparent              ? *Prepared.Translucent
		                                   : *Prepared.Opaque);
		const FLoadedMaterialSlot& Slot = Draw.Batch.Asset->GetSlot(Draw.Batch.Slot);
		const FMaterialAsset DefaultMaterial;
		const FMaterialAsset& Material = Slot.Material ? *Slot.Material : DefaultMaterial;
		const int Flags = (View.bLit ? 1 : 0) | (Slot.NormalTexture ? 4 : 0) |
		    (Material.BlendMode == EMaterialBlendMode::Masked ? 8 : 0) | (Transparent ? 16 : 0) |
		    (static_cast<int>(Material.OpacityMaskClip * 65535.0f + 0.5f) << 5);
		const float Params[16] = {2.0f / View.Width, 2.0f / View.Height, View.ProjectionScale,
		    static_cast<float>(Flags), Material.BaseColor[0], Material.BaseColor[1], Material.BaseColor[2],
		    Material.BaseColor[3], View.Light[0], View.Light[1], View.Light[2], Material.Roughness, View.Up[0],
		    View.Up[1], View.Up[2], Material.Metallic};
		Cmd.SetPushConstants(Params, sizeof(Params));
		if (Draw.GraphPipeline)
		{
			if (Draw.Parameters)
				Cmd.SetUniformBuffer(*Draw.Parameters);
			if (Draw.GraphTextures.empty())
				Cmd.SetTexture(*Draw.Color, 0);
			for (uint32 T = 0; T < Draw.GraphTextures.size(); ++T)
				Cmd.SetTexture(*Draw.GraphTextures[T], T);
		}
		else
		{
			Cmd.SetTexture(*Draw.Color, 0);
			Cmd.SetTexture(*Draw.Normal, 1);
		}
		Cmd.DrawIndexed(Draw.Batch.NumVertices, Draw.Batch.FirstVertex, 0);
	}
}

void FNativeMeshRenderer::SetParameterCollectionInstance(std::shared_ptr<FMaterialParameterCollectionInstance> Instance)
{
	if (!Instance)
		return;
	const FAssetReference& Ref = Instance->GetReference();
	for (std::shared_ptr<FMaterialParameterCollectionInstance>& Current : Collections)
	{
		const FAssetReference& Other = Current->GetReference();
		if (Ref.Id.IsEmpty() ? Ref.Path == Other.Path : Ref.Id == Other.Id)
		{
			Current = std::move(Instance);
			return;
		}
	}
	Collections.push_back(std::move(Instance));
}
