#if !defined(AFX_UCPDETAIL_H__FD5E33DC_62F2_4382_93AA_AE64DC60FB21__INCLUDED_)
#define AFX_UCPDETAIL_H__FD5E33DC_62F2_4382_93AA_AE64DC60FB21__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// UCPDetail.h : header file
//

class CUCInfo;
class CVas;
class CWas;
/////////////////////////////////////////////////////////////////////////////
// CUCPDetail dialog

class CUCPDetail : public CDialog
{
public:
	CUCInfo	*m_ucinfo;
	CVas	*m_vas;
	CWas	*m_was;
	CString	m_tmp_broker, m_tmp_port, m_tmp_shmid;

// Construction
public:
	CUCPDetail(CWnd* pParent = NULL);   // standard constructor
	~CUCPDetail();
	void SetElement(CUCInfo *ucinfo);
	CUCInfo *GetElement();
	void SetDefault(CString broker, CString port, CString shmid);
	void EnableApply(CEdit &edit);
	void EnableApply();
	void CheckFile(CEdit &edit, bool file = true);
	bool CheckApply();


// Dialog Data
	//{{AFX_DATA(CUCPDetail)
	enum { IDD = IDD_UCP_DETAIL };
	CStatic	m_ucpd_txt_appl_server_shm_id;
	CStatic	m_ucpd_txt_time_to_kill;
	CStatic	m_ucpd_txt_appl_server;
	CStatic	m_ucpd_txt_session_timeout;
	CStatic	m_ucpd_txt_sql_log_time;
	CStatic	m_ucpd_txt_appl_server_max_size;
	CStatic	m_ucpd_txt_access_list;
	CButton	m_ucpd_txt_sc;
	CStatic	m_ucpd_txt_job_queue_size;
	CStatic	m_ucpd_txt_broker_port;
	CStatic	m_ucpd_txt_max_num_appl_server;
	CStatic	m_ucpd_txt_min_num_appl_server;
	CStatic	m_ucpd_txt_log_dir;
	CButton	m_ucpd_txt_li;
	CButton	m_ucpd_txt_etc;
	CStatic	m_ucpd_txt_source_env;
	CButton	m_ucpd_txt_cp;
	CStatic	m_ucpd_txt_broker;
	CButton	m_ucpd_txt_as;
	CButton	m_ucpd_cancel;
	CButton	m_ucpd_ok;
	CButton	m_ucpd_more;
	CEdit	m_ucpd_access_list;
	CButton	m_ucpd_security;
	CButton	m_ucpd_access_log;
	CEdit	m_ucpd_job_queue_size;
	CEdit	m_ucpd_session_timeout;
	CEdit	m_ucpd_time_to_kill;
	CEdit	m_ucpd_sql_log_time;
	CButton	m_ucpd_sql_log;
	CEdit	m_ucpd_source_env;
	CButton	m_ucpd_log_backup;
	CEdit	m_ucpd_log_dir;
	CEdit	m_ucpd_appl_server_max_size;
	CEdit	m_ucpd_appl_server_shm_id;
	CButton	m_ucpd_auto_add_appl_server;
	CEdit	m_ucpd_max_num_appl_server;
	CEdit	m_ucpd_min_num_appl_server;
	CComboBox	m_ucpd_appl_server;
	CEdit	m_ucpd_broker_port;
	CButton	m_ucpd_service;
	CEdit	m_ucpd_broker;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CUCPDetail)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CUCPDetail)
	virtual BOOL OnInitDialog();
	afx_msg void OnUcpdService();
	afx_msg void OnUcpdLogbackupOnoff();
	afx_msg void OnUcpdSqlTrackingOnoff();
	afx_msg void OnUcpdAccessLogOnoff();
	afx_msg void OnUcpdSecurityOnoff();
	afx_msg void OnSelchangeServiceType();
	afx_msg void OnUcpdApply();
	afx_msg void OnUcpdCancel();
	afx_msg void OnUcpdMore();
	afx_msg void OnChangeUcpdBname();
	afx_msg void OnChangeUcpdPort();
	afx_msg void OnChangeUcpdSmid();
	afx_msg void OnUcpdAutoadd();
	afx_msg void OnChangeUcpdMin();
	afx_msg void OnChangeUcpdMax();
	afx_msg void OnChangeUcpdSizelimit();
	afx_msg void OnChangeUcpdLogdir();
	afx_msg void OnChangeUcpdSqltime();
	afx_msg void OnChangeUcpdSecuriyFile();
	afx_msg void OnChangeUcpdEnvFile();
	afx_msg void OnChangeUcpdTimetokill();
	afx_msg void OnChangeUcpdStimeout();
	afx_msg void OnChangeUcpdQueuesize();
	afx_msg void OnKillfocusUcpdLogdir();
	afx_msg void OnKillfocusUcpdSecuriyFile();
	afx_msg void OnKillfocusUcpdEnvFile();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_UCPDETAIL_H__FD5E33DC_62F2_4382_93AA_AE64DC60FB21__INCLUDED_)
