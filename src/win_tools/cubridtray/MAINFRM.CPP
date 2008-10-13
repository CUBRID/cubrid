// MainFrm.cpp : implementation of the CMainFrame class
//
#include "stdafx.h"
#include "resource.h"
#include "cubridtray.h"
#include "DBStartUp.h"
#include "Shutdown.h"
#include "ORDBList.h"
#include "TextProgressCtrl.h"
#include "ShowRunDB.h"
#include "UCconf.h"
#include "Monitor.h"
#include "UCProperty.h"
#include "Property.h"
#include "direct.h"
#include "env.h"
#include "Process.h"
#include "lang.h"
#include "uc_admin.h"


#include "MainFrm.h"
#include "ntray.h"


#include "ManageRegistry.h"
//#include "ParseStatus.h"
//#include "unitray_comm.h"



#include "CASManage.h"
#include "CUBRIDManage.h"
#include "Manager.h"
#include <Winsvc.h>

#define WM_SERVICE_STOP		WM_USER + 1
#define WM_SERVICE_START	WM_USER + 2

#define	WAIT_SERVER(SEC)                                 \
	do {                                                 \
		for (int i=0 ; i<SEC*2 ; i++) {                  \
			((i%2) == 0)?  m_TrayIcon.SetIcon(IDR_ING)   \
                         : m_TrayIcon.SetIcon(IDR_STOP); \
			Sleep(500);                                  \
		}                                                \
	} while(0)


#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
//
void CRedir::OnChildStarted(LPCSTR lpszCmdLine)
{
//	AfxMessageBox("start....", IDOK);
	m_errno = false;
	m_run = true;
}

void CRedir::OnChildStdOutWrite(LPCSTR lpszOutput)
{
//	AfxMessageBox(lpszOutput, IDOK);
	m_errno = true;
}

void CRedir::OnChildStdErrWrite(LPCSTR lpszOutput)
{
//		AfxMessageBox(lpszOutput, IDOK);
	m_errno = false;
}

void CRedir::OnChildTerminate()
{
//		AfxMessageBox("credir -> terminate !!", IDOK);
	m_run = false;
}

/////////////////////////////////////////////////////////////////////////////
//
CListRedirect	g_ListDB;

void CListRedirect::OnChildStarted(LPCSTR lpszCmdLine)
{
	m_data.Empty();
	m_errno = false;
	m_run = true;
}

void CListRedirect::OnChildStdOutWrite(LPCSTR lpszOutput)
{
	m_data = m_data + lpszOutput;
	m_errno = true;
}

void CListRedirect::OnChildStdErrWrite(LPCSTR lpszOutput)
{
	AfxMessageBox(lpszOutput, IDOK);
	m_errno = false;
}

void CListRedirect::OnChildTerminate()
{
	AnalysisSDB();
	m_run = false;

	SendMessage(m_dialog->m_hWnd, SPACEDB_MSG, 0, 0);
}

void CListRedirect::AnalysisSDB()
{
CStringList list;
char	*tmp;

	m_gtvol = m_gfvol = m_dtvol = m_dfvol = m_itvol = m_ifvol = m_ttvol = m_tfvol = 0;

	tmp = (char *)strtok((char *)LPCSTR(m_data), "\n");
	do {
		if (tmp != NULL) {
			list.AddTail(tmp);
			tmp = (char *)strtok(NULL, "\n");
		}
	} while (tmp != NULL);

	for (int i = 9; ; i++) {
		CString	tmpStr;
		char	*token, *tsize, *fsize;

		if (!list.FindIndex(i)) break;
		tmpStr = list.GetAt(list.FindIndex(i));
		strtok((char *)LPCSTR(tmpStr), " ");
		token = strtok(NULL, " ");
		if (!token) break;
		tsize = strtok(NULL, " ");
		fsize = strtok(NULL, " ");
		if (!strcmp(token, "GENERIC")) {
			m_gtvol = m_gtvol + atoi(tsize);
			m_gfvol = m_gfvol + atoi(fsize);
		} else if (!strcmp(token, "DATA")) {
			m_dtvol = m_dtvol + atoi(tsize);
			m_dfvol = m_dfvol + atoi(fsize);
		} else if (!strcmp(token, "INDEX")) {
			m_itvol = m_itvol + atoi(tsize);
			m_ifvol = m_ifvol + atoi(fsize);
		} else if (!strcmp(token, "TEMP")) {
			m_ttvol = m_ttvol + atoi(tsize);
			m_tfvol = m_tfvol + atoi(fsize);
		} else break;
	}	

}
/////////////////////////////////////////////////////////////////////////////
//
CServerRedirect	g_Server[MAXSERVER];

void CServerRedirect::OnChildStarted(LPCSTR lpszCmdLine)
{
	m_errno = false;
	m_run = true;
}

void CServerRedirect::OnChildStdOutWrite(LPCSTR lpszOutput)
{
	m_errno = true;
}

void CServerRedirect::OnChildStdErrWrite(LPCSTR lpszOutput)
{
	m_errno = false;
}

void CServerRedirect::OnChildTerminate()
{
	m_run = false;
}

/////////////////////////////////////////////////////////////////////////////
//
CVersionRedirect	g_CUBRIDVersion, g_UniCASVersion;

void CVersionRedirect::OnChildStarted(LPCSTR lpszCmdLine)
{
	m_version.Empty();
	m_run = true;
	m_errno = false;
}

