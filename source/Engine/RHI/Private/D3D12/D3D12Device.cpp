// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "D3D12/D3D12RHI.h"

#include "GenericPlatform/PlatformWindow.h"

#include <d3d12sdklayers.h>
#include <dxgidebug.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

namespace
{
	DXGI_FORMAT ToDxgi(EPixelFormat Format)
	{
		switch (Format)
		{
		case EPixelFormat::R8_UNORM: return DXGI_FORMAT_R8_UNORM;
		case EPixelFormat::R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
		case EPixelFormat::B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
		case EPixelFormat::Unknown: break;
		}
		return DXGI_FORMAT_UNKNOWN;
	}

	DXGI_FORMAT ToDxgi(EVertexFormat Format)
	{
		switch (Format)
		{
		case EVertexFormat::Float2: return DXGI_FORMAT_R32G32_FLOAT;
		case EVertexFormat::Float4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
		case EVertexFormat::UByte4N: return DXGI_FORMAT_R8G8B8A8_UNORM;
		}
		return DXGI_FORMAT_UNKNOWN;
	}

	D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* Resource, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After)
	{
		D3D12_RESOURCE_BARRIER Barrier{};
		Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		Barrier.Transition.pResource = Resource;
		Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		Barrier.Transition.StateBefore = Before;
		Barrier.Transition.StateAfter = After;
		return Barrier;
	}

	void SetName(ID3D12Object* Object, const char* Name)
	{
		if (Object && Name)
		{
			Object->SetName(FString(Name).ToWide().c_str());
		}
	}

	constexpr D3D12_RESOURCE_STATES TextureIdleState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	constexpr uint32 SrvHeapCapacity = 4096;
	constexpr uint32 RtvHeapCapacity = 256;
	constexpr uint64 UploadChunkSize = 4 * 1024 * 1024;
}

TUniquePtr<FRHIDevice> CreateD3D12Device(const FRHIDeviceDesc& Desc)
{
	auto Device = MakeUnique<FD3D12Device>(Desc);
	if (!Device->Initialize())
	{
		return nullptr;
	}
	return Device;
}

// ---------------------------------------------------------------------------
// Descriptor heap

bool FD3D12DescriptorHeap::Initialize(ID3D12Device* Device, D3D12_DESCRIPTOR_HEAP_TYPE Type, uint32 InCapacity, bool bShaderVisible)
{
	D3D12_DESCRIPTOR_HEAP_DESC HeapDesc{};
	HeapDesc.Type = Type;
	HeapDesc.NumDescriptors = InCapacity;
	HeapDesc.Flags = bShaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	if (FAILED(Device->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&Heap))))
	{
		return false;
	}
	Capacity = InCapacity;
	Increment = Device->GetDescriptorHandleIncrementSize(Type);
	CpuStart = Heap->GetCPUDescriptorHandleForHeapStart();
	if (bShaderVisible)
	{
		GpuStart = Heap->GetGPUDescriptorHandleForHeapStart();
	}
	FreeList.reserve(Capacity);
	for (uint32 I = Capacity; I-- > 0;)
	{
		FreeList.push_back(I);
	}
	return true;
}

uint32 FD3D12DescriptorHeap::Allocate()
{
	if (FreeList.empty())
	{
		GE_LOG(LogRHI, Error, "D3D12 descriptor heap exhausted ({} descriptors)", Capacity);
		return Invalid;
	}
	const uint32 Index = FreeList.back();
	FreeList.pop_back();
	return Index;
}

void FD3D12DescriptorHeap::Free(uint32 Index)
{
	if (Index != Invalid)
	{
		FreeList.push_back(Index);
	}
}

D3D12_CPU_DESCRIPTOR_HANDLE FD3D12DescriptorHeap::GetCpuHandle(uint32 Index) const
{
	return D3D12_CPU_DESCRIPTOR_HANDLE{ CpuStart.ptr + SIZE_T(Index) * Increment };
}

D3D12_GPU_DESCRIPTOR_HANDLE FD3D12DescriptorHeap::GetGpuHandle(uint32 Index) const
{
	return D3D12_GPU_DESCRIPTOR_HANDLE{ GpuStart.ptr + uint64(Index) * Increment };
}

// ---------------------------------------------------------------------------
// Resources

FD3D12Buffer::FD3D12Buffer(FD3D12Device& InDevice, const FRHIBufferDesc& InDesc, ComPtr<ID3D12Resource> InResource, void* InMapped)
	: FRHIBuffer(InDesc)
	, Device(InDevice)
	, Resource(std::move(InResource))
	, Mapped(InMapped)
{
	Desc.InitialData = nullptr;
}

FD3D12Buffer::~FD3D12Buffer()
{
	Device.DeferDelete(std::move(Resource));
}

FD3D12Texture::FD3D12Texture(FD3D12Device& InDevice, const FRHITextureDesc& InDesc, ComPtr<ID3D12Resource> InResource, uint32 InSrv, uint32 InRtv)
	: FRHITexture(InDesc)
	, Device(InDevice)
	, Resource(std::move(InResource))
	, Srv(InSrv)
	, Rtv(InRtv)
{
}

FD3D12Texture::~FD3D12Texture()
{
	Device.DeferDelete(std::move(Resource), Srv, Rtv);
}

// ---------------------------------------------------------------------------
// Swapchain

FD3D12SwapChain::FD3D12SwapChain(FD3D12Device& InDevice, IPlatformWindow& InWindow)
	: FRHISwapChain(&InWindow)
	, Device(InDevice)
{
	for (uint32& Rtv : Rtvs)
	{
		Rtv = FD3D12DescriptorHeap::Invalid;
	}
}

FD3D12SwapChain::~FD3D12SwapChain()
{
	Device.WaitIdle();
	ReleaseBuffers();
	SwapChain.Reset();
}

