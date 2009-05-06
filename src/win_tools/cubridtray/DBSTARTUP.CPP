// DBStartUp.cpp : implementation file
//

#include "stdafx.h"
#include "cubridtray.h"
#include "ORDBList.h"
#include "DBStartUp.h"
#include "MainFrm.h"
#include "lang.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

extern	CLang		theLang;
/////////////////////////////////////////////////////////////////////////////
// CDBStartUp dialog


CDBStartUp::CDBStartUp(CORDBList *ordblist, CWnd* pParent /*=NULL*/)
	: CDialog(CDBStartUp::IDD, pParent)
{
	//{{AFX_DATA_INIT(CDBStartUp)
	//}}AFX_DATA_INIT
	
	m_OrdbList = ordblist;

//	m_DBName.RemoveAll();
}

void CDBStartUp::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CDBStartUp)
	DDX_Control(pDX, ID_SU_CANCEL, m_su_cancel);
	DDX_Control(pDX, IDC_SU_LIST, m_ListDB);
	DDX_Control(pDX, ID_SU_OK, m_su_ok);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CDBStartUp, CDialog)
	//{{AFX_MSG_MAP(CDBStartUp)
	ON_BN_CLICKED(ID_SU_OK, OnSuOk)
	ON_BN_CLICKED(ID_SU_CANCEL, OnSuCancel)
	ON_WM_CREATE()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CDBStartUp message handlers
extern CServerRedirect	g_Server[MAXSERVER];

BOOL CDBStartUp::PreCreateWindow(CREATESTRUCT& cs) 
{

	return CDialog::PreCreateWindow(cs);
}

void CDBStartUp::OnSuOk() 
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

void CDBStartUp::OnSuCancel() 
{
	m_DBName.RemoveAll();
	CDialog::OnCancel();	
}


BOOL CDBStartUp::DestroyWindow() 
{
	return CDialog::DestroyWindow();
}

int CDBStartUp::OnCreate(LPCREATESTRUCT lpCreateStruct) 
{
	if (CDialog::OnCreate(lpCreateStruct) == -1)
		return -1;

	return 0;
}

BOOL CDBStartUp::OnInitDialog() 
{
int	index = 0;

	CDialog::OnInitDialog();

	SetWindowText(theLang.GetMessage(WN_STD_TITLE));
	m_su_ok.SetWindowText(theLang.GetMessage(WN_STD_OK));
	m_su_cancel.SetWindowText(theLang.GetMessage(WN_STD_CANCEL));

	// TODO: Add extra initialization here
	int	count = (int) m_OrdbList->m_List.GetCount();

	for (int i = 0; i < count; i++) {
		CDBInfo	*db;
		bool	check = false;

		db = (CDBInfo*) m_OrdbList->m_List.GetAt(m_OrdbList->m_List.FindIndex(i));
		for (int j = 0; j < MAXSERVER; j++) {
			if (g_Server[j].m_DBName.IsEmpty()) continue;
			if (g_Server[j].m_DBName == db->m_dbname) { 
				check = true;
				break;
			}
		}

		if (!check) { 
			m_ListDB.InsertString(index, LPCSTR(db->m_dbname));
			index++;
		}
	}

	if (m_ListDB.GetCount() == 0) m_su_ok.EnableWindow(false);

	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}
