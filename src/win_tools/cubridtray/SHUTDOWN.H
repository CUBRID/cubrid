#if !defined(AFX_SHUTDOWN_H__D44E204C_8C63_4D3B_9C88_C324DCD65933__INCLUDED_)
#define AFX_SHUTDOWN_H__D44E204C_8C63_4D3B_9C88_C324DCD65933__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// Shutdown.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CShutdown dialog

class CShutdown : public CDialog
{
// Construction
public:
	CShutdown(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CShutdown)
	enum { IDD = IDD_STOPSERVER };
	CButton	m_ss_cancel;
	CListBox	m_ListDB;
	CButton	m_ss_ok;
	//}}AFX_DATA
	CStringList		m_DBName;
	int			m_Index;

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CShutdown)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CShutdown)
	afx_msg void OnSsOk();
	afx_msg void OnSsCancel();
	virtual BOOL OnInitDialog();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_SHUTDOWN_H__D44E204C_8C63_4D3B_9C88_C324DCD65933__INCLUDED_)