bool FD3D12SwapChain::Initialize()
{
	const HWND Hwnd = static_cast<HWND>(Window->GetNativeHandle());
	const FIntPoint ClientSize = Window->GetClientSize();

	DXGI_SWAP_CHAIN_DESC1 SwapDesc{};
	SwapDesc.Width = static_cast<UINT>(FMath::Max(1, ClientSize.X));
	SwapDesc.Height = static_cast<UINT>(FMath::Max(1, ClientSize.Y));
	SwapDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	SwapDesc.SampleDesc.Count = 1;
	SwapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	SwapDesc.BufferCount = BufferCount;
	SwapDesc.Scaling = DXGI_SCALING_NONE;
	SwapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	SwapDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

	ComPtr<IDXGISwapChain1> SwapChain1;
	HRESULT Hr = Device.GetFactory()->CreateSwapChainForHwnd(Device.GetQueue(), Hwnd, &SwapDesc, nullptr, nullptr, &SwapChain1);
	if (FAILED(Hr) || FAILED(SwapChain1.As(&SwapChain)))
	{
		GE_LOG(LogRHI, Error, "CreateSwapChainForHwnd failed (0x{:08X})", static_cast<uint32>(Hr));
		return false;
	}
	Device.GetFactory()->MakeWindowAssociation(Hwnd, DXGI_MWA_NO_ALT_ENTER);
	Size = FIntPoint(ClientSize.X, ClientSize.Y);
	AcquireBuffers();
	return true;
}

void FD3D12SwapChain::AcquireBuffers()
{
	for (uint32 I = 0; I < BufferCount; ++I)
	{
		SwapChain->GetBuffer(I, IID_PPV_ARGS(&BackBuffers[I]));
		SetName(BackBuffers[I].Get(), "SwapChain back buffer");
		if (Rtvs[I] == FD3D12DescriptorHeap::Invalid)
		{
			Rtvs[I] = Device.GetRtvHeap().Allocate();
		}
		Device.GetDevice()->CreateRenderTargetView(BackBuffers[I].Get(), nullptr, Device.GetRtvHeap().GetCpuHandle(Rtvs[I]));
	}
}

void FD3D12SwapChain::ReleaseBuffers()
{
	for (uint32 I = 0; I < BufferCount; ++I)
	{
		BackBuffers[I].Reset();
		Device.GetRtvHeap().Free(Rtvs[I]);
		Rtvs[I] = FD3D12DescriptorHeap::Invalid;
	}
}

