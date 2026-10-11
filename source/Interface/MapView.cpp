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

// FMapView.cpp : implementation of the FMapView class
//

#include "stdafx.h"

#include "GenesisEditor.h"
#include "EditorDoc.h"
#include "MapView.h"
#include "MainFrm.h"
#include "Draw.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

//======================================================================
// FMapView
//======================================================================

Vector3d FMapView::opCenterVec;
Vector3d FMapView::oldOpCenterVec;
UINT FMapView::modifyBrushMode;
const GPolygon *FMapView::pManipFace;
const Edge3d *FMapView::pManipEdge;
int FMapView::manipVertex;
bool FMapView::dispManipDots;
bool FMapView::drawScaleBoundBox = false;
bool FMapView::updateSclBoundBox = false;

LConfig *FMapView::cfg = NULL;
bool FMapView::gridSnap = true;
bool FMapView::rotSnap = true;
UINT FMapView::gridSnapVal = 16;
UINT FMapView::rotSnapVal = 15;

bool FMapView::invLMouse = false;
bool FMapView::invRMouse = false;
UINT FMapView::walkSpeed = 16;
UINT FMapView::sensitivity = 10;

WORD FMapView::keyForward = 'W' - 'A' + 0x41;
WORD FMapView::keyBackward = 'X' - 'A' + 0x41;
WORD FMapView::keyLeft = 'A' - 'A' + 0x41;
WORD FMapView::keyRight = 'D' - 'A' + 0x41;
WORD FMapView::keyUp = 'E' - 'A' + 0x41;
WORD FMapView::keyDown = 'C' - 'A' + 0x41;

UINT FMapView::snapAlignment = 0;
UINT FMapView::editConstraint = 0;

char FMapView::draw2dWire[] = "MFC";
char FMapView::draw3dWire[] = "MFC";
char FMapView::draw3dSolid[] = "DX12";
char FMapView::draw3dTex[] = "DX12";

IMPLEMENT_DYNCREATE(FMapView, CView)

BEGIN_MESSAGE_MAP(FMapView, CView)
	//{{AFX_MSG_MAP(FMapView)
	ON_WM_SIZE()
	ON_WM_CONTEXTMENU()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MBUTTONDOWN()
	ON_WM_MBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_RBUTTONDOWN()
	ON_WM_RBUTTONUP()
	ON_WM_KEYUP()
	ON_WM_TIMER()
	ON_WM_PAINT()
	ON_COMMAND(ID_RCMMV_PERFORMCLIP, OnRcmqvPerformClip)
	ON_COMMAND(ID_RCMMV_REMOVELEFT, OnRcmqvRemoveLeft)
	ON_COMMAND(ID_RCMMV_REMOVERIGHT, OnRcmqvRemoveRight)
	ON_COMMAND(ID_RCMMV_SPLITATLINE, OnRcmqvSplitAtLine)
	ON_COMMAND(ID_EDIT_PASTE, OnEditPaste)
	ON_UPDATE_COMMAND_UI(ID_EDIT_PASTE, OnUpdateEditPaste)
	ON_COMMAND(ID_RCMMV_PERFORMFLIP, OnRcmqvPerformFlip)
	ON_COMMAND(ID_RCMMV_PERFORMMIRROR, OnRcmqvPerformMirror)
	ON_WM_KEYDOWN()
	ON_WM_MOUSEWHEEL()
	ON_COMMAND(ID_EDIT_DUPLICATE, OnEditDuplicate)
	ON_UPDATE_COMMAND_UI(ID_EDIT_DUPLICATE, OnUpdateEditDuplicate)
	//}}AFX_MSG_MAP
	ON_COMMAND(ID_EDIT_ALIGNSET, OnEditAlignSet)
	ON_COMMAND(ID_EDIT_ALIGNCENTER, OnEditAlignSelection)
	ON_COMMAND(ID_EDIT_ALIGNTOP, OnEditAlignSelection)
	ON_COMMAND(ID_EDIT_ALIGNBOTTOM, OnEditAlignSelection)
	ON_COMMAND(ID_EDIT_ALIGNLEFT, OnEditAlignSelection)
	ON_COMMAND(ID_EDIT_ALIGNRIGHT, OnEditAlignSelection)
	ON_UPDATE_COMMAND_UI_RANGE(ID_EDIT_ALIGNSET, ID_EDIT_ALIGNRIGHT, OnEditAlignUI)
	ON_COMMAND(ID_EDIT_DECRGRIDSIZE, OnDecrDisplayGridSize)
	ON_COMMAND(ID_EDIT_INCRGRIDSIZE, OnIncrDisplayGridSize)
	ON_COMMAND_RANGE(ID_MODE_FACEMOVE, ID_MODE_VERTEXMOVE, OnModifyBrushModes)
	ON_COMMAND_RANGE(ID_VIEW_RENDER_WIREFRAME, ID_VIEW_RENDER_TEXTURE, // _LIGHTING,
		OnModifyViewRender)
	ON_UPDATE_COMMAND_UI_RANGE(ID_VIEW_RENDER_WIREFRAME, ID_VIEW_RENDER_TEXTURE, // _LIGHTING,
		OnModifyViewRenderUI)
	ON_COMMAND(ID_VIEW_TIMEREFRESH, OnTimeRefresh)
	ON_COMMAND(ID_MAPVIEW_ZOOMIN, OnViewZoomIn)
	ON_COMMAND(ID_MAPVIEW_ZOOMOUT, OnViewZoomOut)
END_MESSAGE_MAP()

//========================================
// FMapView Init and Exit

void FMapView::Init() {
	cfg = new LConfig("MapView");
	cfg->RegisterVar("GridSnap", &gridSnap, LVAR_INT);
	cfg->RegisterVar("GridSnapVal", &gridSnapVal, LVAR_INT);
	cfg->RegisterVar("RotSnap", &rotSnap, LVAR_INT);
	cfg->RegisterVar("RotSnapVal", &rotSnapVal, LVAR_INT);

	cfg->RegisterVar("InvLMouse", &invLMouse, LVAR_INT);
	cfg->RegisterVar("InvRMouse", &invRMouse, LVAR_INT);
	cfg->RegisterVar("WalkSpeed", &walkSpeed, LVAR_INT);
	cfg->RegisterVar("Sensitivity", &sensitivity, LVAR_INT);

	cfg->RegisterVar("KeyForward", &keyForward, LVAR_INT);
	cfg->RegisterVar("KeyBackward", &keyBackward, LVAR_INT);
	cfg->RegisterVar("KeyLeft", &keyLeft, LVAR_INT);
	cfg->RegisterVar("KeyRight", &keyRight, LVAR_INT);
	cfg->RegisterVar("KeyUp", &keyUp, LVAR_INT);
	cfg->RegisterVar("KeyDown", &keyDown, LVAR_INT);

	// cfg->RegisterVar("SnapAlignment", &snapAlignment, LVAR_INT);
	cfg->RegisterVar("EditConstraint", &editConstraint, LVAR_INT);

	cfg->RegisterVar("Draw2DWire", &draw2dWire, LVAR_STR);
	cfg->RegisterVar("Draw3DWire", &draw3dWire, LVAR_STR);
	cfg->RegisterVar("Draw3DSolid", &draw3dSolid, LVAR_STR);
	cfg->RegisterVar("Draw3DTex", &draw3dTex, LVAR_STR);

	FDraw::ValidateDriver(draw2dWire, "MFC");
	FDraw::ValidateDriver(draw3dWire, "MFC");
	FDraw::ValidateDriver(draw3dSolid, "DX12");
	FDraw::ValidateDriver(draw3dTex, "DX12");
}

void FMapView::Exit() {
	cfg->SaveVars();
	delete cfg;
}

/////////////////////////////////////////////////////////////////////////////
// FMapView construction/destruction

FMapView::FMapView() {
	pView = NULL;
	pDraw = NULL;

	qvsBefore = NULL;

	mouseLClick = mouseMClick = mouseRClick = false;
	mouseLDrag = mouseMDrag = mouseRDrag = false;
	reverseSelect = false;
	selectPending = selectAddPending = false;
	dragSelect = dragAddSelect = false;
	operatePending = dragOperate = dragOpCenter = false;
	scrollingView = 0;
	useBack = false;
	planeDefined = false;
	dragPlanePt = 0;

	scissors.LoadBitmap(IDB_SCISSORS);
}

FMapView::~FMapView() {
	ASSERT(pView != NULL);
	delete pView;
	ASSERT(pDraw != NULL);
	delete pDraw;

	if(this == coordView)
		coordView = NULL;
}

BOOL FMapView::PreCreateWindow(CREATESTRUCT& cs) {
	if (!CView::PreCreateWindow(cs))
		return FALSE;

	// Register the new window class.
	WNDCLASS wndclass;
	HINSTANCE hInst = AfxGetInstanceHandle();

	if (!(::GetClassInfo(hInst, "GenesisMapView", &wndclass))) {
		if (::GetClassInfo(hInst, cs.lpszClass, &wndclass)) {
			wndclass.lpszClassName = "GenesisMapView";
			wndclass.hbrBackground = NULL;
			wndclass.style &= ~CS_DBLCLKS;
			if (!AfxRegisterClass(&wndclass))
				AfxThrowResourceException();
		}
		else
			AfxThrowResourceException();
	}

	cs.lpszClass = "GenesisMapView";

	return TRUE;
}

void FMapView::InitView(UINT nID, Object *viewScope,
					 Selector *selector, const Vector3d &posVec) {
	ASSERT(nID >= ID_VIEW_NEWTOP && nID <= ID_VIEW_NEWSIDE);
	ASSERT(viewScope != NULL);
	ASSERT(pView == NULL);

	qvType = nID;
	qvRender = ID_VIEW_RENDER_WIREFRAME;

	pView = new View(NULL, *viewScope, *selector);
	CreateDraw();

	Vector3d pVec(posVec);
	SphrVector orientVec;
	bool bPerspective;

	if (nID == ID_VIEW_NEWTOP) {
		bPerspective = false;
		orientVec.NewVector(0.0f, DEG2RAD(-90.0f), 0.0f);
		zoomVal = 0.75f;
	}
	else if (nID == ID_VIEW_NEWBACK) {
		bPerspective = false;
		orientVec.NewVector(0.0f, 0.0f , 0.0f);

		zoomVal = 0.75f;
	}
	else if (nID == ID_VIEW_NEWSIDE) {
		bPerspective = false;
		orientVec.NewVector(DEG2RAD(-90.0f), 0.0f , 0.0f);

		zoomVal = 0.75f;
	}
	else if (nID == ID_VIEW_NEW3D) {
		Vector3d fv(-100.0f, -100.0f, 100.0f);
		Vector3d tv(0.0f, 0.0f, 1.0f);

		bPerspective = true;
		pVec.AddVector(fv);
		orientVec.NewVector(fv.MultVector(-1.0f), tv);

		zoomVal = 1.0f;
	}

	// Need to use default configurable viewDepth insteal of 1000.0f.
	pView->SetNewView(*viewScope, *selector, true, &pVec,
					  &orientVec, bPerspective, 1, 1000.0f);

	pView->ShowCrossHair(true);
	pView->SetCrossHairPos(Vector3d(0.0f, 0.0f, 0.0f));
}

void FMapView::CreateDraw(void) {
	char *driver;
	int oldBits = bits;

	if(strlen(GetDocument()->GetGame()->GetPalName()))
		bits = 8;
	else
		bits = 16;

	if(qvType == ID_VIEW_NEW3D) {
		if(qvRender == ID_VIEW_RENDER_WIREFRAME) {
			driver = draw3dWire;
			bits = 8;
		}
		else if(qvRender == ID_VIEW_RENDER_SOLID)
			driver = draw3dSolid;
		else if(qvRender == ID_VIEW_RENDER_TEXTURE)
			driver = draw3dTex;
	}
	else {
		driver = draw2dWire;
		bits = 8;
	}

	if(!pDraw || strcmp(driver, pDraw->GetDriver()) || bits != oldBits) {
		if(pDraw)
			delete pDraw;

		pDraw = FDraw::New(driver, bits, this, 2);

		ASSERT(pDraw != NULL);
		pDraw->SetZoomPtr(&zoomVal);

		pView->SetDraw(pDraw);
	}

	if(qvType == ID_VIEW_NEW3D)
		pDraw->ZBufInit();

	useBack = false;

	SetColors();
}

void FMapView::SetColors(void) {
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

	if(qvRender == ID_VIEW_RENDER_WIREFRAME) {
		pDraw->RealizePal();
	}
	else {
		char *palName = GetDocument()->GetGame()->GetPalName();
		if(strlen(palName))
			pDraw->LoadPal(palName);
	}
}

void FMapView::SelColors(int vrc, int red, int grn, int blu) {
	pDraw->SetColor(VRC_VIEW, 0, 0, 0);
	pDraw->SetColor(VRC_NORMAL, 255, 255, 255);
	pDraw->SetColor(vrc, red, grn, blu);
	//pDraw->SetColor(VRC_SELECT, 255, 0, 0);
	pDraw->SetColor(VRC_HOTSELECT, 255, 150, 150);
	pDraw->SetColor(VRC_ENTITY, 120, 150, 255);
	pDraw->SetColor(VRC_MANIPDOT, 255, 255, 0);
	pDraw->SetColor(VRC_FACESELECT, 255, 255, 0);
	pDraw->SetColor(VRC_CROSSHAIR, 24, 100, 244);
	pDraw->SetColor(VRC_CROSSHAIRNORTH, 4, 208, 24);
	pDraw->SetColor(VRC_OPCENTER, 32, 192, 255);
	pDraw->SetColor(VRC_LEAKGEOM, 255, 128, 0);

	if(qvRender == ID_VIEW_RENDER_WIREFRAME) {
		pDraw->RealizePal();
	}
	else {
		char *palName = GetDocument()->GetGame()->GetPalName();
		if(strlen(palName))
			pDraw->LoadPal(palName);
	}
}

void FMapView::SuspendRenderLib(bool suspend) {
	if (suspend) {
		pDraw->Suspend();
	}
	else {
		pDraw->Resume();
		// Refresh the view.
		OnUpdate(NULL, NULL, NULL);
	}
}

//======================================================================
// Interface to Undo module
//======================================================================

void FMapView::GetViewState(Vector3d &posVec, SphrVector &oriVec,
						 float &zoomRate, UINT *viewType) const {
	pView->GetPosition(posVec);
	pView->GetOrientation(oriVec);
	zoomRate = zoomVal;
	if (viewType != NULL)
		*viewType = qvType;
}

void FMapView::SetViewState(const Vector3d &posVec, const SphrVector &oriVec,
						 float zoomRate, bool updateView) {
	pView->SetPosition(posVec);
	pView->SetOrientation(oriVec);

	pView->SetCrossHairPos(GMainFrame->GetEditFocusPos());

	if (zoomVal != zoomRate) {
		zoomVal = zoomRate;
		OnZoomChanged();
	}

	// Update the view..
	if (updateView)
		OnUpdate(NULL, NULL, NULL);
}

void FMapView::RenderFaceSlct(bool faceSlct) {
	pView->DisplayFaceSelect(faceSlct);
}

void FMapView::AdjustViewDepth(float incr) {
	float depth = pView->GetClipDepth();

	depth += incr;
	depth = Max(250.0f, Min(9000.0f, depth));
	pView->SetClipDepth(depth);

	// Update self.
	OnUpdate(NULL, NULL, NULL);
}

// Op Position.
const Vector3d &FMapView::GetOperateCenterPos(void) {
	return opCenterVec;
}

void FMapView::SetOperateCenterPos(const Vector3d &opCenterPos) {
	opCenterVec = opCenterPos;
}

//======================================================================
// General FMapView Maintaince.
//======================================================================

void FMapView::OnSize(UINT nType, int cx, int cy) {
	CView::OnSize(nType, cx, cy);

	// Remember size.
	width = cx;
	height = cy;

	// Remember center.
	orgX = cx / 2;
	orgY = cy / 2;

	if (pView) {
		ASSERT(pDraw != NULL);

		if (cx > 0 && cy > 0) 
			pDraw->Size(cx, cy);

		pView->SetWindowWidth((int) ((float) Max(cx, cy) / (2.0f * zoomVal)));
		useBack = false;
		OnUpdate(NULL, NULL, NULL);
	}
}

void FMapView::OnZoomChanged(void) {
	ASSERT(pView != NULL);

	// Need to update the clip area for views.
	CRect rect;
	GetClientRect(&rect);

	float width = Max(rect.right, rect.bottom) / (2.0f * zoomVal);

	pView->SetWindowWidth((int) width);
}

//======================================================================
// Coord Conversion
//======================================================================

void FMapView::DC2LC(const CPoint &pt, float &x, float &y) const {
	x = (pt.x - orgX) / zoomVal;
	y = (orgY - pt.y) / zoomVal;
}

