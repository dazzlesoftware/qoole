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

// ProcWnd.cpp : implementation file
//

#include "stdafx.h"
#include "GenesisEditor.h"
#include "ProcWnd.h"
#include "Timer.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// FProcessWnd

// Def's taken from mrc's resource.h
#define ID_MRC_ALLOWDOCKING             2
#define ID_MRC_HIDE                     3
#define ID_MRC_MDIFLOAT                 4

BEGIN_MESSAGE_MAP(FProcessWnd, CRichEditCtrl)
	//{{AFX_MSG_MAP(FProcessWnd)
	ON_WM_CHAR()
	ON_WM_TIMER()
	ON_WM_PAINT()
	ON_WM_SIZE()
	ON_WM_CONTEXTMENU()
	ON_WM_RBUTTONUP()
	ON_WM_CREATE()
	ON_WM_DESTROY()
	//}}AFX_MSG_MAP
	ON_COMMAND(ID_RCMPC_COPY, CRichEditCtrl::Copy)
	ON_COMMAND(ID_RCMPC_CLEAR, ClearWindow)
	ON_COMMAND(ID_MRC_HIDE, OnHandleOutputBarCmds)
	ON_COMMAND(ID_MRC_ALLOWDOCKING, OnHandleOutputBarCmds)
	ON_COMMAND(ID_PROCWND_PROCFINISH, OnExecProcFinished)
END_MESSAGE_MAP()

//////////////////////////////////////////////////
// Constructor and Destructor

FProcessWnd *FProcessWnd::pDelObj = NULL;

FProcessWnd::FProcessWnd() {
	bufSize = 0;
	hChildProcess = NULL;
	terminateFlag = false;
	blockPainting = false;
	pPostExecProc = NULL;
	font.CreatePointFont(75, "MS Sans Serif");
}

FProcessWnd::~FProcessWnd() {
	pDelObj = this;

	if (IsProcessRunning())
		TerminateProcess();
}

/////////////////////////////////////////////////////////////////////////////
// FProcessWnd message handlers

BOOL FProcessWnd::Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID) {
	// TODO: Add your specialized code here and/or call the base class
	dwStyle |= ES_MULTILINE | ES_READONLY  | ES_NOHIDESEL | 
			   ES_AUTOVSCROLL | ES_AUTOHSCROLL |
			   WS_HSCROLL | WS_VSCROLL;

	if (!CRichEditCtrl::Create(dwStyle, rect, pParentWnd, nID))
		return FALSE;
	SetFont(&font);

	return TRUE;
}

void FProcessWnd::OnDestroy() {
	CRichEditCtrl::OnDestroy();
}

//===== Starting & Ending Process =====
void FProcessWnd::ExecProcess(char *pCmdLine, char *pStartDir,
							  PostProc pProc, BOOL bClearWnd) {
	if (IsProcessRunning()) {
		OutputText("===== Can't exec process: Another process is still running =====\n");
		return;
	}

	pPostExecProc = pProc;

	if (bClearWnd)
		ClearWindow();

	char stringBuf[512];
	sprintf(stringBuf, "===== Executing process: =====\n%s\n\n", pCmdLine);
	OutputText(stringBuf);

	STARTUPINFO siStartInfo;
	ZeroMemory(&siStartInfo, sizeof(STARTUPINFO) );
	siStartInfo.cb = sizeof(STARTUPINFO); 

	// Redirect stdin and stdout for child process.
	if (!InitChildStdPipes(siStartInfo)) {
		OutputText("===== Failed to create std pipes for child process =====\n");
		return;
	}


	PROCESS_INFORMATION piProcInfo;

	// Create the process.
	if (!CreateProcess(NULL, pCmdLine, NULL, NULL, TRUE,
					   DETACHED_PROCESS | CREATE_DEFAULT_ERROR_MODE,
					   NULL, pStartDir, &siStartInfo, &piProcInfo)) {
		// Process failed to start.
		OutputText("===== Process failed to start =====\n");
		FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM,
					  NULL, GetLastError(),
					  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
					  stringBuf, 512, NULL);
		OutputText(stringBuf);
		return;
	}

	::CloseHandle(piProcInfo.hThread);
	hChildProcess = piProcInfo.hProcess;

	RestoreStdPipes();

	// Create a thread to read the output of the child process.
	CWinThread *pThread = AfxBeginThread(ThreadFunc, (LPVOID) this);
	SetTimer(TIMER_PROCWND_UPDATE, PROCWND_UPDATETIME, NULL);
}