void FD3D12SwapChain::Resize(uint32 Width, uint32 Height)
{
	if (Width == 0 || Height == 0)
	{
		Size = FIntPoint(0, 0); // minimized: keep the buffers, skip drawing
		return;
	}
	DXGI_SWAP_CHAIN_DESC1 Current{};
	SwapChain->GetDesc1(&Current);
	Size = FIntPoint(static_cast<int32>(Width), static_cast<int32>(Height));
	if (Current.Width == Width && Current.Height == Height)
	{
		return;
	}

	// The back buffers may still be in use by frames in flight.
	Device.WaitIdle();
	for (ComPtr<ID3D12Resource>& Buffer : BackBuffers)
	{
		Buffer.Reset();
	}
	const HRESULT Hr = SwapChain->ResizeBuffers(BufferCount, Width, Height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
	if (FAILED(Hr))
	{
		GE_LOG(LogRHI, Error, "ResizeBuffers({}x{}) failed (0x{:08X})", Width, Height, static_cast<uint32>(Hr));
	}
	AcquireBuffers();
}

ID3D12Resource* FD3D12SwapChain::GetCurrentBackBuffer() const
{
	return BackBuffers[SwapChain->GetCurrentBackBufferIndex()].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE FD3D12SwapChain::GetCurrentRtv() const
{
	return Device.GetRtvHeap().GetCpuHandle(Rtvs[SwapChain->GetCurrentBackBufferIndex()]);
}

void FD3D12SwapChain::Present()
{
	const HRESULT Hr = SwapChain->Present(1, 0);
	if (FAILED(Hr))
	{
		GE_LOG(LogRHI, Error, "Present failed (0x{:08X})", static_cast<uint32>(Hr));
	}
}

// ---------------------------------------------------------------------------
// Command list

void FD3D12CommandList::Begin(ID3D12GraphicsCommandList* InList)
{
	List = InList;
	PassResource = nullptr;
	CurrentRootSignature = nullptr;
	CurrentPipeline = nullptr;
	ID3D12DescriptorHeap* Heaps[] = { Device.GetSrvHeap().GetHeap() };
	List->SetDescriptorHeaps(1, Heaps);
	List->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void FD3D12CommandList::BeginPass(D3D12_CPU_DESCRIPTOR_HANDLE Rtv, const TOptional<FLinearColor>& ClearColor, FIntPoint Size)
{
	List->OMSetRenderTargets(1, &Rtv, FALSE, nullptr);
	if (ClearColor)
	{
		const float Color[4] = { ClearColor->R, ClearColor->G, ClearColor->B, ClearColor->A };
		List->ClearRenderTargetView(Rtv, Color, 0, nullptr);
	}
	SetViewport(0.0f, 0.0f, static_cast<float>(Size.X), static_cast<float>(Size.Y));
	SetScissor(FIntRect(0, 0, Size.X, Size.Y));
}

void FD3D12CommandList::BeginRenderPass(FRHISwapChain& Target, const TOptional<FLinearColor>& ClearColor)
{
	check(!PassResource);
	auto& SwapChain = static_cast<FD3D12SwapChain&>(Target);
	check(SwapChain.IsDrawable());
	PassResource = SwapChain.GetCurrentBackBuffer();
	PassRestoreState = D3D12_RESOURCE_STATE_PRESENT;
	const D3D12_RESOURCE_BARRIER Barrier = Transition(PassResource, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	List->ResourceBarrier(1, &Barrier);
	BeginPass(SwapChain.GetCurrentRtv(), ClearColor, SwapChain.GetSize());
	Device.MarkPresent(&SwapChain);
}

void FD3D12CommandList::BeginRenderPass(FRHITexture& Target, const TOptional<FLinearColor>& ClearColor)
{
	check(!PassResource);
	auto& Texture = static_cast<FD3D12Texture&>(Target);
	check(Texture.GetDesc().bRenderTarget);
	PassResource = Texture.GetResource();
	PassRestoreState = TextureIdleState;
	const D3D12_RESOURCE_BARRIER Barrier = Transition(PassResource, TextureIdleState, D3D12_RESOURCE_STATE_RENDER_TARGET);
	List->ResourceBarrier(1, &Barrier);
	BeginPass(Device.GetRtvHeap().GetCpuHandle(Texture.GetRtvIndex()), ClearColor,
		FIntPoint(static_cast<int32>(Texture.GetWidth()), static_cast<int32>(Texture.GetHeight())));
}

void FD3D12CommandList::EndRenderPass()
{
	check(PassResource);
	const D3D12_RESOURCE_BARRIER Barrier = Transition(PassResource, D3D12_RESOURCE_STATE_RENDER_TARGET, PassRestoreState);
	List->ResourceBarrier(1, &Barrier);
	PassResource = nullptr;
}

void FD3D12CommandList::SetViewport(float X, float Y, float Width, float Height)
{
	const D3D12_VIEWPORT Viewport{ X, Y, Width, Height, 0.0f, 1.0f };
	List->RSSetViewports(1, &Viewport);
}

void FD3D12CommandList::SetScissor(const FIntRect& Rect)
{
	const D3D12_RECT Scissor{ Rect.Left, Rect.Top, FMath::Max(Rect.Left, Rect.Right), FMath::Max(Rect.Top, Rect.Bottom) };
	List->RSSetScissorRects(1, &Scissor);
}

void FD3D12CommandList::SetPipeline(FRHIGraphicsPipeline& Pipeline)
{
	auto& D3DPipeline = static_cast<FD3D12Pipeline&>(Pipeline);
	if (&D3DPipeline == CurrentPipeline)
	{
		return;
	}
	CurrentPipeline = &D3DPipeline;
	if (D3DPipeline.GetRootSignature() != CurrentRootSignature)
	{
		CurrentRootSignature = D3DPipeline.GetRootSignature();
		List->SetGraphicsRootSignature(CurrentRootSignature);
	}
	List->SetPipelineState(D3DPipeline.GetPso());
}

void FD3D12CommandList::SetPushConstants(const void* Data, uint32 Size)
{
	check(CurrentPipeline && Size % 4 == 0 && Size <= CurrentPipeline->GetDesc().PushConstantSize);
	List->SetGraphicsRoot32BitConstants(0, Size / 4, Data, 0);
}

void FD3D12CommandList::SetTexture(FRHITexture& Texture)
{
	check(CurrentPipeline);
	const uint32 TableIndex = CurrentPipeline->GetDesc().PushConstantSize > 0 ? 1 : 0;
	List->SetGraphicsRootDescriptorTable(TableIndex, Device.GetSrvHeap().GetGpuHandle(static_cast<FD3D12Texture&>(Texture).GetSrvIndex()));
}

void FD3D12CommandList::SetVertexBuffer(FRHIBuffer& Buffer, uint64 Offset, uint32 Stride)
{
	ID3D12Resource* Resource = static_cast<FD3D12Buffer&>(Buffer).GetResource();
	const D3D12_VERTEX_BUFFER_VIEW View{ Resource->GetGPUVirtualAddress() + Offset, static_cast<UINT>(Buffer.GetSize() - Offset), Stride };
	List->IASetVertexBuffers(0, 1, &View);
}

void FD3D12CommandList::SetIndexBuffer(FRHIBuffer& Buffer, uint64 Offset, EIndexFormat Format)
{
	ID3D12Resource* Resource = static_cast<FD3D12Buffer&>(Buffer).GetResource();
	const D3D12_INDEX_BUFFER_VIEW View{ Resource->GetGPUVirtualAddress() + Offset, static_cast<UINT>(Buffer.GetSize() - Offset),
		Format == EIndexFormat::UInt16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT };
	List->IASetIndexBuffer(&View);
}

void FD3D12CommandList::DrawIndexed(uint32 IndexCount, uint32 FirstIndex, int32 BaseVertex)
{
	List->DrawIndexedInstanced(IndexCount, 1, FirstIndex, BaseVertex, 0);
}

// ---------------------------------------------------------------------------
// Device

FD3D12Device::FD3D12Device(const FRHIDeviceDesc& InDesc)
	: FRHIDevice(ERHIBackend::D3D12)
	, Desc(InDesc)
	, CommandList(*this)
{
}

FD3D12Device::~FD3D12Device()
{
	if (Queue)
	{
		WaitIdle();
	}
	Garbage.clear();
	CollectGarbage();
	FlushDebugMessages();
	if (FenceEvent)
	{
		CloseHandle(FenceEvent);
	}
}

bool FD3D12Device::Initialize()
{
	UINT FactoryFlags = 0;
	if (Desc.bDebugLayer)
	{
		ComPtr<ID3D12Debug> Debug;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&Debug))))
		{
			Debug->EnableDebugLayer();
			FactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
			GE_LOG(LogRHI, Display, "D3D12 debug layer enabled");
		}
		else
		{
			GE_LOG(LogRHI, Display, "D3D12 debug layer requested but not installed (Graphics Tools optional feature)");
		}
	}

	if (FAILED(CreateDXGIFactory2(FactoryFlags, IID_PPV_ARGS(&Factory))))
	{
		GE_LOG(LogRHI, Error, "CreateDXGIFactory2 failed");
		return false;
	}

	ComPtr<IDXGIAdapter1> Adapter;
	if (!Desc.bSoftwareAdapter)
	{
		for (UINT I = 0; Factory->EnumAdapterByGpuPreference(I, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&Adapter)) != DXGI_ERROR_NOT_FOUND; ++I)
		{
			DXGI_ADAPTER_DESC1 AdapterDesc;
			Adapter->GetDesc1(&AdapterDesc);
			if (!(AdapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
				&& SUCCEEDED(D3D12CreateDevice(Adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Device))))
			{
				break;
			}
			Adapter.Reset();
		}
	}
	if (!Device)
	{
		if (!Desc.bSoftwareAdapter)
		{
			GE_LOG(LogRHI, Warning, "No hardware D3D12 adapter; using WARP");
		}
		if (FAILED(Factory->EnumWarpAdapter(IID_PPV_ARGS(&Adapter)))
			|| FAILED(D3D12CreateDevice(Adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Device))))
		{
			GE_LOG(LogRHI, Error, "D3D12CreateDevice failed");
			return false;
		}
	}
	DXGI_ADAPTER_DESC1 AdapterDesc;
	Adapter->GetDesc1(&AdapterDesc);
	AdapterName = FString::FromWide(AdapterDesc.Description);

	if (Desc.bDebugLayer && SUCCEEDED(Device.As(&InfoQueue)))
	{
		InfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
		// Not interesting for a UI renderer that clears with arbitrary colors.
		D3D12_MESSAGE_ID Deny[] = { D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE };
		D3D12_INFO_QUEUE_FILTER Filter{};
		Filter.DenyList.NumIDs = static_cast<UINT>(std::size(Deny));
		Filter.DenyList.pIDList = Deny;
		InfoQueue->AddStorageFilterEntries(&Filter);
	}

	D3D12_COMMAND_QUEUE_DESC QueueDesc{};
	QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	if (FAILED(Device->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&Queue)))
		|| FAILED(Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence))))
	{
		GE_LOG(LogRHI, Error, "Failed to create the D3D12 queue");
		return false;
	}
	SetName(Queue.Get(), "RHI direct queue");
	FenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

	if (!SrvHeap.Initialize(Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, SrvHeapCapacity, true)
		|| !RtvHeap.Initialize(Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, RtvHeapCapacity, false))
	{
		GE_LOG(LogRHI, Error, "Failed to create D3D12 descriptor heaps");
		return false;
	}

	for (FFrame& Frame : Frames)
	{
		Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&Frame.Allocator));
		Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&Frame.UploadAllocator));
	}
	Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&ImmediateAllocator));
	if (FAILED(Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, Frames[0].Allocator.Get(), nullptr, IID_PPV_ARGS(&MainList)))
		|| FAILED(Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, Frames[0].UploadAllocator.Get(), nullptr, IID_PPV_ARGS(&UploadList)))
		|| FAILED(Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ImmediateAllocator.Get(), nullptr, IID_PPV_ARGS(&ImmediateList))))
	{
		GE_LOG(LogRHI, Error, "Failed to create D3D12 command lists");
		return false;
	}
	MainList->Close();
	UploadList->Close();
	ImmediateList->Close();
	SetName(MainList.Get(), "RHI frame command list");
	SetName(UploadList.Get(), "RHI upload command list");
	return true;
}

