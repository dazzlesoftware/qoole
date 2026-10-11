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

#if !defined(AFX_TEXMGR_H__D648D0D9_C93D_11D2_A0D9_00400543C1CD__INCLUDED_)
#define AFX_TEXMGR_H__D648D0D9_C93D_11D2_A0D9_00400543C1CD__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// TexMgr.h : header file
//

#include "TexDraw.h"

/////////////////////////////////////////////////////////////////////////////
// FTexMgr dialog

class FTexMgr : public CDialog
{
// Construction
public:
	FTexMgr(CWnd* pParent = NULL);   // standard constructor
	~FTexMgr();

// Dialog Data
	//{{AFX_DATA(FTexMgr)
	enum { IDD = IDD_TEXMGR };
	CButton	m_removeWad;
	CButton	m_removeList;
	CButton	m_remove;
	CButton	m_newList;
	CButton	m_add;
	CEdit	m_texInfo;
	CButton	m_addDir;
	CButton	m_newDir;
	CListBox	m_pickList;
	CListBox	m_texList;
	CListBox	m_listList;
	CComboBox	m_gameCombo;
	CListBox	m_dirList;
	FTexDraw	m_texWnd;
	//}}AFX_DATA

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(FTexMgr)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(FTexMgr)
	virtual BOOL OnInitDialog();
	afx_msg void OnGameSel();
	afx_msg void OnDirSel();
	afx_msg void OnListSel();
	afx_msg void OnPickSel();
	afx_msg void OnTextureRemove();
	virtual void OnOK();
	afx_msg void OnNewList();
	afx_msg void OnTextureAdd();
	afx_msg void OnAddDir();
	afx_msg void OnRemoveList();
	afx_msg void OnTexSel();
	afx_msg void OnRemoveDir();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

private:
	void BuildDirList(void);
	void SetTexInfo(char *texName, int offset);

	Game *pGame;
	char texDirName[MAX_PATH];
	TexList *texList;
};


/////////////////////////////////////////////////////////////////////////////
// FTexMgrNewList dialog

class FTexMgrNewList : public CDialog
{
// Construction
public:
	FTexMgrNewList(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(FTexMgrNewList)
	enum { IDD = IDD_TEXMGR_NEWLIST };
	CString	m_newList;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(FTexMgrNewList)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(FTexMgrNewList)
		// NOTE: the ClassWizard will add member functions here
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_TEXMGR_H__D648D0D9_C93D_11D2_A0D9_00400543C1CD__INCLUDED_)
