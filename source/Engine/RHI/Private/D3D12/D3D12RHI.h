// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// D3D12 backend of the RHI.

#pragma once

#include "RHIPrivate.h"

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <deque>

using Microsoft::WRL::ComPtr;

class FD3D12Device;

// Fixed-size descriptor heap with a free list.
class FD3D12DescriptorHeap
{
public:
	bool Initialize(ID3D12Device* Device, D3D12_DESCRIPTOR_HEAP_TYPE Type, uint32 Capacity, bool bShaderVisible);
	uint32 Allocate();
	void Free(uint32 Index);
	D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(uint32 Index) const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(uint32 Index) const;
	ID3D12DescriptorHeap* GetHeap() const { return Heap.Get(); }

	static constexpr uint32 Invalid = ~0u;

private:
	ComPtr<ID3D12DescriptorHeap> Heap;
	TArray<uint32> FreeList;
	uint32 Increment = 0;
	uint32 Capacity = 0;
	D3D12_CPU_DESCRIPTOR_HANDLE CpuStart{};
	D3D12_GPU_DESCRIPTOR_HANDLE GpuStart{};
};

class FD3D12Buffer final : public FRHIBuffer
{
public:
	FD3D12Buffer(FD3D12Device& InDevice, const FRHIBufferDesc& InDesc, ComPtr<ID3D12Resource> InResource, void* InMapped);
	~FD3D12Buffer() override;
	void* GetMappedData() override { return Mapped; }
	ID3D12Resource* GetResource() const { return Resource.Get(); }

private:
	FD3D12Device& Device;
	ComPtr<ID3D12Resource> Resource;
	void* Mapped;
};

class FD3D12Texture final : public FRHITexture
{
public:
	FD3D12Texture(FD3D12Device& InDevice, const FRHITextureDesc& InDesc, ComPtr<ID3D12Resource> InResource, uint32 InSrv, uint32 InRtv);
	~FD3D12Texture() override;

	ID3D12Resource* GetResource() const { return Resource.Get(); }
	uint32 GetSrvIndex() const { return Srv; }
	uint32 GetRtvIndex() const { return Rtv; }

private:
	FD3D12Device& Device;
	ComPtr<ID3D12Resource> Resource;
	uint32 Srv;
	uint32 Rtv;
};

class FD3D12SwapChain final : public FRHISwapChain
{
public:
	static constexpr uint32 BufferCount = 3;

	FD3D12SwapChain(FD3D12Device& InDevice, IPlatformWindow& InWindow);
	~FD3D12SwapChain() override;
	bool Initialize();

	void Resize(uint32 Width, uint32 Height) override;
	FIntPoint GetSize() const override { return Size; }
	EPixelFormat GetFormat() const override { return EPixelFormat::R8G8B8A8_UNORM; }
	bool IsDrawable() const override { return Size.X > 0 && Size.Y > 0 && SwapChain; }

	ID3D12Resource* GetCurrentBackBuffer() const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRtv() const;
	void Present();

private:
	void AcquireBuffers();
	void ReleaseBuffers();

	FD3D12Device& Device;
	ComPtr<IDXGISwapChain3> SwapChain;
	ComPtr<ID3D12Resource> BackBuffers[BufferCount];
	uint32 Rtvs[BufferCount];
	FIntPoint Size;
};

class FD3D12Pipeline final : public FRHIGraphicsPipeline
{
public:
	FD3D12Pipeline(const FRHIGraphicsPipelineDesc& InDesc, ComPtr<ID3D12PipelineState> InPso, ComPtr<ID3D12RootSignature> InRootSignature)
		: FRHIGraphicsPipeline(InDesc), Pso(std::move(InPso)), RootSignature(std::move(InRootSignature)) {}

	ID3D12PipelineState* GetPso() const { return Pso.Get(); }
	ID3D12RootSignature* GetRootSignature() const { return RootSignature.Get(); }

private:
	ComPtr<ID3D12PipelineState> Pso;
	ComPtr<ID3D12RootSignature> RootSignature;
};

class FD3D12CommandList final : public FRHICommandList
{
public:
	explicit FD3D12CommandList(FD3D12Device& InDevice) : Device(InDevice) {}

	void Begin(ID3D12GraphicsCommandList* InList);

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
	void BeginPass(D3D12_CPU_DESCRIPTOR_HANDLE Rtv, const TOptional<FLinearColor>& ClearColor, FIntPoint Size);

	FD3D12Device& Device;
	ID3D12GraphicsCommandList* List = nullptr;
	ID3D12Resource* PassResource = nullptr;
	D3D12_RESOURCE_STATES PassRestoreState = D3D12_RESOURCE_STATE_COMMON;
	ID3D12RootSignature* CurrentRootSignature = nullptr;
	FD3D12Pipeline* CurrentPipeline = nullptr;
};