void FMapView::DU2LU(const CPoint &pt, float &x, float &y) const {
	x = pt.x / zoomVal;
	y = - pt.y / zoomVal;
}

void FMapView::LC2DC(float x, float y, CPoint &pt) const {
	x = x * zoomVal + orgX;
	pt.x = ROUNDI(x);
	y = orgY - y * zoomVal;
	pt.y = ROUNDI(y);
}

void FMapView::LC2DC(const Vector3d &vec3D, CPoint &pt) const {
	CRect rect;
	GetClientRect(&rect);
	ASSERT(vec3D.GetY() != 0.0f);
	float div = Max(rect.right, rect.bottom) * 0.5f / vec3D.GetY();

	float x = vec3D.GetX() * div + orgX;
	pt.x = ROUNDI(x);
	float y = orgY - vec3D.GetZ() * div;
	pt.y = ROUNDI(y);
}

void FMapView::LU2DU(float x, float y, CPoint &pt) const {
	x = x * zoomVal;
	pt.x = ROUNDI(x);
	y = - y * zoomVal;
	pt.y = ROUNDI(y);
}

//======================================================================
// FMapView render stuff
//======================================================================

#define DUMV_DISPLAYDRAGBOX		0x1000
#define DUMV_UPDATEMANIPSTAT	0x2000
#define DUMV_INITPLANEDRAG		0x4000
#define DUMV_UPDATEGRIDS		0x8000

void FMapView::OnUpdate(CView* pSender, LPARAM lHint, CObject* pHint) {

	if (lHint & DUAV_NOMAPVIEWS)
		return;

	if (lHint & DUAV_SCOPECHANGED)  {	// Adjust the view's scope durn
		OnScopeChanged((Object *) pHint);
		return;
	}

	if (lHint & DUMV_UPDATEMANIPSTAT)
		UpdateManipDispStat();

	Vector3d viewPos = pView->GetPosition();
	SphrVector orient = pView->GetOrientation();
	Object *viewObj = pView->GetViewObjPtr();

	bool drawFront = true;
	bool objChanged = (lHint & (DUAV_SUPDATEALL | DUMV_UPDATEGRIDS) ? true : false);

	if (qvRender == ID_VIEW_RENDER_WIREFRAME && pDraw->GetNumBufs() == 2) {
		if (viewPos == oldViewPos && orient == oldOrient &&
			zoomVal == oldZoomVal && viewObj == oldViewObj && !objChanged) {
			if(!useBack) {
				useBack = true;
				pDraw->UseBuf(1);
			}
			else
				drawFront = false;
		}
		else
			useBack = false;
	}
	else {
		useBack = false;
		pDraw->UseBuf(0);
	}

	if (drawFront) {
		pDraw->Begin();
		pDraw->Clear();

		if(qvRender == ID_VIEW_RENDER_TEXTURE) {
			pDraw->ZBufClear();
			pDraw->ZBufTextured(true);
			pView->RenderSolid(true);
//			pDraw->ZBufRender();
		}
		else if (qvRender == ID_VIEW_RENDER_SOLID) {
			pDraw->ZBufClear();
			pDraw->ZBufTextured(false);
			pView->RenderSolid(false);
//			pDraw->ZBufRender();
		}
		else {
			// draw grid on 2d views
			if(qvType != ID_VIEW_NEW3D) {
				Matrix44 trans;

				trans.SetIdentity();
				pView->CalInvTransSpaceMatrix(trans);

				Object *obj = pView->GetViewObjPtr();
				ASSERT(obj != NULL);
				while (obj->GetParentPtr() != NULL) {
					obj->CalInvTransSpaceMatrix(trans);
					obj = obj->GetParentPtr();
				}
	
				Vector3d origPos(0.0f, 0.0f, 0.0f);
				trans.Transform(origPos);
				pDraw->Grid(origPos.GetX(), origPos.GetZ());
			}

			pView->RenderWireFrame(true, false, qvType == ID_VIEW_NEW3D);  // render world
		}
	}

	if (useBack) {
		pDraw->UseBuf(0);
		pDraw->CopyBuf(1);
//		pDraw->Clear();  // debugging
	}
	
	if (qvRender == ID_VIEW_RENDER_WIREFRAME) {
		pDraw->Begin();
		pView->RenderWireFrame(false, true, false);  // render selected
	}

	oldViewPos = viewPos;
	oldOrient = orient;
	oldZoomVal = zoomVal;
	oldViewObj = viewObj;
	
	DrawOpCenter();

	if (lHint & DUMV_DISPLAYDRAGBOX)
		DrawDragBox();

	if (lHint & (DUAV_OBJSSEL | DUAV_OBJSMODATTRIB))
		CalcScaleManipDots();

	if (drawScaleBoundBox && qvType != ID_VIEW_NEW3D)
		DrawScaleBoundBox();

	if (lHint & DUMV_INITPLANEDRAG) {
		planeDefined = false;
		dragPlanePt = 0;
	}

	if (planeDefined)
		DrawOpPlane();

	InvalidateRect(NULL, false);
	UpdateWindow();

	if (planeDefined)
		DrawCutSide();
}

static int fooCount = 0;

void FMapView::OnDraw(CDC* pDC) {
	CView::OnDraw(pDC);
	TRACE1("OnDraw %d\n", fooCount);
	fooCount++;
}

int paintCount;
void FMapView::OnPaint() {
	CPaintDC dc(this); // device context for painting
	pDraw->Paint(&dc);
	paintCount++;
}

void FMapView::DrawOpCenter(void) {
	// Figure out if we should render the op center.
	FEditorDoc *pDoc = GetDocument();
	Selector *pSlct = &(GMainFrame->GetSelector());

	UINT opMode = GMainFrame->GetOpMode();
	if (opMode != ID_MODE_OBJECTSELECT && opMode != ID_MODE_OBJECTMOVE &&
		opMode != ID_MODE_OBJECTROTATE && opMode != ID_MODE_OBJECTSCALE)
		return;

	if (pSlct->GetNumMSelectedObjects() == 0)
		return;

	if (qvRender == ID_VIEW_RENDER_SOLID || qvRender == ID_VIEW_RENDER_TEXTURE)
		return;

	CPoint opCenterPos;
	FindOpCenterPos(opCenterPos);

	// Draw the X mark at opcenterPos.
	pDraw->Color(VRC_OPCENTER);
	pDraw->Cross(opCenterPos.x, opCenterPos.y, 5);
}

void FMapView::DrawDragBox(void) {
	// draw rect at coords dragBoxRect.
	pDraw->Color(VRC_OPCENTER);
	pDraw->Box(dragBoxRect.left, dragBoxRect.top, dragBoxRect.right, dragBoxRect.bottom);
}

void FMapView::DrawScaleBoundBox(void) {
	// Draw the scale bounding boxes.
	Selector *pSlctr = &(GMainFrame->GetSelector());
	if (pSlctr->GetNumMSelectedObjects() == 0)
		return;

	CPoint pt1, pt2;
	if (qvType != ID_VIEW_NEW3D) {
		LC2DC(scaleManipDots[0].GetX(), scaleManipDots[0].GetZ(), pt1);
		LC2DC(scaleManipDots[4].GetX(), scaleManipDots[4].GetZ(), pt2);
	}
	else {
		LC2DC(scaleManipDots[0], pt1);
		LC2DC(scaleManipDots[4], pt2);
	}
	
	pDraw->Color(View::GetColor(VRC_MANIPDOT));
	pDraw->StippleBox(pt1.x, pt1.y, pt2.x, pt2.y, 5, 5);

	for (int i = 0; i < 8; i++) {
		if (qvType != ID_VIEW_NEW3D)
			LC2DC(scaleManipDots[i].GetX(), scaleManipDots[i].GetZ(), pt1);
		else
			LC2DC(scaleManipDots[i], pt1);
		pDraw->Point(pt1.x, pt1.y, 5);
	}
}

void FMapView::CalcScaleManipDots(void) {
	if (!drawScaleBoundBox)
		return;

	ASSERT(GMainFrame->GetOpMode() == ID_MODE_OBJECTSCALE);

	Selector *pSlctr = &(GMainFrame->GetSelector());
	if (pSlctr->GetNumMSelectedObjects() == 0)
		return;

	FEditorDoc *pDoc = GetDocument();
	Vector3d minVec, maxVec;
	pDoc->GetObjectsScaleBoundBox(pSlctr->GetMSelectedObjects(),
								  *pView, minVec, maxVec);
	
	float x1, z1, x2, z2, xAve, yAve, zAve;
	x1 = minVec.GetX();
	z1 = minVec.GetZ();
	x2 = maxVec.GetX();
	z2 = maxVec.GetZ();
	xAve = (x1 + x2) / 2.0f;
	yAve = (minVec.GetY() + maxVec.GetY()) / 2.0f;
	zAve = (z1 + z2) / 2.0f;

	scaleManipDots[0].NewVector(x1,   yAve, z1);
	scaleManipDots[1].NewVector(x1,   yAve, zAve);
	scaleManipDots[2].NewVector(x1,   yAve, z2);
	scaleManipDots[3].NewVector(xAve, yAve, z2);
	scaleManipDots[4].NewVector(x2,   yAve, z2);
	scaleManipDots[5].NewVector(x2,   yAve, zAve);
	scaleManipDots[6].NewVector(x2,   yAve, z1);
	scaleManipDots[7].NewVector(xAve, yAve, z1);
}

void FMapView::DrawOpPlane(void) {
	// Draw the operate plane (line) for PlaneCut && Mirror funcs.
	ASSERT (GMainFrame->GetOpMode() == ID_MODE_PLANECLIP ||
			GMainFrame->GetOpMode() == ID_MODE_MIRROR);

	CPoint pt1, pt2;
	LC2DC(planeDragPt1.GetX(), planeDragPt1.GetZ(), pt1);
	LC2DC(planeDragPt2.GetX(), planeDragPt2.GetZ(), pt2);

	pDraw->Color(View::GetColor(VRC_MANIPDOT));

	// Draw the 2 dots.
	pDraw->Point(pt1.x, pt1.y, 5);
	pDraw->Point(pt2.x, pt2.y, 5);

	if (pt1 == pt2)  // Dont draw the line.
		return;

	// Calc the line's slope for render
	CPoint slope = pt2 - pt1;
	CPoint invSlope(-slope.x, slope.y);
	float length = (float) sqrt((float) slope.x * (float) slope.x +
								(float) slope.y * (float) slope.y);
	int mult = (int) (5000.0f / length);

	slope.x *= mult;
	slope.y *= mult;

	pt1 -= slope;
	pt2 += slope;

	// Draw the line.
	pDraw->Line(pt1.x, pt1.y, pt2.x, pt2.y);
}

void FMapView::DrawCutSide(void) {
	// Draw the scissors bitmap to indicate the cuttin side.
	if (GMainFrame->GetOpMode() != ID_MODE_PLANECLIP)
		return;

	CPoint pt1, pt2;
	LC2DC(planeDragPt1.GetX(), planeDragPt1.GetZ(), pt1);
	LC2DC(planeDragPt2.GetX(), planeDragPt2.GetZ(), pt2);

	if (pt1 == pt2)
		return;

	// Calc the line's slope for render
	CPoint slope = pt2 - pt1;
	float length = (float) sqrt((float) slope.x * (float) slope.x +
								(float) slope.y * (float) slope.y);
	float mult = 25.0f / length;
	CPoint invSlope((int) (slope.y * mult), (int) (-slope.x * mult));

	if (clipSide == -1) // Left
		pt1 += invSlope;
	else if (clipSide == 1) // Right
		pt1 -= invSlope;
	else { // clipSide == 0, Middle
		pt1.x += ((int) (slope.x * mult));
		pt1.y += ((int) (slope.y * mult));
	}

	BITMAP bm;
	scissors.GetObject(sizeof(BITMAP), &bm);

	pt1.x -= bm.bmWidth / 2;
	pt1.y -= bm.bmHeight / 2;

	CDC dcMem, *pDC = GetDC();
	dcMem.CreateCompatibleDC(pDC);
	CBitmap *pOldBitmap = dcMem.SelectObject(&scissors);
	pDC->BitBlt(pt1.x, pt1.y, bm.bmWidth, bm.bmHeight,
				&dcMem, 0, 0, SRCPAINT);
	dcMem.SelectObject(pOldBitmap);
	ReleaseDC(pDC);
}

void FMapView::UpdateGridsDisplay() {
	FEditorDoc *pDoc = GMainFrame->GetDeskTopDocument();
	pDoc->UpdateAllViews(NULL, DUAV_MAPVIEWS | DUMV_UPDATEGRIDS, NULL);
}

//===== Op mode changed =====
void FMapView::OnOpModeChanged(UINT opMode, UINT prevOpMode) {

	if (opMode == ID_MODE_MODIFYBRUSH || opMode == ID_MODE_FACEMOVE) {
		modifyBrushMode = ID_MODE_FACEMOVE;
		pView->DisplayFacePts();
	}
	else if (opMode == ID_MODE_EDGEMOVE) {
		modifyBrushMode = ID_MODE_EDGEMOVE;
		pView->DisplayEdgePts();
	}
	else if (opMode == ID_MODE_VERTEXMOVE) {
		modifyBrushMode = ID_MODE_VERTEXMOVE;
		pView->DisplayVertexPts();
	}
	else {
		pView->DisplayNoPts();
	}

	if (opMode == ID_MODE_OBJECTSCALE) {
		drawScaleBoundBox = true;
		CalcScaleManipDots();
	}
	else {
		drawScaleBoundBox = false;
	}

	// Op Plane stuff.
	planeDefined = false;
	dragPlanePt = 0;
	clipSide = 0;

	// Update Self.
	OnUpdate(NULL, NULL, NULL);
}

// Wrap around for Brush Modes.
void FMapView::OnModifyBrushModes(UINT nID) {
	GMainFrame->SwitchOpMode(nID);
}

// Wrap around for View Render menu stuff.
void FMapView::OnModifyViewRender(UINT nID) {
	FMapView *pMapView = GMainFrame->GetStdMapView(ID_VIEW_NEW3D);
	if(!pMapView)
		return;

	pMapView->qvRender = nID;

	pMapView->CreateDraw();

	pMapView->pDraw->RenderMode(nID);
	pMapView->OnUpdate(NULL, NULL, NULL);
}

void FMapView::OnModifyViewRenderUI(CCmdUI* pCmdUI) {
	FMapView *pMapView = GMainFrame->GetStdMapView(ID_VIEW_NEW3D);
	pCmdUI->SetCheck(pMapView && pMapView->qvRender == pCmdUI->m_nID);
}

//===== View scope is changing=====
void FMapView::OnScopeChanged(Object *pOldScope) {
	// Sanity.
	ASSERT(pOldScope == pView->GetViewObjPtr());

	FEditorDoc *pDoc = GetDocument();
	Object *pRootObj = pDoc->GetRootObjectPtr();
	ASSERT(pOldScope == pRootObj || pOldScope->IsMyAncestor(*pRootObj));

	Selector *pSlct = &(GMainFrame->GetSelector());
	Object *pNewScope = pSlct->GetScopePtr();
	ASSERT(pNewScope == pRootObj || pNewScope->IsMyAncestor(*pRootObj));

	Matrix44 trans;
	Object::GetTransMatrix(*pOldScope, *pNewScope, trans);

	// Transform the view's information.
	Vector3d pVec, fVec, tVec;
	pView->GetPosition(pVec);
	pView->GetOrientation(fVec, tVec);
	fVec.AddVector(pVec);
	tVec.AddVector(pVec);

	trans.Transform(pVec);
	(trans.Transform(fVec)).SubVector(pVec);
	(trans.Transform(tVec)).SubVector(pVec);

	SphrVector oriVec;
	oriVec.NewVector(fVec, tVec);

	pView->SetNewView(*pNewScope, *pSlct, true, &pVec,
					  &oriVec, (qvType == ID_VIEW_NEW3D));
}

//======================================================================
// Mouse UI handling logic.
//======================================================================

int startTime;

