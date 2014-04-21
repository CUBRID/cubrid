#if !defined(AFX_STOPTARGETDB_H__602C9456_E662_40C7_ADCA_56EB498FD2C4__INCLUDED_)
#define AFX_STOPTARGETDB_H__602C9456_E662_40C7_ADCA_56EB498FD2C4__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// StopTargetDB.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CStopTargetDB dialog

#include "CUBRIDManage.h"


class CStopTargetDB : public CDialog
{
private:
	CCUBRIDManage* cCUBRID;

// Construction
public:
	CStopTargetDB( CCUBRIDManage* cCUBRID, CWnd* pParent = NULL);   // standard constructor
	CStopTargetDB( CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CStopTargetDB)
	enum { IDD = IDD_STOP_TARGET_DB };
	CButton	m_ss_cancel;
	CListBox	m_ListDB;
	CButton	m_ss_ok;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CStopTargetDB)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CStopTargetDB)
	afx_msg void OnSsOk();
	afx_msg void OnSsCancel();
	virtual BOOL OnInitDialog();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_STOPTARGETDB_H__602C9456_E662_40C7_ADCA_56EB498FD2C4__INCLUDED_)