void FD3D12Device::FlushDebugMessages()
{
	if (!InfoQueue)
	{
		return;
	}
	const UINT64 Count = InfoQueue->GetNumStoredMessages();
	for (UINT64 I = 0; I < Count; ++I)
	{
		SIZE_T Length = 0;
		InfoQueue->GetMessage(I, nullptr, &Length);
		TArray<uint8> Storage(Length);
		auto* Message = reinterpret_cast<D3D12_MESSAGE*>(Storage.data());
		if (SUCCEEDED(InfoQueue->GetMessage(I, Message, &Length)))
		{
			const std::string_view Text(Message->pDescription, Message->DescriptionByteLength > 0 ? Message->DescriptionByteLength - 1 : 0);
			switch (Message->Severity)
			{
			case D3D12_MESSAGE_SEVERITY_CORRUPTION:
			case D3D12_MESSAGE_SEVERITY_ERROR: GE_LOG(LogRHI, Error, "D3D12: {}", Text); break;
			case D3D12_MESSAGE_SEVERITY_WARNING: GE_LOG(LogRHI, Warning, "D3D12: {}", Text); break;
			default: GE_LOG(LogRHI, Verbose, "D3D12: {}", Text); break;
			}
		}
	}
	InfoQueue->ClearStoredMessages();
}

void FD3D12Device::WaitForFence(uint64 Value)
{
	if (Fence->GetCompletedValue() < Value)
	{
		Fence->SetEventOnCompletion(Value, FenceEvent);
		WaitForSingleObject(FenceEvent, INFINITE);
	}
}

void FD3D12Device::WaitIdle()
{
	Queue->Signal(Fence.Get(), ++LastSignaledValue);
	WaitForFence(LastSignaledValue);
	CollectGarbage();
}

void FD3D12Device::DeferDelete(ComPtr<IUnknown> Object, uint32 SrvSlot, uint32 RtvSlot)
{
	FDeferredDelete Entry;
	// Everything submitted so far must finish; work recorded in the open
	// frame is covered by the value the frame will signal.
	Entry.FenceValue = LastSignaledValue + 1;
	Entry.Object = std::move(Object);
	Entry.SrvSlot = SrvSlot;
	Entry.RtvSlot = RtvSlot;
	Garbage.push_back(std::move(Entry));
}

void FD3D12Device::DeferRelease(TSharedPtr<void> Resource)
{
	FDeferredDelete Entry;
	Entry.FenceValue = LastSignaledValue + 1;
	Entry.Shared = std::move(Resource);
	Garbage.push_back(std::move(Entry));
}

