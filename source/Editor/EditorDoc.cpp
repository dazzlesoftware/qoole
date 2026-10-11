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

// FEditorDoc.cpp : implementation of the FEditorDoc class
//

#include "stdafx.h"

#include "GenesisEditor.h"
#include "EditorDoc.h"
#include "MainFrm.h"
#include "MapIO.h"
#include "Draw.h"

#ifdef _SHAREWARE_DEMO_
#define DEMO_BRUSHCOUNT		400
#endif

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

//======================================================================
// FDocTemplate
//======================================================================

// Overriding MFC's CMultiDocTemplate::OpenDocumentFile().
// Copied most of the code from CMultiDocTemplate::OpenDocumentfile().
// Dont open a new frame window upon opening a new document.

CDocument *
FDocTemplate::OpenDocumentFile(LPCTSTR lpszPathName, BOOL bMakeVisible) {
	CDocument* pDocument = CreateNewDocument();
	if (pDocument == NULL) {
		TRACE0("CDocTemplate::CreateNewDocument returned NULL.\n");
		AfxMessageBox(AFX_IDP_FAILED_TO_CREATE_DOC);
		return NULL;
	}
	ASSERT_VALID(pDocument);

	// Snipped out MFC default, which opens one new frame window.
	// ......

	if (lpszPathName == NULL)  {
		// create a new document - with default document name
		SetDefaultTitle(pDocument);

		// avoid creating temporary compound file when starting up invisible
		if (!bMakeVisible)
			pDocument->m_bEmbedded = TRUE;

		if (!pDocument->OnNewDocument()) {
			// user has be alerted to what failed in OnNewDocument
			TRACE0("CDocument::OnNewDocument returned FALSE.\n");
			// pFrame->DestroyWindow();

			// Need to remove and delete the document.
			RemoveDocument(pDocument);
			delete pDocument;

			return NULL;
		}

		// it worked, now bump untitled count
		m_nUntitledCount++;
	}
    else {
		// open an existing document
		CWaitCursor wait;
		if (!pDocument->OnOpenDocument(lpszPathName)) {
			// user has be alerted to what failed in OnOpenDocument
			TRACE0("CDocument::OnOpenDocument returned FALSE.\n");
			// pFrame->DestroyWindow();

			// Need to remove and delete the document.
			RemoveDocument(pDocument);
			delete pDocument;

			return NULL;
		}	

		// Following operation was moved into FEditorDoc::OnOpenDocument()
		// pDocument->SetPathName(lpszPathName);
	}

	return pDocument;
}

FEditorDoc *FDocTemplate::GetObjsDocPtr(Object *pObj) {
	FEditorDoc *pDoc = NULL;
	POSITION rPos = GetFirstDocPosition();
	while (rPos != NULL) {
		pDoc = (FEditorDoc *) GetNextDoc(rPos);
		if (pDoc->GetRootObjectPtr() == pObj)
			return pDoc;
	}
	return NULL;
}

CDocument* FDocTemplate::GetNextDocPtr(CDocument *pDoc) {
	POSITION rPos = GetFirstDocPosition();
	CDocument *nDoc;
	while (rPos != NULL && (nDoc = GetNextDoc(rPos)) == pDoc);
	return (nDoc != pDoc ? nDoc : NULL);
}

void FDocTemplate::SetDefaultTitle(CDocument* pDocument) {
    CString strDocName = "Untitled";
	TCHAR szNum[8];
	wsprintf(szNum, _T("%d"), m_nUntitledCount+1);
	strDocName += szNum;
    pDocument->SetTitle(strDocName);
}

//======================================================================
// FEditorDoc
//======================================================================

IMPLEMENT_DYNCREATE(FEditorDoc, CDocument)

BEGIN_MESSAGE_MAP(FEditorDoc, CDocument)
	//{{AFX_MSG_MAP(FEditorDoc)
		// NOTE - the ClassWizard will add and remove mapping macros here.
		//    DO NOT EDIT what you see in these blocks of generated code!
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

//==================================================
// FEditorDoc construction/destruction
//==================================================

FEditorDoc::FEditorDoc() {
	// Init Document.
	rootObject = NULL;
	holdBrush = NULL;
	holdManipObj = NULL;

	for(int i = 0; i < 16; i++)
		pNonStdViews[i] = NULL;

	game = NULL;
	entList = NULL;
}

FEditorDoc::~FEditorDoc() {
	// Sanity Check.
	ASSERT(rootObject == NULL);
	ASSERT(holdBrush == NULL);
}

void FEditorDoc::DeleteContents() {
	// Sanity.
	ASSERT(holdBrush == NULL);

	if (rootObject != NULL) {
		delete rootObject;
		rootObject = NULL;
	}

	CDocument::DeleteContents();
}

#include "EntSel.h"

BOOL FEditorDoc::OnNewDocument() {
	if (!CDocument::OnNewDocument())
		return FALSE;

	FEntSel entSel;
	entSel.DoModal();

	if(!entSel.m_gameName) 
		return FALSE;

	game = Game::Find(entSel.m_gameName);
	strcpy(palName, entSel.m_palName);
	if (!Game::Set(game, palName, FDraw::textureGamma))
		return FALSE;

	entList = EntList::Find(entSel.m_entName);
	EntList::Set(entList);

	ASSERT(rootObject == NULL);
	rootObject = new Object;

#ifdef _SHAREWARE_DEMO_
	demoBrushCount = 0;
#endif

	// Add world spawn entity.
	Entity *pWorldSpawn = new Entity("worldspawn");
	rootObject->SetEntity(pWorldSpawn);

	GMainFrame->GetTreeView()->AddDocument(this);
	GMainFrame->SetDeskTopDocument(this);

	return TRUE;
}

CProgressWnd *pProgressWnd;
bool ProgressSetPos(int percent) {
	if(pProgressWnd->Cancelled())
		return false;
	pProgressWnd->SetPos(percent);
	pProgressWnd->PeekAndPump();
	return true;
}

