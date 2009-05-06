#if !defined(AFX_UCMDETAIL_H__22FBBB68_4558_45F0_BD6F_E71B8FC8FA86__INCLUDED_)
#define AFX_UCMDETAIL_H__22FBBB68_4558_45F0_BD6F_E71B8FC8FA86__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// UCMDetail.h : header file
//

class CUCMInfo;
/////////////////////////////////////////////////////////////////////////////
// CUCMDetail dialog
#define	MAX_AS	100

class CUCMDetail : public CDialog
{
// Construction
public:
	CDialog	*m_parent;
	int		m_index;
	CString	m_broker;
	int		m_as;
	bool	m_autoadd;
	int		m_min;
	int		m_max;
	bool	m_flag;
	CRect	m_main_client;

public:
	CUCMDetail(CWnd* pParent = NULL);   // standard constructor
	~CUCMDetail();
	void SetBroker(CString broker, int as, bool autoadd, int min, int max, int index, CDialog *parent);
	void Display();
	void DisplayItem(T_AS_INFO info, int position, bool update = false);

// Dialog Data
	//{{AFX_DATA(CUCMDetail)
	enum { IDD = IDD_UCM_DETAIL };
	CButton	m_ucmd_cancel;
	CButton	m_ucmd_restart;
	CButton	m_ucmd_drop;
	CButton	m_ucmd_add;
	CListCtrl	m_ucmd_list;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CUCMDetail)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CUCMDetail)
	virtual BOOL OnInitDialog();
	afx_msg void OnUcmdAdd();
	afx_msg void OnUcmdDrop();
	afx_msg void OnUcmdEnd();
	afx_msg void OnClose();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnUcmdRestart();
	afx_msg void OnClickUcmdList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnDblclkUcmdList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnUnitoolVsql();
	afx_msg void OnUpdateUnitoolVsql(CCmdUI* pCmdUI);
	afx_msg void OnUnitoolEmanager();
	afx_msg void OnUpdateUnitoolEmanager(CCmdUI* pCmdUI);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_UCMDETAIL_H__22FBBB68_4558_45F0_BD6F_E71B8FC8FA86__INCLUDED_)
