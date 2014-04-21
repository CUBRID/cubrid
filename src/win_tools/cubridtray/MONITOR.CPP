// CMonitor.cpp : implementation file
//

#include "stdafx.h"
#include "unitray.h"
#include "UCconf.h"
#include "uc_admin.h"
#include "UCMDetail.h"
#include "UCMInfo.h"
#include "Monitor.h"
#include "lang.h"
#include "env.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

extern CEnv		theEnv;
extern CLang	theLang;
/////////////////////////////////////////////////////////////////////////////
// CMonitor dialog


CMonitor::CMonitor(CWnd* pParent /*=NULL*/)
	: CDialog(CMonitor::IDD, pParent)
{
	//{{AFX_DATA_INIT(Monitor)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT
	m_unicasconf = NULL;
	m_index = -1;
	for (int i = 0; i < MAXBROKER; i++) {
		m_detail[i] = NULL;
	}
}

CMonitor::~CMonitor()
{
CUCInfo	*ucinfo;

	int count = m_unicasconf->m_List.GetCount();
	for (int i = 0; i < count; i++) {
		ucinfo = (CUCInfo *)m_unicasconf->m_List.GetAt(m_unicasconf->m_List.FindIndex(i));
//		if (ucinfo->m_pointer) delete (CUCMInfos *) ucinfo->m_pointer;
	}

	for (int j = 0; j < MAXBROKER; j++) {
		if (m_detail[j]) {
			m_detail[j]->DestroyWindow();
			delete m_detail[j];
			m_detail[j] = NULL;
		}
	}
}

void CMonitor::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CMonitor)
	DDX_Control(pDX, ID_UCM_END, m_ucm_cancel);
	DDX_Control(pDX, ID_UCM_SERVICE_ONOFF, m_ucm_onoff);
	DDX_Control(pDX, ID_UCM_MONITOR, m_ucm_monitor);
	DDX_Control(pDX, IDC_UCM_LIST, m_ucm_list);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CMonitor, CDialog)
	//{{AFX_MSG_MAP(CMonitor)
	ON_NOTIFY(NM_DBLCLK, IDC_UCM_LIST, OnDblclkUcmList)
	ON_BN_CLICKED(ID_UCM_END, OnUcmEnd)
	ON_NOTIFY(NM_CLICK, IDC_UCM_LIST, OnClickUcmList)
	ON_BN_CLICKED(ID_UCM_MONITOR, OnUcmMonitor)
	ON_WM_TIMER()
	ON_BN_CLICKED(ID_UCM_SERVICE_ONOFF, OnUcmServiceOnoff)
	ON_WM_CLOSE()
	//}}AFX_MSG_MAP
	ON_MESSAGE(WM_STOPMONITOR, OnStopDetailMonitor)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CMonitor message handlers
void CMonitor::SetUnicasConf(CUCConf *unicasconf, CWnd *parent)
{
	m_unicasconf = unicasconf;
	m_parent = parent;
}

void CMonitor::DisplayItem(CUCInfo *ucinfo, int position, bool update)
{
char tempstr[100];

	if (!update) m_ucm_list.InsertItem(position, ucinfo->m_broker);
	m_ucm_list.SetItemText(position, 1, ucinfo->m_service ? ON : OFF);
	if (ucinfo->m_appl_server == NO_CAS) {
		m_ucm_list.SetItemText(position, 2, STR_CAS);
	} else if (ucinfo->m_appl_server == NO_WAS) {
		m_ucm_list.SetItemText(position, 2, STR_WAS);
	} else if (ucinfo->m_appl_server == NO_VAS) {
		m_ucm_list.SetItemText(position, 2, STR_VAS);
	} else if (ucinfo->m_appl_server == NO_ULS) {
		m_ucm_list.SetItemText(position, 2, STR_ULS);
	} else if (ucinfo->m_appl_server == NO_AMS) {
		m_ucm_list.SetItemText(position, 2, STR_AMS);
	}
	m_ucm_list.SetItemText(position, 3, "0");
	m_ucm_list.SetItemText(position, 4, LPCSTR(ucinfo->m_broker_port));
	m_ucm_list.SetItemText(position, 5, ucinfo->m_auto_add_appl_server ? ON : OFF);
	if (ucinfo->m_appl_server == NO_CAS) {
		sprintf(tempstr, "%s", ucinfo->m_session_timeout);
		m_ucm_list.SetItemText(position, 6, tempstr);
		sprintf(tempstr, "%s", ucinfo->m_sql_log ? ON : OFF);
		m_ucm_list.SetItemText(position, 7, tempstr);
	} else {
		sprintf(tempstr, "%s", ucinfo->m_session ? ON : OFF,
			ucinfo->m_session_timeout);
		m_ucm_list.SetItemText(position, 6, tempstr);
		sprintf(tempstr, "%s", ucinfo->m_sql_log ? ON : OFF);
		m_ucm_list.SetItemText(position, 7, tempstr);
	}

	SetTimer(1030, 2*1000, NULL);
}

