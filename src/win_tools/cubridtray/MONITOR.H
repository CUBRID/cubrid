#if !defined(AFX_CMONITOR_H__CA3224FC_B1C9_4276_A9D9_E8A096A11857__INCLUDED_)
#define AFX_CMONITOR_H__CA3224FC_B1C9_4276_A9D9_E8A096A11857__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// Monitor.h : header file
//

#include "Redirect.h"

class CUCConf;
class CUCMDetail;
class CMonitor;

#define	MAXBROKER	30
#define	WM_STOPMONITOR	1030
/////////////////////////////////////////////////////////////////////////////
// CMonitor dialog

class CMonitor : public CDialog
{
public:
	CWnd		*m_parent;
	CUCConf		*m_unicasconf;
	CUCMDetail	*m_detail[MAXBROKER];
	int			m_index;

	bool	m_errno;
	CString	m_data;


// Construction
public:
	CMonitor(CWnd* pParent = NULL);   // standard constructor
	~CMonitor();
	void SetUnicasConf(CUCConf *unicasconf, CWnd *parent);
	void DisplayItem(CUCInfo *ucinfo, int position, bool update = false);
	void OnStopDetailMonitor(WPARAM wParam, LPARAM lParam);

// Dialog Data
	//{{AFX_DATA(CMonitor)
	enum { IDD = IDD_UC_MONITOR };
	CButton	m_ucm_cancel;
	CButton	m_ucm_onoff;
	CButton	m_ucm_monitor;
	CListCtrl	m_ucm_list;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CMonitor)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CMonitor)
	virtual BOOL OnInitDialog();
	afx_msg void OnDblclkUcmList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnUcmEnd();
	afx_msg void OnClickUcmList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnUcmMonitor();
	afx_msg void OnTimer(UINT nIDEvent);
	afx_msg void OnUcmServiceOnoff();
	afx_msg void OnClose();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_MONITOR_H__CA3224FC_B1C9_4276_A9D9_E8A096A11857__INCLUDED_)
