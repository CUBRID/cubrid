#if !defined(AFX_WAS_H__67226B99_94FD_4DC5_8D4C_0F7BDA105DA9__INCLUDED_)
#define AFX_WAS_H__67226B99_94FD_4DC5_8D4C_0F7BDA105DA9__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// Was.h : header file
//

class CUCInfo;
/////////////////////////////////////////////////////////////////////////////
// CWas dialog

class CWas : public CDialog
{
public:
	CUCInfo	*m_ucinfo;

// Construction
public:
	CWas(CWnd* pParent = NULL);   // standard constructor
	void SetElement(CUCInfo *ucinfo);
	void EnableApply();
	bool CheckApply();
	void EnableApply(CEdit &edit);
	void CheckFile(CEdit &edit, bool file = false);
	
// Dialog Data
	//{{AFX_DATA(CWas)
	enum { IDD = IDD_UCP_WAS };
	CStatic	m_was_txt_appl_root;
	CStatic	m_was_txt_file_upload_temp_dir;
	CStatic	m_was_txt_error_log;
	CStatic	m_was_txt_file_upload_delimiter;
	CButton	m_was_cancel;
	CComboBox	m_was_error_log;
	CButton	m_was_entry_value_trim;
	CEdit	m_was_appl_root;
	CButton	m_was_session;
	CEdit	m_was_file_upload_temp_dir;
	CButton	m_was_enc_appl;
	CEdit	m_was_file_upload_delimiter;
	CButton	m_was_set_cookie;
	CButton	m_was_oid_check;
	CButton	m_was_apply;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CWas)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CWas)
	virtual BOOL OnInitDialog();
	afx_msg void OnWasEncOnoff();
	afx_msg void OnWasSession();
	afx_msg void OnWasCookie();
	afx_msg void OnWasTrim();
	afx_msg void OnWasCheckoid();
	afx_msg void OnChangeWasSroot();
	afx_msg void OnKillfocusWasSroot();
	afx_msg void OnChangeWasFu();
	afx_msg void OnKillfocusWasFu();
	afx_msg void OnSelchangeWasErrlog();
	afx_msg void OnChangeWasDelimiter();
	afx_msg void OnWasOk();
	afx_msg void OnWasCancel();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_WAS_H__67226B99_94FD_4DC5_8D4C_0F7BDA105DA9__INCLUDED_)