bool FProcessWnd::InitChildStdPipes(STARTUPINFO &si) {
	// Make child process use the specified std handles.
	si.dwFlags |= STARTF_USESTDHANDLES;
	
	// Set the bInheritHandle flag so pipe handles are inherited.
	SECURITY_ATTRIBUTES saAttr;
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	saAttr.bInheritHandle = TRUE;
	saAttr.lpSecurityDescriptor = NULL;

	// Save the handle to the current STDOUT.
	// hSaveStdout = GetStdHandle(STD_OUTPUT_HANDLE); 
 
	// Create a pipe for the child process's STDOUT.
	HANDLE hChildStdoutRdTmp;
	if (!CreatePipe(&hChildStdoutRdTmp, &hChildStdoutWr, &saAttr, 0))
		return false;

	// Set a write handle to the pipe to be STDOUT.
	// if (!SetStdHandle(STD_OUTPUT_HANDLE, hChildStdoutWr))
	//	return false;
	si.hStdOutput = hChildStdoutWr;
	si.hStdError = hChildStdoutWr;
 
	// Create noninheritable read handle and
	//  close the inheritable read handle.
	BOOL fSuccess;
	fSuccess = DuplicateHandle(GetCurrentProcess(), hChildStdoutRdTmp,
							   GetCurrentProcess(), &hChildStdoutRd,
							   0, FALSE, DUPLICATE_SAME_ACCESS);
    if (!fSuccess)
		return false;
	::CloseHandle(hChildStdoutRdTmp);

	// Save the handle to the current STDIN.
	// hSaveStdin = GetStdHandle(STD_INPUT_HANDLE);
 
	// Create a pipe for the child process's STDIN.
	HANDLE hChildStdinWrTmp;
	if (!CreatePipe(&hChildStdinRd, &hChildStdinWrTmp, &saAttr, 0))
		return false;
 
	// Set a read handle to the pipe to be STDIN. 
	// if (!SetStdHandle(STD_INPUT_HANDLE, hChildStdinRd))
	//	return false;
	si.hStdInput = hChildStdinRd;
 
	// Duplicate the write handle to the pipe so it is not inherited.
	fSuccess = DuplicateHandle(GetCurrentProcess(), hChildStdinWrTmp,
							   GetCurrentProcess(), &hChildStdinWr,
							   0, FALSE, DUPLICATE_SAME_ACCESS);
	if (!fSuccess)
		return false;
	::CloseHandle(hChildStdinWrTmp);

	return true;
}
 
void FProcessWnd::RestoreStdPipes(void) {

	// After process creation, restore the saved STDIN and STDOUT.
	// SetStdHandle(STD_INPUT_HANDLE, hSaveStdin);
	// SetStdHandle(STD_OUTPUT_HANDLE, hSaveStdout);

	// Close the write end of the stdout pipe before reading
	//  from the read end of the pipe. 
	::CloseHandle(hChildStdoutWr);

	// Close the read end of the stdin pipe before writing.
	::CloseHandle(hChildStdinRd);
}

void FProcessWnd::TerminateProcess(void) {
	if (hChildProcess == NULL ||
		WaitForSingleObject(hChildProcess, 0) == WAIT_OBJECT_0) {
		return;
	}

	terminateFlag = true;
	::TerminateProcess(hChildProcess, 0);
	// Wait for the process to terminate.  time out 30 secs.
	::WaitForSingleObject(hChildProcess, 30000);
}

bool FProcessWnd::IsProcessRunning(void) const {
	return (hChildProcess != NULL &&
			WaitForSingleObject(hChildProcess, 0) != WAIT_OBJECT_0);
}

//===== Update output content =====
void FProcessWnd::OnTimer(UINT_PTR nIDEvent) {
	CRichEditCtrl::OnTimer(nIDEvent);

	// static int ucount = 0;
	if (nIDEvent == TIMER_PROCWND_UPDATE) {
		// TRACE1("Update %d\n", ucount++);
		OnIdleUpdateContent();
	}
}

