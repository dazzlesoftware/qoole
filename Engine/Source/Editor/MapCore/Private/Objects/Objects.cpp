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
 * objects.cpp
 */

#include "MapCorePCH.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "objects.h"
#include "Assets/RenderAssets.h"

//==================== Object ====================

Object::Object(const char *className) : identity(FActorIdentity::New()) {
  parent = NULL;
  boundRadius = 0.0f;
  iterChildrenPtr = new IterLinkList<Object>(children);
  objName[0] = '\0';
  brush = NULL;
  entityPtr = NULL;
  viewTag = false;
  itemApprx = false;
  treeAttrib = false;
  bSelected = false;

  if (className != NULL) {
    Entity *ePtr = new Entity(className);
    check(ePtr != NULL);  // Sanity Check.
    ePtr = SetEntity(ePtr);
    check(ePtr == NULL);  // Snaity.
  }
}

Object::Object(const Object &src) {
  parent = NULL;
  brush = NULL;
  entityPtr = NULL;
  iterChildrenPtr = new IterLinkList<Object>(children);
  *this = src;  
}

Object::~Object(void) {
  // Sanity Check.
  check(IsLeaf() || (!HasBrush() && !IsItemNode()));

  DelObj();
  delete iterChildrenPtr;
}

Object &
Object::operator=(const Object &src) {

  if (&src == this)
    return *this;

  // ElmNode::operator=(src);
  TransSpace::operator=(src);
  identity = src.identity;

  DelObj(true);

  // Sanity.
  check(parent == NULL);

  brush = (src.brush == NULL ? NULL : new Geometry(*src.brush));
  mesh = src.mesh;
  meshAsset = src.meshAsset;
  landscape = src.landscape;
  foliage = src.foliage;

  Entity *ePtr = (src.entityPtr == NULL ? NULL : new Entity(*src.entityPtr));
  if ((ePtr = SetEntity(ePtr)) != NULL)
    delete ePtr;

  IterLinkList<Object> srcChildren(src.children);
  Object *chld;

  srcChildren.Reset();
  while (!srcChildren.IsDone()) {
    chld = new Object(*srcChildren.GetNext());
    AddChild(*chld, false);
  }

  strcpy(objName, src.objName);

  boundRadius = src.boundRadius;
  viewTag = false;
  itemApprx = false;
  treeAttrib = false;
  bSelected = false;

  return *this;
}

void Object::RegenerateIdentity(bool recursive) {
  identity = FActorIdentity::New();
  if (recursive) for (int i = 0; i < GetNumChildren(); ++i) GetChild(i).RegenerateIdentity(true);
}

void
Object::DelObj(bool rmFrmPrnt, bool resetBound) {
  // Remove from parent.
  if (!IsRoot() && rmFrmPrnt) {
    GetParent().RemoveChild(*this);
    parent = NULL;
  }

  // Delete name.
  objName[0] = '\0';

  // Delete brush.
  if (brush != NULL)
    delete brush;
  brush = NULL;
  mesh.reset();
  meshAsset.reset();
  landscape.reset();
  foliage.reset();

  // Delete all children.
  while (GetNumChildren() > 0)
    delete &(RemoveChild(GetChild(0), false));

  // Delete Entity info
  Entity *ePtr = NULL;
  if ((ePtr = SetEntity(ePtr, false)) != NULL)
    delete ePtr;

  if (resetBound)
	  SetBoundRadius();
}

//===== Object: tree traversal =====

bool
Object::IsRoot(void) const {
  return (GetParentPtr() == NULL);
}

Object &
Object::GetRoot(void) {
  return (IsRoot() ? *this : GetParent().GetRoot());
}

bool
Object::IsLeaf(void) const {
  return (GetNumChildren() == 0);
}

Object *
Object::GetParentPtr(void) const {
  return parent;
}

Object &
Object::GetParent(void) const {
  // Sanity Check.
  check(parent != NULL);
  return *parent;
}

Object &
Object::SetParentPtr(Object *parentPtr) {
  parent = parentPtr;
  return *this;
}

Object &
Object::SetParent(Object &parentObj) {
  parent = &parentObj;
  return *this;
}

int
Object::GetNumChildren(void) const {
  return children.NumOfElm();
}

Object &
Object::AddChild(Object &childNode, bool resetBound, int index) {
  // Sanity Check.
  check(!HasBrush());
  check(!HasEntity() || (IsModelNode() &&
          (strcmp(GetEntityPtr(true)->GetClassname(), "worldspawn") == 0 ||
           childNode.CountEntities() == 0)));
  check(index >= index -1 && index <= children.NumOfElm());

  childNode.SetParent(*this);
  if (index == -1)
    children.AppendNode(childNode);
  else
    children.InsertNode(childNode, index);

  if (resetBound)
    SetBoundRadius();

  return *this;
}

Object &
Object::RemoveChild(Object &childNode, bool resetBound, int *pIndex) {
  // PreCondition.
  check(IsMyChild(childNode));

  Object *rtnVal;

  if (pIndex != NULL) {
	  *pIndex = children.FindNodeIndex(&childNode);
  }

  childNode.SetParentPtr(NULL);
  rtnVal = &(children.RemoveNode(childNode));
  if (resetBound)
    SetBoundRadius();

  return *rtnVal;
}

Object &
Object::GetChild(int childNum) const {
  // PreCondition.
  check(childNum >= 0 && childNum < children.NumOfElm());

  return children[childNum];
}

int
Object::FloatChildren(bool resetBound) {
  // Sanity.
  check(!IsRoot());
  check(GetNumChildren() > 0);

  int rtnVal = GetNumChildren();

  Matrix44 trans;
  CalTransSpaceMatrix(trans.SetIdentity());

  // Reset current obj.
  Vector3d oVec(0.0f, 0.0f, 0.0f);
  TransSpace::SetPosition(oVec);

  SphrVector oriVec(0.0f, 0.0f, 0.0f);
  TransSpace::SetOrientation(oriVec);

  Vector3d sVec(1.0f, 1.0f, 1.0f);
  TransSpace::SetScale(sVec);

  Object *childPtr;
  Object *parentPtr = GetParentPtr();
  check(parentPtr != NULL);  // Sanity.

  IterLinkList<Object> *iter = &(GetIterChildren());
  iter->Reset();
  while (!iter->IsDone()) {
    childPtr = iter->GetNext();
	childPtr->Transform(trans, resetBound);
    RemoveChild(*childPtr, false);
    parentPtr->AddChild(*childPtr, false);
  }

  // Recalculate bound radius.
  if (resetBound)
    SetBoundRadius();

  // Post Condition.
  check(GetNumChildren() == 0);

  return rtnVal;
}

IterLinkList<Object> &
Object::GetIterChildren(void) {
  // Sanity.
  check(iterChildrenPtr != NULL);

  iterChildrenPtr->Reset();
  return *iterChildrenPtr;
}

bool
Object::IsMyChild(const Object &node) const {
  return (children.FindNodeIndex(&node) != -1);
}

bool
Object::IsMyAncestor(const Object &node) const {
  if (IsRoot())
    return false;
  if (GetParentPtr() == &node)
    return true;
  else
    return (GetParent().IsMyAncestor(node));
}

Object *
Object::FindCommonAncestor(Object &node) {
	if (IsMyAncestor(node) || this == &node)
		return &node;
	if (node.IsRoot())
		return NULL;
	return FindCommonAncestor(node.GetParent());
}

int
Object::CountNodes(int cType) const {
  // PreCondition
  check(cType >= 0 && cType < 5);
  // Sanity Check.
  check(IsLeaf() || (!HasBrush() && !IsItemNode()));

  int rtnVal = 0;
  Object *chld;
  IterLinkList<Object> iter(children);

  if (cType == 0) {
    rtnVal++;
  }
  else if (cType == 1) {
    // Kind of useless.
    rtnVal += (HasEntity() && GetEntityPtr()->IsItemEntity() ? 1 : 0);
  }
  else if (cType == 2) {
    rtnVal += (HasBrush() ? 1 : 0);
  }
  else if (cType == 4) {
    rtnVal += (HasMesh() ? 1 : 0);
  }
  else {
    rtnVal += (HasEntity() ? 1 : 0);
  }

  iter.Reset();
  while (!iter.IsDone()) {
    chld = iter.GetNext();
    if (cType == 0)
      rtnVal += chld->CountNodes();
    else if (cType == 1)
      rtnVal += chld->CountItems();
    else if (cType == 2)
      rtnVal += chld->CountBrushes();
    else if (cType == 4)
      rtnVal += chld->CountMeshes();
    else
      rtnVal += chld->CountEntities();
  }

  return rtnVal;
}

int
Object::PreOrderApply(bool (*func)(Object &obj)) {
  int rtnVal = 0;
  IterLinkList<Object> iter(children);
  
  if ((*func)(*this))
    rtnVal++;
  iter.Reset();
  while (!iter.IsDone()) {
    rtnVal += ((iter.GetNext())->PreOrderApply(func));
  }

  return rtnVal;
}

int
Object::PreOrderCondApply(bool (*func)(Object &obj)) {
  int rtnVal = 0;
  IterLinkList<Object> iter(children);

  if ((*func)(*this)) {
    rtnVal++;
    iter.Reset();
    while (!iter.IsDone()) {
      rtnVal += ((iter.GetNext())->PreOrderCondApply(func));
    }
  }

  return rtnVal;
}

int
Object::PostOrderApply(bool (*func)(Object &obj)) {
  int rtnVal = 0;
  IterLinkList<Object> iter(children);

  iter.Reset();
  while (!iter.IsDone()) {
    rtnVal += ((iter.GetNext())->PostOrderApply(func));
  }
  if ((*func)(*this))
    rtnVal++;

  return rtnVal;
}


//===== Object Name =====
const char *
Object::GetObjName(void) const {
  return objName;
}

