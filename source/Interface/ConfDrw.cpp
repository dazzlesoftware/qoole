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

// FConfigDraw.cpp : implementation file
//

#include "stdafx.h"
#include "GenesisEditor.h"
#include "MapView.h"
#include "ConfDrw.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// FConfigDraw property page

IMPLEMENT_DYNCREATE(FConfigDraw, CPropertyPage)

FConfigDraw::FConfigDraw() : CPropertyPage(FConfigDraw::IDD)
{
	//{{AFX_DATA_INIT(FConfigDraw)
	m_draw2dWire = _T("");
	m_draw3dWire = _T("");
	m_draw3dSolid = _T("");
	m_draw3dTex = _T("");
	//}}AFX_DATA_INIT
}

FConfigDraw::~FConfigDraw()
{
}

void FConfigDraw::DoDataExchange(CDataExchange* pDX)
{
	CPropertyPage::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(FConfigDraw)
	DDX_CBString(pDX, IDC_CONFDRAW_2DWIRE, m_draw2dWire);
	DDX_CBString(pDX, IDC_CONFDRAW_3DWIRE, m_draw3dWire);
	DDX_CBString(pDX, IDC_CONFDRAW_3DSOLID, m_draw3dSolid);
	DDX_CBString(pDX, IDC_CONFDRAW_3DTEX, m_draw3dTex);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(FConfigDraw, CPropertyPage)
	//{{AFX_MSG_MAP(FConfigDraw)
	ON_CBN_SELCHANGE(IDC_CONFDRAW_2DWIRE, OnSelchangeConfdraw2dwire)
	ON_CBN_SELCHANGE(IDC_CONFDRAW_3DSOLID, OnSelchangeConfdraw3dsolid)
	ON_CBN_SELCHANGE(IDC_CONFDRAW_3DTEX, OnSelchangeConfdraw3dtex)
	ON_CBN_SELCHANGE(IDC_CONFDRAW_3DWIRE, OnSelchangeConfdraw3dwire)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// FConfigDraw message handlers

BOOL FConfigDraw::OnInitDialog() {
	CPropertyPage::OnInitDialog();
	
	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

BOOL FConfigDraw::OnSetActive() {

	m_draw2dWire = FMapView::draw2dWire;
	m_draw3dWire = FMapView::draw3dWire;
	m_draw3dSolid = FMapView::draw3dSolid;
	m_draw3dTex = FMapView::draw3dTex;

	UpdateData(FALSE);

	return CPropertyPage::OnSetActive();
}

BOOL FConfigDraw::OnKillActive() {
	if(!CPropertyPage::OnKillActive())
		return FALSE;

	strcpy(FMapView::draw2dWire, m_draw2dWire.GetBuffer(8));
	strcpy(FMapView::draw3dWire, m_draw3dWire.GetBuffer(8));
	strcpy(FMapView::draw3dSolid, m_draw3dSolid.GetBuffer(8));
	strcpy(FMapView::draw3dTex, m_draw3dTex.GetBuffer(8));

	FDraw::ValidateDriver(FMapView::draw2dWire, "MFC");
	FDraw::ValidateDriver(FMapView::draw3dWire, "MFC");
	FDraw::ValidateDriver(FMapView::draw3dSolid, "DX12");
	FDraw::ValidateDriver(FMapView::draw3dTex, "DX12");

	FMapView::RefreshAll();

	return TRUE;
}

void FConfigDraw::OnSelchangeConfdraw2dwire() {
	SetModified();
}

void FConfigDraw::OnSelchangeConfdraw3dsolid() {
	SetModified();
}

void FConfigDraw::OnSelchangeConfdraw3dtex() {
	SetModified();
}

void FConfigDraw::OnSelchangeConfdraw3dwire() {
	SetModified();
}

BOOL FConfigDraw::OnApply() {
	FMapView::RefreshAll();
	return CPropertyPage::OnApply();
}
