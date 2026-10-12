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

// PrefbVw.cpp : implementation file
//

#include "stdafx.h"
#include "GenesisEditor.h"
#include "PrefbVw.h"
#include "PrefMgr.h"
#include "MainFrm.h"
#include "MapIO.h"
#include "Draw.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// FPrefabView

FPrefabView::FPrefabView() {
	pRootObj = NULL;
	pSlctr = NULL;
	pView = NULL;
	pDraw = NULL;

	mouseLClick = mouseLDrag = false;
	mouseRClick = mouseRDrag = false;
}

FPrefabView::~FPrefabView() {
	ASSERT(pView != NULL);
	delete pView;
	ASSERT(pDraw != NULL);
	delete pDraw;
	ASSERT(pSlctr != NULL);
	delete pSlctr;
	ASSERT(pRootObj != NULL);
	delete pRootObj;
}

BEGIN_MESSAGE_MAP(FPrefabView, CWnd)
	//{{AFX_MSG_MAP(FPrefabView)
	ON_WM_CREATE()
	ON_WM_SIZE()
	ON_WM_PAINT()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_RBUTTONDOWN()
	ON_WM_RBUTTONUP()
	ON_WM_MOUSEMOVE()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// FPrefabView message handlers

BOOL FPrefabView::PreCreateWindow(CREATESTRUCT& cs) {
	if (!CWnd::PreCreateWindow(cs))
		return FALSE;

	// Register the new window class.
	WNDCLASS wndclass;
	HINSTANCE hInst = AfxGetInstanceHandle();

	if (!(::GetClassInfo(hInst, "GenesisPrefabView", &wndclass))) {
		if (::GetClassInfo(hInst, cs.lpszClass, &wndclass)) {
			wndclass.lpszClassName = "GenesisPrefabView";
			wndclass.hbrBackground = NULL;
			wndclass.style &= ~CS_DBLCLKS;
			wndclass.lpszMenuName = NULL;
			wndclass.hbrBackground = NULL;
			if (!AfxRegisterClass(&wndclass))
				AfxThrowResourceException();
		}
		else
			AfxThrowResourceException();
	}

	cs.lpszClass = "GenesisPrefabView";

	return TRUE;
}

int FPrefabView::OnCreate(LPCREATESTRUCT lpCreateStruct) {
	FDraw::OutputText("Creating prefab window... ");
	if (CWnd::OnCreate(lpCreateStruct) == -1)
		return -1;

	// Init some stuff.
	ASSERT(pRootObj == NULL);
	pRootObj = new Object;

	ASSERT(pSlctr == NULL);
	pSlctr = new Selector(*pRootObj);

	ASSERT(pDraw == NULL);
	CreateDraw();

	ASSERT(pView == NULL);
	pView = new View(pDraw, *pRootObj, *pSlctr);

	Vector3d pVec(128.0f, 128.0f, 128.0f);
	SphrVector orientVec;
	orientVec.NewVector(pVec.MultVector(-1.0f), Vector3d::zAxisVec);
	pVec.MultVector(-1.0f);

	pView->SetNewView(*pRootObj, *pSlctr, true, &pVec, &orientVec, true, 1, 1000.0f);

	FDraw::OutputText("OK.\n");
	return 0;
}

void FPrefabView::CreateDraw(void) {
	char *driver;
	int bits = 8;
	static float zoomVal = 1.0f;

	driver = FMapView::draw3dWire;
	pDraw = FWndDraw::New(driver, bits, this, 1);
	ASSERT(pDraw != NULL);
	
	pDraw->SetZoomPtr(&zoomVal);

	// pDraw->ZBufInit();

	// SetColors();
	pDraw->SetColor(VRC_VIEW, 0, 0, 0);
	pDraw->SetColor(VRC_NORMAL, 255, 255, 255);
	pDraw->SetColor(VRC_SELECT, 255, 0, 0);
	pDraw->SetColor(VRC_HOTSELECT, 255, 150, 150);
	pDraw->SetColor(VRC_ENTITY, 120, 150, 255);
	pDraw->SetColor(VRC_MANIPDOT, 255, 255, 0);
	pDraw->SetColor(VRC_FACESELECT, 255, 255, 0);
	pDraw->SetColor(VRC_CROSSHAIR, 24, 100, 244);
	pDraw->SetColor(VRC_CROSSHAIRNORTH, 4, 208, 24);
	pDraw->SetColor(VRC_OPCENTER, 32, 192, 255);
	pDraw->SetColor(VRC_LEAKGEOM, 255, 128, 0);
	pDraw->RealizePal();

	// if (strlen(Game::GetPalName()))
	// 	pDraw->LoadPal(Game::GetPalName());
}

