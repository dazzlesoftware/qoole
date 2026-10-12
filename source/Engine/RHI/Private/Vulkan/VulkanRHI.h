// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Vulkan backend of the RHI (see RHI.h). Requires Vulkan 1.1 and
// VK_KHR_swapchain; vulkan-1.dll is loaded at runtime (VulkanLoader.h).
//
// Conventions shared with the shaders (HLSL compiled by DXC with -spirv
// -fspv-target-env=vulkan1.1):
//   - push constants: [[vk::push_constant]] struct, up to 64 bytes, VS + PS
//   - set 0, binding 0: [[vk::binding(0)]] Texture2D
//   - set 0, binding 1: [[vk::binding(1)]] SamplerState (an immutable sampler,
//     linear or point, clamp-to-edge, picked by the pipeline's SamplerFilter)
//   - vertex attribute location N = FRHIGraphicsPipelineDesc::VertexAttributes[N]
//     (DXC assigns locations in declaration order of the VS inputs, or use
//     [[vk::location(N)]])
//   - Y flip: SetViewport uses a negative viewport height (core since 1.1), so
//     the same HLSL as D3D12 works unchanged. Do NOT compile the vertex
//     shaders with -fvk-invert-y.
//   - the shader entry point names are read from the SPIR-V (OpEntryPoint),
//     so any -E name works.
//   - R8_UNORM textures sample as (1, 1, 1, R), like the D3D12 backend.
//
// Synchronization model:
//   - Textures are in SHADER_READ_ONLY_OPTIMAL outside render passes; render
//     passes into a texture transition it to COLOR_ATTACHMENT_OPTIMAL and back.
//   - Uploads (UpdateTexture, initial data) recorded inside a frame go into a
//     per-frame upload command buffer submitted just before the frame's main
//     command buffer; outside a frame they are submitted immediately and
//     waited for.
//   - Native objects are never destroyed directly: they go to a garbage list
//     tagged with the serial of the frame being recorded and are destroyed
//     once the GPU has finished that frame.

#pragma once

#include "RHIPrivate.h"

#include "Vulkan/VulkanLoader.h"

#include <deque>

class FVulkanDevice;

class FVulkanBuffer final : public FRHIBuffer
{
public:
	FVulkanBuffer(FVulkanDevice& InDevice, const FRHIBufferDesc& InDesc);
	~FVulkanBuffer() override;

	bool Initialize();
	void* GetMappedData() override { return MappedData; }
	VkBuffer GetHandle() const { return Buffer; }

private:
	FVulkanDevice& Device;
	VkBuffer Buffer = VK_NULL_HANDLE;
	VkDeviceMemory Memory = VK_NULL_HANDLE;
	void* MappedData = nullptr;
};

class FVulkanTexture final : public FRHITexture
{
public:
	FVulkanTexture(FVulkanDevice& InDevice, const FRHITextureDesc& InDesc);
	~FVulkanTexture() override;

	bool Initialize(const void* InitialData, uint32 InitialRowPitch);

	VkImage GetImage() const { return Image; }
	VkFormat GetVkFormat() const { return Format; }
	// Descriptor set (set 0) for pipelines using the given sampler filter.
	VkDescriptorSet GetDescriptorSet(ESamplerFilter Filter);
	// Framebuffer for render passes into this texture (render targets only).
	VkFramebuffer GetFramebuffer(VkRenderPass CompatibleRenderPass);

private:
	FVulkanDevice& Device;
	VkImage Image = VK_NULL_HANDLE;
	VkDeviceMemory Memory = VK_NULL_HANDLE;
	VkFormat Format = VK_FORMAT_UNDEFINED;
	VkImageView SampledView = VK_NULL_HANDLE;
	VkImageView AttachmentView = VK_NULL_HANDLE; // identity swizzle; only when it differs from SampledView
	VkFramebuffer Framebuffer = VK_NULL_HANDLE;
	VkDescriptorSet DescriptorSets[2] = {};
	VkDescriptorPool DescriptorPools[2] = {};
};

