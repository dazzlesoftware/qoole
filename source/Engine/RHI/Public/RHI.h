// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// A thin GPU abstraction (docs/GenesisUI.md §5.3): device + queue, one
// swapchain per platform window, buffers, 2D textures and render targets,
// pipelines from precompiled shaders and a command list with
// viewport/scissor/draw-indexed. Backends: D3D12 and Vulkan, chosen at
// startup.
//
// Binding model (deliberately fixed and small): every pipeline sees
//   - up to 64 bytes of push constants      (HLSL: cbuffer at b0 / [[vk::push_constant]])
//   - one 2D texture                         (t0 / [[vk::binding(0)]])
//   - one sampler chosen at pipeline creation (s0 / [[vk::binding(1)]])
// It grows when the engine renderer needs more.
//
// Frame model: BeginFrame() returns the frame's command list (it waits until
// the GPU has finished the frame that used the same resources,
// MaxFramesInFlight frames ago). Record passes into swapchains and render
// targets, then EndFrame() submits and presents the swapchains that were
// drawn to.

#pragma once

#include "CoreMinimal.h"

class IPlatformWindow;

DECLARE_LOG_CATEGORY_EXTERN(LogRHI, Log);

enum class ERHIBackend : uint8
{
	D3D12,
	Vulkan,
};

const char* ToString(ERHIBackend Backend);

enum class EPixelFormat : uint8
{
	Unknown,
	R8_UNORM,
	R8G8B8A8_UNORM,
	B8G8R8A8_UNORM,
	D32_FLOAT, // depth buffers only (see FRHITextureDesc)
};

uint32 GetBytesPerPixel(EPixelFormat Format);
inline bool IsDepthFormat(EPixelFormat Format) { return Format == EPixelFormat::D32_FLOAT; }

enum class EBufferUsage : uint8
{
	Vertex,
	Index,
};

enum class EBufferMemory : uint8
{
	Upload,  // CPU-writable, GPU-readable; map it every frame (use one per frame in flight)
	Default, // GPU-only; contents come from InitialData
};

struct FRHIBufferDesc
{
	uint64 Size = 0;
	EBufferUsage Usage = EBufferUsage::Vertex;
	EBufferMemory Memory = EBufferMemory::Upload;
	const void* InitialData = nullptr; // required for Default, optional for Upload
	const char* DebugName = nullptr;
};

class FRHIBuffer
{
public:
	virtual ~FRHIBuffer() = default;
	const FRHIBufferDesc& GetDesc() const { return Desc; }
	uint64 GetSize() const { return Desc.Size; }
	// Persistent CPU pointer for Upload buffers; nullptr for Default ones.
	virtual void* GetMappedData() = 0;

protected:
	explicit FRHIBuffer(const FRHIBufferDesc& InDesc) : Desc(InDesc) {}
	FRHIBufferDesc Desc;
};

struct FRHITextureDesc
{
	uint32 Width = 1;
	uint32 Height = 1;
	EPixelFormat Format = EPixelFormat::R8G8B8A8_UNORM;
	bool bRenderTarget = false;          // can be drawn into with BeginRenderPass
	const void* InitialData = nullptr;   // tightly packed rows unless InitialRowPitch is set
	uint32 InitialRowPitch = 0;
	FLinearColor ClearColor = FLinearColor::Transparent(); // optimized clear value for render targets
	const char* DebugName = nullptr;
};

// A sampled 2D texture, optionally also a render target. Outside a render
// pass, textures are always ready to sample. A D32_FLOAT texture is a depth
// buffer instead: it cannot be sampled, only passed to BeginRenderPass.
class FRHITexture
{
public:
	virtual ~FRHITexture() = default;
	const FRHITextureDesc& GetDesc() const { return Desc; }
	uint32 GetWidth() const { return Desc.Width; }
	uint32 GetHeight() const { return Desc.Height; }
	EPixelFormat GetFormat() const { return Desc.Format; }

protected:
	explicit FRHITexture(const FRHITextureDesc& InDesc) : Desc(InDesc) { Desc.InitialData = nullptr; }
	FRHITextureDesc Desc;
};

// A window's swapchain. Sizes are in physical pixels.
class FRHISwapChain
{
public:
	virtual ~FRHISwapChain() = default;

	// Call when the window's client size changes (and so after DPI changes).
	// A zero size (minimized) is fine; the swapchain is skipped until resized.
	virtual void Resize(uint32 Width, uint32 Height) = 0;
	virtual FIntPoint GetSize() const = 0;
	virtual EPixelFormat GetFormat() const = 0;
	// Whether the swapchain can be drawn into this frame (not minimized).
	virtual bool IsDrawable() const = 0;
	IPlatformWindow* GetWindow() const { return Window; }

protected:
	explicit FRHISwapChain(IPlatformWindow* InWindow) : Window(InWindow) {}
	IPlatformWindow* Window;
};

struct FRHIShaderBytecode
{
	const void* Data = nullptr;
	SIZE_T Size = 0;
};

enum class EVertexFormat : uint8
{
	Float2,
	Float3,
	Float4,
	UByte4N, // 4 x uint8, normalized to [0, 1]
};

struct FVertexAttribute
{
	const char* SemanticName = "TEXCOORD"; // D3D12 semantic; Vulkan uses the location (attribute index)
	uint32 SemanticIndex = 0;
	EVertexFormat Format = EVertexFormat::Float2;
	uint32 Offset = 0;
};

enum class EBlendMode : uint8
{
	Opaque,
	AlphaBlend,        // src * a + dst * (1 - a)
	PremultipliedAlpha // src + dst * (1 - a)
};

enum class ESamplerFilter : uint8
{
	Linear,
	Point,
};

