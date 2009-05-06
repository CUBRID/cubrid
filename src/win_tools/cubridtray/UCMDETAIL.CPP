// UCMDetail.cpp : implementation file
//

#include "stdafx.h"
#include "cubridtray.h"
#include "UCMInfo.h"
#include "uc_admin.h"
#include "UCMDetail.h"
#include "lang.h"
#include "time.h"


#include "ManageRegistry.h"




#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

extern	CLang		theLang;
/////////////////////////////////////////////////////////////////////////////
// CUCMDetail dialog


CUCMDetail::CUCMDetail(CWnd* pParent /*=NULL*/)
	: CDialog(CUCMDetail::IDD, pParent)
{
	//{{AFX_DATA_INIT(CUCMDetail)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT
	m_flag = false;
}

CUCMDetail::~CUCMDetail()
{
}

void CUCMDetail::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CUCMDetail)
	DDX_Control(pDX, ID_UCMD_END, m_ucmd_cancel);
	DDX_Control(pDX, ID_UCMD_RESTART, m_ucmd_restart);
	DDX_Control(pDX, ID_UCMD_DROP, m_ucmd_drop);
	DDX_Control(pDX, ID_UCMD_ADD, m_ucmd_add);
	DDX_Control(pDX, IDC_UCMD_LIST, m_ucmd_list);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CUCMDetail, CDialog)
	//{{AFX_MSG_MAP(CUCMDetail)
	ON_BN_CLICKED(ID_UCMD_ADD, OnUcmdAdd)
	ON_BN_CLICKED(ID_UCMD_DROP, OnUcmdDrop)
	ON_BN_CLICKED(ID_UCMD_END, OnUcmdEnd)
	ON_WM_CLOSE()
	ON_WM_TIMER()
	ON_BN_CLICKED(ID_UCMD_RESTART, OnUcmdRestart)
	ON_NOTIFY(NM_CLICK, IDC_UCMD_LIST, OnClickUcmdList)
	ON_NOTIFY(NM_DBLCLK, IDC_UCMD_LIST, OnDblclkUcmdList)
	ON_WM_SIZE()
	ON_COMMAND(PU_UNITOOL_VSQL, OnUnitoolVsql)
	ON_UPDATE_COMMAND_UI(PU_UNITOOL_VSQL, OnUpdateUnitoolVsql)
	ON_COMMAND(PU_UNITOOL_EMANAGER, OnUnitoolEmanager)
	ON_UPDATE_COMMAND_UI(PU_UNITOOL_EMANAGER, OnUpdateUnitoolEmanager)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CUCMDetail message handlers

void CUCMDetail::SetBroker(CString broker, int as, bool autoadd, int min, int max, int index, CDialog *parent)
{
	m_broker = broker;
	m_as = as;
	m_autoadd = autoadd;
	m_min = min;
	m_max = max;
	m_index = index;
	m_parent = parent;
}

void CUCMDetail::Display()
{
#ifdef UNICAS_ON
T_AS_INFO	info[MAX_AS];
char		err_msg[100];
int			rc;
int			max = m_ucmd_list.GetItemCount();

	rc = uc_as_info((char *)LPCSTR(m_broker), info, MAX_AS, err_msg);

	for (int i = 0; i < rc; i++) {
		if (i < max) {
			DisplayItem(info[i], i, true);
		} else {
			DisplayItem(info[i], i);
		}	
	}

	if (i < max) {
		for (int j = i; j < max; j++) {
			m_ucmd_list.DeleteItem(j);
		}
	}

	if (!m_autoadd) {
		if (i >= m_max) { 
			m_ucmd_add.EnableWindow(false);
		} else {
			m_ucmd_add.EnableWindow(true);
		}
		
		if (i <= m_min) {
			m_ucmd_drop.EnableWindow(false);
		} else {
			m_ucmd_drop.EnableWindow(true);
		}
	} else {
		m_ucmd_add.EnableWindow(false);
		m_ucmd_drop.EnableWindow(false);
	}
#endif
}	