void CVersionRedirect::OnChildStdOutWrite(LPCSTR lpszOutput)
{
	m_version = m_version + lpszOutput;
	m_errno = true;
}

void CVersionRedirect::OnChildStdErrWrite(LPCSTR lpszOutput)
{
	m_errno = false;
}

void CVersionRedirect::OnChildTerminate()
{
	m_run = false;
}

/////////////////////////////////////////////////////////////////////////////
#define	WM_TRAYNOTIFY	(WM_USER+100)
/////////////////////////////////////////////////////////////////////////////
// CMainFrame

IMPLEMENT_DYNCREATE(CMainFrame, CFrameWnd)

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
	//{{AFX_MSG_MAP(CMainFrame)
	ON_WM_CREATE()
//	ON_COMMAND(PU_STOP, OnMasterStop)
	ON_COMMAND(PU_STOP, OnStopUniTray)
	ON_COMMAND(PU_ABOUT, OnAbout)
	ON_COMMAND(PU_STARTSERVER, OnStartserver)
	ON_COMMAND(PU_STOPSERVER, OnStopserver)
	ON_UPDATE_COMMAND_UI(PU_STOPSERVER, OnUpdateStopserver)
	ON_COMMAND(PU_UNICAS_STARTUP, OnUnicasStartup)
	ON_COMMAND(PU_UNICAS_SHUTDOWN, OnUnicasShutdown)
	ON_UPDATE_COMMAND_UI(PU_UNICAS_STARTUP, OnUpdateUnicasStartup)
	ON_UPDATE_COMMAND_UI(PU_UNICAS_SHUTDOWN, OnUpdateUnicasShutdown)
	ON_UPDATE_COMMAND_UI(PU_STARTSERVER, OnUpdateStartserver)
	ON_COMMAND(PU_PROPERTY, OnProperty)
	ON_UPDATE_COMMAND_UI(PU_LISTSERVER, OnUpdateListserver)
	ON_COMMAND(PU_UNICAS_STARTNAMED, OnUnicasStartNamed)
	ON_COMMAND(PU_CREATEDB, OnCreateDB)
	ON_COMMAND(PU_LISTRUNDB, OnListserver)
	ON_COMMAND(PU_UNICASPRO, OnUnicasProperty)
	ON_COMMAND(PU_UNICAS_MONITOR, OnUnicasMonitor)
	ON_COMMAND(PU_UNICAS_RESTARTUP, OnUnicasRestartup)
	ON_UPDATE_COMMAND_UI(PU_UNICAS_RESTARTUP, OnUpdateUnicasRestartup)
	ON_WM_TIMER()
	ON_UPDATE_COMMAND_UI(PU_UNICAS_MONITOR, OnUpdateUnicasMonitor)
	ON_COMMAND(PU_UNITOOL_EMANAGER, OnUnitoolEmanager)
	ON_COMMAND(PU_UNITOOL_VSQL, OnUnitoolVsql)
	ON_UPDATE_COMMAND_UI(PU_UNITOOL_VSQL, OnUpdateUnitoolVsql)
	ON_UPDATE_COMMAND_UI(PU_UNITOOL_EMANAGER, OnUpdateUnitoolEmanager)
	ON_WM_SETFOCUS()
	ON_COMMAND(PU_EASY_STOP, OnEasyStop)
	ON_UPDATE_COMMAND_UI(PU_EASY_STOP, OnUpdateEasyStop)
	ON_COMMAND(PU_EASY_STARTUP, OnEasyStartup)
	ON_UPDATE_COMMAND_UI(PU_EASY_STARTUP, OnUpdateEasyStartup)
	ON_COMMAND(PU_EASY_OPTION, OnEasyOption)
	ON_UPDATE_COMMAND_UI(PU_EASY_OPTION, OnUpdateEasyOption)
	ON_COMMAND(ID_EMSAUTOSTARTLOG, OnEmsautostartlog)
	ON_UPDATE_COMMAND_UI(ID_EMSAUTOSTARTLOG, OnUpdateEmsautostartlog)
	ON_COMMAND(ID_EMSJSSTARTLOG, OnEmsjsstartlog)
	ON_UPDATE_COMMAND_UI(ID_EMSJSSTARTLOG, OnUpdateEmsjsstartlog)
	ON_UPDATE_COMMAND_UI(PU_LISTRUNDB, OnUpdateListserver)
	ON_WM_ACTIVATE()
	ON_WM_ACTIVATEAPP()
	ON_WM_INITMENU()
	ON_WM_INITMENUPOPUP()
	ON_WM_SETCURSOR()
	ON_COMMAND(PU_UNICAS_RESTARTUP, OnUnicasRestart)
	ON_UPDATE_COMMAND_UI(PU_UNICAS_RESTARTUP, OnUpdateUnicasRestart)
	//}}AFX_MSG_MAP
	ON_MESSAGE(WM_STOPMONITOR, OnStopMonitor)
	ON_MESSAGE(WM_TRAYNOTIFY, OnTrayNotification)
	ON_MESSAGE(WM_SERVICE_STOP, onReceiveServiceStopMessage)
	ON_MESSAGE(WM_SERVICE_START, onReceiveServiceStartMessage)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// global variable
CLang	theLang;
CEnv	theEnv;

/////////////////////////////////////////////////////////////////////////////
// CMainFrame construction/destruction

