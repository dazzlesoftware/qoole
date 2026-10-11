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

// Conf3DV.cpp : implementation file
//

#include "stdafx.h"
#include "GenesisEditor.h"
#include "MapView.h"
#include "Conf3DV.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

//======================================================================
// FConfig3DView property page
//======================================================================

IMPLEMENT_DYNCREATE(FConfig3DView, CPropertyPage)

FConfig3DView::FConfig3DView()
			  :CPropertyPage(FConfig3DView::IDD) {
	//{{AFX_DATA_INIT(FConfig3DView)
	m_bInvLMouse = FALSE;
	m_iWalkSpeed = 0;
	m_iSensitivity = 0;
	//}}AFX_DATA_INIT
}

FConfig3DView::~FConfig3DView() {
}

void FConfig3DView::DoDataExchange(CDataExchange* pDX) {
	CPropertyPage::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(FConfig3DView)
	DDX_Control(pDX, IDC_CONF3D_FLYDOWN, m_kFlyDown);
	DDX_Control(pDX, IDC_CONF3D_FLYUP, m_kFlyUp);
	DDX_Control(pDX, IDC_CONF3D_RIGHT, m_kStepRight);
	DDX_Control(pDX, IDC_CONF3D_LEFT, m_kStepLeft);
	DDX_Control(pDX, IDC_CONF3D_BACKWARD, m_kBackward);
	DDX_Control(pDX, IDC_CONF3D_FORWARD, m_kForward);
	DDX_Check(pDX, IDC_CONF3D_INVMOUSE, m_bInvLMouse);
	DDX_Text(pDX, IDC_CONF3D_WALKSPEED, m_iWalkSpeed);
	DDV_MinMaxUInt(pDX, m_iWalkSpeed, 0, 32);
	DDX_Text(pDX, IDC_CONF3D_SENSITIVITY, m_iSensitivity);
	DDV_MinMaxUInt(pDX, m_iSensitivity, 1, 50);
	//}}AFX_DATA_MAP
	BOOL spin = !m_bInvRMouse;
	DDX_Check(pDX, IDC_CONF3D_RMSPINBALL, spin);
	DDX_Check(pDX, IDC_CONF3D_RMORBIT, m_bInvRMouse);
}


BEGIN_MESSAGE_MAP(FConfig3DView, CPropertyPage)
	//{{AFX_MSG_MAP(FConfig3DView)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// FConfig3DView message handlers

BOOL FConfig3DView::OnInitDialog() {
	CPropertyPage::OnInitDialog();

	((CHotKeyCtrl *) GetDlgItem(IDC_CONF3D_FORWARD))->SetRules(HKCOMB_NONE, 0);
	((CHotKeyCtrl *) GetDlgItem(IDC_CONF3D_BACKWARD))->SetRules(HKCOMB_NONE, 0);
	((CHotKeyCtrl *) GetDlgItem(IDC_CONF3D_LEFT))->SetRules(HKCOMB_NONE, 0);
	((CHotKeyCtrl *) GetDlgItem(IDC_CONF3D_RIGHT))->SetRules(HKCOMB_NONE, 0);
	((CHotKeyCtrl *) GetDlgItem(IDC_CONF3D_FLYUP))->SetRules(HKCOMB_NONE, 0);
	((CHotKeyCtrl *) GetDlgItem(IDC_CONF3D_FLYDOWN))->SetRules(HKCOMB_NONE, 0);

	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

BOOL FConfig3DView::OnSetActive() {
	m_bInvLMouse = FMapView::invLMouse;
	m_bInvRMouse = FMapView::invRMouse;
	m_iWalkSpeed = FMapView::walkSpeed;
	m_iSensitivity = FMapView::sensitivity;

	UpdateData(FALSE);

	((CHotKeyCtrl *) GetDlgItem(IDC_CONF3D_FORWARD))->SetHotKey(FMapView::keyForward, 0);
	((CHotKeyCtrl *) GetDlgItem(IDC_CONF3D_BACKWARD))->SetHotKey(FMapView::keyBackward, 0);
	((CHotKeyCtrl *) GetDlgItem(IDC_CONF3D_LEFT))->SetHotKey(FMapView::keyLeft, 0);
	((CHotKeyCtrl *) GetDlgItem(IDC_CONF3D_RIGHT))->SetHotKey(FMapView::keyRight, 0);
	((CHotKeyCtrl *) GetDlgItem(IDC_CONF3D_FLYUP))->SetHotKey(FMapView::keyUp, 0);
	((CHotKeyCtrl *) GetDlgItem(IDC_CONF3D_FLYDOWN))->SetHotKey(FMapView::keyDown, 0);

	return CPropertyPage::OnSetActive();
}

BOOL FConfig3DView::OnKillActive() {
	if (!CPropertyPage::OnKillActive())
		return FALSE;

	FMapView::invLMouse = (m_bInvLMouse ? true : false);
	FMapView::invRMouse = (m_bInvRMouse ? true : false);
	FMapView::walkSpeed = m_iWalkSpeed;
	FMapView::sensitivity = m_iSensitivity;

	WORD modifier;

	((CHotKeyCtrl *) GetDlgItem(IDC_CONF3D_FORWARD))->GetHotKey(FMapView::keyForward, modifier);
	((CHotKeyCtrl *) GetDlgItem(IDC_CONF3D_BACKWARD))->GetHotKey(FMapView::keyBackward, modifier);
	((CHotKeyCtrl *) GetDlgItem(IDC_CONF3D_LEFT))->GetHotKey(FMapView::keyLeft, modifier);
	((CHotKeyCtrl *) GetDlgItem(IDC_CONF3D_RIGHT))->GetHotKey(FMapView::keyRight, modifier);
	((CHotKeyCtrl *) GetDlgItem(IDC_CONF3D_FLYUP))->GetHotKey(FMapView::keyUp, modifier);
	((CHotKeyCtrl *) GetDlgItem(IDC_CONF3D_FLYDOWN))->GetHotKey(FMapView::keyDown, modifier);

	return TRUE;
}

void FConfig3DView::OnOK() {
	// TODO: Add your specialized code here and/or call the base class
	
	CPropertyPage::OnOK();
}

BOOL FConfig3DView::OnApply() {
	// TODO: Add your specialized code here and/or call the base class
	
	return CPropertyPage::OnApply();
}
