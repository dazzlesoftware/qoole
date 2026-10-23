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

/*
 * View.cpp
 */

#include "MapCorePCH.h"
#include <functional>
#include "MeshVertexBuffer.h"
#include "Renderer/LandscapeRenderPlan.h"
#include "Renderer/LandscapeDrawList.h"
#include "Renderer/FoliageRenderPlan.h"

#include "view.h"

bool* ent_texd;

//==================== View ====================

bool View::showEntities = true;
int View::manipVertex = -1;
const Edge3d* View::manipEdge = NULL;
const GPolygon* View::manipFace = NULL;

int View::renderColors[VRC_NUMRENDERCOLORS];

const View* View::renderView = NULL;
FDraw* View::renderDraw = NULL;
bool View::skipRender = false;
float View::landscapeDetailPixels = 12.0f;
int View::landscapeComponentsDrawn = 0;
int View::landscapeTrianglesDrawn = 0;
int View::foliageInstancesDrawn = 0;
bool View::skipSlctObjs = false;
Points3d View::vBuffer;
Point3d View::ptsBuf1[48];
Point3d View::ptsBuf2[48];
Object* View::pCrossHair = NULL;
Object* View::pCrossHairNorth = NULL;
// Object *View::pLeakGeomObj = NULL;
Object* View::pLeakObj = NULL;
bool View::aprxWFRender = false;

View::View(FDraw* pQdraw, Object& viewObject, const Selector& selector)
{
	selectorPtr = &selector;

	SetWindowWidth(1);
	SetClipDepth(1000.0f);
	SetNewView(viewObject, selector);

	showEntities = true;
	displayPts = 0;

	qdraw = pQdraw;

	for (int i = 0; i < VRC_NUMRENDERCOLORS; i++)
		renderColors[i] = i;
}

View::~View(void)
{
	ShowCrossHair(false);
}

View& View::operator=(const View& src)
{
	if (this == &src)
		return *this;

	LFatal();

	// ElmNode::operator=(src);
	TransSpace::operator=(src);

	qdraw = src.qdraw;
	viewObj = src.viewObj;
	perspective = src.perspective;
	clipView = src.clipView;
	winWidth = src.winWidth;
	selectorPtr = src.selectorPtr;
	return *this;
}

//===== View Settings =====

void View::SetNewView(Object& viewObject, const Selector& selector, bool reset, Vector3d* posVec, SphrVector* orientVec,
    bool perspectiveView, int windowWidth, float viewDepth)
{
	// Precondition.
	check(&viewObject == &(selector.GetScope()));

	viewObj = &viewObject;
	selectorPtr = &selector;

	if (reset)
	{
		Vector3d pVec(0.0f, 0.0f, 0.0f);
		SphrVector oriVec(0.0f, 0.0f, 0.0f);

		SetPosition(posVec ? *posVec : pVec);
		SetOrientation(orientVec ? *orientVec : oriVec);
		SetPerspective(perspectiveView);
		if (windowWidth != 0)
			SetWindowWidth(windowWidth);
		if (viewDepth > 0.0f)
			SetClipDepth(viewDepth);
	}

	DisplayFaceSelect(false);
}

void View::SetPerspective(bool perspectiveView)
{
	Vector3d cMin = clipView.GetMinClipEdges();
	Vector3d cMax = clipView.GetMaxClipEdges();

	if (perspectiveView && cMin.GetY() < 1.0f)
		cMin.SetY(1.0f);

	perspective = perspectiveView;
	clipView.SetClipVolume(cMin, cMax, perspective);
}

float View::GetClipDepth(void) const
{
	// Sanity
	check(perspective || (clipView.GetMaxClipEdges()).GetY() == -(clipView.GetMinClipEdges()).GetY());

	return (clipView.GetMaxClipEdges()).GetY();
}

void View::SetClipDepth(float depth)
{
	// Sanity.
	check(depth >= 1.0 && depth <= 10000.0f);

	Vector3d cMin = clipView.GetMinClipEdges();
	Vector3d cMax = clipView.GetMaxClipEdges();

	cMax.SetY(depth);
	cMin.SetY(perspective ? 1.0f : -depth);
	clipView.SetClipVolume(cMin, cMax, perspective);
}

void View::SetWindowWidth(int width)
{
	Vector3d cMin = clipView.GetMinClipEdges();
	Vector3d cMax = clipView.GetMaxClipEdges();

	winWidth = (float)width;
	cMin.SetX(-winWidth);
	cMin.SetZ(-winWidth);
	cMax.SetX(winWidth);
	cMax.SetZ(winWidth);
	clipView.SetClipVolume(cMin, cMax, perspective);
}

void View::MapViewSpaceOrigin(float& scrnX, float& scrnY)
{
	// Sanity.
	check(perspective == false);

	Matrix44 trans;
	Vector3d ov(0.0f, 0.0f, 0.0f);

	CalInvTransSpaceMatrix(trans.SetIdentity());
	trans.Transform(ov);
	scrnX = ov.GetX();
	scrnY = ov.GetZ();
}

//===== Object Selection =====

Line& View::SetRay(float scrnX, float scrnY, Line& ray, bool relView) const
{
	Vector3d p0, v0;
	Matrix44 trans;

	if (perspective)
	{
		p0.NewVector(0.0f, 0.0f, 0.0f);
		v0.NewVector(scrnX / winWidth, 1.0f, scrnY / winWidth);
	}
	else
	{
		p0.NewVector(scrnX, 0.0f, scrnY);
		v0.NewVector(0.0f, 1.0f, 0.0f);
	}

	if (relView)
	{
		v0.AddVector(p0);
		TransSpace::CalTransSpaceMatrix(trans.SetIdentity());
		trans.Transform(p0);
		trans.Transform(v0);
		v0.SubVector(p0);
	}

	return ray.NewLine(p0, v0);
}

bool View::IsMultiSelectionHit(float scrnX, float scrnY) const
{
	Line slctRay;
	float rangeBegin, rangeEnd, z;
	bool ok;
	Object* tmp;

	if (selectorPtr->GetNumMSelectedObjects() == 0)
		return NULL;

	SetRay(scrnX, scrnY, slctRay, false);
	ok = clipView.ClipRay(slctRay, rangeBegin, rangeEnd);
	check(ok);
	SetRay(scrnX, scrnY, slctRay, true);

	IterLinkList<ObjectPtr> iter(selectorPtr->GetMSelectedObjects());
	iter.Reset();
	while (!iter.IsDone())
	{
		tmp = (iter.GetNext())->GetPtr();
		if (selectorPtr->IsSelectionHit(tmp, slctRay, rangeBegin, rangeEnd, z))
			return true;
	}
	return false;
}

Object* View::SelectObject(float scrnX, float scrnY, int funcIndex) const
{
	Line slctRay;
	float rangeBegin, rangeEnd;
	bool ok;
	Object* rtnVal = NULL;

	SetRay(scrnX, scrnY, slctRay, false);
	ok = clipView.ClipRay(slctRay, rangeBegin, rangeEnd);
	check(ok);
	SetRay(scrnX, scrnY, slctRay, true);

	if (funcIndex == 0)
		rtnVal = selectorPtr->GetSNextSelect(slctRay, rangeBegin, rangeEnd);
	else if (funcIndex == 1)
		rtnVal = selectorPtr->GetSPrevSelect(slctRay, rangeBegin, rangeEnd);
	else if (funcIndex == 2)
		rtnVal = selectorPtr->GetMNextSelect(slctRay, rangeBegin, rangeEnd);
	else if (funcIndex == 3)
		rtnVal = selectorPtr->GetMPrevSelect(slctRay, rangeBegin, rangeEnd);

	return rtnVal;
}