BOOL FEditorDoc::OnOpenDocument(LPCTSTR lpszPathName) {
	// Hack.
	// MFC's recent file list doesn't remember the .map ext.
	// Check if the .map file exists in the same dir.
	const char *c = strrchr(lpszPathName, '.');
	CString mapPathName;
	if (lpszPathName != NULL &&
		!LFile::Exist(lpszPathName) && stricmp(c, ".qle") == 0) {
		mapPathName = lpszPathName;
		mapPathName = mapPathName.Left(mapPathName.ReverseFind('.')) + ".map";
		if (LFile::Exist(mapPathName))
			lpszPathName = (LPCTSTR) mapPathName;
	}

	// Default implementation.
	if (!CDocument::OnOpenDocument(lpszPathName))
		return FALSE;

	ASSERT(rootObject == NULL);
	c = strrchr(lpszPathName, '.');
	if (c == NULL) // Sanity check.
		return FALSE;

	BOOL rtnVal;

	// Get the game type.
	char gameName[80] = "", entName[80] = "";
	strcpy(palName, "");
	game = NULL;
	entList = NULL;

	if (LoadDocHeader(lpszPathName, gameName, entName, palName)) {
		game = Game::Find(gameName);
	}
	else {
		// Prompt user for Game/Entity selection and setting.
		FEntSel entSel;
		entSel.DoModal();

		if (!entSel.m_gameName)
			return FALSE;

		strcpy(gameName, entSel.m_gameName);
		strcpy(entName, entSel.m_entName);

		game = Game::Find(gameName);
	}

	if (!Game::Set(game, palName, FDraw::textureGamma))
		return FALSE;

	entList = EntList::Find(entName);
	EntList::Set(entList);

	char loading[256];
	const char *c1 = strrchr(lpszPathName, '\\');
	FDraw::OutputText("Loading map: %s...\n", (c1 != NULL ? c1 + 1 : lpszPathName));
	sprintf(loading, "Loading %s", (c1 != NULL ? c1 + 1 : lpszPathName));

	pProgressWnd = new CProgressWnd(AfxGetMainWnd());
	pProgressWnd->SetText(loading);

	CString pathName = lpszPathName;

	if (stricmp(c, ".map") == 0) {
		rootObject = MapIO::ReadMap(lpszPathName, ProgressSetPos);
		rtnVal = rootObject != NULL;
		pathName = pathName.Left(pathName.GetLength() - 4) + ".qle";
	}
	else if (stricmp(c, ".qle") == 0) {
		rootObject = new Object;
		rtnVal = rootObject->LoadObjFile(lpszPathName, NULL, ProgressSetPos);
	}
	else {
		ASSERT(0);
		rtnVal = FALSE;
	}

	if(pProgressWnd->Cancelled())
		rtnVal = FALSE;

	delete pProgressWnd;

	if (rtnVal) {
		SetPathName(pathName);
		GMainFrame->GetTreeView()->AddDocument(this);
		GMainFrame->SetDeskTopDocument(this);

#ifdef _SHAREWARE_DEMO_
		demoBrushCount = rootObject->CountBrushes();
		if (!FReg::Check() && demoBrushCount > DEMO_BRUSHCOUNT) {
			char textBuf[256];
			sprintf(textBuf, 
					"This map contains %d brushes.  You\n"
					"will not be able to save the map\n"
					"later if it exceeeds %d brushes in\n"
					"this shareware version of Genesis Editor.",
					demoBrushCount, DEMO_BRUSHCOUNT);
			MessageBox(NULL, textBuf, "Genesis Editor Notice", MB_OK);
		}
#endif

	}
	else {
		delete rootObject;
		rootObject = NULL;
	}	

	return rtnVal;
}

BOOL FEditorDoc::OnSaveDocument(LPCTSTR lpszPathName) {
	if (!CDocument::OnSaveDocument(lpszPathName))
		return FALSE;

	ASSERT(rootObject != NULL);
	const char *c = strrchr(lpszPathName, '.');
	if (c == NULL || stricmp(c, ".qle") != 0)
		return FALSE;

#ifdef _SHAREWARE_DEMO_
	// Do a brush count.
	int brushCount = rootObject->CountBrushes();
	if (!FReg::Check() && brushCount > DEMO_BRUSHCOUNT) {
		MessageBox(NULL, "Brush count exceeds shareware limit.", "Genesis Editor Notice", MB_OK);
		return FALSE;
	}
#endif

	if (!SaveDocHeader(lpszPathName, game->GetName(), entList->GetName(), palName))
		return FALSE;

	char saving[256];
	c = strrchr(lpszPathName, '\\');
	FDraw::OutputText("Saving map: %s... ", (c != NULL ? c + 1 : lpszPathName));
	sprintf(saving, "Saving %s", (c != NULL ? c + 1 : lpszPathName));

	pProgressWnd = new CProgressWnd(AfxGetMainWnd());
	pProgressWnd->SetText(saving);
	pProgressWnd->NoCancelButton();
	
	BOOL rtnVal = rootObject->SaveObjFile(lpszPathName, true, ProgressSetPos);

	delete pProgressWnd;

	// Change the mainframe's titlebar.
	CString docName = lpszPathName;
	int index = docName.ReverseFind('\\');
	docName = docName.Mid(index + 1);
	docName = docName.Left(docName.GetLength() - 4);
	SetTitle(docName);

	GMainFrame->UpdateFrameTitle();

	FDraw::OutputText("OK.\n");
	return rtnVal;
}

BOOL FEditorDoc::OnExportMap(LPCTSTR lpszPathName) {
	ASSERT(rootObject != NULL);

	const char *c = strrchr(lpszPathName, '.');
	if (c == NULL || stricmp(c, ".map") != 0)
		return FALSE;

#ifdef _SHAREWARE_DEMO_
	// Do a brush count.
	int brushCount = rootObject->CountBrushes();
	if (!FReg::Check() && brushCount > DEMO_BRUSHCOUNT) {
		MessageBox(NULL, "Brush count exceeds shareware limit.", "Genesis Editor Notice", MB_OK);
		return FALSE;
	}
#endif

	char exporting[256];
	c = strrchr(lpszPathName, '\\');
	sprintf(exporting, "Exporting %s", (c != NULL ? c + 1 : lpszPathName));

	if(game->IsExportWadSet())
		ExportWad((char *)lpszPathName);

	if(game->IsSpecifyWadsSet())
		SpecifyWads();

	pProgressWnd = new CProgressWnd(AfxGetMainWnd());
	pProgressWnd->SetText(exporting);
	pProgressWnd->NoCancelButton();

	BOOL rtnVal = MapIO::WriteMap(lpszPathName, *rootObject, ProgressSetPos);
	delete pProgressWnd;

	FDraw::OutputText("OK.\n");
	return rtnVal;
}

void FEditorDoc::OnCloseDocument() {
	FEditorDoc *pNextDoc = (FEditorDoc *)
		((FDocTemplate *) GetDocTemplate())->GetNextDocPtr(this);

	GMainFrame->GetTreeView()->RemoveDocument(this);
	GMainFrame->SetDeskTopDocument(pNextDoc);

	CDocument::OnCloseDocument();
}

void FEditorDoc::SetModifiedFlag(BOOL bModified) {
	if (bModified == IsModified())
		return;

	if (GMainFrame->GetDeskTopDocument() != this)
		return;

	CDocument::SetModifiedFlag(bModified);

	char winTitle[256];
	int len = GMainFrame->GetWindowText(winTitle, 256);
	ASSERT(len > 0);

	/*
	if (winTitle[len - 1] == '*')
		winTitle[len - 2] = '\0';
	*/
	char *c = strstr(winTitle, " *");
	if(c)
		*c = '\0';
	c = strstr(winTitle, " - [");
	if(c)
		*c = '\0';

	if (bModified)
		strcat(winTitle, " *");
	GMainFrame->SetWindowText(winTitle);
}