void FD3D12Device::CollectGarbage()
{
	const uint64 Completed = Fence ? Fence->GetCompletedValue() : ~0ull;
	// Entries can be added while shared resources are destroyed below, so
	// pop one at a time.
	while (!Garbage.empty() && Garbage.front().FenceValue <= Completed)
	{
		FDeferredDelete Entry = std::move(Garbage.front());
		Garbage.pop_front();
		SrvHeap.Free(Entry.SrvSlot);
		RtvHeap.Free(Entry.RtvSlot);
	}
}

TUniquePtr<FRHISwapChain> FD3D12Device::CreateSwapChain(IPlatformWindow& Window)
{
	auto SwapChain = MakeUnique<FD3D12SwapChain>(*this, Window);
	if (!SwapChain->Initialize())
	{
		return nullptr;
	}
	return SwapChain;
}

TSharedPtr<FRHIBuffer> FD3D12Device::CreateBuffer(const FRHIBufferDesc& BufferDesc)
{
	check(BufferDesc.Size > 0);
	const bool bUpload = BufferDesc.Memory == EBufferMemory::Upload;

	D3D12_HEAP_PROPERTIES Heap{};
	Heap.Type = bUpload ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC ResourceDesc{};
	ResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	ResourceDesc.Width = BufferDesc.Size;
	ResourceDesc.Height = 1;
	ResourceDesc.DepthOrArraySize = 1;
	ResourceDesc.MipLevels = 1;
	ResourceDesc.SampleDesc.Count = 1;
	ResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	ComPtr<ID3D12Resource> Resource;
	const D3D12_RESOURCE_STATES InitialState = bUpload ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COMMON;
	if (FAILED(Device->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &ResourceDesc, InitialState, nullptr, IID_PPV_ARGS(&Resource))))
	{
		GE_LOG(LogRHI, Error, "Failed to create a {}-byte buffer", BufferDesc.Size);
		return nullptr;
	}
	SetName(Resource.Get(), BufferDesc.DebugName);

	void* Mapped = nullptr;
	if (bUpload)
	{
		const D3D12_RANGE NoRead{ 0, 0 };
		Resource->Map(0, &NoRead, &Mapped);
		if (BufferDesc.InitialData)
		{
			memcpy(Mapped, BufferDesc.InitialData, static_cast<SIZE_T>(BufferDesc.Size));
		}
	}
	else
	{
		check(BufferDesc.InitialData);
		ID3D12Resource* Target = Resource.Get();
		ExecuteImmediate([&](ID3D12GraphicsCommandList* List)
		{
			const FUploadAllocation Upload = AllocateUpload(ImmediateUpload, BufferDesc.Size, 16);
			memcpy(Upload.Cpu, BufferDesc.InitialData, static_cast<SIZE_T>(BufferDesc.Size));
			D3D12_RESOURCE_BARRIER Barrier = Transition(Target, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
			List->ResourceBarrier(1, &Barrier);
			List->CopyBufferRegion(Target, 0, Upload.Resource, Upload.Offset, BufferDesc.Size);
			const D3D12_RESOURCE_STATES Final = BufferDesc.Usage == EBufferUsage::Index ? D3D12_RESOURCE_STATE_INDEX_BUFFER : D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
			Barrier = Transition(Target, D3D12_RESOURCE_STATE_COPY_DEST, Final);
			List->ResourceBarrier(1, &Barrier);
		});
	}
	return std::make_shared<FD3D12Buffer>(*this, BufferDesc, std::move(Resource), Mapped);
}

TSharedPtr<FRHITexture> FD3D12Device::CreateTexture(const FRHITextureDesc& TextureDesc)
{
	check(TextureDesc.Width > 0 && TextureDesc.Height > 0);
	const DXGI_FORMAT Format = ToDxgi(TextureDesc.Format);

	D3D12_HEAP_PROPERTIES Heap{};
	Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC ResourceDesc{};
	ResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	ResourceDesc.Width = TextureDesc.Width;
	ResourceDesc.Height = TextureDesc.Height;
	ResourceDesc.DepthOrArraySize = 1;
	ResourceDesc.MipLevels = 1;
	ResourceDesc.Format = Format;
	ResourceDesc.SampleDesc.Count = 1;
	ResourceDesc.Flags = TextureDesc.bRenderTarget ? D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET : D3D12_RESOURCE_FLAG_NONE;

	D3D12_CLEAR_VALUE Clear{};
	Clear.Format = Format;
	Clear.Color[0] = TextureDesc.ClearColor.R;
	Clear.Color[1] = TextureDesc.ClearColor.G;
	Clear.Color[2] = TextureDesc.ClearColor.B;
	Clear.Color[3] = TextureDesc.ClearColor.A;

	// Start in the copy state when there is data to upload.
	const D3D12_RESOURCE_STATES InitialState = TextureDesc.InitialData ? D3D12_RESOURCE_STATE_COPY_DEST : TextureIdleState;
	ComPtr<ID3D12Resource> Resource;
	if (FAILED(Device->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &ResourceDesc, InitialState,
		TextureDesc.bRenderTarget ? &Clear : nullptr, IID_PPV_ARGS(&Resource))))
	{
		GE_LOG(LogRHI, Error, "Failed to create a {}x{} texture", TextureDesc.Width, TextureDesc.Height);
		return nullptr;
	}
	SetName(Resource.Get(), TextureDesc.DebugName);

	const uint32 Srv = SrvHeap.Allocate();
	if (Srv == FD3D12DescriptorHeap::Invalid)
	{
		return nullptr;
	}
	D3D12_SHADER_RESOURCE_VIEW_DESC SrvDesc{};
	SrvDesc.Format = Format;
	SrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	SrvDesc.Texture2D.MipLevels = 1;
	// Single-channel textures read as (1, 1, 1, R): alpha masks (glyphs)
	// then work with the same shader path as RGBA images.
	SrvDesc.Shader4ComponentMapping = TextureDesc.Format == EPixelFormat::R8_UNORM
		? D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
			D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1, D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1,
			D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1, D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0)
		: D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	Device->CreateShaderResourceView(Resource.Get(), &SrvDesc, SrvHeap.GetCpuHandle(Srv));

	uint32 Rtv = FD3D12DescriptorHeap::Invalid;
	if (TextureDesc.bRenderTarget)
	{
		Rtv = RtvHeap.Allocate();
		Device->CreateRenderTargetView(Resource.Get(), nullptr, RtvHeap.GetCpuHandle(Rtv));
	}

	auto Texture = std::make_shared<FD3D12Texture>(*this, TextureDesc, std::move(Resource), Srv, Rtv);
	if (TextureDesc.InitialData)
	{
		ExecuteImmediate([&](ID3D12GraphicsCommandList* List)
		{
			// RecordTextureCopy transitions from the idle state; the texture
			// was created in COPY_DEST, so move it there first.
			const D3D12_RESOURCE_BARRIER Barrier = Transition(Texture->GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, TextureIdleState);
			List->ResourceBarrier(1, &Barrier);
			RecordTextureCopy(List, *Texture, FIntRect(0, 0, static_cast<int32>(TextureDesc.Width), static_cast<int32>(TextureDesc.Height)),
				TextureDesc.InitialData, TextureDesc.InitialRowPitch, ImmediateUpload);
		});
	}
	return Texture;
}

