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

// ========== FWndDraw ==========

#ifdef USE_DRAWDLL
static FWndDraw *
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

	FWndDraw::OutputText("Loading %s Genesis Editor driver... ", driver);

	if(!drawInst)
	{
		FWndDraw::OutputText("Error.\nCouldn't load %s\n", dllName);
		return NULL;
	}

	FWndDraw *(*drawProc)(CWnd *wnd) = (FWndDraw *(*)(CWnd *wnd))GetProcAddress(drawInst, "NewDraw");
	if(!drawProc)
	{
		FWndDraw::OutputText("error.\nBad DLL: %s\n", dllName);
		return NULL;
	}
	FWndDraw::OutputText("OK.\n");
	return drawProc(wnd);
}
#endif

FWndDraw *
FWndDraw::New(char *driver, int bits, CWnd *wnd, int _numBufs) {
	FWndDraw *pDraw;

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