void FMapView::OnLButtonDown(UINT nFlags, CPoint point) {

	// Exclude the fly through mode, which has it own LBD processing.
	// if (GMainFrame->GetOpMode() == ID_MODE_FLYTHROUGH)
	//	return;

	// Disable left click during right button ops.
	if (mouseMClick || mouseMDrag || mouseRClick || mouseRDrag)
		return;

	SetCapture();
	mouseLDrag = false;
	mouseLClick = true;
	ptClick = point;

	float x, y;
	DC2LC(point, x, y);
	downClickVec.NewVector(x, y, 0.0f);

	switch (GMainFrame->GetOpMode()) {
	case ID_MODE_OBJECTSELECT:
		OnLMBDObjectsSelect(nFlags, point);
		break;
	case ID_MODE_OBJECTMOVE:
	case ID_MODE_OBJECTROTATE:
	case ID_MODE_OBJECTSCALE:
		OnLMBDObjectsMoveRotateScale(nFlags, point);
		break;
	case ID_MODE_MODIFYBRUSH:
	case ID_MODE_FACEMOVE:
	case ID_MODE_EDGEMOVE:
	case ID_MODE_VERTEXMOVE:
		OnLMBDModifyBrush(nFlags, point);
		break;
	case ID_MODE_PLANECLIP:
	case ID_MODE_MIRROR:
		OnLMBDDefineOpPlane(nFlags, point);
		break;
	case ID_MODE_EYEMOVE:
	case ID_MODE_EYEROTATE:
	case ID_MODE_EYEZOOM:
		OnMBDEyeMoveRotateZoom(nFlags, point);
		break;
	default:
		CView::OnLButtonDown(nFlags, point);
		break;
	}

	startTime = GetTime();
	paintCount = 0;

}

void FMapView::OnLButtonUp(UINT nFlags, CPoint point) {

	if (GMainFrame->GetOpMode() == ID_MODE_FLYTHROUGH)
		return;

	if (!mouseLClick && !mouseLDrag)
		return;

//	TRACE("%2.2f fps\n", (float)paintCount / (float)(GetTime() - startTime) * 1000);

	float x, y;
	DC2LC(point, x, y);
	upClickVec.NewVector(x, y, 0.0f);

	switch (GMainFrame->GetOpMode()) {
	case ID_MODE_OBJECTSELECT:
		OnLMBUObjectsSelect(nFlags, point);
		break;
	case ID_MODE_OBJECTMOVE:
		OnLMBUObjectsMove(nFlags, point);
		break;
	case ID_MODE_OBJECTROTATE:
		OnLMBUObjectsRotate(nFlags, point);
		break;
	case ID_MODE_OBJECTSCALE:
		OnLMBUObjectsScale(nFlags, point);
		break;
	case ID_MODE_MODIFYBRUSH:
	case ID_MODE_FACEMOVE:
	case ID_MODE_EDGEMOVE:
	case ID_MODE_VERTEXMOVE:
		OnLMBUModifyBrush(nFlags, point);
		break;
	case ID_MODE_PLANECLIP:
	case ID_MODE_MIRROR:
		OnLMBUDefineOpPlane(nFlags, point);
		break;
	case ID_MODE_EYEMOVE:
	case ID_MODE_EYEROTATE:
	case ID_MODE_EYEZOOM:
		OnMBUEyeMoveRotateZoom(nFlags, point);
		break;
	default:
		CView::OnLButtonUp(nFlags, point);
		break;
	}

	mouseLDrag = false;
	mouseLClick = false;
	ReleaseCapture();
}

void FMapView::OnMButtonDown(UINT nFlags, CPoint point) {
	// Skip in fly through mode.
	if (GMainFrame->GetOpMode() == ID_MODE_FLYTHROUGH)
		return;

	// Disable middle click during other operations.
	if (mouseLClick || mouseLDrag || mouseRClick || mouseRDrag)
		return;

	// Hack around a windows bug. 
	// Sometimes Windows 95/98 think middle button is down
	//  even when it's released.  Prevents this func
	//  being called 2x and losing the cursor display
	//  during/after panning.
	if (mouseMClick || mouseMDrag)
		return;

	SetCapture();
	mouseMDrag = false;
	mouseMClick = true;
	ptClick = point;

	float x, y;
	DC2LC(point, x, y);
	downClickVec.NewVector(x, y, 0.0f);

	// Pan the view.
	OnMBDEyeMoveRotateZoom(nFlags, point);
}

void FMapView::OnMButtonUp(UINT nFlags, CPoint point) {
	// Skip in fly through.
	if (GMainFrame->GetOpMode() == ID_MODE_FLYTHROUGH)
		return;

	if (!mouseMClick && !mouseMDrag)
		return;

	float x, y;
	DC2LC(point, x, y);
	upClickVec.NewVector(x, y, 0.0f);

	// End pan view.
	OnMBUEyeMoveRotateZoom(nFlags, point);

	mouseMDrag = false;
	mouseMClick = false;
	ReleaseCapture();
}

void FMapView::OnRButtonDown(UINT nFlags, CPoint point) {

	if (GMainFrame->GetOpMode() == ID_MODE_FLYTHROUGH)
		return;

	// Disable right button during left button action.
	if (mouseLClick || mouseLDrag || mouseMClick || mouseMDrag)
		return;

	SetCapture();
	mouseRDrag = false;
	mouseRClick = true;
	ptClick = point;

	float x, y;
	DC2LC(point, x, y);
	downClickVec.NewVector(x, y, 0.0f);

	switch (GMainFrame->GetOpMode()) {
	case ID_MODE_EYEMOVE:
	case ID_MODE_EYEROTATE:
	case ID_MODE_EYEZOOM:
		OnMBDEyeMoveRotateZoom(nFlags, point);
		break;
	case ID_MODE_PLANECLIP:
	case ID_MODE_MIRROR:
		OnRMBDDefineOpPlane(nFlags, point);
	default:
		CView::OnRButtonDown(nFlags, point);
		break;
	}
}

void FMapView::OnRButtonUp(UINT nFlags, CPoint point) {
 
	if (GMainFrame->GetOpMode() == ID_MODE_FLYTHROUGH)
		return;

	if (!mouseRClick && !mouseRDrag)
		return;

	float x, y;
	DC2LC(point, x, y);
	upClickVec.NewVector(x, y, 0.0f);

	switch (GMainFrame->GetOpMode()) {
	case ID_MODE_EYEMOVE:
	case ID_MODE_EYEROTATE:
	case ID_MODE_EYEZOOM:
		OnMBUEyeMoveRotateZoom(nFlags, point);
		break;
	case ID_MODE_OBJECTSELECT:
	case ID_MODE_OBJECTMOVE:
	case ID_MODE_OBJECTROTATE:
	case ID_MODE_OBJECTSCALE:
	case ID_MODE_MODIFYBRUSH:
	case ID_MODE_FACEMOVE:
	case ID_MODE_EDGEMOVE:
	case ID_MODE_VERTEXMOVE:
	case ID_MODE_PLANECLIP:
	case ID_MODE_MIRROR:
		OnRMBUContextMenu(nFlags, point);
		break;
	default:
		// CView::OnRButtonUp(nFlags, point);
		break;
	}

	mouseRDrag = false;
	mouseRClick = false;
	ReleaseCapture();
}

FMapView *FMapView::coordView = NULL;

void FMapView::UpdateCoordInfo(CPoint point) {
	Matrix44 trans;
	Vector3d ov(0.0f, 0.0f, 0.0f);

	trans.SetIdentity();
	pView->CalInvTransSpaceMatrix(trans);

	Object *obj = pView->GetViewObjPtr();
	while(obj) {
		obj->CalInvTransSpaceMatrix(trans);
		obj = obj->GetParentPtr();
	}

	trans.Transform(ov);

	int x = - (int)((orgX - point.x) / zoomVal + ov.GetX());
	int y = - (int)(-(orgY - point.y) / zoomVal + ov.GetZ());

	char title[256];
	if(qvType == ID_VIEW_NEWTOP)
		sprintf(title, "Top (X, Y: %d, %d)", x, y);
	else if(qvType == ID_VIEW_NEWSIDE)
		sprintf(title, "Side (Y, Z: %d, %d)", x, y);
	else if(qvType == ID_VIEW_NEWBACK)
		sprintf(title, "Back (X, Z: %d, %d)", x, y);
	else if(qvType == ID_VIEW_NEW3D)
		sprintf(title, "3D View");

	GetParent()->SetWindowText(title);

	if(coordView == this)
		return;

	if(coordView) { // && ::IsWindow(coordView->m_hWnd)) {
		if(coordView->qvType == ID_VIEW_NEWTOP)
			strcpy(title, "Top");
		else if(coordView->qvType == ID_VIEW_NEWSIDE)
			strcpy(title, "Side");
		else if(coordView->qvType == ID_VIEW_NEWBACK)
			strcpy(title, "Back");
		else {
			coordView = this;
			return;
		}

		coordView->GetParent()->SetWindowText(title);
	}

	coordView = this;
}

void FMapView::OnMouseMove(UINT nFlags, CPoint point) {
	// Special handling for fly through mode.
	if (GMainFrame->GetOpMode() == ID_MODE_FLYTHROUGH) {
		if (qvType == ID_VIEW_NEW3D)
			OnMMFlyThrough(nFlags, point);
		return;
	}

	UpdateCoordInfo(point);

	if (!(nFlags & (MK_LBUTTON | MK_MBUTTON | MK_RBUTTON)))
		return;

	if (mouseLClick || mouseMClick || mouseRClick) {
		CSize sizeMoved = ptClick - point;

		if (abs(sizeMoved.cx) > GetSystemMetrics(SM_CXDRAG) ||
			abs(sizeMoved.cy) > GetSystemMetrics(SM_CYDRAG)) {

			if (mouseLClick)
				mouseLDrag = true;
			else if (mouseMClick)
				mouseMDrag = true;
			else if (mouseRClick)
				mouseRDrag = true;
			mouseLClick = mouseMClick = mouseRClick = false;
		}
	}

	if (!mouseLDrag && !mouseMDrag && !mouseRDrag)
		return;

	// panning.
	if (mouseMDrag) {
		OnLMBEyeMove(nFlags, point);
		return;
	}

	switch (GMainFrame->GetOpMode()) {
	case ID_MODE_OBJECTSELECT:
		OnMMObjectsSelect(nFlags, point);
		break;
	case ID_MODE_OBJECTMOVE:
		OnMMObjectsMove(nFlags, point);
		break;
	case ID_MODE_OBJECTROTATE:
		OnMMObjectsRotate(nFlags, point);
		break;
	case ID_MODE_OBJECTSCALE:
		OnMMObjectsScale(nFlags, point);
		break;
	case ID_MODE_MODIFYBRUSH:
	case ID_MODE_FACEMOVE:
	case ID_MODE_EDGEMOVE:
	case ID_MODE_VERTEXMOVE:
		OnMMModifyBrush(nFlags, point);
		break;
	case ID_MODE_PLANECLIP:
	case ID_MODE_MIRROR:
		OnMMDefineOpPlane(nFlags, point);
		break;
	case ID_MODE_EYEMOVE:
		if (mouseLDrag)
			OnLMBEyeMove(nFlags, point);
		else if (mouseRDrag)
			OnRMBEyeZoom(nFlags, point);
		break;
	case ID_MODE_EYEROTATE:
		if (mouseLDrag)
			OnLMBEyeRotate(nFlags, point);
		else if (mouseRDrag)
			OnRMBEyeRotate(nFlags, point);
		break;
	case ID_MODE_EYEZOOM:
		if (mouseLDrag || mouseRDrag)
			OnRMBEyeZoom(nFlags, point);
		break;
	default:
		CView::OnMouseMove(nFlags, point);
		break;
	}
}

//======================================================================
// Right Click Menu Stuff.
//======================================================================

void FMapView::OnContextMenu(CWnd* pWnd, CPoint point) {
	UINT nFlag = TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_RIGHTBUTTON;
	ClientToScreen(&point);

	UINT opMode = GMainFrame->GetOpMode();

	if (opMode == ID_MODE_OBJECTSELECT || opMode == ID_MODE_OBJECTMOVE ||
		opMode == ID_MODE_OBJECTROTATE || opMode == ID_MODE_OBJECTSCALE) {
		CMenu *pAddMenu = &(GMainFrame->GetAddObjectMenu());

		// Need to append some commands.
		pAddMenu->AppendMenu(MF_SEPARATOR);

		CMenu rMenu;
		rMenu.LoadMenu(IDR_MAPVIEW_CONTEXT_MB);
		CMenu *pEditMenu = rMenu.GetSubMenu(3)->GetSubMenu(0);

		pAddMenu->AppendMenu(MF_POPUP, (UINT_PTR) pEditMenu->Detach(), "Edit");
		pAddMenu->AppendMenu(MF_SEPARATOR);
		pAddMenu->AppendMenu(MF_STRING, ID_OBJECT_SAVEPREFAB, "Save Prefab...");
		pAddMenu->AppendMenu(MF_SEPARATOR);
		pAddMenu->AppendMenu(MF_STRING, ID_OBJECT_PROPERTIES, "Properties...");

		// Pop the menu
		pAddMenu->TrackPopupMenu(nFlag, point.x, point.y, GMainFrame);

		// Remove the extrs commands.
		pAddMenu->RemoveMenu(4, MF_BYPOSITION); // Separator
		pAddMenu->DeleteMenu(4, MF_BYPOSITION); // Edit pop out

		pAddMenu->RemoveMenu(4, MF_BYPOSITION);  // Separator;
		pAddMenu->RemoveMenu(ID_OBJECT_SAVEPREFAB, MF_BYCOMMAND);
		pAddMenu->RemoveMenu(4, MF_BYPOSITION);  // Separator;
		pAddMenu->RemoveMenu(ID_OBJECT_PROPERTIES, MF_BYCOMMAND); 
	}
	else if (opMode >= ID_MODE_MODIFYBRUSH && opMode <= ID_MODE_VERTEXMOVE) {
		/*
		CMenu rMenu;
		rMenu.LoadMenu(IDR_MAPVIEW_CONTEXT_MB);
		CMenu *pContextMenu = rMenu.GetSubMenu(0);
		for (UINT i = ID_MODE_FACEMOVE; i <= ID_MODE_VERTEXMOVE; i++) {
			pContextMenu->CheckMenuItem(i, MF_BYCOMMAND |
				(modifyBrushMode == i ? MF_CHECKED : MF_UNCHECKED));
		}
		pContextMenu->TrackPopupMenu(nFlag, point.x, point.y, this);
		*/
	}
	else if (opMode == ID_MODE_PLANECLIP) {
		if (qvType == ID_VIEW_NEW3D)
			return;

		CMenu rMenu;
		rMenu.LoadMenu(IDR_MAPVIEW_CONTEXT_MB);
		CMenu *pContextMenu = rMenu.GetSubMenu(1);

		UINT style = MF_BYCOMMAND |
			(planeDefined && !(planeDragPt1 == planeDragPt2) ?
			 MF_ENABLED : MF_GRAYED);
		pContextMenu->EnableMenuItem(ID_RCMMV_PERFORMCLIP, style);
		style = MF_BYCOMMAND | (clipSide == -1 ? MF_CHECKED : MF_UNCHECKED);
		pContextMenu->CheckMenuItem(ID_RCMMV_REMOVELEFT, style);
		style = MF_BYCOMMAND | (clipSide == 0 ? MF_CHECKED : MF_UNCHECKED);
		pContextMenu->CheckMenuItem(ID_RCMMV_SPLITATLINE, style);
		style = MF_BYCOMMAND | (clipSide == 1 ? MF_CHECKED : MF_UNCHECKED);
		pContextMenu->CheckMenuItem(ID_RCMMV_REMOVERIGHT, style);

		pContextMenu->TrackPopupMenu(nFlag, point.x, point.y, this);
	}
	else if (opMode == ID_MODE_MIRROR) {
		if (qvType == ID_VIEW_NEW3D)
			return;

		CMenu rMenu;
		rMenu.LoadMenu(IDR_MAPVIEW_CONTEXT_MB);
		CMenu *pContextMenu = rMenu.GetSubMenu(2);

		UINT style = MF_BYCOMMAND |
			(planeDefined && !(planeDragPt1 == planeDragPt2) ?
			 MF_ENABLED : MF_GRAYED);
		pContextMenu->EnableMenuItem(ID_RCMMV_PERFORMFLIP, style);
		pContextMenu->EnableMenuItem(ID_RCMMV_PERFORMMIRROR, style);

		pContextMenu->TrackPopupMenu(nFlag, point.x, point.y, this);
	}
}

void FMapView::OnRMBUContextMenu(UINT nFlags, CPoint point) {
	if (!mouseRClick && !mouseRDrag)
		return;

	// Check if point is still inside window.
	CRect rect;
	GetClientRect(&rect);
	if (!rect.PtInRect(point))
		return;

	// Generate a context menu message.
	SendMessage(WM_CONTEXTMENU, (WPARAM) m_hWnd,
				(LPARAM) ((point.y << 16) | point.x));
}

//======================================================================
// Idle timer to help with rendering.
//======================================================================

void FMapView::OnTimer(UINT_PTR nIDEvent) {
	CView::OnTimer(nIDEvent);

	if (nIDEvent == TIMER_MV_UPDATEDRAGSEL)
		OnUpdateDragSelect();
	else if (nIDEvent == TIMER_MV_RENDERLOCKEDMAPVIEWS)
		OnTimerUpdateLockedMapViews();
	else if (nIDEvent == TIMER_MV_RENDERMAPVIEWS)
		OnTimerUpdateMapViews();
	else if (nIDEvent == TIMER_MV_SCROLLVIEW)
		OnMapViewScrollView();
	else if (nIDEvent == TIMER_MV_UPDATEVIEWS)
		OnEndViewMove();
}