void CUCMDetail::DisplayItem(T_AS_INFO info, int position, bool update)
{
#ifdef UNICAS_ON
CString tempStr;
struct tm	*access_time;

	tempStr.Format("%d", position+1);
	if (!update) m_ucmd_list.InsertItem(position, tempStr);
	tempStr.Format("%d", info.pid);
	m_ucmd_list.SetItemText(position, 1, tempStr);
	tempStr.Format("%d", info.num_request);
	m_ucmd_list.SetItemText(position, 2, tempStr);
	tempStr.Format("%d", info.as_port);
	m_ucmd_list.SetItemText(position, 3, tempStr);
	if (info.status == AS_STATUS_IDLE) {
		m_ucmd_list.SetItemText(position, 4, "idle");
	} else if (info.status == AS_STATUS_BUSY) {
		m_ucmd_list.SetItemText(position, 4, "busy");
	} else {
		m_ucmd_list.SetItemText(position, 4, "restart");
	}
	access_time = localtime((const time_t *)&info.last_access_time);
	char	atime[21];
	strftime(atime, 18, "%y/%m/%d %H:%M:%S", access_time);
	m_ucmd_list.SetItemText(position, 5, atime);
	switch (m_as) {
		case	NO_CAS :
				m_ucmd_list.SetItemText(position, 6, info.log_msg);
				break;
		case	NO_WAS :
				m_ucmd_list.SetItemText(position, 6, info.clt_appl_name);
				break;
		case	NO_VAS :
				m_ucmd_list.SetItemText(position, 6, info.clt_appl_name);
				break;
		case	NO_ULS :
		case	NO_AMS :
		default	:
				break;
	}
#endif
}