bool FPrefabView::LoadPrefab(const char *pathName) {
	FDraw::OutputText("Loading prefab... ");
	if (pathName != NULL)
	{
		if (!LFile::Exist(pathName))
		{
			return false;
		}

		CString fName = pathName;
		fName = fName.Right(4);
		if (fName.CompareNoCase(".qle") != 0 &&	fName.CompareNoCase(".map") != 0)
			return false;
	}
	/*if (pathName != NULL && !LFile::Exist(pathName))
	{

		return false;

	// Do a check on the pathname ext.
	if (pathName != NULL) {
		CString fName = pathName;
		fName = fName.Right(4);
		if (fName.CompareNoCase(".qle") != 0 &&
			fName.CompareNoCase(".map") != 0)
			return false;
	}*/

	ASSERT(pRootObj != NULL);
	delete pRootObj;

	HCURSOR oldCur = AfxGetApp()->LoadCursor(IDC_WAIT);

	// hax0r
	TexDB *texDB = GMainFrame->GetDeskTopDocument()->GetGame()->GetTexDB();
	texDB->SetTryExtract(false);

	bool ok;
	const char *c = strrchr(pathName, '.');
	if (c != NULL && stricmp(c, ".qle") == 0) {
		pRootObj = new Object;
		ok = pRootObj->LoadObjFile(pathName);
	}
	else if (c != NULL && stricmp(c, ".map") == 0) {
		pRootObj = MapIO::ReadMap(pathName);
		ok = (pRootObj != NULL);
	}
	else {
		pRootObj = new Object;
		ok = false;
	}

	// un-hax0r
	texDB->SetTryExtract(true);

	::SetCursor(oldCur);

	ASSERT(pSlctr != NULL);
	delete pSlctr;
	pSlctr = new Selector(*pRootObj);

	Vector3d pVec(128.0f, 128.0f, 128.0f);
	SphrVector orientVec;
	orientVec.NewVector(pVec.MultVector(-1.0f), Vector3d::zAxisVec);
	pVec.MultVector(-1.0f);
	pView->SetNewView(*pRootObj, *pSlctr, true, &pVec,
					  &orientVec, true);

	if (!ok) {
		FDraw::OutputText("Error.\nUnable to load prefab sucessfully.\n");
		MessageBox("Unable to load prefab sucessfully",
				   NULL, MB_ICONWARNING | MB_OK);
		return false;
	}

	// Recenter the object.
	Vector3d centerVec;
	pRootObj->AutoCenterByBound(centerVec);

	// Refresh the view.
	UpdateWnd();

	// Update title bar.
	c = strrchr(pathName, '\\');
	char buf[256];
	sprintf(buf, "Prefab: %s", (c != NULL ? c + 1 : ""));
	SetWindowText(buf);

	FDraw::OutputText("OK.\n");
	return true;
}

void FPrefabView::OnSize(UINT nType, int cx, int cy) {
	CWnd::OnSize(nType, cx, cy);

	// Remember size.
	width = cx;
	height = cy;

	ASSERT(pView != NULL);
	ASSERT(pDraw != NULL);

	if (cx > 0 && cy > 0) 
		pDraw->Size(cx, cy);

	pView->SetWindowWidth((int) ((float) Max(cx, cy) / 2.0f));

	UpdateWnd();
}

void FPrefabView::OnPaint() {
	CPaintDC dc(this);
	pDraw->Paint(&dc);
}

void FPrefabView::UpdateWnd(void) {
	pDraw->UseBuf(0);
	pDraw->Begin();
	pDraw->Clear();
	pView->RenderWireFrame(true, false, false);
	InvalidateRect(NULL, false);
	UpdateWindow();
}

