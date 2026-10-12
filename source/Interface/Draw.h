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


#ifndef __DRAW_H
#define __DRAW_H

#include "stdafx.h"
#include "DrawBase.h"
#include "resource.h"

// ============================
// define to use GenesisDrawXXX.dll's
#ifndef IS_DRAWDLL
#define USE_DRAWDLL
#endif

// ========== FWndDraw ==========

// A renderer drawing into an MFC window. The map core only sees the FDraw
// base; the window and the paint call live here.
class FWndDraw : public FDraw {
public:
	FWndDraw(CWnd *wnd);

	static FWndDraw *New(char *driver, int bits, CWnd *pWnd, int numBufs = 1);

	virtual void Paint(CPaintDC *pPaint) = 0;

protected:
	CWnd *pWnd;
};

#include "DrawMFC.h"
#include "DrawOGL.h"

#endif
