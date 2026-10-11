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

// FTexRep.cpp : implementation file
//

#include "stdafx.h"
#include "GenesisEditor.h"
#include "MainFrm.h"
#include "TexRep.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// FTexRep dialog


FTexRep::FTexRep(CWnd* pParent /*=NULL*/)
	: CDialog(FTexRep::IDD, pParent)
{
	//{{AFX_DATA_INIT(FTexRep)
	//}}AFX_DATA_INIT
}


void FTexRep::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(FTexRep)
	DDX_Control(pDX, IDC_TEXREP_OLD, m_Old);
	DDX_Control(pDX, IDC_TEXREP_NEW, m_New);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(FTexRep, CDialog)
	//{{AFX_MSG_MAP(FTexRep)
	ON_BN_CLICKED(IDC_TEXREP_APPLY, OnApply)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// FTexRep message handlers

char FTexRep::oldTex[64];
char FTexRep::newTex[64];

void FTexRep::OnApply() {
	m_Old.GetWindowText(oldTex, 63);
	m_New.GetWindowText(newTex, 63);

	Object *pObj = &GMainFrame->GetScope();
	pObj->PreOrderApply(FTexRep::Apply);
}

bool FTexRep::Apply(Object &obj) {
	if(!obj.HasBrush())
		return true;

	Geometry *pBrush = &(obj.GetBrush());
	for(int i = 0; i < pBrush->GetNumFaces(); i++) {
		FaceTex *faceTex = pBrush->GetFaceTexturePtr(i);
		if(!strcmpi(faceTex->GetTName(), oldTex)) {
			faceTex->SetTName(newTex);
		}
	}

	return true;
}
