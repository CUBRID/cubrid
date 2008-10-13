// UCProperty.cpp : implementation file
//

#include "stdafx.h"
#include "cubridtray.h"
#include "UCConf.h"
#include "UCPDetail.h"
#include "UCProperty.h"
#include "env.h"
#include "lang.h"
#include "io.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

extern CEnv		theEnv;
extern CLang	theLang;
/////////////////////////////////////////////////////////////////////////////
// CUCProperty dialog


CUCProperty::CUCProperty(CWnd* pParent /*=NULL*/)
	: CDialog(CUCProperty::IDD, pParent)
{
	m_unicasconf = NULL;
	m_detail = NULL;
	//{{AFX_DATA_INIT(CUCProperty)
	//}}AFX_DATA_INIT
}

CUCProperty::~CUCProperty()
{
	if (m_detail) {
		delete m_detail;
	}
}

void CUCProperty::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CUCProperty)
	DDX_Control(pDX, IDC_UCP_TXT_SMID, m_ucp_txt_shmid);
	DDX_Control(pDX, IDC_UCP_TXT_LOGFILE, m_ucp_txt_logfile);
	DDX_Control(pDX, IDC_UCP_NEW, m_ucp_new);
	DDX_Control(pDX, IDC_UCP_MASTERINFO, m_ucp_txt_masterinfo);
	DDX_Control(pDX, IDC_UCP_CANCEL, m_ucp_cancel);
	DDX_Control(pDX, IDC_UCP_APPLY, m_ucp_apply);
	DDX_Control(pDX, IDC_UCP_DELETE, m_ucp_delete);
	DDX_Control(pDX, IDC_UCP_UPDATE, m_ucp_update);
	DDX_Control(pDX, IDC_UCP_LIST, m_ucp_list);
	DDX_Control(pDX, IDC_UCP_LOGFILE, m_ucp_logfile);
	DDX_Control(pDX, IDC_UCP_SMID, m_ucp_shmid);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CUCProperty, CDialog)
	//{{AFX_MSG_MAP(CUCProperty)
	ON_BN_CLICKED(IDC_UCP_CANCEL, OnUcpCancel)
	ON_BN_CLICKED(IDC_UCP_APPLY, OnUcpApply)
	ON_BN_CLICKED(IDC_UCP_NEW, OnUcpNew)
	ON_BN_CLICKED(IDC_UCP_UPDATE, OnUcpUpdate)
	ON_BN_CLICKED(IDC_UCP_DELETE, OnUcpDelete)
	ON_NOTIFY(NM_CLICK, IDC_UCP_LIST, OnClickUcpList)
	ON_NOTIFY(NM_DBLCLK, IDC_UCP_LIST, OnDblclkUcpList)
	ON_EN_CHANGE(IDC_UCP_LOGFILE, OnChangeUcpLogfile)
	ON_EN_KILLFOCUS(IDC_UCP_LOGFILE, OnKillfocusUcpLogfile)
	ON_EN_CHANGE(IDC_UCP_SMID, OnChangeUcpSmid)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CUCProperty message handlers

void CUCProperty::SetUnicasConf(CUCConf *unicasconf)
{
	m_unicasconf = unicasconf;
}

void CUCProperty::DisplayItem(CUCInfo *ucinfo, int position, bool update)
{
char tempstr[100];

	if (!update) m_ucp_list.InsertItem(position, ucinfo->m_broker);
	m_ucp_list.SetItemText(position, 1, ucinfo->m_service ? ON : OFF);
	if (ucinfo->m_appl_server == NO_CAS) {
		m_ucp_list.SetItemText(position, 2, STR_CAS);
	} else if (ucinfo->m_appl_server == NO_WAS) {
		m_ucp_list.SetItemText(position, 2, STR_WAS);
	} else if (ucinfo->m_appl_server == NO_VAS) {
		m_ucp_list.SetItemText(position, 2, STR_VAS);
	} else if (ucinfo->m_appl_server == NO_ULS) {
		m_ucp_list.SetItemText(position, 2, STR_ULS);
	} else if (ucinfo->m_appl_server == NO_AMS) {
		m_ucp_list.SetItemText(position, 2, STR_AMS);
	}
	m_ucp_list.SetItemText(position, 3, LPCSTR(ucinfo->m_broker_port));
	m_ucp_list.SetItemText(position, 4, ucinfo->m_auto_add_appl_server ? ON : OFF);
	sprintf(tempstr, "%d", ucinfo->m_min_num_appl_server);
	m_ucp_list.SetItemText(position, 5, tempstr);
	sprintf(tempstr, "%d", ucinfo->m_max_num_appl_server);
	m_ucp_list.SetItemText(position, 6, tempstr);
}

