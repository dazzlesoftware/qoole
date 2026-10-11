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

#if !defined(AFX_PROPSHEET_H__E0B0EDFB_5719_11D2_BA3A_004005310168__INCLUDED_)
#define AFX_PROPSHEET_H__E0B0EDFB_5719_11D2_BA3A_004005310168__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000
// FPropSheet.h : header file
//

#include "PropMsc.h"
#include "PropEnt.h"
#include "PropTex.h"
#include "PropSrf.h"
#include "PropCnt.h"

#define PROPPAGE_ADDSURFACE		0x0001
#define PROPPAGE_ADDCONTENT		0x0002

//======================================================================
// FPropSheet
//======================================================================

class FPropSheet : public CPropertySheet {
	DECLARE_DYNAMIC(FPropSheet)

// Construction
public:
	FPropSheet(CWnd *pParentWnd = NULL, UINT iSelectPage =0, CRect *pRect = NULL);

// Attributes
public:
	// Standard pages.
	FPropPageFile		m_FilePage;
	FPropPageObject		m_ObjectPage;
	FPropPageEntity		m_EntityPage;
	FPropPageTexture	m_TexturePage;
	// Game Specific pages.
	FPropPageSurface	m_SurfacePage;
	FPropPageContent	m_ContentPage;

// Operations
public:
	void RegisterDocument(FEditorDoc *pOldDoc, FEditorDoc *pNewDoc);

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(FPropSheet)
	//}}AFX_VIRTUAL

// Implementation
public:
	virtual ~FPropSheet();

	// Generated message map functions
protected:
	//{{AFX_MSG(FPropSheet)
	afx_msg void OnNcDestroy();
	afx_msg void OnKillFocus(CWnd* pNewWnd);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};


//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_PROPSHEET_H__E0B0EDFB_5719_11D2_BA3A_004005310168__INCLUDED_)