void
Object::SetObjName(const char *name) {
  int i;

  if (name == NULL) {
    objName[0] = '\0';
    return;
  }

  for(; *name != '\0' && (isspace(*name) || *name == ':'); name++);
  for(i = 0;
      i < 31 && !(name[i] == '\n' || name[i] == '\r' || name[i] == '\0');
      i++) {
    objName[i] = name[i];
  }
  objName[i] = '\0';
}

//===== Static Mesh =====

std::shared_ptr<const StaticMesh>
Object::SetMesh(std::shared_ptr<const StaticMesh> meshPtr) {
  // PreCondition: a leaf with neither a brush nor an entity.
  check(meshPtr == nullptr || (IsLeaf() && !HasBrush() && !HasEntity()));

  std::shared_ptr<const StaticMesh> rtnVal = mesh;
  mesh = std::move(meshPtr);
  meshAsset.reset();
  landscape.reset();
  SetBoundRadius();
  return rtnVal;
}

void Object::SetMeshAsset(std::shared_ptr<const FLoadedMeshAsset> asset) {
  check(asset && asset->DisplayMesh);
  SetMesh(asset->DisplayMesh);
  meshAsset = std::move(asset);
}

std::shared_ptr<const Foliage>
Object::SetFoliage(std::shared_ptr<const Foliage> foliagePtr) {
  check(foliagePtr == nullptr || (IsLeaf() && !HasBrush() && !HasEntity() && !HasMesh()));
  std::shared_ptr<const Foliage> rtnVal = foliage;
  foliage = std::move(foliagePtr);
  SetBoundRadius();
  return rtnVal;
}

std::shared_ptr<const Landscape>
Object::SetLandscape(std::shared_ptr<const Landscape> landPtr) {
  check(landPtr == nullptr || (IsLeaf() && !HasBrush() && !HasEntity()));

  std::shared_ptr<const Landscape> rtnVal = landscape;
  // A landscape's mesh is its own (GetMesh), made only when asked for; a
  // static mesh stays when there was and is no landscape.
  if (landscape != nullptr || landPtr != nullptr) {
    mesh.reset();
    meshAsset.reset();
  }
  landscape = std::move(landPtr);
  SetBoundRadius();
  return rtnVal;
}

//===== Brush Manipulation =====

bool
Object::HasBrush(void) const {
  return (brush != NULL);
}

Geometry &
Object::GetBrush(void) const {
  // Sanity Check.
  check(brush != NULL);

  return *brush;
}

Geometry *
Object::SetBrush(Geometry *brushPtr) {
  // PreCondition.
  check(IsLeaf() && (!HasEntity() || IsModelNode()));

  Geometry *rtnVal = brush;

  brush = brushPtr;
  SetBoundRadius();

  return rtnVal;
}

bool
Object::HollowInBrush(float delta) {
  // PreCondition.
  check(HasBrush());

  LinkList<Geometry> *brushListPtr;
  Geometry *hollowee;
  Object *newObj;
  Matrix44 trans;
  Vector3d pVec;
  bool rtnVal = false;

  if (!HasBrush())
    return false;

  hollowee = new Geometry(GetBrush());
  CalTransSpaceMatrix(trans.SetIdentity());
  hollowee->Transform(trans);
  hollowee->CenterGeometry(pVec);

  brushListPtr = hollowee->HollowInward(delta);

  if (brushListPtr == NULL || brushListPtr->NumOfElm() <= 1) {
    rtnVal = false;
  }
  else {
    Vector3d p2Vec, sVec(1.0f, 1.0f, 1.0f);
    SphrVector oriVec(0.0f, 0.0f, 0.0f);

    delete brush;
    brush = NULL;

    while (brushListPtr->NumOfElm() > 0) {
      newObj = new Object();
      newObj->brush = &(brushListPtr->RemoveNode(0));
      (newObj->brush)->CenterGeometry(p2Vec);
      newObj->SetBoundRadius(false);
      newObj->SetPosition(p2Vec);
      AddChild(*newObj, false);
    }
    SetBoundRadius(true);

    TransSpace::SetPosition(pVec);
    TransSpace::SetOrientation(oriVec);
    TransSpace::SetScale(sVec);

    rtnVal = true;
  }

  delete hollowee;

  return rtnVal;
}

bool
Object::HollowOutBrush(float delta) {
  Geometry *holdBrush = GetBrush().CreateExpandedGeom(delta);
  
  if (holdBrush == NULL)
    return false;

  holdBrush = SetBrush(holdBrush);
  bool rtnVal = HollowInBrush(delta);

  if (!rtnVal) {
    holdBrush = SetBrush(holdBrush);
    check(holdBrush != NULL);
  }
  else {
    check(brush == NULL);
  }

  delete holdBrush;

  return rtnVal;
}

//===== Entity Info =====

bool
Object::HasEntity(void) const {
  return (entityPtr != NULL);
}

Entity *
Object::GetEntityPtr(bool bubble) const {
  if (entityPtr != NULL)
    return entityPtr;
  else if (bubble == false || IsRoot())
    return NULL;
  else
    return (GetParent()).GetEntityPtr(bubble);
}

Entity *
Object::SetEntity(Entity *ePtr, bool resetBound) {
  // PreCondition.
  check(ePtr == NULL || ePtr->IsModelEntity() || (!HasBrush() && IsLeaf()));

  Entity *rtnVal = entityPtr;
  if (ePtr != NULL && objName[0] == '\0') {
	  SetObjName(ePtr->GetClassname());
  }

  entityPtr = ePtr;
  if (resetBound)
	  SetBoundRadius();
  return rtnVal;
}

bool
Object::IsItemNode(void) const {
  return (HasEntity() && (GetEntityPtr())->IsItemEntity());
}

bool
Object::IsModelNode(void) const {
  return (HasEntity() && (GetEntityPtr())->IsModelEntity());
}

const WireFrameGeom &
Object::GetItemWireFrame(void) const {
  // Sanity.
  check(IsItemNode());

  return (GetEntityPtr()->GetItemWireFrame());
}

//===== Object Dimension =====

Vector3d &
Object::AutoCenterByBound(Vector3d &centerVec) {
  IterLinkList<Object> iter(children);
  Object *objPtr;
  Vector3d minVec(0.0f, 0.0f, 0.0f);
  Vector3d maxVec(0.0f, 0.0f, 0.0f);
  Vector3d posVec;
  Matrix44 trans;

  SetTransformation(trans.SetIdentity());
  GetBoundBox(minVec, maxVec);
  centerVec.AddVector(minVec, maxVec);
  centerVec.MultVector(0.5f);

  iter.Reset();
  while (!iter.IsDone()) {
    objPtr = iter.GetNext();
    posVec.SubVector(objPtr->GetPosition(), centerVec);
    objPtr->TransSpace::SetPosition(posVec);
  }

  SetBoundRadius();

  return centerVec;
}

void
Object::GetObjDimension(Vector3d &dimVec) {
  IterLinkList<Object> iter(children);
  Vector3d minVec, maxVec;
  Matrix44 trans;

  SetTransformation(trans.SetIdentity());
  GetBoundBox(minVec, maxVec);

  // Sanity.
  check(minVec.GetX() <= maxVec.GetX());
  check(minVec.GetY() <= maxVec.GetY());
  check(minVec.GetZ() <= maxVec.GetZ());

  dimVec.SubVector(maxVec, minVec);
}

void
Object::GetBoundingVectors(Vector3d &minVec, Vector3d &maxVec) {
	Matrix44 trans;
	SetTransformation(trans.SetIdentity());
	GetBoundBox(minVec, maxVec);
}

Vector3d
Object::GetMinBoundVec(void) {
  IterLinkList<Object> iter(children);
  Vector3d minVec, maxVec;
  Matrix44 trans;

  SetTransformation(trans.SetIdentity());
  GetBoundBox(minVec, maxVec);

  // Sanity.
  check(minVec.GetX() <= maxVec.GetX());
  check(minVec.GetY() <= maxVec.GetY());
  check(minVec.GetZ() <= maxVec.GetZ());

  return minVec;
}

Vector3d
Object::GetMaxBoundVec(void) {
  IterLinkList<Object> iter(children);
  Vector3d minVec, maxVec;
  Matrix44 trans;

  SetTransformation(trans.SetIdentity());
  GetBoundBox(minVec, maxVec);

  // Sanity.
  check(minVec.GetX() <= maxVec.GetX());
  check(minVec.GetY() <= maxVec.GetY());
  check(minVec.GetZ() <= maxVec.GetZ());

  return maxVec;
}

