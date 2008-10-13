// Vas.cpp : implementation file
//

#include "stdafx.h"
#include "cubridtray.h"
#include "Vas.h"
#include "UCconf.h"
#include "Filename.h"
#include "lang.h"
#include "Env.h"
#include "io.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


extern CEnv		theEnv;
extern CLang	theLang;
/////////////////////////////////////////////////////////////////////////////
// CVas dialog


CVas::CVas(CWnd* pParent /*=NULL*/)
	: CDialog(CVas::IDD, pParent)
{
	//{{AFX_DATA_INIT(CVas)
	//}}AFX_DATA_INIT
	m_ucinfo = NULL;
}


void CVas::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CVas)
	DDX_Control(pDX, IDC_VAS_TXT_SROOT, m_vas_txt_appl_root);
	DDX_Control(pDX, IDC_VAS_TXT_PGAP, m_vas_txt_priority_gap);
	DDX_Control(pDX, IDC_VAS_TXT_FU, m_vas_txt_file_upload_temp_dir);
	DDX_Control(pDX, IDC_VAS_TXT_ERRLOG, m_vas_txt_error_log);
	DDX_Control(pDX, IDC_VAS_TXT_CSIZE, m_vas_txt_compress_size);
	DDX_Control(pDX, IDC_VAS_CANCEL, m_vas_cancel);
	DDX_Control(pDX, IDC_VAS_SROOT, m_vas_appl_root);
	DDX_Control(pDX, IDC_VAS_SESSION, m_vas_session);
	DDX_Control(pDX, IDC_VAS_PGAP, m_vas_priority_gap);
	DDX_Control(pDX, IDC_VAS_OK, m_vas_apply);
	DDX_Control(pDX, IDC_VAS_FU, m_vas_file_upload_temp_dir);
	DDX_Control(pDX, IDC_VAS_ERRLOG, m_vas_error_log);
	DDX_Control(pDX, IDC_VAS_ENC_ONOFF, m_vas_enc_appl);
	DDX_Control(pDX, IDC_VAS_CSIZE, m_vas_compress_size);
	DDX_Control(pDX, IDC_VAS_CHECKOID, m_vas_oid_check);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CVas, CDialog)
	//{{AFX_MSG_MAP(CVas)
	ON_BN_CLICKED(IDC_VAS_ENC_ONOFF, OnVasEncOnoff)
	ON_BN_CLICKED(IDC_VAS_SESSION, OnVasSession)
	ON_BN_CLICKED(IDC_VAS_CHECKOID, OnVasCheckoid)
	ON_EN_CHANGE(IDC_VAS_SROOT, OnChangeVasSroot)
	ON_EN_KILLFOCUS(IDC_VAS_SROOT, OnKillfocusVasSroot)
	ON_EN_CHANGE(IDC_VAS_FU, OnChangeVasFu)
	ON_EN_KILLFOCUS(IDC_VAS_FU, OnKillfocusVasFu)
	ON_CBN_SELCHANGE(IDC_VAS_ERRLOG, OnSelchangeVasErrlog)
	ON_EN_CHANGE(IDC_VAS_CSIZE, OnChangeVasCsize)
	ON_EN_CHANGE(IDC_VAS_PGAP, OnChangeVasPgap)
	ON_BN_CLICKED(IDC_VAS_OK, OnVasOk)
	ON_BN_CLICKED(IDC_VAS_CANCEL, OnVasCancel)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CVas message handlers
void CVas::SetElement(CUCInfo *ucinfo)
{
	m_ucinfo = ucinfo;
}