void FEditorDoc::SetTitle(LPCTSTR lpszTitle) {
	CDocument::SetTitle(lpszTitle);
}

const char *FEditorDoc::GetDocName(void) const {
	static CString docName;

	docName = GetPathName();

	if (docName.IsEmpty())
		return "";

	int i;
	if ((i = docName.ReverseFind('\\')) != -1)
		docName = docName.Mid(i + 1);
	if ((i = docName.ReverseFind('.')) != -1)
		docName = docName.Left(i);

	return (LPCTSTR) docName;
}

bool FEditorDoc::LoadDocHeader(LPCTSTR pathName, char *gameName, char *entName, char *palName) {
	CString name;
	LFile inFile;

	if (!inFile.Open(pathName))
		return false;

	inFile.ResetLineNumber();

	// Maps saved by Qoole 99 start with "// QOOLE"; accept them too.
	const char *header = inFile.GetNextLine();
	if (strnicmp(header, "// GENESIS", 10) != 0 && strnicmp(header, "// QOOLE", 8) != 0)
		return false;

	while (strnicmp(inFile.GetNextLine(), "// Game:", 8) != 0) {
		if (inFile.GetLineNumber() > 10)
			return false;
	}
	name = inFile.GetLine() + 8;
	name.TrimLeft();
	name.TrimRight();
	strcpy(gameName, (LPCTSTR) name);

	while (strnicmp(inFile.GetNextLine(), "// EntList:", 11) != 0) {
		if (inFile.GetLineNumber() > 10)
			return false;
	}
	name = inFile.GetLine() + 11;
	name.TrimLeft();
	name.TrimRight();
	strcpy(entName, (LPCTSTR) name);

	while (strnicmp(inFile.GetNextLine(), "// Palette:", 11) != 0) {
		if (inFile.GetLineNumber() > 10) {
			strcpy(palName, "");
			return true;
		}
	}
	name = inFile.GetLine() + 11;
	name.TrimLeft();
	name.TrimRight();
	strcpy(palName, (LPCTSTR) name);

	return true;
}

bool FEditorDoc::SaveDocHeader(LPCTSTR pathName, LPCTSTR gameName, LPCTSTR entName, LPCTSTR palName) {
	ASSERT(gameName != NULL);
	ASSERT(entName != NULL);

	FILE *fOut;
	if ((fOut = fopen(pathName, "wt")) == NULL)
		return false;

	fprintf(fOut, "// GENESIS EDITOR\n");
	fprintf(fOut, "// Game: %s\n", gameName);
	fprintf(fOut, "// EntList: %s\n", entName);
	fprintf(fOut, "// Palette: %s\n", palName);

	fclose(fOut);

	return true;
}

//===== View / Doc =====
int FEditorDoc::RegisterNonStdView(FDocView *pView, bool reg) {
	int i;

	for(i = 0; i < NUM_NONSTD_VIEWS; i++) {
		if (reg && pNonStdViews[i] == NULL)
			break;

		if (!reg && pNonStdViews[i] == pView) {
			pNonStdViews[i] = NULL;
			return i;
		}
	}

	if (!reg && i == NUM_NONSTD_VIEWS)  // Not found.
		return -1;

	ASSERT(reg);

	if (i == NUM_NONSTD_VIEWS)  // No more room.
		return -1;

	pNonStdViews[i] = pView;
	return i;
}

void FEditorDoc::UpdateAllViews(CView* pSender, LPARAM lHint, CObject* pHint) {
	// Update the non standard views first.
	FDocView *pView;
	for(int i = 0; i < NUM_NONSTD_VIEWS; i++) {
		pView = pNonStdViews[i];
		if (pView != NULL)
			pView->OnUpdate(lHint, (Object *) pHint);
	}

	// Update the standard views.
	CDocument::UpdateAllViews(pSender, lHint, pHint);
}

//==================================================
// FEditorDoc diagnostics
//==================================================

#ifdef _DEBUG
void FEditorDoc::AssertValid() const {
	CDocument::AssertValid();
}

void FEditorDoc::Dump(CDumpContext& dc) const {
	CDocument::Dump(dc);
}
#endif //_DEBUG

//==================================================
// FEditorDoc commands
//==================================================

void FEditorDoc::ObjectsAdd(const LinkList<ObjectPtr> &newObjs, Object &parent,
						  bool updateViews, bool resetParent) {

#ifndef NDEBUG 	// Sanity.
	ASSERT(newObjs.NumOfElm() > 0);
	for(int i = 0; i < newObjs.NumOfElm(); i++)
		ASSERT(newObjs[i].GetPtr()->GetParentPtr() == NULL);
	ASSERT(parent.IsMyAncestor(*rootObject) || &parent == rootObject);
#endif
	// Locking check.

	IterLinkList<ObjectPtr> iter(newObjs);
	iter.Reset();
	Object *pObj;
	while (!iter.IsDone()) {
		pObj = iter.GetNext()->GetPtr();
		parent.AddChild(*pObj, false);
	}

	if (resetParent)
		parent.SetBoundRadius();

	SetModifiedFlag();
	if (updateViews)
		UpdateAllViews(NULL, DUAV_OBJSMODSTRUCT, (CObject *) &parent);

#ifdef _SHAREWARE_DEMO_
	int newBrushCount = rootObject->CountBrushes();
	if (!FReg::Check() && demoBrushCount <= DEMO_BRUSHCOUNT &&
		newBrushCount > DEMO_BRUSHCOUNT) {
		char textBuf[256];
		sprintf(textBuf, 
				"This map contains %d brushes.  You\n"
				"will not be able to save the map\n"
				"later if it exceeeds %d brushes in\n"
				"this shareware version of Genesis Editor.",
				demoBrushCount, DEMO_BRUSHCOUNT);
		MessageBox(NULL, textBuf, "Genesis Editor Notice", MB_OK);
	}
	demoBrushCount = newBrushCount;
#endif
}

void FEditorDoc::ObjectsDelete(const LinkList<ObjectPtr> &delObjs,
							 bool destroyObjs,
							 bool updateViews, bool resetParent) {
	ASSERT(delObjs.NumOfElm() > 0);
	Object *pParent = delObjs[0].GetPtr()->GetParentPtr();

#ifndef NDEBUG 	// Sanity.
	ASSERT(pParent != NULL);
	ASSERT(pParent == rootObject|| pParent->IsMyAncestor(*rootObject));
	for(int i = 1; i < delObjs.NumOfElm(); i++)
		ASSERT(delObjs[i].GetPtr()->GetParentPtr() == pParent);
#endif
	// Locking check.

	IterLinkList<ObjectPtr> iter(delObjs);
	iter.Reset();
	Object *pObj;
	while (!iter.IsDone()) {
		pObj = iter.GetNext()->GetPtr();
		pParent->RemoveChild(*pObj, false);
		if (destroyObjs)
			delete pObj;
	}

	if (resetParent)
		pParent->SetBoundRadius();

	SetModifiedFlag();
	if (updateViews)
		UpdateAllViews(NULL, DUAV_OBJSMODSTRUCT, (CObject *) pParent);
}