CMainFrame::CMainFrame()
{
	m_process = new CProcess();
	m_AboutDlg = NULL;	
	m_Master   = NULL;
	m_ShowRunDB = NULL;
	m_Property = NULL;
#if 0
	m_ucmonitor = NULL;
#endif
	m_ucproperty = NULL;
	m_OrdbList = NULL;
	m_unicasconf = NULL;
	cCUBRID = new CCUBRIDManage();
	cUniCAS = new CUniCASManage();
	cTool   = new CUniToolManage();
	cEasy   = new CEasyManager();

	for (int i = 0; i < MAXSERVER; i++) {
		if (!g_Server[i].m_DBName.IsEmpty()) g_Server[i].m_DBName.Empty();
	}
	m_ServerCnt = 0;
	m_StatusOfUniCAS = false;

	bEasyStatus = false;
	bVsqlStatus = false;
	bEasyServerStatus = false;
	bUniCasStartStatus = false;
	bUniCasStopStatus = false;
	bUniCasRestartStatus = false;
	bCUBRIDStatus = false;
	bCUBRIDStartStatus = false;
	bCUBRIDStopStatus = false;
	bEasyServerStartStatus = false;
	bEasyServerStopStatus = false;
	m_refreshTimer = 10;
	m_monitorTimer = 20;
}

CMainFrame::~CMainFrame()
{
	if (m_process) {
		delete m_process;
		m_process = NULL;
	}

	if (m_AboutDlg) {
		delete m_AboutDlg;
		m_AboutDlg = NULL;
	}

	if (m_ShowRunDB) {
		delete m_ShowRunDB;
		m_ShowRunDB = NULL;
	}
	if (m_Property) {
		delete m_Property;
		m_Property = NULL;
	}
#if 0
	if (m_ucmonitor) {
		m_ucmonitor->DestroyWindow();
		delete m_ucmonitor;
		m_ucmonitor = NULL;
	}
#endif
	if (m_ucproperty) {
		delete m_ucproperty;
		m_ucproperty = NULL;
	}
	if (m_OrdbList) {
		delete m_OrdbList;
		m_OrdbList = NULL;
	}
	if (m_unicasconf) {
		delete m_unicasconf;
		m_unicasconf = NULL;
	}

	if (m_Master) {
		StopMaster();
		delete m_Master;
		m_Master = NULL;
	}

	if( cCUBRID ) {
		delete cCUBRID;
		cCUBRID = NULL;
	}
	if( cUniCAS) {
		delete cUniCAS;
		cUniCAS = NULL;
	}
	if( cTool ) {
		delete cTool;
		cTool = NULL;
	}
}

BOOL CMainFrame::PreCreateWindow(CREATESTRUCT& cs)
{

	if( !CFrameWnd::PreCreateWindow(cs) )
		return FALSE;

	cs.lpszClass = _T("cubrid_tray");

	return TRUE;
}

int CMainFrame::OnCreate(LPCREATESTRUCT lpCreateStruct) 
{
	if (CFrameWnd::OnCreate(lpCreateStruct) == -1)
		return -1;

	if (!theEnv.CheckOS()) {
		AfxMessageBox(theEnv.GetErrMsg(), MB_OK | MB_ICONSTOP);
		return -1;
	}


	if (!theEnv.GetEnviornment()) {
		AfxMessageBox(theEnv.GetErrMsg(), MB_OK | MB_ICONSTOP);
		return -1;
	} 

	if (!m_TrayIcon.Create(this, theLang.GetMenu(), "CUBRID Service Tray", AfxGetApp()->LoadIcon(IDR_STOP), WM_TRAYNOTIFY)) {
		int fail = 1;

		CString msg = "CUBRID Service Tray를 등록하는데 실패했습니다. 다시 시도하시겠습니까?";
		if (AfxMessageBox(msg, MB_YESNO | MB_ICONSTOP) == IDYES) {
			if(m_TrayIcon.Create(this, theLang.GetMenu(), "CUBRID Service Tray", AfxGetApp()->LoadIcon(IDR_STOP), WM_TRAYNOTIFY)) {
				fail = 0;
			}
			msg = "CUBRID Service Tray를 등록하는데 실패했습니다. 제어판-관리도구-서비스에서 CUBRIDService 동작 상태를 점검하시기 바랍니다.";
		}
		else {
			msg = "제어판-관리도구-서비스에서 CUBRIDService 동작 상태를 점검하시기 바랍니다.";
		}

		if (fail == 1){
			AfxMessageBox(msg, MB_OK | MB_ICONSTOP);
			return -1;
		}
	}

	m_TrayIcon.SetIcon( IDR_STOP );

	WAIT_SERVER(5);
	if (!bCUBRIDServiceCheck())
		m_TrayIcon.SetIcon( IDR_STOP );
	else
		m_TrayIcon.SetIcon( IDR_ING );

	if (m_TrayIcon.bSetIconFailed == TRUE) {
		refreshIcon();
	}

	SetTimer(m_monitorTimer, 1000, NULL);
	return 0;
}

#include "ToolManage.h"

void CMainFrame::OnTimer(UINT nIDEvent) 
{
	if (nIDEvent == m_refreshTimer) {
		if (!bCUBRIDServiceCheck())
			m_TrayIcon.SetIcon( IDR_STOP );
		else
			m_TrayIcon.SetIcon( IDR_ING );

		if (m_TrayIcon.bSetIconFailed == TRUE) {
			SetTimer(m_refreshTimer, 15000, NULL);
		}
		else {
			KillTimer(m_refreshTimer);
		}

		return;
	}
	else if (nIDEvent == m_monitorTimer) {
		if (!bCUBRIDServiceCheck())
			m_TrayIcon.SetIcon( IDR_STOP );
		else
			m_TrayIcon.SetIcon( IDR_ING );

		SetTimer(m_monitorTimer, 1000, NULL);
	}

	CFrameWnd::OnTimer(nIDEvent);
}


