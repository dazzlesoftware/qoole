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

#if !defined(AFX_CONFSHT_H__1A2006B3_6D67_11D2_BA4C_004005310168__INCLUDED_)
#define AFX_CONFSHT_H__1A2006B3_6D67_11D2_BA4C_004005310168__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000
// ConfSht.h : header file
//

#include "ConfGen.h"
#include "Conf2DV.h"
#include "Conf3DV.h"
#include "ConfGms.h"
#include "ConfDrw.h"

/////////////////////////////////////////////////////////////////////////////
// FConfigSheet

class FConfigSheet : public CPropertySheet {
	DECLARE_DYNAMIC(FConfigSheet)

// Construction
public:
	FConfigSheet(LPCTSTR pszCaption, CWnd* pParentWnd = NULL, UINT iSelectPage = 0);

// Attributes
public:
	FConfGen			m_GeneralPage;
	FConfig2DViews		m_2DViewsPage;
	FConfig3DView		m_3DViewPage;
	FConfigGames		m_GamesPage;
	FConfigDraw			m_DrawPage;

// Operations
public:

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(FConfigSheet)
	//}}AFX_VIRTUAL

// Implementation
public:
	virtual ~FConfigSheet();

	// Generated message map functions
protected:
	//{{AFX_MSG(FConfigSheet)
		// NOTE - the ClassWizard will add and remove member functions here.
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_CONFSHT_H__1A2006B3_6D67_11D2_BA4C_004005310168__INCLUDED_)