void
Object::GetBoundBox(Vector3d &minVec, Vector3d &maxVec) {
  // Sanity.
  // check(!IsRoot());

  if (IsItemNode()) {
    Vector3d bVec(boundRadius, boundRadius, boundRadius);
    minVec.NewVector(0.0f, 0.0f, 0.0f);
    (GetTransformation()).Transform(minVec);
    maxVec.AddVector(minVec, bVec);
    minVec.SubVector(bVec);
    return;
  }

  if (HasBrush()) {
    (GetBrush()).FindTransBoundBox(GetTransformation(), minVec, maxVec);
    return;
  }

  if (HasLandscape() || HasFoliage()) {
    // The box it fills, its corners placed.
    Vector3d low(0.0f, 0.0f, 0.0f), high(0.0f, 0.0f, 0.0f), corner, placed;
    if (HasLandscape())
      GetLandscape().GetBounds(low, high);
    else
      GetFoliage().GetBounds(low, high);
    for (int i = 0; i < 8; i++) {
      corner.NewVector((i & 1) ? high.GetX() : low.GetX(), (i & 2) ? high.GetY() : low.GetY(), (i & 4) ? high.GetZ() : low.GetZ());
      GetTransformation().Transform(placed, corner);
      if (i == 0) {
        minVec = maxVec = placed;
        continue;
      }
      minVec.NewVector(GeometryMath::Min(minVec.GetX(), placed.GetX()), GeometryMath::Min(minVec.GetY(), placed.GetY()), GeometryMath::Min(minVec.GetZ(), placed.GetZ()));
      maxVec.NewVector(GeometryMath::Max(maxVec.GetX(), placed.GetX()), GeometryMath::Max(maxVec.GetY(), placed.GetY()), GeometryMath::Max(maxVec.GetZ(), placed.GetZ()));
    }
    return;
  }

  if (HasMesh()) {
    GetMesh().FindTransBoundBox(GetTransformation(), minVec, maxVec);
    return;
  }

  Object *objPtr;
  Vector3d bMinVec, bMaxVec;
  IterLinkList<Object> iter(children);
  Matrix44 trans;
  bool init = false;

  iter.Reset();
  while (!iter.IsDone()) {
    objPtr = iter.GetNext();

    trans = GetTransformation();
    objPtr->CalTransSpaceMatrix(trans);
    objPtr->SetTransformation(trans);
    objPtr->GetBoundBox(bMinVec, bMaxVec);

    if (!init) {
      init = true;
      minVec = bMinVec;
      maxVec = bMaxVec;
    }
    else {
      minVec.NewVector(GeometryMath::Min(minVec.GetX(), bMinVec.GetX()),
                       GeometryMath::Min(minVec.GetY(), bMinVec.GetY()),
                       GeometryMath::Min(minVec.GetZ(), bMinVec.GetZ()));
      maxVec.NewVector(GeometryMath::Max(maxVec.GetX(), bMaxVec.GetX()),
                       GeometryMath::Max(maxVec.GetY(), bMaxVec.GetY()),
                       GeometryMath::Max(maxVec.GetZ(), bMaxVec.GetZ()));
    }
  }
}

//===== transformation =====
const Matrix44 &
Object::SetTransformation(const Matrix44 &transMatrix) {
  return (absTransMatrix = transMatrix);
}

const Matrix44 &
Object::SetInvTransformation(const Matrix44 &transMatrix) {
  return (invTransMatrix = transMatrix);
}

void
Object::Transform(const Matrix44 &transMatrix,
                  bool resetBound, bool mirrorEffect) {
  Matrix44 m1(transMatrix);
  Vector3d pVec, sVec;
  SphrVector oriVec;  

  CalTransSpaceMatrix(m1);

  TransSpace::SetScale(sVec.NewVector(1.0f, 1.0f, 1.0f));
  if (IsItemNode()) {
    // Sanity
    check(GetNumChildren() == 0);

    Vector3d fVec, tVec;
    m1.Transform(pVec, 0.0f, 0.0f, 0.0f);
    (m1.Transform(fVec, 0.0f, 1.0f, 0.0f)).SubVector(pVec);
    (m1.Transform(tVec, 0.0f, 0.0f, 1.0f)).SubVector(pVec);
	TransSpace::SetPosition(pVec);
	TransSpace::SetOrientation(fVec, tVec);
  }
  else if (HasMesh() || HasFoliage()) {
    // Sanity
    check(GetNumChildren() == 0);

    // Unlike a brush, a mesh keeps its transform: the new one is taken
    // apart into a position, an orientation and a scale per axis (a
    // mirror makes the X scale negative).
    Vector3d xVec, yVec, zVec, cross;
    m1.Transform(pVec, 0.0f, 0.0f, 0.0f);
    (m1.Transform(xVec, 1.0f, 0.0f, 0.0f)).SubVector(pVec);
    (m1.Transform(yVec, 0.0f, 1.0f, 0.0f)).SubVector(pVec);
    (m1.Transform(zVec, 0.0f, 0.0f, 1.0f)).SubVector(pVec);
    float sx = GeometryMath::Max(xVec.GetMag(), 1e-4f);
    const float sy = GeometryMath::Max(yVec.GetMag(), 1e-4f);
    const float sz = GeometryMath::Max(zVec.GetMag(), 1e-4f);
    cross.CrossVector(yVec, zVec);
    if (cross.DotVector(xVec) < 0.0f)
      sx = -sx;
    yVec.MultVector(1.0f / sy);
    zVec.MultVector(1.0f / sz);
    TransSpace::SetPosition(pVec);
    TransSpace::SetOrientation(yVec, zVec);
    TransSpace::SetScale(sVec.NewVector(sx, sy, sz));
  }
  else if (HasBrush()) {
	// Sanity
    check(GetNumChildren() == 0);

    GetBrush().Transform(m1, mirrorEffect);
    GetBrush().CenterGeometry(pVec);
    TransSpace::SetPosition(pVec);

    SphrVector oriVec(0.0f, 0.0f, 0.0f);
	TransSpace::SetOrientation(oriVec);
  }
  else {
    // Sanity.
    check(GetNumChildren() > 0);

    Vector3d aveVec(0.0f, 0.0f, 0.0f);
    Object *childPtr;
    IterLinkList<Object> iter(children);
    iter.Reset();

    while (!(iter.IsDone())) {
      childPtr = iter.GetNext();
      childPtr->Transform(m1, resetBound, mirrorEffect);
      childPtr->GetPosition(pVec);
      aveVec.AddVector(pVec);
    }

    // Reset position based on mean of children's positions.
    aveVec.MultVector(1.0f / children.NumOfElm());
    TransSpace::SetPosition(aveVec);

    // Adjust all children's position relatively.
    iter.Reset();
    while (!iter.IsDone()) { 
      childPtr = iter.GetNext();
      (childPtr->GetPosition(pVec)).SubVector(aveVec);
      childPtr->TransSpace::SetPosition(pVec);
    }

    // Reset orientation.
    SphrVector oriVec(0.0f, 0.0f, 0.0f);
    TransSpace::SetOrientation(oriVec);
  }

  if (resetBound)
    SetBoundRadius(false);
}

void 
Object::GetTransMatrix(Object &startObj,
					   Object &endObj, Matrix44 &transMatrix) {
	const Object *pStart = &startObj;
	const Object *pEnd = &endObj;
	const Object *pRoot = &(startObj.GetRoot());
	check(&(endObj.GetRoot()) == pRoot);

	// Traver up the tree from startObj until
	//  we reach a common ancestor.
	Matrix44 m1, m2, m3;
	m1.SetIdentity();
	while (pStart != pEnd && !pEnd->IsMyAncestor(*pStart)) {
		pStart->CalTransSpaceMatrix(m3.SetIdentity());
		m1 = m2.Multiply(m3, m1);
		pStart = pStart->GetParentPtr();
		check(pStart != NULL);
	}

	m2.SetIdentity();
	while (pStart != pEnd) {
		pEnd->CalInvTransSpaceMatrix(m2);
		pEnd = pEnd->GetParentPtr();
		check(pEnd != NULL);
	}

	transMatrix.Multiply(m2, m1);
}

//===== TransSpace wrapper =====
const Vector3d &
Object::SetPosition(const Vector3d &newPos) {
  const Vector3d *rtnVal;
  rtnVal = &(TransSpace::SetPosition(newPos));
  if (!IsRoot())
    (GetParent()).SetBoundRadius();
  return *rtnVal;
}

const Vector3d &
Object::MoveRelPosition(const Vector3d &posDelta) {
  const Vector3d *rtnVal;
  rtnVal = &(TransSpace::MoveRelPosition(posDelta));
  if (!IsRoot())
    (GetParent()).SetBoundRadius();
  return *rtnVal;
}

const Vector3d &
Object::SetOrientation(const Vector3d &frontVec, const Vector3d &topVec) {
  Vector3d fv(frontVec);
  Vector3d tv(topVec);

  if (CountEntities() > 0) {
    // Hack to check for the camera intermission entity.
    if (!IsItemNode() ||
        strcmp(GetEntityPtr()->GetClassname(), "info_intermission") != 0) {
      Vector3d xAxis(1.0f, 0.0f, 0.0f);
      Vector3d yAxis(0.0f, 1.0f, 0.0f);

      if (frontVec.GetX() == 0.0 && frontVec.GetY() == 0.0)
        fv.NewVector(-xAxis.DotVector(tv), -yAxis.DotVector(tv), 0.0f);
      else
        fv.NewVector(xAxis.DotVector(fv), yAxis.DotVector(fv), 0.0f);

      tv.NewVector(0.0f, 0.0f, 1.0f);
    }
  }

  return (TransSpace::SetOrientation(fv, tv));
}

const SphrVector &
Object::SetOrientation(const SphrVector &orientVec) {
  Vector3d fv, tv;

  orientVec.GetRectVector(fv, tv);
  SetOrientation(fv, tv);
  return (TransSpace::GetOrientation());
}

const SphrVector &
Object::ChangeRelOrientation(const SphrVector &deltaVec) {
  Vector3d fv, tv;

  TransSpace::ChangeRelOrientation(deltaVec);
  TransSpace::GetOrientation(fv, tv);
  SetOrientation(fv, tv);
  return (TransSpace::GetOrientation());
}

const Vector3d &
Object::SetScale(const Vector3d &newScale) {
  const Vector3d *rtnVal;

  if (!IsItemNode()) {
    rtnVal = &(TransSpace::SetScale(newScale));
    if (!IsRoot())
      (GetParent()).SetBoundRadius();
  }
  else {
    rtnVal = &(TransSpace::GetScale());
    // Sanity.
    check(rtnVal->GetX() == 1.0 &&
           rtnVal->GetY() == 1.0 &&
           rtnVal->GetZ() == 1.0);
  }

  return *rtnVal;
}

const Vector3d &
Object::ChangeScale(const Vector3d &scaleDelta) {
  const Vector3d *rtnVal;

  if (!IsItemNode()) {
    rtnVal = &(TransSpace::ChangeScale(scaleDelta));
    if (!IsRoot())
      (GetParent()).SetBoundRadius();
  }
  else {
    rtnVal = &(TransSpace::GetScale());
    // Sanity.
    check(rtnVal->GetX() == 1.0 &&
           rtnVal->GetY() == 1.0 &&
           rtnVal->GetZ() == 1.0);
  }

  return *rtnVal;
}

