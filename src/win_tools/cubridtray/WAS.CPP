// Was.cpp : implementation file
//

#include "stdafx.h"
#include "cubridtray.h"
#include "UCconf.h"
#include "Was.h"
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
// CWas dialog


CWas::CWas(CWnd* pParent /*=NULL*/)
	: CDialog(CWas::IDD, pParent)
{
	//{{AFX_DATA_INIT(CWas)
	//}}AFX_DATA_INIT
	m_ucinfo = NULL;
}


void CWas::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CWas)
	DDX_Control(pDX, IDC_WAS_TXT_SROOT, m_was_txt_appl_root);
	DDX_Control(pDX, IDC_WAS_TXT_FU, m_was_txt_file_upload_temp_dir);
	DDX_Control(pDX, IDC_WAS_TXT_ERRLOG, m_was_txt_error_log);
	DDX_Control(pDX, IDC_WAS_TXT_DELIMITER, m_was_txt_file_upload_delimiter);
	DDX_Control(pDX, IDC_WAS_CANCEL, m_was_cancel);
	DDX_Control(pDX, IDC_WAS_ERRLOG, m_was_error_log);
	DDX_Control(pDX, IDC_WAS_TRIM, m_was_entry_value_trim);
	DDX_Control(pDX, IDC_WAS_SROOT, m_was_appl_root);
	DDX_Control(pDX, IDC_WAS_SESSION, m_was_session);
	DDX_Control(pDX, IDC_WAS_FU, m_was_file_upload_temp_dir);
	DDX_Control(pDX, IDC_WAS_ENC_ONOFF, m_was_enc_appl);
	DDX_Control(pDX, IDC_WAS_DELIMITER, m_was_file_upload_delimiter);
	DDX_Control(pDX, IDC_WAS_COOKIE, m_was_set_cookie);
	DDX_Control(pDX, IDC_WAS_CHECKOID, m_was_oid_check);
	DDX_Control(pDX, ID_WAS_OK, m_was_apply);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CWas, CDialog)
	//{{AFX_MSG_MAP(CWas)
	ON_BN_CLICKED(IDC_WAS_ENC_ONOFF, OnWasEncOnoff)
	ON_BN_CLICKED(IDC_WAS_SESSION, OnWasSession)
	ON_BN_CLICKED(IDC_WAS_COOKIE, OnWasCookie)
	ON_BN_CLICKED(IDC_WAS_TRIM, OnWasTrim)
	ON_BN_CLICKED(IDC_WAS_CHECKOID, OnWasCheckoid)
	ON_EN_CHANGE(IDC_WAS_SROOT, OnChangeWasSroot)
	ON_EN_KILLFOCUS(IDC_WAS_SROOT, OnKillfocusWasSroot)
	ON_EN_CHANGE(IDC_WAS_FU, OnChangeWasFu)
	ON_EN_KILLFOCUS(IDC_WAS_FU, OnKillfocusWasFu)
	ON_CBN_SELCHANGE(IDC_WAS_ERRLOG, OnSelchangeWasErrlog)
	ON_EN_CHANGE(IDC_WAS_DELIMITER, OnChangeWasDelimiter)
	ON_BN_CLICKED(ID_WAS_OK, OnWasOk)
	ON_BN_CLICKED(IDC_WAS_CANCEL, OnWasCancel)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CWas message handlers
void CWas::SetElement(CUCInfo *ucinfo)
{
	m_ucinfo = ucinfo;
}

BOOL CWas::OnInitDialog() 
{
	CDialog::OnInitDialog();
	
	SetWindowText(theLang.GetMessage(WN_WAS_TITLE));
	if (m_ucinfo) {
		m_was_enc_appl.SetWindowText(theLang.GetMessage(WN_WAS_ENC_APPL));
		m_was_enc_appl.SetCheck(m_ucinfo->m_enc_appl);
		m_was_session.SetWindowText(theLang.GetMessage(WN_WAS_SESSION));
		m_was_session.SetCheck(m_ucinfo->m_session);
		m_was_set_cookie.SetWindowText(theLang.GetMessage(WN_WAS_SET_COOKIE));
		m_was_set_cookie.SetCheck(m_ucinfo->m_set_cookie);
		m_was_entry_value_trim.SetWindowText(theLang.GetMessage(WN_WAS_ENTRY_VALUE_TRIM));
		m_was_entry_value_trim.SetCheck(m_ucinfo->m_entry_value_trim);
		m_was_oid_check.SetWindowText(theLang.GetMessage(WN_WAS_OID_CHECK));
		m_was_oid_check.SetCheck(m_ucinfo->m_oid_check);

		m_was_txt_appl_root.SetWindowText(theLang.GetMessage(WN_WAS_APPL_ROOT));
		m_was_appl_root.SetWindowText(m_ucinfo->m_appl_root);
		m_was_txt_file_upload_temp_dir.SetWindowText(theLang.GetMessage(WN_WAS_FILE_UPLOAD_TEMP_DIR));
		m_was_file_upload_temp_dir.SetWindowText(m_ucinfo->m_file_upload_temp_dir);
		m_was_txt_error_log.SetWindowText(theLang.GetMessage(WN_WAS_ERROR_LOG));
		m_was_error_log.SetCurSel(m_ucinfo->m_error_log);
	    CEdit* pComboEdit=(CEdit*)(GetDlgItem( IDC_WAS_ERRLOG )->GetWindow(GW_CHILD));
		pComboEdit->SetReadOnly();
		m_was_txt_file_upload_delimiter.SetWindowText(theLang.GetMessage(WN_WAS_FILE_UPLOAD_DELIMITER));
		m_was_file_upload_delimiter.SetWindowText(m_ucinfo->m_file_upload_delimiter);
	}
	m_was_apply.SetWindowText(theLang.GetMessage(WN_WAS_OK));
	m_was_cancel.SetWindowText(theLang.GetMessage(WN_WAS_CANCEL));

	m_was_apply.EnableWindow(false);

	return TRUE; 
}

