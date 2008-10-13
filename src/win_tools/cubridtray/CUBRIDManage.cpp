// CUBRIDManage.cpp: implementation of the CCUBRIDManage class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "cubridtray.h"
#include "CUBRIDManage.h"

#include "ManageRegistry.h"
#include "Process.h"
#include "lang.h"
#include "MainFrm.h"
//#include "unitray_comm.h"

#include "env.h"
#include "lang.h"
//#include "MainFrm.h"

extern CLang theLang;
extern CEnv  Env;


#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif


char* sExcuteName[] = { "master.exe", "server.exe", NULL, "emg.exe", "Easy-Manager.exe", "Visual-SQL.exe", "commdb.exe -I" };


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CCUBRIDManage::CCUBRIDManage()
{
	memset( sCatchResStr, 0x00, sizeof( sCatchResStr ) );

	bCUBRID = false;
	bMASTER = bCheckMaster();

	time( &pPreTimeFileList );
	time( &pCurTimeFileList );

	time( &pPreTimeProcessList );
	time( &pCurTimeProcessList );

	pExecuteDBList = NULL;
	pFileDBList = NULL;

	pStartDBList = NULL;
	pStopDBList = NULL;
}

CCUBRIDManage::~CCUBRIDManage()
{
	bCUBRID = false;
	bMASTER = false;
}


#include "Process.h"


// master.exe가 수행 되고 있는지 Check한다.
bool CCUBRIDManage::bCheckMaster()
{
	CManageRegistry* cReg = new CManageRegistry( "CUBRID" );
	char* sPath = cReg->sGetItem( "ROOT_PATH" );

	delete cReg;

	if( !sPath )
		return false;

	// master.exe의 FullName을 구성한다.
	char sFullName[1024];
	memset( sFullName, 0x00, sizeof( sFullName ) );
	sprintf( sFullName, "%s\\%s", sPath, "master.exe" );

	CProcess* cProc = new CProcess();
	unsigned long lRes = cProc->FindProcess( sFullName );

	delete cProc;

	bMASTER = false;

	if( lRes <= 0 )
		return false;

	bMASTER = true;
	return true;
}



// server.exe가 실행 되고 있는지 Check한다.
bool CCUBRIDManage::bCheckServer()
{
	CManageRegistry* cReg = new CManageRegistry( "CUBRID" );
	char* sPath = cReg->sGetItem( "ROOT_PATH" );
	delete cReg;

	if( !sPath )
		return false;

	// server.exe의 FullName을 구성한다.
	char sFullName[1024];
	memset( sFullName, 0x00, sizeof( sFullName ) );
	sprintf( sFullName, "%s\\%s", sPath, "server.exe" );

	CProcess* cProc = new CProcess();
	unsigned long lRes = cProc->FindProcess( sFullName );

	delete cProc;

	if( lRes <= 0 ) return false;
	return true;
}






/*
bool CCUBRIDManage::bStatusMaster()
{
	CManageRegistry* cReg = new CManageRegistry( "CUBRID" );
	char* sPath = cReg->sGetItem( "ROOT_PATH" );
	delete cReg;

	if( !sPath )
		return false;

	int dSize = strlen( sPath ) + strlen( "commdb.exe -P" );
	char* sFullName = new char[ dSize + 5 ];
	memset( sFullName, 0x00, dSize + 5 );
	sprintf( sFullName, "%s\\%s", sPath, "commdb.exe -P" );

	delete sPath;

//	char* sResult = sCatchResult( sFullName );
	bCatchResult( sFullName );

	if( !sCatchResStr || strlen( sCatchResStr ) <= 0 )
		return false;


	char* sTmp;

	sTmp = strstr( sResult, "running." );
	if( !sTmp )
	{
		bMASTER = false;
		return false;
	}

	bMASTER = true;
	return true;
}






bool CCUBRIDManage::bStatusServer()
{
	CManageRegistry* cReg = new CManageRegistry( "CUBRID" );
	char* sPath = cReg->sGetItem( "ROOT_PATH" );

	delete cReg;

	if( !sPath )
		return false;



	int dSize = strlen( sPath ) + strlen( "commdb.exe -P" );
	char* sFullName = new char[ dSize + 5 ];
	memset( sFullName, 0x00, dSize + 5 );
	sprintf( sFullName, "%s\\%s", sPath, "commdb.exe -P" );
	delete sPath;

//	char* sResult = sCatchResult( sFullName );
	bCatchResult( sFullName );

	if( !sCatchResStr || strlen( sCatchResStr ) <= 0 )
		return false;

	char* sTmp;

	sTmp = strstr( sResult, " Server" );
	if( !sTmp )
	{
		bMASTER = false;
		return false;
	}

	bMASTER = true;
	return true;
}
*/



