// UCPDetail.cpp : implementation file
//

#include "stdafx.h"
#include "cubridtray.h"
#include "UCconf.h"
#include "Was.h"
#include "Vas.h"
#include "UCPDetail.h"
#include "Filename.h"
#include "lang.h"
#include "env.h"
#include "io.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

extern CLang	theLang;
extern CEnv		theEnv;
/////////////////////////////////////////////////////////////////////////////
// CUCPDetail dialog


CUCPDetail::CUCPDetail(CWnd* pParent /*=NULL*/)
	: CDialog(CUCPDetail::IDD, pParent)
{
	//{{AFX_DATA_INIT(CUCPDetail)
	//}}AFX_DATA_INIT
	m_ucinfo = NULL;
	m_vas = NULL;
	m_was = NULL;
}

CUCPDetail::~CUCPDetail()
{
	if (m_vas) {
		delete m_vas;
		m_vas = NULL;
	}

	if (m_was) {
		delete m_was;
		m_was = NULL;
	}
}

void CUCPDetail::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CUCPDetail)
	DDX_Control(pDX, IDC_UCPD_TXT_SMID, m_ucpd_txt_appl_server_shm_id);
	DDX_Control(pDX, IDC_UCPD_TXT_TIMETOKILL, m_ucpd_txt_time_to_kill);
	DDX_Control(pDX, IDC_UCPD_TXT_STYPE, m_ucpd_txt_appl_server);
	DDX_Control(pDX, IDC_UCPD_TXT_STIMEOUT, m_ucpd_txt_session_timeout);
	DDX_Control(pDX, IDC_UCPD_TXT_SIZELIMIT, m_ucpd_txt_appl_server_max_size);
	DDX_Control(pDX, IDC_UCPD_TXT_SECURIY_FILE, m_ucpd_txt_access_list);
	DDX_Control(pDX, IDC_UCPD_TXT_SC, m_ucpd_txt_sc);
	DDX_Control(pDX, IDC_UCPD_TXT_QUEUESIZE, m_ucpd_txt_job_queue_size);
	DDX_Control(pDX, IDC_UCPD_TXT_PORT, m_ucpd_txt_broker_port);
	DDX_Control(pDX, IDC_UCPD_TXT_MAX, m_ucpd_txt_max_num_appl_server);
	DDX_Control(pDX, IDC_UCPD_TXT_MIN, m_ucpd_txt_min_num_appl_server);
	DDX_Control(pDX, IDC_UCPD_TXT_LOGDIR, m_ucpd_txt_log_dir);
	DDX_Control(pDX, IDC_UCPD_TXT_LI, m_ucpd_txt_li);
	DDX_Control(pDX, IDC_UCPD_TXT_ETC, m_ucpd_txt_etc);
	DDX_Control(pDX, IDC_UCPD_TXT_ENV_FILE, m_ucpd_txt_source_env);
	DDX_Control(pDX, IDC_UCPD_TXT_CP, m_ucpd_txt_cp);
	DDX_Control(pDX, IDC_UCPD_TXT_BNAME, m_ucpd_txt_broker);
	DDX_Control(pDX, IDC_UCPD_TXT_AS, m_ucpd_txt_as);
	DDX_Control(pDX, IDC_UCPD_CANCEL, m_ucpd_cancel);
	DDX_Control(pDX, IDC_UCPD_OK, m_ucpd_ok);
	DDX_Control(pDX, IDC_UCPD_MORE, m_ucpd_more);
	DDX_Control(pDX, IDC_UCPD_SECURIY_FILE, m_ucpd_access_list);
	DDX_Control(pDX, IDC_UCPD_SECURITY_ONOFF, m_ucpd_security);
	DDX_Control(pDX, IDC_UCPD_ALOG_ONOFF, m_ucpd_access_log);
	DDX_Control(pDX, IDC_UCPD_QUEUESIZE, m_ucpd_job_queue_size);
	DDX_Control(pDX, IDC_UCPD_STIMEOUT, m_ucpd_session_timeout);
	DDX_Control(pDX, IDC_UCPD_TIMETOKILL, m_ucpd_time_to_kill);
	DDX_Control(pDX, IDC_UCPD_SQLT_ONOFF, m_ucpd_sql_log);
	DDX_Control(pDX, IDC_UCPD_ENV_FILE, m_ucpd_source_env);
	DDX_Control(pDX, IDC_UCPD_LOGBACKUP_ONOFF, m_ucpd_log_backup);
	DDX_Control(pDX, IDC_UCPD_LOGDIR, m_ucpd_log_dir);
	DDX_Control(pDX, IDC_UCPD_SIZELIMIT, m_ucpd_appl_server_max_size);
	DDX_Control(pDX, IDC_UCPD_SMID, m_ucpd_appl_server_shm_id);
	DDX_Control(pDX, IDC_UCPD_AUTOADD, m_ucpd_auto_add_appl_server);
	DDX_Control(pDX, IDC_UCPD_MAX, m_ucpd_max_num_appl_server);
	DDX_Control(pDX, IDC_UCPD_MIN, m_ucpd_min_num_appl_server);
	DDX_Control(pDX, IDC_UCPD_STYPE, m_ucpd_appl_server);
	DDX_Control(pDX, IDC_UCPD_PORT, m_ucpd_broker_port);
	DDX_Control(pDX, IDC_UCPD_S_ONOFF, m_ucpd_service);
	DDX_Control(pDX, IDC_UCPD_BNAME, m_ucpd_broker);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CUCPDetail, CDialog)
	//{{AFX_MSG_MAP(CUCPDetail)
	ON_BN_CLICKED(IDC_UCPD_S_ONOFF, OnUcpdService)
	ON_BN_CLICKED(IDC_UCPD_LOGBACKUP_ONOFF, OnUcpdLogbackupOnoff)
	ON_BN_CLICKED(IDC_UCPD_SQLT_ONOFF, OnUcpdSqlTrackingOnoff)
	ON_BN_CLICKED(IDC_UCPD_ALOG_ONOFF, OnUcpdAccessLogOnoff)
	ON_BN_CLICKED(IDC_UCPD_SECURITY_ONOFF, OnUcpdSecurityOnoff)
	ON_CBN_SELCHANGE(IDC_UCPD_STYPE, OnSelchangeServiceType)
	ON_BN_CLICKED(IDC_UCPD_OK, OnUcpdApply)
	ON_BN_CLICKED(IDC_UCPD_CANCEL, OnUcpdCancel)
	ON_BN_CLICKED(IDC_UCPD_MORE, OnUcpdMore)
	ON_EN_CHANGE(IDC_UCPD_BNAME, OnChangeUcpdBname)
	ON_EN_CHANGE(IDC_UCPD_PORT, OnChangeUcpdPort)
	ON_EN_CHANGE(IDC_UCPD_SMID, OnChangeUcpdSmid)
	ON_BN_CLICKED(IDC_UCPD_AUTOADD, OnUcpdAutoadd)
	ON_EN_CHANGE(IDC_UCPD_MIN, OnChangeUcpdMin)
	ON_EN_CHANGE(IDC_UCPD_MAX, OnChangeUcpdMax)
	ON_EN_CHANGE(IDC_UCPD_SIZELIMIT, OnChangeUcpdSizelimit)
	ON_EN_CHANGE(IDC_UCPD_LOGDIR, OnChangeUcpdLogdir)
	ON_EN_CHANGE(IDC_UCPD_SECURIY_FILE, OnChangeUcpdSecuriyFile)
	ON_EN_CHANGE(IDC_UCPD_ENV_FILE, OnChangeUcpdEnvFile)
	ON_EN_CHANGE(IDC_UCPD_TIMETOKILL, OnChangeUcpdTimetokill)
	ON_EN_CHANGE(IDC_UCPD_STIMEOUT, OnChangeUcpdStimeout)
	ON_EN_CHANGE(IDC_UCPD_QUEUESIZE, OnChangeUcpdQueuesize)
	ON_EN_KILLFOCUS(IDC_UCPD_LOGDIR, OnKillfocusUcpdLogdir)
	ON_EN_KILLFOCUS(IDC_UCPD_SECURIY_FILE, OnKillfocusUcpdSecuriyFile)
	ON_EN_KILLFOCUS(IDC_UCPD_ENV_FILE, OnKillfocusUcpdEnvFile)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CUCPDetail message handlers