ComPtr<ID3D12RootSignature> FD3D12Device::GetRootSignature(uint32 PushConstantSize, ESamplerFilter Filter)
{
	const uint32 Key = (PushConstantSize << 8) | static_cast<uint32>(Filter);
	if (auto It = RootSignatures.find(Key); It != RootSignatures.end())
	{
		return It->second;
	}

	D3D12_ROOT_PARAMETER Params[2]{};
	uint32 NumParams = 0;
	if (PushConstantSize > 0)
	{
		D3D12_ROOT_PARAMETER& Constants = Params[NumParams++];
		Constants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		Constants.Constants.ShaderRegister = 0;
		Constants.Constants.Num32BitValues = PushConstantSize / 4;
		Constants.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	}
	D3D12_DESCRIPTOR_RANGE Range{};
	Range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	Range.NumDescriptors = 1;
	Range.BaseShaderRegister = 0;
	D3D12_ROOT_PARAMETER& Table = Params[NumParams++];
	Table.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	Table.DescriptorTable.NumDescriptorRanges = 1;
	Table.DescriptorTable.pDescriptorRanges = &Range;
	Table.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_STATIC_SAMPLER_DESC Sampler{};
	Sampler.Filter = Filter == ESamplerFilter::Point ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	Sampler.AddressU = Sampler.AddressV = Sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	Sampler.MaxLOD = D3D12_FLOAT32_MAX;
	Sampler.ShaderRegister = 0;
	Sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC RootDesc{};
	RootDesc.NumParameters = NumParams;
	RootDesc.pParameters = Params;
	RootDesc.NumStaticSamplers = 1;
	RootDesc.pStaticSamplers = &Sampler;
	RootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ComPtr<ID3DBlob> Blob, Error;
	if (FAILED(D3D12SerializeRootSignature(&RootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &Blob, &Error)))
	{
		GE_LOG(LogRHI, Error, "Root signature: {}", Error ? static_cast<const char*>(Error->GetBufferPointer()) : "?");
		return nullptr;
	}
	ComPtr<ID3D12RootSignature> RootSignature;
	if (FAILED(Device->CreateRootSignature(0, Blob->GetBufferPointer(), Blob->GetBufferSize(), IID_PPV_ARGS(&RootSignature))))
	{
		return nullptr;
	}
	RootSignatures.emplace(Key, RootSignature);
	return RootSignature;
}