// Stop대상 DB의 List를 가지고 온다.
DBNAMEPtr_t CCUBRIDManage::pReqStopDBList()
{
	return pStopDBList;
}

// Start대상 DB의 List를 가지고 온다.
DBNAMEPtr_t CCUBRIDManage::pReqStartDBList()
{
	return pStartDBList;
}






#include "ORDBList.h"

//	현재 기동 되고 있는 DB의 List를 가져온다.
DBNAMEPtr_t CCUBRIDManage::pGetStartDBList()
{
	return( pGetDBListProcess() );
}


//	현재 기동 시킬 수 있는 DB List를 가져온다.
DBNAMEPtr_t CCUBRIDManage::pGetStopDBList()
{
	DBNAMEPtr_t pAllDB;
	DBNAMEPtr_t pTmpAll, pTmpStart;
	DBNAMEPtr_t pResult = NULL, pTmpResult, pTmpBack;
	bool bFind;
	int dNum = 0;

	// ordblist.txt에서 설치되어 있는 db의 list를 가져온다.
	pAllDB = pGetDBListFile();
	if( !pAllDB )
	{
		pStopDBList = NULL;
		return NULL;
	}

	// 현재 실행되고 있는 db를 가져온다.
	pStartDBList = pGetDBListProcess();

	// 실행 되고 있는 db가 없으므로, db의 list 모두가 기동 시킬수 있는 db가 된다.
	if( !pStartDBList )
	{
		pStopDBList = pAllDB;
		return pAllDB;
	}

	pTmpAll = pAllDB;

	while( 1 )
	{
		bFind = false;
		pTmpStart = pStartDBList;

		while( 1 )
		{
			// 실행 여부 Check
			if( strcmp( pTmpStart->sName, pTmpAll->sName ) == 0 )
			{
				// 실행된 DB인 경우, Skip한다.( 왜냐하면, 실행 대상 DB가 아니므로 )
				bFind = true;
				break;
			}

			// 다음 항목이 있는지 Check하여, 없으면 loop를 빠져나간다.
			if( !pTmpStart->next )
			{
				bFind = false;
				pTmpStart->next = NULL;
				break;
			}

			pTmpStart = pTmpStart->next;
		}

		// 대상으로 부터 해당 DB name을 찾지 못하였을 경우 
		if( !bFind )
		{
			pTmpBack = pMakeList( dNum ++, pTmpAll->sName );

			if( !pResult )
			{
				pResult = pTmpBack;
				pTmpResult = pTmpBack;
			}
			else
			{
				pTmpResult->next = pTmpBack;
				pTmpResult = pTmpBack;
			}


		}

		// 더 이상 검색할 List가 없을 경우 종료한다.
		if( !pTmpAll->next )
		{
			pTmpResult = NULL;
			break;
		}

		pTmpAll = pTmpAll->next;
	}

	bDestoryDBList( pAllDB );

	// 검색한 결과의 최종 DB List를 전역 변수 pStopDBList에 저장한다.
	pStopDBList = pResult;
	return pResult;
}





//	ORDBLIST.TXT로 부터 DB List를 Catch한다.
DBNAMEPtr_t CCUBRIDManage::pGetDBListFile()
{
	// 시간에 대한 정보를 저장한다.
	time( &pPreTimeFileList );

	CORDBList* cOrdbList = new CORDBList();
	char* sDBName;
	DBNAMEPtr_t pRoot, pParent, pCur;

	pParent = pCur = pRoot = NULL;

	// ORDBLIST.TXT로 부터 DB List를 뽑아온다.
	if( cOrdbList && cOrdbList->ReadDBInfo() )
	{
		CDBInfo	*db;
		int count = cOrdbList->m_List.GetCount();

		for (int i = 0; i < count; i++)
		{
			pCur = NULL;
			db = (CDBInfo *)cOrdbList->m_List.GetAt(cOrdbList->m_List.FindIndex(i));

			// DBName
			sDBName = (char *)LPCSTR(db->m_dbname);

			// Root인 경우
			if( !pParent )
			{
				pParent = pMakeList( i, sDBName );
				pRoot = pParent;
				continue;
			}

			pCur = pMakeList( i, sDBName );
			pParent->next = pCur;
			pParent = pCur;
		}
	}

	// 작업한 결과를 저장한다.
	pFileDBList = pRoot;
	return pRoot;
}