void CWas::OnWasEncOnoff() 
{
	EnableApply();	
}

void CWas::OnWasSession() 
{
	EnableApply();	
}

void CWas::OnWasCookie() 
{
	EnableApply();		
}

void CWas::OnWasTrim() 
{
	EnableApply();		
}

void CWas::OnWasCheckoid() 
{
	EnableApply();		
}

void CWas::OnChangeWasSroot() 
{
	CheckFile(m_was_appl_root);
}

void CWas::OnKillfocusWasSroot() 
{
	CheckFile(m_was_appl_root);
}

void CWas::OnChangeWasFu() 
{
	CheckFile(m_was_file_upload_temp_dir);
}

void CWas::OnKillfocusWasFu() 
{
	CheckFile(m_was_file_upload_temp_dir);
}

void CWas::OnSelchangeWasErrlog() 
{
	EnableApply();		
}

void CWas::OnChangeWasDelimiter() 
{
	EnableApply(m_was_file_upload_delimiter);
}

void CWas::OnWasOk() 
{
	m_ucinfo->m_enc_appl = !m_was_enc_appl.GetCheck() ? false : true;
	m_ucinfo->m_session = !m_was_session.GetCheck() ? false : true;
	m_ucinfo->m_set_cookie = !m_was_set_cookie.GetCheck() ? false : true;
	m_ucinfo->m_entry_value_trim = !m_was_entry_value_trim.GetCheck() ? false : true;
	m_ucinfo->m_oid_check = !m_was_oid_check.GetCheck() ? false : true;
	m_was_appl_root.GetWindowText(m_ucinfo->m_appl_root);
	m_was_file_upload_temp_dir.GetWindowText(m_ucinfo->m_file_upload_temp_dir);
	m_ucinfo->m_error_log = m_was_error_log.GetCurSel();
	m_was_file_upload_delimiter.GetWindowText(m_ucinfo->m_file_upload_delimiter);

	CDialog::OnOK();
}

void CWas::OnWasCancel() 
{
	CDialog::OnCancel();	
}

void CWas::EnableApply(CEdit &edit)
{
CString	tempStr;

	edit.GetWindowText(tempStr);
	if (tempStr.IsEmpty() || !CheckApply()) {
		m_was_apply.EnableWindow(false);
	} else {
		m_was_apply.EnableWindow(true);
	}
}

void CWas::EnableApply()
{
	if (CheckApply()) {
		m_was_apply.EnableWindow(true);
	} else {
		m_was_apply.EnableWindow(false);
	}
}

void CWas::CheckFile(CEdit &edit, bool file)
{
CFilename	tmpFile;

	edit.GetWindowText(tmpFile);
	
	if (file) {
		if (tmpFile.isIncludeDir()) {
			MessageBox(theLang.GetMessage(MSG_UCP_PATH));
			m_was_apply.EnableWindow(false);
			return;
		}

		tmpFile = theEnv.GetUniCAS() + "\\bin\\" + tmpFile;
		tmpFile.Unix2Dos();
		
		if (_access(LPCSTR(tmpFile), 0)) {
			m_was_apply.EnableWindow(false);
			return;
		}
	} else {
		if (tmpFile.isFullpath()) {
			MessageBox(theLang.GetMessage(MSG_UCP_FULLPATH));
			m_was_apply.EnableWindow(false);
			return;
		}

		if (tmpFile.isIncludeDosPath()) {
			MessageBox(theLang.GetMessage(MSG_UCP_DOSPATH));
			m_was_apply.EnableWindow(false);
			return;
		}
		
		tmpFile = theEnv.GetUniCAS() + '\\' + tmpFile;
		tmpFile.Unix2Dos();
		
		if (_access(LPCSTR(tmpFile), 0)) {
			m_was_apply.EnableWindow(false);
			return;
		}
	}

	EnableApply();
}

bool CWas::CheckApply()
{
CString	tempStr;

	m_was_appl_root.GetWindowText(tempStr);
	if (tempStr.IsEmpty()) return false;

	m_was_file_upload_temp_dir.GetWindowText(tempStr);
	if (tempStr.IsEmpty()) return false;

	m_was_file_upload_delimiter.GetWindowText(tempStr);
	if (tempStr.IsEmpty()) return false;

	return true;
}