//===== Hit Detection =====
bool
Object::ResetBoundRadius(Object &obj) {
  obj.SetBoundRadius(false);
  return true;
}

float
Object::SetBoundRadius(bool bubble) {

  if (HasBrush()) {
    boundRadius = (GetBrush()).GetBoundRadius();
    return boundRadius;
  }

  if (IsItemNode()) {
    boundRadius = (GetItemWireFrame()).GetBoundRadius();
    return boundRadius;
  }

  if (HasMesh() || HasFoliage()) {
    boundRadius = HasLandscape() ? GetLandscape().GetBoundRadius() : HasFoliage() ? GetFoliage().GetBoundRadius() : GetMesh().GetBoundRadius();
    if (bubble && !(IsRoot()))
      (GetParent()).SetBoundRadius();
    return boundRadius;
  }

  Object *child;
  Vector3d bound;
  float m1, m2, tmp;

  boundRadius = 0.0f;

  IterLinkList<Object> iter(children);

  iter.Reset();
  while (!iter.IsDone()) {
    child = iter.GetNext();
    child->GetScale(bound);
    m1 = GeometryMath::Max(bound.GetX(), bound.GetY());
    m1 = GeometryMath::Max(m1, bound.GetZ());
    child->GetPosition(bound);
    m2 = bound.GetMag();
    tmp = child->GetBoundRadius() * m1 + m2;
    if (tmp > boundRadius)
      boundRadius = tmp;
  }

  if (bubble && !(IsRoot()))
    (GetParent()).SetBoundRadius();

  return boundRadius;
}

bool
Object::HitObject(const Line &ray, float rangeBegin,
                  float rangeEnd, float &depth) const {
  static const Vector3d origin(0.0f, 0.0f, 0.0f);
  Vector3d intrsctPt;
  Plane perp(ray.GetSlope(), origin);
  bool intrsct;

  intrsct = perp.Intersect(ray, intrsctPt);
  check(intrsct);  // Sanity Check.
  if (intrsctPt.GetMag() > GetBoundRadius() + 0.1)
    return false;

  if (HasBrush())
    return (GetBrush().HitGeometry(ray, rangeBegin, rangeEnd, depth));

  if (IsItemNode())
    return (GetItemWireFrame().HitGeometry(ray, rangeBegin, rangeEnd, depth));

  if (HasLandscape())
    return GetLandscape().Raycast(ray, rangeBegin, rangeEnd, depth);

  // Foliage is not picked (as in Unreal: its object is chosen in the
  // Outliner), nor in the way of rays to the ground under it.
  if (HasFoliage())
    return false;

  if (HasMesh())
    return GetMesh().HitMesh(ray, rangeBegin, rangeEnd, depth);

  // Check children.
  Object *chld;
  Line newRay;
  Matrix44 trans;
  // Matrix41 m;
  Vector3d p0, v0, v1;
  float zTmp;
  IterLinkList<Object> iter(children);

  intrsct = false;
  iter.Reset();
  while(!iter.IsDone()) {
    chld = iter.GetNext();
    chld->CalInvTransSpaceMatrix(trans.SetIdentity());

    // ((m.FillMatrix(ray.GetPoint())).Transform(trans)).FillVector(p0);
    p0 = ray.GetPoint();
    trans.Transform(p0);

    // (chld->GetPosition(v0)).AddVector(ray.GetSlope());
    // (m.Transform(trans, v0.GetX(), v0.GetY(), v0.GetZ())).FillVector(v1);
    v1.AddVector(chld->GetPosition(), ray.GetSlope());
    trans.Transform(v1);

    newRay.NewLine(p0, v1);
    if (chld->HitObject(newRay, rangeBegin, rangeEnd, zTmp)) {
      if (!intrsct || zTmp < depth)
        depth = zTmp;
      intrsct = true;
    }
  }
  return intrsct;
}

//===== CSG Subtraction =====
bool
Object::CSGSubtract(const LinkList<ObjectPtr> &cutters) {
  // Meshes and foliage take no part in CSG.
  if (HasMesh() || HasFoliage())
    return false;

#ifndef NDEBUG
  // Sanity.
  check(!IsRoot());
  check(!IsItemNode());
  check(!IsLeaf() || HasBrush());
  check(cutters.NumOfElm() > 0);
  Object *oPtr;
  for(int i = 0; i < cutters.NumOfElm(); i++) {
    oPtr = cutters[i].GetPtr();
    check(oPtr->GetParentPtr() == GetParentPtr());
	check(oPtr != this);
    // check(!oPtr->IsItemNode());
  }
#endif

  bool modified = false;

  // Set parent's matrix.
  Matrix44 t;
  GetParent().SetTransformation(t.SetIdentity());

  // Iter through cutters and perform subtraction.
  Object *cutterPtr;
  IterLinkList<ObjectPtr> iterCutters(cutters);
  iterCutters.Reset();
  while (!iterCutters.IsDone()) {
    cutterPtr = (iterCutters.GetNext())->GetPtr();

	if (cutterPtr->IsItemNode() || cutterPtr->HasMesh() || cutterPtr->HasFoliage())
		continue;

    if (CSGObjNodeSubtract(*cutterPtr, true))
      modified = true;

    if (IsLeaf() && !HasBrush())  {
      // Hmm... let caller handle deletion.
      // Nothing left.... Delete self.
      // GetParent().RemoveChild(*this, false);
      // delete this;
      return modified;
	}
  }

  // Set bound radius for all nodes in tree.
  PostOrderApply(ResetBoundRadius);

  return modified;
}

bool
Object::CSGObjNodeSubtract(Object &cutter, bool resetBound) {
  // Sanity.
  check(!IsRoot());
  check(!IsItemNode());
  check(this != &cutter);
  check(!IsMyAncestor(cutter));

  Matrix44 t;
  bool modified;

  CalInvTransSpaceMatrix(t.SetIdentity());
  t.Multiply(GetParent().GetTransformation());
  
  if (GetNumChildren() > 0) {
    check(!HasBrush());

    SetTransformation(t);

    Object *objPtr;
    IterLinkList<Object> iter(children);

	modified = false;
    iter.Reset();
    while (!iter.IsDone()) {
      objPtr = iter.GetNext();

      // Skip entities and meshes.
      if (objPtr->IsItemNode() || objPtr->HasMesh() || objPtr->HasFoliage())
        continue;

      if (objPtr->CSGObjNodeSubtract(cutter))
        modified = true;

      if (objPtr->IsLeaf() && !objPtr->HasBrush())
        delete &(RemoveChild(*objPtr));
	}

    return modified;
  }

  // Sanity.
  check(HasBrush());

  // Traverse the cutter tree now.
  cutter.CalTransSpaceMatrix(t);
  cutter.SetTransformation(t);
  modified = cutter.CSGTraverseCutter(*this);

  // Case 1: Brush was deleted.  Nothing left. 
  //  Do nothing. Let parent node handle deletion of current node.
  if (IsLeaf() && !HasBrush())
    return modified;

  // Case 2: One brush left.  Same obj node.
  if (HasBrush()) {
    check(IsLeaf());

    // Re-adjust position and orientation of obj.
    Vector3d posVec, sVec;
    SphrVector oriVec;

    CalTransSpaceMatrix(t.SetIdentity());
    brush->Transform(t);
    brush->CenterGeometry(posVec);
    SetPosition(posVec);
    SetOrientation(oriVec.NewVector(0.0f, 0.0f, 0.0f));
    SetScale(sVec.NewVector(1.0f, 1.0f, 1.0f));

    return modified;
  }

  // Case 3: Sub pieces are introduced.
  //  Float all the new sub pieces to the parent node
  //   if no model entity is attached.
  //  Let parent node handle deletion of current node.
  check(GetNumChildren() > 0);
  check(!HasBrush());

  if (!IsModelNode()) {
    if (resetBound)
      PostOrderApply(ResetBoundRadius);
    FloatChildren(false);
  }

  return modified;
}

bool
Object::CSGTraverseCutter(Object &cuttee) {
  // Sanity.
  check(!IsItemNode());
  check(this != &cuttee);
  check(!cuttee.IsMyAncestor(*this));
  check(cuttee.HasBrush());

  if (HasBrush()) {
    return CSGBrushSubtract(cuttee, *this);
  }

  IterLinkList<Object> iter(children);
  Object *objPtr;
  Matrix44 t;
  bool modified = false;

  iter.Reset();
  while (!iter.IsDone()) {
    objPtr = iter.GetNext();
    if (objPtr->IsItemNode() || objPtr->HasMesh() || objPtr->HasFoliage())
      continue;
    t = GetTransformation();
    objPtr->CalTransSpaceMatrix(t);
    objPtr->SetTransformation(t);
    if (objPtr->CSGTraverseCutter(cuttee))
      modified = true;
    if (!cuttee.HasBrush())
      break;
  }
  return modified;
}

bool
Object::CSGBrushSubtract(Object &cuttee, Object &cutter) {
  // PreCondition.
  check(cuttee.HasBrush());
  check(cutter.HasBrush());
  check(!cuttee.IsRoot());

  Geometry cutterBrush(cutter.GetBrush());
  LinkList<Geometry> *brushListPtr;
  Object *newObj;
  Vector3d posVec;
  const Vector3d sVec(1.0f, 1.0f, 1.0f);
  const SphrVector oriVec(0.0f, 0.0f, 0.0f);

  cutterBrush.Transform(cutter.GetTransformation());
  brushListPtr = Geometry::CSGSubtract(cuttee.GetBrush(), cutterBrush);

  if (brushListPtr == NULL)  // No modification was done.
    return false;

  delete cuttee.brush;
  cuttee.brush = NULL;

  if (brushListPtr->NumOfElm() == 1) {
    cuttee.brush = &(brushListPtr->RemoveNode(0));
  }
  else {
    while (brushListPtr->NumOfElm() > 0) {
      newObj = new Object();
      newObj->brush = &(brushListPtr->RemoveNode(0));
      cuttee.AddChild(*newObj, false);
    }
  }

  return true;
}