int View::DragSelectMultiObjects(
    float scrnX1, float scrnY1, float scrnX2, float scrnY2, LinkList<ObjectPtr>& selectBuf) const
{
	// Set up a clip volume
	Vector3d cMin, cMax;
	Clip clipVol;

	if (perspective)
	{
		// Hacked the Clip class.
		// Passing in bounding ratios instead of bounding vectors.
		cMin.NewVector(scrnX1 / winWidth, (clipView.GetMinClipEdges()).GetY(), scrnY1 / winWidth);
		cMax.NewVector(scrnX2 / winWidth, (clipView.GetMaxClipEdges()).GetY(), scrnY2 / winWidth);
	}
	else
	{
		cMin.NewVector(scrnX1, (clipView.GetMinClipEdges()).GetY(), scrnY1);
		cMax.NewVector(scrnX2, (clipView.GetMaxClipEdges()).GetY(), scrnY2);
	}
	clipVol.SetClipVolume(cMin, cMax, perspective);

	// Set up the view transformation.
	Matrix44 trans;
	CalInvTransSpaceMatrix(trans.SetIdentity());
	viewObj->SetTransformation(trans);

	// Iter through children and find those in clip vol.
	Object* pObj;
	IterLinkList<Object>* pIter = &(viewObj->GetIterChildren());
	pIter->Reset();
	while (!pIter->IsDone())
	{
		pObj = pIter->GetNext();
		if (clipVol.IsObjectInVolume(*pObj))
			selectBuf.AppendNode(*(new ObjectPtr(pObj)));
	}

	return selectBuf.NumOfElm();
}

//===== Brush Manipulation Selection =====
// #define PTSEL_EPSILON 10

int View::SelectVertex(float scrnX, float scrnY, float epsilon, Vector3d* pManipDotPos)
{
	// Sanity.
	check(displayPts == 1);

	int i, rtnVal;
	Matrix44 trans;
	float div, ptX, ptY, diff, depth;

	check(selectorPtr->GetNumMSelectedObjects() == 1);
	Object* objNode = (selectorPtr->GetMSelectedObjects())[0].GetPtr();

	check(objNode->HasBrush());
	Geometry* brush = &(objNode->GetBrush());

	if (clipView.BoundObject(*objNode) == -1)
		return -1;

	TransSpace::CalInvTransSpaceMatrix(trans.SetIdentity());
	objNode->CalTransSpaceMatrix(trans);
	brush->TransformVertexPts(trans, vBuffer, manipVertex);

	// Assume Points3d::DeletePoint() is never used.
	rtnVal = -1;
	depth = (clipView.GetMaxClipEdges()).GetY();
	for (i = 0; i < brush->GetNumVertices(); i++)
	{
		if (clipView.ClipPoint(vBuffer[i]))
			continue;
		if (!perspective)
		{
			ptX = vBuffer[i].GetX() - scrnX;
			ptY = vBuffer[i].GetZ() - scrnY;
		}
		else
		{
			div = winWidth / vBuffer[i].GetY();
			ptX = vBuffer[i].GetX() * div - scrnX;
			ptY = vBuffer[i].GetZ() * div - scrnY;
		}
		diff = (float)sqrt(ptX * ptX + ptY * ptY);
		if (diff <= epsilon && vBuffer[i].GetY() <= depth)
		{
			depth = vBuffer[i].GetY();
			rtnVal = i;
			if (pManipDotPos != NULL)
				*pManipDotPos = vBuffer[i];
		}
	}

	return rtnVal;
}

const Edge3d* View::SelectEdge(float scrnX, float scrnY, float epsilon, Vector3d* pManipDotPos)
{
	// Sanity.
	check(displayPts == 2);

	int i, index;
	Matrix44 trans;
	float div, ptX, ptY, diff, depth;

	check(selectorPtr->GetNumMSelectedObjects() == 1);
	Object* objNode = (selectorPtr->GetMSelectedObjects())[0].GetPtr();

	check(objNode->HasBrush());
	Geometry* brush = &(objNode->GetBrush());

	if (clipView.BoundObject(*objNode) == -1)
		return NULL;

	TransSpace::CalInvTransSpaceMatrix(trans.SetIdentity());
	objNode->CalTransSpaceMatrix(trans);
	brush->TransformEdgePts(trans, vBuffer, manipEdge);

	index = -1;
	depth = (clipView.GetMaxClipEdges()).GetY();
	for (i = 0; i < brush->GetNumEdges(); i++)
	{
		if (clipView.ClipPoint(vBuffer[i]))
			continue;
		if (!perspective)
		{
			ptX = vBuffer[i].GetX() - scrnX;
			ptY = vBuffer[i].GetZ() - scrnY;
		}
		else
		{
			div = winWidth / vBuffer[i].GetY();
			ptX = vBuffer[i].GetX() * div - scrnX;
			ptY = vBuffer[i].GetZ() * div - scrnY;
		}
		diff = (float)sqrt(ptX * ptX + ptY * ptY);
		if (diff <= epsilon && vBuffer[i].GetY() <= depth)
		{
			depth = vBuffer[i].GetY();
			index = i;
			if (pManipDotPos != NULL)
				*pManipDotPos = vBuffer[i];
		}
	}
	// Sanity.
	check(index >= -1 || index < brush->GetNumEdges());

	return (index == -1 ? NULL : &(brush->GetEdgeList())[index]);
}

const GPolygon* View::SelectFace(float scrnX, float scrnY, float epsilon, Vector3d* pManipDotPos)
{
	// Sanity.
	check(displayPts == 3);

	int i, index;
	Matrix44 trans;
	float div, ptX, ptY, diff, depth;

	check(selectorPtr->GetNumMSelectedObjects() == 1);
	Object* objNode = (selectorPtr->GetMSelectedObjects())[0].GetPtr();

	check(objNode->HasBrush());
	Geometry* brush = &(objNode->GetBrush());

	if (clipView.BoundObject(*objNode) == -1)
		return NULL;

	TransSpace::CalInvTransSpaceMatrix(trans.SetIdentity());
	objNode->CalTransSpaceMatrix(trans);
	brush->TransformFacePts(trans, vBuffer, manipFace);

	index = -1;
	depth = (clipView.GetMaxClipEdges()).GetY();
	for (i = 0; i < brush->GetNumFaces(); i++)
	{
		if (clipView.ClipPoint(vBuffer[i]))
			continue;
		if (!perspective)
		{
			ptX = vBuffer[i].GetX() - scrnX;
			ptY = vBuffer[i].GetZ() - scrnY;
		}
		else
		{
			div = winWidth / vBuffer[i].GetY();
			ptX = vBuffer[i].GetX() * div - scrnX;
			ptY = vBuffer[i].GetZ() * div - scrnY;
		}
		diff = (float)sqrt(ptX * ptX + ptY * ptY);
		if (diff <= epsilon && vBuffer[i].GetY() <= depth)
		{
			depth = vBuffer[i].GetY();
			index = i;
			if (pManipDotPos != NULL)
				*pManipDotPos = vBuffer[i];
		}
	}
	// Sanity.
	check(index >= -1 || index < brush->GetNumFaces());

	return (index == -1 ? NULL : &(brush->GetFace(index)));
}

void View::DisplayNoPts(void)
{
	displayPts = 0;
	manipVertex = -1;
	manipEdge = NULL;
	manipFace = NULL;
}

void View::DisplayVertexPts(int vertex)
{
	// PreCondition
	// check(selectorPtr->GetNumMSelectedObjects() == 1);
	// check((selectorPtr->GetMSelectedObjects())[0].GetPtr()->HasBrush());

	displayPts = 1;
	manipVertex = vertex;
}

void View::DisplayEdgePts(const Edge3d* edge)
{
	// PreCondition
	// check(selectorPtr->GetNumMSelectedObjects() == 1);
	// check((selectorPtr->GetMSelectedObjects())[0].GetPtr()->HasBrush());

	displayPts = 2;
	manipEdge = edge;
}

