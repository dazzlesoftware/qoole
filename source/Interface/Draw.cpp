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
#include "MainFrm.h"

// ========== FDraw ==========

LConfig *FDraw::cfg = NULL;
bool FDraw::drawGridStep1 = true;
bool FDraw::drawGridStep2 = true;
int FDraw::gridStep1 = 16;
int FDraw::gridStep2 = 4;
float FDraw::textureGamma = 0.7f;

void
FDraw::Init(void) {
	cfg = new LConfig("Draw");
	cfg->RegisterVar("GridStep1", &gridStep1, LVAR_INT, 1, 8192);
	cfg->RegisterVar("GridStep2", &gridStep2, LVAR_INT, 1, 1024);
	cfg->RegisterVar("TextureGamma", &textureGamma, LVAR_FLOAT);
}

void
FDraw::Exit(void) {
	cfg->SaveVars();
	delete cfg;
}

// Renderer plugins that ship with the editor (GenesisDraw<name>.dll).
static const char *knownDrivers[] = { "MFC", "OpenGL", "DX12", "Vulkan" };

bool
FDraw::IsKnownDriver(const char *driver) {
	for(int i = 0; i < (int)(sizeof(knownDrivers) / sizeof(knownDrivers[0])); i++) {
		if(!strcmpi(driver, knownDrivers[i]))
			return true;
	}
	return false;
}

// Settings saved by older versions can name a renderer that no longer
// exists (MGL, Glide). Replace those with defDriver.
void
FDraw::ValidateDriver(char *driver, const char *defDriver) {
	if(!IsKnownDriver(driver))
		strcpy(driver, defDriver);
}

#ifdef USE_DRAWDLL
static FDraw *
LoadDrawDLL(const char *driver, CWnd *wnd) {
	char dllName[MAX_PATH];
#ifdef _DEBUG
	sprintf(dllName, "%s\\GenesisDraw%sD.dll", LFile::GetInitDir(), driver);
#else
	sprintf(dllName, "%s\\GenesisDraw%s.dll", LFile::GetInitDir(), driver);
#endif

	HINSTANCE drawInst = GetModuleHandle(dllName);
	if(!drawInst)
		drawInst = LoadLibrary(dllName);

	FDraw::OutputText("Loading %s Genesis Editor driver... ", driver);

	if(!drawInst)
	{
		FDraw::OutputText("Error.\nCouldn't load %s\n", dllName);
		return NULL;
	}

	FDraw *(*drawProc)(CWnd *wnd) = (FDraw *(*)(CWnd *wnd))GetProcAddress(drawInst, "NewDraw");
	if(!drawProc)
	{
		FDraw::OutputText("error.\nBad DLL: %s\n", dllName);
		return NULL;
	}
	FDraw::OutputText("OK.\n");
	return drawProc(wnd);
}
#endif

FDraw *
FDraw::New(char *driver, int bits, CWnd *wnd, int _numBufs) {
	FDraw *pDraw;

#ifdef USE_DRAWDLL
	pDraw = NULL;
	if(IsKnownDriver(driver))
		pDraw = LoadDrawDLL(driver, wnd);
	else
		OutputText("Unknown Genesis Editor driver %s.\n", driver);

	// Fall back to the MFC (GDI) renderer, which works everywhere.
	if(!pDraw && strcmpi(driver, "MFC")) {
		OutputText("Using the MFC driver instead.\n");
		pDraw = LoadDrawDLL("MFC", wnd);
	}

	if(!pDraw)
		LFatal("Couldn't load a Genesis Editor driver.\n");

#else
	if(!strcmpi(driver, "OpenGL"))
		pDraw = new FDrawOpenGL(wnd);
	else
		pDraw = new FDrawMFC(wnd);
#endif

	strcpy(pDraw->driver, driver);

	pDraw->bits = bits;

	CRect rect;
	wnd->GetWindowRect(&rect);
	pDraw->Size(rect.Width(), rect.Height());

	pDraw->SetColor(252, 100, 100, 100);
	pDraw->SetColor(253, 150, 150, 150);
	pDraw->SetColor(254, 150, 220, 150);

	return pDraw;
}

void
FDraw::RenderPoint(float x, float y, int size) {
	float zoom = *pZoomVal;
	if(zoom == 1.0f)
		Point(ROUNDI(x) + orgX, orgY - ROUNDI(y), size);
	else
		Point(ROUNDI(x * zoom) + orgX, orgY - ROUNDI(y * zoom), size);
}

