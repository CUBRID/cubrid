/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

// unitray.cpp : Defines the class behaviors for the application.
//

#include "stdafx.h"
#include "cubridtray.h"

#include "MainFrm.h"
#include "unitrayDoc.h"
#include "unitrayView.h"
#include "lang.h"
#include "version.h"

#include <sys/types.h>
#include <sys/stat.h>


#include "ManageRegistry.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CUnitrayApp

BEGIN_MESSAGE_MAP(CUnitrayApp, CWinApp)
	//{{AFX_MSG_MAP(CUnitrayApp)
	ON_COMMAND(ID_APP_ABOUT, OnAppAbout)
		// NOTE - the ClassWizard will add and remove mapping macros here.
		//    DO NOT EDIT what you see in these blocks of generated code!
	//}}AFX_MSG_MAP
	// Standard file based document commands
	ON_COMMAND(ID_FILE_NEW, CWinApp::OnFileNew)
	ON_COMMAND(ID_FILE_OPEN, CWinApp::OnFileOpen)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CUnitrayApp construction

CUnitrayApp::CUnitrayApp()
{

}

CUnitrayApp::~CUnitrayApp()
{
}
/////////////////////////////////////////////////////////////////////////////
// The one and only CUnitrayApp object

CUnitrayApp theApp;

// This identifier was generated to be statistically unique for your app.
// You may change it if you prefer to choose a specific identifier.

// {56E67EB6-4694-4EF6-B8CC-01F4BF58ABBA}
static const CLSID clsid =
{ 0x56e67eb6, 0x4694, 0x4ef6, { 0xb8, 0xcc, 0x1, 0xf4, 0xbf, 0x58, 0xab, 0xba } };


/////////////////////////////////////////////////////////////////////////////
// CUnitrayApp initialization

BOOL CUnitrayApp::FirstInstance()
{
	CWnd *pWndPrev, *pWndChild ;

	if (pWndPrev = CWnd::FindWindow (_T("cubrid_tray"), NULL))
	{
		pWndChild = pWndPrev->GetLastActivePopup () ;

		pWndChild->SetForegroundWindow () ;

		return FALSE ;
	}
	else
		return TRUE ;
}


BOOL CUnitrayApp::InitInstance()
{
	// Initialize OLE libraries
	if (!AfxOleInit())
	{
		AfxMessageBox(IDP_OLE_INIT_FAILED);
		return FALSE;
	}

	AfxEnableControlContainer();

	if (!FirstInstance ())	return FALSE ;

	WNDCLASS wndcls ;

	memset (&wndcls, 0, sizeof (WNDCLASS)) ;

	wndcls.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW ;
	wndcls.lpfnWndProc = ::DefWindowProc ;
	wndcls.hInstance = AfxGetInstanceHandle () ;

	wndcls.hIcon = LoadIcon (IDR_MAINFRAME) ;

	wndcls.hCursor = LoadCursor (IDC_ARROW) ;
	wndcls.hbrBackground = (HBRUSH) (COLOR_WINDOW+1) ;
	wndcls.lpszMenuName = NULL ;

	wndcls.lpszClassName = _T("cubrid_tray") ;

	if (!AfxRegisterClass (&wndcls))
	{
		TRACE ("Class Registration Failed\n") ;
		return FALSE ;
	}
	bClassRegistered = TRUE ;

	/*Enable3dControls();*/

	CMainFrame* pMainFrame = new CMainFrame;

	m_pMainWnd = pMainFrame;

	if (!pMainFrame->Create(NULL, _T("cubrid_tray")))
	{
		return FALSE;
	}

	m_pMainWnd->ShowWindow(SW_HIDE);
	m_pMainWnd->UpdateWindow();

	return TRUE;
}

int CUnitrayApp::ExitInstance() 
{
	if (bClassRegistered)
		::UnregisterClass (_T("cubrid_tray"), AfxGetInstanceHandle ()) ;

	if (m_pMainWnd) {
		delete (m_pMainWnd);
		m_pMainWnd = NULL;
	}

	CWinApp::ExitInstance();

	_exit(0);
}

/////////////////////////////////////////////////////////////////////////////
// CAboutDlg dialog used for App About

extern	CLang		theLang;

CAboutDlg::CAboutDlg() : CDialog(CAboutDlg::IDD)
{
	//{{AFX_DATA_INIT(CAboutDlg)
	//}}AFX_DATA_INIT
	sCUBRIDInfo = NULL;
	sUniCASInfo = NULL;
	sUniTrayInfo = NULL;
}

CAboutDlg::~CAboutDlg()
{
	//{{AFX_DATA_INIT(CAboutDlg)
	//}}AFX_DATA_INIT
	if( sCUBRIDInfo  ) delete sCUBRIDInfo;
	if( sUniCASInfo  ) delete sUniCASInfo;
	if( sUniTrayInfo ) delete sUniTrayInfo;
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CAboutDlg)
	DDX_Control( pDX, IDOK              , m_ok                );
//	DDX_Control( pDX, IDC_ABT_VER_CUBRID, m_txt_CUBRIDversion );
//	DDX_Control( pDX, IDC_ABT_VER_UNICAS, m_txt_unicasversion );
//	DDX_Control( pDX, IDC_ABT_VER_LABEL , m_txt_verlabel      );
//	DDX_Control( pDX, IDC_ABT_COPYRIGHTS, m_txt_copyrights    );
	DDX_Control( pDX, IDC_ABT_MSG       , m_txt_msg           );