void View::DisplayFacePts(const GPolygon* face)
{
	// PreCondition
	// check(selectorPtr->GetNumMSelectedObjects() == 1);
	// check((selectorPtr->GetMSelectedObjects())[0].GetPtr()->HasBrush());

	displayPts = 3;
	manipFace = face;
}

//===== Crosshair =====

bool View::IsCrossHairShown(void)
{
	check((!pCrossHair && !pCrossHairNorth) || (pCrossHair && pCrossHairNorth));
	return (pCrossHair != NULL);
}

void View::ShowCrossHair(bool show)
{
	// Sanity.
	check((!pCrossHair && !pCrossHairNorth) || (pCrossHair && pCrossHairNorth));

	Entity* entPtr = NULL;

	if (show && pCrossHair == NULL)
	{
		pCrossHair = new Object();
		entPtr = new Entity("cross1");
		pCrossHair->SetEntity(entPtr);
		pCrossHairNorth = new Object();
		entPtr = new Entity("cross2");
		pCrossHairNorth->SetEntity(entPtr);
	}
	else if (!show && pCrossHair != NULL)
	{
		delete pCrossHair;
		pCrossHair = NULL;
		delete pCrossHairNorth;
		pCrossHairNorth = NULL;
	}
}

void View::SetCrossHairPos(const Vector3d& posVec)
{
	// Sanity.
	check((!pCrossHair && !pCrossHairNorth) || (pCrossHair && pCrossHairNorth));

	if (pCrossHair == NULL)
		return;

	pCrossHair->SetPosition(posVec);
	pCrossHairNorth->SetPosition(posVec);
}

Vector3d View::GetCrossHairPos(void)
{
	// PreCondition.
	check(pCrossHair && pCrossHairNorth->GetParentPtr());

	return pCrossHair->GetPosition();
}

//===== Leak Geometry =====
const Object* View::GetLeakObject(void)
{
	return pLeakObj;
}

void View::SetLeakObject(Object* pNewObj)
{
	if (pLeakObj != NULL)
		delete pLeakObj;

	pLeakObj = pNewObj;
}

//===== Entities WireFrame Approximation =====

void View::ApproxItems(bool approximate)
{
	aprxWFRender = approximate;
}

//===== Color Settings =====
int View::GetColor(int colorIndex)
{
	// Sanity.
	check(colorIndex >= 0 && colorIndex < VRC_NUMRENDERCOLORS);

	return renderColors[colorIndex];
}

int View::SetColor(int colorIndex, int color)
{
	// Sanity.
	check(colorIndex >= 0 && colorIndex < VRC_NUMRENDERCOLORS);

	int rtnVal = renderColors[colorIndex];
	renderColors[colorIndex] = color;

	return rtnVal;
}

//===== Object Rendering =====

void View::RenderWireFrame(bool renderWorld, bool renderSelected, bool renderCrossHair) const
{
	// Sanity Check.
	check(viewObj != NULL);
	check(renderView == NULL);

	renderView = this;

	int oldNormColor, oldEntColor;

	if (renderWorld)
	{
		// Draw the world first.  Skip selected objects.
		skipSlctObjs = true;
		viewObj->PreOrderCondApply(RenderObjWireFrame);
	}

	// Draw the crosshair.
	if (renderCrossHair && (pCrossHair || pCrossHairNorth))
	{
		oldEntColor = SetColor(VRC_ENTITY, GetColor(VRC_CROSSHAIR));

		check(pCrossHair->GetParentPtr() == NULL);
		pCrossHair->SetParentPtr(viewObj);
		RenderObjWireFrame(*pCrossHair);
		pCrossHair->SetParentPtr(NULL);

		SetColor(VRC_ENTITY, GetColor(VRC_CROSSHAIRNORTH));

		check(pCrossHairNorth->GetParentPtr() == NULL);
		pCrossHairNorth->SetParentPtr(viewObj);
		RenderObjWireFrame(*pCrossHairNorth);
		pCrossHairNorth->SetParentPtr(NULL);

		SetColor(VRC_ENTITY, oldEntColor);
	}

	// Draw the additional objs. (leak geometry...)
	if (renderWorld && pLeakObj)
	{
		int oldBrushColor = SetColor(VRC_NORMAL, GetColor(VRC_MANIPDOT));

		check(pLeakObj->GetParentPtr() == NULL);
		pLeakObj->SetParentPtr(viewObj);
		pLeakObj->PreOrderCondApply(RenderObjWireFrame);
		pLeakObj->SetParentPtr(NULL);

		SetColor(VRC_NORMAL, oldBrushColor);
	}

	// Draw the selected object.
	if (renderSelected)
	{
		if (!renderWorld)
		{
			// Need to set viewObj's trans matrix.

			Matrix44 trans;
			CalInvTransSpaceMatrix(trans.SetIdentity());
			viewObj->SetTransformation(trans);

			// What does this do?????
			skipRender = true;
			viewObj->PreOrderCondApply(RenderObjWireFrame);
			skipRender = false;
		}

		// Draw the (multi) selected objects next.
		Object* rObjPtr;
		int numSlctObjs = selectorPtr->GetNumMSelectedObjects();
		skipSlctObjs = false;

		if (numSlctObjs > 0)
		{
			oldNormColor = SetColor(VRC_NORMAL, GetColor(VRC_SELECT));
			oldEntColor = SetColor(VRC_ENTITY, GetColor(VRC_SELECT));
			const int oldMeshColor = SetColor(VRC_MESH, GetColor(VRC_SELECT));

			IterLinkList<ObjectPtr> iter(selectorPtr->GetMSelectedObjects());
			iter.Reset();
			while (!iter.IsDone())
			{
				rObjPtr = (iter.GetNext())->GetPtr();
				rObjPtr->PreOrderCondApply(RenderObjWireFrame);
			}
			SetColor(VRC_NORMAL, oldNormColor);
			SetColor(VRC_ENTITY, oldEntColor);
			SetColor(VRC_MESH, oldMeshColor);
		}

		// Draw any manipulation dots and outline faces.
		if (numSlctObjs == 1 && rObjPtr->HasBrush())
		{
			if (dispFaceSlct)
				RenderSlctFaceOutline(*rObjPtr);
			if (displayPts != 0)
				DisplayManipPts();
		}

		// Draw the single (hot) selected object last.
		if ((rObjPtr = selectorPtr->GetSSelectedObject()) != NULL)
		{
			oldNormColor = SetColor(VRC_NORMAL, GetColor(VRC_HOTSELECT));
			oldEntColor = SetColor(VRC_ENTITY, GetColor(VRC_HOTSELECT));
			const int oldMeshColor = SetColor(VRC_MESH, GetColor(VRC_HOTSELECT));
			rObjPtr->PreOrderCondApply(RenderObjWireFrame);
			SetColor(VRC_NORMAL, oldNormColor);
			SetColor(VRC_ENTITY, oldEntColor);
			SetColor(VRC_MESH, oldMeshColor);
		}
	}

	renderView = NULL;
}