void FProcessWnd::OnIdleUpdateContent(void) {
	lockBuffer.Lock();

	if (bufSize > 0) {
		EDITSTREAM es;
		es.dwCookie = (DWORD_PTR) this;
		es.pfnCallback = GetInputFromBuffer;

		// Disable repaints.
		blockPainting = true;
		GetViewPos();

		SetSel(-1, -1);
		StreamIn(SF_TEXT | SFF_SELECTION, es);

		SetViewPos();
		blockPainting = false;

		// Refresh.
		Invalidate();
	}

	lockBuffer.Unlock();
}

DWORD CALLBACK FProcessWnd::GetInputFromBuffer(DWORD_PTR dwCookie, LPBYTE bpBuff,
											   LONG cb, LONG FAR *pcb) {
	FProcessWnd *pPWnd = (FProcessWnd *) dwCookie;
	int count = ((int)cb < pPWnd->bufSize) ? (int)cb : pPWnd->bufSize;

	memcpy(bpBuff, pPWnd->childStdinBuf, count);

	// Keep whatever didn't fit for the next call.
	pPWnd->bufSize -= count;
	memmove(pPWnd->childStdinBuf, pPWnd->childStdinBuf + count, pPWnd->bufSize);

	*pcb = count;
	return 0;
}

// Worker thread that reads in the output of the child process.
UINT FProcessWnd::ThreadFunc(LPVOID pParam) {
	FProcessWnd *pPWnd = (FProcessWnd *) pParam;

	// Read output from the child process.
	DWORD dwRead;
	CHAR chBuf[1];
	while (1) {
		if (!ReadFile(pPWnd->hChildStdoutRd, chBuf, 1, &dwRead, NULL) ||
		    dwRead == 0) {
			break;
		}

		pPWnd->lockBuffer.Lock();
		while (pPWnd->bufSize >= PIPEBUFSIZE) {
			pPWnd->lockBuffer.Unlock();
			::Sleep(0);
			pPWnd->lockBuffer.Lock();
		}
		pPWnd->childStdinBuf[pPWnd->bufSize++] = *chBuf;
		pPWnd->lockBuffer.Unlock();
	} 

	// Wait for the child process to terminate.
	::WaitForSingleObject(pPWnd->hChildProcess, INFINITE);
	HANDLE hTmp = pPWnd->hChildProcess;
	pPWnd->hChildProcess = NULL;
	::CloseHandle(hTmp);

	::CloseHandle(pPWnd->hChildStdinWr);
	::CloseHandle(pPWnd->hChildStdoutRd);

	// If called from destructor, dont bother updating window.
	if (pDelObj == pPWnd)
		return 0;

	// Kill Timer && refresh.
	pPWnd->KillTimer(TIMER_PROCWND_UPDATE);
	::SendMessage(pPWnd->m_hWnd, WM_TIMER, TIMER_PROCWND_UPDATE, NULL);

	static char *pMesg1 = "\n===== Termination requested by user =====";
	static char *pMesg2 = "\n===== Process Terminated =====\n\n";
	
	pPWnd->lockBuffer.Lock();
	if (pPWnd->terminateFlag) {
		strcpy(pPWnd->childStdinBuf + pPWnd->bufSize, pMesg1);
		pPWnd->bufSize += (int)strlen(pMesg1);
		pPWnd->terminateFlag = false;
	}
	strcpy(pPWnd->childStdinBuf + pPWnd->bufSize, pMesg2);
	pPWnd->bufSize += (int)strlen(pMesg2);
	pPWnd->lockBuffer.Unlock();

	// Refresh once more.
	::SendMessage(pPWnd->m_hWnd, WM_TIMER, TIMER_PROCWND_UPDATE, 0L);

	// Send self a message to inform end of process.
	::PostMessage(pPWnd->m_hWnd, WM_COMMAND, ID_PROCWND_PROCFINISH, 0L);

	return 0;
}

void FProcessWnd::OnExecProcFinished() {
	if (pPostExecProc != NULL) {
		(*pPostExecProc)();
	}
}

