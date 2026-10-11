#if !defined(AFX_REG_H__5693DB0B_B0D5_11D2_A0BF_00400543C1CD__INCLUDED_)
#define AFX_REG_H__5693DB0B_B0D5_11D2_A0BF_00400543C1CD__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// FReg.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// FReg dialog

class FReg : public CDialog
{
// Construction
public:
	FReg(CWnd* pParent = NULL);   // standard constructor

	bool Verify(void);
	static bool Check(void);
	static void GetRegName(char *regName);

// Dialog Data
	//{{AFX_DATA(FReg)
	enum { IDD = IDD_REGISTER };
	CEdit	m_regName;
	CEdit	m_regKey;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(FReg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(FReg)
	virtual void OnOK();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

private:
	char reg_name[64];
	char reg_code[64];
	int reg_keynum;
};

#define MAX_KEYS	8192

bool Reg_CodeFromName(char *code, char *name);
void Reg_CodeFromName2(char *code, char *name);
void Reg_CodeFromNum(char *code, int num);

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_REG_H__5693DB0B_B0D5_11D2_A0BF_00400543C1CD__INCLUDED_)
