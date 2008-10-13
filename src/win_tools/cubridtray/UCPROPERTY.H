#if !defined(AFX_UCPROPERTY_H__A9B97409_8172_4D83_BA8D_B9A132CE2C6F__INCLUDED_)
#define AFX_UCPROPERTY_H__A9B97409_8172_4D83_BA8D_B9A132CE2C6F__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// UCProperty.h : header file
//

class CUCConf;
class CUCPDetail;
/////////////////////////////////////////////////////////////////////////////
// CUCProperty dialog

class CUCProperty : public CDialog
{
public:
	CUCConf		*m_unicasconf;
	CUCPDetail	*m_detail;

// Construction
public:
	CUCProperty(CWnd* pParent = NULL);   // standard constructor
	~CUCProperty();
	void SetUnicasConf(CUCConf *unicasconf);
	void DisplayItem(CUCInfo *ucinfo, int position, bool update = false);
	bool CheckApply();

// Dialog Data
	//{{AFX_DATA(CUCProperty)
	enum { IDD = IDD_UC_PROPERTY };
	CStatic	m_ucp_txt_shmid;
	CStatic	m_ucp_txt_logfile;
	CButton	m_ucp_new;
	CButton	m_ucp_txt_masterinfo;
	CButton	m_ucp_cancel;
	CButton	m_ucp_apply;
	CButton	m_ucp_delete;
	CButton	m_ucp_update;
	CListCtrl	m_ucp_list;
	CEdit	m_ucp_logfile;
	CEdit	m_ucp_shmid;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CUCProperty)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CUCProperty)
	virtual BOOL OnInitDialog();
	afx_msg void OnUcpCancel();
	afx_msg void OnUcpApply();
	afx_msg void OnUcpNew();
	afx_msg void OnUcpUpdate();
	afx_msg void OnUcpDelete();
	afx_msg void OnClickUcpList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnDblclkUcpList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnChangeUcpLogfile();
	afx_msg void OnKillfocusUcpLogfile();
	afx_msg void OnChangeUcpSmid();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_UCPROPERTY_H__A9B97409_8172_4D83_BA8D_B9A132CE2C6F__INCLUDED_)