//======================================================================
// Specific op mode handling procedures.
//======================================================================

//===== Scroll view(s) during operation. =====
#define UPDATETIME_SCROLLVIEW			100
#define SCROLLVIEW_SCROLLPIXELS			32

void FMapView::OnMapViewScrollView(void) {
	// Sanity.
	ASSERT(scrollingView);

	CPoint pt(SCROLLVIEW_SCROLLPIXELS, 0);
	float dist, y;

	DU2LU(pt, dist, y);

	CRect rect;
	GetClientRect(&rect);

	Vector3d moveVec;
	if (scrollPos.x < rect.left) {
		moveVec.SetX(-dist);
		lastMPos.x += SCROLLVIEW_SCROLLPIXELS;
	}
	else if (scrollPos.x > rect.right) {
		moveVec.SetX(dist);
		lastMPos.x -= SCROLLVIEW_SCROLLPIXELS;
	}
	if (scrollPos.y < rect.top) {
		moveVec.SetY(dist);
		lastMPos.y += SCROLLVIEW_SCROLLPIXELS;
	}
	else if (scrollPos.y > rect.bottom) {
		moveVec.SetY(-dist);
		lastMPos.y -= SCROLLVIEW_SCROLLPIXELS;
	}

	downClickVec.SubVector(moveVec);
	moveVec.NewVector(moveVec.GetX(), 0.0f, moveVec.GetY());
	Vector3d oldMoveVec(moveVec);

	if (GMainFrame->IsLockedView(this)) { // move affects other views.
		Matrix44 trans;
		trans.SetRotate(pView->GetOrientation());
		trans.Transform(moveVec);

		Vector3d editPos = GMainFrame->GetEditFocusPos();
		editPos.AddVector(moveVec);
		GMainFrame->SetEditFocusPos(editPos, false);
	}
	else { // Just move self.
		pView->MoveRelPosition(moveVec);
	}

	if (scrollingView == ID_MODE_OBJECTSELECT) {
		OnMMObjectsSelect(0,  scrollPos);
	}
	else if (scrollingView == ID_MODE_OBJECTMOVE) {
		OnMMObjectsMove(0, scrollPos);
		// For snapping.
		pView->CalInvTransSpaceMatrix(tempInvViewMatrix.SetIdentity());
	}
	else if (scrollingView == ID_MODE_OBJECTSCALE) {
		OnMMObjectsScale(0, scrollPos);
	}
	else if (scrollingView == ID_MODE_MODIFYBRUSH) {
		tempSnapVecTtl.SubVector(oldMoveVec);
		tempSnapVec.SubVector(oldMoveVec);
		OnMMModifyBrush(0, scrollPos);
	}
}

//===== Timer Update Unfocused map views =====
#define UPDATETIME_RENDERMAPVIEWS			50

void FMapView::SetTimerUpdateMapViews(bool set) {
	if (set)
		SetTimer(TIMER_MV_RENDERMAPVIEWS, UPDATETIME_RENDERMAPVIEWS, NULL);
	else
		KillTimer(TIMER_MV_RENDERMAPVIEWS);
}

void FMapView::OnTimerUpdateMapViews(void) {
	FEditorDoc *pDoc = GetDocument();
	pDoc->UpdateAllViews(this, DUAV_MAPVIEWS, NULL);
}

//===== Selection =====
#define UPDATETIME_DRAGSEL				500

void FMapView::OnLMBDObjectsSelect(UINT nFlags, CPoint point) {
	scrollingView = 0;
	selectPending = false;
	selectAddPending = false;
	dragSelect = false;
	dragAddSelect = false;
	reverseSelect = (::GetAsyncKeyState(VK_MENU) > 0);

	Selector *pSlct = &(GMainFrame->GetSelector());

	if (!(nFlags & MK_CONTROL) && !(nFlags & MK_SHIFT)) {
		// Deal with selection at button release.
		selectPending = true;
	}
	else if (nFlags & MK_CONTROL) {
		// Cycle the Ctrl-Add hi-lite obj.
		selectAddPending = true;

		Object *pSelObj;
		float x = downClickVec.GetX();
		float y = downClickVec.GetY();

		if (!reverseSelect)
			pSelObj = pView->SingleForwardSelect(x, y);
		else
			pSelObj = pView->SingleReverseSelect(x, y);
		pSlct->SSelectObject(pSelObj);

		// Update all qviews.
		FEditorDoc *pDoc = GetDocument();
		pDoc->UpdateAllViews(NULL, DUAV_OBJSSEL | DUAV_MAPVIEWS,
							 (CObject *) pSlct->GetScopePtr());
	}
	else if (nFlags & MK_SHIFT) {
		// Drag box to add objs to selection.
		dragAddSelect = true;

		// Remember the old selection.
		ASSERT(oldSelection.NumOfElm() == 0);
		oldSelection = pSlct->GetMSelectedObjects();
	}
}

void FMapView::OnMMObjectsSelect(UINT nFlags, CPoint point) {
	if (!mouseLDrag)
		return;

	if (selectPending)
		selectPending = false;

	if (GMainFrame->GetOpMode() == ID_MODE_OBJECTSELECT &&
		!dragSelect && !dragAddSelect && !selectAddPending) {
		
		// Drag box to set new selection
		dragSelect = true;

		// Remember old selection.
		ASSERT(oldSelection.NumOfElm() == 0);
		Selector *pSlct = &(GMainFrame->GetSelector());
		oldSelection = pSlct->GetMSelectedObjects();
	}

	if (!dragSelect && !dragAddSelect)
		return;

	// Need to scroll the view(s) and clip coord
	//  if mouse is outside the window.
	CRect rect;
	GetClientRect(&rect);

	if (!rect.PtInRect(point)) {
		// Set the scroll update timer.
		if (!scrollingView && qvType != ID_VIEW_NEW3D) {
			scrollingView = ID_MODE_OBJECTSELECT;
			SetTimer(TIMER_MV_SCROLLVIEW, UPDATETIME_SCROLLVIEW, NULL);
		}
		// Used to determine direction of scrolling.
		scrollPos = point;

		// Need to clip the release coord.
		point.x = Max(rect.left, Min(point.x, rect.right));
		point.y = Max(rect.top, Min(point.y, rect.bottom));
	}
	else {
		if (scrollingView) { // Kill the scroll update timer
			KillTimer(TIMER_MV_SCROLLVIEW);
			scrollingView = 0;
		}

		// Reset the idle timer to update drag selection.
		SetTimer(TIMER_MV_UPDATEDRAGSEL, UPDATETIME_DRAGSEL, NULL);
	}

	// Convert down and up click pts.
	LC2DC(downClickVec.GetX(), downClickVec.GetY(), ptClick);

	float x, y;
	DC2LC(point, x, y);
	upClickVec.NewVector(x, y, 0.0f);

	// Need to update drag box's drawing.
	int x1, y1, x2, y2;
	x1 = Min(ptClick.x, point.x);
	y1 = Min(ptClick.y, point.y);
	x2 = Max(ptClick.x, point.x);
	y2 = Max(ptClick.y, point.y);

	dragBoxRect.SetRect(x1, y1, x2, y2);
	OnUpdate(NULL, DUMV_DISPLAYDRAGBOX, NULL);
}

void FMapView::OnLMBUObjectsSelect(UINT nFlags, CPoint point) {
	if (!selectPending && !dragSelect && !dragAddSelect)
		return;

	// Disable scrolling.
	if (scrollingView) {
		scrollingView = 0;
		KillTimer(TIMER_MV_SCROLLVIEW);
	}

	Selector *pSlct = &(GMainFrame->GetSelector());

	if (selectPending) {
		// New selection.
		ASSERT(!mouseLDrag);
		ASSERT(pSlct->GetSSelectedObject() == NULL);

		Object *pSelObj;
		float x, y;
		DC2LC(point, x, y);

		if (!reverseSelect)
			pSelObj = pView->MultiForwardSelect(x, y);
		else
			pSelObj = pView->MultiReverseSelect(x, y);

		// Clear the sel buf.
		slctObjsBuf.DeleteAllNodes();
		if (pSelObj != NULL)
			slctObjsBuf.AppendNode(*(new ObjectPtr(pSelObj)));

		// Commit the operation.
		OpSelectNewObjs *op = new OpSelectNewObjs(slctObjsBuf);
		GMainFrame->CommitOperation(*op);
	}
	else if (dragSelect) {
		// Update selection buffer one last time.
		OnUpdateDragSelect();

		// Restore the old selector.
		pSlct->MSelectObjects(oldSelection);
		oldSelection.DeleteAllNodes();

		// Commit the operation.
		OpSelectNewObjs *op = new OpSelectNewObjs(slctObjsBuf);
		GMainFrame->CommitOperation(*op);
	}
	else if (dragAddSelect) {
		// Update the selection buffer one last time.
		OnUpdateDragSelect();

		// Restore the old selector.
		pSlct->MSelectObjects(oldSelection);
		oldSelection.DeleteAllNodes();

		// Commit the operation.
		if (slctObjsBuf.NumOfElm() > 0) {
			OpSelectAddObjs *op = new OpSelectAddObjs(slctObjsBuf);
			GMainFrame->CommitOperation(*op);
		}
		else { // Need to update self view to clear the drag box.
			OnUpdate(NULL, NULL, NULL);
		}
	}

	selectPending = false;
	dragSelect = false;
	dragAddSelect = false;
}

// Need to catch the release of control key.
// Use this chance to add the Ctrl-Add hi-lited
//  obj into selector.a
void FMapView::OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags) {
	// Default action.
	CView::OnKeyUp(nChar, nRepCnt, nFlags);

	// Handle the ctrl-add selection.
	if (nChar != VK_CONTROL || !selectAddPending)
		return;

	selectAddPending = false;

	Selector *pSlct = &(GMainFrame->GetSelector());
	Object *pSlctObj = pSlct->GetSSelectedObject();

	if (pSlctObj != NULL) {
		// Clear single selection.
		pSlct->SUnselect();

		// Add to selection.
		slctObjsBuf.DeleteAllNodes();
		slctObjsBuf.AppendNode(*(new ObjectPtr(pSlctObj)));

		// Commit operation
		OpSelectAddObjs *op = new OpSelectAddObjs(slctObjsBuf);
		GMainFrame->CommitOperation(*op);
	}
}

// Called during dragging box selection when mouse has 
//  stopped moving for UPDATE_DRAGSEL_TIME millisecs.
// Use this chance to update the selection inside
//  the new drag box.
void FMapView::OnUpdateDragSelect(void) {
	// Sanity.
	ASSERT(dragSelect || dragAddSelect);

	// Kill the old timer.
	KillTimer(TIMER_MV_UPDATEDRAGSEL);

	Selector *pSlct = &(GMainFrame->GetSelector());
	float x1, x2, y1, y2;

	// Get all the objs inside the drag box.
	x1 = Min(downClickVec.GetX(), upClickVec.GetX());
	y1 = Min(downClickVec.GetY(), upClickVec.GetY());
	x2 = Max(downClickVec.GetX(), upClickVec.GetX());
	y2 = Max(downClickVec.GetY(), upClickVec.GetY());

	slctObjsBuf.DeleteAllNodes();
	pView->DragSelectMultiObjects(x1, y1, x2, y2, slctObjsBuf);

	LinkList<ObjectPtr> tmpSlctBuf;

	if (dragSelect)
		tmpSlctBuf = slctObjsBuf;

	if (dragAddSelect) {
		// Remove duplicate selected objs from slctObjsBuf.
		ObjectPtr *pObjPtr;
		IterLinkList<ObjectPtr> iterNew(slctObjsBuf);
		IterLinkList<ObjectPtr> iterOld(oldSelection);
		iterNew.Reset();
		while (!iterNew.IsDone()) {
			pObjPtr = iterNew.GetNext();
			iterOld.Reset();
			while (!iterOld.IsDone()) {
				if (iterOld.GetNext()->GetPtr() == pObjPtr->GetPtr()) {
					delete &(slctObjsBuf.RemoveNode(*pObjPtr));
					break;
				}
			}
		}

		tmpSlctBuf = slctObjsBuf;

		// Add the original selection back into the select buf.
		LinkList<ObjectPtr> tmpOldBuf(oldSelection);
		while (tmpOldBuf.NumOfElm() > 0)
			tmpSlctBuf.AppendNode(tmpOldBuf.RemoveNode(0));
	}

	// Select the objs.
	pSlct->MSelectObjects(tmpSlctBuf);
	tmpSlctBuf.DeleteAllNodes();

	// Update self.
	OnUpdate(NULL, DUMV_DISPLAYDRAGBOX, NULL);

	// Update other qviews.
	FEditorDoc *pDoc = GetDocument();
	pDoc->UpdateAllViews(this, DUAV_OBJSSEL | DUAV_MAPVIEWS,
						 (CObject *) pSlct->GetScopePtr());
}

//===== Snapping to the world coord =====

void FMapView::SetSnapScope(Object &snapScope) {
	Object *pRoot = GetDocument()->GetRootObjectPtr();
	Object::GetTransMatrix(snapScope, *pRoot, tempSnapMatrix);
	Object::GetTransMatrix(*pRoot, snapScope, tempSnapInvMatrix);
}

Vector3d &FMapView::SnapVector2World(Vector3d &snapVec, const Vector3d *pLockVec) {
	// Transform to world.
	tempSnapMatrix.Transform(snapVec);

	bool rx, ry, rz;
	rx = ry = rz = true;

	if (pLockVec != NULL) {
		Vector3d oVec(0.0f, 0.0f, 0.0f);
		Vector3d lVec(*pLockVec);
		
		tempSnapMatrix.Transform(oVec);
		tempSnapMatrix.Transform(lVec);
		lVec.SubVector(oVec);

		rx = (ROUND3(lVec.GetX()) != 0.0f);
		ry = (ROUND3(lVec.GetY()) != 0.0f);
		rz = (ROUND3(lVec.GetZ()) != 0.0f);
	}

	// Snap it to grid.
	float x, y, z;

	x = snapVec.GetX();
	if (rx) {
		x /= gridSnapVal;
		x = ROUND(x) * gridSnapVal;
	}

	y = snapVec.GetY();
	if (ry) {
		y /= gridSnapVal;
		y = ROUND(y) * gridSnapVal;
	}

	z = snapVec.GetZ();
	if (rz) {
		z /= gridSnapVal;
		z = ROUND(z) * gridSnapVal;
	}

	// Transform it back.
	tempSnapInvMatrix.Transform(snapVec, x, y, z);

	return snapVec;
}

void FMapView::SnapAddObjPos(Vector3d &addVec) {
	if (gridSnap) {
		SetSnapScope(*(pView->GetViewObjPtr()));
		SnapVector2World(addVec);
	}
}

//===== Edit Menu: Snap Alignment =====
void FMapView::OnEditAlignUI(CCmdUI *pCmdUI) {
	if (qvType == ID_VIEW_NEW3D) {
		pCmdUI->Enable(FALSE);
		return;
	}

	Selector *pSlctr = &(GMainFrame->GetSelector());
	if (pSlctr->GetNumMSelectedObjects() == 0)
		pCmdUI->Enable(FALSE);
	else
		pCmdUI->Enable(TRUE);
}

void FMapView::OnEditAlignSet() {
	// Just snap the op center to grid.
	SetSnapScope(*(pView->GetViewObjPtr()));
	SnapVector2World(opCenterVec);
	
	FEditorDoc *pDoc = GetDocument();
	pDoc->UpdateAllViews(NULL, DUAV_MAPVIEWS, NULL);
}

