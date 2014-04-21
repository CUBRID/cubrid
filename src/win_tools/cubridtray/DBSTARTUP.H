#if !defined(AFX_DBSTARTUP_H__4E9A0142_AD8C_47D5_84A4_01773334BB51__INCLUDED_)
#define AFX_DBSTARTUP_H__4E9A0142_AD8C_47D5_84A4_01773334BB51__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// DBStartUp.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CDBStartUp dialog

class CORDBList;

class CDBStartUp : public CDialog
{
// Construction
public:
	CORDBList	*m_OrdbList;

public:
	CDBStartUp(CORDBList *ordblist, CWnd* pParent = NULL);

// Dialog Data
	//{{AFX_DATA(CDBStartUp)
	enum { IDD = IDD_STARTSERVER };
	CButton	m_su_cancel;
	CListBox	m_ListDB;
	CButton	m_su_ok;
	//}}AFX_DATA
	
	CStringList		m_DBName;					// Selected DB name from combo-box
	

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CDBStartUp)
	public:
	virtual BOOL DestroyWindow();
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CDBStartUp)
	afx_msg void OnSuOk();
	afx_msg void OnSuCancel();
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	virtual BOOL OnInitDialog();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_DBSTARTUP_H__4E9A0142_AD8C_47D5_84A4_01773334BB51__INCLUDED_)