void
FDraw::RenderLine(float x1, float y1, float x2, float y2) {
	float zoom = *pZoomVal;
	if(zoom == 1.0f)
		Line(ROUNDI(x1) + orgX, orgY - ROUNDI(y1),
		     ROUNDI(x2) + orgX, orgY - ROUNDI(y2));
	else
		Line(ROUNDI(x1 * zoom) + orgX, orgY - ROUNDI(y1 * zoom),
		     ROUNDI(x2 * zoom) + orgX, orgY - ROUNDI(y2 * zoom));
}

void
FDraw::Point(int x, int y, int size) {
	for(int i = -(size / 2); i <= size / 2; i++)
		Line(x - size / 2, y + i, x + size / 2, y + i);
}

void
FDraw::Box(int x1, int y1, int x2, int y2) {
	Line(x1, y1, x2, y1);
	Line(x1, y2, x2, y2);
	Line(x1, y1, x1, y2);
	Line(x2, y1, x2, y2);
}

void
FDraw::Cross(int x1, int y1, int size) {
	int d = size / 2 + 1;
	Line(x1 - d, y1 - d, x1 + d, y1 + d);
	Line(x1 + d, y1 - d, x1 - d, y1 + d);
}

void
FDraw::Grid(float xorg, float yorg) {
	float zoom = *pZoomVal;
	float diff;
	RECT rect;

	rect.left = 0;
	rect.top = 0;
	rect.right = width;
	rect.bottom = height;

	xorg = xorg * zoom + width / 2;
	yorg = -yorg * zoom + height / 2;

	// Draw grid step 1.
	if (drawGridStep1) {
		diff = gridStep1 * zoom;
		if(diff > 2.0f) {
			Color(252);
			GridLines(rect, xorg, yorg, diff);
		}
	}

	// Draw grid step 2.
	if (drawGridStep2) {
		diff = gridStep1 * zoom * gridStep2;
		Color(253);
		GridLines(rect, xorg, yorg, diff);
	}

	// Highlight major axes.
	Color(254);
	Line(ROUNDI(xorg), rect.top, ROUNDI(xorg), rect.bottom);
	Line(rect.left, ROUNDI(yorg), rect.right, ROUNDI(yorg));
}

void
FDraw::GridLines(RECT rect, float xorg, float yorg, float diff) {
	float x, y;
	int r;

	x = xorg;
	y = yorg;
	while(x > rect.left) {
		r = ROUNDI(x);
		x -= diff;
		Line(r, rect.top, r, rect.bottom);
	}
		while(y > rect.top) {
		r = ROUNDI(y);
		y -= diff;
		Line(rect.left, r, rect.right, r);
	}

	x = xorg; y = yorg;
	while(x < rect.right) {
		r = ROUNDI(x);
		x += diff;
		Line(r, rect.top, r, rect.bottom);
	}
	while(y < rect.bottom) {
		r = ROUNDI(y);
		y += diff;
		Line(rect.left, r, rect.right, r);
	}
}

void
FDraw::StippleBox(int x1, int y1, int x2, int y2, int l1, int l2) {
	int x, y;

	if(x1 > x2) {
		x = x1;
		x1 = x2;
		x2 = x;
	}

	if(y1 > y2) {
		y = y1;
		y1 = y2;
		y2 = y;
	}

	x = x1;
	while(x + l1 - 1 < x2) {
		Line(x, y1, x + l1 - 1, y1);
		Line(x, y2, x + l1 - 1, y2);
		x += l1 + l2;
	}
	if(x > x2)
		x -= l2;
	Line(x, y1, x2, y1);
	Line(x, y2, x2, y2);

	y = y1;
	while(y + l1 - 1 < y2) {
		Line(x1, y, x1, y + l1 - 1);
		Line(x2, y, x2, y + l1 - 1);
		y += l1 + l2;
	}
	if(y > y2)
		y -= l2;
	Line(x1, y, x1, y2);
	Line(x2, y, x2, y2);
}

void
FDraw::SetColor(int color, int r, int g, int b) {
	pal[color].red = r;
	pal[color].green = g;
	pal[color].blue = b;
	setpal[color].red = r;
	setpal[color].green = g;
	setpal[color].blue = b;
}

