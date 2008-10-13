// Property.cpp : implementation file
//

#include "stdafx.h"
#include "cubridtray.h"
#include "ORDBList.h"
#include "Property.h"
#include "lang.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

extern	CLang		theLang;
/////////////////////////////////////////////////////////////////////////////
// CProperty dialog


CProperty::CProperty(CORDBList *ordblist, CWnd* pParent /*=NULL*/)
	: CDialog(CProperty::IDD, pParent)
{
	//{{AFX_DATA_INIT(CProperty)
	//}}AFX_DATA_INIT
	m_OrdbList = ordblist;
}


void CProperty::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CProperty)
	DDX_Control(pDX, IDC_PRO_TXT_LOGPATH, m_txt_logpath);
	DDX_Control(pDX, IDC_PRO_TXT_HOST, m_txt_host);
	DDX_Control(pDX, IDC_PRO_TXT_DBPATH, m_txt_dbpath);
	DDX_Control(pDX, ID_PRO_CANCEL, m_pro_cancel);
	DDX_Control(pDX, IDC_PRO_LIST, m_list);
	DDX_Control(pDX, ID_PRO_OK, m_pro_ok);
	DDX_Control(pDX, IDC_PRO_PERMANENT, m_permanent);
	DDX_Control(pDX, IDC_PRO_LOGPATH, m_logpath);
	DDX_Control(pDX, IDC_PRO_HOST, m_host);
	DDX_Control(pDX, IDC_PRO_DBPATH, m_dbpath);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CProperty, CDialog)
	//{{AFX_MSG_MAP(CProperty)
	ON_BN_CLICKED(ID_PRO_CANCEL, OnProCancel)
	ON_BN_CLICKED(ID_PRO_OK, OnProOk)
	ON_BN_CLICKED(IDC_PRO_PERMANENT, OnProPermanent)
	ON_LBN_SELCHANGE(IDC_PRO_LIST, OnSelchangeProList)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CProperty message handlers


void CProperty::OnProCancel() 
{
	CDialog::OnCancel();
}

void CProperty::OnProOk() 
{
	CDialog::OnOK();
}

/*
void CProperty::OnClickProList(NMHDR* pNMHDR, LRESULT* pResult)
{
int	item;
CDBInfo	*db;

	item = m_list.GetNextItem(-1, LVNI_ALL | LVNI_SELECTED);
	if (item < 0 || item >= m_list.GetItemCount()) {
		m_dbpath.SetWindowText("");
		m_host.SetWindowText("");
		m_logpath.SetWindowText("");
		m_permanent.SetCheck(0);
	} else {
		db = (CDBInfo *)m_OrdbList->m_List.GetAt(m_OrdbList->m_List.FindIndex(item));
		m_dbpath.SetWindowText(LPCSTR(db->m_dbpath));
		m_host.SetWindowText(LPCSTR(db->m_host));
		m_logpath.SetWindowText(LPCSTR(db->m_logpath));
		if (db->m_permanent) m_permanent.SetCheck(1);
		else			     m_permanent.SetCheck(0);
	}
}
*/
BOOL CProperty::OnInitDialog() 
{
	CDialog::OnInitDialog();

	SetWindowText(theLang.GetMessage(WN_DBP_TITLE));
	m_txt_dbpath.SetWindowText(theLang.GetMessage(WN_DBP_PATHNAME));
	m_txt_host.SetWindowText(theLang.GetMessage(WN_DBP_DBNAME));
	m_txt_logpath.SetWindowText(theLang.GetMessage(WN_DBP_LOGPATH));
	m_permanent.SetWindowText(theLang.GetMessage(WN_DBP_CHECK));
	m_pro_ok.SetWindowText(theLang.GetMessage(WN_DBP_OK));
	m_pro_cancel.SetWindowText(theLang.GetMessage(WN_DBP_CANCEL));

	if (!m_OrdbList->m_List.IsEmpty()) {
		CDBInfo *db;
		int count = m_OrdbList->m_List.GetCount();
		for (int i = 0; i < count; i++) {
			db = (CDBInfo *)m_OrdbList->m_List.GetAt(m_OrdbList->m_List.FindIndex(i));
			m_list.InsertString(i, (char *)LPCSTR(db->m_dbname));
			if (i == 0) {
				m_dbpath.SetWindowText(LPCSTR(db->m_dbpath));
				m_host.SetWindowText(LPCSTR(db->m_host));
				m_logpath.SetWindowText(LPCSTR(db->m_logpath));
				if (db->m_permanent) m_permanent.SetCheck(1);
				else			     m_permanent.SetCheck(0);
			}
		}
		m_list.SetCurSel(0);
	}

	m_pro_ok.EnableWindow(false);

	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

void CProperty::OnProPermanent() 
{	
int	item;
CDBInfo	*db;

	item = m_list.GetCurSel();
	if (item < 0 || item >= m_list.GetCount()) return;
	db = (CDBInfo *)m_OrdbList->m_List.GetAt(m_OrdbList->m_List.FindIndex(item));
	if (m_permanent.GetCheck() == 1) {
		db->m_permanent = true;
	} else {
		db->m_permanent = false;
	}

	m_pro_ok.EnableWindow(true);
}

void CProperty::OnSelchangeProList() 
{
int	item;
CDBInfo	*db;

	item = m_list.GetCurSel();
	if (item < 0 || item >= m_list.GetCount()) {
		m_dbpath.SetWindowText("");
		m_host.SetWindowText("");
		m_logpath.SetWindowText("");
		m_permanent.SetCheck(0);
	} else {
		db = (CDBInfo *)m_OrdbList->m_List.GetAt(m_OrdbList->m_List.FindIndex(item));
		m_dbpath.SetWindowText(LPCSTR(db->m_dbpath));
		m_host.SetWindowText(LPCSTR(db->m_host));
		m_logpath.SetWindowText(LPCSTR(db->m_logpath));
		if (db->m_permanent) m_permanent.SetCheck(1);
		else			     m_permanent.SetCheck(0);
	}
}
