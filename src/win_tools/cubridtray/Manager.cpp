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

// Manager.cpp: implementation of the CEasyManager class.
//
//////////////////////////////////////////////////////////////////////


#include "stdafx.h"
#include <direct.h>
#include <stdlib.h>
#include "cubridtray.h"
#include "Manager.h"

#include "ManageRegistry.h"
#include "Process.h"


#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CEasyManager::CEasyManager()
{

}

CEasyManager::~CEasyManager()
{

}

bool CEasyManager::bCheckEasyManagerServer()
{
	if( bCheckEMSAuto() && bCheckEMSJS() )
		return true;

	bStopEasyManagerServer();
	return false;
}

bool CEasyManager::bInstallStatus()
{
	// TODO: Add your command handler code here
	CManageRegistry* cReg = new CManageRegistry( "cmserver" );
	char* sRootPath;

	sRootPath = cReg->sGetItem( "ROOT_PATH" );

	if( !sRootPath )
	{
		delete cReg;
		return false;
	}

	delete cReg;
	return true;
}


bool CEasyManager::bStartEasyManagerServer()
{
	CManageRegistry* cReg = new CManageRegistry( "CUBRID" );
	char* sPath = cReg->sGetItem( "ROOT_PATH" );

	char sFullName[1024];
	memset( sFullName, 0x00, sizeof( sFullName ) );
	sprintf( sFullName, "%s\\bin\\ctrlService -start", sPath );

	int dRes = WinExec( sFullName, SW_HIDE );

	delete sPath;
	delete cReg;

	return true;
}


bool CEasyManager::bStopEasyManagerServer()
{
	CManageRegistry* cReg = new CManageRegistry( "CUBRID" );
	char* sPath = cReg->sGetItem( "ROOT_PATH" );

	char sFullName[1024];
	memset( sFullName, 0x00, sizeof( sFullName ) );
	sprintf( sFullName, "%s\\bin\\ctrlService -stop", sPath );

	int dRes = WinExec( sFullName, SW_HIDE );

	delete sPath;
	delete cReg;

	return true;
}

bool CEasyManager::bCheckEMSAuto()
{
	CManageRegistry* cReg = new CManageRegistry( "cmserver" );
	char* sPath = cReg->sGetItem( "ROOT_PATH" );
	char* sCmd = "bin\\cub_auto.exe";

	if( !sPath )
	{
		delete cReg;
		return false;
	}

	int dSize = strlen( sPath ) + strlen( sCmd );
	char* sFullName = new char[ dSize + 5 ];
	memset( sFullName, 0x00, dSize + 5 );
	sprintf( sFullName, "%s\\%s", sPath, sCmd );

	CProcess* cProc = new CProcess();
	unsigned long lRes = cProc->FindProcess( sFullName );

	delete cProc;
	delete cReg;

	if( lRes <= 0 ) return false;
	return true;
}

bool CEasyManager::bCheckEMSJS()
{
	CManageRegistry* cReg = new CManageRegistry( "cmserver" );
	char* sPath = cReg->sGetItem( "ROOT_PATH" );
	char* sCmd = "bin\\cub_js.exe";

	if( !sPath )
	{
		delete cReg;
		return false;
	}

	int dSize = strlen( sPath ) + strlen( sCmd );
	char* sFullName = new char[ dSize + 5 ];
	memset( sFullName, 0x00, dSize + 5 );
	sprintf( sFullName, "%s\\%s", sPath, sCmd );

	CProcess* cProc = new CProcess();
	unsigned long lRes = cProc->FindProcess( sFullName );

	delete cProc;
	delete cReg;

	if( lRes <= 0 ) return false;
	return true;
}

bool CEasyManager::bEasyManagerServerCheckOnly()
{
	if( bCheckEMSAuto() && bCheckEMSJS() )
		return true;
	else
		return false;
}