//===== CSG Intersect =====
Object *
Object::CSGIntersect(const LinkList<ObjectPtr> &objPtrs) const {
  Object *rtnVal;

  Geometry *pCombined = CSGSelfIntersect(&objPtrs);

  if (pCombined == NULL)
    rtnVal = NULL;
  else {
    Vector3d posVec;
    pCombined->CenterGeometry(posVec);

    rtnVal = new Object;
    rtnVal->SetBrush(pCombined);
    rtnVal->SetPosition(posVec);
  }
  
  return rtnVal;
}


Geometry *
Object::CSGSelfIntersect(const LinkList<ObjectPtr> *pObjsPtrs) const {

#ifndef NDEBUG
  if (pObjsPtrs) {
    for(int k = 0; k < pObjsPtrs->NumOfElm(); k++) {
      check((*pObjsPtrs)[k].GetPtr()->GetParentPtr() == this);
    }
  }
#endif

  int numListElm = 0;
  Geometry *rtnVal = NULL;

  if (pObjsPtrs == NULL) {
    if (IsItemNode() || (IsLeaf() && !HasBrush()))
      return NULL;
    if (HasBrush())
      rtnVal = new Geometry(GetBrush());    
    else
      numListElm = GetNumChildren();
  }
  else
    numListElm = pObjsPtrs->NumOfElm();

  if (numListElm > 0) {
    // Sanity.
    check(rtnVal == NULL);

    bool failed = false;
    Geometry *pNewBrush, *pHold, *pCombined = NULL;
    Object *pObj;

    for(int i = 0; i < numListElm; i++) {
      if (pObjsPtrs != NULL)
        pObj = (*pObjsPtrs)[i].GetPtr();
      else
        pObj = &(children[i]);

      pNewBrush = pObj->CSGSelfIntersect(NULL);

      if (pNewBrush == NULL) {
        failed = true;
        break;
      }

      if (pCombined == NULL) {
        pCombined = pNewBrush;
        pNewBrush = NULL;
        continue;
      }

      pHold = Geometry::CSGIntersect(*pNewBrush, *pCombined);
      if (pHold == NULL) {
        failed = true;
        break;
      }

      delete pNewBrush;
      pNewBrush = NULL;
      delete pCombined;
      pCombined = pHold;
    }

    if (pNewBrush)
      delete pNewBrush;

    if (failed) {
      if (pCombined)
        delete pCombined;
    }
    else {
      check(pCombined != NULL);
      rtnVal = pCombined;
    }
  }

  if (rtnVal != NULL) {
    Matrix44 t;
    CalTransSpaceMatrix(t.SetIdentity());
    rtnVal->Transform(t);
  }

  return rtnVal;
}

//===== Plane Clip =====
bool
Object::PlaneClip(const Plane &clipPlane, bool bubble) {

  if (IsItemNode() || HasMesh() || HasFoliage())
    return false;

  Matrix44 m;
  CalInvTransSpaceMatrix(m.SetIdentity());
  Plane cPlane(clipPlane);
  m.Transform(cPlane);

  if (HasBrush()) {
    Geometry *pNewBrush = GetBrush().PlaneClip(cPlane);
    if (pNewBrush == &(GetBrush()))
      return false;
    else if (pNewBrush != NULL) {
      delete brush;
      brush = pNewBrush;
      SetBoundRadius(bubble);
      return true;
    }
    else { // pNewBrush == NULL
      // Let caller handle deletion of object node.
      delete brush;
      brush = NULL;
      return true;
    }
  }

  check(GetNumChildren() > 0);

  bool rtnVal = false;

  Object *pChild;
  IterLinkList<Object> *pIter = &(GetIterChildren());
  pIter->Reset();
  while (!pIter->IsDone()) {
    pChild = pIter->GetNext();
    if (pChild->PlaneClip(cPlane, false)) {
      rtnVal = true;
      if (!pChild->IsItemNode() && !pChild->HasBrush() &&
          pChild->GetNumChildren() == 0) {
        delete &(RemoveChild(*pChild, false));
      }
    }
  }

  if (rtnVal)
    SetBoundRadius(bubble);

  return rtnVal;
}

//===== Object: load/save geometries =====

Object *
Object::CreateObjFromFile(const char *fName,
                          int (*ErrorPromptFunc)(const char *message),
                          bool (*ProgressFunc)(int percent)) {
  Object *rtnObj = new Object;

  if (!rtnObj->LoadObjFile(fName, ErrorPromptFunc, ProgressFunc)) {
    delete rtnObj;
    return NULL;
  }
  return rtnObj;
}

#define PROGRESS_BRUSH	4
#define PROGRESS_ENTITY	1
#define PROGRESS_OBJECT	1

bool
Object::LoadObjFile(const char *fName,
                    int (*ErrorPromptFunc)(const char *message),
                    bool (*ProgressFunc)(int percent)) {
  // PreCondition.
  check(fName != NULL);

  bool rtnVal;
  LFile *inFile = new LFile;
  Geometry *holdPtr;
  Entity *holdEntPtr;
  LinkList<GeometryPtr> shapes;
  LinkList<EntityPtr> entities;

  int brushNum, entNum, objNum;
  int numBrushError, numEntError;
  int pDone, pTotal;
  int i, errorVal;
  bool ignore;
  char tmpError[80];

  if (!inFile->Open(fName)) {
    // Clear self.
    DelObj(false);
    return false;
  }
  inFile->ResetLineNumber();

  brushNum = entNum = objNum = 0;
  // if (strncmp(inFile->GetNextLine(), "// GENESIS", 8) != 0)
  //   goto ERROR_LOAD_OBJFILE;
  while (sscanf(inFile->GetNextLine(), "// Brushes: %d", &brushNum) != 1) {
    if (inFile->EndOfFile())
      goto ERROR_LOAD_OBJFILE;
  }
  while (sscanf(inFile->GetNextLine(), "// Entities: %d", &entNum) != 1) {
    if (inFile->EndOfFile())
      goto ERROR_LOAD_OBJFILE;
  }
  while (sscanf(inFile->GetNextLine(), "// Objects: %d", &objNum) != 1) {
    if (inFile->EndOfFile())
      goto ERROR_LOAD_OBJFILE;
  }

  // Clean up self.
  DelObj(false);

  ignore = false;
  if (ProgressFunc) {
    pDone = 0;
    pTotal = brushNum * PROGRESS_BRUSH
			+ entNum * PROGRESS_ENTITY
			+ objNum * PROGRESS_OBJECT;
    if(!(*ProgressFunc)(pDone)) {
      return false;
	}
  }

  // Load all the brushes in first.  Store them in a list.
  numBrushError = 0;
  for(i = 0; i < brushNum; i++) {
    // Update progress meter.
    if (ProgressFunc != NULL) {
      pDone += PROGRESS_BRUSH;
      (*ProgressFunc)((int) (pDone * 100.0f / pTotal));
    }

    if ((holdPtr = Geometry::ReadBoundGeometry(inFile, i)) == NULL) {
      numBrushError++;
      if (ErrorPromptFunc != NULL && !ignore) {
        sprintf(tmpError,
                "Error loading brush before line %d.\n"
                "Ignore and continue?", inFile->GetLineNumber());
        errorVal = ErrorPromptFunc(tmpError);
        if (errorVal == -1) { // Abort Load.
          rtnVal = false;
          goto RTN_LOADOBJFILE;
        }
        else if (errorVal == 1) // Ignore Errors.
          ignore = true;
      }
    }
    shapes.AppendNode(*(new GeometryPtr(holdPtr, i)));
  }

  // Load all the entities in.  Store in list.
  numEntError = 0;
  for(i = 0; i < entNum; i++) {
    // Update progress meter.
    if (ProgressFunc != NULL) {
      pDone += PROGRESS_ENTITY;
      (*ProgressFunc)((int) (pDone * 100.0f / pTotal));
    }

    if ((holdEntPtr = Entity::ReadEntity(inFile, i)) == NULL) {
      numEntError++;
      if (ErrorPromptFunc != NULL && !ignore) {
        sprintf(tmpError,
                "Error loading entity before line %d.\n"
                "Ignore and continue?", inFile->GetLineNumber());
        errorVal = ErrorPromptFunc(tmpError);
        if (errorVal == -1) {  // Abort Load
          rtnVal = false;
          goto RTN_LOADOBJFILE;
        }
        else if (errorVal == 1) // Ingore Errors.
          ignore = true;
      }
    }
    entities.AppendNode(*(new EntityPtr(holdEntPtr, i)));
  }

  // Now load object grouping information.
  if (LoadObject(inFile, 0, shapes, entities, 
                 pDone, pTotal, ProgressFunc) != 1)
    goto ERROR_LOAD_OBJFILE;

  // Set bound radius for all nodes.
  PostOrderApply(ResetBoundRadius);

  rtnVal = true;
  goto RTN_LOADOBJFILE;

ERROR_LOAD_OBJFILE:
  LError("Parse error before line # %d\n", inFile->GetLineNumber());

  // Clear self.
  DelObj(false);

  rtnVal = false;

RTN_LOADOBJFILE:
  if (inFile != NULL) {
    inFile->Close();
    delete inFile;
  }

  // Clean up any left over geometries.
  while (shapes.NumOfElm() > 0) {
    if (shapes[0].GetPtr() != NULL)
      delete shapes[0].GetPtr();
    delete &(shapes.RemoveNode(0));
  }

  // Clean up any left over entities.
  while (entities.NumOfElm() > 0) {
    if (entities[0].GetPtr() != NULL)
      delete entities[0].GetPtr();
    delete &(entities.RemoveNode(0));
  }

  if (rtnVal && (numBrushError > 0 || numEntError > 0))
    LError("Ignored %d bad brushes and %d bad entities.",
            numBrushError, numEntError);

  return rtnVal;
}


