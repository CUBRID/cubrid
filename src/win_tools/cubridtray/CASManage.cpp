/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA 
 *
 */

// UniCASManage.cpp: implementation of the CUniCASManage class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "cubridtray.h"
#include "CASManage.h"

#include "ManageRegistry.h"
#include "CommonMethod.h"
#include "uc_admin.h"
//#include "unitray_comm.h"


#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CUniCASManage::CUniCASManage()
{
//	bUNICAS = bCheckUniCAS();
}

CUniCASManage::~CUniCASManage()
{
}



// 2002년 10월 19일 By KingCH
// _chdir(), getenv()를 함수를 사용하기 위함
#include <direct.h>
#include <stdlib.h>

bool CUniCASManage::bStartUniCAS()
{
	CManageRegistry* cReg = new CManageRegistry( "CUBRIDCAS" );
	char* sPath = cReg->sGetItem( "ROOT_PATH" );

	// 2002년 10월 18일 By KingCH
	// EasyManager Server의 Root Directory 정보를 가져온다.
	// Working Directory를 바꿔서 실행을 시키면, Log가 시작 메뉴에 남지 않는다.
	char* sWorkDir = getenv( "CUBRID_BROKER" );


	// 2002년 10월 18일 By KingCH
	// UNITOOL_EMGR가 환경 변수에 없으면, Register를 읽어서, sWorkDir로 활용한다.
	if( !sWorkDir ) sWorkDir = sPath;
	int dchdir = _chdir( sWorkDir );

	delete cReg;

	if( !sPath ) return false;

	char sFullName[1024];
	memset( sFullName, 0x00, sizeof( sFullName ) );
	sprintf( sFullName, "%s\\bin\\uc start", sPath );

	int dRes = WinExec( sFullName, SW_HIDE );
	delete sPath;
	if( dRes < 31 ) return false;

	if( bCheckUniCAS() ) return true;
	return false;
}

bool CUniCASManage::bStopUniCAS()
{
	CManageRegistry* cReg = new CManageRegistry( "UNICAS" );
	char* sPath = cReg->sGetItem( "ROOT_PATH" );
	delete cReg;

	if( !sPath ) return false;

	char sFullName[1024];
	memset( sFullName, 0x00, sizeof( sFullName ) );
	sprintf( sFullName, "%s\\bin\\uc stop", sPath );

	int dRes = WinExec( sFullName, SW_HIDE );
	delete sPath;

	if( dRes < 31 )	return false;

	if( bCheckUniCAS() ) return false;
	return true;
}

bool CUniCASManage::bRestartUniCAS()
{
	CManageRegistry* cReg = new CManageRegistry( "UNICAS" );
	char* sPath = cReg->sGetItem( "ROOT_PATH" );
	delete cReg;

	if( !sPath ) return false;

	char sFullName[1024];
	memset( sFullName, 0x00, sizeof( sFullName ) );
	sprintf( sFullName, "%s\\bin\\uc restart", sPath );

	int dRes = WinExec( sFullName, SW_HIDE );
	delete sPath;
	if( dRes < 31 ) return false;

	if( bCheckUniCAS() ) return true;
	return false;
}



#include "Process.h"

bool CUniCASManage::bCheckUniCAS()
{
	CManageRegistry* cReg = new CManageRegistry( "UNICAS" );
	char* sPath = cReg->sGetItem( "ROOT_PATH" );
	delete cReg;

	if( !sPath )
		return false;

	char sFullName[1024];
	memset( sFullName, 0x00, sizeof( sFullName ) );
	sprintf( sFullName, "%s\\bin\\%s", sPath, "cas.exe" );

	CProcess* cProc = new CProcess();
	unsigned long lRes = cProc->FindProcess( sFullName );
	delete cProc;

	if( lRes <= 0 ) return false;
	return true;
}


bool CUniCASManage::bInstallStatus()
{
	CManageRegistry* cReg = new CManageRegistry( "UNICAS" );
	char* sPath = cReg->sGetItem( "ROOT_PATH" );
	delete cReg;

	if( !sPath || strlen( sPath ) <= 0 )
		return false;

	return true;
}


bool CUniCASManage::bStatusUniCAS()
{
	CManageRegistry* cReg = new CManageRegistry( "UNICAS" );
	char* sPath = cReg->sGetItem( "ROOT_PATH" );
	delete cReg;

	if( !sPath )
		return false;

	int dSize = strlen( sPath ) + strlen( "monitor.exe" );
	char* sFullName = new char[ dSize + 5 ];
	memset( sFullName, 0x00, dSize + 5 );
	sprintf( sFullName, "%s\\bin\\%s", sPath, "monitor.exe" );

	CCommonMethod* cComMeth = new CCommonMethod();
	char* sResult = cComMeth->sCatchResult( sFullName );

	delete cComMeth;
	delete sFullName;
	delete sPath;

	if( !sResult || strlen( sResult ) <= 0 )
		return false;

	char* sTmp;

	// DB 이름을 이용하여, Linked List를 구성한다.
	sTmp = strstr( sResult, "broker" );
	delete sResult;

	if( !sTmp )
	{
		bUNICAS = false;
		return false;
	}

	bUNICAS = true;
	return true;
}












