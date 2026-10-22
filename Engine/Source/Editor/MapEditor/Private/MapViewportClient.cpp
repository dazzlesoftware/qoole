// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "MapViewportClient.h"

#include "MapDraw.h"
#include "MapMaterialTextures.h"
#include "MapLandscapeMode.h"
#include "MapEditorPrivate.h"

#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontCache.h"
#include "Styling/SlateStyle.h"

#include "MapShaders_PSLandscape_DXIL.h"
#include "MapShaders_PSTextured_DXIL.h"
#include "MapShaders_VSFoliage_DXIL.h"
#include "MapShaders_VSMain_DXIL.h"
#include "Shaders/MapShaders_PSLandscape_SPIRV.h"
#include "Shaders/MapShaders_PSTextured_SPIRV.h"
#include "Shaders/MapShaders_VSFoliage_SPIRV.h"
#include "Shaders/MapShaders_VSMain_SPIRV.h"
#include "MeshMaterial_VSMaterial_DXIL.h"
#include "MeshMaterial_PSMaterial_DXIL.h"
#include "Shaders/MeshMaterial_VSMaterial_SPIRV.h"
#include "Shaders/MeshMaterial_PSMaterial_SPIRV.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace MapEditorPrivate;

const char* ToString(EMapViewType Type)
{
	switch (Type)
	{
	case EMapViewType::Top: return "Top";
	case EMapViewType::Front: return "Front";
	case EMapViewType::Right: return "Right";
	case EMapViewType::Perspective: return "Perspective";
	case EMapViewType::Bottom: return "Bottom";
	case EMapViewType::Back: return "Back";
	case EMapViewType::Left: return "Left";
	case EMapViewType::Count: break;
	}
	return "?";
}

TOptional<EMapViewType> MapViewTypeFromString(const FString& Name)
{
	for (int32 Index = 0; Index < static_cast<int32>(EMapViewType::Count); ++Index)
	{
		if (Name.Equals(ToString(static_cast<EMapViewType>(Index)), ESearchCase::IgnoreCase))
		{
			return static_cast<EMapViewType>(Index);
		}
	}
	return {};
}

const char* ToString(EMapViewMode Mode)
{
	switch (Mode)
	{
	case EMapViewMode::Wireframe: return "Wireframe";
	case EMapViewMode::Unlit: return "Unlit";
	case EMapViewMode::Lit: return "Lit";
	}
	return "?";
}

namespace
{
	constexpr FColor BackgroundColor(24, 26, 31);
	// How deep the views see: far enough for the biggest landscapes, which
	// are drawn coarser with distance (View::RenderLandscapeSolid).
	constexpr float ClipDepth2D = 65536.0f;
	constexpr float ClipDepth3D = 65536.0f;
	constexpr float DropDepth3D = 4096.0f; // the ground counts for drops this near
	constexpr float DragThreshold = 4.0f; // logical pixels before a press becomes a drag
	constexpr float MinZoom = 0.02f;
	constexpr float MaxZoom = 16.0f;
	constexpr float LookSpeed = 0.0035f;  // radians per raw mouse count
	constexpr float OrbitSpeed = 0.008f;  // radians per logical pixel
	constexpr float FlySpeed = 640.0f;    // map units per second, at camera speed 1
	constexpr float FocusDistance = 512.0f; // what a 3D camera looks at, without a selection ahead
	constexpr float NearPlane = 2.0f;     // the perspective grid stops there
	const Vector3d DefaultCameraOffset(-384.0f, -384.0f, 256.0f); // 3D camera from what it looks at

	// Palette entries the editor draws with, besides View's VRC_* colors.
	constexpr int ColorGridMinor = 252;
	constexpr int ColorGridMajor = 253;
	constexpr int ColorGridAxis = 254;
	constexpr int ColorBox = 250;

	void SetPalette(FDraw& Draw)
	{
		Draw.SetColor(VRC_VIEW, BackgroundColor.R, BackgroundColor.G, BackgroundColor.B);
		Draw.SetColor(VRC_NORMAL, 196, 202, 212);
		Draw.SetColor(VRC_ENTITY, 110, 172, 255);
		Draw.SetColor(VRC_SELECT, 255, 92, 64);
		Draw.SetColor(VRC_HOTSELECT, 255, 168, 140);
		Draw.SetColor(VRC_MANIPDOT, 255, 220, 0);
		Draw.SetColor(VRC_FACESELECT, 255, 220, 0);
		Draw.SetColor(VRC_CROSSHAIR, 24, 100, 244);
		Draw.SetColor(VRC_CROSSHAIRNORTH, 4, 208, 24);
		Draw.SetColor(VRC_OPCENTER, 32, 192, 255);
		Draw.SetColor(VRC_LEAKGEOM, 255, 128, 0);
		Draw.SetColor(VRC_MESH, 120, 214, 196);
		Draw.SetColor(ColorGridMinor, 36, 40, 48);
		Draw.SetColor(ColorGridMajor, 54, 60, 72);
		Draw.SetColor(ColorGridAxis, 84, 94, 116);
		Draw.SetColor(ColorBox, 120, 170, 255);
	}

	float AxisComponent(const Vector3d& V, int Axis)
	{
		return Axis == 0 ? V.GetX() : (Axis == 1 ? V.GetY() : V.GetZ());
	}

	void SetAxisComponent(Vector3d& V, int Axis, float Value)
	{
		if (Axis == 0) V.SetX(Value);
		else if (Axis == 1) V.SetY(Value);
		else V.SetZ(Value);
	}

	// The scope axis a view direction runs along (2D views are axis-aligned),
	// and whether it runs backwards.
	int DominantAxis(const Vector3d& V, float& OutSign)
	{
		int Axis = 0;
		float Best = FMath::Abs(V.GetX());
		if (FMath::Abs(V.GetY()) > Best) { Axis = 1; Best = FMath::Abs(V.GetY()); }
		if (FMath::Abs(V.GetZ()) > Best) { Axis = 2; }
		OutSign = AxisComponent(V, Axis) < 0.0f ? -1.0f : 1.0f;
		return Axis;
	}

	const char* AxisName(int Axis)
	{
		return Axis == 0 ? "X" : (Axis == 1 ? "Y" : "Z");
	}

	// Which way SphrVector turns: does a larger yaw turn right, a larger
	// pitch look up? Measured once from the map core's own conversion.
	struct FTurnSigns
	{
		float Yaw = 1.0f;
		float Pitch = 1.0f;
		FTurnSigns()
		{
			Vector3d Front, Top;
			SphrVector::Sphr2Rect(SphrVector(0.1f, 0.0f, 0.0f), Front, Top);
			Yaw = Front.GetX() > 0.0f ? 1.0f : -1.0f;  // forward is +Y; right is +X
			SphrVector::Sphr2Rect(SphrVector(0.0f, 0.1f, 0.0f), Front, Top);
			Pitch = Front.GetZ() > 0.0f ? 1.0f : -1.0f;
		}
	};
	const FTurnSigns& GetTurnSigns()
	{
		static const FTurnSigns Signs;
		return Signs;
	}

	// How a 2D view of a type looks at the map.
	SphrVector OrthoOrientation(EMapViewType Type)
	{
		const FTurnSigns& Signs = GetTurnSigns();
		SphrVector Orientation;
		switch (Type)
		{
		case EMapViewType::Top: Orientation.NewVector(0.0f, Signs.Pitch * DEG2RAD(-90.0f), 0.0f); break;
		case EMapViewType::Bottom: Orientation.NewVector(0.0f, Signs.Pitch * DEG2RAD(90.0f), 0.0f); break;
		case EMapViewType::Front: Orientation.NewVector(0.0f, 0.0f, 0.0f); break;
		case EMapViewType::Back: Orientation.NewVector(DEG2RAD(180.0f), 0.0f, 0.0f); break;
		case EMapViewType::Right: Orientation.NewVector(Signs.Yaw * DEG2RAD(-90.0f), 0.0f, 0.0f); break;
		case EMapViewType::Left: Orientation.NewVector(Signs.Yaw * DEG2RAD(90.0f), 0.0f, 0.0f); break;
		default: break;
		}
		return Orientation;
	}

	Vector3d Normalized(Vector3d V)
	{
		const float Length = V.GetMag();
		if (Length > 1e-6f)
		{
			V.MultVector(1.0f / Length);
		}
		return V;
	}

	// While it lives, the map core's entity display (a global) follows a view's show flag.
	class FScopedEntityDisplay
	{
	public:
		explicit FScopedEntityDisplay(bool bShow) : bWasShown(View::EntitiesDisplayed()) { View::DisplayEntities(bShow); }
		~FScopedEntityDisplay() { View::DisplayEntities(bWasShown); }

	private:
		bool bWasShown;
	};
}

// ---------------------------------------------------------------------------

FRHIGraphicsPipeline* FMapRenderResources::GetSolidPipeline(EPixelFormat TargetFormat)
{
	if (SolidPipeline && SolidFormat == TargetFormat)
	{
		return SolidPipeline.get();
	}
	FRHIGraphicsPipelineDesc Desc;
	const bool bVulkan = Device.GetBackend() == ERHIBackend::Vulkan;
	Desc.VertexShader = bVulkan ? FRHIShaderBytecode{ g_MapShaders_VSMain_SPIRV, sizeof(g_MapShaders_VSMain_SPIRV) }
		: FRHIShaderBytecode{ g_MapShaders_VSMain_DXIL, sizeof(g_MapShaders_VSMain_DXIL) };
	Desc.PixelShader = bVulkan ? FRHIShaderBytecode{ g_MapShaders_PSTextured_SPIRV, sizeof(g_MapShaders_PSTextured_SPIRV) }
		: FRHIShaderBytecode{ g_MapShaders_PSTextured_DXIL, sizeof(g_MapShaders_PSTextured_DXIL) };
	Desc.VertexAttributes = {
		{ "POSITION", 0, EVertexFormat::Float3, offsetof(FMapVertex, X) },
		{ "TEXCOORD", 0, EVertexFormat::Float2, offsetof(FMapVertex, U) },
		{ "COLOR", 0, EVertexFormat::UByte4N, offsetof(FMapVertex, Color) },
	};
	Desc.VertexStride = sizeof(FMapVertex);
	Desc.BlendMode = EBlendMode::Opaque;
	Desc.RenderTargetFormat = TargetFormat;
	Desc.DepthFormat = EPixelFormat::D32_FLOAT;
	// The shaders' constants are 64 bytes (MapShaders.hlsl), faces use 16.
	Desc.PushConstantSize = 64;
	Desc.DebugName = "Map solid faces";
	SolidPipeline = Device.CreateGraphicsPipeline(Desc);
	SolidFormat = TargetFormat;
	return SolidPipeline.get();
}