/*
DBNAMEPtr_t CCUBRIDManage::pMakeList( DBNAMEPtr_t pParent, unsigned int dNum, char* sName )
{
	if( !sName ) return NULL;

	DBNAMEPtr_t pName = new DBNAME_t;
	memset( pName, 0x00, sizeof( DBNAME_t ) );

	pName->sName = sName;
	pName->dNum = dNum;
	pName->bStart = true;
	pName->next = NULL;

	if( !pParent ) return pName;

	pParent->next = pName;
	return pParent;
}
*/



//	현재 수행 중인 DB를 Process를 검색을 통하여, Catch한다.
DBNAMEPtr_t CCUBRIDManage::pGetDBListProcess()
{
	// 시간에 대한 정보를 저장한다.
	time( &pPreTimeProcessList );

	CManageRegistry* cReg = new CManageRegistry( "CUBRID" );
	char* sPath = cReg->sGetItem( "ROOT_PATH" );
	delete cReg;

	pStartDBList = NULL;

	if( !sPath )
		return NULL;


	char sFullName[1024];
	memset( sFullName, 0x00, sizeof( sFullName ) );
	sprintf( sFullName, "%s\\%s", sPath, "commdb.exe -P" );

//	char* sResult = sCatchResult( sFullName );
	// sFullName을 실행하여, sCatchResStr이라는 변수에 저장한다.
	bCatchResult( sFullName );

	delete sPath;


	if( !sCatchResStr || strlen( sCatchResStr ) <= 0 )
		return NULL;


	// DB 이름을 이용하여, Linked List를 구성한다.
	char* sTmp = sCatchResStr;
	char* sStrRes;
	int dNum = 1;
	DBNAMEPtr_t pParent, pChild;

	// " Server"를 Key하는 String의 시작 포인트를 찾는다.
	sTmp = strstr( sTmp, " Server" );
	if( !sTmp ) return NULL;

	// String에서 DB Name을 가져온다.
	sStrRes = sGetName( sTmp );
	if( !sStrRes ) return NULL;

	// DB List를 구성한다.
	pStartDBList = pMakeList( dNum ++, sStrRes );
	delete sStrRes;
	if( !pStartDBList ) return NULL;

	sTmp = strchr( sTmp, ',' );

	pParent = pStartDBList;

	while( 1 )
	{
		if( !sTmp ) break;
		sTmp = strstr( sTmp, " Server" );
		if( !sTmp ) break;

		sStrRes = sGetName( sTmp );
		if( !sStrRes ) break;

		pChild = pMakeList( dNum ++, sStrRes );
		delete sStrRes;
		if( !pChild ) break;

		sTmp = strchr( sTmp, ',' );
		if( !sTmp ) break;

		pParent->next = pChild;
		pParent = pChild;
	}

	return pStartDBList;
}



// DB Name에 대한 List를 구성하기 위한 원소를 만든다.
DBNAMEPtr_t CCUBRIDManage::pMakeList( unsigned int dNum, char* sName )
{
	if( !sName ) return NULL;

	DBNAMEPtr_t pName = new DBNAME_t;
	memset( pName, 0x00, sizeof( DBNAME_t ) );

	memset( pName->sName, 0x00, sizeof( pName->sName ) );
	strcpy( pName->sName, sName );
	pName->dNum = dNum;
	pName->bStart = false;
	pName->next = NULL;

	return pName;
}

