#if !defined(AFX_LOGDIALOG_H__F973F6B9_658F_4B9D_8A4F_DBAFAE1D2DF0__INCLUDED_)
#define AFX_LOGDIALOG_H__F973F6B9_658F_4B9D_8A4F_DBAFAE1D2DF0__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// LogDialog.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CLogDialog dialog

class CLogDialog : public CDialog
{
// Construction
public:
	void Refresh();
	void Print_Log(CString Logfile);
	UINT m_Filename;
	CLogDialog(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CLogDialog)
	enum { IDD = IDD_FILEDIALOG };
	CString	m_log;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CLogDialog)
	public:

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	virtual void OnCancel();
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CLogDialog)
	afx_msg void OnRefresh();
	virtual BOOL OnInitDialog();
	afx_msg void OnOk();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_LOGDIALOG_H__F973F6B9_658F_4B9D_8A4F_DBAFAE1D2DF0__INCLUDED_)