bool View::RenderObjWireFrame(Object& objNode)
{
	Matrix44 trans;
	bool lineClip;
	int bound;

	check(!objNode.HasBrush() || !objNode.IsItemNode());

	// Check to see if objNode is one of the selected objects.
	if (skipSlctObjs)
	{
		const Selector* slctPtr = renderView->selectorPtr;

		/*
		    if (slctPtr->GetNumMSelectedObjects() > 0) {
		      IterLinkList<ObjectPtr> oIter(slctPtr->GetMSelectedObjects());
		      oIter.Reset();
		      while (!oIter.IsDone()) {
		        if ((oIter.GetNext())->GetPtr() == &objNode)
		          return false;
		      }
		    }
		*/
		if (objNode.IsSelected())
			return false;

		if (slctPtr->GetSSelectedObject() == &objNode)
			return false;
	}

	// Calculate the transformation matrix.
	if (&objNode == renderView->viewObj)
	{
		renderView->CalInvTransSpaceMatrix(trans.SetIdentity());
		objNode.SetTransformation(trans);
	}
	else if (!objNode.IsItemNode())
	{
		check(!objNode.IsRoot());

		trans = (objNode.GetParent()).GetTransformation();
		objNode.CalTransSpaceMatrix(trans);
		objNode.SetTransformation(trans);
	}
	else
	{ // Account for item scaling problem inside other objects.
		check(!objNode.IsRoot());

		if (!renderView->showEntities)
			return false;

		Matrix44 tr;
		SphrVector oriVec;
		Vector3d ov, fv, tv;

		trans = (objNode.GetParent()).GetTransformation();
		objNode.CalTransSpaceMatrix(trans);

		trans.Transform(ov, Vector3d::origVec);
		(trans.Transform(fv, Vector3d::yAxisVec)).SubVector(ov);
		(trans.Transform(tv, Vector3d::zAxisVec)).SubVector(ov);
		tr.SetRotate(oriVec.NewVector(fv, tv));
		(trans.SetTranslation(ov)).Multiply(tr);

		objNode.SetTransformation(trans);
	}

	// Boundary check the object.
	bound = (renderView->clipView).BoundObject(objNode);
	if (bound == 1)
		lineClip = false;
	else if (bound == 0)
		lineClip = true;
	else // bound == -1
		return false;

	// Prepare to render wireframe.
	const LinkList<Edge3d>* listPtr = nullptr;
	const std::vector<StaticMesh::Edge>* meshEdges = nullptr;

	if (objNode.HasBrush())
	{
		Geometry* brush = &(objNode.GetBrush());
		brush->Transform(objNode.GetTransformation(), vBuffer);
		listPtr = &(brush->GetEdgeList());

		renderView->qdraw->Color(GetColor(VRC_NORMAL));
	}
	else if (objNode.IsItemNode())
	{
		const WireFrameGeom* wfg = &(objNode.GetItemWireFrame());
		if (!aprxWFRender || objNode.IsSelected() || &objNode == pCrossHair || &objNode == pCrossHairNorth ||
		    &objNode == pLeakObj)
		{
			wfg->TransformWireFrame(objNode.GetTransformation(), vBuffer);
			listPtr = &(wfg->GetWireFrameEdgeList());
		}
		else
		{
			wfg->TransformApproxBox(objNode.GetTransformation(), vBuffer);
			listPtr = &(wfg->GetApproxBoxEdgeList());
		}

		renderView->qdraw->Color(GetColor(VRC_ENTITY));
	}
	else if (objNode.HasLandscape())
	{
		renderView->qdraw->Color(GetColor(VRC_MESH));
		if (skipRender)
			return false;
		RenderLandscapeWireFrame(objNode, lineClip);
		return true;
	}
	else if (objNode.HasFoliage())
	{
		renderView->qdraw->Color(GetColor(VRC_MESH));
		if (skipRender)
			return false;
		RenderFoliageWireFrame(objNode, lineClip);
		return true;
	}
	else if (objNode.HasMesh())
	{
		const StaticMesh& mesh = objNode.GetMesh();
		FMeshVertexBufferAdapter::Transform(mesh, objNode.GetTransformation(), vBuffer);
		meshEdges = &(mesh.GetEdgeList());

		renderView->qdraw->Color(GetColor(VRC_MESH));
	}
	else
	{
		return true;
	}

	if (skipRender)
		return false;

	// Render the geometry.
	const std::function<void(int, int)> DrawEdge = [&](int a, int b)
	{
		Point3d pt1 = vBuffer[a], pt2 = vBuffer[b];

		// Clip line.
		if (lineClip && !(renderView->clipView).ClipLine(pt1, pt2))
			return;

		if (!renderView->perspective)
		{
			renderView->qdraw->RenderLine(pt1.GetX(), pt1.GetZ(), pt2.GetX(), pt2.GetZ());
		}
		else
		{
			float div1 = renderView->winWidth / pt1.GetY();
			float div2 = renderView->winWidth / pt2.GetY();
			renderView->qdraw->RenderLine(pt1.GetX() * div1, pt1.GetZ() * div1, pt2.GetX() * div2, pt2.GetZ() * div2);
		}
	};
	if (meshEdges)
	{
		for (const StaticMesh::Edge& edge : *meshEdges)
			DrawEdge(edge.start, edge.end);
	}
	else
	{
		IterLinkList<Edge3d> edgeIter(*listPtr);
		for (edgeIter.Reset(); !edgeIter.IsDone();)
		{
			const Edge3d* edge = edgeIter.GetNext();
			DrawEdge(edge->GetStartPoint(), edge->GetEndPoint());
		}
	}

	return true;
}

void View::RenderSlctFaceOutline(Object& objNode) const
{
	// Sanity.
	check(dispFaceSlct);
	check(!objNode.IsItemNode());
	check(objNode.HasBrush());
	check(GetSelectFace() != NULL);

	bool lineClip;
	int bound;

	// Boundary check the object.
	bound = clipView.BoundObject(objNode);
	if (bound == 1)
		lineClip = false;
	else if (bound == 0)
		lineClip = true;
	else // bound == -1
		return;

	Geometry* brush = &(objNode.GetBrush());
	IterLinkList<Edge3d> eIter((GetSelectFace())->GetEdgeList());
	Point3d pt1, pt2;
	Edge3d* ePtr;

	// Assume objNode has the right transformation from RenderObjWF().
	brush->Transform(objNode.GetTransformation(), vBuffer);

	qdraw->UseColor(GetColor(VRC_FACESELECT));
	eIter.Reset();
	while (!eIter.IsDone())
	{
		ePtr = eIter.GetNext();
		pt1 = vBuffer[ePtr->GetStartPoint()];
		pt2 = vBuffer[ePtr->GetEndPoint()];

		// Clip line.
		if (lineClip && !clipView.ClipLine(pt1, pt2))
			continue;

		if (!perspective)
		{
			qdraw->RenderLine(pt1.GetX(), pt1.GetZ(), pt2.GetX(), pt2.GetZ());
		}
		else
		{
			float div1 = winWidth / pt1.GetY();
			float div2 = winWidth / pt2.GetY();
			qdraw->RenderLine(pt1.GetX() * div1, pt1.GetZ() * div1, pt2.GetX() * div2, pt2.GetZ() * div2);
		}
	}
}

void View::DisplayManipPts(void) const
{
	// Sanity.
	check(displayPts > 0 && displayPts <= 3);

	float div;
	int i, numPts;
	Matrix44 trans;

	check(selectorPtr->GetNumMSelectedObjects() == 1);
	Object* objNode = (selectorPtr->GetMSelectedObjects())[0].GetPtr();

	check(objNode->HasBrush());
	Geometry* brush = &(objNode->GetBrush());

	trans = objNode->GetTransformation();
	if (displayPts == 1)
	{
		brush->TransformVertexPts(trans, vBuffer, manipVertex);
		numPts = (manipVertex != -1 ? 1 : brush->GetNumVertices());
	}
	else if (displayPts == 2)
	{
		brush->TransformEdgePts(trans, vBuffer, manipEdge);
		numPts = (manipEdge != NULL ? 1 : brush->GetNumEdges());
	}
	else
	{ // displayPts == 3
		brush->TransformFacePts(trans, vBuffer, manipFace);
		numPts = (manipFace != NULL ? 1 : brush->GetNumFaces());
	}

	qdraw->Color(GetColor(VRC_MANIPDOT));

	for (i = 0; i < numPts; i++)
	{
		if (clipView.ClipPoint(vBuffer[i]))
			continue;
		if (!perspective)
		{
			qdraw->RenderPoint(vBuffer[i].GetX(), vBuffer[i].GetZ());
		}
		else
		{
			div = winWidth / vBuffer[i].GetY();
			qdraw->RenderPoint(vBuffer[i].GetX() * div, vBuffer[i].GetZ() * div);
		}
	}
}