BOOL CUCProperty::OnInitDialog() 
{
CUCInfo	*ucinfo;

	CDialog::OnInitDialog();

	SetWindowText(theLang.GetMessage(WN_UCP_TITLE));
	m_ucp_new.SetWindowText(theLang.GetMessage(WN_UCP_NEW));
	m_ucp_update.SetWindowText(theLang.GetMessage(WN_UCP_UPDATE));
	m_ucp_delete.SetWindowText(theLang.GetMessage(WN_UCP_DELETE));
	m_ucp_apply.SetWindowText(theLang.GetMessage(WN_UCP_OK));
	m_ucp_cancel.SetWindowText(theLang.GetMessage(WN_UCP_CANCEL));
	m_ucp_txt_masterinfo.SetWindowText(theLang.GetMessage(WN_UCP_MASTERINFO));
	m_ucp_txt_shmid.SetWindowText(theLang.GetMessage(WN_UCP_SM));
	m_ucp_txt_logfile.SetWindowText(theLang.GetMessage(WN_UCP_LOGFILE));

	m_ucp_list.InsertColumn(0, theLang.GetMessage(TITLE_UCP_BROKER), LVCFMT_CENTER, 10*10);
	m_ucp_list.InsertColumn(1, theLang.GetMessage(TITLE_UCP_SERVICE), LVCFMT_CENTER, 6*10);
	m_ucp_list.InsertColumn(2, theLang.GetMessage(TITLE_UCP_APPL_SERVER), LVCFMT_CENTER, 6*10);
	m_ucp_list.InsertColumn(3, theLang.GetMessage(TITLE_UCP_BROKER_PORT), LVCFMT_CENTER, 6*10);
	m_ucp_list.InsertColumn(4, theLang.GetMessage(TITLE_UCP_AUTO_ADD_APPL_SERVER), LVCFMT_CENTER, 4*10);
	m_ucp_list.InsertColumn(5, theLang.GetMessage(TITLE_UCP_MIN_NUM_APPL_SERVER), LVCFMT_CENTER, 4*10);
	m_ucp_list.InsertColumn(6, theLang.GetMessage(TITLE_UCP_MAX_NUM_APPL_SERVER), LVCFMT_CENTER, 4*10);

	if (m_unicasconf) {
		int count = m_unicasconf->m_List.GetCount();
		for (int i = 0; i < count; i++) {
			ucinfo = (CUCInfo *)m_unicasconf->m_List.GetAt(m_unicasconf->m_List.FindIndex(i));
			DisplayItem(ucinfo, i);
		}
		m_ucp_shmid.SetWindowText(m_unicasconf->m_master_shm_id);
		m_ucp_logfile.SetWindowText(m_unicasconf->m_admin_log_file);
	}
	
	m_ucp_update.EnableWindow(false);
	m_ucp_delete.EnableWindow(false);
	m_ucp_apply.EnableWindow(false);

	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

void CUCProperty::OnUcpNew() 
{
CUCInfo	*ucinfo;
int		index = m_unicasconf->m_List.GetCount();

	if (!m_detail) {
		m_detail = new CUCPDetail();

		CString	broker, port, shmid;
		broker.Format("broker%d", index);
		ucinfo = (CUCInfo *)m_unicasconf->m_List.GetAt(m_unicasconf->m_List.FindIndex(index-1));
		port.Format("%d", atoi(ucinfo->m_broker_port) + 1000);
		shmid.Format("%d", atoi(port) + 1);

		m_detail->SetDefault(broker, port, shmid);

		if (m_detail->DoModal() == IDOK) {
			ucinfo = m_detail->GetElement();
			DisplayItem(ucinfo, index);
			m_unicasconf->m_List.AddTail(ucinfo);
			m_ucp_apply.EnableWindow(true);
		}
		delete m_detail;
		m_detail = NULL;
	}
}

void CUCProperty::OnUcpUpdate() 
{
CUCInfo	*ucinfo;
int	index;
	
	index = m_ucp_list.GetNextItem( -1, LVNI_ALL | LVNI_SELECTED);
	if (index == -1) return;
	ucinfo = (CUCInfo *)m_unicasconf->m_List.GetAt(m_unicasconf->m_List.FindIndex(index));

	if (!m_detail) {
		m_detail = new CUCPDetail();
		m_detail->SetElement(ucinfo);
		if (m_detail->DoModal() == IDOK) {
			DisplayItem(ucinfo, index, true);
			m_ucp_apply.EnableWindow(true);
		}
		delete m_detail;
		m_detail = NULL;
	}
}

void CUCProperty::OnUcpDelete() 
{
int	index;

	index = m_ucp_list.GetNextItem( -1, LVNI_ALL | LVNI_SELECTED);
	if (index == -1) return;

	if (AfxMessageBox(theLang.GetMessage(MSG_UCP_DELETE), MB_YESNO | MB_ICONQUESTION) == IDYES) {
		m_unicasconf->m_List.RemoveAt(m_unicasconf->m_List.FindIndex(index));
		m_ucp_list.DeleteItem(index);
		m_ucp_apply.EnableWindow(true);
	}
}

void CUCProperty::OnUcpApply() 
{
CFilename	logfile;

	m_ucp_logfile.GetWindowText(logfile);
	if (logfile.isFullpath()) {
		MessageBox(theLang.GetMessage(MSG_UCP_FULLPATH));
		m_ucp_apply.EnableWindow(false);
		return;
	}

	if (logfile.isIncludeDosPath()) {
		MessageBox(theLang.GetMessage(MSG_UCP_DOSPATH));
		m_ucp_apply.EnableWindow(false);
		return;
	}

	logfile = theEnv.GetUniCAS() + '\\' + logfile;
	logfile.Unix2Dos();
	if (_access(LPCSTR(logfile.GetNonLeaf()), 0)) {
		MessageBox(theLang.GetMessage(MSG_UCP_FILENOTFOUND));
		return;
	}

	CDialog::OnOK();	
}

void CUCProperty::OnUcpCancel() 
{
	CDialog::OnCancel();
}

void CUCProperty::OnClickUcpList(NMHDR* pNMHDR, LRESULT* pResult) 
{
	m_ucp_update.EnableWindow(true);
	m_ucp_delete.EnableWindow(true);
}

void CUCProperty::OnDblclkUcpList(NMHDR* pNMHDR, LRESULT* pResult) 
{
	OnUcpUpdate();
}

void CUCProperty::OnChangeUcpSmid() 
{
	if (CheckApply()) {
		m_ucp_apply.EnableWindow(true);
	} else {
		m_ucp_apply.EnableWindow(false);
	}
}

void CUCProperty::OnChangeUcpLogfile() 
{
 CFilename	logfile;

	m_ucp_logfile.GetWindowText(logfile);
	if (logfile.isFullpath()) {
		MessageBox(theLang.GetMessage(MSG_UCP_FULLPATH));
		m_ucp_apply.EnableWindow(false);
		return;
	}

	if (logfile.isIncludeDosPath()) {
		MessageBox(theLang.GetMessage(MSG_UCP_DOSPATH));
		m_ucp_apply.EnableWindow(false);
		return;
	}

	logfile = theEnv.GetUniCAS() + '\\' + logfile;
	logfile.Unix2Dos();
	if (_access(LPCSTR(logfile.GetNonLeaf()), 0)) {
		m_ucp_apply.EnableWindow(false);
		return;
	}

	if (CheckApply()) {
		m_ucp_apply.EnableWindow(true);
	} else {
		m_ucp_apply.EnableWindow(false);
	}
}

void CUCProperty::OnKillfocusUcpLogfile() 
{
CFilename	logfile;

	m_ucp_logfile.GetWindowText(logfile);
	logfile = theEnv.GetUniCAS() + '\\' + logfile;
	logfile.Unix2Dos();
	if (_access(LPCSTR(logfile.GetNonLeaf()), 0)) {
		MessageBox(theLang.GetMessage(MSG_UCP_FILENOTFOUND));
		m_ucp_apply.EnableWindow(false);
	}
}

bool CUCProperty::CheckApply()
{
CString	tempStr;

	m_ucp_shmid.GetWindowText(tempStr);
	if (tempStr.IsEmpty()) return false;
	m_ucp_logfile.GetWindowText(tempStr);
	if (tempStr.IsEmpty()) return false;

	return true;
}