void FPrefabView::OnLButtonDown(UINT nFlags, CPoint point) {
	if (mouseRClick || mouseRDrag)
		return;

	mouseLDrag = false;
	mouseLClick = true;
	ptClick = point;

	SetCapture();

	// Hide the mouse.
	ShowCursor(FALSE);

	lastMPos = point;
	ReCenterMousePos(point);

	// Turn on wireframe approximation rendering.
	//View::ApproxItems(true);
}

void FPrefabView::OnLButtonUp(UINT nFlags, CPoint point) {
	if (!mouseLClick && !mouseLDrag)
		return;

	// Turn off wireframe approximation rendering.
	//View::ApproxItems(false);

	// Restore original mouse pos.
	CPoint ptScreen(ptClick);
	ClientToScreen(&ptScreen);
	::SetCursorPos(ptScreen.x, ptScreen.y);

	ShowCursor(TRUE);

	ReleaseCapture();

	mouseLDrag = false;
	mouseLClick = false;

	// Refresh view.
	UpdateWnd();
}

void FPrefabView::OnRButtonDown(UINT nFlags, CPoint point) {
	if (mouseLClick || mouseLDrag)
		return;

	mouseRDrag = false;
	mouseRClick = true;
	ptClick = point;

	SetCapture();

	// Hide the mouse.
	ShowCursor(FALSE);

	lastMPos = point;
	ReCenterMousePos(point);

	// Turn on wireframe approximation rendering.
	//View::ApproxItems(true);
}

void FPrefabView::OnRButtonUp(UINT nFlags, CPoint point) {
	if (!mouseRClick && !mouseRDrag)
		return;

	// Turn off wireframe approximation rendering.
	//View::ApproxItems(false);

	// Restore original mouse pos.
	CPoint ptScreen(ptClick);
	ClientToScreen(&ptScreen);
	::SetCursorPos(ptScreen.x, ptScreen.y);

	ShowCursor(TRUE);

	ReleaseCapture();

	mouseRDrag = false;
	mouseRClick = false;

	// Refresh view.
	UpdateWnd();
}

void FPrefabView::OnMouseMove(UINT nFlags, CPoint point) {

	if (mouseLClick || mouseRClick) {
		CSize sizeMoved = ptClick - point;

		if (abs(sizeMoved.cx) > GetSystemMetrics(SM_CXDRAG) ||
			abs(sizeMoved.cy) > GetSystemMetrics(SM_CYDRAG)) {

			if (mouseLClick)
				mouseLDrag = true;
			else if (mouseRClick)
				mouseRDrag = true;
			mouseLClick = mouseRClick = false;
		}
	}

	if (!mouseLDrag && !mouseRDrag)
		return;

	if (mouseLDrag)
		OnEyeRotate(nFlags, point);
	else if (mouseRDrag)
		OnEyeZoom(nFlags, point);
}

void FPrefabView::ReCenterMousePos(CPoint &ptMousePos) {
	// Adjust the mouse to the middle of screen if it's near edges.
	// Used during operations where the mouse is hidden.

	CRect deskRect;
	(GetDesktopWindow())->GetWindowRect(&deskRect);

	CRect boundRect(deskRect);
	boundRect.DeflateRect(deskRect.right / 4, deskRect.bottom / 4);

	CPoint ptTemp(ptMousePos);
	ClientToScreen(&ptTemp);
	if (boundRect.PtInRect(ptTemp)) {
		// Do nothing.  We're fine.
		return;
	}

	// The mouse cursor is outside the bounding area.
	// Center it.

	ptTemp.x = deskRect.right / 2;
	ptTemp.y = deskRect.bottom / 2;
	ScreenToClient(&ptTemp);

	// Traverse through message queue to modify mouse events.
	// ......

	// Adjust both last pos and current pos relative to center.
	lastMPos.x -= ptMousePos.x - ptTemp.x;
	lastMPos.y -= ptMousePos.y - ptTemp.y;
	ptMousePos = ptTemp;

	// Center cursor.
	::SetCursorPos(deskRect.right / 2, deskRect.bottom / 2);
}