bool
Object::SaveObjFile(const char *fName, bool append,
                    bool (*ProgressFunc)(int percent)) {
  // PreCondition.
  check(fName != NULL);

  FILE *fOutPtr;
  int brushNum, entNum, objNum;
  int brushSaved, entSaved, objSaved;
  int pDone, pTotal;

  if ((fOutPtr = fopen(fName, (append ? "at" : "wt"))) == NULL)
    return false;

  brushNum = CountBrushes();
  entNum = CountEntities();
  objNum = CountNodes();

  // fprintf(fOutPtr, "// GENESIS\n");
  fprintf(fOutPtr, "// Brushes: %d\n", brushNum);
  fprintf(fOutPtr, "// Entities: %d\n", entNum);
  fprintf(fOutPtr, "// Objects: %d\n", objNum);
  fprintf(fOutPtr, "\n");

  if (ProgressFunc) {
    pDone = 0;
    pTotal = brushNum + entNum + objNum;
    (*ProgressFunc)(pDone);
  }

  // Remember position.
  Vector3d origPosition = TransSpace::GetPosition();
  TransSpace::SetPosition(Vector3d::origVec);

  brushSaved = entSaved = objSaved = 0;
  AssignObjIds(brushSaved, entSaved, objSaved);  
  brushSaved = SaveBrushes(fOutPtr, pDone, pTotal, ProgressFunc);
  entSaved = SaveEntities(fOutPtr, pDone, pTotal, ProgressFunc);
  objSaved = SaveObject(fOutPtr, pDone, pTotal, ProgressFunc);

  // Restore position.
  TransSpace::SetPosition(origPosition);

  // Sanity Check.
  check(brushSaved == brushNum);
  check(entSaved == entNum);
  check(objSaved == objNum);

  if (fOutPtr != NULL)
    fclose(fOutPtr);

  return true;
}

