#if !defined(AFX_VAS_H__D67F82EC_496C_42A1_8AB4_0CB69D837B4D__INCLUDED_)
#define AFX_VAS_H__D67F82EC_496C_42A1_8AB4_0CB69D837B4D__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// Vas.h : header file
//

class CUCInfo;
/////////////////////////////////////////////////////////////////////////////
// CVas dialog

class CVas : public CDialog
{
public:
	CUCInfo	*m_ucinfo;
// Construction
public:
	CVas(CWnd* pParent = NULL);   // standard constructor
	void SetElement(CUCInfo *ucinfo);
	void EnableApply();
	bool CheckApply();
	void EnableApply(CEdit &edit);
	void CheckFile(CEdit &edit, bool file = false);

// Dialog Data
	//{{AFX_DATA(CVas)
	enum { IDD = IDD_UCP_VAS };
	CStatic	m_vas_txt_appl_root;
	CStatic	m_vas_txt_priority_gap;
	CStatic	m_vas_txt_file_upload_temp_dir;
	CStatic	m_vas_txt_error_log;
	CStatic	m_vas_txt_compress_size;
	CButton	m_vas_cancel;
	CEdit	m_vas_appl_root;
	CButton	m_vas_session;
	CEdit	m_vas_priority_gap;
	CButton	m_vas_apply;
	CEdit	m_vas_file_upload_temp_dir;
	CComboBox	m_vas_error_log;
	CButton	m_vas_enc_appl;
	CEdit	m_vas_compress_size;
	CButton	m_vas_oid_check;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CVas)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CVas)
	virtual BOOL OnInitDialog();
	afx_msg void OnVasEncOnoff();
	afx_msg void OnVasSession();
	afx_msg void OnVasCheckoid();
	afx_msg void OnChangeVasSroot();
	afx_msg void OnKillfocusVasSroot();
	afx_msg void OnChangeVasFu();
	afx_msg void OnKillfocusVasFu();
	afx_msg void OnSelchangeVasErrlog();
	afx_msg void OnChangeVasCsize();
	afx_msg void OnChangeVasPgap();
	afx_msg void OnVasOk();
	afx_msg void OnVasCancel();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_VAS_H__D67F82EC_496C_42A1_8AB4_0CB69D837B4D__INCLUDED_)
