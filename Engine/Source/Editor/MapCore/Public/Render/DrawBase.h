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

// The part of the renderer interface that the map core uses, free of any
// window system. The UI derives FWndDraw (Interface/Draw.h) from it to
// attach a window; the GenesisDraw*.dll plugins implement that.

#ifndef __DRAWBASE_H
#define __DRAWBASE_H

#include "LCommon.h"
#include "Texture.h"
#include "Objects.h"
#include "Geometry.h"
#include <math.h>

#define TEXTRI_MAX 2500

typedef struct {
	float x, y, z;
	float s, t;
} ZPoint;

typedef struct {
	int x, y, z;
	int s, t;
} IZPoint;


struct TexTri {
	void *next;
	int zAvg;
	IZPoint zp[3];
	Texture *zbufTex;
	unsigned short zbufColor;
};


// ========== FDraw ==========

class FDraw {
public:
	FDraw() {
		numBufs = 2;
		useBuf = 0;
		pZoomVal = NULL;
		useZBuf = false;
		renderMode = 0;
		fixedPal = false;
	}
	virtual ~FDraw() { }

	static void Init(void);
	static void Exit(void);

	void UseBuf(int bufNum) { check(bufNum < numBufs); useBuf = bufNum; }
	int GetNumBufs(void) { return numBufs; }
	void SetZoomPtr(float *_pZoomVal) { pZoomVal = _pZoomVal; }
	// What RenderLine and RenderPoint scale by.
	float GetZoom(void) const { return pZoomVal ? *pZoomVal : 1.0f; }

	void RenderLine(float x1, float y1, float x2, float y2);
	void RenderPoint(float x, float y, int size = 5);
	void Grid(float xorg, float yorg);
	void Point(int x, int y, int size);

	void Box(int x1, int y1, int x2, int y2);
	void StippleBox(int x1, int y1, int x2, int y2, int l1, int l2);
	void Cross(int x1, int y1, int size);
	void SetColor(int color, int r, int g, int b);
	void UseColor(int color);
	void LoadPal(char *name);

	void RenderMode(int mode) { renderMode = mode; }

	virtual void Size(int cx, int cy) {
		if(cx < 1)
			cx = 1;
		if(cy < 1)
			cy = 1;

		width = cx;
		height = cy;
		orgX = cx / 2;
		orgY = cy / 2;
	}

	virtual void Suspend(void) { };
	virtual void Resume(void) { };

	virtual void Clear(void) = 0;
	virtual void Color(int color) = 0;
	virtual void Begin(void) = 0;
	virtual void Line(int x1, int y1, int x2, int y2) = 0;
	virtual void CopyBuf(int bufNum) = 0;
	virtual void RealizePal(void) = 0;

	// Z-Buffering
	virtual void ZBufInit(void) = 0;
	virtual void ZBufClear(void) = 0;
	virtual void ZBufTriangle(ZPoint zpoint[3]) = 0;
	// A triangle lit per corner (static meshes): Shade is the light at
	// each corner, 0 to 1. Without its own, a renderer draws it flat.
	virtual void ZBufShadedTriangle(ZPoint zpoint[3], const float shade[3]) { (void)shade; ZBufTriangle(zpoint); }
	// The same with a color per corner (RGBA, red in the low byte): painted
	// landscapes.
	virtual void ZBufColoredTriangle(ZPoint zpoint[3], const float shade[3], const unsigned int color[3]) { (void)color; ZBufShadedTriangle(zpoint, shade); }
	// A landscape's triangle with where its corners are on the landscape's
	// grid in the points' s and t (0 to 1 across it), for a renderer that
	// draws the landscape textured; colored (color is null when the
	// landscape has no paint) otherwise.
	virtual void ZBufLandscapeTriangle(ZPoint zpoint[3], const float shade[3], const unsigned int color[3]) {
		if (color) ZBufColoredTriangle(zpoint, shade, color); else ZBufShadedTriangle(zpoint, shade);
	}
	virtual void ZBufRender(void) = 0;
	// Instanced meshes (foliage). Before an object's copies View gives the
	// light's and the sky's directions in view space and the projection's
	// width (x and z are scaled by winWidth / y); then for each copy of a
	// mesh its object-to-view matrix. A renderer that draws copies on the
	// GPU returns true; otherwise View draws the copy's triangles.
	virtual void ZBufInstanceLight(const Vector3d &light, const Vector3d &up, float winWidth) { (void)light; (void)up; (void)winWidth; }
	virtual bool ZBufMeshInstance(const StaticMesh &mesh, const Matrix44 &toView) { (void)mesh; (void)toView; return false; }
	// Native materials can retain view-space positions for GPU clipping/shading.
	virtual bool ZBufMaterialMesh(const Object &object, const Matrix44 &toView) { (void)object; (void)toView; return false; }

	void ZBufTextured(bool textured) { zbufTextured = textured; }
	bool ZBufSetup(Object *obj, FaceTex *faceTex, Vector3d norm);
	void ZBufCalcST(float x, float y, float z, float &s, float &t);

	// Writes ANSI text to the log (LogMapCore), which the editor shows in its
	// output pane.
	static void OutputText(const char *text, ...);

	char *GetDriver(void) { return driver; }

	// Config stuff.
	static LConfig *cfg;
	static bool drawGridStep1, drawGridStep2;
	static int gridStep1, gridStep2;
	static float textureGamma;

protected:

	int numBufs;
	int useBuf;
	float *pZoomVal;
	int width, height;
	int orgX, orgY;

	int bits;
	palette_t pal[256];
	palette_t setpal[256];
	bool fixedPal;

	bool useZBuf;
	bool zbufTextured;
	Texture *zbufTex;
	unsigned short zbufColor;
	int axisAlign;
	Object *zbufObject = NULL; // what ZBufSetup was last given (Genesis)
	float soffs, toffs;
	float rot;
	float sscale, tscale;

	int zTexes;
	TexTri *texTri;

	int renderMode;

	char driver[16];

private:
	struct GridRect {
		int left, top, right, bottom;
	};

	void GridLines(GridRect rect, float xorg, float yorg, float diff);
};

#endif
