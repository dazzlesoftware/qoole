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

// FSetColor.cpp : implementation file
//

#include "stdafx.h"
#include "GenesisEditor.h"
#include "MainFrm.h"
#include "SetColor.h"
#include "Selector.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// FSetColor dialog


FSetColor::FSetColor(CWnd* pParent /*=NULL*/)
	: CDialog(FSetColor::IDD, pParent)
{
	//{{AFX_DATA_INIT(FSetColor)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT

	inUse = false;

	red = 0;
	grn = 0;
	blu = 0;
	changed = false;

	pScope = NULL;
	pObject = NULL;
}


void FSetColor::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(FSetColor)
	DDX_Control(pDX, IDC_SETCOLOR_COLOR, m_Color);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(FSetColor, CDialog)
	//{{AFX_MSG_MAP(FSetColor)
	ON_WM_HSCROLL()
	ON_WM_PAINT()
	ON_WM_CLOSE()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// FSetColor message handlers

void FSetColor::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar) {
	if(!inUse) {
		CDialog::OnHScroll(nSBCode, nPos, pScrollBar);
		return;
	}

	int lastPos = pScrollBar->GetScrollPos();

	switch(nSBCode) {
	case SB_LEFT:
		pScrollBar->SetScrollPos(0);
		break;
	case SB_LINELEFT:
		pScrollBar->SetScrollPos(lastPos - 1);
		break;
	case SB_LINERIGHT:
		pScrollBar->SetScrollPos(lastPos + 1);
		break;
	case SB_PAGELEFT:
		pScrollBar->SetScrollPos(lastPos - 16);
		break;
	case SB_PAGERIGHT:
		pScrollBar->SetScrollPos(lastPos + 16);
		break;
	case SB_RIGHT:
		pScrollBar->SetScrollPos(255);
		break;
	case SB_THUMBPOSITION:
	case SB_THUMBTRACK:
		pScrollBar->SetScrollPos(nPos);
		break;
	default:
		break;
	}

	CScrollBar *pRedBar = (CScrollBar *)GetDlgItem(IDC_SETCOLOR_RED);
	CScrollBar *pGrnBar = (CScrollBar *)GetDlgItem(IDC_SETCOLOR_GRN);
	CScrollBar *pBluBar = (CScrollBar *)GetDlgItem(IDC_SETCOLOR_BLU);

	red = pRedBar->GetScrollPos();
	grn = pGrnBar->GetScrollPos();
	blu = pBluBar->GetScrollPos();

	changed = true;

	OnRedraw();

	CDialog::OnHScroll(nSBCode, nPos, pScrollBar);
}

void FSetColor::OnUpdate(LPARAM lHint, Object *_pScope) {
	if(!(lHint & (DUAV_OBJSSEL | DUAV_OBJENTMODATTRIB)))
		return;

	inUse = false;
	const char *color = "0.0 0.0 0.0";

	float r, g, b;
	sscanf(color, "%f %f %f", &r, &g, &b);

	red = (int)(r * 255);
	grn = (int)(g * 255);
	blu = (int)(b * 255);

	CScrollBar *pRedBar = (CScrollBar *)GetDlgItem(IDC_SETCOLOR_RED);
	CScrollBar *pGrnBar = (CScrollBar *)GetDlgItem(IDC_SETCOLOR_GRN);
	CScrollBar *pBluBar = (CScrollBar *)GetDlgItem(IDC_SETCOLOR_BLU);

	pRedBar->SetScrollPos(red);
	pGrnBar->SetScrollPos(grn);
	pBluBar->SetScrollPos(blu);
	
	//SetColors();
	//FMapView::SelColors(3, (int)red, (int)grn, (int)blu);

	OnRedraw();
}

void FSetColor::OnRedraw(void) {
	CStatic *pRedVal = (CStatic *)GetDlgItem(IDC_SETCOLOR_REDVAL);
	CStatic *pGrnVal = (CStatic *)GetDlgItem(IDC_SETCOLOR_GRNVAL);
	CStatic *pBluVal = (CStatic *)GetDlgItem(IDC_SETCOLOR_BLUVAL);

	char buf[8];
	sprintf(buf, "%d", red);
	pRedVal->SetWindowText(buf);
	sprintf(buf, "%d", grn);
	pGrnVal->SetWindowText(buf);
	sprintf(buf, "%d", blu);
	pBluVal->SetWindowText(buf);

	RECT rect;
	m_Color.GetWindowRect(&rect);
	ScreenToClient(&rect);
	InvalidateRect(&rect, false);
	UpdateWindow();
}

BOOL FSetColor::OnInitDialog() {
	CDialog::OnInitDialog();
	
	CScrollBar *pRedBar = (CScrollBar *)GetDlgItem(IDC_SETCOLOR_RED);
	CScrollBar *pGrnBar = (CScrollBar *)GetDlgItem(IDC_SETCOLOR_GRN);
	CScrollBar *pBluBar = (CScrollBar *)GetDlgItem(IDC_SETCOLOR_BLU);

	pRedBar->SetScrollRange(0, 255);
	pGrnBar->SetScrollRange(0, 255);
	pBluBar->SetScrollRange(0, 255);

	//OnUpdate(DUAV_OBJSSEL, &GMainFrame->GetScope());

	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

void FSetColor::OnPaint() {
	CPaintDC dc(this); // device context for painting

	CRect rect;
	m_Color.GetWindowRect(&rect);
	ScreenToClient(&rect);
	rect.DeflateRect(1, 1);

	dc.FillSolidRect(&rect, blu << 16 | grn << 8 | red);
}

void FSetColor::SetColors(void) {
	/*FMapView *pMapView = (FMapView *)
	pMapView->qvRender = nID;
	pDraw->SetColor(VRC_SELECT, (int)red, (int)grn, (int)blu);
	if(qvRender == ID_VIEW_RENDER_WIREFRAME) {
		pDraw->RealizePal();
	}
	else {
		char *palName = GetDocument()->GetGame()->GetPalName();
		if(strlen(palName))
			pDraw->LoadPal(palName);
	}

	FEditorDoc *pDoc = GMainFrame->GetDeskTopDocument();
	pDoc->UpdateAllViews(NULL, DUAV_MAPVIEWS | DUAV_SUPDATEALL, NULL);
	//pDoc->UpdateAllViews(NULL, DUAV_OBJSMODATTRIB, NULL);*/
}

void FSetColor::OnClose() {
	//SetColors();
	//FMapView::SelColors(3, (int)red, (int)grn, (int)blu);
	CRect rect;
	GetWindowRect(&rect);
	GMainFrame->OnDestroyColorWnd(rect);
}

void FSetColor::OnOK() {
	//SetColors();
	//FMapView::SelColors(3, (int)red, (int)grn, (int)blu);
	CRect rect;
	GetWindowRect(&rect);
	GMainFrame->OnDestroyColorWnd(rect);
}