// Orbit the view around the edit focus point.
void FPrefabView::OnEyeRotate(UINT nFlags, CPoint point) {
	// Readjust cursor.
	ReCenterMousePos(point);

	// Get rotation vector.
	CRect rect;
	GetClientRect(&rect);

	CPoint diffVec(point);
	diffVec -= lastMPos;

	lastMPos = point;

	float x, y;
	x = diffVec.x * 180.0f / rect.right;
	y = diffVec.y * 180.0f / rect.bottom;
	x *= 10.0f / 5.0f;
	y *= 10.0f / 5.0f;

	// if (invRMouse) {
	// 	x *= -1.0f;
	// 	y *= -1.0f;
	// }

	// Get radius distance.
	Vector3d radiusVec = pView->GetPosition();
	ASSERT(radiusVec.GetMag() >= 1.0f); // Sanity.

	// Get rotation vector.
	SphrVector orientVec;
	float angYaw, angPitch;
	orientVec.NewVector(radiusVec, Vector3d::zAxisVec);
	angYaw = orientVec.GetYaw() + DEG2RAD(x);
	angPitch = orientVec.GetPitch() + DEG2RAD(y);
	angPitch = Min(Max(angPitch, DEG2RAD(-89.5f)), DEG2RAD(89.5f));	
	orientVec.NewVector(angYaw, angPitch, 0.0f);

	// Rotate it.
	Matrix44 trans;
	trans.SetRotate(orientVec);
	radiusVec.NewVector(0.0f, radiusVec.GetMag(), 0.0f);
	trans.Transform(radiusVec);

	// Set view pos.
	pView->SetPosition(radiusVec);

	// Set view orientataion.
    orientVec.NewVector(orientVec.GetYaw() + DEG2RAD(180.0f),
						-orientVec.GetPitch(), 0.0f);
	pView->SetOrientation(orientVec);

	// update view.
	UpdateWnd();

}

void FPrefabView::OnEyeZoom(UINT nFlags, CPoint point) {
	// Check for cursor adjustments.
	ReCenterMousePos(point);

	CPoint diffVec(point);
	diffVec -= lastMPos;

	lastMPos = point;

	// Assume view will always face origin.
	Vector3d radiusVec = pView->GetPosition();
	float r = radiusVec.GetMag();
	float y = diffVec.y * -2.0f;
	y = Min(y, r - 1.1f);
	Vector3d moveVec(0.0f, y, 0.0f);

	pView->MoveRelPosition(moveVec);

	// Update just self.
	UpdateWnd();
}

/////////////////////////////////////////////////////////////////////////////
// FPrefabFrame

FPrefabFrame::FPrefabFrame() {
}

FPrefabFrame::~FPrefabFrame() {
}

BEGIN_MESSAGE_MAP(FPrefabFrame, CControlBar)
	//{{AFX_MSG_MAP(FPrefabFrame)
	ON_WM_CREATE()
	ON_WM_NCDESTROY()
	ON_WM_SIZE()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// FPrefabFrame message handlers

int FPrefabFrame::OnCreate(LPCREATESTRUCT lpCreateStruct) {
	if (CControlBar::OnCreate(lpCreateStruct) == -1)
		return -1;

	// Create the child preview window.
	DWORD dwStyle = WS_VISIBLE | WS_CHILD;
	CRect rect(0, 0, 1, 1);
	if (prefabView.Create(NULL, "",  dwStyle, rect, this, 1) == 0)
		return -1;

	return 0;
}

void FPrefabFrame::OnSize(UINT nType, int cx, int cy) {
	CControlBar::OnSize(nType, cx, cy);

	prefabView.SetWindowPos(NULL, 3, 3, cx - 7, cy - 7, SWP_NOREPOSITION);
}

void FPrefabFrame::OnNcDestroy() {
	CControlBar::OnNcDestroy();
}

void FPrefabFrame::OnUpdateCmdUI(CFrameWnd* pTarget, BOOL bDisableIfNoHndler) {
}

CSize FPrefabFrame::CalcDynamicLayout(int nLength, DWORD dwMode) {
	CSize rtnSize;

	rtnSize.cx = 240;
	rtnSize.cy = 240;

	return rtnSize;
}