/*
char* CCUBRIDManage::sCatchResult( char* sCmd )
{
	CLang	theLang;
	CVersionRedirect	g_CUBRIDVersion;

//	if( sCatchResStr ) delete sCatchResStr;
//	sCatchResStr = NULL;

	if( !g_CUBRIDVersion.StartChildProcess( sCmd ) )
		return NULL;

	int  dRepCnt = 0;

	while(1)
	{
		Sleep(500);

//		sCatchResStr = new char[ strlen( ( char * )LPCSTR( g_CUBRIDVersion.m_version ) + 1 ) ];
		memset( sCatchResStr, 0x00, sizeof( sCatchResStr ) );
		strcpy( sCatchResStr, ( char * )LPCSTR( g_CUBRIDVersion.m_version ) );

		if( sCatchResStr ) return sCatchResStr;

		if( ++dRepCnt >= 10 ) return NULL;
	}

	return NULL;
}
*/


bool CCUBRIDManage::bCatchResult( char* sCmd )
{
	CLang	theLang;
	CVersionRedirect	g_CUBRIDVersion;

	if( !g_CUBRIDVersion.StartChildProcess( sCmd ) ) return false;

	int  dRepCnt = 0;

	while(1)
	{
		Sleep(500);

		memset( sCatchResStr, 0x00, sizeof( sCatchResStr ) );
		strcpy( sCatchResStr, ( char * )LPCSTR( g_CUBRIDVersion.m_version ) );

		if( strlen( sCatchResStr ) > 0 ) return true;

		if( ++dRepCnt >= 10 ) return false;
	}

	return false;
}


char* CCUBRIDManage::sGetName( char* sStr )
{
	if( !sStr ) return NULL;

	char* sTmp = strstr( sStr, "Server " );
	if( !sTmp ) return NULL;

	sTmp += strlen( "Server " );
	char* sEnd = strchr( sTmp, ',' );
	if( !sEnd ) return NULL;

	int dSize = sEnd - sTmp;

	char* sResult = new char[ dSize + 1 ];
	memset( sResult, 0x00, dSize + 1 );
	memcpy( sResult, sTmp, dSize );

	return sResult;
}


/* 실행된 DB 중, 찾고자 하는 DB가 있는지 확인한다 */
bool CCUBRIDManage::bCompareDB( char* sDBName, DBNAMEPtr_t pDBList )
{
	if( !sDBName || !pDBList ) return false;

	DBNAMEPtr_t pCur = pDBList;

	while( 1 )
	{
		TRACE2( "Compare String -> [%d] : [%s]\n", pCur->dNum, pCur->sName );

		if( strcmp( pCur->sName, sDBName ) == 0 ) return true;

		if( !pCur->next ) break;

		pCur = pCur->next;
	}

	return false;
}


bool CCUBRIDManage::bDestoryDBList( DBNAMEPtr_t pDBList )
{
	if( !pDBList ) return true;

	DBNAMEPtr_t pRoot = pDBList;
	DBNAMEPtr_t pCur  = pRoot;
	DBNAMEPtr_t pNext;

	while( 1 )
	{
		pNext = pCur->next;

		delete pCur;

		if( !pNext ) break;
		pCur = pNext;
	}

	return true;
}









// 2002년 10월 19일 By KingCH
// _chdir(), getenv()를 함수를 사용하기 위함
#include <direct.h>
#include <stdlib.h>


/*
	CUBRID에 관련된 Process를 실행한다.
*/
bool CCUBRIDManage::bStartMaster()
{
	CManageRegistry* cReg = new CManageRegistry( "CUBRID" );
	char* sPath = cReg->sGetItem( "ROOT_PATH" );

	// 2002년 10월 18일 By KingCH
	// EasyManager Server의 Root Directory 정보를 가져온다.
	// Working Directory를 바꿔서 실행을 시키면, Log가 시작 메뉴에 남지 않는다.
	char* sWorkDir = getenv( "CUBRID_DATABASES" );


	// 2002년 10월 18일 By KingCH
	// UNITOOL_EMGR가 환경 변수에 없으면, Register를 읽어서, sWorkDir로 활용한다.
	if( !sWorkDir ) sWorkDir = sPath;
	int dchdir = _chdir( sWorkDir );


	delete cReg;

	if( !sPath ) return false;

	char sFullName[1024];
	memset( sFullName, 0x00, sizeof( sFullName ) );
	sprintf( sFullName, "%s\\%s", sPath, "master.exe" );

	int dRes = WinExec( sFullName, SW_HIDE );
	delete sPath;
	if( dRes < 31 ) return false;

	CProcess* cProc = new CProcess();
	unsigned long bRes;
	int  dRepCnt = 0;

	while(1)
	{
		Sleep(100);
		bRes = cProc->FindProcess( sFullName );

		if( bRes > 0 ) break;
		if( ++dRepCnt >= 3 ) return false;
	}

	delete cProc;

	return true;
}