void CMainFrame::vExchangeUniCASStatus( bool bStartStatus )
{
	bUniCasStartStatus   = bStartStatus;
	bUniCasStopStatus    = !bStartStatus;
	bUniCasRestartStatus = !bStartStatus;
}



LRESULT CMainFrame::OnTrayNotification(WPARAM wParam, LPARAM lParam)
{
	return m_TrayIcon.OnTrayNotification(wParam, lParam);
}

LRESULT CMainFrame::onReceiveServiceStopMessage(WPARAM wParam, LPARAM lParam)
{
/*	WAIT_SERVER(5);
	if (!bCUBRIDServiceCheck()) {
		bEasyServerStopStatus = true;
		bEasyServerStartStatus = false;
		m_TrayIcon.SetIcon(IDR_STOP);
	}
	else {
		bEasyServerStopStatus = false;
		bEasyServerStartStatus = true;
		m_TrayIcon.SetIcon(IDR_ING);
	}

	if (m_TrayIcon.bSetIconFailed == TRUE) {
		refreshIcon();
	}
*/
	return 0;
}

LRESULT CMainFrame::onReceiveServiceStartMessage(WPARAM wParam, LPARAM lParam)
{
/*	WAIT_SERVER(5);
	if (!bCUBRIDServiceCheck()) {
		bEasyServerStopStatus = false;
		bEasyServerStartStatus = true;
		m_TrayIcon.SetIcon(IDR_STOP);
	}
	else {
		bEasyServerStopStatus = false;
		bEasyServerStartStatus = true;
		m_TrayIcon.SetIcon(IDR_ING);
	}

	if (m_TrayIcon.bSetIconFailed == TRUE) {
		refreshIcon();
	}
*/
	return 0;
}

void CMainFrame::SetTooltipText()
{
int		i;
CString	tooltip, tempstr;

	tooltip.Empty();
	for (i = 0; i < MAXSERVER; i++) {
		if (!g_Server[i].m_DBName.IsEmpty()) {
			if (!tooltip.IsEmpty()) tooltip = tooltip + "\n";
			tooltip = tooltip + g_Server[i].m_DBName;
		}
	}

	if (tooltip.IsEmpty()) {
		tooltip = theLang.GetMessage(MSG_MAIN_TITLE);
	} else {
		tempstr = theLang.GetMessage(MSG_MAIN_TITLE);
		tempstr = tempstr + "\n===================" + tooltip;
		tooltip = tempstr;
	}

	m_TrayIcon.SetTooltipText(LPCSTR(tooltip));
}


///////////////////////////////////////////////////////////////////////////////////////
// About Box
//
void CMainFrame::OnAbout() 
{
	if (!m_AboutDlg) {
		m_AboutDlg = new CAboutDlg();

		m_AboutDlg->SetVersion();
		m_AboutDlg->DoModal();
		delete m_AboutDlg;
		m_AboutDlg = NULL;
	}
}





///////////////////////////////////////////////////////////////////////////////////////
// CUBRID Server 관련
// 
#include "StartTargetDB.h"


void CMainFrame::OnStartserver() 
{
	cCUBRID->pGetStopDBList();

	CStartTargetDB* cStart = new CStartTargetDB( cCUBRID );

	cStart->DoModal();

	delete cStart;

	m_TrayIcon.SetIcon(IDR_STOP);

	return;
}

#include "StopTargetDB.h"

void CMainFrame::OnStopserver() 
{
	cCUBRID->bCheckServer();

	CStopTargetDB* cStop = new CStopTargetDB( cCUBRID );

	cStop->DoModal();

	delete cStop;

	m_TrayIcon.SetIcon(IDR_STOP);

	return;
}


///////////////////////////////////////////////////////////////////////////////////////
// UniCAS 관련 기능들~~~~
//
#include "CASManage.h"

void CMainFrame::OnUnicasStartup() 
{
	bool buniCas = cUniCAS->bCheckUniCAS();

	if( buniCas ) 
	{
		// 2002년 10월 19일 By KingCH
		// UniCAS가 설치되어 있지 않는데, 설치되었다는 것으로 인식
//		vExchangeUniCASStatus( true );
		vExchangeUniCASStatus( false );
		return;
	}

	if( !cUniCAS->bStartUniCAS() )
	{
//		AfxMessageBox(theLang.GetMessage(MSG_START_UNICAS_ERROR), MB_OK | MB_ICONSTOP);

		// 2002년 10월 19일 By KingCH
		// UniCAS가 설치되어 있지 않는데, 설치되었다는 것으로 인식
//		vExchangeUniCASStatus( false );
		vExchangeUniCASStatus( true );
		return;
	}

	// 2002년 10월 19일 By KingCH
	// UniCAS가 설치되어 있지 않는데, 설치되었다는 것으로 인식
//	vExchangeUniCASStatus( true );
	vExchangeUniCASStatus( false );

//	AfxMessageBox(theLang.GetMessage(MSG_START_UNICAS), MB_OK | MB_ICONINFORMATION);
	return;
}