FRHIGraphicsPipeline* FMapRenderResources::GetMaterialPipeline(EPixelFormat TargetFormat, bool Translucent)
{
	if (Translucent)
	{
		const auto* Opaque = GetMaterialPipeline(TargetFormat);
		if (!Opaque)
			return nullptr;
		if (!TranslucentMaterialPipeline)
		{
			auto Desc = Opaque->GetDesc();
			Desc.BlendMode = EBlendMode::AlphaBlend;
			Desc.bDepthWrite = false;
			Desc.DebugName = "Translucent mesh materials";
			TranslucentMaterialPipeline = Device.CreateGraphicsPipeline(Desc);
		}
		return TranslucentMaterialPipeline.get();
	}
	if (MaterialPipeline && MaterialFormat == TargetFormat)
		return MaterialPipeline.get();
	const auto* Solid = GetSolidPipeline(TargetFormat);
	if (!Solid)
		return nullptr;
	auto Desc = Solid->GetDesc();
	const bool Vulkan = Device.GetBackend() == ERHIBackend::Vulkan;
	Desc.VertexShader =
		Vulkan ? FRHIShaderBytecode{g_MeshMaterial_VSMaterial_SPIRV, sizeof(g_MeshMaterial_VSMaterial_SPIRV)}
			   : FRHIShaderBytecode{g_MeshMaterial_VSMaterial_DXIL, sizeof(g_MeshMaterial_VSMaterial_DXIL)};
	Desc.PixelShader =
		Vulkan ? FRHIShaderBytecode{g_MeshMaterial_PSMaterial_SPIRV, sizeof(g_MeshMaterial_PSMaterial_SPIRV)}
			   : FRHIShaderBytecode{g_MeshMaterial_PSMaterial_DXIL, sizeof(g_MeshMaterial_PSMaterial_DXIL)};
	Desc.VertexAttributes = {{"POSITION", 0, EVertexFormat::Float3, offsetof(FMapMaterialVertex, Position)},
							 {"NORMAL", 0, EVertexFormat::Float3, offsetof(FMapMaterialVertex, Normal)},
							 {"TEXCOORD", 0, EVertexFormat::Float2, offsetof(FMapMaterialVertex, UV)},
							 {"COLOR", 0, EVertexFormat::UByte4N, offsetof(FMapMaterialVertex, Color)}};
	Desc.VertexStride = sizeof(FMapMaterialVertex);
	Desc.NumTextures = 2;
	Desc.SamplerAddress = ESamplerAddress::Wrap;
	Desc.DebugName = "Native mesh materials";
	TranslucentMaterialPipeline.reset();
	MaterialPipeline = Device.CreateGraphicsPipeline(Desc);
	MaterialFormat = TargetFormat;
	return MaterialPipeline.get();
}

bool FMapRenderResources::GetMaterialTextures(const std::shared_ptr<const FLoadedMeshAsset>& Asset, FRHITexture*& Color,
											  FRHITexture*& Normal)
{
	Color = Normal = &GetWhiteTexture();
	if (!Asset)
		return false;
	if (auto It = Materials.find(Asset.get()); It != Materials.end())
	{
		It->second.LastUsed = Frame;
		if (It->second.Color)
			Color = It->second.Color.get();
		if (It->second.Normal)
			Normal = It->second.Normal.get();
		return true;
	}
	FMaterialEntry Entry;
	Entry.Asset = Asset;
	Entry.LastUsed = Frame;
	const auto Upload = [&](const FTextureAsset& Image, bool IsNormal, TSharedPtr<FRHITexture>& Out) {
		FMapMaterialMipChain Mips;
		FString Error;
		if (!FMapMaterialMipChain::Build(Image, IsNormal, Mips, Error))
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
	if ((Asset->Texture && !Upload(*Asset->Texture, false, Entry.Color)) ||
		(Asset->NormalTexture && !Upload(*Asset->NormalTexture, true, Entry.Normal)))
	{
		if (Entry.Color)
			Device.DeferRelease(Entry.Color);
		if (Entry.Normal)
			Device.DeferRelease(Entry.Normal);
		return false;
	}
	if (Entry.Color)
		Color = Entry.Color.get();
	if (Entry.Normal)
		Normal = Entry.Normal.get();
	Materials[Asset.get()] = std::move(Entry);
	return true;
}

FRHIGraphicsPipeline* FMapRenderResources::GetLandscapePipeline(EPixelFormat TargetFormat)
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
	Desc.PixelShader = bVulkan ? FRHIShaderBytecode{ g_MapShaders_PSLandscape_SPIRV, sizeof(g_MapShaders_PSLandscape_SPIRV) }
		: FRHIShaderBytecode{ g_MapShaders_PSLandscape_DXIL, sizeof(g_MapShaders_PSLandscape_DXIL) };
	// Slot 0 unused, then the weights and the layers' textures (which repeat).
	Desc.NumTextures = 3;
	Desc.SamplerAddress = ESamplerAddress::Wrap;
	Desc.DebugName = "Map landscape layers";
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
}

FRHIGraphicsPipeline* FMapRenderResources::GetInstancedPipeline(EPixelFormat TargetFormat)
{
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
	Desc.VertexShader = bVulkan ? FRHIShaderBytecode{ g_MapShaders_VSFoliage_SPIRV, sizeof(g_MapShaders_VSFoliage_SPIRV) }
		: FRHIShaderBytecode{ g_MapShaders_VSFoliage_DXIL, sizeof(g_MapShaders_VSFoliage_DXIL) };
	// The mesh's corners, then each copy's object-to-view rows.
	Desc.VertexAttributes = {
		{ "POSITION", 0, EVertexFormat::Float3, offsetof(FInstancedVertex, Position), 0 },
		{ "NORMAL", 0, EVertexFormat::Float3, offsetof(FInstancedVertex, Normal), 0 },
		{ "COLOR", 0, EVertexFormat::UByte4N, offsetof(FInstancedVertex, Color), 0 },
		{ "TEXCOORD", 1, EVertexFormat::Float4, 0, 1 },
		{ "TEXCOORD", 2, EVertexFormat::Float4, 16, 1 },
		{ "TEXCOORD", 3, EVertexFormat::Float4, 32, 1 },
	};
	Desc.VertexStride = sizeof(FInstancedVertex);
	Desc.InstanceStride = 12 * sizeof(float);
	// Closed meshes (and two-sided blades): back faces need not be drawn.
	// Counterclockwise from outside in map space, their outsides are
	// counterclockwise on screen (y flipped): "front" to the RHI.
	Desc.CullMode = ECullMode::Front;
	Desc.DebugName = "Map instanced meshes";
	InstancedPipeline = Device.CreateGraphicsPipeline(Desc);
	InstancedFormat = TargetFormat;
	return InstancedPipeline.get();
}

const FMapRenderResources::FMeshBuffers* FMapRenderResources::GetMeshBuffers(const StaticMesh& Mesh)
{
	if (const auto Found = MeshBuffers.find(&Mesh); Found != MeshBuffers.end())
	{
		return &Found->second;
	}
	// A vertex a corner: the triangles' own normals there.
	TArray<FInstancedVertex> Corners;
	for (const StaticMesh::Triangle& Triangle : Mesh.GetTriangles())
	{
		for (int Corner = 0; Corner < 3; ++Corner)
		{
			const Vector3d& P = Mesh.GetVertex(Triangle.v[Corner]);
			const Vector3d& N = Triangle.norm[Corner];
			FInstancedVertex Vertex{ { P.GetX(), P.GetY(), P.GetZ() }, { N.GetX(), N.GetY(), N.GetZ() },
				Mesh.HasVertexColors() ? Mesh.GetVertexColor(Triangle.v[Corner]) : 0xFFFFFFFFu };
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
	FMeshBuffers Buffers;
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
		return nullptr;
	}
	return &(MeshBuffers[&Mesh] = std::move(Buffers));
}

FMapRenderResources::~FMapRenderResources()
{
	for(auto& [Asset,Entry]:Materials) { if(Entry.Color) Device.DeferRelease(Entry.Color); if(Entry.Normal) Device.DeferRelease(Entry.Normal); }
	for (auto& [Mesh, Buffers] : MeshBuffers)
	{
		Device.DeferRelease(Buffers.Vertices);
		Device.DeferRelease(Buffers.Indices);
	}
	for (auto& [Land, Entry] : Landscapes)
	{
		Release(*Entry);
	}
}

void FMapRenderResources::Release(FLandscapeEntry& Entry)
{
	for (TSharedPtr<FRHITexture>* Texture : { &Entry.Texture, &Entry.WeightTexture, &Entry.LayerTexture })
	{
		if (*Texture)
		{
			Device.DeferRelease(*Texture);
			Texture->reset();
		}
	}
}

FMapRenderResources::FLandscapeEntry& FMapRenderResources::FindEntry(const Object& Land)
{
	std::unique_ptr<FLandscapeEntry>& Entry = Landscapes[&Land];
	if (!Entry)
	{
		Entry = std::make_unique<FLandscapeEntry>();
	}
	Entry->LastUsed = Frame;
	return *Entry;
}

bool FMapRenderResources::GetLandscapeLayers(const Object& Land, FRHITexture*& OutWeights, FRHITexture*& OutLayers)
{
	if (!bBlendLandscapeLayers || !Land.HasLandscape() || !FLandscapeLayerImages::CanBlend(Land.GetLandscape()))
	{
		return false;
	}
	FLandscapeEntry& Entry = FindEntry(Land);
	FLandscapeLayerImages& Images = Entry.Layers;

	// The weights: a two-slice texture of the grid's size; a stroke copies
	// just the points it changed.
	const FIntRect Changed = Images.UpdateWeights(Land.GetLandscapePtr());
	const uint32 PointsX = static_cast<uint32>(Images.GetPointsX());
	const uint32 PointsY = static_cast<uint32>(Images.GetPointsY());
	if (!Entry.WeightTexture || Entry.WeightTexture->GetWidth() != PointsX || Entry.WeightTexture->GetHeight() != PointsY)
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
			const uint8* First = Images.GetWeights().data() + SIZE_T(Pitch) * PointsY * Slice + SIZE_T(Changed.Top) * Pitch + SIZE_T(Changed.Left) * 4;
			Device.UpdateTexture(*Entry.WeightTexture, Changed, First, Pitch, Slice);
		}
	}

	// The layers' textures: made again when a layer looks different.
	if (Images.UpdateTextures(Land.GetLandscape()) || !Entry.LayerTexture)
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

FRHITexture* FMapRenderResources::GetLandscapeTexture(const Object& Land)
{
	if (!Land.HasLandscape() || !FLandscapeTextureLibrary::IsTextured(Land.GetLandscape()))
	{
		return nullptr;
	}
	FLandscapeEntry* Entry = &FindEntry(Land);
	const FIntRect Baked = Entry->Bake.Update(Land.GetLandscapePtr());
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
		const uint8* First = Entry->Bake.GetPixels().data() + static_cast<SIZE_T>(Baked.Top) * Pitch + static_cast<SIZE_T>(Baked.Left) * 4;
		Device.UpdateTexture(*Entry->Texture, Baked, First, Pitch);
	}
	return Entry->Texture.get();
}