class FVulkanGraphicsPipeline final : public FRHIGraphicsPipeline
{
public:
	FVulkanGraphicsPipeline(FVulkanDevice& InDevice, const FRHIGraphicsPipelineDesc& InDesc);
	~FVulkanGraphicsPipeline() override;

	bool Initialize();
	VkPipeline GetHandle() const { return Pipeline; }

private:
	FVulkanDevice& Device;
	VkPipeline Pipeline = VK_NULL_HANDLE;
};

class FVulkanSwapChain final : public FRHISwapChain
{
public:
	FVulkanSwapChain(FVulkanDevice& InDevice, IPlatformWindow& InWindow);
	~FVulkanSwapChain() override;

	bool Initialize();

	void Resize(uint32 Width, uint32 Height) override;
	FIntPoint GetSize() const override { return FIntPoint(static_cast<int32>(Width), static_cast<int32>(Height)); }
	EPixelFormat GetFormat() const override { return PixelFormat; }
	bool IsDrawable() const override { return Width > 0 && Height > 0 && !bZeroExtent; }

	// Acquires this frame's image (once per frame); false when the swapchain
	// cannot be drawn into right now.
	bool AcquireNextImage(uint32 FrameIndex);
	VkSemaphore GetAcquireSemaphore() const { return AcquireSemaphores[AcquiredFrameIndex]; }
	VkSemaphore GetRenderFinishedSemaphore() const { return Images[ImageIndex].RenderFinished; }
	VkFramebuffer GetCurrentFramebuffer() const { return Images[ImageIndex].Framebuffer; }
	VkFormat GetVkFormat() const { return SurfaceFormat.format; }
	VkExtent2D GetExtent() const { return Extent; }
	// Whether a pass already drew into this frame's image (later passes load it).
	bool bRenderedThisFrame = false;
	// Presents the acquired image (after the frame was submitted).
	void Present(VkQueue Queue);

private:
	struct FImage
	{
		VkImage Image = VK_NULL_HANDLE;
		VkImageView View = VK_NULL_HANDLE;
		VkFramebuffer Framebuffer = VK_NULL_HANDLE;
		VkSemaphore RenderFinished = VK_NULL_HANDLE;
	};

	bool Recreate();
	void DestroyImages();

	FVulkanDevice& Device;
	VkSurfaceKHR Surface = VK_NULL_HANDLE;
	VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
	VkSurfaceFormatKHR SurfaceFormat = {};
	EPixelFormat PixelFormat = EPixelFormat::Unknown;
	VkExtent2D Extent = {};
	TArray<FImage> Images;
	VkSemaphore AcquireSemaphores[FRHIDevice::MaxFramesInFlight] = {};
	uint32 Width = 0;
	uint32 Height = 0;
	uint32 ImageIndex = 0;
	uint32 AcquiredFrameIndex = 0;
	bool bAcquired = false;
	bool bNeedsRecreate = true;
	bool bZeroExtent = false; // the surface reported a zero extent (minimized)
};

class FVulkanCommandList final : public FRHICommandList
{
public:
	explicit FVulkanCommandList(FVulkanDevice& InDevice) : Device(InDevice) {}

	void Begin(VkCommandBuffer InCommandBuffer);
	void End();

	void BeginRenderPass(FRHISwapChain& Target, const TOptional<FLinearColor>& ClearColor) override;
	void BeginRenderPass(FRHITexture& Target, const TOptional<FLinearColor>& ClearColor) override;
	void EndRenderPass() override;