void CMainFrame::OnUnicasRestartup() 
{
	if( cUniCAS->bCheckUniCAS() ) cUniCAS->bStopUniCAS();
	if( cUniCAS->bStartUniCAS() ) 
//	if( !cUniCAS->bStartUniCAS() )
	{
		AfxMessageBox(theLang.GetMessage(MSG_RESTART_UNICAS_ERROR), MB_OK | MB_ICONSTOP);
		// 2002년 10월 19일 By KingCH
		// UniCAS가 설치되어 있지 않는데, 설치되었다는 것으로 인식
//		vExchangeUniCASStatus( false );
		vExchangeUniCASStatus( true );
		return;
	}

	// 2002년 10월 19일 By KingCH
	// UniCAS가 설치되어 있지 않는데, 설치되었다는 것으로 인식
//	vExchangeUniCASStatus( true );
	vExchangeUniCASStatus( false );

	AfxMessageBox(theLang.GetMessage(MSG_RESTART_UNICAS), MB_OK | MB_ICONINFORMATION);
	return;
}



void CMainFrame::OnUnicasShutdown() 
{
	bool buniCas = cUniCAS->bCheckUniCAS();

	// UniCAS가 수행 중이 아님.
	if( !buniCas )
	{
		// 2002년 10월 19일 By KingCH
		// UniCAS가 설치되어 있지 않는데, 설치되었다는 것으로 인식
//		vExchangeUniCASStatus( false );
		vExchangeUniCASStatus( true );
		return;
	}

	if( !cUniCAS->bStopUniCAS() )
	{
//		AfxMessageBox(theLang.GetMessage(MSG_STOP_UNICAS_ERROR), MB_OK | MB_ICONSTOP);
		// 2002년 10월 19일 By KingCH
		// UniCAS가 설치되어 있지 않는데, 설치되었다는 것으로 인식
//		vExchangeUniCASStatus( cUniCAS->bCheckUniCAS() );
		vExchangeUniCASStatus( !cUniCAS->bCheckUniCAS() );
		return;
	}

	// 2002년 10월 19일 By KingCH
	// UniCAS가 설치되어 있지 않는데, 설치되었다는 것으로 인식
//	vExchangeUniCASStatus( false );
	vExchangeUniCASStatus( true );

//	AfxMessageBox(theLang.GetMessage(MSG_STOP_UNICAS), MB_OK | MB_ICONINFORMATION);
	return;

}


///////////////////////////////////////////////////////////////////////////////////////
// 메뉴의 상태를 결정한다.
//

// CUBRID 관련
void CMainFrame::OnUpdateStartserver(CCmdUI* pCmdUI) 
{
	TRACE1( "bCUBRIDStartStatus : %s\n", bCUBRIDStartStatus?"TURE":"FALSE" );

	if (bCUBRIDStartStatus) pCmdUI->Enable(true);
	else					pCmdUI->Enable(false);
}

void CMainFrame::OnUpdateStopserver(CCmdUI* pCmdUI) 
{
	TRACE1( "bCUBRIDStopStatus : %s\n", bCUBRIDStopStatus?"TURE":"FALSE" );

	if (bCUBRIDStopStatus) pCmdUI->Enable(true);
	else				   pCmdUI->Enable(false);
}




// UniCAS 관련
void CMainFrame::OnUpdateUnicasStartup(CCmdUI* pCmdUI) 
{
	TRACE1( "bUniCasStartStatus : %s\n", bUniCasStartStatus?"TURE":"FALSE" );

	if (bUniCasStartStatus)
		pCmdUI->Enable(true);
	else
		pCmdUI->Enable(false);
}

void CMainFrame::OnUpdateUnicasRestartup(CCmdUI* pCmdUI) 
{
	TRACE1( "bUniCasRestartStatus : %s\n", bUniCasRestartStatus?"TURE":"FALSE" );

	if (bUniCasRestartStatus) pCmdUI->Enable(false);
	else					  pCmdUI->Enable(true);
}

void CMainFrame::OnUpdateUnicasShutdown(CCmdUI* pCmdUI) 
{
	TRACE1( "bUniCasStopStatus : %s\n", bUniCasStopStatus?"TURE":"FALSE" );

	if (bUniCasStopStatus)
		pCmdUI->Enable(true);	
	else
		pCmdUI->Enable(false);
}

bool CMainFrame::CheckEnv()
{
	return (theEnv.GetEnviornment());
	return true;
}

#include "ToolManage.h"

void CMainFrame::OnUnitoolEmanager() 
{
	cTool->bStartEasyManage();
	return;
}

void CMainFrame::OnUnitoolVsql() 
{
	cTool->bStartVSQL();
	return;
}

void CMainFrame::OnUpdateUnitoolVsql(CCmdUI* pCmdUI) 
{
	if (bVsqlStatus) pCmdUI->Enable(true);
	else			 pCmdUI->Enable(false);	
}

void CMainFrame::OnUpdateUnitoolEmanager(CCmdUI* pCmdUI) 
{
	if (bEasyStatus) pCmdUI->Enable(true);
	else			 pCmdUI->Enable(false);	
}