void FMapRenderResources::Trim()
{
	// Several views draw each frame; a landscape none drew for a few
	// seconds (deleted, or its layers cleared) goes.
	++Frame;
	for(auto It=Materials.begin(); It!=Materials.end();) {
		if(Frame-It->second.LastUsed>300) { if(It->second.Color) Device.DeferRelease(It->second.Color); if(It->second.Normal) Device.DeferRelease(It->second.Normal); It=Materials.erase(It); }
		else ++It;
	}
	for (auto It = Landscapes.begin(); It != Landscapes.end();)
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

const FLandscapeBake* FMapRenderResources::FindLandscapeBake(const Object& Land) const
{
	const auto Found = Landscapes.find(&Land);
	return Found != Landscapes.end() ? &Found->second->Bake : nullptr;
}

FRHITexture& FMapRenderResources::GetWhiteTexture()
{
	if (!WhiteTexture)
	{
		// The pipelines' texture slot must hold something; the faces are not textured yet.
		const uint32 White = 0xFFFFFFFFu;
		FRHITextureDesc Desc;
		Desc.InitialData = &White;
		Desc.DebugName = "Map white";
		WhiteTexture = Device.CreateTexture(Desc);
	}
	return *WhiteTexture;
}

// ---------------------------------------------------------------------------

FMapViewportClient::FMapViewportClient(FRHIDevice& InDevice, FSlateRHIRenderingPolicy& InRenderer, FMapRenderResources& InResources,
	FMapViewSettings& InSettings, EMapViewType InType)
	: FRenderTargetViewportClient(InDevice, InRenderer)
	, Resources(InResources)
	, Settings(InSettings)
	, Type(InType)
	, MapDraw(std::make_unique<FMapDraw>())
{
	MapDraw->SetZoomPtr(&DrawZoom);
	SetPalette(*MapDraw);
	SettingsHandle = Settings.OnChanged.AddLambda([this] { RequestRedraw(); });
}

FMapViewportClient::~FMapViewportClient()
{
	Settings.OnChanged.Remove(SettingsHandle);
	SetDocument(nullptr);
}

void FMapViewportClient::SetDocument(FMapDocument* InDocument)
{
	if (Document)
	{
		Document->OnChanged.Remove(DocumentHandle);
	}
	if (Drag == EDrag::Move && Document && Document->IsInTransaction())
	{
		Document->EndTransaction();
	}
	Drag = EDrag::None;
	bHasClipLine = false;
	GrabbedDot = EDot::None;
	delete MapView;
	MapView = nullptr;
	Document = InDocument;
	if (Document)
	{
		DocumentHandle = Document->OnChanged.AddLambda([this](EMapChange Change) { OnDocumentChanged(Change); });
		CreateView();
	}
	RequestRedraw();
}

void FMapViewportClient::ViewChanged()
{
	OnViewChanged.Broadcast();
	RequestRedraw();
}

void FMapViewportClient::SetViewMode(EMapViewMode Mode)
{
	// Kept by orthographic views too, for when they turn perspective.
	if (ViewMode != Mode)
	{
		ViewMode = Mode;
		ViewChanged();
	}
}

void FMapViewportClient::SetShowFlags(const FMapShowFlags& Flags)
{
	if (!(ShowFlags == Flags))
	{
		ShowFlags = Flags;
		ViewChanged();
	}
}

void FMapViewportClient::SetViewFOV(float Degrees)
{
	Degrees = FMath::Clamp(Degrees, MinViewFOV, MaxViewFOV);
	if (ViewFOV != Degrees)
	{
		ViewFOV = Degrees;
		UpdateProjection();
		ViewChanged();
	}
}

void FMapViewportClient::SetCameraSpeed(float Speed)
{
	Speed = FMath::Clamp(Speed, MinCameraSpeed, MaxCameraSpeed);
	if (CameraSpeed != Speed)
	{
		CameraSpeed = Speed;
		ViewChanged();
	}
}

void FMapViewportClient::SetViewportType(EMapViewType InType)
{
	if (InType == Type || InType == EMapViewType::Count)
	{
		return;
	}
	if (!MapView || !Document)
	{
		Type = InType;
		ViewChanged();
		return;
	}
	if (Drag != EDrag::None)
	{
		OnMouseCaptureLost();
	}
	bHasClipLine = false;
	GrabbedDot = EDot::None;

	// Keep what the camera looks at and about as much of the map in view:
	// a 2D view's half width at its zoom is a 3D camera's distance times
	// tan(FOV / 2).
	const TSharedPtr<SViewport> Widget = GetViewport();
	const FVector2 LocalSize = Widget ? Widget->GetCachedGeometry().GetLocalSize() : FVector2(512.0f, 512.0f);
	const float HalfWidth = FMath::Max(FMath::Max(LocalSize.X, LocalSize.Y) * 0.5f, 16.0f);
	const float TanHalfFOV = std::tan(DEG2RAD(ViewFOV * 0.5f));
	const bool bWasPerspective = IsPerspective();
	float Distance = 0.0f;
	const Vector3d Focus = GetFocusPoint(&Distance);
	Type = InType;
	if (IsPerspective() && !bWasPerspective)
	{
		Distance = FMath::Clamp(HalfWidth / Zoom / TanHalfFOV, 64.0f, 6000.0f);
	}
	else if (!IsPerspective() && bWasPerspective)
	{
		Zoom = FMath::Clamp(HalfWidth / FMath::Max(Distance * TanHalfFOV, 1.0f), MinZoom, MaxZoom);
	}
	PlaceCamera(Focus, Distance);
	ViewChanged();
}

void FMapViewportClient::PlaceCamera(const Vector3d& Center, float Distance)
{
	Vector3d Position(Center);
	SphrVector Orientation;
	if (IsPerspective())
	{
		const Vector3d Back = Normalized(DefaultCameraOffset);
		Vector3d Offset(Back);
		Offset.MultVector(Distance);
		Position.AddVector(Offset);
		Vector3d Front(Back);
		Front.MultVector(-1.0f);
		Orientation.NewVector(Front, Vector3d(0.0f, 0.0f, 1.0f));
		Yaw = Orientation.GetYaw();
		Pitch = Orientation.GetPitch();
	}
	else
	{
		Orientation = OrthoOrientation(Type);
	}
	MapView->SetNewView(Document->GetScope(), Document->GetSelector(), true, &Position, &Orientation, IsPerspective(), 1,
		IsPerspective() ? ClipDepth3D : ClipDepth2D);
	UpdateProjection();
	RequestRedraw();
}

Vector3d FMapViewportClient::GetFocusPoint(float* OutDistance) const
{
	if (!MapView)
	{
		if (OutDistance)
		{
			*OutDistance = FocusDistance;
		}
		return Vector3d(0.0f, 0.0f, 0.0f);
	}
	const Vector3d Position = MapView->GetPosition();
	if (!IsPerspective())
	{
		if (OutDistance)
		{
			*OutDistance = 0.0f;
		}
		return Position;
	}
	const Vector3d Forward = Normalized(ViewToScope(Vector3d(0.0f, 1.0f, 0.0f)));
	float Distance = FocusDistance;
	Vector3d Min, Max;
	if (Document && Document->GetSelectionBounds(Min, Max))
	{
		Vector3d Center(Min);
		Center.AddVector(Max).MultVector(0.5f);
		Center.SubVector(Position);
		const float Ahead = Center.DotVector(Forward);
		if (Ahead > 32.0f)
		{
			Distance = Ahead;
		}
	}
	if (OutDistance)
	{
		*OutDistance = Distance;
	}
	Vector3d Focus(Forward);
	Focus.MultVector(Distance);
	return Focus.AddVector(Position);
}

bool FMapViewportClient::GetCursorRay(FVector2 LocalPosition, Vector3d& OutOrigin, Vector3d& OutDirection) const
{
	if (!MapView)
	{
		return false;
	}
	if (!IsPerspective())
	{
		ScreenToMap(LocalPosition, OutOrigin);
		// From well in front of the plane, so things behind it are hit too.
		OutDirection = Normalized(ViewToScope(Vector3d(0.0f, 1.0f, 0.0f)));
		Vector3d Back(OutDirection);
		Back.MultVector(-ClipDepth2D);
		OutOrigin.AddVector(Back);
		return true;
	}
	float X, Y;
	ToScreen(LocalPosition, X, Y);
	const float WinWidth = GetWindowWidth();
	OutDirection = Normalized(ViewToScope(Vector3d(X / WinWidth, 1.0f, Y / WinWidth)));
	OutOrigin = MapView->GetPosition();
	return true;
}

Vector3d FMapViewportClient::GetDropPoint(FVector2 LocalPosition) const
{
	if (!MapView)
	{
		return Vector3d(0.0f, 0.0f, 0.0f);
	}
	if (!IsPerspective())
	{
		Vector3d Point;
		ScreenToMap(LocalPosition, Point);
		return Point;
	}
	float X, Y;
	ToScreen(LocalPosition, X, Y);
	const float WinWidth = GetWindowWidth();
	const Vector3d Direction = Normalized(ViewToScope(Vector3d(X / WinWidth, 1.0f, Y / WinWidth)));
	const Vector3d Origin = MapView->GetPosition();
	float Distance = FocusDistance * 0.5f;
	if (Direction.GetZ() < -1e-3f)
	{
		const float ToGround = -Origin.GetZ() / Direction.GetZ();
		if (ToGround > 0.0f && ToGround < DropDepth3D)
		{
			Distance = ToGround;
		}
	}
	Vector3d Point(Direction);
	Point.MultVector(Distance);
	return Point.AddVector(Origin);
}

int32 FMapViewportClient::GetDepthAxis() const
{
	if (!MapView || IsPerspective())
	{
		return 2;
	}
	float Sign;
	return DominantAxis(ViewToScope(Vector3d(0.0f, 1.0f, 0.0f)), Sign);
}

void FMapViewportClient::OnDocumentChanged(EMapChange Change)
{
	if (HasAnyChange(Change, EMapChange::Scope))
	{
		FollowScope();
	}
	RequestRedraw();
}

void FMapViewportClient::FollowScope()
{
	if (!MapView || !Document)
	{
		return;
	}
	Object* OldScope = MapView->GetViewObjPtr();
	Object& NewScope = Document->GetScope();
	if (OldScope == &NewScope)
	{
		return;
	}
	// Keep the camera where it was in the map: move it into the new
	// scope's space (as the MFC views did), unless the old scope is gone.
	Vector3d Position = MapView->GetPosition();
	SphrVector Orientation = MapView->GetOrientation();
	const Object* Top = OldScope;
	while (Top && Top->GetParentPtr())
	{
		Top = Top->GetParentPtr();
	}
	if (Top == &Document->GetRoot())
	{
		Matrix44 Transform;
		Object::GetTransMatrix(*OldScope, NewScope, Transform);
		Vector3d Front, Up;
		MapView->GetOrientation(Front, Up);
		Front.AddVector(Position);
		Up.AddVector(Position);
		Transform.Transform(Position);
		Transform.Transform(Front).SubVector(Position);
		Transform.Transform(Up).SubVector(Position);
		Orientation.NewVector(Front, Up);
	}
	bHasClipLine = false;
	MapView->SetNewView(NewScope, Document->GetSelector(), true, &Position, &Orientation, IsPerspective(), 1,
		IsPerspective() ? ClipDepth3D : ClipDepth2D);
	UpdateProjection();
}

void FMapViewportClient::Message(const FString& Text) const
{
	if (Settings.OnMessage)
	{
		Settings.OnMessage(Text);
	}
}

void FMapViewportClient::CreateView()
{
	MapView = new View(MapDraw.get(), Document->GetScope(), Document->GetSelector());
	ResetCamera();
}

void FMapViewportClient::ResetCamera()
{
	if (!MapView)
	{
		return;
	}
	Zoom = 0.5f;
	PlaceCamera(Vector3d(0.0f, 0.0f, 0.0f), DefaultCameraOffset.GetMag());
}

float FMapViewportClient::GetDpiScale() const
{
	const TSharedPtr<SViewport> Widget = GetViewport();
	return Widget ? FMath::Max(Widget->GetCachedGeometry().Scale, 0.01f) : 1.0f;
}

float FMapViewportClient::GetDrawZoom() const
{
	return IsPerspective() ? 1.0f : Zoom * GetDpiScale();
}

float FMapViewportClient::GetWindowWidth() const
{
	// Half the larger side, in map units for 2D views; for 3D views the
	// distance at which that half spans the field of view, in pixels.
	const FIntPoint PixelSize = GetSize();
	const float Half = static_cast<float>(FMath::Max(FMath::Max(PixelSize.X, PixelSize.Y), 1)) * 0.5f;
	const float Width = IsPerspective() ? Half / std::tan(DEG2RAD(ViewFOV * 0.5f)) : Half / GetDrawZoom();
	return static_cast<float>(FMath::Max(1, static_cast<int>(Width)));
}

void FMapViewportClient::UpdateProjection()
{
	const FIntPoint PixelSize = GetSize();
	MapDraw->Size(FMath::Max(PixelSize.X, 1), FMath::Max(PixelSize.Y, 1));
	DrawZoom = GetDrawZoom();
	if (MapView)
	{
		MapView->SetWindowWidth(static_cast<int>(GetWindowWidth()));
	}
}

void FMapViewportClient::ToScreen(FVector2 Local, float& OutX, float& OutY) const
{
	const float Scale = GetDpiScale();
	const FIntPoint PixelSize = GetSize();
	// Points outside the view (a drag that left it) are taken at its edge:
	// the map core's picking needs rays inside the view's clip volume.
	Local.X = FMath::Clamp(Local.X, 0.0f, static_cast<float>(PixelSize.X) / Scale);
	Local.Y = FMath::Clamp(Local.Y, 0.0f, static_cast<float>(PixelSize.Y) / Scale);
	const float Zoom2 = GetDrawZoom();
	OutX = (Local.X * Scale - static_cast<float>(FMath::Max(PixelSize.X, 1) / 2)) / Zoom2;
	OutY = (static_cast<float>(FMath::Max(PixelSize.Y, 1) / 2) - Local.Y * Scale) / Zoom2;
}

Vector3d FMapViewportClient::ViewToScope(const Vector3d& ViewVector) const
{
	Matrix44 Transform;
	MapView->CalTransSpaceMatrix(Transform.SetIdentity());
	Vector3d Origin(0.0f, 0.0f, 0.0f);
	Vector3d Result(ViewVector);
	Transform.Transform(Origin);
	Transform.Transform(Result);
	return Result.SubVector(Origin);
}

Vector3d FMapViewportClient::ScopeToView(const Vector3d& Point) const
{
	Matrix44 Transform;
	MapView->CalInvTransSpaceMatrix(Transform.SetIdentity());
	Vector3d Result(Point);
	Transform.Transform(Result);
	return Result;
}

bool FMapViewportClient::ScreenToMap(FVector2 LocalPosition, Vector3d& OutPoint) const
{
	if (!MapView || IsPerspective())
	{
		return false;
	}
	float X, Y;
	ToScreen(LocalPosition, X, Y);
	Matrix44 Transform;
	MapView->CalTransSpaceMatrix(Transform.SetIdentity());
	OutPoint.NewVector(X, 0.0f, Y);
	Transform.Transform(OutPoint);
	return true;
}

bool FMapViewportClient::MapToScreen(const Vector3d& Point, FVector2& OutLocal) const
{
	if (!MapView)
	{
		return false;
	}
	const Vector3d InView = ScopeToView(Point);
	const FIntPoint PixelSize = GetSize();
	const float Scale = GetDpiScale();
	float X = InView.GetX();
	float Y = InView.GetZ();
	if (IsPerspective())
	{
		if (InView.GetY() <= 1.0f)
		{
			return false;
		}
		const float WinWidth = GetWindowWidth();
		X = X * WinWidth / InView.GetY();
		Y = Y * WinWidth / InView.GetY();
	}
	else
	{
		X *= GetDrawZoom();
		Y *= GetDrawZoom();
	}
	OutLocal = FVector2((static_cast<float>(FMath::Max(PixelSize.X, 1) / 2) + X) / Scale, (static_cast<float>(FMath::Max(PixelSize.Y, 1) / 2) - Y) / Scale);
	return true;
}

Vector3d FMapViewportClient::GetPlacementPoint() const
{
	if (!MapView)
	{
		return Vector3d(0.0f, 0.0f, 0.0f);
	}
	Vector3d Point(0.0f, IsPerspective() ? 256.0f : 0.0f, 0.0f);
	Matrix44 Transform;
	MapView->CalTransSpaceMatrix(Transform.SetIdentity());
	Transform.Transform(Point);
	return Point;
}

void FMapViewportClient::PanBy(FVector2 LocalDelta)
{
	if (IsPerspective())
	{
		MapView->MoveRelPosition(Vector3d(-LocalDelta.X, 0.0f, LocalDelta.Y));
	}
	else
	{
		MapView->MoveRelPosition(Vector3d(-LocalDelta.X / Zoom, 0.0f, LocalDelta.Y / Zoom));
	}
	RequestRedraw();
}

void FMapViewportClient::ZoomBy(float Factor)
{
	if (!MapView)
	{
		return;
	}
	if (IsPerspective())
	{
		MapView->MoveRelPosition(Vector3d(0.0f, Factor > 1.0f ? 128.0f : -128.0f, 0.0f));
	}
	else
	{
		Zoom = FMath::Clamp(Zoom * Factor, MinZoom, MaxZoom);
	}
	RequestRedraw();
}

void FMapViewportClient::FrameSelection()
{
	if (!MapView || !Document)
	{
		return;
	}
	Vector3d Min, Max;
	if (!Document->GetSelectionBounds(Min, Max))
	{
		// Nothing selected: the whole map.
		bool bAny = false;
		for (Object* Obj : Document->GetObjects())
		{
			Vector3d ObjMin, ObjMax;
			GetObjectBounds(*Obj, ObjMin, ObjMax);
			if (!bAny)
			{
				Min = ObjMin;
				Max = ObjMax;
				bAny = true;
				continue;
			}
			Min.NewVector(FMath::Min(Min.GetX(), ObjMin.GetX()), FMath::Min(Min.GetY(), ObjMin.GetY()), FMath::Min(Min.GetZ(), ObjMin.GetZ()));
			Max.NewVector(FMath::Max(Max.GetX(), ObjMax.GetX()), FMath::Max(Max.GetY(), ObjMax.GetY()), FMath::Max(Max.GetZ(), ObjMax.GetZ()));
		}
		if (!bAny)
		{
			ResetCamera();
			return;
		}
	}
	Vector3d Center(Min);
	Center.AddVector(Max).MultVector(0.5f);
	Vector3d Extent(Max);
	Extent.SubVector(Min);
	const float Radius = FMath::Max(Extent.GetMag() * 0.5f, 32.0f);

	if (IsPerspective())
	{
		// Back off along the view direction until the bounds fit the field of view.
		Vector3d Back = Normalized(ViewToScope(Vector3d(0.0f, -1.0f, 0.0f)));
		Back.MultVector(Radius * 1.15f / std::sin(DEG2RAD(ViewFOV * 0.5f)));
		Vector3d Position(Center);
		Position.AddVector(Back);
		MapView->SetPosition(Position);
	}
	else
	{
		// Keep the camera's depth; center it and fit the bounds.
		Vector3d Position = MapView->GetPosition();
		Vector3d Offset(Center);
		Offset.SubVector(Position);
		const Vector3d ViewOffset = ScopeToView(Center);
		Position.AddVector(ViewToScope(Vector3d(ViewOffset.GetX(), 0.0f, ViewOffset.GetZ())));
		MapView->SetPosition(Position);
		const TSharedPtr<SViewport> Widget = GetViewport();
		const FVector2 LocalSize = Widget ? Widget->GetCachedGeometry().GetLocalSize() : FVector2(512.0f, 512.0f);
		const float Fit = FMath::Min(LocalSize.X, LocalSize.Y) * 0.45f / Radius;
		Zoom = FMath::Clamp(Fit, MinZoom, MaxZoom);
	}
	RequestRedraw();
}

// ----- Input -----

FReply FMapViewportClient::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	const TSharedPtr<SViewport> Widget = GetViewport();
	if (!Widget || !MapView || Drag != EDrag::None)
	{
		return FReply::Unhandled();
	}
	const TSharedRef<SWidget> Self = ToSharedRef(StaticCastSharedPtr<SWidget>(Widget));
	const FVector2 Local = Geometry.AbsoluteToLocal(Event.GetAbsolutePosition());
	DragStart = DragLast = Local;
	DragButton = Event.GetEffectingButton();
	bAltDrag = IsPerspective() && Event.IsAltDown();
	if (DragButton == EMouseButton::Left && !bAltDrag && Settings.EditorMode && Settings.EditorMode->InputPress(*this, Local, Event))
	{
		Drag = EDrag::Mode;
		LastModeTime = FPlatformTime::Seconds();
		RequestRedraw();
		return FReply::Handled().CaptureMouse(Self);
	}
	switch (DragButton)
	{
	case EMouseButton::Left:
		Drag = EDrag::Pending;
		bDragAdd = Event.IsShiftDown();
		if (Settings.Tool == EMapTool::Vertex || Settings.Tool == EMapTool::Edge || Settings.Tool == EMapTool::Face)
		{
			PickBrushDot(Local);
		}
		return FReply::Handled().CaptureMouse(Self);
	case EMouseButton::Middle:
		Drag = EDrag::Pan;
		return FReply::Handled().CaptureMouse(Self);
	case EMouseButton::Right:
		if (bAltDrag)
		{
			Drag = EDrag::Dolly;
			return FReply::Handled().CaptureMouse(Self);
		}
		if (IsPerspective())
		{
			Drag = EDrag::Look;
			bLooking = true;
			LastFlyTime = FPlatformTime::Seconds();
			RequestRedraw();
			return FReply::Handled().UseHighPrecisionMouseMovement(Self);
		}
		Drag = EDrag::Pan;
		return FReply::Handled().CaptureMouse(Self);
	default:
		break;
	}
	return FReply::Unhandled();
}