struct FRHIGraphicsPipelineDesc
{
	FRHIShaderBytecode VertexShader;
	FRHIShaderBytecode PixelShader;
	TArray<FVertexAttribute> VertexAttributes;
	uint32 VertexStride = 0;
	EBlendMode BlendMode = EBlendMode::AlphaBlend;
	ESamplerFilter SamplerFilter = ESamplerFilter::Linear;
	EPixelFormat RenderTargetFormat = EPixelFormat::R8G8B8A8_UNORM;
	uint32 PushConstantSize = 0; // bytes, multiple of 4, at most 64
	// D32_FLOAT for pipelines used in passes with a depth buffer; the test is
	// "less or equal" and passes with depth draw only with such pipelines.
	EPixelFormat DepthFormat = EPixelFormat::Unknown;
	bool bDepthWrite = true;
	const char* DebugName = nullptr;
};

class FRHIGraphicsPipeline
{
public:
	virtual ~FRHIGraphicsPipeline() = default;
	const FRHIGraphicsPipelineDesc& GetDesc() const { return Desc; }

protected:
	explicit FRHIGraphicsPipeline(const FRHIGraphicsPipelineDesc& InDesc) : Desc(InDesc) {}
	FRHIGraphicsPipelineDesc Desc;
};

enum class EIndexFormat : uint8
{
	UInt16,
	UInt32,
};

// Records the work of one frame. Only valid between BeginFrame and EndFrame.
class FRHICommandList
{
public:
	virtual ~FRHICommandList() = default;

	// Draw into a swapchain's current back buffer (must be IsDrawable()).
	virtual void BeginRenderPass(FRHISwapChain& Target, const TOptional<FLinearColor>& ClearColor) = 0;
	// Draw into a render target texture.
	virtual void BeginRenderPass(FRHITexture& Target, const TOptional<FLinearColor>& ClearColor) = 0;
	// Draw into a render target texture with a depth buffer of the same size
	// (D32_FLOAT); ClearDepth unset keeps its contents.
	virtual void BeginRenderPass(FRHITexture& Target, FRHITexture& DepthTarget, const TOptional<FLinearColor>& ClearColor, const TOptional<float>& ClearDepth) = 0;
	virtual void EndRenderPass() = 0;

	virtual void SetViewport(float X, float Y, float Width, float Height) = 0;
	virtual void SetScissor(const FIntRect& Rect) = 0;
	virtual void SetPipeline(FRHIGraphicsPipeline& Pipeline) = 0;
	virtual void SetPushConstants(const void* Data, uint32 Size) = 0;
	virtual void SetTexture(FRHITexture& Texture) = 0;
	virtual void SetVertexBuffer(FRHIBuffer& Buffer, uint64 Offset, uint32 Stride) = 0;
	virtual void SetIndexBuffer(FRHIBuffer& Buffer, uint64 Offset, EIndexFormat Format) = 0;
	virtual void DrawIndexed(uint32 IndexCount, uint32 FirstIndex, int32 BaseVertex) = 0;
};

struct FRHIDeviceDesc
{
	ERHIBackend Backend = ERHIBackend::D3D12;
	bool bDebugLayer = GENESIS_DEBUG != 0; // D3D12 debug layer / Vulkan validation, when installed
	bool bSoftwareAdapter = false;         // D3D12 WARP (headless tests); ignored by Vulkan
};

class FRHIDevice
{
public:
	static constexpr uint32 MaxFramesInFlight = 2;

	// Returns nullptr (and logs why) when the backend is unavailable.
	static TUniquePtr<FRHIDevice> Create(const FRHIDeviceDesc& Desc);

	virtual ~FRHIDevice() = default;

	ERHIBackend GetBackend() const { return Backend; }
	virtual FString GetAdapterName() const = 0;

	virtual TUniquePtr<FRHISwapChain> CreateSwapChain(IPlatformWindow& Window) = 0;
	virtual TSharedPtr<FRHIBuffer> CreateBuffer(const FRHIBufferDesc& Desc) = 0;
	virtual TSharedPtr<FRHITexture> CreateTexture(const FRHITextureDesc& Desc) = 0;
	virtual TSharedPtr<FRHIGraphicsPipeline> CreateGraphicsPipeline(const FRHIGraphicsPipelineDesc& Desc) = 0;

	// Copies pixels into a region of a texture. The copy happens on the GPU
	// timeline before the next submitted frame; Data may be freed on return.
	// RowPitch 0 means tightly packed.
	virtual void UpdateTexture(FRHITexture& Texture, const FIntRect& Region, const void* Data, uint32 RowPitch = 0) = 0;

	// Waits for the GPU and copies a texture's pixels back (tightly packed
	// rows). Slow; meant for tests and screenshots.
	virtual bool ReadbackTexture(FRHITexture& Texture, TArray<uint8>& OutPixels) = 0;

	// Starts a frame; see the header comment.
	virtual FRHICommandList& BeginFrame() = 0;
	// Submits the frame and presents every swapchain drawn into.
	virtual void EndFrame() = 0;
	// Index of the current frame in flight, in [0, MaxFramesInFlight).
	virtual uint32 GetFrameIndex() const = 0;
	// Blocks until the GPU is idle (before destroying resources in bulk).
	virtual void WaitIdle() = 0;

	// Resources released while a frame may still use them must be kept
	// alive; this keeps a reference until the GPU is done with the frame.
	virtual void DeferRelease(TSharedPtr<void> Resource) = 0;

protected:
	explicit FRHIDevice(ERHIBackend InBackend) : Backend(InBackend) {}
	ERHIBackend Backend;
};

// Parses "-d3d12" / "-vulkan" style flags; returns Default when absent.
ERHIBackend ParseBackendFromCommandLine(std::string_view CommandLine, ERHIBackend Default = ERHIBackend::D3D12);
