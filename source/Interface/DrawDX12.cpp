/*
Copyright (C) 1996-1997 GX Media, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "stdafx.h"
#include "Draw.h"
#include "DrawDX12.h"

#include <map>

// Generated from DrawShaders.hlsl by the project's CustomBuild step.
#include "DrawShaders_DX12_VS.h"
#include "DrawShaders_DX12_PSColor.h"
#include "DrawShaders_DX12_PSTexture.h"

using Microsoft::WRL::ComPtr;

static const DXGI_FORMAT COLOR_FORMAT = DXGI_FORMAT_B8G8R8A8_UNORM;
static const DXGI_FORMAT DEPTH_FORMAT = DXGI_FORMAT_D32_FLOAT;
static const UINT SRV_HEAP_SIZE = 8192;

#ifdef IS_DRAWDLL
FDrawDX12 *
NewDraw(CWnd *wnd) {
	return new FDrawDX12(wnd);
}
#endif

static void
Check(HRESULT hr, const char *what) {
	if(FAILED(hr))
		LFatal("DirectX 12: %s failed (error 0x%08X).", what, (unsigned int)hr);
}

static D3D12_HEAP_PROPERTIES
HeapProps(D3D12_HEAP_TYPE type) {
	D3D12_HEAP_PROPERTIES props = {};
	props.Type = type;
	props.CreationNodeMask = 1;
	props.VisibleNodeMask = 1;
	return props;
}

static D3D12_RESOURCE_DESC
BufferDesc(UINT64 size) {
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = size;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	return desc;
}

static D3D12_RESOURCE_DESC
Texture2DDesc(DXGI_FORMAT format, UINT width, UINT height, D3D12_RESOURCE_FLAGS flags) {
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = width;
	desc.Height = height;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = format;
	desc.SampleDesc.Count = 1;
	desc.Flags = flags;
	return desc;
}

static void
Transition(ID3D12GraphicsCommandList *list, ID3D12Resource *res,
		   D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = res;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = before;
	barrier.Transition.StateAfter = after;
	list->ResourceBarrier(1, &barrier);
}

// ========== DX12Context ==========
//
// Device, queue, pipelines and texture cache shared by every view.
// Each Paint() records one command list and waits for it to finish,
// so nothing here needs per-frame lifetime tracking.

class DX12Context {
public:
	static DX12Context *Acquire(void);
	void Release(void);

	void BeginFrame(void);
	void Submit(void);
	void WaitIdle(void);
	bool GetTexture(Texture *texture, const palette_t *pal, D3D12_GPU_DESCRIPTOR_HANDLE &handle);

	ComPtr<IDXGIFactory4> factory;
	ComPtr<ID3D12Device> device;
	ComPtr<ID3D12CommandQueue> queue;
	ComPtr<ID3D12GraphicsCommandList> list;
	ComPtr<ID3D12RootSignature> rootSig;
	ComPtr<ID3D12PipelineState> psoLines, psoSolid, psoTexture;
	ComPtr<ID3D12DescriptorHeap> srvHeap;

private:
	struct TexEntry {
		ComPtr<ID3D12Resource> resource;
		void *mip0;
		int width, height, bits;
		UINT slot;
	};

	DX12Context();
	~DX12Context();

	void CreateDevice(void);
	void CreatePipelines(void);
	ComPtr<ID3D12PipelineState> CreatePipeline(D3D12_PRIMITIVE_TOPOLOGY_TYPE topology,
		const void *ps, size_t psSize, bool depth);

	static DX12Context *instance;
	int refs;

	ComPtr<ID3D12CommandAllocator> allocator;
	ComPtr<ID3D12Fence> fence;
	UINT64 fenceValue;
	HANDLE fenceEvent;

	UINT srvInc;
	UINT nextSlot;
	std::vector<UINT> freeSlots;
	std::map<Texture *, TexEntry> textures;
	std::vector<ComPtr<ID3D12Resource> > pendingUploads;
};

DX12Context *DX12Context::instance = NULL;

DX12Context *
DX12Context::Acquire(void) {
	if(!instance)
		instance = new DX12Context();
	instance->refs++;
	return instance;
}

void
DX12Context::Release(void) {
	if(--refs == 0) {
		instance = NULL;
		delete this;
	}
}

DX12Context::DX12Context() {
	refs = 0;
	fenceValue = 0;
	nextSlot = 0;

	CreateDevice();

	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)), "CreateCommandQueue");
	Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
		"CreateCommandAllocator");
	Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), NULL,
		IID_PPV_ARGS(&list)), "CreateCommandList");
	list->Close();

	Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "CreateFence");
	fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.NumDescriptors = SRV_HEAP_SIZE;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	Check(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&srvHeap)), "CreateDescriptorHeap");
	srvInc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	CreatePipelines();
}

DX12Context::~DX12Context() {
	WaitIdle();
	CloseHandle(fenceEvent);
}

void
DX12Context::CreateDevice(void) {
	UINT factoryFlags = 0;

#ifdef _DEBUG
	ComPtr<ID3D12Debug> debug;
	if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
		debug->EnableDebugLayer();
		factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
	}
#endif

	Check(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2");

	// Default adapter first, then the WARP software rasterizer.
	if(SUCCEEDED(D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
		return;

	ComPtr<IDXGIAdapter> warp;
	if(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))) &&
	   SUCCEEDED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
		return;

	LFatal("DirectX 12 is not available on this system.\n"
		   "Select a different renderer in the drawing options.");
}

void
DX12Context::CreatePipelines(void) {
	D3D12_ROOT_PARAMETER params[2] = {};

	params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	params[0].Constants.ShaderRegister = 0;
	params[0].Constants.Num32BitValues = 4;
	params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	D3D12_DESCRIPTOR_RANGE range = {};
	range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	range.NumDescriptors = 1;
	range.BaseShaderRegister = 0;
	range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[1].DescriptorTable.NumDescriptorRanges = 1;
	params[1].DescriptorTable.pDescriptorRanges = &range;
	params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_STATIC_SAMPLER_DESC sampler = {};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderRegister = 0;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
	rsDesc.NumParameters = 2;
	rsDesc.pParameters = params;
	rsDesc.NumStaticSamplers = 1;
	rsDesc.pStaticSamplers = &sampler;
	rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ComPtr<ID3DBlob> blob, error;
	Check(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
		"D3D12SerializeRootSignature");
	Check(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
		IID_PPV_ARGS(&rootSig)), "CreateRootSignature");

	psoLines = CreatePipeline(D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE,
		g_DX12_PSColor, sizeof(g_DX12_PSColor), false);
	psoSolid = CreatePipeline(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
		g_DX12_PSColor, sizeof(g_DX12_PSColor), true);
	psoTexture = CreatePipeline(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
		g_DX12_PSTexture, sizeof(g_DX12_PSTexture), true);
}

ComPtr<ID3D12PipelineState>
DX12Context::CreatePipeline(D3D12_PRIMITIVE_TOPOLOGY_TYPE topology,
							const void *ps, size_t psSize, bool depth) {
	static const D3D12_INPUT_ELEMENT_DESC layout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(GpuVertex, x),
		  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(GpuVertex, s),
		  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(GpuVertex, color),
		  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
	desc.pRootSignature = rootSig.Get();
	desc.VS.pShaderBytecode = g_DX12_VSMain;
	desc.VS.BytecodeLength = sizeof(g_DX12_VSMain);
	desc.PS.pShaderBytecode = ps;
	desc.PS.BytecodeLength = psSize;
	desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	desc.SampleMask = UINT_MAX;
	desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	desc.RasterizerState.DepthClipEnable = TRUE;
	desc.DepthStencilState.DepthEnable = depth ? TRUE : FALSE;
	desc.DepthStencilState.DepthWriteMask = depth ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
	desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	desc.InputLayout.pInputElementDescs = layout;
	desc.InputLayout.NumElements = sizeof(layout) / sizeof(layout[0]);
	desc.PrimitiveTopologyType = topology;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = COLOR_FORMAT;
	desc.DSVFormat = DEPTH_FORMAT;
	desc.SampleDesc.Count = 1;

	ComPtr<ID3D12PipelineState> pso;
	Check(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso)), "CreateGraphicsPipelineState");
	return pso;
}

void
DX12Context::BeginFrame(void) {
	Check(allocator->Reset(), "CommandAllocator::Reset");
	Check(list->Reset(allocator.Get(), NULL), "CommandList::Reset");
}

void
DX12Context::Submit(void) {
	Check(list->Close(), "CommandList::Close");
	ID3D12CommandList *lists[] = { list.Get() };
	queue->ExecuteCommandLists(1, lists);
}

void
DX12Context::WaitIdle(void) {
	fenceValue++;
	Check(queue->Signal(fence.Get(), fenceValue), "CommandQueue::Signal");
	if(fence->GetCompletedValue() < fenceValue) {
		fence->SetEventOnCompletion(fenceValue, fenceEvent);
		WaitForSingleObject(fenceEvent, INFINITE);
	}
	pendingUploads.clear();
}

bool
DX12Context::GetTexture(Texture *texture, const palette_t *pal, D3D12_GPU_DESCRIPTOR_HANDLE &handle) {
	std::map<Texture *, TexEntry>::iterator it = textures.find(texture);

	if(it != textures.end()) {
		TexEntry &e = it->second;
		if(e.mip0 == texture->mip[0] && e.width == texture->width &&
		   e.height == texture->height && e.bits == texture->bits) {
			handle = srvHeap->GetGPUDescriptorHandleForHeapStart();
			handle.ptr += (UINT64)e.slot * srvInc;
			return true;
		}
		// The Texture was reloaded or its memory reused; the GPU is idle
		// between frames, so the old copy can go right away.
		freeSlots.push_back(e.slot);
		textures.erase(it);
	}

	UINT slot;
	if(!freeSlots.empty()) {
		slot = freeSlots.back();
		freeSlots.pop_back();
	}
	else if(nextSlot < SRV_HEAP_SIZE)
		slot = nextSlot++;
	else
		return false;

	unsigned char *rgba = FDrawGPU::ConvertTexture(texture, pal);
	if(!rgba) {
		freeSlots.push_back(slot);
		return false;
	}

	TexEntry e;
	e.mip0 = texture->mip[0];
	e.width = texture->width;
	e.height = texture->height;
	e.bits = texture->bits;
	e.slot = slot;

	D3D12_HEAP_PROPERTIES defaultHeap = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
	D3D12_RESOURCE_DESC texDesc = Texture2DDesc(DXGI_FORMAT_R8G8B8A8_UNORM,
		texture->width, texture->height, D3D12_RESOURCE_FLAG_NONE);
	Check(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
		D3D12_RESOURCE_STATE_COPY_DEST, NULL, IID_PPV_ARGS(&e.resource)), "CreateCommittedResource (texture)");

	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
	UINT64 uploadSize;
	device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, NULL, NULL, &uploadSize);

	ComPtr<ID3D12Resource> upload;
	D3D12_HEAP_PROPERTIES uploadHeap = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
	D3D12_RESOURCE_DESC uploadDesc = BufferDesc(uploadSize);
	Check(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, NULL, IID_PPV_ARGS(&upload)), "CreateCommittedResource (upload)");

	unsigned char *mapped;
	Check(upload->Map(0, NULL, (void **)&mapped), "Map (texture upload)");
	for(int y = 0; y < texture->height; y++)
		memcpy(mapped + footprint.Offset + y * footprint.Footprint.RowPitch,
			   rgba + y * texture->width * 4, texture->width * 4);
	upload->Unmap(0, NULL);
	delete [] rgba;

	D3D12_TEXTURE_COPY_LOCATION dst = {};
	dst.pResource = e.resource.Get();
	dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dst.SubresourceIndex = 0;

	D3D12_TEXTURE_COPY_LOCATION src = {};
	src.pResource = upload.Get();
	src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	src.PlacedFootprint = footprint;

	list->CopyTextureRegion(&dst, 0, 0, 0, &src, NULL);
	Transition(list.Get(), e.resource.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	pendingUploads.push_back(upload);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;

	D3D12_CPU_DESCRIPTOR_HANDLE cpu = srvHeap->GetCPUDescriptorHandleForHeapStart();
	cpu.ptr += (SIZE_T)slot * srvInc;
	device->CreateShaderResourceView(e.resource.Get(), &srvDesc, cpu);

	textures[texture] = e;

	handle = srvHeap->GetGPUDescriptorHandleForHeapStart();
	handle.ptr += (UINT64)slot * srvInc;
	return true;
}

// ========== FDrawDX12 ==========

FDrawDX12::FDrawDX12(CWnd *wnd) : FDrawGPU(wnd) {
	vertexCapacity = 0;

	ctx = DX12Context::Acquire();

	CRect rect;
	wnd->GetClientRect(&rect);
	FDraw::Size(rect.Width(), rect.Height());

	DXGI_SWAP_CHAIN_DESC1 desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.Format = COLOR_FORMAT;
	desc.SampleDesc.Count = 1;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.BufferCount = BACK_BUFFERS;
	desc.Scaling = DXGI_SCALING_STRETCH;
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

	ComPtr<IDXGISwapChain1> swapChain1;
	Check(ctx->factory->CreateSwapChainForHwnd(ctx->queue.Get(), wnd->m_hWnd, &desc, NULL, NULL,
		&swapChain1), "CreateSwapChainForHwnd");
	Check(swapChain1.As(&swapChain), "IDXGISwapChain3");
	ctx->factory->MakeWindowAssociation(wnd->m_hWnd, DXGI_MWA_NO_ALT_ENTER);

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	heapDesc.NumDescriptors = BACK_BUFFERS;
	Check(ctx->device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeap)), "CreateDescriptorHeap (RTV)");
	rtvInc = ctx->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	heapDesc.NumDescriptors = 1;
	Check(ctx->device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&dsvHeap)), "CreateDescriptorHeap (DSV)");

	CreateTargets();
}

FDrawDX12::~FDrawDX12() {
	ctx->WaitIdle();
	ReleaseTargets();
	vertexBuffer.Reset();
	swapChain.Reset();
	ctx->Release();
}

void
FDrawDX12::CreateTargets(void) {
	D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
	for(int i = 0; i < BACK_BUFFERS; i++) {
		Check(swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer[i])), "GetBuffer");
		ctx->device->CreateRenderTargetView(backBuffer[i].Get(), NULL, rtv);
		rtv.ptr += rtvInc;
	}

	D3D12_HEAP_PROPERTIES heap = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
	D3D12_RESOURCE_DESC desc = Texture2DDesc(DEPTH_FORMAT, width, height,
		D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
	D3D12_CLEAR_VALUE clear = {};
	clear.Format = DEPTH_FORMAT;
	clear.DepthStencil.Depth = 1.0f;
	Check(ctx->device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&depthBuffer)), "CreateCommittedResource (depth)");
	ctx->device->CreateDepthStencilView(depthBuffer.Get(), NULL, dsvHeap->GetCPUDescriptorHandleForHeapStart());
}

void
FDrawDX12::ReleaseTargets(void) {
	for(int i = 0; i < BACK_BUFFERS; i++)
		backBuffer[i].Reset();
	depthBuffer.Reset();
}

void
FDrawDX12::Size(int cx, int cy) {
	FDraw::Size(cx, cy);

	DXGI_SWAP_CHAIN_DESC1 desc;
	swapChain->GetDesc1(&desc);
	if(desc.Width == (UINT)width && desc.Height == (UINT)height)
		return;

	ctx->WaitIdle();
	ReleaseTargets();
	Check(swapChain->ResizeBuffers(BACK_BUFFERS, width, height, COLOR_FORMAT, 0), "ResizeBuffers");
	CreateTargets();
}

void
FDrawDX12::Paint(CPaintDC *pPaint) {
	BuildFrame(frame);

	ctx->BeginFrame();
	ID3D12GraphicsCommandList *list = ctx->list.Get();

	// Upload any new textures before the render pass.
	std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> texHandle(frame.triBatches.size());
	std::vector<bool> texReady(frame.triBatches.size(), false);
	for(size_t i = 0; i < frame.triBatches.size(); i++) {
		if(frame.triBatches[i].texture)
			texReady[i] = ctx->GetTexture(frame.triBatches[i].texture, GetPalette(), texHandle[i]);
	}

	UINT vbBytes = (UINT)(frame.verts.size() * sizeof(GpuVertex));
	if(vbBytes) {
		if(vbBytes > vertexCapacity) {
			vertexCapacity = max(vbBytes + vbBytes / 2, 64u * 1024u);
			vertexBuffer.Reset();
			D3D12_HEAP_PROPERTIES heap = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
			D3D12_RESOURCE_DESC desc = BufferDesc(vertexCapacity);
			Check(ctx->device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
				D3D12_RESOURCE_STATE_GENERIC_READ, NULL, IID_PPV_ARGS(&vertexBuffer)),
				"CreateCommittedResource (vertices)");
		}

		void *mapped;
		Check(vertexBuffer->Map(0, NULL, &mapped), "Map (vertices)");
		memcpy(mapped, &frame.verts[0], vbBytes);
		vertexBuffer->Unmap(0, NULL);
	}

	UINT index = swapChain->GetCurrentBackBufferIndex();
	ID3D12Resource *target = backBuffer[index].Get();
	Transition(list, target, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

	D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
	rtv.ptr += index * rtvInc;
	D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsvHeap->GetCPUDescriptorHandleForHeapStart();

	static const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	list->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
	list->ClearRenderTargetView(rtv, black, 0, NULL);
	list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);

	D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };
	D3D12_RECT scissor = { 0, 0, width, height };
	list->RSSetViewports(1, &viewport);
	list->RSSetScissorRects(1, &scissor);

	if(vbBytes) {
		ID3D12DescriptorHeap *heaps[] = { ctx->srvHeap.Get() };
		list->SetDescriptorHeaps(1, heaps);
		list->SetGraphicsRootSignature(ctx->rootSig.Get());

		float scale[4] = { 2.0f / width, 2.0f / height, 0.0f, 0.0f };
		list->SetGraphicsRoot32BitConstants(0, 4, scale, 0);

		D3D12_VERTEX_BUFFER_VIEW vbv;
		vbv.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
		vbv.SizeInBytes = vbBytes;
		vbv.StrideInBytes = sizeof(GpuVertex);
		list->IASetVertexBuffers(0, 1, &vbv);

		list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		for(size_t i = 0; i < frame.triBatches.size(); i++) {
			const GpuBatch &batch = frame.triBatches[i];
			if(texReady[i]) {
				list->SetPipelineState(ctx->psoTexture.Get());
				list->SetGraphicsRootDescriptorTable(1, texHandle[i]);
			}
			else
				list->SetPipelineState(ctx->psoSolid.Get());
			list->DrawInstanced(batch.count, 1, batch.first, 0);
		}

		if(frame.lineCount) {
			list->SetPipelineState(ctx->psoLines.Get());
			list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
			list->DrawInstanced(frame.lineCount, 1, frame.lineFirst, 0);
		}
	}

	Transition(list, target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

	ctx->Submit();
	HRESULT hr = swapChain->Present(0, 0);
	if(hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
		LFatal("DirectX 12: the graphics device was removed (error 0x%08X).",
			   (unsigned int)ctx->device->GetDeviceRemovedReason());
	ctx->WaitIdle();
}
