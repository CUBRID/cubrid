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


// master.exe�� ���� �ǰ� �ִ��� Check�Ѵ�.
bool CCUBRIDManage::bCheckMaster()
{
	CManageRegistry* cReg = new CManageRegistry( "CUBRID" );
	char* sPath = cReg->sGetItem( "ROOT_PATH" );

	delete cReg;

	if( !sPath )
		return false;

	// master.exe�� FullName�� �����Ѵ�.
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



// server.exe�� ���� �ǰ� �ִ��� Check�Ѵ�.
bool CCUBRIDManage::bCheckServer()
{
	CManageRegistry* cReg = new CManageRegistry( "CUBRID" );
	char* sPath = cReg->sGetItem( "ROOT_PATH" );
	delete cReg;

	if( !sPath )
		return false;

	// server.exe�� FullName�� �����Ѵ�.
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



// Stop��� DB�� List�� ������ �´�.
DBNAMEPtr_t CCUBRIDManage::pReqStopDBList()
{
	return pStopDBList;
}

// Start��� DB�� List�� ������ �´�.
DBNAMEPtr_t CCUBRIDManage::pReqStartDBList()
{
	return pStartDBList;
}






#include "ORDBList.h"

//	���� �⵿ �ǰ� �ִ� DB�� List�� �����´�.
DBNAMEPtr_t CCUBRIDManage::pGetStartDBList()
{
	return( pGetDBListProcess() );
}


//	���� �⵿ ��ų �� �ִ� DB List�� �����´�.
DBNAMEPtr_t CCUBRIDManage::pGetStopDBList()
{
	DBNAMEPtr_t pAllDB;
	DBNAMEPtr_t pTmpAll, pTmpStart;
	DBNAMEPtr_t pResult = NULL, pTmpResult, pTmpBack;
	bool bFind;
	int dNum = 0;

	// ordblist.txt���� ��ġ�Ǿ� �ִ� db�� list�� �����´�.
	pAllDB = pGetDBListFile();
	if( !pAllDB )
	{
		pStopDBList = NULL;
		return NULL;
	}

	// ���� ����ǰ� �ִ� db�� �����´�.
	pStartDBList = pGetDBListProcess();

	// ���� �ǰ� �ִ� db�� �����Ƿ�, db�� list ��ΰ� �⵿ ��ų�� �ִ� db�� �ȴ�.
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
			// ���� ���� Check
			if( strcmp( pTmpStart->sName, pTmpAll->sName ) == 0 )
			{
				// ����� DB�� ���, Skip�Ѵ�.( �ֳ��ϸ�, ���� ��� DB�� �ƴϹǷ� )
				bFind = true;
				break;
			}

			// ���� �׸��� �ִ��� Check�Ͽ�, ������ loop�� ����������.
			if( !pTmpStart->next )
			{
				bFind = false;
				pTmpStart->next = NULL;
				break;
			}

			pTmpStart = pTmpStart->next;
		}

		// ������� ���� �ش� DB name�� ã�� ���Ͽ��� ��� 
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

		// �� �̻� �˻��� List�� ���� ��� �����Ѵ�.
		if( !pTmpAll->next )
		{
			pTmpResult = NULL;
			break;
		}

		pTmpAll = pTmpAll->next;
	}

	bDestoryDBList( pAllDB );

	// �˻��� ����� ���� DB List�� ���� ���� pStopDBList�� �����Ѵ�.
	pStopDBList = pResult;
	return pResult;
}





//	ORDBLIST.TXT�� ���� DB List�� Catch�Ѵ�.
DBNAMEPtr_t CCUBRIDManage::pGetDBListFile()
{
	// �ð��� ���� ������ �����Ѵ�.
	time( &pPreTimeFileList );

	CORDBList* cOrdbList = new CORDBList();
	char* sDBName;
	DBNAMEPtr_t pRoot, pParent, pCur;

	pParent = pCur = pRoot = NULL;

	// ORDBLIST.TXT�� ���� DB List�� �̾ƿ´�.
	if( cOrdbList && cOrdbList->ReadDBInfo() )
	{
		CDBInfo	*db;
		int count = (int) cOrdbList->m_List.GetCount();

		for (int i = 0; i < count; i++)
		{
			pCur = NULL;
			db = (CDBInfo *)cOrdbList->m_List.GetAt(cOrdbList->m_List.FindIndex(i));

			// DBName
			sDBName = (char *)LPCSTR(db->m_dbname);

			// Root�� ���
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

	// �۾��� ����� �����Ѵ�.
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



//	���� ���� ���� DB�� Process�� �˻��� ���Ͽ�, Catch�Ѵ�.
DBNAMEPtr_t CCUBRIDManage::pGetDBListProcess()
{
	// �ð��� ���� ������ �����Ѵ�.
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
	// sFullName�� �����Ͽ�, sCatchResStr�̶�� ������ �����Ѵ�.
	bCatchResult( sFullName );

	delete sPath;


	if( !sCatchResStr || strlen( sCatchResStr ) <= 0 )
		return NULL;


	// DB �̸��� �̿��Ͽ�, Linked List�� �����Ѵ�.
	char* sTmp = sCatchResStr;
	char* sStrRes;
	int dNum = 1;
	DBNAMEPtr_t pParent, pChild;

	// " Server"�� Key�ϴ� String�� ���� ����Ʈ�� ã�´�.
	sTmp = strstr( sTmp, " Server" );
	if( !sTmp ) return NULL;

	// String���� DB Name�� �����´�.
	sStrRes = sGetName( sTmp );
	if( !sStrRes ) return NULL;

	// DB List�� �����Ѵ�.
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



// DB Name�� ���� List�� �����ϱ� ���� ���Ҹ� �����.
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

	int dSize = (int) (sEnd - sTmp);

	char* sResult = new char[ dSize + 1 ];
	memset( sResult, 0x00, dSize + 1 );
	memcpy( sResult, sTmp, dSize );

	return sResult;
}


/* ����� DB ��, ã���� �ϴ� DB�� �ִ��� Ȯ���Ѵ� */
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









// 2002�� 10�� 19�� By KingCH
// _chdir(), getenv()�� �Լ��� ����ϱ� ����
#include <direct.h>
#include <stdlib.h>


/*
	CUBRID�� ���õ� Process�� �����Ѵ�.
*/
bool CCUBRIDManage::bStartMaster()
{
	CManageRegistry* cReg = new CManageRegistry( "CUBRID" );
	char* sPath = cReg->sGetItem( "ROOT_PATH" );

	// 2002�� 10�� 18�� By KingCH
	// EasyManager Server�� Root Directory ������ �����´�.
	// Working Directory�� �ٲ㼭 ������ ��Ű��, Log�� ���� �޴��� ���� �ʴ´�.
	char* sWorkDir = getenv( "CUBRID_DATABASES" );


	// 2002�� 10�� 18�� By KingCH
	// UNITOOL_EMGR�� ȯ�� ������ ������, Register�� �о, sWorkDir�� Ȱ���Ѵ�.
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

	// 2002�� 10�� 18�� By KingCH
	// EasyManager Server�� Root Directory ������ �����´�.
	// Working Directory�� �ٲ㼭 ������ ��Ű��, Log�� ���� �޴��� ���� �ʴ´�.
	char* sWorkDir = getenv( "CUBRID_DATABASES" );


	// 2002�� 10�� 18�� By KingCH
	// UNITOOL_EMGR�� ȯ�� ������ ������, Register�� �о, sWorkDir�� Ȱ���Ѵ�.
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








