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
// CommonMethod.cpp: implementation of the CCommonMethod class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "cubridtray.h"
#include "CommonMethod.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif


char* sRegName[] = { "CUBRID", "CUBRID", "CUBRIDCAS", "cmserver", "cmserver", "Visual-SQL", "CUBRID" };
char* sProcessKey[] = { "running.", " Server", "broker", NULL, NULL, NULL, NULL };
char* sProcessCmd[] = { "commdb.exe -P", "commdb.exe -P", "monitor.exe", NULL, "Easy-Manager.exe", "Visual-SQL.exe", "commdb.exe -I" };
char* sProductName[] = { "Master", "CUBRID", "CUBRIDCas", "cmserver", "cmclient", "Visual-SQL", "CUBRID" };


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CCommonMethod::CCommonMethod()
{

}

CCommonMethod::~CCommonMethod()
{

}


///////////////////////////////
#include "lang.h"
#include "MainFrm.h"
/////////////////////////////// sCatchResult¸¦ À§ÇÑ include

char* CCommonMethod::sCatchResult( char* sCmd )
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



///////////////////////////////
#include "ManageRegistry.h"
///////////////////////////////
bool CCommonMethod::bStatusProcess( DREGNAME_t dProdNum )
{
	CManageRegistry* cReg = new CManageRegistry( sRegName[dProdNum] );
//	char* sPath = cReg->sGetItem( sRegKey[REG_ROOTPATH] );
	char* sPath = cReg->sGetItem( "ROOT_PATH" );

	if( !sPath )
	{
		delete cReg;
		return false;
	}

	int dSize = strlen( sPath ) + strlen( sProcessCmd[dProdNum] );
	char* sFullName = new char[ dSize + 5 ];
	memset( sFullName, 0x00, dSize + 5 );
	sprintf( sFullName, "%s\\%s", sPath, sProcessCmd[dProdNum] );

	char* sResult = sCatchResult( sFullName );

	if( !sResult )
	{
		delete[] sFullName;
		delete sPath;
		delete cReg;
		return false;
	}


	char* sTmp;

	sTmp = strstr( sResult, sProcessKey[dProdNum] );
	if( !sTmp )
	{
		delete sResult;
		delete[] sFullName;
		delete sPath;
		delete cReg;
		return false;
	}

	delete sResult;
	delete[] sFullName;
	delete sPath;
	delete cReg;
	return true;
}