void FMapView::OnEditAlignSelection() {
 	ASSERT(qvType != ID_VIEW_NEW3D);

	// Get selections bounding vectors.
	Object *pNewObj, *pTmpObj = new Object;
	Selector *pSlctr = &(GMainFrame->GetSelector());
	IterLinkList<ObjectPtr> iter(pSlctr->GetMSelectedObjects());
	iter.Reset();
	while (!iter.IsDone()) {
		pNewObj = new Object(*(iter.GetNext()->GetPtr()));
		pTmpObj->AddChild(*pNewObj, false);
	}
	pTmpObj->SetBoundRadius();

	Vector3d minVec, maxVec;
	pTmpObj->GetBoundingVectors(minVec, maxVec);

	delete pTmpObj;

	UINT msg = (UINT) LOWORD (GetCurrentMessage()->wParam);
	if (msg == ID_EDIT_ALIGNCENTER) {
		opCenterVec.AddVector(minVec, maxVec);
		opCenterVec.MultVector(0.5f);
	}
	else if (msg == ID_EDIT_ALIGNTOP || msg == ID_EDIT_ALIGNBOTTOM) {
		if (qvType == ID_VIEW_NEWTOP)
			opCenterVec.SetY(msg == ID_EDIT_ALIGNTOP ? maxVec.GetY() : minVec.GetY());
		else
			opCenterVec.SetZ(msg == ID_EDIT_ALIGNTOP ? maxVec.GetZ() : minVec.GetZ());
	}
	else {
		if (qvType != ID_VIEW_NEWSIDE)
			opCenterVec.SetX(msg == ID_EDIT_ALIGNLEFT ? minVec.GetX() : maxVec.GetX());
		else
			opCenterVec.SetY(msg == ID_EDIT_ALIGNLEFT ? minVec.GetY() : maxVec.GetY());
	}

	// Remember view states with new op center.
	ASSERT(qvsBefore == NULL);
	qvsBefore = new FMapViewsState();

	// Find the snap position.
	Vector3d oldPos(opCenterVec), deltaVec;

	SetSnapScope(*(pView->GetViewObjPtr()));
	SnapVector2World(opCenterVec);
	deltaVec.SubVector(opCenterVec, oldPos);

	// Find the move vector.
	Vector3d moveVec;

	if (msg == ID_EDIT_ALIGNCENTER) {
		SphrVector oriVec;
		pView->GetOrientation(oriVec);

		Matrix44 trans;
		trans.SetInvRotate(oriVec);

		moveVec = deltaVec;
		trans.Transform(moveVec);

		oldPos = opCenterVec;
	}
	else if (msg == ID_EDIT_ALIGNTOP || msg == ID_EDIT_ALIGNBOTTOM) {
		if (qvType == ID_VIEW_NEWTOP) {
			moveVec.NewVector(0.0f, 0.0f, deltaVec.GetY());
			oldPos.SetY(oldPos.GetY() + deltaVec.GetY());
		}
		else {
			moveVec.NewVector(0.0f, 0.0f, deltaVec.GetZ());
			oldPos.SetZ(oldPos.GetZ() + deltaVec.GetZ());
		}
	}
	else {
		if (qvType != ID_VIEW_NEWSIDE) {
			moveVec.NewVector(deltaVec.GetX(), 0.0f, 0.0f);
			oldPos.SetX(oldPos.GetX() + deltaVec.GetX());
		}
		else {
			moveVec.NewVector(deltaVec.GetY(), 0.0f, 0.0f);
			oldPos.SetY(oldPos.GetY() + deltaVec.GetY());
		}
	}
	opCenterVec = oldPos;

	FEditorDoc *pDoc = GetDocument();

	if (moveVec != Vector3d::origVec) {
		// Move the selection.
		pDoc->ObjectsMove(pSlctr->GetMSelectedObjects(), *pView, moveVec);

		// Commit the operation.
		OpSelectionMove *op =
			new OpSelectionMove(*pView, moveVec, qvsBefore);
		GMainFrame->CommitOperation(*op);
	}
	else {
		// Update qviews for the opCenter change.
		pDoc->UpdateAllViews(NULL, DUAV_MAPVIEWS, NULL);
		delete qvsBefore;
	}
	qvsBefore = NULL;
}

//===== Translate opCenterVec to and from device coord =====
#define DRAG_CLICKPOS_EPSILON			5

void FMapView::FindOpCenterPos(CPoint &opCenterPos, Vector3d *pCenterVec) {
	Matrix44 m;
	pView->CalInvTransSpaceMatrix(m.SetIdentity());

	Vector3d centerVec;
	m.Transform(centerVec, opCenterVec);

	if (qvType != ID_VIEW_NEW3D)
		LC2DC(centerVec.GetX(), centerVec.GetZ(), opCenterPos);
	else
		LC2DC(centerVec, opCenterPos);

	if (pCenterVec)
		*pCenterVec = centerVec;
}

void FMapView::DragOpCenterPos(CPoint point, bool updateView) {
	// Need to clip mouse position.
	CRect rect;
	GetClientRect(&rect);
	if (!rect.PtInRect(point)) {
		point.x = Max(rect.left, Min(point.x, rect.right));
		point.y = Max(rect.top, Min(point.y, rect.bottom));
	}

	// Get the amount of mouse's movement.
	CPoint mouseMove(point);
	mouseMove -= lastMPos;
	lastMPos = point;

	float x, y;
	DU2LU(mouseMove, x, y);

	// check for edit constraints.
	if (qvType != ID_VIEW_NEW3D && editConstraint == 1)
		y = 0.0f;   // Horizontal lock.
	else if (qvType != ID_VIEW_NEW3D && editConstraint == 2)
		x = 0.0f;		// Vertical lock.

	Vector3d moveVec(x, 0.0f, y);

	Matrix44 m;
	pView->CalTransSpaceMatrix(m.SetIdentity());
	(m.Transform(moveVec)).SubVector(pView->GetPosition());

	tempSnapVec.AddVector(moveVec);

	bool viewChanged = false;
	bool snap = (gridSnap && (::GetAsyncKeyState(VK_MENU) >> 31) == 0);

	if (!snap) {
		viewChanged = true;
		opCenterVec = tempSnapVec;
	}
	else {
		// Edit contraint
		Vector3d lockVec, *pLockVec = NULL;

		if (editConstraint == 1)
			lockVec.NewVector(1.0f, 0.0f, 0.0f);
		else if (editConstraint == 2)
			lockVec.NewVector(0.0f, 0.0f, 1.0f);

		if (qvType != ID_VIEW_NEW3D && editConstraint != 0) {
			pLockVec = &lockVec;
			(m.Transform(lockVec)).SubVector(pView->GetPosition());
		}

		// Transform op center pos to abs world coord.
		Vector3d tempNewPos(tempSnapVec);
		SnapVector2World(tempNewPos, pLockVec);

		if (!(tempNewPos == opCenterVec)) {
			opCenterVec = tempNewPos;
			viewChanged = true;
		}
	}

	// Update self.
	if (viewChanged && updateView)
		OnUpdate(NULL, NULL, NULL);
}

//===== Button Down: prepare for operation =====
void FMapView::OnLMBDObjectsMoveRotateScale(UINT nFlags, CPoint point) {
	OnLMBDObjectsSelect(nFlags, point);
	
	scrollingView = 0;
	operatePending = false;
	dragOperate = false;
	dragOpCenter = false;

	if (selectAddPending || dragAddSelect)
		return;

	lastMPos = ptClick;

	// Remember op center for scale operation.
	oldOpCenterVec = opCenterVec;

	// Test for click on manip dots in scale mode.
	if (GMainFrame->GetOpMode() == ID_MODE_OBJECTSCALE) {
		drawScaleBoundBox = false;
		dragScaleManipDot = false;

		CPoint mPos;
		for(int i = 0; i < 8; i++) {
			if (qvType != ID_VIEW_NEW3D)
				LC2DC(scaleManipDots[i].GetX(), scaleManipDots[i].GetZ(), mPos);
			else
				LC2DC(scaleManipDots[i], mPos);
			mPos -= point;
			if (ABS(mPos.x) <= DRAG_CLICKPOS_EPSILON &&
				ABS(mPos.y) <= DRAG_CLICKPOS_EPSILON) {
				Matrix44 m;
				pView->CalTransSpaceMatrix(m.SetIdentity());
				i = (i + 4) % 8;
				m.Transform(opCenterVec, scaleManipDots[i]);
				dragScaleManipDot = true;
				operatePending = true;
				return;
			}
		}
	}

	// Test for click on op center.
	CPoint opCenterPos;
	FindOpCenterPos(opCenterPos);
	point -= opCenterPos;

	if (GMainFrame->GetOpMode() != ID_MODE_OBJECTMOVE &&
		ABS(point.x) <= DRAG_CLICKPOS_EPSILON  &&
		ABS(point.y) <= DRAG_CLICKPOS_EPSILON) {
		dragOpCenter = true;
		selectPending = false;

		// Need to turn on timer rendering for the unfocused qviews.
		SetTimerUpdateMapViews(true);

		// Set up snapping stuff.
		tempSnapVec = opCenterVec;
		SetSnapScope(*(pView->GetViewObjPtr()));
	}
	else if (pView->IsMultiSelectionHit(downClickVec.GetX(),
										downClickVec.GetY())) {
		operatePending = true;
	}
}

//===== Move =====
void FMapView::OnMMObjectsMove(UINT nFlags, CPoint point) {
	OnMMObjectsSelect(nFlags, point);

	// Sanity.
	ASSERT(!dragSelect);

	// Used to snap the op center upon opcenter drag.
	// if (dragOpCenter) { // Need to update opCenter's display.
	// 	DragOpCenterPos(point);
	// 	return;
	// }

	if (operatePending) {
		selectPending = false;
		operatePending = false;
		dragOperate = true;

		// Need to turn on timer rendering for the unfocused qviews.
		SetTimerUpdateMapViews(true);

		// Turn on wireframe approximation rendering.
		View::ApproxItems(true);
		
		// Set up snapping stuff.
		tempSnapVec = opCenterVec;
		tempSnapVecTtl.NewVector(0.0f, 0.0f, 0.0f);
		SetSnapScope(*(pView->GetViewObjPtr()));
		pView->CalInvTransSpaceMatrix(tempInvViewMatrix.SetIdentity());

		// Snap the op center to grid.
		// DragOpCenterPos(point);
		
		// Update the status bar
		CString mesg;
		mesg.LoadString(ID_STATUS_ALTSNAPOFF);
		GMainFrame->UpdateStatusBar(mesg);

		// Remember view states.
		ASSERT(qvsBefore == NULL);
		qvsBefore = new FMapViewsState();

		// Duplicate drag.
		if ((::GetAsyncKeyState(VK_SPACE) >> 31) != 0) {
			Vector3d dupVec(0.0f, 0.0f, 0.0f);
			OpEditDuplicate *op = new OpEditDuplicate(dupVec);
			GMainFrame->CommitOperation(*op);
		}
	}
	
	if (!dragOperate)
		return;

	// Clip the mouse coord and scroll view.
	CRect rect;
	GetClientRect(&rect);

	if (!rect.PtInRect(point)) {
		if (!scrollingView) {
			scrollingView = ID_MODE_OBJECTMOVE;
 			SetTimer(TIMER_MV_SCROLLVIEW, UPDATETIME_SCROLLVIEW, NULL);
		}
		scrollPos = point;

		// clip.
		point.x = Max(rect.left, Min(point.x, rect.right));
		point.y = Max(rect.top, Min(point.y, rect.bottom));
	}
	else if (scrollingView) {  // stop scrolling view.
		KillTimer(TIMER_MV_SCROLLVIEW);
		scrollingView = 0;
	}

	// Move the op center first.
	Vector3d oldOpPos(opCenterVec);
	DragOpCenterPos(point, false);

	if (!(oldOpPos == opCenterVec)) {
		Vector3d deltaVec(opCenterVec);
		tempInvViewMatrix.Transform(deltaVec);
		tempInvViewMatrix.Transform(oldOpPos);
		deltaVec.SubVector(oldOpPos);

		FEditorDoc *pDoc = GetDocument();
		Selector *pSlct = &(GMainFrame->GetSelector());

		// Move the selected objs.
		pDoc->ObjectsMove(pSlct->GetMSelectedObjects(),
						  *pView, deltaVec, false);
		tempSnapVecTtl.AddVector(deltaVec);

		// Update only self.
		OnUpdate(NULL, NULL, NULL);
	}
}

void FMapView::OnLMBUObjectsMove(UINT nFlags, CPoint point) {
	OnLMBUObjectsSelect(nFlags, point);

	if (!dragOperate && !dragOpCenter)
		return;

	// Need to turn off timer rendering for unfocused qviews.
	SetTimerUpdateMapViews(false);

	// Turn off wireframe approximation rendering.
	View::ApproxItems(false);

	if (dragOpCenter) {
		// Do one last update on the other qviews.
		OnTimerUpdateMapViews();
		dragOpCenter = false;
		return;
	}

	if (scrollingView) {  // Turn off scrolling.
		scrollingView = 0;
		KillTimer(TIMER_MV_SCROLLVIEW);
	}

	Selector *pSlct = &(GMainFrame->GetSelector());
	FEditorDoc *pDoc = GetDocument();

	// Commit the operation.
	OpSelectionMove *op =
		new OpSelectionMove(*pView, tempSnapVecTtl, qvsBefore);
	GMainFrame->CommitOperation(*op);
	qvsBefore = NULL;

	// Update all views.
	pDoc->UpdateAllViews(NULL, DUAV_OBJSMODATTRIB,
						 (CObject *) pSlct->GetScopePtr());
	
	dragOperate = false;

	// Update the status bar
	CString mesg;
	mesg.LoadString(AFX_IDS_IDLEMESSAGE);
	GMainFrame->UpdateStatusBar(mesg);
}

//===== Rotate =====
void FMapView::OnMMObjectsRotate(UINT nFlags, CPoint point) {
	OnMMObjectsSelect(nFlags, point);

	// Sanity.
	ASSERT(!dragSelect);

	if (dragOpCenter) {  // Need to update the opCenter's display.
		DragOpCenterPos(point);
		return;
	}

	if (operatePending) {
		selectPending = false;
		operatePending = false;
		dragOperate = true;

		// Need to turn on timer rendering for the unfocused qviews.
		SetTimerUpdateMapViews(true);

		// Turn on wireframe approximation rendering.
		View::ApproxItems(true);

		// Snapping.
		tempSnapRotVal = 0.0f;
		tempLastSnapRotVal = 0.0f;

		// Update the status bar
		CString mesg;
		mesg.LoadString(ID_STATUS_ALTSNAPOFF);
		GMainFrame->UpdateStatusBar(mesg);

		// qvs
		ASSERT(qvsBefore == NULL);
		qvsBefore = new FMapViewsState();
	}

	if (!dragOperate)
		return;

	CPoint opCenterPos;
	Vector3d rotPt;
	FindOpCenterPos(opCenterPos, &rotPt);

	CPoint lastVec(lastMPos);
	lastVec -= opCenterPos;

	lastMPos = point;
	point -= opCenterPos;

	float rotAngle = ATan((float) -lastVec.y, (float) lastVec.x) -
						ATan((float) -point.y, (float) point.x);
	tempSnapRotVal += rotAngle;

	bool snap = (rotSnap && (::GetAsyncKeyState(VK_MENU) >> 31) == 0);

	if (snap) {
		float toBe = tempSnapRotVal / DEG2RAD(rotSnapVal);
		toBe = ROUND(toBe) * DEG2RAD(rotSnapVal);
		rotAngle = toBe - tempLastSnapRotVal;
	}
	else {
		rotAngle = tempSnapRotVal - tempLastSnapRotVal;
	}
	tempLastSnapRotVal += rotAngle;

	// Rotate the selected objs.
	FEditorDoc *pDoc = GetDocument();
	Selector *pSlct = &(GMainFrame->GetSelector());
	pDoc->ObjectsRotate(pSlct->GetMSelectedObjects(),
						*pView, rotPt, rotAngle, false);

	// Update only self.
	OnUpdate(NULL, NULL, NULL);
}

void FMapView::OnLMBUObjectsRotate(UINT nFlags, CPoint point) {
	OnLMBUObjectsSelect(nFlags, point);

	if (!dragOperate && !dragOpCenter)
		return;

	// Need to turn off timer rendering for unfocused qviews.
	SetTimerUpdateMapViews(false);

	// Turn off wireframe approximation rendering.
	View::ApproxItems(false);

	if (dragOpCenter) {
		// Do one last update on the other qviews.
		OnTimerUpdateMapViews();
		dragOpCenter = false;
		return;
	}

	CPoint opCenterPos;
	Vector3d rotPt;
	FindOpCenterPos(opCenterPos, &rotPt);

	// Commit the operation.
	Selector *pSlct = &(GMainFrame->GetSelector());
	FEditorDoc *pDoc = GetDocument();

	OpSelectionRotate *op =
		new OpSelectionRotate(*pView, rotPt, tempLastSnapRotVal, qvsBefore);
	GMainFrame->CommitOperation(*op);
	qvsBefore = NULL;

	// Update all views.
	pDoc->UpdateAllViews(NULL, DUAV_OBJSMODATTRIB,
						 (CObject *) pSlct->GetScopePtr());

	dragOperate = false;

	// Update the status bar
	CString mesg;
	mesg.LoadString(AFX_IDS_IDLEMESSAGE);
	GMainFrame->UpdateStatusBar(mesg);
}