int
Object::LoadObject(LFile *inFile, int objNum,
                   LinkList<GeometryPtr> &shapes,
                   LinkList<EntityPtr> &entities,
                   int &pDone, int &pTotal,
                   bool (*ProgressFunc)(int percent)) {
  int rtnVal;
  char srch[20];
  float f1, f2, f3, f4, f5, f6;
  int i1;
  const char *s;
  Vector3d inVec, inVec2;
  SphrVector inSVec1;
  Object *holdPtr;
  IterLinkList<Object> iter(children);
  int landQuadsX = -1, landQuadsY = 0;
  float landQuadSize = 0.0f;
  std::vector<float> landHeights;
  std::vector<LandscapeLayer> landLayers;
  std::vector<LandscapeSpline> landSplines;
  size_t landTangentsRead = 0; // of the last spline's points
  bool foliageLoaded = false;
  std::vector<FoliageType> foliageTypes;
  std::vector<FoliageInstance> foliageInstances;

  if (ProgressFunc) {
    pDone += PROGRESS_OBJECT;
    (*ProgressFunc)((int) (pDone * 100.0f / pTotal));
  }

  // Clear Self; 
  DelObj(false, false);

  // Find object in file.
  sprintf(srch, "{ // Object %d", objNum);  
  if (inFile->Search(srch) == NULL)
    return false;

  s = strchr(inFile->GetLine(), ':');
  SetObjName(s);

  // Parse until the end of object section.
  while (1) {
    if (inFile->GetNextLine() == NULL)
      goto ERROR_LOAD_OBJECT;
    else if (strncmp(inFile->GetLine(), "}", 1) == 0)
      break;
    else if (strncmp(inFile->GetLine(), "Position: ", 10) == 0) {
      if (sscanf(inFile->GetLine() + 10, "(%f, %f, %f)", &f1, &f2, &f3) != 3)
        goto ERROR_LOAD_OBJECT;
      TransSpace::SetPosition(inVec.NewVector(f1, f2, f3));
    }
    else if (strncmp(inFile->GetLine(), "Orientation (deg): ", 19) == 0) {
      if (sscanf(inFile->GetLine() + 19, "(%f, %f, %f)", &f1, &f2, &f3) != 3)
        goto ERROR_LOAD_OBJECT;
      f1 = DEG2RAD(f1);
      f2 = DEG2RAD(f2);
      f3 = DEG2RAD(f3);
      TransSpace::SetOrientation(inSVec1.NewVector(f1, f2, f3));
    }
    else if (strncmp(inFile->GetLine(), "Orientation (rad): ", 19) == 0) {
      if (sscanf(inFile->GetLine() + 19, "(%f, %f, %f)", &f1, &f2, &f3) != 3)
        goto ERROR_LOAD_OBJECT;
      TransSpace::SetOrientation(inSVec1.NewVector(f1, f2, f3));
    }
    else if (strncmp(inFile->GetLine(), "Orientation (f,t):", 19) == 0) {
      if (sscanf(inFile->GetLine() + 19, "(%f, %f, %f), (%f, %f, %f)",
                 &f1, &f2, &f3, &f4, &f5, &f6) != 6)
        goto ERROR_LOAD_OBJECT;
      TransSpace::SetOrientation(inVec.NewVector(f1, f2, f3),
                                 inVec2.NewVector(f4, f5, f6));
    }
    else if (strncmp(inFile->GetLine(), "Orientation (rad):", 19) == 0) {
      if (sscanf(inFile->GetLine() + 19, "(%f, %f, %f)", &f1, &f2, &f3) != 3)
        goto ERROR_LOAD_OBJECT;
      TransSpace::SetOrientation(inSVec1.NewVector(f1, f2, f3));
    }
    else if (strncmp(inFile->GetLine(), "Scale: ", 7) == 0) {
      if (sscanf(inFile->GetLine() + 7, "(%f, %f, %f)", &f1, &f2, &f3) != 3)
        goto ERROR_LOAD_OBJECT;
      TransSpace::SetScale(inVec.NewVector(f1, f2, f3));
    }
    else if (strncmp(inFile->GetLine(), "Brush:", 6) == 0) {
      if (!IsLeaf() || HasBrush())
        goto ERROR_LOAD_OBJECT;
      if (sscanf(inFile->GetLine() + 6, "%d", &i1) != 1)
        goto ERROR_LOAD_OBJECT;
       if (i1 < 0 || shapes[0].GetRec() != i1)
        goto ERROR_LOAD_OBJECT;
      brush = shapes[0].GetPtr();
      delete &(shapes.RemoveNode(0));
      if (brush == NULL)
        return 0;
    }
    else if (strncmp(inFile->GetLine(), "Entity:", 7) == 0) {
      if (sscanf(inFile->GetLine() + 7, "%d", &i1) != 1)
        goto ERROR_LOAD_OBJECT;
      if (HasEntity() || i1 < 0 || entities[0].GetRec() != i1)
        goto ERROR_LOAD_OBJECT;
	  SetEntity(entities[0].GetPtr(), false);
      delete &(entities.RemoveNode(0));
      if (entityPtr == NULL)
        return 0;
    }
    else if (strncmp(inFile->GetLine(), "Landscape: ", 11) == 0) {
      // Genesis: a heightfield, its heights on the lines after.
      if (!IsLeaf() || HasBrush() || HasEntity() ||
          sscanf(inFile->GetLine() + 11, "%d %d %f", &landQuadsX, &landQuadsY, &landQuadSize) != 3)
        goto ERROR_LOAD_OBJECT;
      landHeights.reserve((size_t) (landQuadsX + 1) * (landQuadsY + 1));
    }
    else if (strncmp(inFile->GetLine(), "LandscapeLayer: ", 16) == 0) {
      // A paint layer: its color and name, its weights on the lines after.
      LandscapeLayer layer;
      int r = 255, g = 255, b = 255, used = 0;
      if (landQuadsX < 0 || sscanf(inFile->GetLine() + 16, "%d %d %d %n", &r, &g, &b, &used) < 3)
        goto ERROR_LOAD_OBJECT;
      layer.r = (unsigned char) GeometryMath::Max(0, GeometryMath::Min(r, 255));
      layer.g = (unsigned char) GeometryMath::Max(0, GeometryMath::Min(g, 255));
      layer.b = (unsigned char) GeometryMath::Max(0, GeometryMath::Min(b, 255));
      layer.name = inFile->GetLine() + 16 + used;
      while (!layer.name.empty() && (layer.name.back() == '\n' || layer.name.back() == '\r' || layer.name.back() == ' '))
        layer.name.pop_back();
      landLayers.push_back(std::move(layer));
    }
    else if (strncmp(inFile->GetLine(), "Foliage:", 8) == 0) {
      if (!IsLeaf() || HasBrush() || HasEntity() || HasMesh())
        goto ERROR_LOAD_OBJECT;
      foliageLoaded = true;
    }
    else if (strncmp(inFile->GetLine(), "FoliageType: ", 13) == 0) {
      // Its numbers, its mesh, then its name to the end of the line.
      FoliageType type;
      int align = 0, randomYaw = 1, used = 0;
      char meshName[128] = { 0 };
      if (!foliageLoaded ||
          sscanf(inFile->GetLine() + 13, "%f %f %f %f %d %f %d %f %f %f %f %f %f %127s %n", &type.density, &type.radius,
                 &type.scaleMin, &type.scaleMax, &align, &type.alignMaxAngle, &randomYaw, &type.slopeMin, &type.slopeMax,
                 &type.heightMin, &type.heightMax, &type.zOffset, &type.cullDistance, meshName, &used) < 14)
        goto ERROR_LOAD_OBJECT;
      type.alignToNormal = align != 0;
      type.randomYaw = randomYaw != 0;
      type.mesh = strcmp(meshName, "-") == 0 ? "" : meshName;
      type.name = used > 0 ? inFile->GetLine() + 13 + used : "";
      while (!type.name.empty() && (type.name.back() == '\n' || type.name.back() == '\r' || type.name.back() == ' '))
        type.name.pop_back();
      foliageTypes.push_back(std::move(type));
    }
    else if (strncmp(inFile->GetLine(), "FoliageInstances:", 17) == 0) {
      // Groups of nine: type, x y z, up x y z, yaw, scale.
      if (!foliageLoaded)
        goto ERROR_LOAD_OBJECT;
      const char *p = inFile->GetLine() + 17;
      char *end = NULL;
      float v[9];
      while (true) {
        int got = 0;
        for (; got < 9; got++) {
          v[got] = strtof(p, &end);
          if (end == p)
            break;
          p = end;
        }
        if (got < 9)
          break;
        FoliageInstance instance;
        instance.type = (int) v[0];
        instance.position.NewVector(v[1], v[2], v[3]);
        instance.up.NewVector(v[4], v[5], v[6]);
        instance.yaw = v[7];
        instance.scale = v[8];
        foliageInstances.push_back(instance);
      }
    }
    else if (strncmp(inFile->GetLine(), "LandscapeSpline: ", 17) == 0) {
      // Closed, raises, lowers, then the layer it paints to the end.
      LandscapeSpline spline;
      int closed = 0, raise = 1, lower = 1, used = 0;
      if (landQuadsX < 0 || sscanf(inFile->GetLine() + 17, "%d %d %d %n", &closed, &raise, &lower, &used) < 3)
        goto ERROR_LOAD_OBJECT;
      spline.closed = closed != 0;
      spline.raiseTerrain = raise != 0;
      spline.lowerTerrain = lower != 0;
      spline.layer = inFile->GetLine() + 17 + used;
      while (!spline.layer.empty() && (spline.layer.back() == '\n' || spline.layer.back() == '\r' || spline.layer.back() == ' '))
        spline.layer.pop_back();
      landSplines.push_back(std::move(spline));
      landTangentsRead = 0;
    }
    else if (strncmp(inFile->GetLine(), "LandscapeSplineMesh: ", 21) == 0) {
      // The last spline's mesh: scale, fit to width, z offset, its name.
      float scale = 1.0f, zOffset = 0.0f;
      int fit = 1;
      char meshName[128] = { 0 };
      if (landSplines.empty() || sscanf(inFile->GetLine() + 21, "%f %d %f %127s", &scale, &fit, &zOffset, meshName) < 4)
        goto ERROR_LOAD_OBJECT;
      LandscapeSpline &spline = landSplines.back();
      spline.meshScale = scale;
      spline.meshFitWidth = fit != 0;
      spline.meshZOffset = zOffset;
      spline.mesh = meshName;
    }
    else if (strncmp(inFile->GetLine(), "LandscapeSplineTangents:", 24) == 0) {
      // For the last spline's points in turn, groups of five: automatic (1)
      // or not, the tangent's x y z, the joint.
      if (landSplines.empty())
        goto ERROR_LOAD_OBJECT;
      LandscapeSpline &spline = landSplines.back();
      const char *p = inFile->GetLine() + 24;
      char *end = NULL;
      size_t &index = landTangentsRead;
      while (index < spline.points.size()) {
        float v[5];
        int got = 0;
        for (; got < 5; got++) {
          v[got] = strtof(p, &end);
          if (end == p)
            break;
          p = end;
        }
        if (got < 5)
          break;
        LandscapeSplinePoint &point = spline.points[index++];
        point.autoTangent = v[0] != 0.0f;
        point.tangent.NewVector(v[1], v[2], v[3]);
        point.joint = (int) v[4];
      }
    }
    else if (strncmp(inFile->GetLine(), "LandscapeSplinePoints:", 22) == 0) {
      // Groups of five: x y z, width, falloff.
      if (landSplines.empty())
        goto ERROR_LOAD_OBJECT;
      const char *p = inFile->GetLine() + 22;
      char *end = NULL;
      while (true) {
        float v[5];
        int got = 0;
        for (; got < 5; got++) {
          v[got] = strtof(p, &end);
          if (end == p)
            break;
          p = end;
        }
        if (got < 5)
          break;
        LandscapeSplinePoint point;
        point.position.NewVector(v[0], v[1], v[2]);
        point.width = v[3];
        point.falloff = v[4];
        landSplines.back().points.push_back(point);
      }
    }
    else if (strncmp(inFile->GetLine(), "LandscapeTexture: ", 18) == 0) {
      // The last layer's texture: its tiling, then the texture to the end.
      float tiling = 0.0f;
      int used = 0;
      if (landLayers.empty() || sscanf(inFile->GetLine() + 18, "%f %n", &tiling, &used) < 1)
        goto ERROR_LOAD_OBJECT;
      LandscapeLayer &layer = landLayers.back();
      layer.tiling = GeometryMath::Max(tiling, 1.0f);
      layer.texture = inFile->GetLine() + 18 + used;
      while (!layer.texture.empty() && (layer.texture.back() == '\n' || layer.texture.back() == '\r' || layer.texture.back() == ' '))
        layer.texture.pop_back();
    }
    else if (strncmp(inFile->GetLine(), "LandscapeWeights:", 17) == 0) {
      if (landLayers.empty())
        goto ERROR_LOAD_OBJECT;
      const char *p = inFile->GetLine() + 17;
      char *end = NULL;
      while (true) {
        const float w = strtof(p, &end);
        if (end == p)
          break;
        landLayers.back().weights.push_back(w);
        p = end;
      }
    }
    else if (strncmp(inFile->GetLine(), "LandscapeHeights:", 17) == 0) {
      const char *p = inFile->GetLine() + 17;
      char *end = NULL;
      while (true) {
        const float h = strtof(p, &end);
        if (end == p)
          break;
        landHeights.push_back(h);
        p = end;
      }
    }
    else if (strncmp(inFile->GetLine(), "  StaticMesh: ", 14) == 0 || strncmp(inFile->GetLine(), "StaticMesh: ", 12) == 0) {
      // Genesis: a mesh by name. Unknown meshes leave the object empty
      // (dropped below), as older readers drop the whole line.
      const char *meshName = strchr(inFile->GetLine(), ':') + 1;
      while (*meshName == ' ')
        meshName++;
      std::string trimmed(meshName);
      while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r' || trimmed.back() == ' '))
        trimmed.pop_back();
      if (!IsLeaf() || HasBrush() || HasEntity())
        goto ERROR_LOAD_OBJECT;
      mesh = StaticMesh::Find(trimmed.c_str());
      if (mesh == nullptr)
        LError("Unknown static mesh \"%s\" before line %d.", trimmed.c_str(), inFile->GetLineNumber());
    }
    else if (strncmp(inFile->GetLine(), "Objects:", 8) == 0) {
      if (HasBrush() || IsItemNode() || HasMesh() || foliageLoaded)
        goto ERROR_LOAD_OBJECT;
      s = inFile->GetLine() + 8;
      while (sscanf(s, "%d", &i1) == 1) {
        holdPtr = new Object;
        holdPtr->objID = i1;
        AddChild(*holdPtr, false);
        if ((s = strchr(s, ',')) == NULL)
          break;
        s++;
      }
    }
  }

  if (foliageLoaded)
    SetFoliage(std::make_shared<Foliage>(std::move(foliageTypes), std::move(foliageInstances)));

  if (landQuadsX >= 0) {
    std::shared_ptr<const Landscape> land = Landscape::Create(landQuadsX, landQuadsY, landQuadSize, std::move(landHeights), std::move(landLayers), std::move(landSplines));
    if (land == nullptr)
      LError("A landscape's heights or layer weights do not match its size before line %d.", inFile->GetLineNumber());
    else
      SetLandscape(land);
  }

  // Load in all sub objects.
  iter.Reset();
  while (!iter.IsDone()) {
    holdPtr = iter.GetNext();
    rtnVal = holdPtr->LoadObject(inFile, holdPtr->objID, shapes, entities,
                                 pDone, pTotal, ProgressFunc);
    if (rtnVal == 0) {
      RemoveChild(*holdPtr, false);
      delete holdPtr;
    }
    else if (rtnVal == -1) {
      goto ERROR_LOAD_OBJECT;
    }
  }

  // Check for empty nodes.
  if (IsLeaf() && !(HasBrush() || IsItemNode() || HasMesh() || HasFoliage()))
    rtnVal = 0;
  else
    rtnVal = 1;

  goto RTN_LOAD_OBJECT;

ERROR_LOAD_OBJECT:
  rtnVal = -1;
  while (children.NumOfElm() > 0)
    delete &(RemoveChild(GetChild(0), false));

RTN_LOAD_OBJECT:
  return rtnVal;
}

void
Object::AssignObjIds(int &brushCnt, int &entCnt, int &objCnt) {
  // PreCondition.
  check(brushCnt >=0 && entCnt >= 0 && objCnt >= 0);

  brushID = (HasBrush() ? brushCnt++ : -1);
  entID = (HasEntity() ? entCnt++ : -1);
  objID = objCnt++;

  IterLinkList<Object> iter(children);

  // Now traverse the children.
  iter.Reset();
  while (!iter.IsDone()) {
    (iter.GetNext())->AssignObjIds(brushCnt, entCnt, objCnt);
  }
}