FReply FMapViewportClient::OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event)
{
	// Quick presses in an editor mode are strokes of their own, not a double click.
	return Settings.EditorMode ? OnMouseButtonDown(Geometry, Event) : FReply::Unhandled();
}

FReply FMapViewportClient::OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Drag == EDrag::None || Event.GetEffectingButton() != DragButton)
	{
		return FReply::Unhandled();
	}
	const FVector2 Local = Geometry.AbsoluteToLocal(Event.GetAbsolutePosition());
	switch (Drag)
	{
	case EDrag::Pending:
		SelectAt(Local, Event);
		break;
	case EDrag::BoxSelect:
		UpdateBoxSelection(true);
		break;
	case EDrag::Move:
		Document->EndTransaction();
		MovedObjects.clear();
		break;
	case EDrag::Look:
		bLooking = false;
		FlyKeys.clear();
		break;
	case EDrag::Mode:
		if (Settings.EditorMode)
		{
			Settings.EditorMode->InputRelease(*this);
		}
		break;
	case EDrag::Rotate:
	case EDrag::Scale:
	case EDrag::ClipLine:
	case EDrag::BrushEdit:
		EndToolDrag();
		break;
	default:
		break;
	}
	GrabbedDot = EDot::None;
	Drag = EDrag::None;
	RequestRedraw();
	return FReply::Handled().ReleaseMouseCapture();
}