//===== Solid Rendering: Z Buffer =====

// Hack.
static int renderTMap = false;
static Matrix44 svTrans;

static void CalSVTrans(Object& objNode)
{

	if (objNode.IsRoot())
	{
		svTrans.SetIdentity();
	}
	else
	{
		CalSVTrans(objNode.GetParent());
		objNode.CalTransSpaceMatrix(svTrans);
	}
}

void View::RenderSolid(bool tmap) const
{
	// Sanity Check.
	check(viewObj != NULL);

	renderView = this;
	renderDraw = qdraw;
	renderTMap = tmap;

	CalSVTrans(*viewObj);

	Matrix44 viewTrans;
	CalTransSpaceMatrix(viewTrans.SetIdentity());
	svTrans.Multiply(viewTrans);

	// For a renderer drawing the copies itself: the light (as RenderMeshSolid
	// lights meshes, from map space) and the sky in view space, through the
	// view's rotation (svTrans: view to map) turned back.
	const std::function<Vector3d(const Vector3d&)> toView = [](const Vector3d& mapDir)
	{
		float v[3];
		for (int c = 0; c < 3; c++)
			v[c] = svTrans.GetMatrix(0, c) * mapDir.GetX() + svTrans.GetMatrix(1, c) * mapDir.GetY() +
			    svTrans.GetMatrix(2, c) * mapDir.GetZ();
		Vector3d out(v[0], v[1], v[2]);
		const float len = out.GetMag();
		if (len > 1e-8f)
			out.MultVector(1.0f / len);
		return out;
	};
	Vector3d light(0.35f, -0.5f, 0.79f);
	light.MultVector(1.0f / light.GetMag());
	renderDraw->ZBufInstanceLight(toView(light), toView(Vector3d(0.0f, 0.0f, 1.0f)), renderView->winWidth);

	viewObj->PreOrderCondApply(RenderObjSolid);

	qdraw->ZBufRender();

	if (selectorPtr->GetNumMSelectedObjects() == 1)
	{
		Object* rObjPtr = (selectorPtr->GetMSelectedObjects())[0].GetPtr();
		if (rObjPtr->HasBrush() && dispFaceSlct)
			RenderSlctFaceOutline(*rObjPtr);
	}

	renderView = NULL;
	renderDraw = NULL;
}

extern bool* ent_texd;

bool View::RenderObjSolid(Object& objNode)
{
	Matrix44 trans;
	bool polyClip;
	int bound;

	//  if(!*ent_texd && renderTMap && objNode.IsItemNode())
	//  if(renderTMap && objNode.IsItemNode())
	//    return false;

	// Calculate transformation matrix.
	if (&objNode == renderView->viewObj)
	{
		if (renderTMap)
		{
			renderView->TransSpace::CalTransSpaceMatrix(trans.SetIdentity());
			objNode.SetInvTransformation(trans);
		}

		renderView->TransSpace::CalInvTransSpaceMatrix(trans.SetIdentity());
		objNode.SetTransformation(trans);
	}
	else if (!objNode.IsItemNode())
	{
		if (renderTMap)
		{
			objNode.CalInvTransSpaceMatrix(trans.SetIdentity());
			trans.Multiply(objNode.GetParent().GetInvTransformation());
			objNode.SetInvTransformation(trans);
		}

		trans = (objNode.GetParent()).GetTransformation();
		objNode.CalTransSpaceMatrix(trans);
		objNode.SetTransformation(trans);
	}
	else
	{ // Account for item scaling problem inside other objects.
		// Sanity Check.
		check(!objNode.IsRoot());

		if (!renderView->showEntities)
			return false;

		Matrix44 tr;
		SphrVector oriVec;
		Vector3d ov, fv, tv;

		trans = (objNode.GetParent()).GetTransformation();
		objNode.CalTransSpaceMatrix(trans);

		trans.Transform(ov, Vector3d::origVec);
		(trans.Transform(fv, Vector3d::yAxisVec)).SubVector(ov);
		(trans.Transform(tv, Vector3d::zAxisVec)).SubVector(ov);
		tr.SetRotate(oriVec.NewVector(fv, tv));
		(trans.SetTranslation(ov)).Multiply(tr);

		objNode.SetTransformation(trans);
	}

	// Boundary check the object.
	bound = renderView->clipView.BoundObject(objNode);
	if (bound == 1)
		polyClip = false;
	else if (bound == 0)
		polyClip = true;
	else // bound == -1
		return false;

	// Prepare for rendering.
	IterLinkList<PlaneNode>* planeIter;

	if (objNode.HasBrush())
	{
		const Geometry* brush = &(objNode.GetBrush());
		planeIter = new IterLinkList<PlaneNode>(brush->GetPlaneList());
		brush->Transform(trans, vBuffer);
	}
	else if (objNode.IsItemNode())
	{
		const WireFrameGeom* wfg = &(objNode.GetItemWireFrame());
		planeIter = new IterLinkList<PlaneNode>(wfg->GetPlaneList());
		wfg->TransformApproxBox(trans, vBuffer);
	}
	else if (objNode.HasLandscape())
	{
		RenderLandscapeSolid(objNode, trans);
		return true;
	}
	else if (objNode.HasFoliage())
	{
		RenderFoliageSolid(objNode, trans);
		return true;
	}
	else if (objNode.HasMesh())
	{
		RenderMeshSolid(objNode, trans, polyClip);
		return true;
	}
	else
	{
		return true;
	}

	// Solid render the brush.
	static const Vector3d oVec(0.0f, 0.0f, 0.0f);
	static Vector3d nVec(0.0f, 1.0f, 0.0f), pVec, cVec;
	static Plane facePlane(nVec, oVec);
	IterLinkList<Edge3d>* edgeIter;
	PlaneNode* pNodePtr;
	Point3d *vPtr, *fvPtr;
	int i, numPts;
	static int color = 0;
	float div;
	ZPoint zpts[3];
	bool lockTex;
	Matrix44 absTrans;

	absTrans.Multiply(svTrans, trans);
	trans.Transform(cVec, oVec);

	planeIter->Reset();
	while (!planeIter->IsDone())
	{
		pNodePtr = planeIter->GetNext();
		trans.Transform(nVec, (pNodePtr->GetPlane()).GetNorm());
		nVec.SubVector(cVec);
		trans.Transform(pVec, (pNodePtr->GetPlane()).GetPoint());
		facePlane.NewPlane(nVec, pVec);
		if (facePlane.PointInPlane(oVec) <= 0.0f)
			continue; // Back face.  Skip.

		// Set Texture/Color
		lockTex = (pNodePtr->GetPolygon()).GetTexturePtr()->IsTexLocked();

		nVec = (pNodePtr->GetPlane()).GetNorm();

		if (!lockTex)
		{
			absTrans.Transform(pVec, oVec);
			(absTrans.Transform(nVec)).SubVector(pVec);
		}

		bool texd = renderDraw->ZBufSetup(&objNode, (pNodePtr->GetPolygon()).GetTexturePtr(), nVec);

		// Copy the vertices to local buffer.
		numPts = (pNodePtr->GetPolygon()).GetNumSides();
		check(numPts <= 32); // Sanity.
		edgeIter = new IterLinkList<Edge3d>(pNodePtr->GetPolygon().GetEdgeList());
		edgeIter->Reset();
		for (fvPtr = ptsBuf1; !edgeIter->IsDone(); fvPtr++)
			*fvPtr = vBuffer[(edgeIter->GetNext())->GetStartPoint()];
		delete edgeIter;

		// Clip Polygon.
		fvPtr = ptsBuf1;
		if (polyClip && renderView->clipView.ClipPolygon(numPts, ptsBuf1, ptsBuf2, &fvPtr) < 3)
			continue;

		check(numPts <= 48); // Sanity.

		// Break into triangles and render.
		vPtr = fvPtr;

		div = renderView->winWidth / fvPtr->GetY();
		zpts[0].x = fvPtr->GetX() * div;
		zpts[0].y = fvPtr->GetZ() * div;
		zpts[0].z = fvPtr->GetY();

		if (texd)
		{
			if (lockTex)
				(objNode.GetInvTransformation()).Transform(pVec, *vPtr);
			else
				svTrans.Transform(pVec, *vPtr);

			renderDraw->ZBufCalcST(pVec.GetX(), pVec.GetY(), pVec.GetZ(), zpts[0].s, zpts[0].t);
		}
		fvPtr++;

		div = renderView->winWidth / fvPtr->GetY();
		zpts[2].x = fvPtr->GetX() * div;
		zpts[2].y = fvPtr->GetZ() * div;
		zpts[2].z = fvPtr->GetY();

		if (texd)
		{
			if (lockTex)
				(objNode.GetInvTransformation()).Transform(pVec, *fvPtr);
			else
				svTrans.Transform(pVec, *fvPtr);

			renderDraw->ZBufCalcST(pVec.GetX(), pVec.GetY(), pVec.GetZ(), zpts[2].s, zpts[2].t);
		}
		fvPtr++;

		for (i = 2; i < numPts; i++, fvPtr++)
		{
			zpts[1].x = zpts[2].x;
			zpts[1].y = zpts[2].y;
			zpts[1].z = zpts[2].z;

			if (!fvPtr->GetY())
				continue;

			div = renderView->winWidth / fvPtr->GetY();
			zpts[2].x = fvPtr->GetX() * div;
			zpts[2].y = fvPtr->GetZ() * div;
			zpts[2].z = fvPtr->GetY();

			if (texd)
			{
				zpts[1].s = zpts[2].s;
				zpts[1].t = zpts[2].t;

				if (lockTex)
					(objNode.GetInvTransformation()).Transform(pVec, *fvPtr);
				else
					svTrans.Transform(pVec, *fvPtr);

				renderDraw->ZBufCalcST(pVec.GetX(), pVec.GetY(), pVec.GetZ(), zpts[2].s, zpts[2].t);
			}

			renderDraw->ZBufTriangle(zpts);
		}
	}
	delete planeIter;

	return true;
}