TSharedPtr<FRHIGraphicsPipeline> FD3D12Device::CreateGraphicsPipeline(const FRHIGraphicsPipelineDesc& PipelineDesc)
{
	check(PipelineDesc.PushConstantSize % 4 == 0 && PipelineDesc.PushConstantSize <= 64);
	ComPtr<ID3D12RootSignature> RootSignature = GetRootSignature(PipelineDesc.PushConstantSize, PipelineDesc.SamplerFilter);
	if (!RootSignature)
	{
		return nullptr;
	}

	TArray<D3D12_INPUT_ELEMENT_DESC> Elements;
	for (const FVertexAttribute& Attribute : PipelineDesc.VertexAttributes)
	{
		Elements.push_back({ Attribute.SemanticName, Attribute.SemanticIndex, ToDxgi(Attribute.Format), 0, Attribute.Offset,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC PsoDesc{};
	PsoDesc.pRootSignature = RootSignature.Get();
	PsoDesc.VS = { PipelineDesc.VertexShader.Data, PipelineDesc.VertexShader.Size };
	PsoDesc.PS = { PipelineDesc.PixelShader.Data, PipelineDesc.PixelShader.Size };
	PsoDesc.InputLayout = { Elements.data(), static_cast<UINT>(Elements.size()) };
	PsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	PsoDesc.NumRenderTargets = 1;
	PsoDesc.RTVFormats[0] = ToDxgi(PipelineDesc.RenderTargetFormat);
	PsoDesc.SampleDesc.Count = 1;
	PsoDesc.SampleMask = UINT_MAX;
	PsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	PsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	PsoDesc.RasterizerState.DepthClipEnable = TRUE;
	PsoDesc.DepthStencilState.DepthEnable = FALSE;

	D3D12_RENDER_TARGET_BLEND_DESC& Blend = PsoDesc.BlendState.RenderTarget[0];
	Blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	Blend.LogicOp = D3D12_LOGIC_OP_NOOP;
	if (PipelineDesc.BlendMode != EBlendMode::Opaque)
	{
		Blend.BlendEnable = TRUE;
		Blend.SrcBlend = PipelineDesc.BlendMode == EBlendMode::PremultipliedAlpha ? D3D12_BLEND_ONE : D3D12_BLEND_SRC_ALPHA;
		Blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		Blend.BlendOp = D3D12_BLEND_OP_ADD;
		Blend.SrcBlendAlpha = D3D12_BLEND_ONE;
		Blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		Blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	}

	ComPtr<ID3D12PipelineState> Pso;
	const HRESULT Hr = Device->CreateGraphicsPipelineState(&PsoDesc, IID_PPV_ARGS(&Pso));
	if (FAILED(Hr))
	{
		GE_LOG(LogRHI, Error, "CreateGraphicsPipelineState failed (0x{:08X})", static_cast<uint32>(Hr));
		FlushDebugMessages();
		return nullptr;
	}
	SetName(Pso.Get(), PipelineDesc.DebugName);
	return std::make_shared<FD3D12Pipeline>(PipelineDesc, std::move(Pso), std::move(RootSignature));
}

namespace
{
	ComPtr<ID3D12Resource> CreateUploadBuffer(ID3D12Device* Device, uint64 Size, uint8*& OutCpu)
	{
		D3D12_HEAP_PROPERTIES Heap{};
		Heap.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC ResourceDesc{};
		ResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		ResourceDesc.Width = Size;
		ResourceDesc.Height = 1;
		ResourceDesc.DepthOrArraySize = 1;
		ResourceDesc.MipLevels = 1;
		ResourceDesc.SampleDesc.Count = 1;
		ResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		ComPtr<ID3D12Resource> Buffer;
		verify(SUCCEEDED(Device->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &ResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&Buffer))));
		SetName(Buffer.Get(), "RHI upload memory");
		void* Cpu = nullptr;
		const D3D12_RANGE NoRead{ 0, 0 };
		Buffer->Map(0, &NoRead, &Cpu);
		OutCpu = static_cast<uint8*>(Cpu);
		return Buffer;
	}
}

FD3D12Device::FUploadAllocation FD3D12Device::AllocateUpload(FUploadRing& Ring, uint64 Size, uint64 Alignment)
{
	if (Ring.ChunkSize == 0)
	{
		Ring.ChunkSize = UploadChunkSize;
	}
	if (Size > Ring.ChunkSize)
	{
		// Too big for a chunk: a dedicated buffer, released once the GPU is done.
		uint8* Cpu = nullptr;
		ComPtr<ID3D12Resource> Buffer = CreateUploadBuffer(Device.Get(), Size, Cpu);
		ID3D12Resource* Raw = Buffer.Get();
		DeferDelete(std::move(Buffer));
		return { Raw, 0, Cpu };
	}

	uint64 Offset = FMath::AlignUp(Ring.CurrentOffset, Alignment);
	if (Ring.CurrentChunk < Ring.Chunks.size() && Offset + Size > Ring.ChunkSize)
	{
		++Ring.CurrentChunk;
		Offset = 0;
	}
	if (Ring.CurrentChunk >= Ring.Chunks.size())
	{
		// Chunks are kept and reused every time the ring is reset.
		uint8* Cpu = nullptr;
		Ring.Chunks.push_back(CreateUploadBuffer(Device.Get(), Ring.ChunkSize, Cpu));
		Ring.ChunkCpu.push_back(Cpu);
		Ring.CurrentChunk = static_cast<uint32>(Ring.Chunks.size() - 1);
		Offset = 0;
	}
	Ring.CurrentOffset = Offset + Size;
	return { Ring.Chunks[Ring.CurrentChunk].Get(), Offset, Ring.ChunkCpu[Ring.CurrentChunk] + Offset };
}

void FD3D12Device::RecordTextureCopy(ID3D12GraphicsCommandList* List, FD3D12Texture& Texture, const FIntRect& Region, const void* Data, uint32 RowPitch, FUploadRing& Ring)
{
	const uint32 Width = static_cast<uint32>(Region.Width());
	const uint32 Height = static_cast<uint32>(Region.Height());
	const uint32 Bpp = GetBytesPerPixel(Texture.GetFormat());
	const uint32 SourcePitch = RowPitch ? RowPitch : Width * Bpp;
	const uint32 AlignedPitch = FMath::AlignUp(Width * Bpp, uint32(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT));

	const FUploadAllocation Upload = AllocateUpload(Ring, uint64(AlignedPitch) * Height, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
	for (uint32 Row = 0; Row < Height; ++Row)
	{
		memcpy(Upload.Cpu + SIZE_T(Row) * AlignedPitch, static_cast<const uint8*>(Data) + SIZE_T(Row) * SourcePitch, SIZE_T(Width) * Bpp);
	}

	D3D12_TEXTURE_COPY_LOCATION Dest{};
	Dest.pResource = Texture.GetResource();
	Dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	D3D12_TEXTURE_COPY_LOCATION Source{};
	Source.pResource = Upload.Resource;
	Source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	Source.PlacedFootprint.Offset = Upload.Offset;
	Source.PlacedFootprint.Footprint = { ToDxgi(Texture.GetFormat()), Width, Height, 1, AlignedPitch };

	D3D12_RESOURCE_BARRIER Barrier = Transition(Texture.GetResource(), TextureIdleState, D3D12_RESOURCE_STATE_COPY_DEST);
	List->ResourceBarrier(1, &Barrier);
	List->CopyTextureRegion(&Dest, static_cast<UINT>(Region.Left), static_cast<UINT>(Region.Top), 0, &Source, nullptr);
	Barrier = Transition(Texture.GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, TextureIdleState);
	List->ResourceBarrier(1, &Barrier);
}

template <typename F>
void FD3D12Device::ExecuteImmediate(F&& Record)
{
	// Rare (resource creation, readback): simple and synchronous.
	WaitIdle();
	ImmediateAllocator->Reset();
	ImmediateList->Reset(ImmediateAllocator.Get(), nullptr);
	ImmediateUpload.CurrentChunk = 0;
	ImmediateUpload.CurrentOffset = 0;
	Record(ImmediateList.Get());
	ImmediateList->Close();
	ID3D12CommandList* Lists[] = { ImmediateList.Get() };
	Queue->ExecuteCommandLists(1, Lists);
	WaitIdle();
	FlushDebugMessages();
}

void FD3D12Device::UpdateTexture(FRHITexture& Texture, const FIntRect& Region, const void* Data, uint32 RowPitch)
{
	check(!Region.IsEmpty() && Region.Right <= static_cast<int32>(Texture.GetWidth()) && Region.Bottom <= static_cast<int32>(Texture.GetHeight()));
	auto& D3DTexture = static_cast<FD3D12Texture&>(Texture);
	if (!bInFrame)
	{
		ExecuteImmediate([&](ID3D12GraphicsCommandList* List) { RecordTextureCopy(List, D3DTexture, Region, Data, RowPitch, ImmediateUpload); });
		return;
	}
	FFrame& Frame = Frames[FrameIndex];
	if (!Frame.bUploadOpen)
	{
		UploadList->Reset(Frame.UploadAllocator.Get(), nullptr);
		Frame.bUploadOpen = true;
	}
	RecordTextureCopy(UploadList.Get(), D3DTexture, Region, Data, RowPitch, Frame.Upload);
}

bool FD3D12Device::ReadbackTexture(FRHITexture& Texture, TArray<uint8>& OutPixels)
{
	check(!bInFrame);
	auto& D3DTexture = static_cast<FD3D12Texture&>(Texture);
	const uint32 Width = Texture.GetWidth();
	const uint32 Height = Texture.GetHeight();
	const uint32 Bpp = GetBytesPerPixel(Texture.GetFormat());
	const uint32 AlignedPitch = FMath::AlignUp(Width * Bpp, uint32(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT));

	D3D12_HEAP_PROPERTIES Heap{};
	Heap.Type = D3D12_HEAP_TYPE_READBACK;
	D3D12_RESOURCE_DESC ResourceDesc{};
	ResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	ResourceDesc.Width = uint64(AlignedPitch) * Height;
	ResourceDesc.Height = 1;
	ResourceDesc.DepthOrArraySize = 1;
	ResourceDesc.MipLevels = 1;
	ResourceDesc.SampleDesc.Count = 1;
	ResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	ComPtr<ID3D12Resource> Readback;
	if (FAILED(Device->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &ResourceDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&Readback))))
	{
		return false;
	}

	ExecuteImmediate([&](ID3D12GraphicsCommandList* List)
	{
		D3D12_RESOURCE_BARRIER Barrier = Transition(D3DTexture.GetResource(), TextureIdleState, D3D12_RESOURCE_STATE_COPY_SOURCE);
		List->ResourceBarrier(1, &Barrier);
		D3D12_TEXTURE_COPY_LOCATION Dest{};
		Dest.pResource = Readback.Get();
		Dest.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		Dest.PlacedFootprint.Footprint = { ToDxgi(Texture.GetFormat()), Width, Height, 1, AlignedPitch };
		D3D12_TEXTURE_COPY_LOCATION Source{};
		Source.pResource = D3DTexture.GetResource();
		Source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		List->CopyTextureRegion(&Dest, 0, 0, 0, &Source, nullptr);
		Barrier = Transition(D3DTexture.GetResource(), D3D12_RESOURCE_STATE_COPY_SOURCE, TextureIdleState);
		List->ResourceBarrier(1, &Barrier);
	});

	void* Mapped = nullptr;
	const D3D12_RANGE ReadRange{ 0, static_cast<SIZE_T>(ResourceDesc.Width) };
	if (FAILED(Readback->Map(0, &ReadRange, &Mapped)))
	{
		return false;
	}
	OutPixels.resize(SIZE_T(Width) * Height * Bpp);
	for (uint32 Row = 0; Row < Height; ++Row)
	{
		memcpy(OutPixels.data() + SIZE_T(Row) * Width * Bpp, static_cast<const uint8*>(Mapped) + SIZE_T(Row) * AlignedPitch, SIZE_T(Width) * Bpp);
	}
	const D3D12_RANGE NoWrite{ 0, 0 };
	Readback->Unmap(0, &NoWrite);
	return true;
}