BOOL CVas::OnInitDialog() 
{
	CDialog::OnInitDialog();
	
	SetWindowText(theLang.GetMessage(WN_VAS_TITLE));
	if (m_ucinfo) {
		m_vas_enc_appl.SetWindowText(theLang.GetMessage(WN_VAS_ENC_APPL));
		m_vas_enc_appl.SetCheck(m_ucinfo->m_enc_appl);
		m_vas_session.SetWindowText(theLang.GetMessage(WN_VAS_SESSION));
		m_vas_session.SetCheck(m_ucinfo->m_session);
		m_vas_oid_check.SetWindowText(theLang.GetMessage(WN_VAS_OID_CHECK));
		m_vas_oid_check.SetCheck(m_ucinfo->m_oid_check);
		m_vas_txt_appl_root.SetWindowText(theLang.GetMessage(WN_VAS_APPL_ROOT));
		m_vas_appl_root.SetWindowText(m_ucinfo->m_appl_root);
		m_vas_txt_file_upload_temp_dir.SetWindowText(theLang.GetMessage(WN_VAS_FILE_UPLOAD_TEMP_DIR));
		m_vas_file_upload_temp_dir.SetWindowText(m_ucinfo->m_file_upload_temp_dir);
		m_vas_txt_error_log.SetWindowText(theLang.GetMessage(WN_VAS_ERROR_LOG));
		m_vas_error_log.SetCurSel(m_ucinfo->m_error_log);
		m_vas_txt_compress_size.SetWindowText(theLang.GetMessage(WN_VAS_COMPRESS_SIZE));
		m_vas_compress_size.SetWindowText(m_ucinfo->m_compress_size);
		m_vas_txt_priority_gap.SetWindowText(theLang.GetMessage(WN_VAS_PRIORITY_GAP));
		m_vas_priority_gap.SetWindowText(m_ucinfo->m_priority_gap);
	}

	m_vas_apply.SetWindowText(theLang.GetMessage(WN_VAS_OK));
	m_vas_cancel.SetWindowText(theLang.GetMessage(WN_VAS_CANCEL));
	m_vas_apply.EnableWindow(false);

	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

void CVas::OnVasEncOnoff() 
{
	EnableApply();
}

void CVas::OnVasSession() 
{
	EnableApply();
}

void CVas::OnVasCheckoid() 
{
	EnableApply();
}

void CVas::OnChangeVasSroot() 
{
	CheckFile(m_vas_appl_root);
}

void CVas::OnKillfocusVasSroot() 
{
	CheckFile(m_vas_appl_root);
}

void CVas::OnChangeVasFu() 
{
	CheckFile(m_vas_file_upload_temp_dir);
}

void CVas::OnKillfocusVasFu() 
{
	CheckFile(m_vas_file_upload_temp_dir);
}

void CVas::OnSelchangeVasErrlog() 
{
	EnableApply();
}

void CVas::OnChangeVasCsize() 
{
	EnableApply(m_vas_compress_size);
}

void CVas::OnChangeVasPgap() 
{
	EnableApply(m_vas_priority_gap);
}

void CVas::OnVasOk() 
{
	m_ucinfo->m_enc_appl = !m_vas_enc_appl.GetCheck() ? false : true;
	m_ucinfo->m_session = !m_vas_session.GetCheck() ? false : true;
	m_ucinfo->m_oid_check = !m_vas_oid_check.GetCheck() ? false : true;
	m_vas_appl_root.GetWindowText(m_ucinfo->m_appl_root);
	m_vas_file_upload_temp_dir.GetWindowText(m_ucinfo->m_file_upload_temp_dir);
	m_ucinfo->m_error_log = m_vas_error_log.GetCurSel();
	m_vas_compress_size.GetWindowText(m_ucinfo->m_compress_size);
	m_vas_priority_gap.GetWindowText(m_ucinfo->m_priority_gap);

	CDialog::OnOK();
}

void CVas::OnVasCancel() 
{
	CDialog::OnCancel();
}

void CVas::EnableApply(CEdit &edit)
{
CString	tempStr;

	edit.GetWindowText(tempStr);
	if (tempStr.IsEmpty() || !CheckApply()) {
		m_vas_apply.EnableWindow(false);
	} else {
		m_vas_apply.EnableWindow(true);
	}
}

void CVas::EnableApply()
{
	if (CheckApply()) {
		m_vas_apply.EnableWindow(true);
	} else {
		m_vas_apply.EnableWindow(false);
	}
}

void CVas::CheckFile(CEdit &edit, bool file)
{
CFilename	tmpFile;

	edit.GetWindowText(tmpFile);
	
	if (file) {
		if (tmpFile.isIncludeDir()) {
			MessageBox(theLang.GetMessage(MSG_UCP_PATH));
			m_vas_apply.EnableWindow(false);
			return;
		}

		tmpFile = theEnv.GetUniCAS() + "\\bin\\" + tmpFile;
		tmpFile.Unix2Dos();
		
		if (_access(LPCSTR(tmpFile), 0)) {
			m_vas_apply.EnableWindow(false);
			return;
		}
	} else {
		if (tmpFile.isFullpath()) {
			MessageBox(theLang.GetMessage(MSG_UCP_FULLPATH));
			m_vas_apply.EnableWindow(false);
			return;
		}

		if (tmpFile.isIncludeDosPath()) {
			MessageBox(theLang.GetMessage(MSG_UCP_DOSPATH));
			m_vas_apply.EnableWindow(false);
			return;
		}
		
		tmpFile = theEnv.GetUniCAS() + '\\' + tmpFile;
		tmpFile.Unix2Dos();
		
		if (_access(LPCSTR(tmpFile), 0)) {
			m_vas_apply.EnableWindow(false);
			return;
		}
	}

	EnableApply();
}

bool CVas::CheckApply()
{
CString	tempStr;

	m_vas_appl_root.GetWindowText(tempStr);
	if (tempStr.IsEmpty()) return false;

	m_vas_file_upload_temp_dir.GetWindowText(tempStr);
	if (tempStr.IsEmpty()) return false;

	m_vas_compress_size.GetWindowText(tempStr);
	if (tempStr.IsEmpty()) return false;

	m_vas_priority_gap.GetWindowText(tempStr);
	if (tempStr.IsEmpty()) return false;

	return true;
}
