#if !defined(AFX_OPTIONDIALOG_H__1F779387_71FE_4DE8_999F_C39615D0317F__INCLUDED_)
#define AFX_OPTIONDIALOG_H__1F779387_71FE_4DE8_999F_C39615D0317F__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// OptionDialog.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// COptionDialog dialog

class COptionDialog : public CDialog
{
// Construction
public:
	bool OpChanged;
	COptionDialog(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(COptionDialog)
	enum { IDD = IDD_OPTION_DIALOG };
	CButton	m_cDisplayLog;
	CButton	m_cServerAutoStop;
	CButton	m_cServerAutoStart;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(COptionDialog)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(COptionDialog)
	afx_msg void OnOk();
	virtual BOOL OnInitDialog();
	afx_msg void OnCheck1();
	afx_msg void OnCheck2();
	afx_msg void OnCheck3();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_OPTIONDIALOG_H__1F779387_71FE_4DE8_999F_C39615D0317F__INCLUDED_)