///////////////////////////////////////////////////////////////////////////////////////
//  CUBRID 제품군의 현재 상태를 Check
//
void CMainFrame::OnSetFocus(CWnd* pOldWnd) 
{
	CFrameWnd::OnSetFocus(pOldWnd);

	// TODO: Add your message handler code here

	// UniTool 관련 Program의 상태를 Check한다.
	bEasyStatus = cTool->bCheckInstallEasyManage();
	bVsqlStatus = cTool->bCheckInstallVSQL();

// 2003.12.09 Service Program으로 대체
/*
	// UniCAS의 설치 여부를 판단한다.
	if( cUniCAS->bInstallStatus() )
	{
		// 2002년 10월 19일 By KingCH
		// UniCAS가 설치되어 있지 않는데, 설치되었다는 것으로 인식
		// UniCAS의 현재의 상황을 판단한다.
//		if( cUniCAS->bCheckUniCAS() ) vExchangeUniCASStatus( true );
//		else						  vExchangeUniCASStatus( false );
		if( cUniCAS->bCheckUniCAS() ) vExchangeUniCASStatus( false );
		else						  vExchangeUniCASStatus( true );
	}
	else
	{
		// 2002년 10월 19일 By KingCH
		// UniCAS가 설치되어 있지 않는데, 설치되었다는 것으로 인식
		bUniCasStartStatus = false;
		bUniCasStopStatus = false;
		bUniCasRestartStatus = false;
	}
*/

/*
	// CUBRID의 현재의 상황을 판단한다.
	// CUBRID의 설치 여부를 판단한다.
	if( cCUBRID->bInstallStatus() )
	{
		bCUBRIDStartStatus = true;
		bCUBRIDStopStatus  = true;
	}
	else
	{
		bCUBRIDStartStatus = false;
		bCUBRIDStopStatus = false;
	}
*/

	if( cEasy->bInstallStatus() )
	{
		if( bCUBRIDServiceCheck() )
		{
			bEasyServerStopStatus = true;
			bEasyServerStartStatus = false;
			m_TrayIcon.SetIcon( IDR_ING );
		}
		else
		{
			bEasyServerStopStatus = false;
			bEasyServerStartStatus = true;
			m_TrayIcon.SetIcon( IDR_STOP );
		}
	}
	else
	{
		bEasyServerStartStatus = false;
		bEasyServerStopStatus  = false;
	}
}

// EasyManager Server와 관련
void CMainFrame::OnEasyStop() 
{
	// TODO: Add your command handler code here
	if (AfxMessageBox(theLang.GetMessage(MSG_STOP_SERVICE), MB_YESNO | MB_ICONQUESTION) == IDYES) {
		cEasy->bStopEasyManagerServer();
		WAIT_SERVER(5);
	}
}

void CMainFrame::OnUpdateEasyStop(CCmdUI* pCmdUI) 
{
	// TODO: Add your command update UI handler code here
	if( bEasyServerStopStatus )
		pCmdUI->Enable( true );
	else
		pCmdUI->Enable( false );

	return;
}

void CMainFrame::OnEasyStartup() 
{
	cEasy->bStartEasyManagerServer();
	WAIT_SERVER(5);
}

void CMainFrame::OnUpdateEasyStartup(CCmdUI* pCmdUI) 
{
	// TODO: Add your command update UI handler code here
	if( bEasyServerStartStatus )
		pCmdUI->Enable( true );
	else
		pCmdUI->Enable( false );

	return;
}

#include "OptionDialog.h"

void CMainFrame::OnEasyOption() 
{
	// TODO: Add your command handler code here
//	COptionDialog*	dlg = new COptionDialog();
//	dlg->DoModal();
//	delete dlg;
	return;
}

void CMainFrame::OnUpdateEasyOption(CCmdUI* pCmdUI) 
{
	// TODO: Add your command update UI handler code here
	return;
}

void CMainFrame::OnEmsautostartlog() 
{
	// TODO: Add your command handler code here
//	cEasy->EmsAutoStartLog();
	return;
}

void CMainFrame::OnUpdateEmsautostartlog(CCmdUI* pCmdUI) 
{
	// TODO: Add your command update UI handler code here
	return;
}

void CMainFrame::OnEmsjsstartlog() 
{
	// TODO: Add your command handler code here
//	cEasy->EmsjsStartLog();
	return;
}

void CMainFrame::OnUpdateEmsjsstartlog(CCmdUI* pCmdUI) 
{
	// TODO: Add your command update UI handler code here
	return;
}

void CMainFrame::OnStopUniTray() 
{
	if (AfxMessageBox(theLang.GetMessage(MSG_EXIT), MB_YESNO | MB_ICONQUESTION) == IDYES) {
		cEasy->bStopEasyManagerServer();
		DestroyWindow();
	}

	return;
}

///////////////////////////////////////////////////////////////////////////////////////
//  UniCAS 관련 
//
bool CMainFrame::StartUniCAS()
{
#ifdef UNICAS_ON
int	rc;
	
	if (m_StatusOfUniCAS) return true;

	rc = uc_start();
	if (rc == 0) {
		m_StatusOfUniCAS = true;

		return true;
	} else {
		m_StatusOfUniCAS = false;

		return false;
	}
#else
	return false;
#endif
}

bool CMainFrame::StopUniCAS()
{
#ifdef UNICAS_ON
int	rc;
char	err_msg[1024];

	if (!m_StatusOfUniCAS) return true;

	rc = uc_stop(err_msg);
	if (rc == 0) {
		m_StatusOfUniCAS = false;

		return true;
	} else {
		m_StatusOfUniCAS = true;

		return false;
	}

	return true;
#else
	return false;
#endif
}

void CMainFrame::KillUniCAS()
{
#ifdef UNICAS_ON
int		rc;
char	err_msg[1024];

	rc = uc_stop(err_msg);
#endif
}




///////////////////////////////////////////////////////////////////////////////////////
//  Vision3 관련
//
void CMainFrame::StartNamed()
{
char	namedir[100];
int		error;

	sprintf(namedir, "%s\\bin", theEnv.GetUniCAS());
	_chdir(namedir);
	error = WinExec(CMD_NAMED, SW_SHOWDEFAULT);
}

