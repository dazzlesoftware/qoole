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

#ifndef __DRAWGPU_H
#define __DRAWGPU_H

#include <vector>

// ========== FDrawGPU ==========
//
// Shared front end for the explicit-API renderers (DirectX 12, Vulkan).
// Like FDrawOpenGL, lines and triangles are collected between Clear()
// and Paint(); Paint() hands the backend a ready-to-upload vertex list.

struct GpuVertex {
	float x, y;			// window pixels, y down
	float q;			// 1 / view depth (1 for 2D lines)
	float s, t;			// normalized texture coordinates
	unsigned int color;	// R8G8B8A8
};

struct GpuBatch {
	Texture *texture;	// NULL = flat colored
	unsigned int first, count;
};

struct GpuFrame {
	std::vector<GpuVertex> verts;
	std::vector<GpuBatch> triBatches;
	unsigned int lineFirst, lineCount;
};

class FDrawGPU : public FDraw {
public:
	FDrawGPU(CWnd *wnd);
	~FDrawGPU();

	void Clear(void);
	void Color(int color);
	void Begin(void);
	void Line(int x1, int y1, int x2, int y2);
	void CopyBuf(int bufNum);
	void RealizePal(void);

	void ZBufInit(void);
	void ZBufClear(void);
	void ZBufTriangle(ZPoint zpoint[3]);
	void ZBufRender(void);

	// Returns a new[]'d RGBA8 copy of the texture's top mip, or NULL.
	static unsigned char *ConvertTexture(Texture *texture, const palette_t *pal);

protected:
	// Fills frame with the current lines and triangles for renderMode.
	void BuildFrame(GpuFrame &frame);

	const palette_t *GetPalette(void) const { return pal; }

private:
	struct Tri {
		GpuVertex v[3];
		Texture *texture;
	};

	unsigned int PalColor(int index) const;

	int color;
	std::vector<GpuVertex> lines;
	std::vector<Tri> tris;
};

#endif
