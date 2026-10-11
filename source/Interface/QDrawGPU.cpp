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
#include "QDraw.h"
#include "QDrawGPU.h"

#include <algorithm>

#ifdef IS_QDRAWDLL
void
LFatal(const char *fmt, ...) {
	va_list arglist;
	char buf[256] = "";

	if(fmt) {
		va_start(arglist, fmt);
		vsprintf(buf, fmt, arglist);
		va_end(arglist);
	}

	MessageBox(NULL, buf, "Qoole 99 Fatal Error", MB_OK);

	exit(1);
}
#endif

static inline unsigned int
PackRGBA(int r, int g, int b) {
	return (unsigned int)r | ((unsigned int)g << 8) | ((unsigned int)b << 16) | 0xff000000;
}

// ========== QDrawGPU ==========

QDrawGPU::QDrawGPU(CWnd *wnd) : QDraw(wnd) {
	numBufs = 1;
	color = 0;
}

QDrawGPU::~QDrawGPU() {
}

unsigned int
QDrawGPU::PalColor(int index) const {
	return PackRGBA(pal[index].red, pal[index].green, pal[index].blue);
}

void
QDrawGPU::Clear(void) {
	lines.clear();
	tris.clear();
}

void
QDrawGPU::Color(int newcolor) {
	color = newcolor;
}

void
QDrawGPU::Begin(void) {
}

void
QDrawGPU::Line(int x1, int y1, int x2, int y2) {
	// Offset to pixel centers so lines land on the same pixels as GDI.
	GpuVertex v = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, PalColor(color) };

	v.x = x1 + 0.5f;
	v.y = y1 + 0.5f;
	lines.push_back(v);

	v.x = x2 + 0.5f;
	v.y = y2 + 0.5f;
	lines.push_back(v);
}

void
QDrawGPU::CopyBuf(int bufNum) {
}

void
QDrawGPU::RealizePal(void) {
}

void
QDrawGPU::ZBufInit(void) {
}

void
QDrawGPU::ZBufClear(void) {
}

void
QDrawGPU::ZBufTriangle(ZPoint zpoint[3]) {
	Tri tri;
	unsigned int c;

	if(bits == 8)
		c = PalColor(zbufColor);
	else // bits == 16
		c = PackRGBA(((zbufColor >> 11) & 31) << 3,
					 ((zbufColor >> 5) & 63) << 2,
					 ((zbufColor >> 0) & 31) << 3);

	for(int i = 0; i < 3; i++) {
		GpuVertex &v = tri.v[i];
		v.x = zpoint[i].x + orgX;
		v.y = orgY - zpoint[i].y;
		v.q = 1.0f / zpoint[i].z;
		v.s = zbufTex ? zpoint[i].s / zbufTex->width : 0.0f;
		v.t = zbufTex ? zpoint[i].t / zbufTex->height : 0.0f;
		v.color = c;
	}

	tri.texture = (zbufTex && zbufTex->mip[0]) ? zbufTex : NULL;
	tris.push_back(tri);
}

void
QDrawGPU::ZBufRender(void) {
}

void
QDrawGPU::BuildFrame(GpuFrame &frame) {
	frame.verts.clear();
	frame.triBatches.clear();

	if(renderMode == ID_VIEW_RENDER_SOLID && !tris.empty()) {
		GpuBatch batch = { NULL, 0, (unsigned int)tris.size() * 3 };
		for(size_t i = 0; i < tris.size(); i++)
			frame.verts.insert(frame.verts.end(), tris[i].v, tris[i].v + 3);
		frame.triBatches.push_back(batch);
	}
	else if(renderMode == ID_VIEW_RENDER_TEXTURE && !tris.empty()) {
		// Group triangles by texture to keep state changes down.
		std::vector<unsigned int> order(tris.size());
		for(unsigned int i = 0; i < order.size(); i++)
			order[i] = i;
		std::stable_sort(order.begin(), order.end(),
			[this](unsigned int a, unsigned int b) { return tris[a].texture < tris[b].texture; });

		for(size_t i = 0; i < order.size(); i++) {
			const Tri &tri = tris[order[i]];
			if(frame.triBatches.empty() || frame.triBatches.back().texture != tri.texture) {
				GpuBatch batch = { tri.texture, (unsigned int)frame.verts.size(), 0 };
				frame.triBatches.push_back(batch);
			}
			frame.verts.insert(frame.verts.end(), tri.v, tri.v + 3);
			frame.triBatches.back().count += 3;
		}
	}

	frame.lineFirst = (unsigned int)frame.verts.size();
	frame.lineCount = (unsigned int)lines.size();
	frame.verts.insert(frame.verts.end(), lines.begin(), lines.end());
}

unsigned char *
QDrawGPU::ConvertTexture(Texture *texture, const palette_t *pal) {
	if(!texture->mip[0] || texture->width <= 0 || texture->height <= 0)
		return NULL;

	int count = texture->width * texture->height;
	unsigned char *rgba = new unsigned char[count * 4];
	unsigned char *dst = rgba;

	if(texture->bits == 8) {
		unsigned char *src = (unsigned char *)texture->mip[0];
		for(int i = 0; i < count; i++) {
			const palette_t &p = pal[*src++];
			*dst++ = p.red;
			*dst++ = p.green;
			*dst++ = p.blue;
			*dst++ = 255;
		}
	}
	else {
		unsigned short *src = (unsigned short *)texture->mip[0];
		for(int i = 0; i < count; i++) {
			*dst++ = ((*src >> 11) & 31) << 3;
			*dst++ = ((*src >> 5) & 63) << 2;
			*dst++ = ((*src >> 0) & 31) << 3;
			*dst++ = 255;
			src++;
		}
	}

	return rgba;
}