void
FDraw::UseColor(int color) {
	if(!fixedPal) {
		Color(color);
		return;
	}

	int best = color;
	int diff = 256 * 256 * 256;

	int val1 =
		setpal[color].red * setpal[color].red + 
		setpal[color].red * setpal[color].green + 
		setpal[color].red * setpal[color].blue;

	for(int i = 0; i < 256; i++) {
		int val2 =
			pal[i].red * pal[i].red + 
			pal[i].red * pal[i].green + 
			pal[i].red * pal[i].blue;

		if(abs(val1 - val2) < diff) {
			best = i;
			diff = abs(val1 - val2);
		}
	}

	Color(best);
}

void
FDraw::LoadPal(char *name) {
	OutputText("Loading palette... ");
	ASSERT(bits == 8);
	LPalette lpal;
	lpal.Load(name, textureGamma);
	for(int i = 0; i < 256; i++)
		pal[i] = lpal.pal[i];
	RealizePal();
	fixedPal = true;
	OutputText("OK.\n");
}

bool
FDraw::ZBufSetup(Object *obj, FaceTex *faceTex, Vector3d norm) {
	float z, zMax = 0.0f;
	int soffsi, toffsi;

	if(faceTex) {
		faceTex->GetTInfo(soffsi, toffsi, rot, sscale, tscale);
		soffs = (float) soffsi;
		toffs = (float) toffsi;
		zbufTex = faceTex->GetTexture();
		if(zbufTex && zbufTextured) {
			if(!zbufTex->Cache())
				zbufTex = NULL;
		}
	}
	else
		zbufTex = NULL;

	if(sscale == 0.0f)
		sscale = 1.0f;
	if(tscale == 0.0f)
		tscale = 1.0f;

	// Q: Why are we rounding the norm vector?
	// A: Norm Vec gets processed via 2 matrices.
	//    the final norm may have floating precision errors.
	//    Round at 3rd digit.

	// Check East and West
	z = ROUND4(norm.GetX());
	if (z > zMax) {
		zMax = z;
		axisAlign = 4;
	}
	z = ROUND4(-norm.GetX());
	if (z > zMax) {
		zMax = z;
		axisAlign = 5;
	}

	// Check North and South
	z = ROUND4(norm.GetY());
	if (z > zMax) {
		zMax = z;
		axisAlign = 2;
	}
	z = ROUND4(-norm.GetY());
	if (z > zMax) {
		zMax = z;
		axisAlign = 3;
	}

	// Check Up and Down
	z = ROUND4(norm.GetZ());
	if (z > zMax) {
		zMax = z;
		axisAlign = 0;
	}
	z = ROUND4(-norm.GetZ());
	if (z > zMax) {
		zMax = z;
		axisAlign = 1;
	}

	if(zbufTex) {
		zbufColor = zbufTex->GetSolidInfo()[axisAlign];

		if(sscale > -0.05 && sscale < 0.05f)
			sscale = 1.0f;

		if(tscale > -0.05 && tscale < 0.05f)
			tscale = 1.0f;

		// Account for textures that have been scaled internally
		// (textures are scaled internally so their width and height
		// (will always be a power of 2, required by FDraw libraries)

		sscale *= zbufTex->divWidth;
		tscale *= zbufTex->divHeight;

		soffs /= zbufTex->divWidth;
		toffs /= zbufTex->divHeight;
	}
	else
		zbufColor = (short)(((INT_PTR)obj + axisAlign * 10) % 255);

	return (zbufTex && zbufTextured);
}

void
FDraw::ZBufCalcST(float x, float y, float z, float &s, float &t) {
	if(axisAlign < 2) {
		s = x;
		t = y;
	}
	else if(axisAlign < 4) {
		s = x;
		t = z;
	}
	else {
		s = y;
		t = z;
	}

	if(rot != 0.0f) {
		float crot = (float)cos(DEG2RAD(rot));
		float srot = (float)sin(DEG2RAD(rot));
		float ssave = s;
		s = s * crot + t * srot;
		t = t * crot - ssave * srot;
	}

	if(sscale != 1.0f)
		s /= sscale;

	if(tscale != 1.0f)
		t /= tscale;

	s += soffs;
	t -= toffs;

	t = -t;
}

void FDraw::OutputText(const char *text, ...)
{
	va_list arglist;
	char buf[256] = "";
	
	if(text)
	{
		va_start(arglist, text);
		vsprintf(buf, text, arglist);
		va_end(arglist);
	}

	FProcessWnd *pProcWnd = GMainFrame->GetProcessWnd();
	ASSERT(pProcWnd != NULL);

	pProcWnd->OutputText((char *) buf);
	pProcWnd->GetViewPos();
	pProcWnd->SetViewPos();
}