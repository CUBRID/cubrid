// StopTargetDB.cpp : implementation file
//

#include "stdafx.h"
#include "cubridtray.h"
#include "StopTargetDB.h"

#include "env.h"
#include "lang.h"
//#include "MainFrm.h"

extern CLang theLang;
extern CEnv  Env;



#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CStopTargetDB dialog
CStopTargetDB::CStopTargetDB( CWnd* pParent /*=NULL*/ )
	: CDialog(CStopTargetDB::IDD, pParent)
{
	cCUBRID = new CCUBRIDManage();
	//{{AFX_DATA_INIT(CStopTargetDB)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT
}

CStopTargetDB::CStopTargetDB( CCUBRIDManage* cCUBRID, CWnd* pParent /*=NULL*/ )
	: CDialog(CStopTargetDB::IDD, pParent)
{
	cCUBRID = cCUBRID;
	//{{AFX_DATA_INIT(CStopTargetDB)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT
}


void CStopTargetDB::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CStopTargetDB)
	DDX_Control(pDX, ID_SS_CANCEL, m_ss_cancel);
	DDX_Control(pDX, IDC_SS_LISTDB, m_ListDB);
	DDX_Control(pDX, ID_SS_OK, m_ss_ok);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CStopTargetDB, CDialog)
	//{{AFX_MSG_MAP(CStopTargetDB)
	ON_BN_CLICKED(ID_SS_OK, OnSsOk)
	ON_BN_CLICKED(ID_SS_CANCEL, OnSsCancel)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CStopTargetDB message handlers

void CStopTargetDB::OnSsOk() 
{
	// TODO: Add your control notification handler code here
	int i, maxcount = m_ListDB.GetCount();
	CString	tmpString;
	bool bResult;
	char sSucDB[1024];
	memset( sSucDB, 0x00, sizeof( sSucDB ) );
	CString msg;

	for (i = 0; i < maxcount; i++) {
		if (m_ListDB.GetSel(i) > 0) {
			m_ListDB.GetText(i, tmpString);
			bResult = cCUBRID->bStopCUBRID( (char *)LPCSTR( tmpString ) );
TRACE2( "Stop DB( %s ) : %s\n", (char *)LPCSTR( tmpString ), bResult?"TRUE":"FALSE" );

			if( !bResult )
			{
				msg.Empty();
				msg.Format(theLang.GetMessage(MSG_SHUTDOWN_ERROR), (char *)LPCSTR( tmpString ));
				AfxMessageBox(msg, MB_OK | MB_ICONSTOP);
			}
			else
			{
				if( strlen( sSucDB ) <= 0 ) sprintf( sSucDB, "%s", (char *)LPCSTR( tmpString ) );
				else                        sprintf( sSucDB, "%s, %s", sSucDB, (char *)LPCSTR( tmpString ) );
			}
		}
	}

//	msg.Empty();
//	msg.Format(theLang.GetMessage(MSG_SHUTDOWN), sSucDB);
//	AfxMessageBox(msg, MB_OK | MB_ICONINFORMATION);

	CDialog::OnOK();
}

void CStopTargetDB::OnSsCancel() 
{
	// TODO: Add your control notification handler code here
	CDialog::OnCancel();	
}

#define		MAXSERVER	100

BOOL CStopTargetDB::OnInitDialog() 
{
	CDialog::OnInitDialog();

	// Windows의 Button 등의 글자를 선택한 언어에 맞게 보여진다.
	SetWindowText(theLang.GetMessage(WN_SHD_TITLE));
	m_ss_ok.SetWindowText(theLang.GetMessage(WN_SHD_OK));
	m_ss_cancel.SetWindowText(theLang.GetMessage(WN_SHD_CANCEL));

	// Master를 검사하여, Master가 실행이 안 되어있으면, 실행시킨다.
	if( cCUBRID->bCheckMaster() )
		if( !cCUBRID->bStartMaster() )
		{
			AfxMessageBox(theLang.GetMessage(MSG_MASTER_ERROR), MB_OK | MB_ICONSTOP);
			return FALSE;
		}

	// 기동된 DB를 가져온다. DB가 없으면, OK Buttone을 Disable한다.
	DBNAMEPtr_t pStopList = cCUBRID->pGetStartDBList();
//	DBNAMEPtr_t pStopList = cCUBRID->pReqStopDBList();

	if( !pStopList )
	{
		m_ss_ok.EnableWindow(false);
		return TRUE;
	}

	DBNAMEPtr_t pCur = pStopList;
	int index = 0;

	// ListBox에서 보여 줄수 있는 DB의 최대 갯수
	for (int i = 0; i < MAXSERVER; i++) {
		m_ListDB.InsertString( index, pCur->sName );

		if( !pCur->next ) break;
		pCur = pCur->next;
		index++;
	}


	cCUBRID->bDestoryDBList( pStopList );

	// TODO: Add extra initialization here
	
	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}