void CMainFrame::StopNamed()
{
CString	process_name;
DWORD	process_id;

	process_name.Format("%s\\bin\\%s", theEnv.GetUniCAS(), CMD_NAMED);
	process_name.MakeLower();
	if ((process_id = m_process->FindProcess(process_name)) != 0) {
		m_process->KillProcess(process_id);
	}
}

///////////////////////////////////////////////////////////////////////////////////////
//  Version 관련
//
CString CMainFrame::GetCUBRIDVersion()
{
char	versioncmd[100];
CString	version;

	version.Empty();
	sprintf(versioncmd, "%s\\%s", theEnv.GetCUBRID(), CMD_CUBRID_RELEASE);
	if (!g_CUBRIDVersion.StartChildProcess(versioncmd)) {
		return theLang.GetMessage(MSG_UNKNOWN_CUBRID);
	} 

//	while(g_CUBRIDVersion.m_run) Sleep(50);
	Sleep(500);

	char *temp;
	strtok((char *)LPCSTR(g_CUBRIDVersion.m_version), " ");
	strtok(NULL, " ");
	strtok(NULL, " ");
	temp = strtok(NULL, " ");
	version = temp;
	temp = strtok(NULL, " ");
	if (temp && !strcmp(temp, "Patch")) {
		strtok(NULL, " ");
		temp = strtok(NULL, " ");
		version = version + " patch " + temp;
	}

	return version;
}

CString CMainFrame::GetUniCASVersion()
{
	char	versioncmd[100];
	CString	version;

	version.Empty();
	sprintf(versioncmd, "%s\\%s", theEnv.GetUniCAS(), CMD_UNICAS_RELEASE);
	if (!g_UniCASVersion.StartChildProcess(versioncmd)) {
		return theLang.GetMessage(MSG_UNKNOWN_UNICAS);
	} 

//	while(g_UniCASVersion.m_run) Sleep(50);
	Sleep(500);

	CString temp;
	strtok((char *)LPCSTR(g_UniCASVersion.m_version), " ");
	temp = strtok(NULL, "`");
	temp.Replace("\r\n", " ");
	version = temp;

	return version;
}


///////////////////////////////////////////////////////////////////////////////////////
// CUBRID Server 관련( DB )
//
bool CMainFrame::StartServer(char *dbname)
{
char	serverpath[100];
int		i;

	sprintf(serverpath, "%s\\%s %s", theEnv.GetCUBRID(), CMD_SERVER, dbname);
		
	for (i = 0; i < MAXSERVER; i++) {
			if (g_Server[i].m_DBName.IsEmpty()) break;
	}
			
	if (i < MAXSERVER) {
		if (!g_Server[i].StartChildProcess(serverpath)) {
			return false;
		} else {
			g_Server[i].m_DBName.Format("%s", dbname); 
			m_ServerCnt++;
		}
	} else {
		return false;
	}

	return true;
}

bool CMainFrame::StopServer(char *dbname)
{
char	commdbpath[100];

	sprintf(commdbpath, "%s\\%s %s", theEnv.GetCUBRID(), CMD_SHUTDOWN_SERVER, dbname);

	CRedir	shutdown;
	shutdown.StartChildProcess(commdbpath);
	for (int i = 0; i < MAXSERVER; i++) {
		if (g_Server[i].m_DBName.IsEmpty()) continue;
		if (!g_Server[i].m_DBName.Compare(dbname)) {
			g_Server[i].m_DBName.Empty();
		}
	}

	m_ServerCnt--;
	return true;
}


bool CMainFrame::StopServer(int index)
{
char	commdbpath[100];

	sprintf(commdbpath, "%s\\%s %s", theEnv.GetCUBRID(), CMD_SHUTDOWN_SERVER, LPCSTR(g_Server[index].m_DBName));

	CRedir	shutdown;
	shutdown.StartChildProcess(commdbpath);
	g_Server[index].m_DBName.Empty();
	m_ServerCnt--;

	return true;
}

void CMainFrame::KillServer()
{
CString	process_name;
DWORD	process_id;

	process_name.Format("%s\\%s", theEnv.GetCUBRID(), CMD_SERVER);
	process_name.MakeLower();
	while ((process_id = m_process->FindProcess(process_name)) != 0) {
		m_process->KillProcess(process_id);
	}
}


///////////////////////////////////////////////////////////////////////////////////////
// Master 관련
//
bool CMainFrame::StartMaster()
{
	char	masterpath[100];

	sprintf(masterpath, "%s\\%s", theEnv.GetCUBRID(), CMD_MASTER);

	m_Master = new CRedir();
	if (!m_Master->StartChildProcess(masterpath)) {
		OnMasterStop();
		return false;
	}

	Sleep(500);

	if (!m_Master->IsChildRunning()) {
//		OnMasterStop();
		return false;
	}

	return true;
}

bool CMainFrame::StopMaster()
{
	if (m_Master->IsChildRunning()) {
		VERIFY(::TerminateProcess(m_Master->m_hChildProcess, 1));
	}
	m_Master->TerminateChildProcess();

	return true;
}

void CMainFrame::KillMaster()
{
CString	process_name;
DWORD	process_id;
CString	tmpStr;

	process_name.Format("%s\\%s", theEnv.GetCUBRID(), CMD_MASTER);
	process_name.MakeLower();

	if ((process_id = m_process->FindProcess(process_name)) != 0) {
		m_process->KillProcess(process_id);
	}
}



