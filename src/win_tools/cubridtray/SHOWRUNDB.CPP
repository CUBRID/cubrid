// ShowRunDB.cpp : implementation file
//

#include "stdafx.h"
#include "unitray.h"
#include "MainFrm.h"
#include "TextProgressCtrl.h"
#include "ShowRunDB.h"
#include "env.h"
#include "lang.h"


#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

extern CEnv		theEnv;
extern CLang	theLang;
/////////////////////////////////////////////////////////////////////////////
// CShowRunDB dialog


CShowRunDB::CShowRunDB(CWnd* pParent /*=NULL*/)
	: CDialog(CShowRunDB::IDD, pParent)
{
	//{{AFX_DATA_INIT(CShowRunDB)
	//}}AFX_DATA_INIT
}


void CShowRunDB::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CShowRunDB)
	DDX_Control(pDX, IDOK, m_ok);
	DDX_Control(pDX, IDCANCEL, m_cancel);
	DDX_Control(pDX, IDC_SRD_TXT_TPRO, m_txt_tempvol);
	DDX_Control(pDX, IDC_SRD_TXT_IPRO, m_txt_indexvol);
	DDX_Control(pDX, IDC_SRD_TXT_GPRO, m_txt_genericvol);
	DDX_Control(pDX, IDC_SRD_TXT_DPRO, m_txt_datavol);
	DDX_Control(pDX, IDC_SRD_TPRO, m_tempvol);
	DDX_Control(pDX, IDC_SRD_IPRO, m_indexvol);
	DDX_Control(pDX, IDC_SRD_GPRO, m_genericvol);
	DDX_Control(pDX, IDC_SRD_DPRO, m_datavol);
	DDX_Control(pDX, IDC_SRD_LIST, m_list);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CShowRunDB, CDialog)
	//{{AFX_MSG_MAP(CShowRunDB)
	ON_LBN_SELCHANGE(IDC_SRD_LIST, OnSelchangeSrdList)
	ON_MESSAGE(SPACEDB_MSG, OnDisplayDBsize)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CShowRunDB message handlers
extern CServerRedirect	g_Server[MAXSERVER];

void CShowRunDB::OnOK() 
{	
	CDialog::OnOK();
}

void CShowRunDB::OnCancel() 
{	
	CDialog::OnCancel();
}

BOOL CShowRunDB::OnInitDialog() 
{
	CDialog::OnInitDialog();

	SetWindowText(theLang.GetMessage(WN_SRD_TITLE));
	m_txt_genericvol.SetWindowText(theLang.GetMessage(WN_SRD_GVOL));
	m_txt_datavol.SetWindowText(theLang.GetMessage(WN_SRD_DVOL));
	m_txt_indexvol.SetWindowText(theLang.GetMessage(WN_SRD_IVOL));
	m_txt_tempvol.SetWindowText(theLang.GetMessage(WN_SRD_TVOL));
	m_ok.SetWindowText(theLang.GetMessage(WN_SRD_OK));
	m_cancel.SetWindowText(theLang.GetMessage(WN_SRD_CANCEL));

	for (int i = 0; i < MAXSERVER; i++) {
		if (!g_Server[i].m_DBName.IsEmpty()) {
			m_list.AddString(LPCSTR(g_Server[i].m_DBName));
		}
	}

	m_genericvol.SetShowText(true);
	m_datavol.SetShowText(true);
	m_indexvol.SetShowText(true);
	m_tempvol.SetShowText(true);

	m_list.SetCurSel(0);
	OnSelchangeSrdList();

	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

extern CListRedirect	g_ListDB;

void CShowRunDB::OnSelchangeSrdList() 
{
char	commdbpath[100];
int	item;
CString	tmpDB;

	item = m_list.GetCurSel();
	m_list.GetText(item, tmpDB);

	sprintf(commdbpath, "%s\\%s %s", theEnv.GetCUBRID(), CMD_SPACEDB_CS, LPCSTR(tmpDB));
	g_ListDB.m_dialog = this;
	g_ListDB.StartChildProcess(commdbpath);
}

void CShowRunDB::OnDisplayDBsize(WPARAM wParam, LPARAM lParam)
{
CString	genericvol, datavol, indexvol, tempvol;

	if (g_ListDB.m_gtvol != 0) {
		m_genericvol.SetRange(0, g_ListDB.m_gtvol);
		m_genericvol.SetPos(g_ListDB.m_gtvol - g_ListDB.m_gfvol);
		genericvol.Format("%d/%d (%d%%)", g_ListDB.m_gtvol - g_ListDB.m_gfvol, g_ListDB.m_gtvol, ((g_ListDB.m_gtvol - g_ListDB.m_gfvol) * 100 / g_ListDB.m_gtvol));
		m_genericvol.SetWindowText(LPCSTR(genericvol));
	} else {
		m_genericvol.SetRange(0, 1000);
		m_genericvol.SetPos(0);
		m_genericvol.SetWindowText(theLang.GetMessage(MSG_NO_VOLUME));
	}

	if (g_ListDB.m_dtvol != 0) {
		m_datavol.SetRange(0, g_ListDB.m_dtvol);
		m_datavol.SetPos(g_ListDB.m_dtvol - g_ListDB.m_dfvol);
		datavol.Format("%d/%d (%d%%)", g_ListDB.m_dtvol - g_ListDB.m_dfvol, g_ListDB.m_dtvol, ((g_ListDB.m_dtvol - g_ListDB.m_dfvol) * 100 / g_ListDB.m_dtvol));
		m_datavol.SetWindowText(LPCSTR(datavol));
	} else {
		m_datavol.SetRange(0, 1000);
		m_datavol.SetPos(0);
		m_datavol.SetWindowText(theLang.GetMessage(MSG_NO_VOLUME));
	}

	if (g_ListDB.m_itvol != 0) {
		m_indexvol.SetRange(0, g_ListDB.m_itvol);
		m_indexvol.SetPos(g_ListDB.m_itvol - g_ListDB.m_ifvol);
		indexvol.Format("%d/%d (%d%%)", g_ListDB.m_itvol - g_ListDB.m_ifvol, g_ListDB.m_itvol, ((g_ListDB.m_itvol - g_ListDB.m_ifvol) * 100 / g_ListDB.m_itvol));
		m_indexvol.SetWindowText(LPCSTR(indexvol));
	} else {
		m_indexvol.SetRange(0, 1000);
		m_indexvol.SetPos(0);
		m_indexvol.SetWindowText(theLang.GetMessage(MSG_NO_VOLUME));
	}

	if (g_ListDB.m_ttvol != 0) {
		m_tempvol.SetRange(0, g_ListDB.m_ttvol);
		m_tempvol.SetPos(g_ListDB.m_ttvol - g_ListDB.m_tfvol);
		tempvol.Format("%d/%d (%d%%)", g_ListDB.m_ttvol - g_ListDB.m_tfvol, g_ListDB.m_ttvol, ((g_ListDB.m_ttvol - g_ListDB.m_tfvol) * 100 / g_ListDB.m_ttvol));
		m_tempvol.SetWindowText(LPCSTR(tempvol));
	} else {
		m_tempvol.SetRange(0, 1000);
		m_tempvol.SetPos(0);
		m_tempvol.SetWindowText(theLang.GetMessage(MSG_NO_VOLUME));
	}
}
