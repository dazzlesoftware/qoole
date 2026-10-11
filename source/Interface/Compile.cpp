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

// FCompile.cpp : implementation file
//

#include "stdafx.h"
#include "GenesisEditor.h"
#include "Game.h"
#include "MainFrm.h"
#include "Compile.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

//======================================================================
// FCompile dialog
//======================================================================
BOOL	FCompile::m_bUtils[MAXUTILS];
CString	FCompile::m_sParams[MAXUTILS];
int		FCompile::m_iSetParam;
int		FCompile::cmpStage = -1;
bool	FCompile::renderSuspend = false;

FCompile::FCompile(CWnd* pParent /*=NULL*/)
		 : CDialog(FCompile::IDD, pParent) {
	//{{AFX_DATA_INIT(FCompile)
	m_sParam = _T("");
	//}}AFX_DATA_INIT
	for(int i = 0; i < MAXUTILS; i++) {
		m_bUtils[i] = FALSE;
		m_sParams[i] = _T("");
	}
}

void FCompile::DoDataExchange(CDataExchange* pDX) {
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(FCompile)
	DDX_Text(pDX, IDC_CMPL_PARAM, m_sParam);
	DDV_MaxChars(pDX, m_sParam, 128);
	//}}AFX_DATA_MAP
	Game *pGame = GMainFrame->GetDeskTopDocument()->GetGame();
	int numUtils = pGame->GetNumUtils();
	int i;
	for(i = 0; i < numUtils - 1; i++) {
		DDX_Check(pDX, IDC_CMPL_UTIL1 + i, m_bUtils[i]);
	}
	DDX_Check(pDX, IDC_CMPL_GAME, m_bUtils[i]);
}

BEGIN_MESSAGE_MAP(FCompile, CDialog)
	//{{AFX_MSG_MAP(FCompile)
	ON_CBN_SELCHANGE(IDC_CMPL_UTILCOMBO, OnUtilSelChanged)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// FCompile message handlers

BOOL FCompile::OnInitDialog() {
	CDialog::OnInitDialog();

	Game *pGame = GMainFrame->GetDeskTopDocument()->GetGame();
	int i, numUtils = pGame->GetNumUtils();
	ASSERT(numUtils <= MAXUTILS);

	const char *utilName;
	CComboBox *pCombo = (CComboBox *) GetDlgItem(IDC_CMPL_UTILCOMBO);

	// Change the util names.
	for(i = 0; i < numUtils - 1; i++) {
		utilName = pGame->GetUtilName(i);
		pCombo->AddString(utilName);
		GetDlgItem(IDC_CMPL_UTIL1 + i)->SetWindowText(utilName);
		GetDlgItem(IDC_CMPL_UTIL1 + i)->ShowWindow(SW_SHOW);
	}

	// Change the game name.
	utilName = pGame->GetUtilName(i);
	pCombo->AddString(utilName);
	GetDlgItem(IDC_CMPL_GAME)->SetWindowText(utilName);
	GetDlgItem(IDC_CMPL_GAME)->ShowWindow(SW_SHOW);

	// Load the dialog states.
	pGame->InitCompileParams(m_bUtils, m_sParams, m_iSetParam);

	pCombo->SetCurSel(m_iSetParam);
	m_sParam = m_sParams[m_iSetParam];

	UpdateData(FALSE);
	
	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

void FCompile::OnOK() {
	CDialog::OnOK();

	Game *pGame = GMainFrame->GetDeskTopDocument()->GetGame();
	CComboBox *pCombo = (CComboBox *) GetDlgItem(IDC_CMPL_UTILCOMBO);

	m_iSetParam = pCombo->GetCurSel();
	m_sParams[m_iSetParam] = m_sParam;

	// Save the dialog state.
	pGame->SaveCompileParams(m_bUtils, m_sParams, m_iSetParam);

	// Init the compile stage.
	// ASSERT(cmpStage == -1);
	cmpStage = 0;
}

void FCompile::OnUtilSelChanged() {
	UpdateData(TRUE);
	m_sParams[m_iSetParam] = m_sParam;

	CComboBox * pCombo = (CComboBox *) GetDlgItem(IDC_CMPL_UTILCOMBO);
	
	if (m_iSetParam == pCombo->GetCurSel())
		return;

	m_iSetParam = pCombo->GetCurSel();
	m_sParam = m_sParams[m_iSetParam];
	UpdateData(FALSE);
}

// Compile map.
void FCompile::ExecuteCompiles(void) {
	FProcessWnd *pProcWnd = GMainFrame->GetProcessWnd();
	FEditorDoc *pDoc = GMainFrame->GetDeskTopDocument();
	Game *pGame = pDoc->GetGame();
	const char *docName = pDoc->GetDocName();
	int i, numUtils = pGame->GetNumUtils();

	ASSERT(cmpStage >= 0 && cmpStage <= numUtils);

	// Find the next compile stage.
	for(i = 0; i < numUtils; i++) {
		if (cmpStage == i && !m_bUtils[i])
			cmpStage++;
	}

	// Check if need to resume rendering library from game.
	if (renderSuspend) {
		// PAUL: cmpStage was 0 here once?
		ASSERT(cmpStage == numUtils);
		renderSuspend = false;

		GMainFrame->SuspendRenderLib(false);
	}

	// Check if compile's finished.
	if (cmpStage == numUtils)
		goto COMPILE_FINISH;
	
	// Check for the 1st compile stage.
	if (cmpStage == 0) {
		// Clear the window. 
		pProcWnd->ClearWindow();

		// Export the map first.
		const char *mapPath = pGame->GetMapExportPath(docName, PrintText);
		if (mapPath == NULL)
			goto COMPILE_FINISH;

		PrintText("Saving file: ");
		PrintText(mapPath);
		PrintText("\n\n");

		if (!pDoc->OnExportMap(mapPath)) {
			goto COMPILE_FINISH;
			PrintText("Failed to save file\n");
		}
	}

	// Get the util command line.
	const char *cmdLine, *initDir;
	if (!pGame->GetUtilCmdLine(cmpStage, docName, &cmdLine, &initDir, PrintText))
		goto COMPILE_FINISH;

	// Run the util.
	ASSERT(!pProcWnd->IsProcessRunning());

	// Check for the run game stage to suspend 3dfx card.
	ASSERT(renderSuspend == false);
	if (cmpStage == numUtils - 1) {
		renderSuspend = true;
		GMainFrame->SuspendRenderLib(true);
	}

	pProcWnd->ExecProcess((char *) cmdLine, (char *) initDir, ExecuteCompiles);
	cmpStage++;

	return;

COMPILE_FINISH:
	cmpStage = -1;
	return;
}

void FCompile::StopCompiles(void) {
	Game *pGame = GMainFrame->GetDeskTopDocument()->GetGame();
	cmpStage = pGame->GetNumUtils();

	FProcessWnd *pProcWnd = GMainFrame->GetProcessWnd();
	pProcWnd->TerminateProcess();

	return;
}

void FCompile::PrintText(LPCTSTR text) {
	ASSERT(text != NULL);

	FProcessWnd *pProcWnd = GMainFrame->GetProcessWnd();
	ASSERT(pProcWnd != NULL);

	pProcWnd->OutputText((char *) text);
}