int
Object::SaveObject(FILE *outFile, int &pDone, int &pTotal, 
                   bool (*ProgressFunc)(int percent)) {
  // Sanity Check.
  check(IsLeaf() || (!HasBrush() && !IsItemNode()));

  int i, id, rtnVal;
  Vector3d vec1;
  SphrVector sVec1;
  IterLinkList<Object> iter(children);

  fprintf(outFile, "{ // Object %d : %s\n", objID, GetObjName());
  GetPosition(vec1);
  fprintf(outFile, "  Position: (%s,", FormatFloat(vec1.GetX()));
  fprintf(outFile, " %s,", FormatFloat(vec1.GetY()));
  fprintf(outFile, " %s)\n", FormatFloat(vec1.GetZ()));
  GetOrientation(sVec1);
  fprintf(outFile, "  Orientation (rad): (%s,", FormatFloat(sVec1.GetYaw()));
  fprintf(outFile, " %s,", FormatFloat(sVec1.GetPitch()));
  fprintf(outFile, " %s)\n", FormatFloat(sVec1.GetRoll()));
  GetScale(vec1);
  fprintf(outFile, "  Scale: (%s,", FormatFloat(vec1.GetX()));
  fprintf(outFile, " %s,", FormatFloat(vec1.GetY()));
  fprintf(outFile, " %s)\n", FormatFloat(vec1.GetZ()));

  if (GetNumChildren() > 0) {
    iter.Reset();
    for(i = 0; !iter.IsDone(); i++) {
      check(i < GetNumChildren());  // Sanity.
      id = (iter.GetNext())->objID;
      check(id >= 0);  // Sanity.
      if ((i % 10) == 0) {
        if (i != 0)
          fprintf(outFile, "\n");
        fprintf(outFile, "  Objects: %d", id);
      }
      else
        fprintf(outFile, ", %d", id);
    }
    fprintf(outFile, "\n");
  }

  if (HasBrush()) {
    check(!IsItemNode());
    check(brushID >= 0);
    fprintf(outFile, "  Brush: %d\n", brushID);
  }

  if (HasEntity()) {
    // Sanity Check.
    check(entID >= 0);
    fprintf(outFile, "  Entity: %d\n", entID);
  }

  if (HasLandscape()) {
    const Landscape &land = GetLandscape();
    fprintf(outFile, "  Landscape: %d %d %s\n", land.GetQuadsX(), land.GetQuadsY(), FormatFloat(land.GetQuadSize()));
    // 32 to a line: the reader's lines hold 1024 characters.
    const std::vector<float> &heights = land.GetHeights();
    for (size_t i = 0; i < heights.size(); i += 32) {
      fprintf(outFile, "  LandscapeHeights:");
      for (size_t j = i; j < GeometryMath::Min(heights.size(), i + 32); j++)
        fprintf(outFile, " %s", FormatFloat(heights[j]));
      fprintf(outFile, "\n");
    }
    for (const LandscapeLayer &layer : land.GetLayers()) {
      fprintf(outFile, "  LandscapeLayer: %d %d %d %s\n", layer.r, layer.g, layer.b, layer.name.c_str());
      if (!layer.texture.empty())
        fprintf(outFile, "  LandscapeTexture: %s %s\n", FormatFloat(layer.tiling), layer.texture.c_str());
      for (size_t i = 0; i < layer.weights.size(); i += 32) {
        fprintf(outFile, "  LandscapeWeights:");
        for (size_t j = i; j < GeometryMath::Min(layer.weights.size(), i + 32); j++)
          fprintf(outFile, " %s", FormatFloat(layer.weights[j]));
        fprintf(outFile, "\n");
      }
    }
    for (const LandscapeSpline &spline : land.GetSplines()) {
      fprintf(outFile, "  LandscapeSpline: %d %d %d %s\n", spline.closed ? 1 : 0, spline.raiseTerrain ? 1 : 0, spline.lowerTerrain ? 1 : 0,
              spline.layer.c_str());
      if (!spline.mesh.empty()) {
        // FormatFloat's buffer is shared: a copy of each.
        const std::string scale = FormatFloat(spline.meshScale), zOffset = FormatFloat(spline.meshZOffset);
        fprintf(outFile, "  LandscapeSplineMesh: %s %d %s %s\n", scale.c_str(), spline.meshFitWidth ? 1 : 0, zOffset.c_str(), spline.mesh.c_str());
      }
      // 12 to a line: the reader's lines hold 1024 characters.
      for (size_t i = 0; i < spline.points.size(); i += 12) {
        fprintf(outFile, "  LandscapeSplinePoints:");
        for (size_t j = i; j < GeometryMath::Min(spline.points.size(), i + 12); j++) {
          const LandscapeSplinePoint &point = spline.points[j];
          for (const float v : { point.position.GetX(), point.position.GetY(), point.position.GetZ(), point.width, point.falloff })
            fprintf(outFile, " %s", FormatFloat(v));
        }
        fprintf(outFile, "\n");
      }
      // Tangents set by hand and junctions, when there are any (older
      // readers skip the line).
      bool special = false;
      for (const LandscapeSplinePoint &point : spline.points)
        special |= !point.autoTangent || point.joint >= 0;
      for (size_t i = 0; special && i < spline.points.size(); i += 12) {
        fprintf(outFile, "  LandscapeSplineTangents:");
        for (size_t j = i; j < GeometryMath::Min(spline.points.size(), i + 12); j++) {
          const LandscapeSplinePoint &point = spline.points[j];
          fprintf(outFile, " %d", point.autoTangent ? 1 : 0);
          for (const float v : { point.tangent.GetX(), point.tangent.GetY(), point.tangent.GetZ() })
            fprintf(outFile, " %s", FormatFloat(v));
          fprintf(outFile, " %d", point.joint);
        }
        fprintf(outFile, "\n");
      }
    }
  }
  else if (HasMesh())
    fprintf(outFile, "  StaticMesh: %s\n", GetMeshName());
  else if (HasFoliage()) {
    const Foliage &foliage = GetFoliage();
    fprintf(outFile, "  Foliage: %d\n", foliage.GetNumTypes());
    for (const FoliageType &type : foliage.GetTypes()) {
      // FormatFloat's buffer is shared: a call for each.
      fprintf(outFile, "  FoliageType: %s", FormatFloat(type.density));
      for (const float v : { type.radius, type.scaleMin, type.scaleMax })
        fprintf(outFile, " %s", FormatFloat(v));
      fprintf(outFile, " %d %s %d", type.alignToNormal ? 1 : 0, FormatFloat(type.alignMaxAngle), type.randomYaw ? 1 : 0);
      for (const float v : { type.slopeMin, type.slopeMax, type.heightMin, type.heightMax, type.zOffset, type.cullDistance })
        fprintf(outFile, " %s", FormatFloat(v));
      fprintf(outFile, " %s %s\n", type.mesh.empty() ? "-" : type.mesh.c_str(), type.name.c_str());
    }
    // 6 to a line: the reader's lines hold 1024 characters.
    const std::vector<FoliageInstance> &instances = foliage.GetInstances();
    for (size_t i = 0; i < instances.size(); i += 6) {
      fprintf(outFile, "  FoliageInstances:");
      for (size_t j = i; j < GeometryMath::Min(instances.size(), i + 6); j++) {
        const FoliageInstance &f = instances[j];
        fprintf(outFile, " %d", f.type);
        for (const float v : { f.position.GetX(), f.position.GetY(), f.position.GetZ(), f.up.GetX(), f.up.GetY(), f.up.GetZ(), f.yaw, f.scale })
          fprintf(outFile, " %s", FormatFloat(v));
      }
      fprintf(outFile, "\n");
    }
  }

  fprintf(outFile, "} // Object %d\n", objID);
  fprintf(outFile, "\n");

  if (ProgressFunc) {
    pDone += 1;
    (*ProgressFunc)((int) (pDone * 100.0f / pTotal));
  }

  // Now traverse all children.
  rtnVal = 1;
  iter.Reset();
  while (!iter.IsDone()) {
    rtnVal += (iter.GetNext())->
        SaveObject(outFile, pDone, pTotal, ProgressFunc);
  }

  return rtnVal;
}

int
Object::SaveBrushes(FILE *outFile, int &pDone, int &pTotal,
                    bool (*ProgressFunc)(int percent)) {
  // Sanity Check
  check(IsLeaf() || (!HasBrush() && !IsItemNode()));

  IterLinkList<Object> iter(children);
  int rtnVal = 0;

  // Write out the current brush.
  if (HasBrush()) {
    Matrix44 t;
    t.SetIdentity();
    check(brushID >= 0);
    Geometry::WriteBoundGeometry(outFile, *brush, brushID, t);
    fprintf(outFile, "\n");
    rtnVal++;

    if (ProgressFunc) {
      pDone += 1;
      (*ProgressFunc)((int) (pDone * 100.0f / pTotal));
	}
  }

  // Traverse All Children.
  iter.Reset();
  while (!iter.IsDone()) {
    rtnVal += (iter.GetNext())->
        SaveBrushes(outFile, pDone, pTotal, ProgressFunc);
  }

  return rtnVal;
}

int
Object::SaveEntities(FILE *outFile, int &pDone, int &pTotal,
                     bool (*ProgressFunc)(int percent)) {
  // Sanity Check.
  check(IsLeaf() || (!HasBrush() && !IsItemNode()));

  IterLinkList<Object> iter(children);
  int rtnVal = 0;

  // Write out the current entity.
  if (HasEntity()) {
    // Sanity Check.
    check(!IsItemNode() || IsLeaf());
    check(entID >= 0);
    Entity::WriteEntity(outFile, *(GetEntityPtr()), entID);
    fprintf(outFile, "\n");
    rtnVal++;

    if (ProgressFunc) {
      pDone += 1;
      (*ProgressFunc)((int) (pDone * 100.0f / pTotal));
	}
  }

  // Now traverse the children.
  iter.Reset();
  while (!iter.IsDone()) {
    rtnVal += (iter.GetNext())->
        SaveEntities(outFile, pDone, pTotal, ProgressFunc);
  }

  return rtnVal;
}

//========== Testing ==========
#if 0
Object *
Object::LoadWFGeom(const GeomWFInfo &infoRec) {
  Object *rtnVal = NULL;
  Geometry *geomPtr = NULL;

  // geomPtr = Geometry::ReadGeomFile(fName);
  geomPtr = Geometry::NewWFGeom(infoRec);
  if (geomPtr != NULL) {
    rtnVal = new Object;
    rtnVal->geomPtr = geomPtr;
  }
  rtnVal->SetBoundRadius(false);

  return rtnVal;
}
#endif