void CMainFrame::OnUpdateUnicasMonitor(CCmdUI* pCmdUI) 
{
	if (m_StatusOfUniCAS) pCmdUI->Enable(true);
	else				  pCmdUI->Enable(false);
}

void CMainFrame::OnUpdateListserver(CCmdUI* pCmdUI) 
{
	if (m_ServerCnt == 0) pCmdUI->Enable(false);
	else				  pCmdUI->Enable(true);
}




///////////////////////////////////////////////////////////////////////////////////////
// 요 기능은 내가 확!!!! 없애 버려쮜~~~~~~~
//
void CMainFrame::OnUnicasProperty() 
{
CUCConf	*unicasconf;

	if (!m_ucproperty) {
		unicasconf = new CUCConf();
		unicasconf->Read();

		m_ucproperty = new CUCProperty();
		m_ucproperty->SetUnicasConf(unicasconf);
		if (m_ucproperty->DoModal() == IDOK) {
			unicasconf->Write();
		}
		delete m_ucproperty;
		m_ucproperty = NULL;

		delete unicasconf;
	}
}



///////////////////////////////////////////////////////////////////////////////////////
// 요 기능은 내가 확!!!! 없애 버려쮜~~~~~~~
//
void CMainFrame::OnUnicasStartNamed() 
{
	StartNamed();
}

///////////////////////////////////////////////////////////////////////////////////////
// 요 기능은 내가 확!!!! 없애 버려쮜~~~~~~~
//
void CMainFrame::OnUnicasMonitor() 
{
#if 0
	if (!m_ucmonitor) {
		m_ucmonitor = new CMonitor();
		m_ucmonitor->SetUnicasConf(m_unicasconf, this);
		m_ucmonitor->Create(CMonitor::IDD);
		m_ucmonitor->ShowWindow(SW_SHOW);
	}
#endif
}

///////////////////////////////////////////////////////////////////////////////////////
// 요 기능은 내가 확!!!! 없애 버려쮜~~~~~~~
//
LRESULT CMainFrame::OnStopMonitor(WPARAM wParam, LPARAM lParam)
{
#if 0
	if (m_ucmonitor) {
		m_ucmonitor->DestroyWindow();
		delete m_ucmonitor;
		m_ucmonitor = NULL;
	}
#endif
	return 0;
}


///////////////////////////////////////////////////////////////////////////////////////
// 요 기능은 내가 확!!!! 없애 버려쮜~~~~~~~
//
void CMainFrame::OnProperty() 
{
	if (m_OrdbList->m_List.GetCount() == 0) return;

	if (!m_Property) {
		m_Property = new CProperty(m_OrdbList);

		m_OrdbList->ReReadDBInfo();
		if (m_Property->DoModal() == IDOK) {
			m_OrdbList->WriteDBInfo();
		}
	}

	delete m_Property;
	m_Property = NULL;
}

///////////////////////////////////////////////////////////////////////////////////////
// CUBRID의 Start 또는 Stop을 눌렀을 경우 표시되는 부분
//
void CMainFrame::OnListserver() 
{
#if 0
	if (!m_ShowRunDB) {
		m_ShowRunDB = new CShowRunDB();

		m_ShowRunDB->DoModal();
		delete m_ShowRunDB;
		m_ShowRunDB = NULL;
	}
#endif
}

void CMainFrame::OnCreateDB() 
{
char	makedb[100];

	sprintf(makedb, "%s\\%s",theEnv.GetCUBRID(), CMD_MAKEDB);
    WinExec(makedb, SW_SHOWDEFAULT);
}



void CMainFrame::OnMasterStop() 
{
	if (AfxMessageBox(theLang.GetMessage(MSG_EXIT), MB_YESNO | MB_ICONQUESTION) == IDYES) {
		// kill server process
		if (m_StatusOfUniCAS) {
			StopUniCAS();
		}
		StopNamed();

		for (int i = 0; i < MAXSERVER; i++) {
			if (!g_Server[i].m_DBName.IsEmpty()) {
				StopServer(i);
			}
		}
		// kill master process
		if (m_Master) {
			StopMaster();
			delete m_Master;
			m_Master = NULL;
		}
		DestroyWindow();
	}
}

void CMainFrame::OnUnicasRestart() 
{
	// TODO: Add your command handler code here
	
}

void CMainFrame::OnUpdateUnicasRestart(CCmdUI* pCmdUI) 
{
	// TODO: Add your command update UI handler code here
	
}

BOOL CMainFrame::refreshIcon()
{
	return SetTimer(m_refreshTimer, 15000, NULL);	
}

bool CMainFrame::bCUBRIDServiceCheck()
{
    SERVICE_STATUS ssStatus; 
	bool ret_val;
	SC_HANDLE hCubridService;

	SC_HANDLE scmHandle = OpenSCManager ( NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE );

	if (scmHandle == NULL) {
		return false;
	}
    hCubridService = OpenService( 
        scmHandle,             // SCM database 
        "CUBRIDService",          // service name
        SERVICE_QUERY_STATUS); 
 
    if (hCubridService == NULL) { 
		return false;
    }
 
    if (!QueryServiceStatus(hCubridService, &ssStatus)) {
        return false;
    }

    if (ssStatus.dwCurrentState == SERVICE_RUNNING) ret_val = true;
	else ret_val = false;

    CloseServiceHandle(hCubridService); 
	CloseServiceHandle(scmHandle); 

	return ret_val;
}
