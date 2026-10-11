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

// qpropmsc.cpp : implementation file
//

#include "stdafx.h"
#include "GenesisEditor.h"
#include "MainFrm.h"
#include "PropMsc.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

//======================================================================
// FPropPageFile property page
//======================================================================

IMPLEMENT_DYNCREATE(FPropPageFile, FPropPage)

FPropPageFile::FPropPageFile() : FPropPage(FPropPageFile::IDD) {
	//{{AFX_DATA_INIT(FPropPageFile)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT
}

FPropPageFile::~FPropPageFile() {
}

void FPropPageFile::DoDataExchange(CDataExchange* pDX) {
	FPropPage::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(FPropPageFile)
		// NOTE: the ClassWizard will add DDX and DDV calls here
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(FPropPageFile, FPropPage)
	//{{AFX_MSG_MAP(FPropPageFile)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// FPropPageFile message handlers

BOOL FPropPageFile::OnInitDialog() {
	FPropPage::OnInitDialog();
	
	// TODO: Add extra initialization here
	
	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

void FPropPageFile::OnUpdate(LPARAM lHint, Object *pScope) {
	if (lHint & DUAV_NOPROPPAGES || !active)
		return;

	FEditorDoc *pDoc = GMainFrame->GetDeskTopDocument();
	Game *pGame = pDoc->GetGame();

	// Filename.
	SetDlgItemText(IDC_PPF_FILEFILENAME, pDoc->GetPathName());

	// Last modified.
	char shortPath[256], textBuf[256];
	::GetShortPathName(pDoc->GetPathName(), shortPath, 256);
	strcpy(textBuf, LFile::GetModifyTime(shortPath));
	textBuf[strlen(textBuf) - 1] = '\0';
	SetDlgItemText(IDC_PPF_LASTMODIFIED, textBuf);

	// Game.
	SetDlgItemText(IDC_PPF_GAMETYPE, pGame->GetName());
}

//======================================================================
// FPropPageObject property page
//======================================================================

IMPLEMENT_DYNCREATE(FPropPageObject, FPropPage)

FPropPageObject::FPropPageObject() : FPropPage(FPropPageObject::IDD) {
	//{{AFX_DATA_INIT(FPropPageObject)
	//}}AFX_DATA_INIT
}

FPropPageObject::~FPropPageObject() {
}

void FPropPageObject::DoDataExchange(CDataExchange* pDX) {
	FPropPage::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(FPropPageObject)
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(FPropPageObject, FPropPage)
	//{{AFX_MSG_MAP(FPropPageObject)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

////////////////////////////////////////
// FPropPageObject message handlers

BOOL FPropPageObject::OnInitDialog() {
	FPropPage::OnInitDialog();
	

	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

void FPropPageObject::OnUpdate(LPARAM lHint, Object *pScope) {
	if (lHint & DUAV_NOPROPPAGES || !active)
		return;

	Selector *pSlctr = &(GMainFrame->GetSelector());
	Object *pEditScope = pSlctr->GetScopePtr();
	if (pScope != NULL && pEditScope != pScope &&
		!pScope->IsMyAncestor(*pEditScope)) {
		return;
	}

	char textBuf[64];

	if (pScope == NULL || (lHint & (DUAV_OBJSSEL | DUAV_SCOPECHANGED))) {
		int num = pSlctr->GetNumMSelectedObjects();
		sprintf(textBuf, "%d", num);
		ctlObjCount().SetWindowText(textBuf);
	}

	if (pScope == NULL || (lHint & (DUAV_OBJSMODSTRUCT | DUAV_SCOPECHANGED))) {
		IterLinkList<ObjectPtr> iter(pSlctr->GetMSelectedObjects());
		iter.Reset();
		int brushCount, entCount;
		brushCount = entCount = 0;
		Object *pSelObj;
		while (!iter.IsDone()) {
			pSelObj = iter.GetNext()->GetPtr();
			brushCount += pSelObj->CountBrushes();
			entCount += pSelObj->CountEntities();
		}
		sprintf(textBuf, "%d", entCount);
		ctlEntCount().SetWindowText(textBuf);
		sprintf(textBuf, "%d", brushCount);
		ctlBrushCount().SetWindowText(textBuf);
	}

	if (pScope == NULL ||
		(lHint & (DUAV_OBJSMODATTRIB | DUAV_SCOPECHANGED | DUAV_OBJSSEL))) {
		Vector3d pVec;
		pSlctr->GetMSelectedObjectsCenter(pVec);
		sprintf(textBuf, "(%.0f, %.0f, %.0f)", pVec.GetX(), pVec.GetY(), pVec.GetZ());
		ctlObjPos().SetWindowText(textBuf);

		IterLinkList<ObjectPtr> iter(pSlctr->GetMSelectedObjects());
		iter.Reset();
		Object *pSelObj = NULL;
		Vector3d minVec, maxVec, tMinVec, tMaxVec;

		if (!iter.IsDone()) {
			pSelObj = iter.GetNext()->GetPtr();
			pVec = pSelObj->GetPosition();
			pSelObj->GetBoundingVectors(tMinVec, tMaxVec);
			tMinVec.AddVector(pVec);
			tMaxVec.AddVector(pVec);
		}

		while (!iter.IsDone()) {
			pSelObj = iter.GetNext()->GetPtr();
			pVec = pSelObj->GetPosition();
			pSelObj->GetBoundingVectors(minVec, maxVec);
			minVec.AddVector(pVec);
			maxVec.AddVector(pVec);
			tMinVec.NewVector(Min(minVec.GetX(), tMinVec.GetX()),
							  Min(minVec.GetY(), tMinVec.GetY()),
							  Min(minVec.GetZ(), tMinVec.GetZ()));
			tMaxVec.NewVector(Max(maxVec.GetX(), tMaxVec.GetX()),
							  Max(maxVec.GetY(), tMaxVec.GetY()),
							  Max(maxVec.GetZ(), tMaxVec.GetZ()));
		}
		tMaxVec.SubVector(tMinVec);
		sprintf(textBuf, "(%.0f, %.0f, %.0f)\n",
				tMaxVec.GetX(), tMaxVec.GetY(), tMaxVec.GetZ());
		ctlObjDimen().SetWindowText(textBuf);
	}

	UpdateData(FALSE);
}
