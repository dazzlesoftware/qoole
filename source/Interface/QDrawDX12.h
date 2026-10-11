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

#ifndef __QDRAWDX12_H
#define __QDRAWDX12_H

#include "QDrawGPU.h"

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

class DX12Context;

// ========== QDrawDX12 ==========

class QDrawDX12 : public QDrawGPU {
public:
	QDrawDX12(CWnd *wnd);
	~QDrawDX12();

	void Size(int cx, int cy);
	void Paint(CPaintDC *pPaint);

private:
	enum { BACK_BUFFERS = 2 };

	void CreateTargets(void);
	void ReleaseTargets(void);

	DX12Context *ctx;

	Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain;
	Microsoft::WRL::ComPtr<ID3D12Resource> backBuffer[BACK_BUFFERS];
	Microsoft::WRL::ComPtr<ID3D12Resource> depthBuffer;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap;
	UINT rtvInc;

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
	UINT vertexCapacity;

	GpuFrame frame;
};

#endif