class FD3D12Device final : public FRHIDevice
{
public:
	explicit FD3D12Device(const FRHIDeviceDesc& InDesc);
	~FD3D12Device() override;
	bool Initialize();

	FString GetAdapterName() const override { return AdapterName; }
	TUniquePtr<FRHISwapChain> CreateSwapChain(IPlatformWindow& Window) override;
	TSharedPtr<FRHIBuffer> CreateBuffer(const FRHIBufferDesc& Desc) override;
	TSharedPtr<FRHITexture> CreateTexture(const FRHITextureDesc& Desc) override;
	TSharedPtr<FRHIGraphicsPipeline> CreateGraphicsPipeline(const FRHIGraphicsPipelineDesc& Desc) override;
	void UpdateTexture(FRHITexture& Texture, const FIntRect& Region, const void* Data, uint32 RowPitch) override;
	bool ReadbackTexture(FRHITexture& Texture, TArray<uint8>& OutPixels) override;
	FRHICommandList& BeginFrame() override;
	void EndFrame() override;
	uint32 GetFrameIndex() const override { return FrameIndex; }
	void WaitIdle() override;
	void DeferRelease(TSharedPtr<void> Resource) override;

	// Internal.
	ID3D12Device* GetDevice() const { return Device.Get(); }
	IDXGIFactory4* GetFactory() const { return Factory.Get(); }
	ID3D12CommandQueue* GetQueue() const { return Queue.Get(); }
	FD3D12DescriptorHeap& GetSrvHeap() { return SrvHeap; }
	FD3D12DescriptorHeap& GetRtvHeap() { return RtvHeap; }
	// Keeps a native object (and optionally descriptor slots) alive until the
	// GPU has finished the work submitted so far.
	void DeferDelete(ComPtr<IUnknown> Object, uint32 SrvSlot = FD3D12DescriptorHeap::Invalid, uint32 RtvSlot = FD3D12DescriptorHeap::Invalid);
	void MarkPresent(FD3D12SwapChain* SwapChain);
	void FlushDebugMessages();

private:
	struct FDeferredDelete
	{
		uint64 FenceValue = 0;
		ComPtr<IUnknown> Object;
		TSharedPtr<void> Shared;
		uint32 SrvSlot = FD3D12DescriptorHeap::Invalid;
		uint32 RtvSlot = FD3D12DescriptorHeap::Invalid;
	};

	struct FUploadAllocation
	{
		ID3D12Resource* Resource = nullptr;
		uint64 Offset = 0;
		uint8* Cpu = nullptr;
	};

	// Per-frame linear upload memory for texture updates.
	struct FUploadRing
	{
		TArray<ComPtr<ID3D12Resource>> Chunks;
		TArray<uint8*> ChunkCpu;
		uint64 ChunkSize = 0;
		uint32 CurrentChunk = 0;
		uint64 CurrentOffset = 0;
	};

	FUploadAllocation AllocateUpload(FUploadRing& Ring, uint64 Size, uint64 Alignment);
	void RecordTextureCopy(ID3D12GraphicsCommandList* List, FD3D12Texture& Texture, const FIntRect& Region, const void* Data, uint32 RowPitch, FUploadRing& Ring);
	template <typename F> void ExecuteImmediate(F&& Record);
	void WaitForFence(uint64 Value);
	void CollectGarbage();
	ComPtr<ID3D12RootSignature> GetRootSignature(uint32 PushConstantSize, ESamplerFilter Filter);

	FRHIDeviceDesc Desc;
	FString AdapterName;
	ComPtr<IDXGIFactory6> Factory;
	ComPtr<ID3D12Device> Device;
	ComPtr<ID3D12CommandQueue> Queue;
	ComPtr<ID3D12Fence> Fence;
	HANDLE FenceEvent = nullptr;
	uint64 LastSignaledValue = 0;
	ComPtr<ID3D12InfoQueue> InfoQueue;

	FD3D12DescriptorHeap SrvHeap;
	FD3D12DescriptorHeap RtvHeap;

	struct FFrame
	{
		ComPtr<ID3D12CommandAllocator> Allocator;
		ComPtr<ID3D12CommandAllocator> UploadAllocator;
		uint64 FenceValue = 0;
		FUploadRing Upload;
		bool bUploadOpen = false;
	};
	FFrame Frames[MaxFramesInFlight];
	ComPtr<ID3D12GraphicsCommandList> MainList;
	ComPtr<ID3D12GraphicsCommandList> UploadList;
	ComPtr<ID3D12CommandAllocator> ImmediateAllocator;
	ComPtr<ID3D12GraphicsCommandList> ImmediateList;
	FUploadRing ImmediateUpload;
	FD3D12CommandList CommandList;
	uint32 FrameIndex = 0;
	bool bInFrame = false;

	TArray<FD3D12SwapChain*> PresentList;
	std::deque<FDeferredDelete> Garbage;
	TMap<uint32, ComPtr<ID3D12RootSignature>> RootSignatures;
};
