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

// FConfGen.cpp : implementation file
//

#include "stdafx.h"
#include "GenesisEditor.h"
#include "MainFrm.h"
#include "ConfGen.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// FConfGen property page

IMPLEMENT_DYNCREATE(FConfGen, CPropertyPage)

FConfGen::FConfGen()
		 : CPropertyPage(FConfGen::IDD) {
	//{{AFX_DATA_INIT(FConfGen)
	m_bHollowPrompt = FALSE;
	m_iHollowWidth = 0;
	m_iUndoLevels = 0;
	m_bGroupPrompt = FALSE;
	//}}AFX_DATA_INIT
}

FConfGen::~FConfGen() {
}

void FConfGen::DoDataExchange(CDataExchange* pDX) {
	CPropertyPage::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(FConfGen)
	DDX_Check(pDX, IDC_CONFGEN_HOLLOWPROMPT, m_bHollowPrompt);
	DDX_Text(pDX, IDC_CONFGEN_HOLLOWWIDTH, m_iHollowWidth);
	DDV_MinMaxInt(pDX, m_iHollowWidth, 4, 128);
	DDX_Text(pDX, IDC_CONFGEN_UNDOLEVELS, m_iUndoLevels);
	DDV_MinMaxInt(pDX, m_iUndoLevels, 1, 100);
	DDX_Check(pDX, IDC_CONFGEN_GROUPNAME, m_bGroupPrompt);
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(FConfGen, CPropertyPage)
	//{{AFX_MSG_MAP(FConfGen)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

//================================================================
// FConfGen message handlers
//================================================================

BOOL FConfGen::OnInitDialog() {
	CPropertyPage::OnInitDialog();

	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

BOOL FConfGen::OnSetActive() {
	m_iUndoLevels = GMainFrame->GetUndoLevels();
	m_bHollowPrompt = (FMainFrame::promptHollow != 0);
	m_iHollowWidth = FMainFrame::hollowThickness;
	m_bGroupPrompt = (FMainFrame::promptGroupName != 0);

	UpdateData(FALSE);

	return CPropertyPage::OnSetActive();
}


BOOL FConfGen::OnKillActive() {
	if (!CPropertyPage::OnKillActive())
		return FALSE;
	
	GMainFrame->SetUndoLevels(m_iUndoLevels);
	FMainFrame::promptHollow = (m_bHollowPrompt ? 1 : 0);
	FMainFrame::hollowThickness = m_iHollowWidth;
	FMainFrame::promptGroupName = (m_bGroupPrompt ? 1 : 0);

	return TRUE;
}
