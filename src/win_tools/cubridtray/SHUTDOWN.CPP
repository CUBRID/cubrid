// Shutdown.cpp : implementation file
//

#include "stdafx.h"
#include "MainFrm.h"
#include "cubridtray.h"
#include "Shutdown.h"
#include "lang.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

extern	CLang		theLang;
/////////////////////////////////////////////////////////////////////////////
// CShutdown dialog


CShutdown::CShutdown(CWnd* pParent /*=NULL*/)
	: CDialog(CShutdown::IDD, pParent)
{
//	m_DBName.RemoveAll();
	//{{AFX_DATA_INIT(CShutdown)
	//}}AFX_DATA_INIT
}


void CShutdown::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CShutdown)
	DDX_Control(pDX, ID_SS_CANCEL, m_ss_cancel);
	DDX_Control(pDX, IDC_SS_LISTDB, m_ListDB);
	DDX_Control(pDX, ID_SS_OK, m_ss_ok);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CShutdown, CDialog)
	//{{AFX_MSG_MAP(CShutdown)
	ON_BN_CLICKED(ID_SS_OK, OnSsOk)
	ON_BN_CLICKED(ID_SS_CANCEL, OnSsCancel)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CShutdown message handlers
extern CServerRedirect	g_Server[MAXSERVER];

void CShutdown::OnSsOk() 
{	
int i, maxcount = m_ListDB.GetCount();
CString	tmpString;

	for (i = 0; i < maxcount; i++) {
		if (m_ListDB.GetSel(i) > 0) {
			m_ListDB.GetText(i, tmpString);
			m_DBName.AddHead(tmpString);
		}
	}

	CDialog::OnOK();
}

void CShutdown::OnSsCancel() 
{
	m_DBName.RemoveAll();
	CDialog::OnCancel();	
}

BOOL CShutdown::OnInitDialog() 
{
int	index = 0;

	CDialog::OnInitDialog();

	// Windows의 Button 등의 글자를 선택한 언어에 맞게 보여진다.
	SetWindowText(theLang.GetMessage(WN_SHD_TITLE));
	m_ss_ok.SetWindowText(theLang.GetMessage(WN_SHD_OK));
	m_ss_cancel.SetWindowText(theLang.GetMessage(WN_SHD_CANCEL));

	for (int i = 0; i < MAXSERVER; i++) {
		if (!g_Server[i].m_DBName.IsEmpty()) {
			m_ListDB.InsertString(index, LPCSTR(g_Server[i].m_DBName));
			index++;
		}
	}

	if (m_ListDB.GetCount() == 0) m_ss_ok.EnableWindow(false);

	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}
