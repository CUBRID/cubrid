// unitrayDlg.cpp : implementation file
//

#include "stdafx.h"
#include "unitray.h"
#include "unitrayDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CAboutDlg dialog used for App About

class CAboutDlg : public CDialog
{
public:
	CAboutDlg();

// Dialog Data
	//{{AFX_DATA(CAboutDlg)
	enum { IDD = IDD_ABOUTBOX };
	//}}AFX_DATA

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CAboutDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	//{{AFX_MSG(CAboutDlg)
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialog(CAboutDlg::IDD)
{
	//{{AFX_DATA_INIT(CAboutDlg)
	//}}AFX_DATA_INIT
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CAboutDlg)
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialog)
	//{{AFX_MSG_MAP(CAboutDlg)
		// No message handlers
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CUnitrayDlg dialog

CUnitrayDlg::CUnitrayDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CUnitrayDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CUnitrayDlg)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT
	// Note that LoadIcon does not require a subsequent DestroyIcon in Win32
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CUnitrayDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CUnitrayDlg)
		// NOTE: the ClassWizard will add DDX and DDV calls here
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CUnitrayDlg, CDialog)
	//{{AFX_MSG_MAP(CUnitrayDlg)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_WM_DESTROY()
	//}}AFX_MSG_MAP
	ON_MESSAGE(WM_TRAYICON_MSG, TrayIconMsg)

END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CUnitrayDlg message handlers

BOOL CUnitrayDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	// Add "About..." menu item to system menu.

	// IDM_ABOUTBOX must be in the system command range.
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL)
	{
		CString strAboutMenu;
		strAboutMenu.LoadString(IDS_ABOUTBOX);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	// Set the icon for this dialog.  The framework does this automatically
	//  when the application's main window is not a dialog
	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon
	
	// TODO: Add extra initialization here
	m_bIsTrayIcon = FALSE;

	// Tray Icon Regist
	RegistTrayIcon();
	
	return TRUE;  // return TRUE  unless you set the focus to a control
}

void CUnitrayDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	// 최소화 버튼을 눌렀을때
	else if(nID == SC_MINIMIZE)
	{	
		ShowWindow(SW_HIDE);
	}
	else
	{
		CDialog::OnSysCommand(nID, lParam);
	}
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CUnitrayDlg::OnPaint() 
{
	if (IsIconic())
	{
		CPaintDC dc(this); // device context for painting

		SendMessage(WM_ICONERASEBKGND, (WPARAM) dc.GetSafeHdc(), 0);

		// Center icon in client rectangle
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// Draw the icon
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialog::OnPaint();
	}
}

// The system calls this to obtain the cursor to display while the user drags
//  the minimized window.
HCURSOR CUnitrayDlg::OnQueryDragIcon()
{
	return (HCURSOR) m_hIcon;
}

void CUnitrayDlg::RegistTrayIcon()
{
	NOTIFYICONDATA  nid;
	nid.cbSize = sizeof(nid);
	nid.hWnd = m_hWnd; // 메인 윈도우 핸들
	nid.uID = IDR_MAINFRAME;  // 아이콘 리소스 ID
	nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; // 플래그 설정
	nid.uCallbackMessage = WM_TRAYICON_MSG; // 콜백메시지 설정
	nid.hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME); // 아이콘 로드	

	char strTitle[256];
	GetWindowText(strTitle, sizeof(strTitle)); // 캡션바에 출력된 문자열 얻음
	lstrcpy(nid.szTip, strTitle); 
	Shell_NotifyIcon(NIM_ADD, &nid);
	SendMessage(WM_SETICON, (WPARAM)TRUE, (LPARAM)nid.hIcon);
	m_bIsTrayIcon = TRUE;
}

void CUnitrayDlg::OnDestroy() 
{
	CDialog::OnDestroy();
	
	// TODO: Add your message handler code here	
	if(m_bIsTrayIcon) // 현재 트레이 아이콘으로 설정되었는지 확인	
	{
		NOTIFYICONDATA  nid;
		nid.cbSize = sizeof(nid);
		nid.hWnd = m_hWnd; // 메인 윈도우 핸들
		nid.uID = IDR_MAINFRAME;

		// 작업 표시줄(TaskBar)의 상태 영역에 아이콘을 삭제한다.
		Shell_NotifyIcon(NIM_DELETE, &nid);
	}
	
}

// WM_TRAYICON_MSG 이놈 메세지를 받으면 구동..
void CUnitrayDlg::TrayIconMsg(WPARAM wParam, LPARAM lParam)
{
	// 등록된 TrayIcon 클릭하면 Show....
	if(lParam == WM_LBUTTONDBLCLK)
	{		
		ShowWindow(SW_SHOW);
	}
}