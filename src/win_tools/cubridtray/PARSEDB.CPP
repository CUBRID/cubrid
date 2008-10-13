// ParseDB.cpp: implementation of the CParseDB class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "unitray.h"
#include "ParseDB.h"


#include "ManageRegistry.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CParseDB::CParseDB()
{

}

CParseDB::~CParseDB()
{

}






/*
	실행된 DataBase 목록을 Linked List로 구성하여, 반환한다.
*/
DBNAMEPtr_t CParseDB::pGetDBList()
{
	CManageRegistry* cReg = new CManageRegistry( "CUBRID" );
	char* sPath = cReg->sGetItem( "ROOT_PATH" );

	if( !sPath )
	{
		delete cReg;
		return NULL;
	}

	int dSize = strlen( sPath ) + strlen( "commdb.exe -P" );
	char* sFullName = new char[ dSize + 5 ];
	memset( sFullName, 0x00, dSize + 5 );
	sprintf( sFullName, "%s\\%s", sPath, "commdb.exe -P" );

	char* sResult = sCatchResult( sFullName );

	if( !sResult || strlen( sResult ) <= 0 )
	{
		delete sFullName;
		delete sPath;
		delete cReg;
		return NULL;
	}



	// DB 이름을 이용하여, Linked List를 구성한다.
	char* sTmp = sResult;
	char* sStrRes;
	int dNum = 1;
	DBNAMEPtr_t pRoot, pParent, pChild;

	sTmp = strstr( sResult, " Server" );
	if( !sTmp )
	{
		delete sResult;
		delete sFullName;
		delete sPath;
		delete cReg;
		return NULL;
	}

	sStrRes = sGetName( sTmp );
	if( !sStrRes )
	{
		delete sTmp;
		delete sResult;
		delete sFullName;
		delete sPath;
		delete cReg;
		return NULL;
	}

	pRoot = pMakeList( dNum ++, sStrRes );
	if( !pRoot )
	{
		delete sStrRes;
		delete sTmp;
		delete sResult;
		delete sFullName;
		delete sPath;
		delete cReg;
		return NULL;
	}

	sTmp = strchr( sTmp, ',' );

	pParent = pRoot;

	while( 1 )
	{
		if( !sTmp ) break;
		sTmp = strstr( sTmp, " Server" );
		if( !sTmp ) break;
		sStrRes = sGetName( sTmp );
		if( !sResult ) break;
		pChild = pMakeList( dNum ++, sStrRes );
		if( !pChild ) break;
		sTmp = strchr( sTmp, ',' );
		if( !sTmp ) break;
		pParent->next = pChild;
		pParent = pChild;
	}


	delete sTmp;
	delete sResult;
	delete sFullName;
	delete sPath;
	delete cReg;

	return pRoot;
}

DBNAMEPtr_t CParseDB::pMakeList( unsigned int dNum, char* sName )
{
	if( !sName ) return NULL;

	DBNAMEPtr_t pName = new DBNAME_t;
	memset( pName, 0x00, sizeof( DBNAME_t ) );

	pName->sName = sName;
	pName->dNum = dNum;
	pName->next = NULL;

	return pName;
}

char* CParseDB::sCatchResult( char* sCmd )
{
	CLang	theLang;
	CVersionRedirect	g_CUBRIDVersion;

	char* sResult;

	if( !g_CUBRIDVersion.StartChildProcess( sCmd ) )
		return NULL;

	Sleep(500);

	sResult = new char[ strlen( ( char * )LPCSTR( g_CUBRIDVersion.m_version ) + 1 ) ];
	memset( sResult, 0x00, strlen( ( char * )LPCSTR( g_CUBRIDVersion.m_version ) + 1 ) );
	strcpy( sResult, ( char * )LPCSTR( g_CUBRIDVersion.m_version ) );

TRACE2( "Catch Message( %s ) : %s\n", sCmd, sResult );

	return sResult;
}


char* CParseDB::sGetName( char* sStr )
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
bool CParseDB::bCompareDB( char* sDBName, DBNAMEPtr_t pDBList )
{
	if( !sDBName || !pDBList ) return false;

	DBNAMEPtr_t pCur = pDBList;

	while( 1 )
	{
		TRACE2( "Compare String -> [%s] : [%s]\n", pCur->dNum, pCur->sName );

		if( strcmp( pCur->sName, sDBName ) == 0 ) return true;

		if( !pCur->next ) break;

		pCur = pCur->next;
	}

	return false;
}

bool CParseDB::bDestoryDBList( DBNAMEPtr_t pDBList )
{
	if( !pDBList ) return true;

	DBNAMEPtr_t pRoot = pDBList;
	DBNAMEPtr_t pCur  = pRoot;
	DBNAMEPtr_t pNext;

	while( 1 )
	{
		pNext = pCur->next;

		delete pCur->sName;
		delete pCur;

		if( !pNext ) break;
		pCur = pNext;
	}

	return true;
}