//===== Static meshes (Genesis) =====

void View::RenderMeshSolid(Object& objNode, const Matrix44& trans, bool polyClip, const StaticMesh* meshOverride,
    const Vector3d* scaleOverride)
{
	if (!meshOverride && renderDraw->ZBufMaterialMesh(objNode, trans))
		return;
	const StaticMesh& mesh = meshOverride ? *meshOverride : objNode.GetMesh();
	FMeshVertexBufferAdapter::Transform(mesh, trans, vBuffer);

	// Light from one direction in map space, over a soft fill, so curved
	// meshes shade smoothly whichever way they are turned.
	static Vector3d lightDir(0.0f, 0.0f, 0.0f);
	if (lightDir.GetMag() == 0.0f)
	{
		lightDir.NewVector(0.35f, -0.5f, 0.79f);
		lightDir.MultVector(1.0f / lightDir.GetMag());
	}

	// Object to map space; normals go through it divided by the scale
	// squared (the inverse transpose of a scale and a rotation).
	Matrix44 absTrans;
	absTrans.Multiply(svTrans, trans);
	Vector3d origin, scale, viewX, viewY, viewZ, cross;
	absTrans.Transform(origin, Vector3d::origVec);
	if (scaleOverride)
		scale = *scaleOverride;
	else
		objNode.GetScale(scale);
	const float isx = 1.0f / GeometryMath::Max(scale.GetX() * scale.GetX(), 1e-8f);
	const float isy = 1.0f / GeometryMath::Max(scale.GetY() * scale.GetY(), 1e-8f);
	const float isz = 1.0f / GeometryMath::Max(scale.GetZ() * scale.GetZ(), 1e-8f);

	// A mirrored transform turns the triangles' winding around.
	Vector3d viewOrigin;
	trans.Transform(viewOrigin, Vector3d::origVec);
	(trans.Transform(viewX, Vector3d::xAxisVec)).SubVector(viewOrigin);
	(trans.Transform(viewY, Vector3d::yAxisVec)).SubVector(viewOrigin);
	(trans.Transform(viewZ, Vector3d::zAxisVec)).SubVector(viewOrigin);
	cross.CrossVector(viewY, viewZ);
	const bool mirrored = cross.DotVector(viewX) < 0.0f;

	// One color per object, as brushes have.
	static const Vector3d up(0.0f, 0.0f, 1.0f);
	renderDraw->ZBufSetup(&objNode, NULL, up);

	const bool colored = mesh.HasVertexColors();
	// A landscape's vertices are its grid points, row by row: where each is
	// on the grid goes along, for drawing it textured.
	const Landscape* land = !meshOverride && objNode.HasLandscape() ? &objNode.GetLandscape() : NULL;
	const int landPointsX = land ? land->GetPointsX() : 1;
	const float landInvX = land ? 1.0f / land->GetQuadsX() : 0.0f;
	const float landInvY = land ? 1.0f / land->GetQuadsY() : 0.0f;
	Vector3d e1, e2, faceNorm, n;
	ZPoint zpts[3];
	float shades[3];
	float gridS[3] = {0.0f, 0.0f, 0.0f}, gridT[3] = {0.0f, 0.0f, 0.0f};
	unsigned int colors[3] = {0, 0, 0};
	for (const StaticMesh::Triangle& tri : mesh.GetTriangles())
	{
		const Point3d& p0 = vBuffer[tri.v[0]];
		const Point3d& p1 = vBuffer[tri.v[1]];
		const Point3d& p2 = vBuffer[tri.v[2]];

		// Back faces: the camera is at the view space origin.
		e1.SubVector(p1, p0);
		e2.SubVector(p2, p0);
		faceNorm.CrossVector(e1, e2);
		const float facing = faceNorm.DotVector(p0);
		if (mirrored ? facing <= 0.0f : facing >= 0.0f)
			continue;

		for (int k = 0; k < 3; k++)
		{
			const Vector3d& local = tri.norm[k];
			absTrans.Transform(n, local.GetX() * isx, local.GetY() * isy, local.GetZ() * isz);
			n.SubVector(origin);
			const float len = n.GetMag();
			const float ndl = len > 1e-8f ? n.DotVector(lightDir) / len : 0.0f;
			const float sky = len > 1e-8f ? n.GetZ() / len : 0.0f;
			shades[k] =
			    GeometryMath::Min(1.0f, 0.32f + 0.58f * GeometryMath::Max(ndl, 0.0f) + 0.12f * (sky * 0.5f + 0.5f));
			if (colored)
				colors[k] = mesh.GetVertexColor(tri.v[k]);
			if (land)
			{
				gridS[k] = (tri.v[k] % landPointsX) * landInvX;
				gridT[k] = (tri.v[k] / landPointsX) * landInvY;
			}
		}

		if (!polyClip)
		{
			const Point3d* corners[3] = {&p0, &p1, &p2};
			bool behind = false;
			for (int k = 0; k < 3; k++)
			{
				if (corners[k]->GetY() <= 0.0f)
				{
					behind = true;
					break;
				}
				const float div = renderView->winWidth / corners[k]->GetY();
				zpts[k].x = corners[k]->GetX() * div;
				zpts[k].y = corners[k]->GetZ() * div;
				zpts[k].z = corners[k]->GetY();
				zpts[k].s = gridS[k];
				zpts[k].t = gridT[k];
			}
			if (!behind)
			{
				if (land)
					renderDraw->ZBufLandscapeTriangle(zpts, shades, colored ? colors : NULL);
				else if (colored)
					renderDraw->ZBufColoredTriangle(zpts, shades, colors);
				else
					renderDraw->ZBufShadedTriangle(zpts, shades);
			}
			continue;
		}

		// Clipped: the pieces take the triangle's mean light.
		ptsBuf1[0] = p0;
		ptsBuf1[1] = p1;
		ptsBuf1[2] = p2;
		Point3d* fvPtr = ptsBuf1;
		int numPts = 3;
		if (renderView->clipView.ClipPolygon(numPts, ptsBuf1, ptsBuf2, &fvPtr) < 3)
			continue;
		const float mean = (shades[0] + shades[1] + shades[2]) / 3.0f;
		const float flat[3] = {mean, mean, mean};
		// And their mean color.
		unsigned int meanColor = 0xFF000000u;
		for (int c = 0; c < 3; c++)
		{
			const unsigned int sum =
			    ((colors[0] >> (8 * c)) & 0xFF) + ((colors[1] >> (8 * c)) & 0xFF) + ((colors[2] >> (8 * c)) & 0xFF);
			meanColor |= (sum / 3) << (8 * c);
		}
		const unsigned int flatColors[3] = {meanColor, meanColor, meanColor};
		const std::function<void(const Point3d&, ZPoint&)> project = [](const Point3d& pt, ZPoint& zp)
		{
			const float div = renderView->winWidth / pt.GetY();
			zp.x = pt.GetX() * div;
			zp.y = pt.GetZ() * div;
			zp.z = pt.GetY();
			zp.s = zp.t = 0.0f;
		};
		// A landscape's pieces keep their place on the grid, and their light:
		// both from where each corner is in the triangle.
		float pieceShades[3];
		const std::function<void(const Point3d&, ZPoint&, float&)> locate =
		    [&](const Point3d& pt, ZPoint& zp, float& shade)
		{
			Vector3d d0, d1, d2;
			d0.SubVector(p1, p0);
			d1.SubVector(p2, p0);
			d2.SubVector(pt, p0);
			const float a00 = d0.DotVector(d0), a01 = d0.DotVector(d1), a11 = d1.DotVector(d1);
			const float b0 = d2.DotVector(d0), b1 = d2.DotVector(d1);
			const float den = a00 * a11 - a01 * a01;
			const float w1 = den != 0.0f ? (a11 * b0 - a01 * b1) / den : 0.0f;
			const float w2 = den != 0.0f ? (a00 * b1 - a01 * b0) / den : 0.0f;
			const float w0 = 1.0f - w1 - w2;
			zp.s = w0 * gridS[0] + w1 * gridS[1] + w2 * gridS[2];
			zp.t = w0 * gridT[0] + w1 * gridT[1] + w2 * gridT[2];
			shade = w0 * shades[0] + w1 * shades[1] + w2 * shades[2];
		};
		project(fvPtr[0], zpts[0]);
		if (land)
			locate(fvPtr[0], zpts[0], pieceShades[0]);
		for (int i = 2; i < numPts; i++)
		{
			if (fvPtr[i - 1].GetY() <= 0.0f || fvPtr[i].GetY() <= 0.0f)
				continue;
			project(fvPtr[i - 1], zpts[1]);
			project(fvPtr[i], zpts[2]);
			if (land)
			{
				locate(fvPtr[i - 1], zpts[1], pieceShades[1]);
				locate(fvPtr[i], zpts[2], pieceShades[2]);
				renderDraw->ZBufLandscapeTriangle(zpts, pieceShades, colored ? flatColors : NULL);
			}
			else if (colored)
				renderDraw->ZBufColoredTriangle(zpts, flat, flatColors);
			else
				renderDraw->ZBufShadedTriangle(zpts, flat);
		}
	}
}