//	DDX_Control( pDX, IDC_CUBRID_VERSION, m_CUBRIDversion     );
//	DDX_Control( pDX, IDC_UNICAS_VERSION, m_unicasversion     );
//	DDX_Control( pDX, IDC_ABOUT_UNITRAY , m_unitray_Info      );
//	DDX_Control( pDX, IDC_HIDDEN_CMD    , m_hidden_kingch     );

	//}}AFX_DATA_MAP
}

void CAboutDlg::SetVersion (CString CUBRID_version, CString unicas_version)
{
	m_CUBRID_version = CUBRID_version;
	m_unicas_version = unicas_version;
}


// 2002.10.11 By KingCH
// Object : Patch �Ǵ� Tcl Version�� Null�� ��� Null Point Exception �߻��ϴ� ����� �� �κп� ���Ͽ� ������ �ߴ�.
void CAboutDlg::SetVersion()
{
	CManageRegistry* cCUBRIDReg = new CManageRegistry( "CUBRID" );
	CManageRegistry* cUniCASReg = new CManageRegistry( "CUBRIDCAS" );
	int dUniCASsz, dCUBRIDsz;

	char* sCasVers = cUniCASReg->sGetItem( "Version" );
	char* sCasTcl  = cUniCASReg->sGetItem( "TCL" );

	char* sSQLVers = cCUBRIDReg->sGetItem( "Version" );
	char* sSQLPatch = cCUBRIDReg->sGetItem( "Patch" );

	m_CUBRID_version.Empty();
	m_unicas_version.Empty();


	// 2002�� 10�� 19�� By KingCH
	// Registry ������ ������ ����
	if( !sCasTcl && !sCasVers )
	{
		sUniCASInfo = new char[ 100 ];
		memset( sUniCASInfo, 0x00, 100 );
		sprintf ( sUniCASInfo, "UniCAS is not installed" );
	}
	else
	{
		if( sCasTcl )
			dUniCASsz = strlen( sCasVers ) + strlen( sCasTcl ) + 5;
		else
			dUniCASsz = strlen( sCasVers ) + 5;

		sUniCASInfo = new char[ dUniCASsz + 1 ];
		memset( sUniCASInfo, 0x00, dUniCASsz + 1 );

		if( sCasTcl )
			sprintf( sUniCASInfo, "%s TCL %s", sCasVers, sCasTcl );
		else
			sprintf( sUniCASInfo, "%s", sCasVers );
	}

/*
	if( !sCasVers || !sCasTcl ) 
			sUniCASInfo = sGetReleaseString( cUniCASReg->sGetItem( "ROOT_PATH" ) );
	else
	{
		dUniCASsz = strlen( sCasVers ) + strlen( sCasTcl ) + 5;
		sUniCASInfo = new char[ dUniCASsz + 1 ];
		memset( sUniCASInfo, 0x00, dUniCASsz + 1 );
		sprintf( sUniCASInfo, "%s TCL %s", sCasVers, sCasTcl );
	}
*/


	// 2002�� 10�� 19�� By KingCH
	// Registry ������ ������ ����
	if( !sSQLPatch && !sSQLVers )
	{
		sCUBRIDInfo = new char[ 100 ];
		memset( sCUBRIDInfo, 0x00, 100 );
		sprintf ( sCUBRIDInfo, "CUBRID is not installed." );
	}
	else
	{
		if( sSQLPatch )
			dCUBRIDsz = strlen( sSQLVers ) + strlen( sSQLPatch ) + 7;
		else
			dCUBRIDsz = strlen( sSQLVers ) + 7;

		sCUBRIDInfo = new char[ dCUBRIDsz + 1 ];
		memset( sCUBRIDInfo, 0x00, dCUBRIDsz + 1 );
		if( sSQLPatch )
			sprintf( sCUBRIDInfo, "%s patch %s", sSQLVers, sSQLPatch );
		else
			sprintf( sCUBRIDInfo, "%s", sSQLVers );
	}

	delete cCUBRIDReg;
	delete cUniCASReg;

	return;
}



char* CAboutDlg::sGetReleaseString( char* sPath )
{
	char* sFullName;
	char* sValue;
	FILE* fd_rel;
	struct _stat buf;
	int result;

	if( !sPath ) return NULL;

	sFullName = new char[ strlen( sPath ) + 15 ];
	memset( sFullName, 0x00, strlen( sPath ) + 15 );
	sprintf( sFullName, "%s\\RELEASE_STRING", sPath );

	fd_rel = fopen( sFullName, "r" );

	if( fd_rel )
	{
		delete sFullName;
		return NULL;
	}

	result = _stat( sFullName, &buf );

	/* Check if statistics are valid: */
	if( result != 0 )
	{
		delete sFullName;
		return NULL;
	}

	sValue = new char[ buf.st_size + 1 ];
	memset( sValue, 0x00, buf.st_size );

	fread( sValue, buf.st_size, 1, fd_rel );

	fclose( fd_rel );
	return sValue;
}





BOOL CAboutDlg::OnInitDialog() 
{
	CDialog::OnInitDialog();

	CString serverVersion;

	serverVersion.Format("CUBRID %s", PRODUCT_STRING);
	m_txt_msg.SetWindowText((LPCTSTR)serverVersion);
	m_ok.SetWindowText(theLang.GetMessage(WN_ABT_OK));

	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialog)
//	ON_BN_CLICKED(IDC_HIDDEN_CMD, OnHidden)
	//{{AFX_MSG_MAP(CAboutDlg)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

char* CAboutDlg::sGetFileInfo( char* sCmd )
{

	return NULL;
}




// App command to run the dialog
void CUnitrayApp::OnAppAbout()
{
	CAboutDlg aboutDlg;

	aboutDlg.DoModal();
}