//===== Scale =====
void FMapView::OnMMObjectsScale(UINT nFlags, CPoint point) {
	OnMMObjectsSelect(nFlags, point);

	// Sanity.
	ASSERT(!dragSelect);

	if (dragOpCenter) {  // Need to update the opCenter's display.
		DragOpCenterPos(point);
		return;
	}

	if (!dragOperate && !operatePending)
		return;

	// Get the standard ref ptrs.
	FEditorDoc* pDoc = GetDocument();
	Selector *pSlct = &(GMainFrame->GetSelector());

	// Convert opCenter to view space.
	CPoint opCenterPos;
	Vector3d basisVec;
	FindOpCenterPos(opCenterPos, &basisVec);

	if (operatePending) {
		selectPending = false;
		operatePending = false;
		dragOperate = true;

		// Need to turn on timer rendering for unfocused qviews.
		SetTimerUpdateMapViews(true);

		// Turn on wireframe approximation rendering.
		View::ApproxItems(true);

		// Begin the scale operation.
		Object *pOpObj;
		pOpObj = &(pDoc->ObjectsScaleBegin(pSlct->GetMSelectedObjects(),
										   *pView, basisVec));
		// Set up snapping stuff.
		SetSnapScope(*pOpObj);
		tempScaleVec.NewVector(1.0f, 1.0f, 1.0f);
		tempSnapBoundVec1 = pOpObj->GetMinBoundVec();
		tempSnapBoundVec2 = pOpObj->GetMaxBoundVec();

		// Update the status bar
		CString mesg;
		mesg.LoadString(ID_STATUS_ALTSNAPOFF);
		GMainFrame->UpdateStatusBar(mesg);

		// Remember view states.
		Vector3d tmpVec(opCenterVec);
		opCenterVec = oldOpCenterVec;

		ASSERT(qvsBefore == NULL);
		qvsBefore = new FMapViewsState();

		opCenterVec = tmpVec;
	}

	// Clip the mouse coord and scroll view.
	CRect rect;
	GetClientRect(&rect);
	if (!rect.PtInRect(point)) {
		if (!scrollingView) {
			scrollingView = ID_MODE_OBJECTSCALE;
			SetTimer(TIMER_MV_SCROLLVIEW, UPDATETIME_SCROLLVIEW, NULL);
		}
		scrollPos = point;

		point.x = Max(rect.left, Min(point.x, rect.right));
		point.y = Max(rect.top, Min(point.y, rect.bottom));
	}
	else {  // stop scrolling view.
		KillTimer(TIMER_MV_SCROLLVIEW);
		scrollingView = 0;
	}

	// Get the downclick vec and the current mouse vec.
	LC2DC(downClickVec.GetX(), downClickVec.GetY(), ptClick);
	ptClick -= opCenterPos;
	point -= opCenterPos;

	// Calculate the new scale vec.
	float sx, sz;

	if (ABS(ptClick.x) < 5)  // Disable horizontal scaling.
		sx = 1.0f;
	else if (editConstraint == 2) // Vertical lock.  Disable horizontal scale.
		sx = 1.0f;
	else {
		if (point.x * ptClick.x < 0 || ABS(point.x) < 5)
			point.x = (ptClick.x < 0 ? -5 : 5);  // Prevent negative scaling.
		sx = ((float) point.x) / ptClick.x;
	}

	if (ABS(ptClick.y) < 5) // Disable vertical scaling.
		sz = 1.0f;
	else if (editConstraint == 1) // Horizontal lock.  Disable Vertical Scale.
		sz = 1.0f;
	else {
		if (point.y * ptClick.y < 0 || ABS(point.y) < 5)
			point.y = (ptClick.y < 0 ? -5 : 5);  // Prevent negative scaling.
		sz = ((float) point.y) / ptClick.y;
	}

	bool snap = (gridSnap && (::GetAsyncKeyState(VK_MENU) >> 31) == 0);
	if (snap) {
		// Snap min bounding vec.
		Vector3d newBoundVec1(tempSnapBoundVec1.GetX() * sx,
							  tempSnapBoundVec1.GetY(),
							  tempSnapBoundVec1.GetZ() * sz);
		SnapVector2World(newBoundVec1);

		// Snap max bounding vec.
		Vector3d newBoundVec2(tempSnapBoundVec2.GetX() * sx,
							  tempSnapBoundVec2.GetY(),
							  tempSnapBoundVec2.GetZ() * sz);
		SnapVector2World(newBoundVec2);

		// Derive new scale vector.
		if (sx != 1.0f) {
			if (ptClick.x < 0) {
				sx = (newBoundVec1.GetX() >= -1.0f ?
					  tempScaleVec.GetX() : 
					  newBoundVec1.GetX() / tempSnapBoundVec1.GetX());
			}
			else {
				sx = (newBoundVec2.GetX() < 1.0f ?
					  tempScaleVec.GetX() :
					  newBoundVec2.GetX() / tempSnapBoundVec2.GetX());
			}
		}

		if (sz != 1.0f) {
			if (ptClick.y > 0) {
				sz = (newBoundVec1.GetZ() >= -1.0f ?
					  tempScaleVec.GetZ() :
					  newBoundVec1.GetZ() / tempSnapBoundVec1.GetZ());
			}
			else {
				sz = (newBoundVec2.GetZ() < 1.0f ?
					  tempScaleVec.GetZ() :
					  newBoundVec2.GetZ() / tempSnapBoundVec2.GetZ());
			}
		}
	}

	tempScaleVec.NewVector(sx, 1.0f, sz);

	// Set the new scale.
	pDoc->ObjectsScaleChange(pSlct->GetMSelectedObjects(), tempScaleVec, false);

	// Update only self.
	OnUpdate(NULL, NULL, NULL);
}

void FMapView::OnLMBUObjectsScale(UINT nFlags, CPoint point) {
	drawScaleBoundBox = true;
	OnLMBUObjectsSelect(nFlags, point);

	if (operatePending) {
		ASSERT(!dragOperate);
		operatePending = false;
		opCenterVec = oldOpCenterVec;
	}

	if (!dragOperate && !dragOpCenter)
		return;

	// Need to turn off timer rendering for unfocused qviews.
	SetTimerUpdateMapViews(false);

	// Turn off wireframe approximation rendering.
	View::ApproxItems(false);

	if (dragOpCenter) {
		// Do one last update on the other qviews.
		OnTimerUpdateMapViews();
		// Update self for the scale bounding box.
		OnUpdate(NULL, NULL, NULL);

		dragOpCenter = false;
		return;
	}

	if (scrollingView) {  // Turn off scrolling.
		scrollingView = 0;
		KillTimer(TIMER_MV_SCROLLVIEW);
	}

	// Get standard ref ptrs.
	Selector *pSlct = &(GMainFrame->GetSelector());
	FEditorDoc *pDoc = GetDocument();

	// End the scale operation.
	pDoc->ObjectsScaleEnd(pSlct->GetMSelectedObjects(), false);

	CPoint opCenterPos;
	Vector3d basisVec;
	FindOpCenterPos(opCenterPos, &basisVec);

	// Swap the op center pos back.
	if (!dragScaleManipDot)
		opCenterVec = oldOpCenterVec;
	else
		pSlct->GetMSelectedObjectsCenter(opCenterVec);

	// Commit the operation.
	OpSelectionScale *op =
		new OpSelectionScale(*pView, basisVec, tempScaleVec, qvsBefore);
	GMainFrame->CommitOperation(*op);
	qvsBefore = NULL;

	// Update all map views.. Hack. use DUAV_OBJSSEL to update manip dots.
	pDoc->UpdateAllViews(NULL, DUAV_MAPVIEWS | DUAV_OBJSSEL, NULL);

	// Update the rest.
	pDoc->UpdateAllViews(NULL, DUAV_OBJSMODATTRIB | DUAV_NOMAPVIEWS,
						 (CObject *) pSlct->GetScopePtr());
	
	dragOperate = false;

	// Update the status bar
	CString mesg;
	mesg.LoadString(AFX_IDS_IDLEMESSAGE);
	GMainFrame->UpdateStatusBar(mesg);
}

//===== Modify Brush =====
void FMapView::UpdateManipDispStat(void) {
	if (!dispManipDots)
		pView->DisplayNoPts();
	else if (modifyBrushMode == ID_MODE_FACEMOVE)
		pView->DisplayFacePts(pManipFace);
	else if (modifyBrushMode == ID_MODE_EDGEMOVE)
		pView->DisplayEdgePts(pManipEdge);
	else if (modifyBrushMode == ID_MODE_VERTEXMOVE)
		pView->DisplayVertexPts(manipVertex);
}

void FMapView::OnLMBDModifyBrush(UINT nFlags, CPoint point) {
	dragOperate = false;
	pManipFace = NULL;
	pManipEdge = NULL;
	manipVertex = -1;
	dispManipDots = false;

	// Get standard ptrs.
	FEditorDoc* pDoc = GetDocument();
	Selector *pSlct = &(GMainFrame->GetSelector());

	// Check if the selected object has a brush.
	if (pSlct->GetNumMSelectedObjects() != 1 ||
		!(pSlct->GetMSelectedObjects())[0].GetPtr()->HasBrush()) {
		return;
	}

	Object *pBrushObj = (pSlct->GetMSelectedObjects())[0].GetPtr();

	// Clip mouse coord.
	CRect rect;
	GetClientRect(&rect);
	if (!rect.PtInRect(point)) {
		point.x = Max(rect.left, Min(point.x, rect.right));
		point.y = Max(rect.top, Min(point.y, rect.bottom));
	}

	// Test if clicked on a manip dot.
	float x, y;
	DC2LC(point, x, y);
	float epsilon = DRAG_CLICKPOS_EPSILON / zoomVal;

	if ((modifyBrushMode == ID_MODE_FACEMOVE &&
		 (pManipFace = pView->SelectFace(x, y, epsilon, &tempSnapVec)) != NULL) ||
		(modifyBrushMode == ID_MODE_EDGEMOVE &&
		 (pManipEdge = pView->SelectEdge(x, y, epsilon, &tempSnapVec)) != NULL) ||
		(modifyBrushMode == ID_MODE_VERTEXMOVE &&
		 (manipVertex = pView->SelectVertex(x, y, epsilon, &tempSnapVec)) != -1)) {
		// Init manipulation stuff.
		dragOperate = true;
		scrollingView = 0;
		lastMPos = ptClick;
		
		// Init snapping
		SetSnapScope(*(pView->GetViewObjPtr()));
		tempSnapVecTtl = tempSnapVec;

		// Turn on timer rendering.
		dispManipDots = true;
		SetTimerUpdateMapViews(true);

		// Turn on wireframe approximation rendering.
		View::ApproxItems(true);

		pDoc->BrushManipulateBegin(*pBrushObj);

		// Update the status bar
		CString mesg;
		mesg.LoadString(ID_STATUS_ALTSNAPOFF);
		GMainFrame->UpdateStatusBar(mesg);

		// Remember view states.
		ASSERT(qvsBefore == NULL);
		qvsBefore = new FMapViewsState();
	}
	
	// Update all Qviews to display the dragging manip dot.
	pDoc->UpdateAllViews(NULL, DUAV_MAPVIEWS | DUMV_UPDATEMANIPSTAT, NULL);
}

void FMapView::OnMMModifyBrush(UINT nFlags, CPoint point) {
	if (!dragOperate)
		return;

	FEditorDoc* pDoc = GetDocument();

	// Clip the mouse coord and scroll view.
	CRect rect;
	GetClientRect(&rect);
	if (!rect.PtInRect(point)) {
		if (!scrollingView) {
			scrollingView = ID_MODE_MODIFYBRUSH;
 			SetTimer(TIMER_MV_SCROLLVIEW, UPDATETIME_SCROLLVIEW, NULL);
		}
		scrollPos = point;

		// clip.
		point.x = Max(rect.left, Min(point.x, rect.right));
		point.y = Max(rect.top, Min(point.y, rect.bottom));
	}
	else if (scrollingView) {  // stop scrolling view.
		KillTimer(TIMER_MV_SCROLLVIEW);
		scrollingView = 0;
	}

	CPoint mouseMove(point);
	mouseMove -= lastMPos;
	lastMPos = point;

	float x, y;
	DU2LU(mouseMove, x, y);

	// check for edit constraints.
	if (qvType != ID_VIEW_NEW3D && editConstraint == 1)
		y = 0.0f;   // Horizontal lock.
	else if (qvType != ID_VIEW_NEW3D && editConstraint == 2)
		x = 0.0f;		// Vertical lock.

	Vector3d moveVec(x, 0.0f, y);

	tempSnapVecTtl.AddVector(moveVec);
	moveVec = tempSnapVecTtl;

	bool snap = (gridSnap && (::GetAsyncKeyState(VK_MENU) >> 31) == 0);
	if (snap) {
		Matrix44 m;
		pView->CalTransSpaceMatrix(m.SetIdentity());
		m.Transform(moveVec);

		// Edit contraint
		Vector3d lockVec, *pLockVec = NULL;

		if (editConstraint == 1)
			lockVec.NewVector(1.0f, 0.0f, 0.0f);
		else if (editConstraint == 2)
			lockVec.NewVector(0.0f, 0.0f, 1.0f);

		if (qvType != ID_VIEW_NEW3D && editConstraint != 0)
			pLockVec = &lockVec;

		if (editConstraint)
			(m.Transform(lockVec)).SubVector(pView->GetPosition());

		SnapVector2World(moveVec, pLockVec);

		pView->CalInvTransSpaceMatrix(m.SetIdentity());
		m.Transform(moveVec);
	}

	moveVec.SubVector(tempSnapVec);
	tempSnapVec.AddVector(moveVec);

	if (modifyBrushMode == ID_MODE_FACEMOVE)
		pDoc->FaceMove(pManipFace, moveVec, *pView, false);
	else if (modifyBrushMode == ID_MODE_EDGEMOVE)
		pDoc->EdgeMove(pManipEdge, moveVec, *pView, false);
	else if (modifyBrushMode == ID_MODE_VERTEXMOVE)
		pDoc->VertexMove(manipVertex, moveVec, *pView, false);

	// Update only self.
	OnUpdate(NULL, NULL, NULL);
}

void FMapView::OnLMBUModifyBrush(UINT nFlags, CPoint point) {
	FEditorDoc *pDoc = GetDocument();

	// Need to turn off timer rendering for unfocused qviews.
	SetTimerUpdateMapViews(false);

	// Turn off wireframe approximation rendering.
	View::ApproxItems(false);

	if (dragOperate) {
		Geometry *pOldBrush = pDoc->BrushManipulateEnd(false);

		if (pOldBrush != NULL) {
			Selector *pSlctr = &(GMainFrame->GetSelector());
			ASSERT(pSlctr->GetNumMSelectedObjects() == 1);
			Object *pManipObj = (pSlctr->GetMSelectedObjects())[0].GetPtr();

			OpBrushModification *op =
				new OpBrushModification(*pManipObj, *pOldBrush, qvsBefore);
			GMainFrame->CommitOperation(*op);
		}
		else {
			MessageBeep(MB_OK);
			delete qvsBefore;
		}

		dragOperate = false;
		qvsBefore = NULL;
	}

	// Restore manip dot displays.
	pManipFace = NULL;
	pManipEdge = NULL;
	manipVertex = -1;
	dispManipDots = true;

	// Upate all map views
	pDoc->UpdateAllViews(NULL, DUAV_MAPVIEWS | DUMV_UPDATEMANIPSTAT, NULL);

	// Update FTreeView.
	pDoc->UpdateAllViews(NULL, DUAV_NOMAPVIEWS | DUAV_OBJSMODATTRIB,
						 (CObject *) pView->GetViewObjPtr());

	// Update the status bar
	CString mesg;
	mesg.LoadString(AFX_IDS_IDLEMESSAGE);
	GMainFrame->UpdateStatusBar(mesg);
}

//===== Define an operation plane for plane clip and mirror =====
void FMapView::OnLMBDDefineOpPlane(UINT nFlags, CPoint point) {
	dragOperate = false;

	// Disable plane defs in 3d window.
	if (qvType == ID_VIEW_NEW3D)
		return;

	if (planeDefined) {
		// See if down clicked on plane drag pts.
		ASSERT(dragPlanePt == 0);
		CPoint pt;

		// Check 1st pt.
		LC2DC(planeDragPt1.GetX(), planeDragPt1.GetZ(), pt);
		pt -= point;
		if (ABS(pt.x) <= DRAG_CLICKPOS_EPSILON &&
			ABS(pt.y) <= DRAG_CLICKPOS_EPSILON) {
			dragOperate = true;
			dragPlanePt = 1;
		}

		// Check 1st pt.
		LC2DC(planeDragPt2.GetX(), planeDragPt2.GetZ(), pt);
		pt -= point;
		if (ABS(pt.x) <= DRAG_CLICKPOS_EPSILON &&
			ABS(pt.y) <= DRAG_CLICKPOS_EPSILON) {
			dragOperate = true;
			dragPlanePt = 2;
		}
	}
	else {
		dragOperate = true;
	}

	if (dragOperate) {  // Setup snapping.
		Matrix44 m;
		SetSnapScope(*(pView->GetViewObjPtr()));
		pView->CalTransSpaceMatrix(tempSnapMatrix);
		pView->CalInvTransSpaceMatrix(m.SetIdentity());
		m.Multiply(tempSnapInvMatrix);
		tempSnapInvMatrix = m;

		// Update the status bar
		CString mesg;
		mesg.LoadString(ID_STATUS_ALTSNAPOFF);
		GMainFrame->UpdateStatusBar(mesg);
	}

	if (planeDefined)
		return;

	planeDefined = true;

	// Turn off all plane defs in other qviews.
	FEditorDoc *pDoc = GetDocument();
	pDoc->UpdateAllViews(this, DUAV_MAPVIEWS | DUMV_INITPLANEDRAG, NULL);

	// Define the first plane pt.
	float x, y;
	DC2LC(point, x, y);
	planeDragPt1.NewVector(x, 0.0f, y);

	// Snap the first plane pt.
	SnapVector2World(planeDragPt1);

	// Setup the dragging for the 2nd pt.
	dragPlanePt = 2;
	planeDragPt2 = planeDragPt1;

	// Update self to display the drag manip dots.
	OnUpdate(NULL, NULL, NULL);
}

