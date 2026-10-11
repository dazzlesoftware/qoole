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

#if !defined(AFX_WSBAR_H__E4A49311_E133_11D1_9F42_0040C757D406__INCLUDED_)
#define AFX_WSBAR_H__E4A49311_E133_11D1_9F42_0040C757D406__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000

#include "TrView.h"
#include "TexView.h"
#include "PrefMgr.h"

//======================================================================
// FTabCtrl
//======================================================================

class FTabCtrl : public CTabCtrl {
// Construction
public:
	FTabCtrl();

// Operations
public:
	void RegisterTabLabels(const char **pLabels) { pTabLabels = pLabels; }

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(FTabCtrl)
	protected:
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
	//}}AFX_VIRTUAL

// Implementation
public:
	virtual ~FTabCtrl();

	// Generated message map functions
protected:
	//{{AFX_MSG(FTabCtrl)
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	//}}AFX_MSG

	// !!!WARNING!!!!
	// strbuf needs to have at least room for strlen + 4 chars.
	void SqueezeStr(int displayWidth, int strLen, LPSTR strBuf);
	void ResizeLabels(int displayWidth);

	CRect displayArea;
	const char **pTabLabels;

	DECLARE_MESSAGE_MAP()
};

//======================================================================
// FWorkSpaceBar window
//======================================================================

#define TAB_TREEVIEW		0
#define TAB_TEXTUREVIEW		1
#define TAB_PREFABVIEW		2

class FWorkSpaceBar : public CMRCSizeControlBar {
public:
	FWorkSpaceBar();
	virtual ~FWorkSpaceBar();

	FTreeView *GetTreeView(void) { return pTreeView; };
	FTexView *GetTextureView(void) { return pTextureView; };
	FPrefabMgr *GetPrefabView(void) { return pPrefabView; };

	CWnd *GetTabWindow(int nTab = -1);
    void ShowSelTabWindow(int nSel = -1);

	// CTabCtrl m_TabCtrl;
	FTabCtrl m_TabCtrl;
	CImageList m_ImgList;

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(FWorkSpaceBar)
	protected:
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
	//}}AFX_VIRTUAL

protected:
	FTreeView *pTreeView;
	FTexView *pTextureView;
	FPrefabMgr *pPrefabView;
	
	// Generated message map functions
protected:
	//{{AFX_MSG(FWorkSpaceBar)
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnDestroy();
	afx_msg void OnTabChanged(NMHDR * pNotifyStruct, LRESULT* result);
	afx_msg void OnPaint();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

	virtual void OnSizedOrDocked(int cx, int cy, BOOL bFloating, int flags);
};

//======================================================================
// FOutputBar Window.
//======================================================================

#include "ProcWnd.h"

class FOutputBar : public CMRCSizeControlBar {
public:
	FOutputBar();
	virtual ~FOutputBar();

	FProcessWnd	*GetOutputWnd(void) { return pOutputWnd; };

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(FOutputBar)
	//}}AFX_VIRTUAL

protected:
	virtual void OnSizedOrDocked(int cx, int cy, BOOL bFloating, int flags);

	FProcessWnd *pOutputWnd;

	// Generated message map functions
protected:
	//{{AFX_MSG(FOutputBar)
	afx_msg void OnDestroy();
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_WSBAR_H__E4A49311_E133_11D1_9F42_0040C757D406__INCLUDED_)