BOOL CUCMDetail::OnInitDialog() 
{
	CDialog::OnInitDialog();

	/* get window size */
	m_flag = true;
	GetClientRect(m_main_client);

	/* display title */
	CString	title;
	title.Format(theLang.GetMessage(WN_UMD_TITLE), LPCSTR(m_broker));
	SetWindowText(title);
	m_ucmd_add.SetWindowText(theLang.GetMessage(WN_UMD_ADD));
	m_ucmd_drop.SetWindowText(theLang.GetMessage(WN_UMD_DROP));
	m_ucmd_restart.SetWindowText(theLang.GetMessage(WN_UMD_RESTART));
	m_ucmd_cancel.SetWindowText(theLang.GetMessage(WN_UMD_CANCEL));

	m_ucmd_list.InsertColumn(0, theLang.GetMessage(TITLE_UCM_ID), LVCFMT_CENTER, 3*10);
	m_ucmd_list.InsertColumn(1, theLang.GetMessage(TITLE_UCM_PID), LVCFMT_CENTER, 5*10);
	m_ucmd_list.InsertColumn(2, theLang.GetMessage(TITLE_UCM_C), LVCFMT_CENTER, 5*10);
	m_ucmd_list.InsertColumn(3, theLang.GetMessage(TITLE_UCM_PORT), LVCFMT_CENTER, 6*10);
	m_ucmd_list.InsertColumn(4, theLang.GetMessage(TITLE_UCM_STATUS), LVCFMT_CENTER, 6*10);
	m_ucmd_list.InsertColumn(5, theLang.GetMessage(TITLE_UCM_LASTACCESSTIME), LVCFMT_CENTER, 15*10);
	m_ucmd_list.InsertColumn(6, theLang.GetMessage(TITLE_UCM_ACTION), LVCFMT_CENTER, 20*10);

	Display();

	SetTimer(1031, 500, NULL);

	if (m_autoadd) {
		m_ucmd_add.EnableWindow(false);
		m_ucmd_drop.EnableWindow(false);
	} else {
		m_ucmd_add.EnableWindow(true);
		m_ucmd_drop.EnableWindow(true);
	}

	m_ucmd_restart.EnableWindow(false);


	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

void CUCMDetail::OnUcmdAdd() 
{
#ifdef UNICAS_ON
char		err_msg[100];
int			rc;
	
	rc = uc_add((char *) LPCSTR(m_broker), err_msg);
#endif
}

void CUCMDetail::OnUcmdDrop() 
{
#ifdef UNICAS_ON
char		err_msg[100];
int			rc;

	rc = uc_drop((char *) LPCSTR(m_broker), err_msg);
#endif
}

void CUCMDetail::OnClickUcmdList(NMHDR* pNMHDR, LRESULT* pResult) 
{
int	index;

	index = m_ucmd_list.GetNextItem(-1, LVNI_ALL | LVNI_SELECTED);
	if (index == -1) { 
		m_ucmd_restart.EnableWindow(false);
	} else {
		m_ucmd_restart.EnableWindow(true);
	}
}

void CUCMDetail::OnDblclkUcmdList(NMHDR* pNMHDR, LRESULT* pResult) 
{
	OnUcmdRestart();
}

void CUCMDetail::OnUcmdRestart() 
{
#ifdef UNICAS_ON
int	index;
char err_msg[100];
int	rc;

	index = m_ucmd_list.GetNextItem(-1, LVNI_ALL | LVNI_SELECTED);
	if (index == -1) { 
		m_ucmd_restart.EnableWindow(false);
		return;
	} else {
		if (AfxMessageBox(theLang.GetMessage(MSG_UC_RESTART), MB_YESNO | MB_ICONQUESTION) == IDYES) {
			rc = uc_restart((char *)LPCSTR(m_broker), index+1, err_msg);
		}
	}
#endif
}

void CUCMDetail::OnSize(UINT nType, int cx, int cy) 
{
int x, y;
CRect	main_screen, listbox, btn_add, btn_drop, btn_restart, btn_cancel;
CRect	tmpRect;

	if (!m_flag) return;

	main_screen = m_main_client;
	ClientToScreen(main_screen);

	y = cy - m_main_client.bottom;
	x = cx - m_main_client.right;
	
	m_ucmd_list.GetWindowRect(listbox);
	listbox.top = listbox.top - main_screen.top;
	listbox.left = listbox.left - main_screen.left;
	listbox.bottom = listbox.bottom - main_screen.top + y;
	listbox.right = listbox.right - main_screen.left + x;

	m_ucmd_add.GetWindowRect(btn_add);
	btn_add.top = btn_add.top - main_screen.top + y;
	btn_add.left = btn_add.left - main_screen.left + x;
	btn_add.bottom = btn_add.bottom - main_screen.top + y;
	btn_add.right = btn_add.right - main_screen.left + x;

	m_ucmd_drop.GetWindowRect(btn_drop);
	btn_drop.top = btn_drop.top - main_screen.top + y;
	btn_drop.left = btn_drop.left - main_screen.left + x;
	btn_drop.bottom = btn_drop.bottom - main_screen.top + y;
	btn_drop.right = btn_drop.right - main_screen.left + x;

	m_ucmd_restart.GetWindowRect(btn_restart);
	btn_restart.top = btn_restart.top - main_screen.top + y;
	btn_restart.left = btn_restart.left - main_screen.left + x;
	btn_restart.bottom = btn_restart.bottom - main_screen.top + y;
	btn_restart.right = btn_restart.right - main_screen.left + x;

	m_ucmd_cancel.GetWindowRect(btn_cancel);
	btn_cancel.top = btn_cancel.top - main_screen.top + y;
	btn_cancel.left = btn_cancel.left - main_screen.left + x;
	btn_cancel.bottom = btn_cancel.bottom - main_screen.top + y;
	btn_cancel.right = btn_cancel.right - main_screen.left + x;


	m_ucmd_list.MoveWindow(listbox);
	if (x <= 0) {
		m_ucmd_add.MoveWindow(btn_add);
		m_ucmd_drop.MoveWindow(btn_drop);
		m_ucmd_restart.MoveWindow(btn_restart);
		m_ucmd_cancel.MoveWindow(btn_cancel);
	} else {
		m_ucmd_cancel.MoveWindow(btn_cancel);
		m_ucmd_restart.MoveWindow(btn_restart);
		m_ucmd_drop.MoveWindow(btn_drop);
		m_ucmd_add.MoveWindow(btn_add);
	}

	CDialog::OnSize(nType, cx, cy);

	GetClientRect(m_main_client);
}

void CUCMDetail::OnUcmdEnd() 
{
	KillTimer(1031);
//	CDialog::OnOK();	
	m_parent->SendMessage(1030, 0, (LPARAM) m_index);
}

void CUCMDetail::OnClose() 
{
	OnUcmdEnd();
}

void CUCMDetail::OnTimer(UINT_PTR nIDEvent) 
{
	Display();
	
	CDialog::OnTimer(nIDEvent);
}

void CUCMDetail::OnUnitoolVsql() 
{
	// TODO: Add your command handler code here
	CManageRegistry* cReg = new CManageRegistry( "Visual-SQL" );
	char* sRootPath;
	char* sVersion;
	char* sBuild;
	char* sFullName;
	int   dNameSz;

	sRootPath = cReg->sGetItem( "ROOT_PATH" );
	sVersion = cReg->sGetItem( "Version" );
	sBuild = cReg->sGetItem( "Build" );

	if( !sRootPath || !sVersion || !sBuild )
	{
		AfxMessageBox( "Visual-SQL이 설치되지 않았습니다.\n설치 후에 재시도 해주십시오.", MB_OK | MB_SYSTEMMODAL | MB_ICONEXCLAMATION );

		if( sRootPath ) delete sRootPath;
		if( sVersion  ) delete sVersion;
		if( sBuild    ) delete sBuild;
		delete cReg;

		return;
	}

	dNameSz = strlen( sRootPath ) + strlen( "Visual-SQL.exe" );
	sFullName = new char[dNameSz + 1];
	memset( sFullName, 0x00, dNameSz + 1 );
	sprintf( sFullName, "%s\\Visual-SQL.exe", sRootPath );
//	system( sFullName );
	WinExec( sFullName, SW_MAXIMIZE );

	delete sRootPath;
	delete sVersion;
	delete sBuild;
	delete sFullName;
	delete cReg;

	return;
}

void CUCMDetail::OnUpdateUnitoolVsql(CCmdUI* pCmdUI) 
{
	// TODO: Add your command update UI handler code here
	
}

void CUCMDetail::OnUnitoolEmanager() 
{
	// TODO: Add your command handler code here
	CManageRegistry* cReg = new CManageRegistry( "Easy-Manager" );
	char* sRootPath;
	char* sFullName;
	int   dNameSz;

	sRootPath = cReg->sGetItem( "ROOT_PATH" );

	if( !sRootPath )
	{
		AfxMessageBox( "Easy-Manager가 설치되지 않았습니다.\n설치 후에 재시도 해주십시오.", MB_OK | MB_SYSTEMMODAL | MB_ICONEXCLAMATION );

		if( sRootPath ) delete sRootPath;
		delete cReg;

		return;
	}

	dNameSz = strlen( sRootPath ) + strlen( "Visual-SQL.exe" );
	sFullName = new char[dNameSz + 1];
	memset( sFullName, 0x00, dNameSz + 1 );
	sprintf( sFullName, "%s\\Visual-SQL.exe", sRootPath );
	system( sFullName );

	delete sRootPath;
	delete sFullName;
	delete cReg;

	return;
}

void CUCMDetail::OnUpdateUnitoolEmanager(CCmdUI* pCmdUI) 
{
	// TODO: Add your command update UI handler code here
	
}