void FMapView::OnMMDefineOpPlane(UINT nFlags, CPoint point) {
	if (!dragOperate)
		return;

	// Optimization.  Drop mouse move messages to speed up updates.
	CPoint currPos;
	GetCursorPos(&currPos);
	ScreenToClient(&currPos);
	currPos -= point;
	if (ABS(currPos.x) > 1 || ABS(currPos.y) > 1) {
		static int count = 0;
		TRACE1("skip %d\n", count++);
		return;
	}

	// Clip the mouse coord in window.
	CRect rect;
	GetClientRect(&rect);
	if (!rect.PtInRect(point)) {
		point.x = Max(rect.left, Min(point.x, rect.right));
		point.y = Max(rect.top, Min(point.y, rect.bottom));
	}

	// Define the new drag pt.
	ASSERT(dragPlanePt == 1 || dragPlanePt == 2);
	Vector3d *pDragPt = (dragPlanePt == 1 ? &planeDragPt1 : &planeDragPt2);

	float x, y;
	DC2LC(point, x, y);
	pDragPt->NewVector(x, 0.0f, y);

	bool snap = (gridSnap && (::GetAsyncKeyState(VK_MENU) >> 31) == 0);

	if (snap) {
		SnapVector2World(*pDragPt);
	}

	// Update self to display the drag manip dots.
	OnUpdate(NULL, NULL, NULL);
}

void FMapView::OnLMBUDefineOpPlane(UINT nFlags, CPoint point) {
	if (!dragOperate)
		return;

	dragOperate = false;
	dragPlanePt = 0;

	// Update self to display the drag manip dots.
	OnUpdate(NULL, NULL, NULL);

	// Update the status bar
	CString mesg;
	mesg.LoadString(AFX_IDS_IDLEMESSAGE);
	GMainFrame->UpdateStatusBar(mesg);
}

void FMapView::OnRMBDDefineOpPlane(UINT nFlags, CPoint point) {
	// Turn off all plane defs in other qviews.
	FEditorDoc *pDoc = GetDocument();
	pDoc->UpdateAllViews(this, DUAV_MAPVIEWS | DUMV_INITPLANEDRAG, NULL);
}

Plane FMapView::CalcOpPlane(void) {
	ASSERT(planeDefined);

	Vector3d pt0(planeDragPt1);
	pt0.SetY(clipSide < 0 ? -100.0f : 100.0f);
	Plane opPlane(pt0, planeDragPt1, planeDragPt2);

	Matrix44 m;
	pView->CalTransSpaceMatrix(m.SetIdentity());
	m.Transform(opPlane);
	return opPlane;
}

//===== Plane Clip Stuff =====
void FMapView::OnRcmqvPerformClip(void) {
	Plane cutPlane(CalcOpPlane());
	OpPlaneClip *op = NULL;

	if (clipSide == 0) // Split down the middle.
		op = OpPlaneClip::NewPlaneSplitOp(cutPlane);
	else // Either clip left or right side.
		op = OpPlaneClip::NewPlaneClipOp(cutPlane);

	if (op != NULL)
		GMainFrame->CommitOperation(*op);
	else
		MessageBeep(MB_OK);
}

void FMapView::OnRcmqvRemoveLeft(void) {
	clipSide = -1;
	OnUpdate(NULL, NULL, NULL);
}

void FMapView::OnRcmqvRemoveRight(void) {
	clipSide = 1;
	OnUpdate(NULL, NULL, NULL);
}

void FMapView::OnRcmqvSplitAtLine(void) {
	clipSide = 0;
	OnUpdate(NULL, NULL, NULL);
}

//===== Plane Flip Stuff =====
void FMapView::OnRcmqvPerformFlip() {
	Plane flipPlane(CalcOpPlane());
	OpPlaneFlip *op = new OpPlaneFlip(flipPlane);
	ASSERT(op != NULL);

	GMainFrame->CommitOperation(*op);
}

void FMapView::OnRcmqvPerformMirror() {
	Plane mirrorPlane(CalcOpPlane());
	OpPlaneMirror *op = new OpPlaneMirror(mirrorPlane);
	ASSERT(op != NULL);

	GMainFrame->CommitOperation(*op);
}

//===== Hide the mouse during drag operations =====
void FMapView::ReCenterMousePos(CPoint &ptMousePos) {
	// Adjust the mouse to the middle of screen if it's near edges.
	// Used during operations where the mouse is hidden.

	CRect deskRect;
	// (GetDesktopWindow())->GetWindowRect(&deskRect);

	// Use the main window instead of desktopa
	GMainFrame->GetWindowRect(&deskRect);

	CRect boundRect(deskRect);
	// boundRect.DeflateRect(deskRect.right / 4, deskRect.bottom / 4);
	boundRect.DeflateRect(deskRect.Width() / 5, deskRect.Height() / 5);

	CPoint ptTemp(ptMousePos);
	ClientToScreen(&ptTemp);
	if (boundRect.PtInRect(ptTemp)) {
		// Do nothing.  We're fine.
		return;
	}

	// The mouse cursor is outside the bounding area.
	// Center it.

	long cx = ptTemp.x = (deskRect.left + deskRect.right) / 2;
	long cy = ptTemp.y = (deskRect.top + deskRect.bottom) / 2;
	ScreenToClient(&ptTemp);

	// Traverse through message queue to modify mouse events.
	// ......

	// Adjust both last pos and current pos relative to center.
	lastMPos.x -= ptMousePos.x - ptTemp.x;
	lastMPos.y -= ptMousePos.y - ptTemp.y;
	ptMousePos = ptTemp;

	// Center cursor.
	::SetCursorPos(cx, cy);
}

//===== Timer update the other locked views =====
#define UPDATETIME_RENDERLOCKEDMAPVIEWS	100

void FMapView::SetTimerUpdateLockedMapViews(bool set, UINT nFlags) {

	// Return if not a locked view.
	if (!GMainFrame->IsLockedView(this))
		return;

	// Return if updates of other qviews' not required.
	if (GMainFrame->GetOpMode() == ID_MODE_EYEROTATE &&
		((nFlags & MK_RBUTTON) || qvType != ID_VIEW_NEW3D))
		return;

	if (GMainFrame->GetOpMode() == ID_MODE_EYEMOVE &&
		(nFlags & MK_RBUTTON) && qvType == ID_VIEW_NEW3D)
		return;

	if (!set) {
		// Turn off timer.
		KillTimer(TIMER_MV_RENDERLOCKEDMAPVIEWS);
		// One last update.
		OnTimerUpdateLockedMapViews();
		return;
	}

	// Set timer.
	SetTimer(TIMER_MV_RENDERLOCKEDMAPVIEWS, UPDATETIME_RENDERLOCKEDMAPVIEWS, NULL);
}

void FMapView::OnTimerUpdateLockedMapViews(void) {
	FMapView *qvsArray[4];

	GMainFrame->GetLockedMapViews(qvsArray);
	for(int i = 0; i < 4; i++) {
		if (qvsArray[i] == this || qvsArray[i] == NULL)
			continue;
		if (updateSclBoundBox)
			qvsArray[i]->CalcScaleManipDots();
		qvsArray[i]->OnUpdate(NULL, NULL, NULL);
	}

	// Hack to fix the scaleBoundBox update during
	//  eye move w/ middle mouse drag
	updateSclBoundBox = false;
}

//===== Button Down Click =====
void FMapView::OnMBDEyeMoveRotateZoom(UINT nFlags, CPoint point) {
	// Hide the mouse.
	ShowCursor(FALSE);

	lastMPos = point;
	ReCenterMousePos(point);

	// Turn on timer rendering for locked qviews.
	SetTimerUpdateLockedMapViews(true, nFlags);

	// Turn on wireframe approximation rendering.
	View::ApproxItems(true);
}

void FMapView::OnMBUEyeMoveRotateZoom(UINT nFlags, CPoint point) {
	// Turn off timer rendering.
	SetTimerUpdateLockedMapViews(false, nFlags);

	// Turn off wireframe approximation rendering.
	View::ApproxItems(false);

	// Restore original mouse pos.
	CPoint ptScreen(ptClick);
	ClientToScreen(&ptScreen);
	::SetCursorPos(ptScreen.x, ptScreen.y);

	ShowCursor(TRUE);

	// Refresh all views.
	FEditorDoc *pDoc = GetDocument();
	pDoc->UpdateAllViews(NULL, DUAV_SUPDATEALL, NULL);
}

// Move the view(s).
void FMapView::OnLMBEyeMove(UINT nFlags, CPoint point) {
	if (!mouseMDrag && (nFlags & MK_SHIFT)) {
		// Rotate the view.
		OnLMBEyeRotate(0, point);
		return;
	}

	// Check for cursor adjustments.
	ReCenterMousePos(point);

	CPoint diffVec(point);
	diffVec -= lastMPos;

	float x, y;
	DU2LU(diffVec, x, y);
	Vector3d moveVec(x, 0.0f, y);

	// Move it.
	MoveEyeView(moveVec);

	lastMPos = point;
}

void FMapView::MoveEyeView(const Vector3d &moveVec) {
	Vector3d mVec(moveVec);

	if (GMainFrame->IsLockedView(this)) { // Move affects other views.
		Matrix44 trans;
		trans.SetRotate(pView->GetOrientation());
		trans.Transform(mVec);

		Vector3d editPos = GMainFrame->GetEditFocusPos();
		editPos.AddVector(mVec);
		GMainFrame->SetEditFocusPos(editPos, false);

		UINT opMode = GMainFrame->GetOpMode();
		if (opMode == ID_MODE_OBJECTSCALE)
			updateSclBoundBox = true;
		if (planeDefined) {
			planeDragPt1.SubVector(moveVec);
			planeDragPt2.SubVector(moveVec);
		}
	}
	else { // Just move self.
		pView->MoveRelPosition(mVec);
	}

	if (GMainFrame->GetOpMode() == ID_MODE_OBJECTSCALE) {
		// Update the scale box.
		for(int i = 0; i < 8; i++)
			scaleManipDots[i].SubVector(moveVec);
	}

	// Just update self.
	OnUpdate(NULL, NULL, NULL);
}

//===== Handle key strokes to move the views =====
#define MAPVIEW_EYEMOVE_STEPINC	64.0f
#define UPDATETIME_ENDPAN		500

void FMapView::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)  {
	// Default action.
	CView::OnKeyDown(nChar, nRepCnt, nFlags);

	if (!(nChar == VK_UP || nChar == VK_DOWN ||
		  nChar == VK_LEFT || nChar == VK_RIGHT))
		return;

	// Check if any of the mouse buttons are held down.
	if (::GetAsyncKeyState(VK_LBUTTON) < 0 ||
		::GetAsyncKeyState(VK_MBUTTON) < 0 ||
		::GetAsyncKeyState(VK_RBUTTON) < 0)
		return;

	Vector3d moveVec;

	if (nChar == VK_UP)
		moveVec.SetZ(MAPVIEW_EYEMOVE_STEPINC);
	else if (nChar == VK_DOWN)
		moveVec.SetZ(-MAPVIEW_EYEMOVE_STEPINC);
	else if (nChar == VK_LEFT)
		moveVec.SetX(-MAPVIEW_EYEMOVE_STEPINC);
	else
		moveVec.SetX(MAPVIEW_EYEMOVE_STEPINC);
		
	MoveEyeView(moveVec);

	// Set a timer to update all other views.
	if (GMainFrame->IsLockedView(this))  // Move affects other views.
		SetTimer(TIMER_MV_UPDATEVIEWS, UPDATETIME_ENDPAN, NULL);
}

void FMapView::OnEndViewMove(void) {
	OnTimerUpdateLockedMapViews();
	KillTimer(TIMER_MV_UPDATEVIEWS);
}

//===== Zoom stuff =====
#define ZOOM_SPEED  0.01f
#define ZOOM_MIN	0.025f
#define ZOOM_MAX	10.0f

// Zoom the view for 2d wins.
// Change orbit radius for locked 3d view.
// Move view forward/backward for unlocked 3d view.
void FMapView::OnRMBEyeZoom(UINT nFlags, CPoint point) {
	UINT opMode = GMainFrame->GetOpMode();

	if (opMode != ID_MODE_EYEZOOM && !mouseMDrag && (nFlags & MK_SHIFT)) {
		// Use the rotate function.
		OnRMBEyeRotate(0, point);
		return;
	}

	// Check for cursor adjustments.
	ReCenterMousePos(point);

	CPoint diffVec(point);
	diffVec -= lastMPos;

	if (qvType != ID_VIEW_NEW3D) {  // It's a 2d window.
		float newZoomVal = zoomVal * (1.0f - ZOOM_SPEED * diffVec.y);

		if (newZoomVal < ZOOM_MIN || newZoomVal > ZOOM_MAX)
			newZoomVal = zoomVal;

		if (GMainFrame->IsLockedView(this)) { // zoom affects other views.
			GMainFrame->Set2DLockedZoom(newZoomVal, false);
		}
		else {
			zoomVal = newZoomVal;
			OnZoomChanged();
		}
	}
	else { // 3d window.
		float x, y;
		DU2LU(diffVec, x, y);
		Vector3d moveVec;

		if (GMainFrame->IsLockedView(this)) {
			// Assume 3d view will always face edit focus pos.
			Vector3d radiusVec = pView->GetPosition();
			radiusVec.SubVector(GMainFrame->GetEditFocusPos());
			float r = radiusVec.GetMag();
			y = Min(y, r - 1.1f);
			moveVec.NewVector(0.0f, y, 0.0f);
		}
		else {
			moveVec.NewVector(x, y, 0.0f);
		}

		pView->MoveRelPosition(moveVec);
	}

	// Update just self.
	OnUpdate(NULL, NULL, NULL);
	lastMPos = point;
}

void FMapView::ZoomEyeView(float depthVal) {

	if (qvType != ID_VIEW_NEW3D) {  // It's a 2d window.
		float newZoomVal = zoomVal * (1.0f + ZOOM_SPEED * depthVal);

		if (newZoomVal < ZOOM_MIN || newZoomVal > ZOOM_MAX)
			newZoomVal = zoomVal;

		if (GMainFrame->IsLockedView(this)) { // zoom affects other views.
			GMainFrame->Set2DLockedZoom(newZoomVal, false);
		}
		else {
			zoomVal = newZoomVal;
			OnZoomChanged();
		}
	}
	else { // 3d window.
		Vector3d moveVec;

		if (GMainFrame->IsLockedView(this)) {
			// Assume 3d view will always face edit focus pos.
			Vector3d radiusVec = pView->GetPosition();
			radiusVec.SubVector(GMainFrame->GetEditFocusPos());
			float r = radiusVec.GetMag();
			depthVal = Min(depthVal, r - 1.1f);
			moveVec.NewVector(0.0f, depthVal, 0.0f);
		}
		else {
			moveVec.NewVector(0.0f, depthVal, 0.0f);
		}

		pView->MoveRelPosition(moveVec);
	}

	// Update just self.
	OnUpdate(NULL, NULL, NULL);
}

//===== Use mouse wheel to zoom in and out.
#define UPDATETIME_ENDZOOM		150

BOOL FMapView::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt) {
	int n = ABS(zDelta / 120);
	for(;n > 0; n--) {
		ZoomEyeView(zDelta > 0 ? 32.0f : -32.0f);
	}

	// Set a timer to update all other views.
	if (GMainFrame->IsLockedView(this))  // Move affects other views.
		SetTimer(TIMER_MV_UPDATEVIEWS, UPDATETIME_ENDZOOM, NULL);

	return TRUE;
}

void FMapView::OnViewZoomIn(void) {
	if (qvType == ID_VIEW_NEW3D)
		return;
	
	ZoomEyeView(32.0f);

	if (GMainFrame->IsLockedView(this)) { // Move affects other views.
		FEditorDoc *pDoc = GetDocument();
		pDoc->UpdateAllViews(this, DUAV_SUPDATEALL | DUAV_MAPVIEWS, NULL);
	}
}