//===== Landscapes =====

namespace
{
	FSceneViewVolume LandscapeRenderView(const Clip& clip, bool perspective)
	{
		FSceneViewVolume view{clip.GetMinClipEdges(), clip.GetMaxClipEdges(), perspective};
		// Legacy perspective clipping uses a unit cone, regardless of its stored
		// orthographic X/Z extents. The runtime API takes explicit cone slopes.
		if (perspective)
		{
			view.Minimum.SetX(-1);
			view.Minimum.SetZ(-1);
			view.Maximum.SetX(1);
			view.Maximum.SetZ(1);
		}
		return view;
	}

	// Where a mesh's box is against the view (Clip::BoundBox): 1 inside, -1
	// out, 0 across its edges.
	int BoundMesh(const StaticMesh& mesh, const Matrix44& trans, const Clip& clip)
	{
		Vector3d low, high;
		mesh.GetBounds(low, high);
		Point3d corners[8];
		for (int i = 0; i < 8; i++)
			trans.Transform(corners[i],
			    Vector3d((i & 1) ? high.GetX() : low.GetX(), (i & 2) ? high.GetY() : low.GetY(),
			        (i & 4) ? high.GetZ() : low.GetZ()));
		return clip.BoundBox(corners);
	}
} // namespace

void View::RenderLandscapeSolid(Object& objNode, const Matrix44& trans)
{
	const Landscape& land = objNode.GetLandscape();
	Vector3d scale;
	objNode.GetScale(scale);
	for (const std::shared_ptr<const StaticMesh>& mesh : land.GetSplineMeshes())
	{
		const int bound = mesh ? BoundMesh(*mesh, trans, renderView->clipView) : -1;
		if (bound >= 0)
			RenderMeshSolid(objNode, trans, bound == 0, mesh.get(), &scale);
	}
	static const Vector3d up(0, 0, 1);
	renderDraw->ZBufSetup(&objNode, NULL, up);
	class FTriangleAdapter final : public ILandscapeTriangleSink
	{
	public:
		FTriangleAdapter(FDraw& draw, float projection) : Draw(draw), Projection(projection) {}
		void OnTriangle(const FLandscapeTriangle& triangle) override
		{
			ZPoint points[3];
			float shades[3];
			unsigned int colors[3];
			for (int i = 0; i < 3; i++)
			{
				const FLandscapeTriangleVertex& v = triangle.Vertices[i];
				const float div = Projection / v.Position.GetY();
				points[i].x = v.Position.GetX() * div;
				points[i].y = v.Position.GetZ() * div;
				points[i].z = v.Position.GetY();
				points[i].s = v.U;
				points[i].t = v.V;
				shades[i] = v.Shade;
				colors[i] = v.Color;
			}
			Draw.ZBufLandscapeTriangle(points, shades, triangle.bColored ? colors : NULL);
		}

	private:
		FDraw& Draw;
		float Projection;
	} sink(*renderDraw, renderView->winWidth);
	Matrix44 world;
	world.Multiply(svTrans, trans);
	FLandscapeDrawStats stats;
	BuildLandscapeTriangles(land, trans, world, scale, LandscapeRenderView(renderView->clipView, true),
	    renderView->winWidth, landscapeDetailPixels, sink, stats);
	landscapeComponentsDrawn += stats.Components;
	landscapeTrianglesDrawn += stats.Triangles;
}