FReply FMapViewportClient::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	const FVector2 Local = Geometry.AbsoluteToLocal(Event.GetAbsolutePosition());
	CursorLocal = Local;
	ReportCursor(Local);
	if (!MapView)
	{
		return FReply::Unhandled();
	}
	if (Settings.EditorMode && (Drag == EDrag::None || Drag == EDrag::Mode))
	{
		Settings.EditorMode->MouseMove(*this, Local, Event);
		RequestRedraw();
		if (Drag == EDrag::Mode)
		{
			DragLast = Local;
			return FReply::Handled();
		}
	}
	const FVector2 Delta = Local - DragLast;
	DragLast = Local;
	switch (Drag)
	{
	case EDrag::Pending:
	{
		if ((Local - DragStart).Size() < DragThreshold)
		{
			return FReply::Handled();
		}
		UpdateProjection();
		if (bAltDrag)
		{
			// Alt + left drag in 3D turns the camera about what it looks at.
			Drag = EDrag::Orbit;
			OrbitPivot = GetFocusPoint(&OrbitDistance);
			Orbit(Local - DragStart);
			return FReply::Handled();
		}
		if (BeginToolDrag(Local, Event))
		{
			return FReply::Handled();
		}
		float X, Y;
		ToScreen(DragStart, X, Y);
		const FScopedEntityDisplay Entities(ShowFlags.bEntities);
		const bool bOnSelection = !IsPerspective() && !Event.IsShiftDown() && !Event.IsControlDown()
			&& Document->GetSelector().GetNumMSelectedObjects() > 0 && MapView->IsMultiSelectionHit(X, Y);
		if (bOnSelection)
		{
			Drag = EDrag::Move;
			MovedObjects = Document->GetSelection();
			Vector3d Max;
			Document->GetSelectionBounds(MoveReference, Max);
			MoveApplied.NewVector(0.0f, 0.0f, 0.0f);
			Document->BeginTransaction(MovedObjects.size() == 1 ? "Move Object" : FString::Format("Move {} Objects", MovedObjects.size()));
			UpdateMove(Local, Settings.bSnapToGrid && !Event.IsAltDown());
		}
		else
		{
			Drag = EDrag::BoxSelect;
			SelectionBeforeBox = Document->GetSelection();
			UpdateBoxSelection(false);
		}
		return FReply::Handled();
	}
	case EDrag::BoxSelect:
		UpdateBoxSelection(false);
		return FReply::Handled();
	case EDrag::Move:
		UpdateMove(Local, Settings.bSnapToGrid && !Event.IsAltDown());
		return FReply::Handled();
	case EDrag::Pan:
		PanBy(DragButton == EMouseButton::Middle && Settings.bInvertMiddleMousePan ? Delta * -1.0f : Delta);
		return FReply::Handled();
	case EDrag::Orbit:
		Orbit(Delta);
		return FReply::Handled();
	case EDrag::Dolly:
		Dolly(Delta.X - Delta.Y);
		return FReply::Handled();
	case EDrag::Rotate:
	case EDrag::Scale:
	case EDrag::ClipLine:
	case EDrag::BrushEdit:
		UpdateToolDrag(Local, Event);
		return FReply::Handled();
	default:
		break;
	}
	return FReply::Unhandled();
}

FReply FMapViewportClient::OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (!MapView)
	{
		return FReply::Unhandled();
	}
	const float Wheel = Event.GetWheelDelta();
	if (bLooking)
	{
		// While looking around, the wheel sets how fast W/A/S/D fly.
		SetCameraSpeed(CameraSpeed * std::pow(1.25f, Wheel));
		Message(FString::Format("Camera speed {:.2f}", CameraSpeed));
		return FReply::Handled();
	}
	if (IsPerspective())
	{
		MapView->MoveRelPosition(Vector3d(0.0f, Wheel * 96.0f * FMath::Sqrt(CameraSpeed), 0.0f));
		RequestRedraw();
		return FReply::Handled();
	}
	// Zoom about the cursor: the map point under it stays put.
	const FVector2 Local = Geometry.AbsoluteToLocal(Event.GetAbsolutePosition());
	const FVector2 Center = Geometry.GetLocalSize() * 0.5f;
	const FVector2 Offset = Local - Center;
	const float OldZoom = Zoom;
	Zoom = FMath::Clamp(Zoom * std::pow(1.2f, Wheel), MinZoom, MaxZoom);
	const float Shift = 1.0f / OldZoom - 1.0f / Zoom;
	MapView->MoveRelPosition(Vector3d(Offset.X * Shift, 0.0f, -Offset.Y * Shift));
	ReportCursor(Local);
	RequestRedraw();
	return FReply::Handled();
}

FReply FMapViewportClient::OnRawMouseMove(FVector2 Delta)
{
	if (Drag != EDrag::Look || !MapView)
	{
		return FReply::Unhandled();
	}
	const FTurnSigns& Signs = GetTurnSigns();
	const float Speed = LookSpeed * Settings.MouseSensitivity;
	Yaw += Signs.Yaw * Delta.X * Speed;
	Pitch = FMath::Clamp(Pitch - Signs.Pitch * Delta.Y * Speed, DEG2RAD(-89.0f), DEG2RAD(89.0f));
	MapView->SetOrientation(SphrVector(Yaw, Pitch, 0.0f));
	RequestRedraw();
	return FReply::Handled();
}

void FMapViewportClient::Orbit(FVector2 Delta)
{
	const FTurnSigns& Signs = GetTurnSigns();
	const float Speed = OrbitSpeed * Settings.MouseSensitivity;
	Yaw += Signs.Yaw * Delta.X * Speed;
	Pitch = FMath::Clamp(Pitch - Signs.Pitch * Delta.Y * Speed, DEG2RAD(-89.0f), DEG2RAD(89.0f));
	MapView->SetOrientation(SphrVector(Yaw, Pitch, 0.0f));
	Vector3d Back = Normalized(ViewToScope(Vector3d(0.0f, -1.0f, 0.0f)));
	Back.MultVector(OrbitDistance);
	Vector3d Position(OrbitPivot);
	MapView->SetPosition(Position.AddVector(Back));
	RequestRedraw();
}

void FMapViewportClient::Dolly(float Delta)
{
	MapView->MoveRelPosition(Vector3d(0.0f, Delta * 2.0f * FMath::Sqrt(CameraSpeed), 0.0f));
	RequestRedraw();
}

void FMapViewportClient::OnMouseLeave(const FPointerEvent&)
{
	CursorLocal = FVector2(-1.0f, -1.0f);
	if (Settings.EditorMode)
	{
		Settings.EditorMode->MouseLeave(*this);
		RequestRedraw();
	}
	if (Settings.OnCursorText)
	{
		Settings.OnCursorText(FString());
	}
}

void FMapViewportClient::OnMouseCaptureLost()
{
	if (Drag == EDrag::Move && Document && Document->IsInTransaction())
	{
		Document->EndTransaction();
	}
	if (Drag == EDrag::BoxSelect && Document)
	{
		UpdateBoxSelection(true);
	}
	if (Document && (Drag == EDrag::Rotate || Drag == EDrag::Scale || Drag == EDrag::BrushEdit))
	{
		CancelToolDrag();
	}
	if (Drag == EDrag::Mode && Settings.EditorMode)
	{
		Settings.EditorMode->InputRelease(*this);
	}
	GrabbedDot = EDot::None;
	Drag = EDrag::None;
	bLooking = false;
	FlyKeys.clear();
	MovedObjects.clear();
	RequestRedraw();
}

FReply FMapViewportClient::OnKeyDown(const FGeometry&, const FKeyEvent& Event)
{
	if (ToolKeyDown(Event).IsEventHandled())
	{
		return FReply::Handled();
	}
	// Flying keys only count while looking around (right button held), so
	// the same letters stay shortcuts otherwise.
	if (bLooking)
	{
		switch (Event.GetKey())
		{
		case EKey::W: case EKey::A: case EKey::S: case EKey::D: case EKey::Q: case EKey::E:
		case EKey::Up: case EKey::Down: case EKey::Left: case EKey::Right:
		case EKey::LeftShift: case EKey::RightShift:
			FlyKeys.insert(Event.GetKey());
			return FReply::Handled();
		default:
			break;
		}
	}
	return FReply::Unhandled();
}