void FMapView::OnViewZoomOut(void) {
	if (qvType == ID_VIEW_NEW3D)
		return;

	ZoomEyeView(-32.0f);

	if (GMainFrame->IsLockedView(this)) { // Move affects other views.
		FEditorDoc *pDoc = GetDocument();
		pDoc->UpdateAllViews(this, DUAV_SUPDATEALL | DUAV_MAPVIEWS, NULL);
	}
}

//===== Eye Rotate =====
// Rotate view's orientation.
void FMapView::OnLMBEyeRotate(UINT nFlags, CPoint point) {
	if (nFlags & MK_CONTROL) {
		// Move the view.
		OnLMBEyeMove(0, point);
		return;
	}

	// Only applies to 3d views.
	if (qvType != ID_VIEW_NEW3D)
		return;

	// Readjust cursor.
	ReCenterMousePos(point);

	float radius;
	bool viewLocked = GMainFrame->IsLockedView(this);
	if (viewLocked) {
		// Need to remember old radius.
		Vector3d radiusVec = pView->GetPosition();
		radiusVec.SubVector(GMainFrame->GetEditFocusPos());
		radius = radiusVec.GetMag();
		ASSERT(radius >= 1.0f);  // Sanity.
	}

	// Get rotation vector.
	CRect rect;
	GetClientRect(&rect);

	CPoint diffVec(point);
	diffVec -= lastMPos;

	float x, y;
	x = diffVec.x * 180.0f / rect.right;
	y = diffVec.y * 180.0f / rect.bottom;
	x *= sensitivity / 5.0f;
	y *= sensitivity / 5.0f;

	if (!invLMouse)
		y *= -1.0f;

	// Rotate the view's orientation.
	SphrVector orientVec;
	float angYaw, angPitch;

	pView->GetOrientation(orientVec);
	angPitch = orientVec.GetPitch() + DEG2RAD(y);
	angPitch = Min(Max(angPitch, DEG2RAD(-89.9f)), DEG2RAD(89.9f));
	angYaw = orientVec.GetYaw() + DEG2RAD(x);
	orientVec.NewVector(angYaw, angPitch, 0.0f);
	pView->SetOrientation(orientVec);

	if (viewLocked) {
		// Calculate new focus pos
		Vector3d newFocusPos(0.0f, radius, 0.0f);
		Matrix44 trans;
		trans.SetRotate(orientVec);
		trans.Transform(newFocusPos);
		newFocusPos.AddVector(pView->GetPosition());
		GMainFrame->SetEditFocusPos(newFocusPos, false);
	}

	// Update only self.
	OnUpdate(NULL, NULL, NULL);
	lastMPos = point;
}

// Orbit the view around the edit focus point.
void FMapView::OnRMBEyeRotate(UINT nFlags, CPoint point) {
	if (nFlags & MK_CONTROL) {
		// Move view.
		OnRMBEyeZoom(0, point);
		return;
	}

	// Only applies to 3d locked views.
	if (qvType != ID_VIEW_NEW3D || !GMainFrame->IsLockedView(this))
		return;

	// Readjust cursor.
	ReCenterMousePos(point);

	// Get rotation vector.
	CRect rect;
	GetClientRect(&rect);

	CPoint diffVec(point);
	diffVec -= lastMPos;

	float x, y;
	x = diffVec.x * 180.0f / rect.right;
	y = diffVec.y * 180.0f / rect.bottom;
	x *= sensitivity / 5.0f;
	y *= sensitivity / 5.0f;

	if (invRMouse) {
		x *= -1.0f;
		y *= -1.0f;
	}

	// Get radius distance.
	Vector3d radiusVec;
	radiusVec.SubVector(pView->GetPosition(), GMainFrame->GetEditFocusPos());
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
	radiusVec.AddVector(GMainFrame->GetEditFocusPos());
	pView->SetPosition(radiusVec);

	// Set view orientataion.
    orientVec.NewVector(orientVec.GetYaw() + DEG2RAD(180.0f),
						-orientVec.GetPitch(), 0.0f);
	pView->SetOrientation(orientVec);

	// update view.
	OnUpdate(NULL, NULL, NULL);

	lastMPos = point;
}

//===== Fly Through =====

void FMapView::OnStartFlyThrough(void) {
	FDraw::OutputText("Switching to flythrough mode... ");
	ASSERT(GMainFrame->GetOpMode() == ID_MODE_FLYTHROUGH);
	ASSERT(qvType == ID_VIEW_NEW3D);

	// Update status bar.
	GMainFrame->UpdateStatusBar();

	// Capture mouse.
	SetCapture();

	// Hide the mouse.
	::ShowCursor(FALSE);

	// Remember mouse pos.
	::GetCursorPos(&ptClick);
	lastMPos = ptClick;
	ScreenToClient(&lastMPos);

	FDraw::OutputText("OK.\n");

	// Init mouse pos.
	CPoint point(lastMPos);
	ReCenterMousePos(point);

	// Remember old view pos for later.
	if (GMainFrame->IsLockedView(this)) {
		downClickVec = GMainFrame->GetEditFocusPos();
		downClickVec.SubVector(pView->GetPosition());
	}

	// Turn on wireframe approximation rendering.
	//View::ApproxItems(true);

	flyTime = GetTime();

	// Empty the queue of mouse and key events.
	MSG tmpMsg;
	while (PeekMessage(&tmpMsg, NULL, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE));
	while (PeekMessage(&tmpMsg, NULL, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE));

	// Busy loop for input.
	MSG msg;
	while (true) {
		// While idling, check key state.
		while (!PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
			OnKeyCheckFlyThrough();

		// Check for mode exit.
		if (msg.message == WM_LBUTTONDOWN) {
			FDraw::OutputText("Switching back to editing mode... ");
			// DispatchMessage(&msg);
			break;
		}

		// To prevent delayed input processing, 
		//  check and clear out all mouse move events on the queue.
		if (msg.message == WM_MOUSEMOVE) {
 			while (PeekMessage(&tmpMsg, NULL, WM_MOUSEMOVE, WM_MOUSEMOVE, PM_REMOVE));
			DispatchMessage(&msg);
		}
		else if (msg.message >= WM_KEYFIRST && msg.message <= WM_KEYLAST) {
			while (PeekMessage(&tmpMsg, NULL, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE));
			// Check for mode exit.
			if (msg.message = WM_KEYDOWN && msg.wParam == VK_ESCAPE)
			{
				FDraw::OutputText("Switching back to editing mode... ");
				break;
			}
		}
		else {
			DispatchMessage(&msg);
		}

		OnKeyCheckFlyThrough();
	}

	// Turn off wireframe approximation rendering.
	View::ApproxItems(false);

	// Restore original mouse pos & cursor.
	::SetCursorPos(ptClick.x, ptClick.y);
	::ShowCursor(TRUE);

	// Release the mouse.
	::ReleaseCapture();

	// Calc new focus pos.
	if (GMainFrame->IsLockedView(this)) {
		Vector3d diffVec(0.0f, downClickVec.GetMag(), 0.0f);
		Matrix44 m;
		m.SetRotate(pView->GetOrientation());
		m.Transform(diffVec);
		diffVec.AddVector(pView->GetPosition());
		GMainFrame->Set3DViewLock(false);
		GMainFrame->SetEditFocusPos(diffVec);
		GMainFrame->Set3DViewLock(true);
	}
	FDraw::OutputText(" OK.\n");
}

#define GETKEYSTATE(k) (::GetAsyncKeyState(k) >> 31)

void FMapView::OnKeyCheckFlyThrough(bool forceUpdate) {
	ASSERT(GMainFrame->GetOpMode() == ID_MODE_FLYTHROUGH);
	ASSERT(qvType == ID_VIEW_NEW3D);
	ASSERT(GetCapture() == this);

	if (qvType != ID_VIEW_NEW3D)
		return;

	Vector3d moveVec;

	// Determine distance to move, based on time since last frame
	// This preserves a constant movement speed in the world
	float moveDist = walkSpeed * (float)(GetTime() - flyTime) / 50.0f;
	moveDist = Min(moveDist, walkSpeed);
	flyTime = GetTime();

	if (GETKEYSTATE(VK_RBUTTON) || GETKEYSTATE(keyForward))
		moveVec.SetY(moveVec.GetY() + moveDist);
	if (GETKEYSTATE(keyBackward))
		moveVec.SetY(moveVec.GetY() - moveDist);
	if (GETKEYSTATE(keyLeft))
		moveVec.SetX(moveVec.GetX() - moveDist);
	if (GETKEYSTATE(keyRight))
		moveVec.SetX(moveVec.GetX() + moveDist);
	if (GETKEYSTATE(keyUp))
		moveVec.SetZ(moveVec.GetZ() + moveDist);
	if (GETKEYSTATE(keyDown))
		moveVec.SetZ(moveVec.GetZ() - moveDist);

	pView->MoveRelPosition(moveVec);

	// Only update when neccessary.
	if (!(moveVec == Vector3d::origVec) || forceUpdate) {
		OnUpdate(NULL, NULL, NULL);
	}
}

void FMapView::OnMMFlyThrough(UINT nFlags, CPoint point) {
	ASSERT(GMainFrame->GetOpMode() == ID_MODE_FLYTHROUGH);
	ASSERT(qvType == ID_VIEW_NEW3D);

	if (qvType != ID_VIEW_NEW3D)
		return;

	// Check for cursor adjustments.
	ReCenterMousePos(point);

	// Get rotation vector.
	CRect rect;
	GetClientRect(&rect);

	CPoint diffVec(point);
	diffVec -= lastMPos;

	float x, y;
	x = diffVec.x * 180.0f / rect.right;
	y = diffVec.y * 180.0f / rect.bottom;
	// x *= 2.0f;
	// y *= 2.0f;

	if (!invLMouse)
		y *= -1.0f;

	// Rotate the view's orientation.
	SphrVector orientVec;
	float angYaw, angPitch;

	pView->GetOrientation(orientVec);
	angPitch = orientVec.GetPitch() + DEG2RAD(y);
	angPitch = Min(Max(angPitch, DEG2RAD(-89.9f)), DEG2RAD(89.9f));
	angYaw = orientVec.GetYaw() + DEG2RAD(x);
	orientVec.NewVector(angYaw, angPitch, 0.0f);
	pView->SetOrientation(orientVec);

	lastMPos = point;

	// Let the key check handler do the render update.
	OnKeyCheckFlyThrough(true);
}

//===== Edit Menu: Paste =====
void FMapView::OnEditPaste() {
	Selector *pSlctr = &(GMainFrame->GetSelector());
	ASSERT(pSlctr != NULL);

	Vector3d pasteVec;
	if (GMainFrame->IsLockedView(this)) {
		pasteVec = GMainFrame->GetEditFocusPos();
	}
	else if (qvType != ID_VIEW_NEW3D) {
		pasteVec = pView->GetPosition();
	}
	else {
		pasteVec.NewVector(0.0f, 128.0f, 0.0f);
		Matrix44 m;
		pView->CalTransSpaceMatrix(m.SetIdentity());
		m.Transform(pasteVec);
	}

	SnapAddObjPos(pasteVec);

	OpEditPaste *opPaste = new OpEditPaste(pasteVec);
	GMainFrame->CommitOperation(*opPaste);
}

void FMapView::OnUpdateEditPaste(CCmdUI* pCmdUI) {
	FEditorDoc *pDoc = GetDocument();

	pCmdUI->Enable(pDoc != NULL && (GMainFrame->clipBoard).NumOfElm() > 0);
}

//==== Edit: Duplicate ====

void FMapView::OnEditDuplicate() {
	Vector3d mVec;

	if (qvType == ID_VIEW_NEWTOP)
		mVec.NewVector(16.0f, -16.0f, 0.0f);
	else if (qvType == ID_VIEW_NEWBACK)
		mVec.NewVector(16.0f, 0.0f, -16.0f);
	else if (qvType == ID_VIEW_NEWSIDE)
		mVec.NewVector(0.0f, 16.f, -16.0f);
	else if (qvType == ID_VIEW_NEW3D)
		mVec.NewVector(16.0f, -16.0f, -16.0f);

	OpEditDuplicate *opDup = new OpEditDuplicate(mVec);
	GMainFrame->CommitOperation(*opDup);

}

void FMapView::OnUpdateEditDuplicate(CCmdUI* pCmdUI) {
	FEditorDoc *pDoc = GetDocument();
	Selector *pSlctr = &(GMainFrame->GetSelector());

	pCmdUI->Enable(pDoc != NULL && pSlctr != NULL &&
				   pSlctr->GetNumMSelectedObjects() != 0);
}

//===== Render library change =====
void FMapView::RefreshAll(void) {
	if(!GMainFrame)
		return;

	FEditorDoc *pDeskTopDoc = GMainFrame->GetDeskTopDocument();
	if(!pDeskTopDoc)
		return;

	FMapView *pView;
	POSITION pos = pDeskTopDoc->GetFirstViewPosition();
	while (pos != NULL) {
		pView = (FMapView *)pDeskTopDoc->GetNextView(pos);
		if (!pView->IsKindOf(RUNTIME_CLASS(FMapView)))
			continue;

		pView->CreateDraw();

		pView->pDraw->RenderMode(pView->qvRender);
		pView->pDraw->Size(pView->width, pView->height);
		pView->OnUpdate(NULL, NULL, NULL);
	}
}

void FMapView::OnTimeRefresh() {
#ifdef _DEBUG
	FMapView *pMapView = GMainFrame->GetStdMapView(ID_VIEW_NEW3D);

	if(!pMapView)
		return;

	int time = GetTime();
	
	float i;
	for(i = 0; i < 180; i++) {
		// Get radius distance.
		Vector3d radiusVec;
		radiusVec.SubVector(pMapView->pView->GetPosition(), GMainFrame->GetEditFocusPos());
		ASSERT(radiusVec.GetMag() >= 1.0f); // Sanity.

		// Get rotation vector.
		SphrVector orientVec;
		float angYaw, angPitch;
		orientVec.NewVector(radiusVec, Vector3d::zAxisVec);
		angYaw = orientVec.GetYaw() + DEG2RAD(2);
		angPitch = orientVec.GetPitch() + DEG2RAD(0);
		angPitch = Min(Max(angPitch, DEG2RAD(-89.5f)), DEG2RAD(89.5f));	
		orientVec.NewVector(angYaw, angPitch, 0.0f);

		// Rotate it.
		Matrix44 trans;
		trans.SetRotate(orientVec);
		radiusVec.NewVector(0.0f, radiusVec.GetMag(), 0.0f);
		trans.Transform(radiusVec);

		// Set view pos.
		radiusVec.AddVector(GMainFrame->GetEditFocusPos());
		pMapView->pView->SetPosition(radiusVec);

		// Set view orientataion.
	    orientVec.NewVector(orientVec.GetYaw() + DEG2RAD(180.0f),
						-orientVec.GetPitch(), 0.0f);
		pMapView->pView->SetOrientation(orientVec);

		// update view.
		pMapView->OnUpdate(NULL, NULL, NULL);
	}

	LError("%2.2f fps\n", 180.0f / (float)(GetTime() - time) * 1000.0f);
#endif
}

/*
void FMapView::OnCaptureChanged(CWnd *pWnd) {
	CView::OnCaptureChanged(pWnd);
	if (GMainFrame->GetOpMode() == ID_MODE_FLYTHROUGH) {
		CWnd *captWnd = SetCapture();
//		ASSERT_VALID(captWnd);
	}
}
*/

//===== Grid size display =====
void FMapView::OnDecrDisplayGridSize() {
	if (!FDraw::drawGridStep1)
		return;

	FDraw::OutputText("Decreasing grid size... ");
	double newSize = log(FDraw::gridStep1) / log(2) - 1.0f;
	newSize = ROUND(newSize);
	newSize = Max(0.0f, Min(7.0f, newSize));
	newSize = (float) pow(2, newSize);

	// Set the display grid size.
	FDraw::gridStep1 = (int) newSize;

	// Set the snap size.
	FMapView::gridSnapVal = (int) newSize;

	// Update map views.
	UpdateGridsDisplay();
	FDraw::OutputText("OK.\n");
}

void FMapView::OnIncrDisplayGridSize() {
	if (!FDraw::drawGridStep1)
		return;

	FDraw::OutputText("Increasing grid size... ");
	double newSize = log(FDraw::gridStep1) / log(2) + 1.0f;
	newSize = ROUND(newSize);
	newSize = Max(0.0f, Min(7.0f, newSize));
	newSize = pow(2, newSize);

	// Set the display grid size.
	FDraw::gridStep1 = (int) newSize;

	// Set the snap size.
	FMapView::gridSnapVal = (int) newSize;

	// Update map views.
	UpdateGridsDisplay();
	FDraw::OutputText("OK.\n");
}