bool CCUBRIDManage::bStartCUBRID( char *sdbname )
{
	CManageRegistry* cReg = new CManageRegistry( "CUBRID" );
	char* sPath = cReg->sGetItem( "ROOT_PATH" );

	// 2002년 10월 18일 By KingCH
	// EasyManager Server의 Root Directory 정보를 가져온다.
	// Working Directory를 바꿔서 실행을 시키면, Log가 시작 메뉴에 남지 않는다.
	char* sWorkDir = getenv( "CUBRID_DATABASES" );


	// 2002년 10월 18일 By KingCH
	// UNITOOL_EMGR가 환경 변수에 없으면, Register를 읽어서, sWorkDir로 활용한다.
	if( !sWorkDir ) sWorkDir = sPath;
	int dchdir = _chdir( sWorkDir );

	delete cReg;

	if( !sPath ) return false;

	if( !bCheckMaster() )
		if( !bStartMaster() )
			return false;

	char sFullName[1024];
	memset( sFullName, 0x00, sizeof( sFullName ) );
	sprintf( sFullName, "%s\\%s %s", sPath, "server.exe", sdbname );

	int dRes = WinExec( sFullName, SW_HIDE );
	delete sPath;
	if( dRes < 31 ) return false;

	DBNAMEPtr_t pCurDB;
	int  dRepCnt = 0;
	unsigned long bRes;
	
	while(1)
	{
		Sleep(100);
		pCurDB = pGetDBListProcess();

		if( !pCurDB ) Sleep(50);

		if( ++dRepCnt >= 3 )
			return false;

		if( pCurDB )
		{
			bRes = bCompareDB( sdbname, pCurDB );
			bDestoryDBList( pCurDB );

			if( bRes ) break;
		}
	}

	return true;
}

bool CCUBRIDManage::bStopMaster()
{
	return true;
}

bool CCUBRIDManage::bStopCUBRID( char* sdbname )
{
	CManageRegistry* cReg = new CManageRegistry( "CUBRID" );
	char* sPath = cReg->sGetItem( "ROOT_PATH" );
	delete cReg;

	if( !sPath ) return false;

	char sFullName[1024];
	memset( sFullName, 0x00, sizeof( sFullName ) );
	sprintf( sFullName, "%s\\commdb.exe -I %s", sPath, sdbname );

	int dRes = WinExec( sFullName, SW_HIDE );
	delete sPath;

	if( dRes < 31 )	return false;

	DBNAMEPtr_t pCurDB;
	int  dRepCnt = 0;
	bool bRes;

	while(1)
	{
		Sleep(100);
		pCurDB = pGetDBListProcess();

		if( !pCurDB ) break;

		bRes = bCompareDB( sdbname, pCurDB );
		bDestoryDBList( pCurDB );

		if( !bRes ) break;

		if( ++dRepCnt >= 3 ) return false;
	}


	return true;
}

/*
bool CCUBRIDManage::bStopCUBRID( int dInd )
{
	char	commdbpath[100];

	sprintf(commdbpath, "%s\\%s %s", theEnv.GetCUBRIDX(), CMD_SHUTDOWN_SERVER, LPCSTR(g_Server[index].m_DBName));

	CRedir	shutdown;
	shutdown.StartChildProcess(commdbpath);
	g_Server[index].m_DBName.Empty();
	m_ServerCnt--;

	return true;
}
*/





bool CCUBRIDManage::bInstallStatus()
{
	CManageRegistry* cReg = new CManageRegistry( "CUBRID" );
	char* sPath = cReg->sGetItem( "ROOT_PATH" );
	delete cReg;

	if( !sPath || strlen( sPath ) <= 0 ) return false;

	delete sPath;
	return true;
}