FRHICommandList& FD3D12Device::BeginFrame()
{
	check(!bInFrame);
	FrameIndex = (FrameIndex + 1) % MaxFramesInFlight;
	FFrame& Frame = Frames[FrameIndex];
	WaitForFence(Frame.FenceValue);
	CollectGarbage();

	Frame.Allocator->Reset();
	Frame.UploadAllocator->Reset();
	Frame.Upload.CurrentChunk = 0;
	Frame.Upload.CurrentOffset = 0;
	Frame.bUploadOpen = false;
	MainList->Reset(Frame.Allocator.Get(), nullptr);
	CommandList.Begin(MainList.Get());
	PresentList.clear();
	bInFrame = true;
	return CommandList;
}

void FD3D12Device::MarkPresent(FD3D12SwapChain* SwapChain)
{
	if (!ArrayUtil::Contains(PresentList, SwapChain))
	{
		PresentList.push_back(SwapChain);
	}
}

void FD3D12Device::EndFrame()
{
	check(bInFrame);
	FFrame& Frame = Frames[FrameIndex];
	MainList->Close();
	ID3D12CommandList* Lists[2];
	UINT NumLists = 0;
	if (Frame.bUploadOpen)
	{
		UploadList->Close();
		Lists[NumLists++] = UploadList.Get();
	}
	Lists[NumLists++] = MainList.Get();
	Queue->ExecuteCommandLists(NumLists, Lists);

	for (FD3D12SwapChain* SwapChain : PresentList)
	{
		SwapChain->Present();
	}
	PresentList.clear();

	Queue->Signal(Fence.Get(), ++LastSignaledValue);
	Frame.FenceValue = LastSignaledValue;
	bInFrame = false;
	FlushDebugMessages();
}