void FEditorDoc::ObjectReplace(Object &replacee, Object &replacer,
							 bool updateViews, bool resetParent) {
	// Sanity.
	ASSERT(replacer.GetParentPtr() == NULL);

	// Locking Check.

	Object *pParent = replacee.GetParentPtr();
	ASSERT(pParent != NULL);

	int index;

	pParent->RemoveChild(replacee, false, &index);
	pParent->AddChild(replacer, resetParent, index);

	SetModifiedFlag();
	if (updateViews) {
		UpdateAllViews(NULL, DUAV_OBJSMODSTRUCT | DUAV_OBJSSEL,
			(CObject *) pParent);
	}
}

// Moves the object nodes around within the tree.
// Transform the objects to compensate so that
//  they appear to stay the same "shape" and "place"
//  from root level.

void
FEditorDoc::MoveObjNodesInTree(const LinkList<ObjectPtr> &moveObjs,
							 Object *pNewParent, bool updateViews ) {
	// Sanity.
	ASSERT(moveObjs.NumOfElm() > 0);
	Object *pParent = (moveObjs[0].GetPtr())->GetParentPtr();

#ifndef NDEBUG
	// Sanity.
	ASSERT(pParent == rootObject || pParent->IsMyAncestor(*rootObject));
	ASSERT(pNewParent == rootObject || pNewParent->IsMyAncestor(*rootObject));
	ASSERT(pParent != pNewParent);

	// Make sure all objs have the same parent.
	for(int i = 1; i < moveObjs.NumOfElm(); i++)
		ASSERT((moveObjs[i].GetPtr())->GetParentPtr() == pParent);
#endif

	// Calculate relative positioning.
	Matrix44 trans, m;
	Object *pAncestor = pNewParent->FindCommonAncestor(*pParent);
	Object *pObj = pParent;
	ASSERT(pAncestor != NULL);

	trans.SetIdentity();
	while (pObj != pAncestor) {
		pObj->CalTransSpaceMatrix(m.SetIdentity());
		trans = m.Multiply(trans);
		pObj = pObj->GetParentPtr();
	}

	pObj = pNewParent;
	m.SetIdentity();
	while (pObj != pAncestor) {
		pObj->CalInvTransSpaceMatrix(m);
		pObj = pObj->GetParentPtr();
	}
	trans = m.Multiply(trans);

	// Remove the objs from tree, transform them,
	//  and add them back to the new scope.
	IterLinkList<ObjectPtr> iter(moveObjs);
	iter.Reset();
	while (!iter.IsDone()) {
		pObj = (iter.GetNext())->GetPtr();
		pParent->RemoveChild(*pObj, false);
		pObj->Transform(trans, true);
		pNewParent->AddChild(*pObj, false);
	}
	pParent->SetBoundRadius();
	pNewParent->SetBoundRadius();

	SetModifiedFlag();

	if (updateViews) {
		// Update the old scope first.
		UpdateAllViews(NULL, 0, (CObject *) pParent);
		// Update the new scope.
		UpdateAllViews(NULL, 0, (CObject *) pNewParent);
	}
}

#if 0
void
FEditorDoc::TranslateObjectCenter(Object &obj, const Vector3d &DeltaVec) {
  // Sanity.
  ASSERT(obj.IsMyAncestor(*rootObject));


  SetModifiedFlag();
  UpdateAllViews(NULL, 0, (CObject *) &obj);
}
#endif

/*  Do we need these functions???
void
FEditorDoc::ObjectSetPos(Object &obj, const Vector3d &posVec) {
  // Sanity.
  ASSERT(obj.IsMyAncestor(*rootObject));

  obj.SetPosition(posVec);
  SetModifiedFlag();
  UpdateAllViews(NULL, 0, (CObject *) &obj);
}

void
FEditorDoc::ObjectSetOrient(Object &obj, const SphrVector &orientVec) {
  // Sanity
  ASSERT(obj.IsMyAncestor(*rootObject));

  obj.SetOrientation(orientVec);
  SetModifiedFlag();
  UpdateAllViews(NULL, 0, (CObject *) &obj);
}

void
FEditorDoc::ObjectSetScale(Object &obj, const Vector3d &scaleVec) {
  // Sanity.
  ASSERT(obj.IsMyAncestor(*rootObject));

  obj.SetScale(scaleVec);
  SetModifiedFlag();
  UpdateAllViews(NULL, 0, (CObject *) &obj);
}
*/

void FEditorDoc::ObjectsMove(const LinkList<ObjectPtr> &mObjs,
						   const TransSpace &operateSpace,
						   const Vector3d &mVec, bool updateViews) {
#ifndef NDEBUG 	// Sanity.
	ASSERT(mObjs.NumOfElm() > 0);
	for(int i = 0; i < mObjs.NumOfElm(); i++)
		ASSERT(mObjs[i].GetPtr()->IsMyAncestor(*rootObject));
#endif

	Matrix44 m1;
	Vector3d posVec, dVec;

	operateSpace.CalTransSpaceMatrix(m1.SetIdentity());  
	m1.Transform(dVec, mVec);
	dVec.SubVector(operateSpace.GetPosition());

	IterLinkList<ObjectPtr> iter(mObjs);
	iter.Reset();
	Object *pObj;
	while (!iter.IsDone()) {
		pObj = iter.GetNext()->GetPtr();
		(pObj->GetPosition(posVec)).AddVector(dVec);
		pObj->TransSpace::SetPosition(posVec);
	}

	Object *pParent = mObjs[0].GetPtr()->GetParentPtr();
	if (pParent)
		pParent->SetBoundRadius();

	SetModifiedFlag();
	if (updateViews)
		UpdateAllViews(NULL, DUAV_OBJSMODATTRIB, (CObject *) pParent);
}