void FProcessWnd::OnPaint() {
	// Trap redrawing of the win when we're manually scrolling
	//  the text.  Dont want the screen to flicker.
	if (blockPainting) {
		// Just validate the client area for now.
		ValidateRect(NULL);
	}
	
	// Paint the damn thing.
	CRichEditCtrl::OnPaint();
}

//===== Output Format =====
void FProcessWnd::ClearWindow(void) {
	SetSel(0, -1);
	SetReadOnly(false);
	Clear();
	SetReadOnly(true);
}

void FProcessWnd::OutputText(char *pText) {
	
	lockBuffer.Lock();

	int len = (int)strlen(pText);
	if (len + bufSize >= PIPEBUFSIZE) {  // Clean up the buffer first.
		lockBuffer.Unlock();
		OnIdleUpdateContent();
		lockBuffer.Lock();
	}

	strcpy(childStdinBuf + bufSize, pText);
	bufSize += len;

	lockBuffer.Unlock();

	OnIdleUpdateContent();
}

void FProcessWnd::GetViewPos(void) {
	viewLinePos = GetFirstVisibleLine();
	GetSel(curStart, curEnd);
	int lineFromChar = LineFromChar(-1);
	int numLine = GetLineCount();
	int lineIndex = LineIndex(-1);
	int lineLen = LineLength(-1);
	scrollEnd = (curStart == -1 ||
				 (LineFromChar(-1) == (GetLineCount() - 1) &&
				  curStart >= LineLength(-1) + LineIndex(-1)));
}

void FProcessWnd::SetViewPos(void) {
	if (scrollEnd) {  // Scroll with the outputing text.
		LineScroll(GetLineCount());
		LineScroll(-(winRowHeight - 1));
	}
	else {  // restore old view & selection.
		SetSel(curStart, curEnd);
		LineScroll(-GetLineCount());
		LineScroll(viewLinePos);
	}
}

void FProcessWnd::OnSize(UINT nType, int cx, int cy) {
	CRichEditCtrl::OnSize(nType, cx, cy);

	// Get the # of rows in window.
	CDC *hdc = GetDC();
	TEXTMETRIC tm;
	hdc->GetTextMetrics(&tm);
	ReleaseDC(hdc);
	winRowHeight = cy / (tm.tmHeight + tm.tmExternalLeading);
}

//===== Input to child process =====
void FProcessWnd::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags) {
	CRichEditCtrl::OnChar(nChar, nRepCnt, nFlags);

	DWORD dwWritten;
	char chBuf[1];

	chBuf[0] = nChar;

	// Send the char input into the child process.
	WriteFile(hChildStdinWr, chBuf, 1, &dwWritten, NULL);
}

//===== Context Menu =====
void FProcessWnd::OnRButtonUp(UINT nFlags, CPoint point) {
	CRichEditCtrl::OnRButtonUp(nFlags, point);
	OnContextMenu(this, point);
}

void FProcessWnd::OnContextMenu(CWnd* pWnd, CPoint point) {
	CMenu rMenu;
	rMenu.CreatePopupMenu();

	UINT flag = (IsSelEmpty() ? MF_GRAYED : 0);
	rMenu.AppendMenu(MF_STRING | flag, ID_RCMPC_COPY, "Copy");
	rMenu.AppendMenu(MF_SEPARATOR);
	rMenu.AppendMenu(MF_STRING, ID_RCMPC_CLEAR, "Clear");
	rMenu.AppendMenu(MF_SEPARATOR);

	// Add the menu for the control bar.
	CWnd *pWorkSpaceBar = GetParent();
	pWorkSpaceBar->SendMessage(WM_ADDCONTEXTMENUITEMS, 0, (LPARAM) rMenu.m_hMenu);

	ClientToScreen(&point);

	rMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_RIGHTBUTTON,
		point.x, point.y, this);
}

bool FProcessWnd::IsSelEmpty(void) const {
	long cStart, cEnd;
	GetSel(cStart, cEnd);
	return (cStart >= cEnd && cEnd != -1);
}

void FProcessWnd::OnHandleOutputBarCmds() {
	// Pass the commands into the Output Control Bar.
	const MSG *pMesg = GetCurrentMessage();

	CWnd *pWSBar = GetParent();
	pWSBar->SendMessage(WM_COMMAND, pMesg->wParam, NULL);
}