	void SetViewport(float X, float Y, float Width, float Height) override;
	void SetScissor(const FIntRect& Rect) override;
	void SetPipeline(FRHIGraphicsPipeline& Pipeline) override;
	void SetPushConstants(const void* Data, uint32 Size) override;
	void SetTexture(FRHITexture& Texture) override;
	void SetVertexBuffer(FRHIBuffer& Buffer, uint64 Offset, uint32 Stride) override;
	void SetIndexBuffer(FRHIBuffer& Buffer, uint64 Offset, EIndexFormat Format) override;
	void DrawIndexed(uint32 IndexCount, uint32 FirstIndex, int32 BaseVertex) override;

private:
	void BeginPass(VkRenderPass RenderPass, VkFramebuffer Framebuffer, uint32 Width, uint32 Height, const TOptional<FLinearColor>& ClearColor);
	void ResetState();

	FVulkanDevice& Device;
	VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
	bool bInRenderPass = false;
	bool bSkipPass = false; // the target could not be drawn into; commands are dropped
	FIntPoint PassSize;
	FVulkanGraphicsPipeline* Pipeline = nullptr;
	FVulkanTexture* Texture = nullptr;
	bool bDescriptorDirty = true;
	bool bPushConstantsDirty = false;
	uint8 PushConstantData[64] = {};
};

// Where an upload's commands and staging bytes go (FVulkanDevice::BeginUpload).
struct FVulkanUploadContext
{
	VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
	VkBuffer StagingBuffer = VK_NULL_HANDLE;
	VkDeviceSize StagingOffset = 0;
	uint8* StagingData = nullptr;
	bool bImmediate = false;
	VkDeviceMemory TempMemory = VK_NULL_HANDLE;
};

class FVulkanDevice final : public FRHIDevice
{
public:
	static constexpr uint32 MaxPushConstantSize = 64;

	FVulkanDevice();
	~FVulkanDevice() override;

	bool Initialize(const FRHIDeviceDesc& Desc);

	// FRHIDevice
	FString GetAdapterName() const override { return AdapterName; }
	TUniquePtr<FRHISwapChain> CreateSwapChain(IPlatformWindow& Window) override;
	TSharedPtr<FRHIBuffer> CreateBuffer(const FRHIBufferDesc& Desc) override;
	TSharedPtr<FRHITexture> CreateTexture(const FRHITextureDesc& Desc) override;
	TSharedPtr<FRHIGraphicsPipeline> CreateGraphicsPipeline(const FRHIGraphicsPipelineDesc& Desc) override;
	void UpdateTexture(FRHITexture& Texture, const FIntRect& Region, const void* Data, uint32 RowPitch = 0) override;
	bool ReadbackTexture(FRHITexture& Texture, TArray<uint8>& OutPixels) override;
	FRHICommandList& BeginFrame() override;
	void EndFrame() override;
	uint32 GetFrameIndex() const override { return FrameIndex; }
	void WaitIdle() override;
	void DeferRelease(TSharedPtr<void> Resource) override;

	// Backend internals.
	VkInstance GetInstance() const { return Instance; }
	VkPhysicalDevice GetPhysicalDevice() const { return PhysicalDevice; }
	VkDevice GetHandle() const { return Device; }
	uint32 GetQueueFamily() const { return QueueFamily; }
	bool IsInFrame() const { return bInFrame; }

	// Destroys native objects once the GPU is done with the frame being recorded.
	void DeferDestroy(TFunction<void()> Destroy);
	void SetDebugName(VkObjectType Type, uint64 Handle, const char* Name);

	uint32 FindMemoryType(uint32 TypeBits, VkMemoryPropertyFlags Required, VkMemoryPropertyFlags Preferred) const;
	bool AllocateMemory(const VkMemoryRequirements& Requirements, VkMemoryPropertyFlags Required, VkMemoryPropertyFlags Preferred, VkDeviceMemory& OutMemory);
	bool CreateBufferObject(VkDeviceSize Size, VkBufferUsageFlags Usage, bool bHostVisible, VkBuffer& OutBuffer, VkDeviceMemory& OutMemory, void** OutMapped);