void FEditorDoc::ObjectsRotate(const LinkList<ObjectPtr> &rObjs,
							 const TransSpace &operateSpace,
							 const Vector3d &rotPt, const float rotAngle,
							 bool updateViews) {
#ifndef NDEBUG 	// Sanity.
	ASSERT(rObjs.NumOfElm() > 0);
	for(int i = 0; i < rObjs.NumOfElm(); i++)
		ASSERT(rObjs[i].GetPtr()->IsMyAncestor(*rootObject));
#endif
	// Locking checks.

	Matrix44 m1, m2, m3;

	// Translate coord into view's system.
	operateSpace.CalInvTransSpaceMatrix(m1.SetIdentity());

	// Translate into rotPt's pos coord system.
	m2.SetInvTranslation(rotPt);
	m3.Multiply(m2, m1);

	// Rotate rotAng.
	m2.SetRotateY(rotAngle);
	m1.Multiply(m2, m3);

	// Translate back into edit scope pos coord system.
	m2.SetTranslation(rotPt);
	m3.Multiply(m2, m1);

	// Translate back into edit scope orientation system.
	operateSpace.CalTransSpaceMatrix(m2.SetIdentity());
	m1.Multiply(m2, m3);

	// Iter through the objects and apply transformation.
	Object *pObj;
	Vector3d fVec, tVec, pVec;
	IterLinkList<ObjectPtr> iter(rObjs);
	iter.Reset();
	while (!iter.IsDone()) {
		pObj = iter.GetNext()->GetPtr();

		pObj->GetPosition(pVec);
		pObj->GetOrientation(fVec, tVec);
		fVec.AddVector(pVec);
		tVec.AddVector(pVec);

		m1.Transform(pVec);
		(m1.Transform(fVec)).SubVector(pVec);
		(m1.Transform(tVec)).SubVector(pVec);

		pObj->TransSpace::SetPosition(pVec);
		pObj->TransSpace::SetOrientation(fVec, tVec);
	}

	// Reset the bounding information for the scope object.
	Object *pParent = rObjs[0].GetPtr()->GetParentPtr();
	if (pParent)
		pParent->SetBoundRadius();

	SetModifiedFlag();
	if (updateViews)
		UpdateAllViews(NULL, DUAV_OBJSMODATTRIB, (CObject *) pParent);
}

//======================================================================
// For scaling operation, create a temporary parent object to store
//  all the selected objects.  Align the temp obj with the view/operate
//  space, so that we only need to change the obj's scale settings to
//  correspond with the scale operation.  Float the selected objects
//  back up and delete the temp obj when operation is finished.
/////======================================================================

void FEditorDoc::GetObjectsScaleBoundBox(const LinkList<ObjectPtr> &sObjs,
									   const TransSpace &operateSpace,
									   Vector3d &minBoundVec,
									   Vector3d &maxBoundVec) const {
#ifndef NDEBUG 	// Sanity.
	ASSERT(sObjs.NumOfElm() > 0);
	for(int i = 0; i < sObjs.NumOfElm(); i++)
		ASSERT(sObjs[i].GetPtr()->IsMyAncestor(*rootObject));
#endif

	Matrix44 m1;
	operateSpace.CalInvTransSpaceMatrix(m1.SetIdentity());

	// Copy and move all selected objects into temp obj.
	Object *pOpObj = new Object;
	Object *pSelObj;
	IterLinkList<ObjectPtr> iter(sObjs);
	iter.Reset();
	while (!(iter.IsDone())) {
		pSelObj = (iter.GetNext())->GetPtr();
		pSelObj = new Object(*pSelObj);
		pSelObj->Transform(m1);
		pOpObj->AddChild(*pSelObj, false);
	}
	pOpObj->SetBoundRadius();
	pOpObj->GetBoundingVectors(minBoundVec, maxBoundVec);
	delete pOpObj;
}

Object &FEditorDoc::ObjectsScaleBegin(const LinkList<ObjectPtr> &sObjs,
									const TransSpace &operateSpace,
									const Vector3d &basisVec) {
#ifndef NDEBUG 	// Sanity.
	ASSERT(sObjs.NumOfElm() > 0);
	for(int i = 0; i < sObjs.NumOfElm(); i++)
		ASSERT(sObjs[i].GetPtr()->IsMyAncestor(*rootObject));
#endif

	// Remember scope.
	Object *pScope = sObjs[0].GetPtr()->GetParentPtr();

	// Create a temp holder obj and set it's pos and orientation.
	Object *pOpObj = new Object;

	Matrix44 m1;
	operateSpace.CalTransSpaceMatrix(m1.SetIdentity());

	Vector3d pVec;
	m1.Transform(pVec, basisVec);

	pOpObj->SetPosition(pVec);
	pOpObj->SetOrientation(operateSpace.GetOrientation());

	// Find the transformation matrix for selected objs.
	Matrix44 m2;

	// Transform into view space's system.
	operateSpace.CalInvTransSpaceMatrix(m1.SetIdentity());

	// Translate into basisVec's system.
	m2.SetInvTranslation(basisVec);
	m2.Multiply(m1);

	// Move all the selected objects into the temp obj.
	Object *pSelObj;
	IterLinkList<ObjectPtr> iter(sObjs);
	iter.Reset();
	while (!(iter.IsDone())) {
		pSelObj = (iter.GetNext())->GetPtr();
		pSelObj->Transform(m2);
		pSelObj->GetParent().RemoveChild(*pSelObj, false);
		pOpObj->AddChild(*pSelObj, false);
	}
	pOpObj->SetBoundRadius();
	pScope->AddChild(*pOpObj);

	return *pOpObj;
}

void FEditorDoc::ObjectsScaleChange(const LinkList<ObjectPtr> &sObjs,
								  const Vector3d &newScaleVec,
								  bool updateViews) {
#ifndef NDEBUG 	// Sanity.
	ASSERT(sObjs.NumOfElm() > 0);
	for(int i = 0; i < sObjs.NumOfElm(); i++)
		ASSERT(sObjs[i].GetPtr()->IsMyAncestor(*rootObject));
#endif

	Object *pOpObj = (sObjs[0].GetPtr())->GetParentPtr();
	ASSERT(pOpObj != NULL);

	pOpObj->SetScale(newScaleVec);

	Object *pScope = pOpObj->GetParentPtr();
	ASSERT(pScope != NULL);

	SetModifiedFlag();
	if (updateViews) {
		UpdateAllViews(NULL, DUAV_OBJSMODATTRIB | DUAV_NOTREEVIEW,
					   (CObject *) pScope);
	}
}

void FEditorDoc::ObjectsScaleEnd(const LinkList<ObjectPtr> &sObjs,
							   bool updateViews) {
#ifndef NDEBUG 	// Sanity.
	ASSERT(sObjs.NumOfElm() > 0);
	for(int i = 0; i < sObjs.NumOfElm(); i++)
		ASSERT(sObjs[i].GetPtr()->IsMyAncestor(*rootObject));
#endif

	Object *pOpObj = (sObjs[0].GetPtr())->GetParentPtr();
	ASSERT(pOpObj != NULL);
	Object *pScope = pOpObj->GetParentPtr();
	ASSERT(pScope != NULL);

	// Float the selected objs back to their original scope.
	pOpObj->FloatChildren();

	// Delete the temp obj;
	delete &(pScope->RemoveChild(*pOpObj));

	SetModifiedFlag();
	if (updateViews)
		UpdateAllViews(NULL, DUAV_OBJSMODATTRIB, (CObject *) pScope);
}

//===== Vertex/Edge/Face Move =====

void
FEditorDoc::BrushManipulateBegin(Object &obj) {
	// Sanity.
	ASSERT(obj.IsMyAncestor(*rootObject));
	ASSERT(obj.HasBrush());
	// Can't do integrity check, because it rebuilds plane & edge info.
	// ASSERT((obj.GetBrush()).IntegrityCheck());
	ASSERT(holdBrush == NULL);
	ASSERT(holdManipObj == NULL);

	holdBrush = new Geometry(obj.GetBrush());
	holdManipObj = &obj;
	brushManipInit = true;
}

