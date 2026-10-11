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

// PropPg.cpp : implementation file
//

#include "stdafx.h"
#include "GenesisEditor.h"
#include "MainFrm.h"
#include "PropPg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

//======================================================================
// FPropPage property page
//======================================================================

IMPLEMENT_DYNCREATE(FPropPage, CPropertyPage)

FPropPage::FPropPage() {
}

FPropPage::FPropPage(UINT nIDTemplate)
		  : CPropertyPage(nIDTemplate) {
	//{{AFX_DATA_INIT(FPropPage)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT

	FEditorDoc *pDoc = GMainFrame->GetDeskTopDocument();
	RegisterDocument(pDoc);
	active = false;
}

FPropPage::~FPropPage() {
}

void FPropPage::DoDataExchange(CDataExchange* pDX) {
	CPropertyPage::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(FPropPage)
		// NOTE: the ClassWizard will add DDX and DDV calls here
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(FPropPage, CPropertyPage)
	//{{AFX_MSG_MAP(FPropPage)
		// NOTE: the ClassWizard will add message map macros here
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// FPropPage message handlers

BOOL FPropPage::OnSetActive() {
	if (!CPropertyPage::OnSetActive())
		return FALSE;

	active = true;
	this->OnUpdate(NULL, NULL);
	return TRUE;
}

BOOL FPropPage::OnKillActive() {
	if (!CPropertyPage::OnKillActive())
		return FALSE;

	active = false;
	return TRUE;
}