FReply FMapViewportClient::OnKeyUp(const FGeometry&, const FKeyEvent& Event)
{
	if (FlyKeys.erase(Event.GetKey()) > 0)
	{
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void FMapViewportClient::OnFocusLost(const FFocusEvent&)
{
	FlyKeys.clear();
}

FCursorReply FMapViewportClient::OnCursorQuery(const FGeometry&, const FPointerEvent&) const
{
	switch (Drag)
	{
	case EDrag::Move: return FCursorReply::Cursor(EMouseCursor::CardinalCross);
	case EDrag::Pan:
	case EDrag::Orbit:
	case EDrag::Dolly: return FCursorReply::Cursor(EMouseCursor::GrabHand);
	case EDrag::Look: return FCursorReply::Cursor(EMouseCursor::None);
	default: break;
	}
	return FCursorReply::Cursor(EMouseCursor::Default);
}

void FMapViewportClient::SelectAt(FVector2 Local, const FPointerEvent& Event)
{
	UpdateProjection(); // the view may have zoomed since it last drew
	const FScopedEntityDisplay Entities(ShowFlags.bEntities);
	float X, Y;
	ToScreen(Local, X, Y);
	if (Event.IsControlDown() || Event.IsShiftDown())
	{
		if (Object* Hit = MapView->SingleForwardSelect(X, Y))
		{
			Document->ToggleSelection(Hit);
		}
		return;
	}
	// Clicking again cycles through the objects under the cursor.
	Object* Hit = Event.IsAltDown() ? MapView->MultiReverseSelect(X, Y) : MapView->MultiForwardSelect(X, Y);
	Document->SetSelection(Hit ? TArray<Object*>{ Hit } : TArray<Object*>{});
}

void FMapViewportClient::UpdateBoxSelection(bool bFinal)
{
	UpdateProjection(); // the view may have zoomed since it last drew
	const FScopedEntityDisplay Entities(ShowFlags.bEntities);
	float X1, Y1, X2, Y2;
	ToScreen(DragStart, X1, Y1);
	ToScreen(DragLast, X2, Y2);
	LinkList<ObjectPtr> Found;
	MapView->DragSelectMultiObjects(FMath::Min(X1, X2), FMath::Min(Y1, Y2), FMath::Max(X1, X2), FMath::Max(Y1, Y2), Found);
	TArray<Object*> Selection = bDragAdd ? SelectionBeforeBox : TArray<Object*>{};
	for (Object* Obj : ToArray(Found))
	{
		Selection.push_back(Obj);
	}
	Document->SetSelection(Selection);
	if (bFinal)
	{
		SelectionBeforeBox.clear();
	}
	RequestRedraw();
}

void FMapViewportClient::UpdateMove(FVector2 Local, bool bSnap)
{
	const FVector2 Total = Local - DragStart;
	const Vector3d ScopeDelta = ViewToScope(Vector3d(Total.X / Zoom, 0.0f, -Total.Y / Zoom));
	Vector3d Goal(MoveReference);
	Goal.AddVector(ScopeDelta);
	{
		// Moves stay in the view's plane (rounding would drift the depth axis).
		float Sign;
		const int DepthAxis = DominantAxis(ViewToScope(Vector3d(0.0f, 1.0f, 0.0f)), Sign);
		SetAxisComponent(Goal, DepthAxis, AxisComponent(MoveReference, DepthAxis));
	}
	if (bSnap)
	{
		// Snap the selection's corner to the grid on the view's two axes.
		float Sign;
		const int RightAxis = DominantAxis(ViewToScope(Vector3d(1.0f, 0.0f, 0.0f)), Sign);
		const int UpAxis = DominantAxis(ViewToScope(Vector3d(0.0f, 0.0f, 1.0f)), Sign);
		for (const int Axis : { RightAxis, UpAxis })
		{
			const float Grid = Settings.GridSize;
			SetAxisComponent(Goal, Axis, std::round(AxisComponent(Goal, Axis) / Grid) * Grid);
		}
	}
	Vector3d Wanted(Goal);
	Wanted.SubVector(MoveReference);
	Vector3d Step(Wanted);
	Step.SubVector(MoveApplied);
	if (!(Step == Vector3d(0.0f, 0.0f, 0.0f)))
	{
		Document->MoveObjects(MovedObjects, Step);
		MoveApplied = Wanted;
	}
}

void FMapViewportClient::ReportCursor(FVector2 Local)
{
	if (!Settings.OnCursorText)
	{
		return;
	}
	Vector3d Point;
	if (!ScreenToMap(Local, Point))
	{
		Settings.OnCursorText(FString());
		return;
	}
	float Sign;
	const int RightAxis = DominantAxis(ViewToScope(Vector3d(1.0f, 0.0f, 0.0f)), Sign);
	const int UpAxis = DominantAxis(ViewToScope(Vector3d(0.0f, 0.0f, 1.0f)), Sign);
	Settings.OnCursorText(FString::Format("{} {:.0f}   {} {:.0f}", AxisName(RightAxis), AxisComponent(Point, RightAxis), AxisName(UpAxis),
		AxisComponent(Point, UpAxis)));
}

void FMapViewportClient::Fly(float DeltaSeconds)
{
	if (!MapView)
	{
		return;
	}
	const auto Held = [this](EKey A, EKey B) { return FlyKeys.contains(A) || FlyKeys.contains(B); };
	Vector3d Move(0.0f, 0.0f, 0.0f);
	if (Held(EKey::W, EKey::Up)) Move.SetY(Move.GetY() + 1.0f);
	if (Held(EKey::S, EKey::Down)) Move.SetY(Move.GetY() - 1.0f);
	if (Held(EKey::D, EKey::Right)) Move.SetX(Move.GetX() + 1.0f);
	if (Held(EKey::A, EKey::Left)) Move.SetX(Move.GetX() - 1.0f);
	if (Held(EKey::E, EKey::E)) Move.SetZ(Move.GetZ() + 1.0f);
	if (Held(EKey::Q, EKey::Q)) Move.SetZ(Move.GetZ() - 1.0f);
	const bool bFast = Held(EKey::LeftShift, EKey::RightShift);
	Move.MultVector(FlySpeed * CameraSpeed * (bFast ? 4.0f : 1.0f) * FMath::Min(DeltaSeconds, 0.1f));
	MapView->MoveRelPosition(Move);
}

// ----- Drawing -----

void FMapViewportClient::DrawGrid(FMapDraw& Draw)
{
	const FIntPoint PixelSize = GetSize();
	const float Width = static_cast<float>(PixelSize.X);
	const float Height = static_cast<float>(PixelSize.Y);
	const float OrgX = static_cast<float>(PixelSize.X / 2);
	const float OrgY = static_cast<float>(PixelSize.Y / 2);
	const float Scale = GetDpiScale();

	float RightSign, UpSign;
	const int RightAxis = DominantAxis(ViewToScope(Vector3d(1.0f, 0.0f, 0.0f)), RightSign);
	const int UpAxis = DominantAxis(ViewToScope(Vector3d(0.0f, 0.0f, 1.0f)), UpSign);
	const Vector3d& Camera = MapView->GetPosition();
	const float CameraRight = AxisComponent(Camera, RightAxis);
	const float CameraUp = AxisComponent(Camera, UpAxis);

	// Minor lines at the grid size (doubled until they are far enough apart),
	// major lines every 64 units or more.
	float Minor = FMath::Max(Settings.GridSize, 1.0f);
	while (Minor * DrawZoom < 7.0f * Scale)
	{
		Minor *= 2.0f;
	}
	float Major = 64.0f;
	while (Major < Minor * 4.0f)
	{
		Major *= 2.0f;
	}

	const auto ScreenX = [&](float Value) { return OrgX + RightSign * (Value - CameraRight) * DrawZoom; };
	const auto ScreenY = [&](float Value) { return OrgY - UpSign * (Value - CameraUp) * DrawZoom; };
	const float HalfW = OrgX / DrawZoom + Minor;
	const float HalfH = OrgY / DrawZoom + Minor;

	for (const int Pass : { 0, 1 })
	{
		const float Step = Pass == 0 ? Minor : Major;
		Draw.Color(Pass == 0 ? ColorGridMinor : ColorGridMajor);
		for (float V = std::floor((CameraRight - HalfW) / Step) * Step; V <= CameraRight + HalfW; V += Step)
		{
			if (Pass == 0 && std::fmod(std::fabs(V), Major) < 0.01f)
			{
				continue; // drawn as a major line
			}
			const int X = static_cast<int>(std::floor(ScreenX(V)));
			Draw.Line(X, 0, X, static_cast<int>(Height));
		}
		for (float V = std::floor((CameraUp - HalfH) / Step) * Step; V <= CameraUp + HalfH; V += Step)
		{
			if (Pass == 0 && std::fmod(std::fabs(V), Major) < 0.01f)
			{
				continue;
			}
			const int Y = static_cast<int>(std::floor(ScreenY(V)));
			Draw.Line(0, Y, static_cast<int>(Width), Y);
		}
	}
	Draw.Color(ColorGridAxis);
	const int AxisX = static_cast<int>(std::floor(ScreenX(0.0f)));
	const int AxisY = static_cast<int>(std::floor(ScreenY(0.0f)));
	Draw.Line(AxisX, 0, AxisX, static_cast<int>(Height));
	Draw.Line(0, AxisY, static_cast<int>(Width), AxisY);
}

void FMapViewportClient::DrawPerspectiveGrid(FMapDraw& Draw, bool bDepthTested)
{
	// Lines on the ground plane (z = 0) around the camera, clipped to the
	// near plane in view space and projected like the map core does.
	const FIntPoint PixelSize = GetSize();
	const float OrgX = static_cast<float>(FMath::Max(PixelSize.X, 1) / 2);
	const float OrgY = static_cast<float>(FMath::Max(PixelSize.Y, 1) / 2);
	const float WinWidth = GetWindowWidth();
	Matrix44 ToView;
	MapView->CalInvTransSpaceMatrix(ToView.SetIdentity());

	const Vector3d& Camera = MapView->GetPosition();
	const float Height = FMath::Max(FMath::Abs(Camera.GetZ()), 1.0f);
	float Step = FMath::Max(Settings.GridSize, 1.0f);
	while (Step < Height / 12.0f)
	{
		Step *= 2.0f;
	}
	const float Major = Step * 8.0f;
	const int32 Lines = 48;
	const float Extent = Step * static_cast<float>(Lines);
	const float CenterX = std::round(Camera.GetX() / Step) * Step;
	const float CenterY = std::round(Camera.GetY() / Step) * Step;

	const auto Segment = [&](Vector3d A, Vector3d B)
	{
		ToView.Transform(A);
		ToView.Transform(B);
		if (A.GetY() < NearPlane && B.GetY() < NearPlane)
		{
			return;
		}
		if (A.GetY() < NearPlane || B.GetY() < NearPlane)
		{
			Vector3d& Behind = A.GetY() < NearPlane ? A : B;
			const Vector3d& Ahead = A.GetY() < NearPlane ? B : A;
			const float T = (NearPlane - Behind.GetY()) / (Ahead.GetY() - Behind.GetY());
			Behind.NewVector(Behind.GetX() + (Ahead.GetX() - Behind.GetX()) * T, NearPlane, Behind.GetZ() + (Ahead.GetZ() - Behind.GetZ()) * T);
		}
		const float X1 = OrgX + A.GetX() * WinWidth / A.GetY();
		const float Y1 = OrgY - A.GetZ() * WinWidth / A.GetY();
		const float X2 = OrgX + B.GetX() * WinWidth / B.GetY();
		const float Y2 = OrgY - B.GetZ() * WinWidth / B.GetY();
		if (bDepthTested)
		{
			Draw.DepthLine(X1, Y1, 1.0f / A.GetY(), X2, Y2, 1.0f / B.GetY());
		}
		else
		{
			Draw.Line(static_cast<int>(X1), static_cast<int>(Y1), static_cast<int>(X2), static_cast<int>(Y2));
		}
	};

	for (int32 Index = -Lines; Index <= Lines; ++Index)
	{
		const float X = CenterX + static_cast<float>(Index) * Step;
		const float Y = CenterY + static_cast<float>(Index) * Step;
		for (const bool bAlongY : { true, false })
		{
			const float Value = bAlongY ? X : Y;
			const bool bAxis = FMath::Abs(Value) < 0.01f;
			const bool bMajor = std::fmod(FMath::Abs(Value), Major) < 0.01f;
			Draw.Color(bAxis ? ColorGridAxis : (bMajor ? ColorGridMajor : ColorGridMinor));
			if (bAlongY)
			{
				Segment(Vector3d(X, CenterY - Extent, 0.0f), Vector3d(X, CenterY + Extent, 0.0f));
			}
			else
			{
				Segment(Vector3d(CenterX - Extent, Y, 0.0f), Vector3d(CenterX + Extent, Y, 0.0f));
			}
		}
	}
}

void FMapViewportClient::DrawAxes(FSlateWindowElementList& Out, FIntPoint PixelSize)
{
	// X, Y and Z as seen from the camera, at the bottom left (Unreal's axis indicator).
	const float Scale = GetDpiScale();
	const FVector2 LocalSize = PixelSize.ToVector2() / Scale;
	const FVector2 Origin(26.0f, LocalSize.Y - 26.0f);
	const Vector3d Right = Normalized(ViewToScope(Vector3d(1.0f, 0.0f, 0.0f)));
	const Vector3d Up = Normalized(ViewToScope(Vector3d(0.0f, 0.0f, 1.0f)));
	const Vector3d Forward = Normalized(ViewToScope(Vector3d(0.0f, 1.0f, 0.0f)));
	struct FAxis
	{
		const char* Name;
		Vector3d Direction;
		FColor Color;
		float Depth;
	};
	FAxis Axes[3] = {
		{ "X", Vector3d(1.0f, 0.0f, 0.0f), FColor(232, 74, 74), 0.0f },
		{ "Y", Vector3d(0.0f, 1.0f, 0.0f), FColor(118, 200, 72), 0.0f },
		{ "Z", Vector3d(0.0f, 0.0f, 1.0f), FColor(72, 132, 244), 0.0f },
	};
	for (FAxis& Axis : Axes)
	{
		Axis.Depth = Axis.Direction.DotVector(Forward);
	}
	std::sort(std::begin(Axes), std::end(Axes), [](const FAxis& A, const FAxis& B) { return A.Depth > B.Depth; });

	const FSlateFontInfo Font = FAppStyle::Get().GetFont("BoldFont");
	FSlateFontCache& Fonts = FSlateApplication::Get().GetFontCache();
	const FPaintGeometry Whole = FPaintGeometry::Make(FVector2::Zero(), LocalSize, Scale);
	for (const FAxis& Axis : Axes)
	{
		const FVector2 Direction(Axis.Direction.DotVector(Right), -Axis.Direction.DotVector(Up));
		if (Direction.Size() < 0.15f)
		{
			continue; // pointing at or away from the camera
		}
		const FVector2 Tip = Origin + Direction * 18.0f;
		Out.AddLines(6, Whole, { Origin, Tip }, Axis.Color, 2.0f);
		if (Font.Font)
		{
			const TSharedRef<const FShapedGlyphSequence> Label = Fonts.ShapeText(FString(Axis.Name).View(), FSlateFontInfo(Font.Font, 10.0f), Scale);
			const FVector2 LabelSize = Label->GetLogicalSize();
			const FVector2 At = Origin + Direction * 27.0f - LabelSize * 0.5f;
			Out.AddText(7, FPaintGeometry::Make(At, LabelSize, Scale), Label, Axis.Color);
		}
	}
}

void FMapViewportClient::DrawScaleBar(FSlateWindowElementList& Out, FIntPoint PixelSize)
{
	// A power-of-two length about 100 logical pixels long, next to the axes.
	const float Scale = GetDpiScale();
	const FVector2 LocalSize = PixelSize.ToVector2() / Scale;
	const float Units = std::exp2(std::round(std::log2(100.0f / Zoom)));
	const float Length = Units * Zoom;
	if (Length < 8.0f || LocalSize.X < Length + 80.0f)
	{
		return;
	}
	const FVector2 Start(64.0f, LocalSize.Y - 14.0f);
	const FVector2 End = Start + FVector2(Length, 0.0f);
	const FColor Color(176, 184, 198);
	const FPaintGeometry Whole = FPaintGeometry::Make(FVector2::Zero(), LocalSize, Scale);
	Out.AddLines(6, Whole, { Start - FVector2(0.0f, 5.0f), Start, End, End - FVector2(0.0f, 5.0f) }, Color, 1.0f);
	const FSlateFontInfo Font = FAppStyle::Get().GetFont("NormalFont");
	if (Font.Font)
	{
		const FString Text = FString::Format("{:.0f} units", Units);
		const TSharedRef<const FShapedGlyphSequence> Label = FSlateApplication::Get().GetFontCache().ShapeText(Text.View(), FSlateFontInfo(Font.Font, 10.0f), Scale);
		const FVector2 LabelSize = Label->GetLogicalSize();
		Out.AddText(7, FPaintGeometry::Make(FVector2(Start.X + (Length - LabelSize.X) * 0.5f, Start.Y - 6.0f - LabelSize.Y), LabelSize, Scale), Label, Color);
	}
}

void FMapViewportClient::DrawOverlay(FSlateWindowElementList& Out, FIntPoint PixelSize)
{
	const float Scale = GetDpiScale();
	// Landscape splines, in every mode (the Landscape mode marks its pick).
	if (Document)
	{
		DrawLandscapeSplines(*Document, *this, Out, Scale);
	}
	const auto Geo = [Scale](FVector2 Position, FVector2 Extent) { return FPaintGeometry::Make(Position, Extent, Scale); };

	if (Drag == EDrag::BoxSelect)
	{
		const FVector2 Min(FMath::Min(DragStart.X, DragLast.X), FMath::Min(DragStart.Y, DragLast.Y));
		const FVector2 Max(FMath::Max(DragStart.X, DragLast.X), FMath::Max(DragStart.Y, DragLast.Y));
		Out.AddSolidBox(2, Geo(Min, Max - Min), FColor(120, 170, 255, 40));
		Out.AddLines(3, Geo(FVector2::Zero(), PixelSize.ToVector2() / Scale), { Min, FVector2(Max.X, Min.Y), Max, FVector2(Min.X, Max.Y) },
			FColor(120, 170, 255), 1.0f, true);
	}
	if (MapView && Settings.EditorMode)
	{
		Settings.EditorMode->DrawOverlay(*this, Out, Scale);
	}
	if (MapView && ShowFlags.bAxes)
	{
		DrawAxes(Out, PixelSize);
		if (!IsPerspective())
		{
			DrawScaleBar(Out, PixelSize);
		}
	}

	const FSlateFontInfo Font = FAppStyle::Get().GetFont("NormalFont");
	if (!Font.Font || !ShowFlags.bViewportStats)
	{
		return;
	}
	// The view's type and mode are in its toolbar; the label says the rest.
	TArray<FString> Parts;
	if (Settings.EditorMode && !Settings.EditorMode->GetHint().IsEmpty())
	{
		Parts.push_back(Settings.EditorMode->GetHint());
	}
	if (IsPerspective())
	{
		if (bLooking)
		{
			Parts.push_back(FString::Format("Camera speed {:.2f}", CameraSpeed));
		}
	}
	else if (MapView)
	{
		float Sign;
		const int RightAxis = DominantAxis(ViewToScope(Vector3d(1.0f, 0.0f, 0.0f)), Sign);
		const int UpAxis = DominantAxis(ViewToScope(Vector3d(0.0f, 0.0f, 1.0f)), Sign);
		Parts.push_back(FString::Format("{}/{}  ·  {:.0f}%", AxisName(RightAxis), AxisName(UpAxis), Zoom * 100.0f));
		const FString Hint = GetToolHint();
		if (!Hint.IsEmpty())
		{
			Parts.push_back(Hint);
		}
	}
	if (Document && &Document->GetScope() != &Document->GetRoot())
	{
		const char* Name = Document->GetScope().GetObjName();
		Parts.push_back(FString::Format("in {}", Name && Name[0] ? FString::FromAnsi(Name) : FString("a group")));
	}
	if (Parts.empty())
	{
		return;
	}
	FString Label = Parts[0];
	for (SIZE_T Index = 1; Index < Parts.size(); ++Index)
	{
		Label += FString::Format("  ·  {}", Parts[Index]);
	}
	FSlateFontCache& Fonts = FSlateApplication::Get().GetFontCache();
	const TSharedRef<const FShapedGlyphSequence> Text = Fonts.ShapeText(Label.View(), FSlateFontInfo(Font.Font, 11.0f), Scale);
	const FVector2 TextSize = Text->GetLogicalSize();
	Out.AddRoundedBox(4, Geo(FVector2(6.0f, 6.0f), TextSize + FVector2(12.0f, 6.0f)), FColor(12, 13, 16, 190), 4.0f);
	Out.AddText(5, Geo(FVector2(12.0f, 9.0f), TextSize), Text, FColor(176, 184, 198));
}

void FMapViewportClient::Draw(FRHICommandList& CommandList, FSlateRenderTargetRHI& RenderTarget)
{
	const FIntPoint PixelSize = RenderTarget.GetSize();
	const FLinearColor Clear(BackgroundColor.R / 255.0f, BackgroundColor.G / 255.0f, BackgroundColor.B / 255.0f, 1.0f);
	Elements.Reset();
	Elements.SetViewportSize(PixelSize);
	if (!MapView || !Document)
	{
		Renderer.Render(CommandList, RenderTarget.GetTexture(), Elements, Clear);
		return;
	}

	if (bLooking)
	{
		const double Now = FPlatformTime::Seconds();
		Fly(static_cast<float>(Now - LastFlyTime));
		LastFlyTime = Now;
	}
	if (Drag == EDrag::Mode && Settings.EditorMode)
	{
		const double Now = FPlatformTime::Seconds();
		Settings.EditorMode->Tick(*this, static_cast<float>(FMath::Min(Now - LastModeTime, 0.1)));
		LastModeTime = Now;
	}

	UpdateProjection();
	MapDraw->SetLineThickness(FMath::Max(1.0f, std::floor(GetDpiScale() + 0.25f)));
	MapDraw->Reset();
	MapDraw->bInstancing = Resources.bInstanceMeshes;
	const FScopedEntityDisplay Entities(ShowFlags.bEntities);
	const EMapViewMode Mode = GetViewMode();
	const bool bSolid = Mode != EMapViewMode::Wireframe;
	const bool bGrid = Settings.bShowGrid && ShowFlags.bGrid;
	if (bSolid)
	{
		MapDraw->SetShaded(Mode == EMapViewMode::Lit);
		MapDraw->bMaterials = Resources.bMeshMaterials && Resources.GetMaterialPipeline(RenderTarget.GetTexture().GetFormat()) != nullptr;
		MapDraw->ZBufTextured(false);
		MapView->RenderSolid(false);
		if (bGrid)
		{
			DrawPerspectiveGrid(*MapDraw, true);
		}
		// The selection on top of the faces.
		MapView->RenderWireFrame(false, true, false);
	}
	else
	{
		if (bGrid)
		{
			if (IsPerspective())
			{
				DrawPerspectiveGrid(*MapDraw, false);
			}
			else
			{
				DrawGrid(*MapDraw);
			}
		}
		UpdateDotDisplay();
		MapView->RenderWireFrame(true, true, false);
	}
	DrawTools(*MapDraw);
	MapDraw->FlushLines(Elements, 1);
	DrawOverlay(Elements, PixelSize);

	const TArray<FMapVertex>& Triangles = MapDraw->GetTriangles();
	const auto& MaterialVertices = MapDraw->GetMaterialVertices();
	const auto& MaterialBatches = MapDraw->GetMaterialBatches();
	const TArray<FMapInstanceBatch>& InstanceBatches = MapDraw->GetInstanceBatches();
	FRHIGraphicsPipeline* Pipeline = bSolid && (!Triangles.empty() || !InstanceBatches.empty() || !MaterialVertices.empty())
		? Resources.GetSolidPipeline(RenderTarget.GetTexture().GetFormat()) : nullptr;
	Resources.Trim();
	if (!Pipeline)
	{
		Renderer.Render(CommandList, RenderTarget.GetTexture(), Elements, Clear);
		return;
	}

	if (!DepthBuffer || DepthBuffer->GetWidth() != static_cast<uint32>(PixelSize.X) || DepthBuffer->GetHeight() != static_cast<uint32>(PixelSize.Y))
	{
		if (DepthBuffer)
		{
			Device.DeferRelease(DepthBuffer);
		}
		FRHITextureDesc DepthDesc;
		DepthDesc.Width = static_cast<uint32>(PixelSize.X);
		DepthDesc.Height = static_cast<uint32>(PixelSize.Y);
		DepthDesc.Format = EPixelFormat::D32_FLOAT;
		DepthDesc.DebugName = "Map view depth";
		DepthBuffer = Device.CreateTexture(DepthDesc);
	}

	// One-frame buffers, released once the GPU is done with them.
	// (One vertex at least: a frame may have only instanced meshes.)
	FRHIBufferDesc VertexDesc;
	VertexDesc.Size = FMath::Max<SIZE_T>(Triangles.size(), 1) * sizeof(FMapVertex);
	VertexDesc.Usage = EBufferUsage::Vertex;
	VertexDesc.DebugName = "Map faces";
	TSharedPtr<FRHIBuffer> Vertices = Device.CreateBuffer(VertexDesc);
	TArray<uint32> IndexData(FMath::Max<SIZE_T>(FMath::Max(Triangles.size(),MaterialVertices.size()), 1), 0);
	for (SIZE_T Index = 0; Index < IndexData.size(); ++Index)
	{
		IndexData[Index] = static_cast<uint32>(Index);
	}
	FRHIBufferDesc IndexDesc;
	IndexDesc.Size = IndexData.size() * sizeof(uint32);
	IndexDesc.Usage = EBufferUsage::Index;
	IndexDesc.DebugName = "Map face indices";
	TSharedPtr<FRHIBuffer> Indices = Device.CreateBuffer(IndexDesc);
	if (!DepthBuffer || !Vertices || !Indices)
	{
		Renderer.Render(CommandList, RenderTarget.GetTexture(), Elements, Clear);
		return;
	}
	std::memcpy(Vertices->GetMappedData(), Triangles.data(), Triangles.size() * sizeof(FMapVertex));
	std::memcpy(Indices->GetMappedData(), IndexData.data(), IndexDesc.Size);

	TSharedPtr<FRHIBuffer> MaterialBuffer;
	FRHIGraphicsPipeline* MaterialPipeline=MaterialVertices.empty()?nullptr:Resources.GetMaterialPipeline(RenderTarget.GetTexture().GetFormat());
	struct FMaterialDraw { FRHITexture* Color=nullptr; FRHITexture* Normal=nullptr; bool Valid=false; };
	TArray<FMaterialDraw> MaterialDraws(MaterialBatches.size());
	FRHIGraphicsPipeline* TransparentPipeline = MaterialPipeline ? Resources.GetMaterialPipeline(RenderTarget.GetTexture().GetFormat(), true) : nullptr;
	if(MaterialPipeline) {
		FRHIBufferDesc Desc; Desc.Size=MaterialVertices.size()*sizeof(FMapMaterialVertex); Desc.Usage=EBufferUsage::Vertex; Desc.DebugName="Native material vertices";
		MaterialBuffer=Device.CreateBuffer(Desc);
		if(MaterialBuffer) {
			std::memcpy(MaterialBuffer->GetMappedData(),MaterialVertices.data(),Desc.Size);
			for(size_t I=0; I<MaterialBatches.size(); ++I) MaterialDraws[I].Valid=Resources.GetMaterialTextures(MaterialBatches[I].Mesh->GetMeshAsset(),MaterialDraws[I].Color,MaterialDraws[I].Normal);
		}
	}

	// Instanced meshes: each copy's rows in one buffer, a mesh's together;
	// the meshes' own buffers made (once) before the pass.
	struct FInstanceDraw
	{
		const FMapRenderResources::FMeshBuffers* Mesh = nullptr;
		uint32 First = 0;
		uint32 Count = 0;
	};
	TArray<FInstanceDraw> InstanceDraws;
	TSharedPtr<FRHIBuffer> InstanceRows;
	FRHIGraphicsPipeline* InstancedPipeline = InstanceBatches.empty() ? nullptr : Resources.GetInstancedPipeline(RenderTarget.GetTexture().GetFormat());
	if (InstancedPipeline)
	{
		uint32 Total = 0;
		for (const FMapInstanceBatch& Batch : InstanceBatches)
		{
			if (const FMapRenderResources::FMeshBuffers* Mesh = Resources.GetMeshBuffers(*Batch.Mesh))
			{
				InstanceDraws.push_back({ Mesh, Total, Batch.Count() });
			}
			Total += Batch.Count();
		}
		FRHIBufferDesc RowsDesc;
		RowsDesc.Size = SIZE_T(Total) * 12 * sizeof(float);
		RowsDesc.Usage = EBufferUsage::Vertex;
		RowsDesc.DebugName = "Map instances";
		InstanceRows = Total > 0 ? Device.CreateBuffer(RowsDesc) : nullptr;
		if (InstanceRows)
		{
			uint8* Out = static_cast<uint8*>(InstanceRows->GetMappedData());
			for (const FMapInstanceBatch& Batch : InstanceBatches)
			{
				std::memcpy(Out, Batch.Rows.data(), Batch.Rows.size() * sizeof(float));
				Out += Batch.Rows.size() * sizeof(float);
			}
		}
		else
		{
			InstanceDraws.clear();
		}
	}

	// Textured landscapes: their layers for the GPU to blend, or else their
	// baked images, brought up to date before the pass.
	struct FBatchDraw
	{
		FRHITexture* Texture = nullptr; // baked, or none (white)
		FRHITexture* Weights = nullptr; // with Layers: blended per pixel
		FRHITexture* Layers = nullptr;
		float LandConstants[12] = {};
	};
	const TArray<FMapTriangleBatch>& Batches = MapDraw->GetBatches();
	TArray<FBatchDraw> Draws(Batches.size());
	FRHIGraphicsPipeline* LandscapePipeline = Resources.GetLandscapePipeline(RenderTarget.GetTexture().GetFormat());
	for (SIZE_T Index = 0; Index < Batches.size(); ++Index)
	{
		const Object* Land = Batches[Index].Landscape;
		if (!Land)
		{
			continue;
		}
		FBatchDraw& Draw = Draws[Index];
		if (LandscapePipeline && Resources.GetLandscapeLayers(*Land, Draw.Weights, Draw.Layers))
		{
			FLandscapeLayerImages::GetConstants(Land->GetLandscape(), Draw.LandConstants);
			continue;
		}
		Draw.Weights = Draw.Layers = nullptr;
		Draw.Texture = Resources.GetLandscapeTexture(*Land);
	}

	FRHITexture& White = Resources.GetWhiteTexture();
	CommandList.BeginRenderPass(RenderTarget.GetTexture(), *DepthBuffer, Clear, 1.0f);
	float Constants[16] = { 2.0f / static_cast<float>(PixelSize.X), 2.0f / static_cast<float>(PixelSize.Y), 0.0f, 0.0f };
	FRHIGraphicsPipeline* Bound = nullptr;
	for (SIZE_T Index = 0; Index < Batches.size(); ++Index)
	{
		const FBatchDraw& Draw = Draws[Index];
		FRHIGraphicsPipeline* Wanted = Draw.Layers ? LandscapePipeline : Pipeline;
		if (Wanted != Bound)
		{
			CommandList.SetPipeline(*Wanted);
			CommandList.SetVertexBuffer(*Vertices, 0, sizeof(FMapVertex));
			CommandList.SetIndexBuffer(*Indices, 0, EIndexFormat::UInt32);
			Bound = Wanted;
		}
		if (Draw.Layers)
		{
			std::memcpy(&Constants[4], Draw.LandConstants, sizeof(Draw.LandConstants));
			CommandList.SetPushConstants(Constants, sizeof(Constants));
			CommandList.SetTexture(White, 0);
			CommandList.SetTexture(*Draw.Weights, 1);
			CommandList.SetTexture(*Draw.Layers, 2);
		}
		else
		{
			CommandList.SetPushConstants(Constants, sizeof(float) * 4);
			CommandList.SetTexture(Draw.Texture ? *Draw.Texture : White);
		}
		CommandList.DrawIndexed(Batches[Index].NumVertices, Batches[Index].FirstVertex, 0);
	}
	auto DrawMaterials = [&](bool Transparent) {
		if (MaterialBuffer && MaterialPipeline)
		{
			if (Transparent && !TransparentPipeline)
				return;
			CommandList.SetPipeline(Transparent ? *TransparentPipeline : *MaterialPipeline);
			CommandList.SetVertexBuffer(*MaterialBuffer, 0, sizeof(FMapMaterialVertex));
			CommandList.SetIndexBuffer(*Indices, 0, EIndexFormat::UInt32);
			const auto& Light = MapDraw->GetInstanceLight();
			const auto& Up = MapDraw->GetInstanceUp();
			TArray<size_t> Order;
			for (size_t I = 0; I < MaterialBatches.size(); ++I)
			{
				const auto M = MaterialBatches[I].Mesh->GetMeshAsset()->Material;
				if ((M && M->BlendMode == EMaterialBlendMode::Translucent) == Transparent)
					Order.push_back(I);
			}
			if (Transparent)
				std::stable_sort(Order.begin(), Order.end(), [&](size_t A, size_t B) {
					return MaterialBatches[A].Depth > MaterialBatches[B].Depth;
				});
			for (size_t I : Order)
			{
				const auto& Draw = MaterialDraws[I];
				if (!Draw.Valid)
					continue;
				const auto Asset = MaterialBatches[I].Mesh->GetMeshAsset();
				const FMaterialAsset Material = Asset->Material.value_or(FMaterialAsset{});
				const int Flags = (MapDraw->IsShaded() ? 1 : 0) | (Asset->NormalTexture ? 4 : 0) |
								  (Material.BlendMode == EMaterialBlendMode::Masked ? 8 : 0) | (Transparent ? 16 : 0) |
								  (static_cast<int>(Material.OpacityMaskClip * 65535.0f + 0.5f) << 5);
				const float Params[16] = {Constants[0],
										  Constants[1],
										  MapDraw->GetInstanceWinWidth(),
										  static_cast<float>(Flags),
										  Material.BaseColor[0],
										  Material.BaseColor[1],
										  Material.BaseColor[2],
										  Material.BaseColor[3],
										  Light.GetX(),
										  Light.GetY(),
										  Light.GetZ(),
										  Material.Roughness,
										  Up.GetX(),
										  Up.GetY(),
										  Up.GetZ(),
										  Material.Metallic};
				CommandList.SetPushConstants(Params, sizeof(Params));
				CommandList.SetTexture(*Draw.Color, 0);
				CommandList.SetTexture(*Draw.Normal, 1);
				CommandList.DrawIndexed(MaterialBatches[I].NumVertices, MaterialBatches[I].FirstVertex, 0);
			}
		}
	};
	DrawMaterials(false);
	if (!InstanceDraws.empty())
	{
		// Projected and lit as View would (MapDraw has View's numbers).
		const FVector2 Origin = MapDraw->GetOrigin();
		const Vector3d& Light = MapDraw->GetInstanceLight();
		const Vector3d& Up = MapDraw->GetInstanceUp();
		const float InstanceConstants[16] = {Constants[0],
											 Constants[1],
											 0.0f,
											 0.0f,
											 Origin.X,
											 Origin.Y,
											 MapDraw->GetInstanceWinWidth(),
											 MapDraw->IsShaded() ? 1.0f : 0.0f,
											 Light.GetX(),
											 Light.GetY(),
											 Light.GetZ(),
											 0.0f,
											 Up.GetX(),
											 Up.GetY(),
											 Up.GetZ(),
											 0.0f};
		CommandList.SetPipeline(*InstancedPipeline);
		CommandList.SetPushConstants(InstanceConstants, sizeof(InstanceConstants));
		CommandList.SetTexture(White);
		for (const FInstanceDraw& Draw : InstanceDraws)
		{
			CommandList.SetVertexBuffer(*Draw.Mesh->Vertices, 0, sizeof(FInstancedVertex), 0);
			CommandList.SetVertexBuffer(*InstanceRows, 0, 12 * sizeof(float), 1);
			CommandList.SetIndexBuffer(*Draw.Mesh->Indices, 0, EIndexFormat::UInt32);
			CommandList.DrawIndexedInstanced(Draw.Mesh->IndexCount, Draw.Count, 0, 0, Draw.First);
		}
		Device.DeferRelease(InstanceRows);
	}
	DrawMaterials(true);
	CommandList.EndRenderPass();
	if(MaterialBuffer) Device.DeferRelease(MaterialBuffer);
	Device.DeferRelease(Vertices);
	Device.DeferRelease(Indices);

	// Lines and labels over the faces.
	Renderer.Render(CommandList, RenderTarget.GetTexture(), Elements, {});
}