Geometry *
FEditorDoc::BrushManipulateEnd(bool updateViews) {
	// Sanity.
	ASSERT(holdBrush != NULL);
	ASSERT(holdManipObj != NULL);
	ASSERT(holdManipObj->HasBrush());

	bool ok;
	Geometry *rtnVal;
	Geometry *brush = &(holdManipObj->GetBrush());

	ok = brush->IntegrityCheck();
	if (ok) {
		rtnVal = holdBrush;
	}
	else {
		rtnVal = holdManipObj->SetBrush(holdBrush);
		delete rtnVal;
		rtnVal = NULL;
	}

	holdManipObj->SetBoundRadius(true);

	holdManipObj = NULL;
	holdBrush = NULL;
	brushManipInit = false;

	SetModifiedFlag();
	if (updateViews) {
		UpdateAllViews(NULL, DUAV_OBJSMODATTRIB,
					   (CObject *) holdManipObj);
	}

	return rtnVal;
}

void
FEditorDoc::FaceMove(const GPolygon *facePtr, const Vector3d &deltaVec,
				   const TransSpace &operateSpace, bool updateViews) {
	// Sanity.
	ASSERT(holdManipObj != NULL);
	ASSERT(facePtr != NULL);

	Geometry *brush = &(holdManipObj->GetBrush());
	Vector3d dVec(deltaVec), oVec;
	Matrix44 m1;

	if (brushManipInit) {
		brush->BreakPlanes(*facePtr);
		brushManipInit = false;
	}

	// Translate deltaVec into object's space.
	holdManipObj->CalInvTransSpaceMatrix(m1.SetIdentity());
	operateSpace.CalTransSpaceMatrix(m1);
	m1.Transform(dVec);
	m1.Transform(oVec, Vector3d::origVec);
	dVec.SubVector(oVec);

	// Then move the face.
	brush->MoveFace(*facePtr, dVec);
	holdManipObj->SetBoundRadius(true);

	SetModifiedFlag();
	if (updateViews) {
		UpdateAllViews(NULL, DUAV_OBJSMODATTRIB | DUAV_NOTREEVIEW,
					   (CObject *) holdManipObj->GetParentPtr());
	}
}

void
FEditorDoc::EdgeMove(const Edge3d *edgePtr, const Vector3d &deltaVec,
				   const TransSpace &operateSpace, bool updateViews) {
	// Sanity.
	ASSERT(holdManipObj != NULL);
	ASSERT(edgePtr != NULL);

	Geometry *brush = &(holdManipObj->GetBrush());
	Vector3d dVec(deltaVec), oVec;
	Matrix44 m1;

	if (brushManipInit) {
		brush->BreakPlanes(*edgePtr);
		brushManipInit = false;
	}

	// Translate deltaVec into object's space.
	holdManipObj->CalInvTransSpaceMatrix(m1.SetIdentity());
	operateSpace.CalTransSpaceMatrix(m1);
	m1.Transform(dVec);
	m1.Transform(oVec, Vector3d::origVec);
	dVec.SubVector(oVec);

	// Then move the edge.
	brush->MoveEdge(*edgePtr, dVec);
	holdManipObj->SetBoundRadius(true);

	SetModifiedFlag();
	if (updateViews)
		UpdateAllViews(NULL, DUAV_OBJSMODATTRIB | DUAV_NOTREEVIEW,
					   (CObject *) holdManipObj->GetParentPtr());
}

void
FEditorDoc::VertexMove(int vIndex, const Vector3d &deltaVec,
					 const TransSpace &operateSpace, bool updateViews) {
	// Sanity.
	ASSERT(holdManipObj != NULL);
	ASSERT(vIndex >= 0 && vIndex < holdManipObj->GetBrush().GetNumVertices());

	Geometry *brush = &(holdManipObj->GetBrush());
	Vector3d dVec(deltaVec), oVec;
	Matrix44 m1;

	if (brushManipInit) {
		brush->BreakPlanes(vIndex);
		brushManipInit = false;
	}

	// Translate deltaVec into object's space.
	holdManipObj->CalInvTransSpaceMatrix(m1.SetIdentity());
	operateSpace.CalTransSpaceMatrix(m1);
	m1.Transform(dVec);
	m1.Transform(oVec, Vector3d::origVec);
	dVec.SubVector(oVec);

	// Then move the vertex.
	brush->MoveVertex(vIndex, dVec);
	holdManipObj->SetBoundRadius(true);

	SetModifiedFlag();
	if (updateViews) {
		UpdateAllViews(NULL, DUAV_OBJSMODATTRIB | DUAV_NOTREEVIEW,
					   (CObject *) holdManipObj->GetParentPtr());
	}
}

//===== Replacing brush in obj =====
Geometry *FEditorDoc::BrushReplace(Object &brushObj, Geometry &newBrush,
								 bool updateViews) {
	Geometry *rtnVal;
	rtnVal = brushObj.SetBrush(&newBrush);
	brushObj.SetBoundRadius(true);

	SetModifiedFlag();
	if (updateViews) {
		UpdateAllViews(NULL, DUAV_OBJSMODATTRIB,
					   (CObject *) holdManipObj->GetParentPtr());
	}

	return rtnVal;
}

//===== Hollowing brush & CSG operations =====
bool FEditorDoc::HollowBrush(Object &brushObj, bool inward, float thickness,
						   bool updateViews) {
	// Sanity.
	ASSERT(brushObj.IsMyAncestor(*rootObject));
	ASSERT(brushObj.HasBrush());

	bool modified;
	if (inward)
		modified = brushObj.HollowInBrush(thickness);
	else
		modified = brushObj.HollowOutBrush(thickness);

	if (modified) {
		SetModifiedFlag();
		if (updateViews) {
			UpdateAllViews(NULL, DUAV_OBJSSEL | DUAV_OBJSMODSTRUCT,
						   (CObject *) brushObj.GetParentPtr());
		}
	}

	return modified;
}

bool FEditorDoc::CSGSubtract(Object &cuttee,
						   const LinkList<ObjectPtr> &cutters,
						   bool updateViews) {
	// Sanity.
	ASSERT(cuttee.IsMyAncestor(*rootObject));
	ASSERT(cutters.NumOfElm() > 0);

	bool modified;
	modified = cuttee.CSGSubtract(cutters);

	SetModifiedFlag();
	if (modified && updateViews) {
		UpdateAllViews(NULL, DUAV_OBJSSEL | DUAV_OBJSMODSTRUCT,
					   (CObject *) &cuttee);
	}

	return modified;
}

Object *FEditorDoc::CSGIntersect(Object &scopeObj,
							   const LinkList<ObjectPtr> &nodes,
							   bool updateViews) {
	// Sanity.
	ASSERT(nodes.NumOfElm() > 0);

	Object *newObj = scopeObj.CSGIntersect(nodes);

	SetModifiedFlag();
	if (newObj && updateViews) {
		UpdateAllViews(NULL, DUAV_OBJSSEL | DUAV_OBJSMODSTRUCT,
					   (CObject *) &scopeObj);
	}

	return newObj;
}

