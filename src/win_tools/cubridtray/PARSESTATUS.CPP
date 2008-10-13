// ParseStatus.cpp: implementation of the CParseStatus class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "unitray.h"
#include "ParseStatus.h"

#include "ManageRegistry.h"

/*
#include "lang.h"
#include "MainFrm.h"
*/

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CParseStatus::CParseStatus()
{

}

CParseStatus::~CParseStatus()
{

}






////////////////////////////////////////////////////////////////////////////
// DB Name을 가져와서 Linked List를 구성한다.
////////////////////////////////////////////////////////////////////////////

/*
char* CParseStatus::sCatchResult( char* sCmd )
{
	CLang	theLang;
	CVersionRedirect	g_CUBRIDVersion;

	char* sResult;

	if( !g_CUBRIDVersion.StartChildProcess( sCmd ) )
		return NULL;

	Sleep(500);

	sResult = (char *)LPCSTR(g_CUBRIDVersion.m_version);

	return sResult;
}
*/

DBNAME_Ptr CParseStatus::pGetDBList( )
{
	CManageRegistry* cReg = new CManageRegistry( "CUBRID" );
	char* sPath = cReg->sGetItem( sRegKey[REG_ROOTPATH] );

	if( !sPath )
	{
		delete cReg;
		return NULL;
	}

	char* sFullName = new char[ strlen( sPath ) + strlen( "commdb.exe" ) + 5 ];
	memset( sFullName, 0x00, strlen( sPath ) + strlen( "commdb.exe" ) + 5 );
	sprintf( sFullName, "%s\\commdb.exe -P", sPath );

	char* sResult = sCatchResult( sFullName );

	if( !sResult )
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
	DBNAME_Ptr pRoot, pParent, pChild;

	sTmp = strstr( sTmp, "Server " );
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
		sTmp = strstr( sTmp, "Server " );
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


char* CParseStatus::sGetName( char* sStr )
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



DBNAME_Ptr CParseStatus::pMakeList( unsigned int dNum, char* sName )
{
	if( !sName ) return NULL;

	DBNAME_Ptr pName = new DBNAME_t;
	memset( pName, 0x00, sizeof( DBNAME_t ) );

	pName->sName = sName;
	pName->dNum = dNum;
	pName->next = NULL;

	return pName;
}

////////////////////////////////////////////////////////////////////////////
// DB Name을 가져와서 Linked List를 구성하는 작업 완료
////////////////////////////////////////////////////////////////////////////



bool CParseStatus::bEasyManagerServer()
{
	FILE*	f_ems_auto_pid;
	FILE*	f_ems_js_pid;
	int		ems_auto_pid,ems_js_pid;

	ems_auto_pid = ems_js_pid = -1;

	f_ems_auto_pid = fopen("logs\\ems_auto.pid","r");
	f_ems_js_pid = fopen("logs\\ems_js.pid","r");
	if( (f_ems_auto_pid) && (f_ems_js_pid) )
	{
		fscanf(f_ems_auto_pid,"%d",&ems_auto_pid);
		fscanf(f_ems_js_pid,"%d",&ems_js_pid);
		fclose(f_ems_auto_pid);	fclose(f_ems_js_pid);

		if((ems_auto_pid >=0) && (ems_js_pid>=0))
		{
			if(OpenProcess(PROCESS_ALL_ACCESS,false,ems_auto_pid)
				||OpenProcess(PROCESS_ALL_ACCESS,false,ems_js_pid) )
			{
				// Server Is Running
				return true;
			}
		}
	}

	// Server Is Not Running
	return false;
}


bool CParseStatus::bStatusUniCAS()
{
	CManageRegistry* cReg = new CManageRegistry( "CUBRIDCAS" );
	char* sPath = cReg->sGetItem( sRegKey[REG_ROOTPATH] );

	if( !sPath )
	{
		delete cReg;
		return NULL;
	}

	char* sFullName = new char[ strlen( sPath ) + strlen( "monitor.exe" ) + 5 ];
	memset( sFullName, 0x00, strlen( sPath ) + strlen( "monitor.exe" ) + 5 );
	sprintf( sFullName, "%s\\monitor.exe", sPath );

	char* sResult = sCatchResult( sFullName );

	if( !sResult )
	{
		delete sFullName;
		delete sPath;
		delete cReg;
		return NULL;
	}

	char* sTmp;

	// DB 이름을 이용하여, Linked List를 구성한다.
	sTmp = strstr( sResult, "broker" );
	if( !sTmp )
	{
		delete sResult;
		delete sFullName;
		delete sPath;
		delete cReg;
		return false;
	}

	delete sResult;
	delete sFullName;
	delete sPath;
	delete cReg;
	return true;
}

char* CParseStatus::sCatchResult( char* sCmd )
{
	CLang	theLang;
	CVersionRedirect	g_CUBRIDVersion;

	char* sResult;

	if( !g_CUBRIDVersion.StartChildProcess( sCmd ) )
		return NULL;

	Sleep(500);

	sResult = (char *)LPCSTR(g_CUBRIDVersion.m_version);

	return sResult;
}


bool CParseStatus::bStatusMaster()
{
	CManageRegistry* cReg = new CManageRegistry( "CUBRID" );
	char* sPath = cReg->sGetItem( sRegKey[REG_ROOTPATH] );

	if( !sPath )
	{
		delete cReg;
		return NULL;
	}

	char* sFullName = new char[ strlen( sPath ) + strlen( "commdb.exe" ) + 5 ];
	memset( sFullName, 0x00, strlen( sPath ) + strlen( "commdb.exe" ) + 5 );
	sprintf( sFullName, "%s\\commdb.exe -P", sPath );

	char* sResult = sCatchResult( sFullName );

	if( !sResult )
	{
		delete sFullName;
		delete sPath;
		delete cReg;
		return NULL;
	}


	char* sTmp;

	sTmp = strstr( sResult, "running." );
	if( !sTmp )
	{
		delete sResult;
		delete sFullName;
		delete sPath;
		delete cReg;
		return false;
	}

	delete sResult;
	delete sFullName;
	delete sPath;
	delete cReg;
	return true;
}




bool CParseStatus::bStatusServer()
{
	CManageRegistry* cReg = new CManageRegistry( "CUBRID" );
	char* sPath = cReg->sGetItem( sRegKey[REG_ROOTPATH] );

	if( !sPath )
	{
		delete cReg;
		return NULL;
	}

	char* sFullName = new char[ strlen( sPath ) + strlen( "commdb.exe" ) + 5 ];
	memset( sFullName, 0x00, strlen( sPath ) + strlen( "commdb.exe" ) + 5 );
	sprintf( sFullName, "%s\\commdb.exe -P", sPath );

	char* sResult = sCatchResult( sFullName );

	if( !sResult )
	{
		delete sFullName;
		delete sPath;
		delete cReg;
		return NULL;
	}


	char* sTmp;

	sTmp = strstr( sResult, " Server" );
	if( !sTmp )
	{
		delete sResult;
		delete sFullName;
		delete sPath;
		delete cReg;
		return false;
	}

	delete sResult;
	delete sFullName;
	delete sPath;
	delete cReg;
	return true;
}


/*
bool CParseStatus::bStatusProcess( DREGNAME_t dProdNum )
{
	CManageRegistry* cReg = new CManageRegistry( sRegName[dProdNum] );
	char* sPath = cReg->sGetItem( sRegKey[REG_ROOTPATH] );

	if( !sPath )
	{
		delete cReg;
		return NULL;
	}

	char* sFullName = new char[ strlen( sPath ) + strlen( sProcessCmd[dProdNum] ) + 5 ];
	memset( sFullName, 0x00, strlen( sPath ) + strlen( sProcessCmd[dProdNum] ) + 5 );
	sprintf( sFullName, "%s\\%s", sPath, sProcessCmd[dProdNum] );

	char* sResult = sCatchResult( sFullName );

	if( !sResult )
	{
		delete sFullName;
		delete sPath;
		delete cReg;
		return NULL;
	}


	char* sTmp;

	sTmp = strstr( sResult, sProcessKey[dProdNum] );
	if( !sTmp )
	{
		delete sResult;
		delete sFullName;
		delete sPath;
		delete cReg;
		return false;
	}

	delete sResult;
	delete sFullName;
	delete sPath;
	delete cReg;
	return true;
}
*/