BOOL CMonitor::OnInitDialog()
{
CUCInfo	*ucinfo;

	CDialog::OnInitDialog();

	SetWindowText(theLang.GetMessage(WN_UCM_TITLE));
	m_ucm_monitor.SetWindowText(theLang.GetMessage(WN_UCM_MONITOR));
	m_ucm_onoff.SetWindowText(theLang.GetMessage(WN_UCM_SERVICE));
	m_ucm_cancel.SetWindowText(theLang.GetMessage(WN_UCM_CANCEL));

	m_ucm_list.InsertColumn(0, theLang.GetMessage(TITLE_UCP_BROKER), LVCFMT_CENTER, 10*10);
	m_ucm_list.InsertColumn(1, theLang.GetMessage(TITLE_UCP_SERVICE), LVCFMT_CENTER, 6*10);
	m_ucm_list.InsertColumn(2, theLang.GetMessage(TITLE_UCP_APPL_SERVER), LVCFMT_CENTER, 6*10);
	m_ucm_list.InsertColumn(3, theLang.GetMessage(TITLE_UCP_JOB_QUEUE), LVCFMT_CENTER, 6*10);
	m_ucm_list.InsertColumn(4, theLang.GetMessage(TITLE_UCP_BROKER_PORT), LVCFMT_CENTER, 6*10);
	m_ucm_list.InsertColumn(5, theLang.GetMessage(TITLE_UCP_AUTO_ADD_APPL_SERVER), LVCFMT_CENTER, 4*10);
	m_ucm_list.InsertColumn(6, theLang.GetMessage(TITLE_UCP_TIMEOUT), LVCFMT_CENTER, 6*10);
	m_ucm_list.InsertColumn(7, theLang.GetMessage(TITLE_UCP_SQL_LOG), LVCFMT_CENTER, 6*10);

	if (m_unicasconf) {
		int count = m_unicasconf->m_List.GetCount();
		for (int i = 0; i < count; i++) {
			ucinfo = (CUCInfo *)m_unicasconf->m_List.GetAt(m_unicasconf->m_List.FindIndex(i));
			DisplayItem(ucinfo, i);
		}
	}

	m_ucm_onoff.EnableWindow(false);
	m_ucm_monitor.EnableWindow(false);

	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

void CMonitor::OnClickUcmList(NMHDR* pNMHDR, LRESULT* pResult)
{
CUCInfo	*ucinfo;
int	index;

	index = m_ucm_list.GetNextItem(-1, LVNI_ALL | LVNI_SELECTED);
	if (index == -1) {
		m_ucm_monitor.EnableWindow(false);
		m_ucm_onoff.EnableWindow(false);
		m_ucm_onoff.SetWindowText(theLang.GetMessage(WN_UCM_SERVICE));
	} else {
		ucinfo = (CUCInfo *)m_unicasconf->m_List.GetAt(m_unicasconf->m_List.FindIndex(index));

		m_ucm_onoff.EnableWindow(true);
		if (ucinfo->m_service) {
			m_ucm_monitor.EnableWindow(true);
			m_ucm_onoff.SetWindowText(theLang.GetMessage(WN_UCM_SERVICE_OFF));
		} else {
			m_ucm_monitor.EnableWindow(false);
			m_ucm_onoff.SetWindowText(theLang.GetMessage(WN_UCM_SERVICE_ON));
		}
	}
}

void CMonitor::OnDblclkUcmList(NMHDR* pNMHDR, LRESULT* pResult)
{
	OnUcmMonitor();
//	*pResult = 0;
}

void CMonitor::OnUcmMonitor()
{
CUCInfo	*ucinfo;

	m_index = m_ucm_list.GetNextItem( -1, LVNI_ALL | LVNI_SELECTED);
	if (m_index == -1) return;

	ucinfo = (CUCInfo *)m_unicasconf->m_List.GetAt(m_unicasconf->m_List.FindIndex(m_index));

	if (!ucinfo->m_service) {
		AfxMessageBox(theLang.GetMessage(MSG_UC_CANT_MONITOR), MB_OK | MB_ICONSTOP);
		return;
	}

	if (!m_detail[m_index]) {
		m_detail[m_index] = new CUCMDetail();
		m_detail[m_index]->SetBroker(ucinfo->m_broker,
								     ucinfo->m_appl_server,
								  	 ucinfo->m_auto_add_appl_server,
									 ucinfo->m_min_num_appl_server,
									 ucinfo->m_max_num_appl_server,
									 m_index,
									 this);
		m_detail[m_index]->Create(CUCMDetail::IDD);
		m_detail[m_index]->ShowWindow(SW_SHOW);
//		delete m_detail;
//		m_detail = NULL;
	}
}

void CMonitor::OnUcmServiceOnoff()
{
#ifdef UNICAS_ON
CUCInfo	*ucinfo;
char	err_msg[100];
int		rc;

	m_index = m_ucm_list.GetNextItem( -1, LVNI_ALL | LVNI_SELECTED);
	if (m_index == -1) return;

	ucinfo = (CUCInfo *)m_unicasconf->m_List.GetAt(m_unicasconf->m_List.FindIndex(m_index));

	if (ucinfo->m_service) {
		rc = uc_off((char *)LPCSTR(ucinfo->m_broker), err_msg);
		if (rc == 0) {
			ucinfo->m_service = false;
			m_ucm_list.SetItemText(m_index, 1, ucinfo->m_service ? ON : OFF);
			m_ucm_onoff.SetWindowText(theLang.GetMessage(WN_UCM_SERVICE_ON));
			if (m_detail[m_index]) {
				m_detail[m_index]->DestroyWindow();
				delete m_detail[m_index];
				m_detail[m_index] = NULL;
			}
		}
	} else {
		rc = uc_on((char *)LPCSTR(ucinfo->m_broker), err_msg);
		if (rc == 0) {
			ucinfo->m_service = true;
			m_ucm_list.SetItemText(m_index, 1, ucinfo->m_service ? ON : OFF);
			m_ucm_onoff.SetWindowText(theLang.GetMessage(WN_UCM_SERVICE_OFF));
		}
	}
#endif
}

void CMonitor::OnUcmEnd()
{
	KillTimer(1030);
//	CDialog::OnOK();
	m_parent->SendMessage(WM_STOPMONITOR, 0, 0);
}

void CMonitor::OnClose()
{
	KillTimer(1030);
	m_parent->SendMessage(WM_STOPMONITOR, 0, 0);

//	CDialog::OnClose();
}

void CMonitor::OnStopDetailMonitor(WPARAM wParam, LPARAM lParam)
{
int	index = (int) lParam;

	if (m_detail[index]) {
		m_detail[index]->DestroyWindow();
		m_detail[index] = NULL;
	}
}

void CMonitor::OnTimer(UINT nIDEvent)
{
#ifdef UNICAS_ON
CUCInfo	*ucinfo;
char	err_msg[100];
CString	job_queue;

	int count = m_unicasconf->m_List.GetCount();
	for (int i = 0; i < count; i++) {
		ucinfo = (CUCInfo *)m_unicasconf->m_List.GetAt(m_unicasconf->m_List.FindIndex(i));
		if (ucinfo->m_service) {
			job_queue.Format("%d", uc_job_queue((char *)LPCSTR(ucinfo->m_broker), err_msg));
			m_ucm_list.SetItemText(i, 3, job_queue);
		} else {
			m_ucm_list.SetItemText(i, 3, "0");
		}
	}
#endif
}