void
FEditorDoc::FlipObjects(const LinkList<ObjectPtr> &sObjs, const Plane &flipPlane,
					  bool updateViews) {
	ASSERT(sObjs.NumOfElm() > 0);
	Object *pParent = sObjs[0].GetPtr()->GetParentPtr();
	
#ifndef NDEBUG 	// Sanity.
	ASSERT(pParent != NULL);
	ASSERT(pParent == rootObject || pParent->IsMyAncestor(*rootObject));
	for(int i = 0; i < sObjs.NumOfElm(); i++)
		ASSERT(sObjs[i].GetPtr()->GetParentPtr() == pParent);
#endif

	// Setup plane's coord system.
	TransSpace planeCoord;
	planeCoord.SetPosition(flipPlane.GetPoint());

	float xAng = (flipPlane.GetNorm()).CosTheta(Vector3d::xAxisVec);
	float yAng = (flipPlane.GetNorm()).CosTheta(Vector3d::yAxisVec);
	float zAng = (flipPlane.GetNorm()).CosTheta(Vector3d::zAxisVec);

	Vector3d topVec;
	if (ABS(xAng) < ABS(yAng) && ABS(xAng) < ABS(zAng))
		topVec = Vector3d::xAxisVec;
	else if (ABS(yAng) < ABS(xAng) && ABS(yAng) < ABS(zAng))
		topVec = Vector3d::yAxisVec;
	else
		topVec = Vector3d::zAxisVec;

	planeCoord.SetOrientation(flipPlane.GetNorm(), topVec);

	// Transform to plane's coord.
	Matrix44 m1;
	planeCoord.CalInvTransSpaceMatrix(m1.SetIdentity());

	// Setup flip matrix.
	Matrix44 m2;
	m2.SetIdentity();
	m2.SetMatrix(1, 1, -1.0f);
	m2.Multiply(m1);

	// Transform back to world's coord.
	planeCoord.CalTransSpaceMatrix(m1.SetIdentity());
	m1.Multiply(m2);

	// Transform each of the selected objects.
	IterLinkList<ObjectPtr> iter(sObjs);
	Object *pObj;
	iter.Reset();
	while (!iter.IsDone()) {
		pObj = iter.GetNext()->GetPtr();
		pObj->Transform(m1, false, true);
	}
	pParent->SetBoundRadius();

	// Update views.
	SetModifiedFlag();
	if (updateViews) 
		UpdateAllViews(NULL, DUAV_OBJSMODATTRIB, (CObject *) pParent);
}

// Grouping.
void FEditorDoc::GroupObjects(Object &newGroup,
							const LinkList<ObjectPtr> &objPtrs,
							bool updateViews) {
	// PreCondition.
	ASSERT(newGroup.GetParentPtr() == NULL);
	ASSERT(newGroup.GetNumChildren() == 0);
	ASSERT(!newGroup.HasBrush() && !newGroup.IsItemNode());

	// Find GrandParent.
	Object *pGrandParent = (objPtrs[0].GetPtr())->GetParentPtr();
	ASSERT(pGrandParent != NULL);

	// Move the children nodes into new group (parent) node.
	Vector3d avePosVec(0.0f, 0.0f, 0.0f);
	Object *pObj;
	IterLinkList<ObjectPtr> iter(objPtrs);
	iter.Reset();
	while (!iter.IsDone()) {
		pObj = (iter.GetNext())->GetPtr();
		ASSERT(pObj->GetParentPtr() == pGrandParent);
		pGrandParent->RemoveChild(*pObj, false);
		newGroup.AddChild(*pObj, false);
		avePosVec.AddVector(pObj->GetPosition());
	}
	avePosVec.MultVector(1.0f / objPtrs.NumOfElm());

	// Adjust children's positions.
	Vector3d pVec;
	iter.Reset();
	while (!iter.IsDone()) {
		pObj = (iter.GetNext())->GetPtr();
		(pObj->GetPosition(pVec)).SubVector(avePosVec);
		pObj->TransSpace::SetPosition(pVec);
	}

	// Reset the new group's info.
	Vector3d sVec(1.0f, 1.0f, 1.0f);
	SphrVector oriVec(0.0f, 0.0f, 0.0f);
	newGroup.TransSpace::SetPosition(avePosVec);
	newGroup.TransSpace::SetOrientation(oriVec);
	newGroup.TransSpace::SetScale(sVec);
	newGroup.SetBoundRadius();

	// Add the new group back in.
	pGrandParent->AddChild(newGroup);

	SetModifiedFlag();
	if (updateViews) {
		UpdateAllViews(NULL, DUAV_OBJSMODSTRUCT | DUAV_OBJSMODATTRIB,
					  (CObject *) pGrandParent);
	}
}

void FEditorDoc::UngroupObjects(Object &disbandGroup, bool updateViews) {
	// PreCondition.
	ASSERT(disbandGroup.GetNumChildren() > 0);
	ASSERT(!disbandGroup.IsRoot());

	// Get grand parent.
	Object *pGrandParent = disbandGroup.GetParentPtr();

	// Just float the children up.
	// Already implemented in Object.cpp.
	disbandGroup.FloatChildren(false);

	// Don't delete the DisbandGruop object.
	// Leave it for the callee to deal with.
	pGrandParent->RemoveChild(disbandGroup, true);

	SetModifiedFlag();
	if (updateViews) {
		UpdateAllViews(NULL, DUAV_OBJSMODSTRUCT | DUAV_OBJSMODATTRIB,
					  (CObject *) pGrandParent);
	}
}

Texture *FEditorDoc::applyTexture = NULL;

bool FEditorDoc::TextureApplyObj(Object &obj) {
	if(!obj.HasBrush())
		return true;

	Geometry *pBrush = &(obj.GetBrush());
	for(int i = 0; i < pBrush->GetNumFaces(); i++)
		pBrush->GetFaceTexturePtr(i)->SetTexture(applyTexture);

	return true;
}

void FEditorDoc::TextureApplyObjs(LinkList<ObjectPtr> &objs, char *name,
								bool updateViews) {

	applyTexture = game->GetTexDB()->FindTexture(name);

	IterLinkList<ObjectPtr> iter(objs);
	iter.Reset();
	while(!iter.IsDone()) {
		Object *pObj = iter.GetNext()->GetPtr();
		pObj->PreOrderApply(FEditorDoc::TextureApplyObj);
	}

	SetModifiedFlag();
	if (updateViews)
		UpdateAllViews(NULL, DUAV_NOTREEVIEW | DUAV_OBJTEXMODATTRIB, NULL);
}

