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

// ========== FDrawDirectX ==========

FDrawDirectX::FDrawDirectX(CWnd *wnd) : FDraw(wnd) { }
FDrawDirectX::~FDrawDirectX() { }
void FDrawDirectX::Size(int cx, int cy) { }
void FDrawDirectX::Clear(void) { }
void FDrawDirectX::Color(int color) { }
void FDrawDirectX::Begin(void) { }
void FDrawDirectX::Line(int x1, int y1, int x2, int y2) { }
void FDrawDirectX::Paint(CPaintDC *pPaint) { }
void FDrawDirectX::CopyBuf(int bufNum) { }
void FDrawDirectX::RealizePal(void) { }
void FDrawDirectX::ZBufInit(void) { }
void FDrawDirectX::ZBufClear(void) { }
void FDrawDirectX::ZBufTriangle(ZPoint zpoint[3]) { }
void FDrawDirectX::ZBufRender(void) { }
