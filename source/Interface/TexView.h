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

#if !defined(AFX_TEXVIEW_H__7F2A06CB_4E33_11D2_A047_00400543C1CD__INCLUDED_)
#define AFX_TEXVIEW_H__7F2A06CB_4E33_11D2_A047_00400543C1CD__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000
// FTexView.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// FTexView view

#include "Texture.h"
#include "Draw.h"
#include "list.h"

class FTexWnd;

class FTexCell {
public:
	FTexCell(Texture *texture);
	~FTexCell(void);

	void PreDraw(CDC *pDC);
	void Draw(CDC *pDC);
	void Draw(CDC *pDC, bool isSelected, bool isFocused);

	int num;
	Texture *texture;
	CRect rect;
	int width, height;
	int scale;
	int fixed;
	bool show;
	bool selected;

	int fontHeight;

	CDC *dc;

	int tableX, tableY;
	int posX, posY;
};


#define MAX_DC	128

class FTexWnd : public CWnd {
// Construction
public:
	FTexWnd();

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(FTexWnd)
	//}}AFX_VIRTUAL

// Implementation
public:
	virtual ~FTexWnd();

	void SetTexList(TexList *texList);
	void SetCellSize(int size);
	Texture *GetSelTexture(void);

	CDC *dc[MAX_DC];
	CBitmap *bmp[MAX_DC];

	int cellWidth, cellHeight;
	int cellTexSize, oldCellTexSize;

// Generated message map functions
protected:
	//{{AFX_MSG(FTexWnd)
	afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnPaint();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnSetFocus(CWnd* pOldWnd);
	afx_msg void OnKillFocus(CWnd* pNewWnd);
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	//}}AFX_MSG
	afx_msg void OnTextureApply(void);
	DECLARE_MESSAGE_MAP()

private:
	TexList *texList;

	int numCells;
	FTexCell **texCells;
	FTexCell *selCell;

	int winWidth, winHeight;
	int winColumns, winRows;
	int totalRows;
	int posTable[1024];
	int dcCellX[MAX_DC];
	int dcCellY[MAX_DC];

	CFont font;

	bool isFocused;

	void CalcTexCells(bool redraw);
	void UpdateTexCells(void);
	void CalcScrollBar(void);
	void SelectTexture(CPoint point);
};


/////////////////////////////////////////////////////////////////////////////
// FTexCur window

class FTexCur : public CWnd, public FDocView {
// Construction
public:
	FTexCur();

// Attributes
public:

// Operations
public:

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(FTexCur)
	//}}AFX_VIRTUAL

// Implementation
public:
	virtual ~FTexCur();

	void OnUpdate(LPARAM lHint, Object *pScope);
	void SetTexture(Texture *texture);

	// Generated message map functions
protected:
	//{{AFX_MSG(FTexCur)
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnPaint();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

private:
	int width, height;

	CFont font;

	FTexCell *texCell;
	Texture *texture;
	CDC *dc;
	CBitmap *bmp;
};

/////////////////////////////////////////////////////////////////////////////


class FTexView : public CWnd {
public:
	FTexView();           // protected constructor used by dynamic creation
	DECLARE_DYNCREATE(FTexView)

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(FTexView)
	protected:
	//}}AFX_VIRTUAL

// Implementation
public:
	virtual ~FTexView();

	void OnNewContents(void);
	Texture *GetSelTexture(void) { return texWnd.GetSelTexture(); }

	// Generated message map functions
	//{{AFX_MSG(FTexView)
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
	//}}AFX_MSG
	afx_msg void OnSelChange(void);
	afx_msg void OnCloseUp(void);
	afx_msg void OnSelChange2(void);
	afx_msg void OnCloseUp2(void);
	DECLARE_MESSAGE_MAP()

private:
	void BuildComboBox(void);

	FTexCur texCur;
	CComboBox texListBox;
	CComboBox texSizeBox;
	FTexWnd texWnd;

	bool buildLater;

	char texFiles[64][MAX_PATH];
};


/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_TEXVIEW_H__7F2A06CB_4E33_11D2_A047_00400543C1CD__INCLUDED_)