void FEditorDoc::TextureApplyFace(Object &texObj, int faceIndex,
								  char *name, bool updateViews) {
	ASSERT(texObj.HasBrush());
	Geometry *pBrush = &(texObj.GetBrush());

	ASSERT(faceIndex >= 0 && faceIndex < pBrush->GetNumFaces());

	FaceTex *pFaceTex = pBrush->GetFaceTexturePtr(faceIndex);
	pFaceTex->SetTName(name);

	SetModifiedFlag();
	if (updateViews) {
		UpdateAllViews(NULL, DUAV_NOTREEVIEW | DUAV_OBJTEXMODATTRIB,
					   (CObject *) &texObj);
	}
}

void FEditorDoc::TextureManipulate(Object &texObj, int faceIndex, int xOff,
								 int yOff, float rotAng, float xScale,
								 float yScale, bool texLock,
								 bool updateViews) {
	ASSERT(texObj.HasBrush());
	Geometry *pBrush = &(texObj.GetBrush());

	ASSERT(faceIndex >= 0 && faceIndex < pBrush->GetNumFaces());

	FaceTex *pFaceTex = pBrush->GetFaceTexturePtr(faceIndex);
	pFaceTex->SetTInfo(xOff, yOff, rotAng, xScale, yScale);
	pFaceTex->SetTexLock(texLock);

	SetModifiedFlag();
	if (updateViews) {
		UpdateAllViews(NULL, DUAV_NOTREEVIEW | DUAV_OBJTEXMODATTRIB,
					   (CObject *) &texObj);
	}
}

void FEditorDoc::ModifyTexSurfAttrib(Object &texObj, int faceIndex,
								   UINT newVal, bool updateViews) {
	ASSERT(texObj.HasBrush());
	Geometry *pBrush = &(texObj.GetBrush());

	ASSERT(faceIndex >= 0 && faceIndex < pBrush->GetNumFaces());

	FaceTex *pFaceTex = pBrush->GetFaceTexturePtr(faceIndex);
	UINT a1, a2, a3;
	pFaceTex->GetTAttribs(a1, a2, a3);
	pFaceTex->SetTAttribs(a1, newVal, a3);

	SetModifiedFlag();
	if (updateViews) {
		UpdateAllViews(NULL, /*DUAV_NOTREEVIEW*/
					   DUAV_PROPPAGES | DUAV_OBJTEXMODATTRIB,
					   (CObject *) &texObj);
	}
}

void FEditorDoc::ModifyTexValAttrib(Object &texObj, int faceIndex,
								  UINT newVal, bool updateViews) {
	ASSERT(texObj.HasBrush());
	Geometry *pBrush = &(texObj.GetBrush());

	ASSERT(faceIndex < pBrush->GetNumFaces());

	FaceTex *pFaceTex = pBrush->GetFaceTexturePtr(faceIndex);
	UINT a1, a2, a3;
	pFaceTex->GetTAttribs(a1, a2, a3);
	pFaceTex->SetTAttribs(a2, a2, newVal);

	SetModifiedFlag();
	if (updateViews) {
		UpdateAllViews(NULL, /*DUAV_NOTREEVIEW*/
					   DUAV_PROPPAGES | DUAV_OBJTEXMODATTRIB,
					   (CObject *) &texObj);
	}
}

void FEditorDoc::ModifyContentAttrib(Object &opObj, UINT newVal,
								   bool updateViews) {
	ASSERT(opObj.HasBrush());
	Geometry *pBrush = &(opObj.GetBrush());

	int i, numFaces = pBrush->GetNumFaces();
	UINT a1, a2, a3;
	FaceTex *pFaceTex;
	for(i = 0; i < numFaces; i++) {
		pFaceTex = pBrush->GetFaceTexturePtr(i);
		pFaceTex->GetTAttribs(a1, a2, a3);
		pFaceTex->SetTAttribs(newVal, a2, a3);
	}

	SetModifiedFlag();
	if (updateViews) {
		UpdateAllViews(NULL, /*DUAV_NOTREEVIEW*/
					   DUAV_PROPPAGES | DUAV_OBJTEXMODATTRIB,
					   (CObject *) &opObj);
	}
}

// Entity handling
void FEditorDoc::EntitySetKey(Entity *ent, const char *key, const char *arg, 
							bool updateViews) {
	ASSERT(ent != NULL);
	ent->SetKey(key, arg);

	SetModifiedFlag();
	if (updateViews)
		 UpdateAllViews(NULL, /*DUAV_NOTREEVIEW*/ 
						DUAV_PROPPAGES | DUAV_OBJENTMODATTRIB, NULL);
}

void FEditorDoc::EntityApply(Object *obj, Entity *ent, bool updateViews) {
	ASSERT(obj != NULL);

	// NOTE: old entity isn't deleted! (OpEntityApply handles this)
	obj->SetEntity(ent);

	SetModifiedFlag();
	if (updateViews)
		UpdateAllViews(NULL, /*DUAV_NOTREEVIEW*/
					   DUAV_NOMAPVIEWS | DUAV_OBJENTMODATTRIB, NULL);
}


//======================================================================
// WAD file creation
//======================================================================

void FEditorDoc::ExportWad(char *filename) {
	char wadName[MAX_PATH];
	strcpy(wadName, filename);
	char *c = strrchr(wadName, '.');
	if(c)
		strcpy(c, ".wad");
	else
		strcat(wadName, ".wad");

	game->GetTexDB()->ResetUsed();
	rootObject->PreOrderApply(FEditorDoc::MarkTexsUsed);
	game->GetTexDB()->BuildWadFromUsed(wadName);

	c = strrchr(wadName, '\\');
	if(c)
		strcpy(wadName, c + 1);
	rootObject->GetEntityPtr()->SetKey("wad", wadName);
}

void FEditorDoc::SpecifyWads(void) {
	game->GetTexDB()->ResetUsed();
	rootObject->PreOrderApply(FEditorDoc::MarkTexsUsed);
	rootObject->GetEntityPtr()->SetKey("wad",
		game->GetTexDB()->BuildWadListFromUsed());
}

bool FEditorDoc::MarkTexsUsed(Object &obj) {
	if(!obj.HasBrush())
		return true;

	Geometry *geo;
	GPolygon *poly;
	Texture *text;
	int i;

	geo = &obj.GetBrush();

	for(i = 0; i < geo->GetNumFaces(); i++) {
		poly = &geo->GetFace(i);
		text = poly->GetTexturePtr()->GetTexture();

		text->texEntry->used = true;
	}
	
	return true;
}


//======================================================================
// FDocView
//======================================================================

IMPLEMENT_DYNAMIC(FDocView, CObject)

FDocView::FDocView() {
	pEditorDoc = NULL;
}

FDocView::~FDocView() {
	if (pEditorDoc != NULL)
		pEditorDoc->RegisterNonStdView(this, false);
}

void FDocView::RegisterDocument(FEditorDoc *pDoc) {

	if (pEditorDoc != NULL)
		pEditorDoc->RegisterNonStdView(this, false);

	pEditorDoc = pDoc;

	if (pEditorDoc != NULL)
		pEditorDoc->RegisterNonStdView(this, true);
}