void View::RenderLandscapeWireFrame(Object& objNode, bool lineClip)
{
	const Landscape& land = objNode.GetLandscape();
	const Matrix44& trans = objNode.GetTransformation();
	FLandscapeRenderPlan plan;
	// Lines may be a little denser than faces before they are thinned.
	PlanLandscape(land, trans, LandscapeRenderView(renderView->clipView, renderView->perspective),
	    renderView->perspective, renderView->winWidth, renderView->qdraw->GetZoom(), landscapeDetailPixels * 0.75f,
	    plan);
	FLandscapeComponentMesh component;
	std::vector<int>&xs = component.X, &ys = component.Y;
	std::vector<Point3d> pts;
	Point3d pt1, pt2;
	const std::function<void(const Point3d&, const Point3d&, bool)> line =
	    [&](const Point3d& a, const Point3d& b, bool clip)
	{
		pt1 = a;
		pt2 = b;
		if (clip && !(renderView->clipView).ClipLine(pt1, pt2))
			return;
		if (!renderView->perspective)
		{
			renderView->qdraw->RenderLine(pt1.GetX(), pt1.GetZ(), pt2.GetX(), pt2.GetZ());
		}
		else
		{
			const float div1 = renderView->winWidth / pt1.GetY();
			const float div2 = renderView->winWidth / pt2.GetY();
			renderView->qdraw->RenderLine(pt1.GetX() * div1, pt1.GetZ() * div1, pt2.GetX() * div2, pt2.GetZ() * div2);
		}
	};
	for (int cy = 0; cy < plan.cys; cy++)
	{
		for (int cx = 0; cx < plan.cxs; cx++)
		{
			const int step = plan.Step(cx, cy);
			if (step == 0)
				continue;
			const bool clip = lineClip && plan.bounds[(size_t)cy * plan.cxs + cx] == 0;
			const int x0 = cx * Landscape::ComponentQuads,
			          x1 = GeometryMath::Min(x0 + Landscape::ComponentQuads, land.GetQuadsX());
			const int y0 = cy * Landscape::ComponentQuads,
			          y1 = GeometryMath::Min(y0 + Landscape::ComponentQuads, land.GetQuadsY());
			BuildLandscapeComponent(land, plan, cx, cy, trans, false, component);
			pts.resize(component.Points.size());
			for (size_t k = 0; k < pts.size(); ++k)
			{
				Vector3d const& p = component.Points[k];
				pts[k].NewPos(p.GetX(), p.GetY(), p.GetZ());
			}
			const int nx = (int)xs.size(), ny = (int)ys.size();
			// The grid's lines (no diagonals); a component draws its lower and
			// left edges, and its upper and right ones only at the landscape's
			// edge, so shared lines are drawn once.
			const bool lastX = x1 == land.GetQuadsX(), lastY = y1 == land.GetQuadsY();
			for (int j = 0; j < ny; j++)
			{
				if (j == ny - 1 && !lastY)
					break;
				for (int i = 0; i + 1 < nx; i++)
					line(pts[(size_t)j * nx + i], pts[(size_t)j * nx + i + 1], clip);
			}
			for (int i = 0; i < nx; i++)
			{
				if (i == nx - 1 && !lastX)
					break;
				for (int j = 0; j + 1 < ny; j++)
					line(pts[(size_t)j * nx + i], pts[(size_t)(j + 1) * nx + i], clip);
			}
		}
	}
	// Its splines' meshes.
	for (const std::shared_ptr<const StaticMesh>& splineMesh : land.GetSplineMeshes())
	{
		const int bound = splineMesh ? BoundMesh(*splineMesh, trans, renderView->clipView) : -1;
		if (bound < 0)
			continue;
		FMeshVertexBufferAdapter::Transform(*splineMesh, trans, vBuffer);
		for (const StaticMesh::Edge& meshEdge : splineMesh->GetEdgeList())
		{
			const StaticMesh::Edge* edge = &meshEdge;
			line(vBuffer[edge->GetStartPoint()], vBuffer[edge->GetEndPoint()], lineClip || bound == 0);
		}
	}
}

//===== Foliage =====

namespace
{

	float ObjectScale(const Object& obj)
	{
		Vector3d scale;
		obj.GetScale(scale);
		return GeometryMath::Max(GeometryMath::Max(fabsf(scale.GetX()), fabsf(scale.GetY())), fabsf(scale.GetZ()));
	}
} // namespace

void View::RenderFoliageSolid(Object& objNode, const Matrix44& trans)
{
	const Foliage& foliage = objNode.GetFoliage();
	Vector3d objectScale;
	objNode.GetScale(objectScale);
	const float scale = ObjectScale(objNode);

	Matrix44 placed;
	for (const FoliageInstance& instance : foliage.GetInstances())
	{
		bool clip = false;
		if (!IsFoliageInstanceVisible(
		        foliage, instance, trans, scale, LandscapeRenderView(renderView->clipView, true), placed, clip))
			continue;
		// The GPU's, when the renderer draws copies (its near plane cuts them).
		if (renderDraw->ZBufMeshInstance(foliage.GetMesh(instance.type), placed))
		{
			foliageInstancesDrawn++;
			continue;
		}
		Vector3d instanceScale(objectScale);
		instanceScale.MultVector(instance.scale);
		RenderMeshSolid(objNode, placed, clip, foliage.GetMesh(instance.type).get(), &instanceScale);
		foliageInstancesDrawn++;
	}
}

void View::RenderFoliageWireFrame(Object& objNode, bool lineClip)
{
	const Foliage& foliage = objNode.GetFoliage();
	const Matrix44& trans = objNode.GetTransformation();
	const float scale = ObjectScale(objNode);
	const float zoom = renderView->qdraw->GetZoom();
	Matrix44 placed;
	Point3d pt1, pt2;
	for (const FoliageInstance& instance : foliage.GetInstances())
	{
		bool clip = false;
		if (!IsFoliageInstanceVisible(foliage, instance, trans, scale,
		        LandscapeRenderView(renderView->clipView, renderView->perspective), placed, clip))
			continue;
		clip = clip || lineClip;
		const StaticMesh& mesh = *foliage.GetMesh(instance.type);
		// Small on screen: a stroke from its foot to its top, not its edges.
		Vector3d foot, top, low, high;
		mesh.GetBounds(low, high);
		placed.Transform(foot, Vector3d::origVec);
		placed.Transform(top, Vector3d(0.0f, 0.0f, high.GetZ()));
		const float size = mesh.GetBoundRadius() * instance.scale * scale;
		const float pixels =
		    renderView->perspective ? size * renderView->winWidth / GeometryMath::Max(foot.GetY(), 1.0f) : size * zoom;
		const std::function<void(const Vector3d&, const Vector3d&)> line = [&](const Vector3d& a, const Vector3d& b)
		{
			pt1.NewVector(a.GetX(), a.GetY(), a.GetZ());
			pt2.NewVector(b.GetX(), b.GetY(), b.GetZ());
			if (clip && !(renderView->clipView).ClipLine(pt1, pt2))
				return;
			if (!renderView->perspective)
			{
				renderView->qdraw->RenderLine(pt1.GetX(), pt1.GetZ(), pt2.GetX(), pt2.GetZ());
				return;
			}
			const float div1 = renderView->winWidth / pt1.GetY();
			const float div2 = renderView->winWidth / pt2.GetY();
			renderView->qdraw->RenderLine(pt1.GetX() * div1, pt1.GetZ() * div1, pt2.GetX() * div2, pt2.GetZ() * div2);
		};
		// In 2D views foliage is many and seen from the side or above: only
		// what is big on screen shows its edges.
		if (pixels < (renderView->perspective ? 6.0f : 96.0f))
		{
			line(foot, top);
			continue;
		}
		FMeshVertexBufferAdapter::Transform(mesh, placed, vBuffer);
		for (const StaticMesh::Edge& meshEdge : mesh.GetEdgeList())
		{
			const StaticMesh::Edge* edge = &meshEdge;
			line(vBuffer[edge->GetStartPoint()], vBuffer[edge->GetEndPoint()]);
		}
	}
}