	// Render passes are cached by format, load op and layouts. Every pass has
	// one color attachment; all passes of a format are compatible.
	VkRenderPass GetRenderPass(VkFormat Format, bool bClear, VkImageLayout InitialLayout, VkImageLayout FinalLayout);
	VkPipelineLayout GetPipelineLayout(ESamplerFilter Filter) const { return PipelineLayouts[static_cast<uint32>(Filter)]; }
	VkDescriptorSet AllocateDescriptorSet(ESamplerFilter Filter, VkDescriptorPool& OutPool);
	FVulkanTexture& GetWhiteTexture() const { return *WhiteTexture; }

	// Starts an upload; see the header comment. StagingSize may be 0.
	bool BeginUpload(VkDeviceSize StagingSize, FVulkanUploadContext& OutContext);
	void EndUpload(FVulkanUploadContext& Context);

	void OnSwapChainAcquired(FVulkanSwapChain* SwapChain);
	void OnSwapChainDestroyed(FVulkanSwapChain* SwapChain);

private:
	struct FFrame
	{
		VkCommandPool CommandPool = VK_NULL_HANDLE;
		VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
		VkCommandBuffer UploadCommandBuffer = VK_NULL_HANDLE;
		VkFence Fence = VK_NULL_HANDLE;
		uint64 SubmittedSerial = 0;
		bool bUploadRecording = false;
		VkBuffer StagingBuffer = VK_NULL_HANDLE;
		VkDeviceMemory StagingMemory = VK_NULL_HANDLE;
		uint8* StagingData = nullptr;
		VkDeviceSize StagingSize = 0;
		VkDeviceSize StagingOffset = 0;
	};

	struct FGarbage
	{
		uint64 Serial = 0;
		TFunction<void()> Destroy;
	};

	bool CreateInstance(bool bDebugLayer);
	bool PickPhysicalDevice();
	bool CreateLogicalDevice();
	bool CreateSharedObjects();
	void CollectGarbage();
	void SubmitImmediate(VkCommandBuffer CommandBuffer);
	// Submits a pending in-frame upload command buffer now (before readbacks).
	void FlushPendingUploads();

	VkInstance Instance = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT DebugMessenger = VK_NULL_HANDLE;
	bool bDebugUtils = false;
	VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
	VkPhysicalDeviceProperties PhysicalDeviceProperties = {};
	VkPhysicalDeviceMemoryProperties MemoryProperties = {};
	VkDevice Device = VK_NULL_HANDLE;
	uint32 QueueFamily = 0;
	VkQueue Queue = VK_NULL_HANDLE;
	FString AdapterName;
	bool bLibraryLoaded = false;

	VkSampler Samplers[2] = {};
	VkDescriptorSetLayout SetLayouts[2] = {};
	VkPipelineLayout PipelineLayouts[2] = {};
	TArray<VkDescriptorPool> DescriptorPools;
	TMap<uint64, VkRenderPass> RenderPasses;

	FFrame Frames[MaxFramesInFlight];
	uint32 FrameIndex = 0;
	bool bInFrame = false;
	uint64 CurrentSerial = 1;   // serial of the frame being recorded / next submitted
	uint64 CompletedSerial = 0; // every frame up to this one has finished on the GPU
	std::deque<FGarbage> Garbage;

	VkCommandPool ImmediatePool = VK_NULL_HANDLE;
	VkCommandBuffer ImmediateCommandBuffer = VK_NULL_HANDLE;
	VkFence ImmediateFence = VK_NULL_HANDLE;

	TUniquePtr<FVulkanCommandList> CommandList;
	TArray<FVulkanSwapChain*> AcquiredSwapChains;
	TSharedPtr<FVulkanTexture> WhiteTexture;
};

namespace VulkanUtil
{
	VkFormat ToVkFormat(EPixelFormat Format);
	// Logs a failed call; returns whether Result is VK_SUCCESS.
	bool Check(VkResult Result, const char* What);
}
