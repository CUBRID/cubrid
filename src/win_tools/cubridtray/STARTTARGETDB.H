#if !defined(AFX_STARTTARGETDB_H__33885BA6_DCBD_4092_B37E_62E5E09FD549__INCLUDED_)
#define AFX_STARTTARGETDB_H__33885BA6_DCBD_4092_B37E_62E5E09FD549__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// StartTargetDB.h : header file
//


#include "CUBRIDManage.h"


/////////////////////////////////////////////////////////////////////////////
// CStartTargetDB dialog

class CStartTargetDB : public CDialog
{
private:
	CCUBRIDManage* cCUBRID;
// Construction
public:
	CStartTargetDB(CWnd* pParent = NULL);   // standard constructor
	CStartTargetDB( CCUBRIDManage* cCUBRID, CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CStartTargetDB)
	enum { IDD = IDD_START_TARGET_DB };
	CButton	m_su_cancel;
	CListBox	m_ListDB;
	CButton	m_su_ok;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CStartTargetDB)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CStartTargetDB)
	virtual BOOL OnInitDialog();
	afx_msg void OnSuOk();
	afx_msg void OnSuCancel();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_STARTTARGETDB_H__33885BA6_DCBD_4092_B37E_62E5E09FD549__INCLUDED_)