void CUCPDetail::SetElement(CUCInfo *ucinfo)
{
	m_ucinfo = ucinfo;
}

CUCInfo *CUCPDetail::GetElement()
{
	return (m_ucinfo);
}

void CUCPDetail::SetDefault(CString broker, CString port, CString shmid)
{
	m_tmp_broker = broker;
	m_tmp_port = port;
	m_tmp_shmid = shmid;
}

BOOL CUCPDetail::OnInitDialog()
{
char	tempstr[100];

	CDialog::OnInitDialog();
	
	if (!m_ucinfo) {
		m_ucinfo = new CUCInfo();
		m_ucinfo->m_broker = m_tmp_broker;
		m_ucinfo->m_broker_port = m_tmp_port;
		m_ucinfo->m_appl_server_shm_id = m_tmp_shmid;
	} else {
		m_ucpd_broker.SetReadOnly(true);
	}
	
	CString	title;
	title.Format(theLang.GetMessage(WN_UPD_TITLE), m_ucinfo->m_broker);
	SetWindowText(title);

	m_ucpd_txt_cp.SetWindowText(theLang.GetMessage(WN_UPD_COMMON_PROPERTY));
	m_ucpd_txt_broker.SetWindowText(theLang.GetMessage(WN_UPD_BROKER));
	m_ucpd_broker.SetWindowText(LPCSTR(m_ucinfo->m_broker));
	m_ucpd_service.SetCheck(m_ucinfo->m_service);
	if (m_ucinfo->m_service) {
		m_ucpd_service.SetWindowText(theLang.GetMessage(WN_UPD_SERVICE_ON));
	} else {
		m_ucpd_service.SetWindowText(theLang.GetMessage(WN_UPD_SERVICE_OFF));
	}
	m_ucpd_txt_appl_server.SetWindowText(theLang.GetMessage(WN_UPD_APPL_SERVER));
    CEdit* pComboEdit=(CEdit*)(GetDlgItem( IDC_UCPD_STYPE )->GetWindow(GW_CHILD));
    pComboEdit->SetReadOnly();
	m_ucpd_appl_server.SetCurSel(m_ucinfo->m_appl_server);
	m_ucpd_more.SetWindowText(theLang.GetMessage(WN_UPD_MORE));
	if (m_ucinfo->m_appl_server == 0) {
		m_ucpd_more.EnableWindow(false);
	} else {
		m_ucpd_more.EnableWindow(true);
	}
	m_ucpd_txt_broker_port.SetWindowText(theLang.GetMessage(WN_UPD_BROKER_PORT));
	m_ucpd_broker_port.SetWindowText(LPCSTR(m_ucinfo->m_broker_port));
	m_ucpd_txt_appl_server_shm_id.SetWindowText(theLang.GetMessage(WN_UPD_APPL_SERVER_SHM_ID));
	m_ucpd_appl_server_shm_id.SetWindowText(LPCSTR(m_ucinfo->m_appl_server_shm_id));

	m_ucpd_txt_as.SetWindowText(theLang.GetMessage(WN_UPD_APPLICATION_SERVER));
	m_ucpd_auto_add_appl_server.SetWindowText(theLang.GetMessage(WN_UPD_AUTO_ADD_APPL_SERVER));
	m_ucpd_auto_add_appl_server.SetCheck(m_ucinfo->m_auto_add_appl_server);
	m_ucpd_txt_min_num_appl_server.SetWindowText(theLang.GetMessage(WN_UPD_MIN_NUM_APPL_SERVER));
	sprintf(tempstr, "%d", m_ucinfo->m_min_num_appl_server);
	m_ucpd_min_num_appl_server.SetWindowText(tempstr);
	m_ucpd_txt_max_num_appl_server.SetWindowText(theLang.GetMessage(WN_UPD_MAX_NUM_APPL_SERVER));
	sprintf(tempstr, "%d", m_ucinfo->m_max_num_appl_server);
	m_ucpd_max_num_appl_server.SetWindowText(tempstr);
	m_ucpd_txt_appl_server_max_size.SetWindowText(theLang.GetMessage(WN_UPD_APPL_SERVER_MAX_SIZE));
	sprintf(tempstr, "%d", m_ucinfo->m_appl_server_max_size);
	m_ucpd_appl_server_max_size.SetWindowText(tempstr);

	m_ucpd_txt_li.SetWindowText(theLang.GetMessage(WN_UPD_LOG_INFORMATION));
	m_ucpd_txt_log_dir.SetWindowText(theLang.GetMessage(WN_UPD_LOG_DIR));
	m_ucpd_log_dir.SetWindowText(LPCSTR(m_ucinfo->m_log_dir));
	if (m_ucinfo->m_log_backup) {
		m_ucpd_log_backup.SetWindowText(theLang.GetMessage(WN_UPD_LOG_BACKUP_ON));
	} else {
		m_ucpd_log_backup.SetWindowText(theLang.GetMessage(WN_UPD_LOG_BACKUP_OFF));
	}
	m_ucpd_log_backup.SetCheck(m_ucinfo->m_log_backup);
	if (m_ucinfo->m_sql_log) {
		m_ucpd_sql_log.SetWindowText(theLang.GetMessage(WN_UPD_SQL_LOG_ON));
	} else {
		m_ucpd_sql_log.SetWindowText(theLang.GetMessage(WN_UPD_SQL_LOG_OFF));
	}
	m_ucpd_sql_log.SetCheck(m_ucinfo->m_sql_log);
	if (m_ucinfo->m_access_log) {
		m_ucpd_access_log.SetWindowText(theLang.GetMessage(WN_UPD_ACCESS_LOG_ON));
	} else {
		m_ucpd_access_log.SetWindowText(theLang.GetMessage(WN_UPD_ACCESS_LOG_OFF));
	}
	m_ucpd_access_log.SetCheck(m_ucinfo->m_access_log);

	m_ucpd_txt_sc.SetWindowText(theLang.GetMessage(WN_UPD_SECURITY));
	if (m_ucinfo->m_security) {
		m_ucpd_security.SetWindowText(theLang.GetMessage(WN_UPD_SECURITY_ON));
	} else {
		m_ucpd_security.SetWindowText(theLang.GetMessage(WN_UPD_SECURITY_OFF));
	}
	m_ucpd_security.SetCheck(m_ucinfo->m_security);
	m_ucpd_txt_access_list.SetWindowText(theLang.GetMessage(WN_UPD_ACCESS_LIST));
	m_ucpd_access_list.SetWindowText(LPCSTR(m_ucinfo->m_access_list));

	m_ucpd_txt_etc.SetWindowText(theLang.GetMessage(WN_UPD_ETC));
	m_ucpd_txt_source_env.SetWindowText(theLang.GetMessage(WN_UPD_SOURCE_ENV));
	m_ucpd_source_env.SetWindowText(LPCSTR(m_ucinfo->m_source_env));
	m_ucpd_txt_time_to_kill.SetWindowText(theLang.GetMessage(WN_UPD_TIME_TO_KILL));
	m_ucpd_time_to_kill.SetWindowText(LPCSTR(m_ucinfo->m_time_to_kill));
	m_ucpd_txt_session_timeout.SetWindowText(theLang.GetMessage(WN_UPD_SESSION_TIMEOUT));
	m_ucpd_session_timeout.SetWindowText(LPCSTR(m_ucinfo->m_session_timeout));
	m_ucpd_txt_job_queue_size.SetWindowText(theLang.GetMessage(WN_UPD_JOB_QUEUE_SIZE));
	m_ucpd_job_queue_size.SetWindowText(LPCSTR(m_ucinfo->m_job_queue_size));

	m_ucpd_ok.SetWindowText(theLang.GetMessage(WN_UPD_OK));
	m_ucpd_cancel.SetWindowText(theLang.GetMessage(WN_UPD_CANCEL));
//	m_ucpd_ok.EnableWindow(false);

	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

void CUCPDetail::OnChangeUcpdBname()
{
	EnableApply(m_ucpd_broker);
}

void CUCPDetail::OnUcpdService()
{
	m_ucinfo->m_service = !m_ucinfo->m_service;
	if (m_ucinfo->m_service) {
		m_ucpd_service.SetWindowText("Service ON");
	} else {
		m_ucpd_service.SetWindowText("Service OFF");
	}

	EnableApply();
}

void CUCPDetail::OnSelchangeServiceType()
{
int	sel;

	sel = m_ucpd_appl_server.GetCurSel();
	m_ucinfo->m_appl_server = sel;
	if (m_ucinfo->m_appl_server == 0) {
		m_ucpd_more.EnableWindow(false);
	} else {
		m_ucpd_more.EnableWindow(true);
	}

	EnableApply();
}

void CUCPDetail::OnChangeUcpdPort()
{
	EnableApply(m_ucpd_broker_port);
}

void CUCPDetail::OnChangeUcpdSmid()
{
	EnableApply(m_ucpd_appl_server_shm_id);
}

void CUCPDetail::OnUcpdAutoadd()
{
	EnableApply();
}

void CUCPDetail::OnChangeUcpdMin()
{
	EnableApply(m_ucpd_min_num_appl_server);
}

void CUCPDetail::OnChangeUcpdMax()
{
	EnableApply(m_ucpd_max_num_appl_server);
}

void CUCPDetail::OnChangeUcpdSizelimit()
{
	EnableApply(m_ucpd_appl_server_max_size);
}

void CUCPDetail::OnChangeUcpdLogdir()
{
	CheckFile(m_ucpd_log_dir, false);
}

void CUCPDetail::OnKillfocusUcpdLogdir()
{
	CheckFile(m_ucpd_log_dir, false);
}

void CUCPDetail::OnUcpdLogbackupOnoff()
{
	m_ucinfo->m_log_backup = !m_ucinfo->m_log_backup;
	if (m_ucinfo->m_log_backup) {
		m_ucpd_log_backup.SetWindowText("Backup ON");
	} else {
		m_ucpd_log_backup.SetWindowText("Backup OFF");
	}

	EnableApply();
}

void CUCPDetail::OnUcpdSqlTrackingOnoff()
{
	m_ucinfo->m_sql_log = !m_ucinfo->m_sql_log;
	if (m_ucinfo->m_sql_log) {
		m_ucpd_sql_log.SetWindowText("SQL Tracking ON");
	} else {
		m_ucpd_sql_log.SetWindowText("SQL Tracking OFF");
	}

	EnableApply();
}

void CUCPDetail::OnUcpdAccessLogOnoff()
{
	m_ucinfo->m_access_log = !m_ucinfo->m_access_log;
	if (m_ucinfo->m_access_log) {
		m_ucpd_access_log.SetWindowText("Access LOG ON");
	} else {
		m_ucpd_access_log.SetWindowText("Access LOG OFF");
	}

	EnableApply();
}


void CUCPDetail::OnUcpdSecurityOnoff()
{
	m_ucinfo->m_security = !m_ucinfo->m_security;
	if (m_ucinfo->m_security) {
		m_ucpd_security.SetWindowText("Security ON");
	} else {
		m_ucpd_security.SetWindowText("Security OFF");
	}

	EnableApply();
}

void CUCPDetail::OnChangeUcpdSecuriyFile()
{
	CheckFile(m_ucpd_access_list);
}

void CUCPDetail::OnKillfocusUcpdSecuriyFile()
{
	CheckFile(m_ucpd_access_list);
}


void CUCPDetail::OnChangeUcpdEnvFile()
{
	CheckFile(m_ucpd_source_env);
}

void CUCPDetail::OnKillfocusUcpdEnvFile()
{
	CheckFile(m_ucpd_source_env);
}

void CUCPDetail::OnChangeUcpdTimetokill()
{
	EnableApply(m_ucpd_time_to_kill);
}

void CUCPDetail::OnChangeUcpdStimeout()
{
	EnableApply(m_ucpd_session_timeout);
}

void CUCPDetail::OnChangeUcpdQueuesize()
{
	EnableApply(m_ucpd_job_queue_size);
}

void CUCPDetail::OnUcpdApply()
{
CString	tempstr;

	m_ucpd_broker.GetWindowText(m_ucinfo->m_broker);
	if (m_ucpd_service.GetCheck() == 0) {
		m_ucinfo->m_service = false;
	} else {
		m_ucinfo->m_service = true;
	}
	m_ucinfo->m_appl_server = m_ucpd_appl_server.GetCurSel();
	m_ucpd_broker_port.GetWindowText(m_ucinfo->m_broker_port);
	m_ucpd_appl_server_shm_id.GetWindowText(m_ucinfo->m_appl_server_shm_id);
	if (m_ucpd_auto_add_appl_server.GetCheck() == 0) {
		m_ucinfo->m_auto_add_appl_server = false;
	} else {
		m_ucinfo->m_auto_add_appl_server = true;
	}
	m_ucpd_min_num_appl_server.GetWindowText(tempstr);
	m_ucinfo->m_min_num_appl_server = atoi(LPCSTR(tempstr));
	m_ucpd_max_num_appl_server.GetWindowText(tempstr);
	m_ucinfo->m_max_num_appl_server = atoi(LPCSTR(tempstr));
	m_ucpd_appl_server_max_size.GetWindowText(tempstr);
	m_ucinfo->m_appl_server_max_size = atoi(LPCSTR(tempstr));

	CheckFile(m_ucpd_log_dir, false);
	m_ucpd_log_dir.GetWindowText(m_ucinfo->m_log_dir);
	if (m_ucpd_log_backup.GetCheck() == 0) {
		m_ucinfo->m_log_backup = false;
	} else {
		m_ucinfo->m_log_backup = true;
	}
	if (m_ucpd_sql_log.GetCheck() == 0) {
		m_ucinfo->m_sql_log = false;
	} else {
		m_ucinfo->m_sql_log = true;
	}
	if (m_ucpd_access_log.GetCheck() == 0) {
		m_ucinfo->m_access_log = false;
	} else {
		m_ucinfo->m_access_log = true;
	}

	if (m_ucpd_security.GetCheck() == 0) {
		m_ucinfo->m_security = false;
	} else {
		m_ucinfo->m_security = true;
	}
	CheckFile(m_ucpd_access_list);
	m_ucpd_access_list.GetWindowText(m_ucinfo->m_access_list);

	CheckFile(m_ucpd_source_env);
	m_ucpd_source_env.GetWindowText(m_ucinfo->m_source_env);
	m_ucpd_time_to_kill.GetWindowText(m_ucinfo->m_time_to_kill);
	m_ucpd_session_timeout.GetWindowText(m_ucinfo->m_session_timeout);
	m_ucpd_job_queue_size.GetWindowText(m_ucinfo->m_job_queue_size);

	CDialog::OnOK();
}

void CUCPDetail::OnUcpdCancel()
{
	CDialog::OnCancel();
}

void CUCPDetail::OnUcpdMore()
{
	if (m_ucinfo->m_appl_server == 1) {
		if (!m_was) {
			m_was = new CWas();
			m_was->SetElement(m_ucinfo);
			if (m_was->DoModal() == IDOK) {
			}
			delete m_was;
			m_was = NULL;
		}
	} else {
		if (!m_vas) {
			m_vas = new CVas();
			m_vas->SetElement(m_ucinfo);
			if (m_vas->DoModal() == IDOK) {
			}
			delete m_vas;
			m_vas = NULL;
		}
	}
}

void CUCPDetail::EnableApply(CEdit &edit)
{
CString	tempStr;

	edit.GetWindowText(tempStr);
	if (tempStr.IsEmpty() || !CheckApply()) {
		m_ucpd_ok.EnableWindow(false);
	} else {
		m_ucpd_ok.EnableWindow(true);
	}
}

void CUCPDetail::EnableApply()
{
	if (CheckApply()) {
		m_ucpd_ok.EnableWindow(true);
	} else {
		m_ucpd_ok.EnableWindow(false);
	}
}

void CUCPDetail::CheckFile(CEdit &edit, bool file)
{
CFilename	tmpFile;

	edit.GetWindowText(tmpFile);
	
	if (file) {
		if (tmpFile.isIncludeDir()) {
			MessageBox(theLang.GetMessage(MSG_UCP_PATH));
			m_ucpd_ok.EnableWindow(false);
			return;
		}

		tmpFile = theEnv.GetUniCAS() + "\\bin\\" + tmpFile;
		tmpFile.Unix2Dos();
		
		if (_access(LPCSTR(tmpFile), 0)) {
			m_ucpd_ok.EnableWindow(false);
			return;
		}
	} else {
		if (tmpFile.isFullpath()) {
			MessageBox(theLang.GetMessage(MSG_UCP_FULLPATH));
			m_ucpd_ok.EnableWindow(false);
			return;
		}

		if (tmpFile.isIncludeDosPath()) {
			MessageBox(theLang.GetMessage(MSG_UCP_DOSPATH));
			m_ucpd_ok.EnableWindow(false);
			return;
		}
		
		tmpFile = theEnv.GetUniCAS() + '\\' + tmpFile;
		tmpFile.Unix2Dos();
		
		if (_access(LPCSTR(tmpFile), 0)) {
			m_ucpd_ok.EnableWindow(false);
			return;
		}
	}

	EnableApply();
}

bool CUCPDetail::CheckApply()
{
CString	tempStr;

	m_ucpd_broker.GetWindowText(tempStr);
	if (tempStr.IsEmpty()) return false;

	if (m_ucpd_service.GetCheck() == 0) return true;

	m_ucpd_broker_port.GetWindowText(tempStr);
	if (tempStr.IsEmpty()) return false;

	m_ucpd_appl_server_shm_id.GetWindowText(tempStr);
	if (tempStr.IsEmpty()) return false;

	m_ucpd_min_num_appl_server.GetWindowText(tempStr);
	if (tempStr.IsEmpty()) return false;

	m_ucpd_max_num_appl_server.GetWindowText(tempStr);
	if (tempStr.IsEmpty()) return false;

	m_ucpd_appl_server_max_size.GetWindowText(tempStr);
	if (tempStr.IsEmpty()) return false;

	m_ucpd_log_dir.GetWindowText(tempStr);
	if (tempStr.IsEmpty()) return false;

	m_ucpd_sql_log.GetWindowText(tempStr);
	if (tempStr.IsEmpty()) return false;

	if (m_ucpd_security.GetCheck() != 0) {
		m_ucpd_access_list.GetWindowText(tempStr);
		if (tempStr.IsEmpty()) return false;
	}

	m_ucpd_source_env.GetWindowText(tempStr);
	if (tempStr.IsEmpty()) return false;

	m_ucpd_time_to_kill.GetWindowText(tempStr);
	if (tempStr.IsEmpty()) return false;

	m_ucpd_session_timeout.GetWindowText(tempStr);
	if (tempStr.IsEmpty()) return false;

	m_ucpd_job_queue_size.GetWindowText(tempStr);
	if (tempStr.IsEmpty()) return false;

	return true;
}






