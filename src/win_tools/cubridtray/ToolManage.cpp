// UniToolManage.cpp: implementation of the CUniToolManage class.
//
//////////////////////////////////////////////////////////////////////


#include "stdafx.h"
#include "cubridtray.h"
#include "ToolManage.h"

#include "ManageRegistry.h"

//#include "unitray_comm.h"



#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CUniToolManage::CUniToolManage()
{
	cubridmanager_path = NULL;
}

CUniToolManage::~CUniToolManage()
{
	if (cubridmanager_path != NULL)
	{
		free(cubridmanager_path);
	}
}

bool CUniToolManage::bCheckInstallEasyManage()
{
	bool ret_val;

	if (cubridmanager_path == NULL)
	{
		CManageRegistry* cReg = new CManageRegistry( "cmclient" );
		char* sRootPath = cReg->sGetItem( "ROOT_PATH" );
		delete cReg;
		if (sRootPath != NULL)
		{
			cubridmanager_path = (char*) malloc (strlen(sRootPath)+1);
			if (cubridmanager_path != NULL) {
				strcpy(cubridmanager_path, sRootPath);
			}

			delete sRootPath;
			return true;
		}
		else 
		{
			return false;
		}
	}

	return true;
}


// 2002년 10월 19일 By KingCH
// _chdir() 함수를 사용하기 위함
#include <direct.h>
#include <stdlib.h>

bool CUniToolManage::bStartEasyManage()
{
	char java_root_path[2048];
	if (cubridmanager_path == NULL)
	{
		CManageRegistry* cReg = new CManageRegistry( "cmclient" );
		char* sRootPath = cReg->sGetItem( "ROOT_PATH" );

		delete cReg;

		if( !sRootPath )
		{
			return false;
		}
		
		cubridmanager_path = (char*) malloc (strlen(sRootPath) + 1);
		
		if (cubridmanager_path == NULL)
		{
			return false;
		}

		strcpy(cubridmanager_path, sRootPath);
		delete sRootPath;
	}

	int dchdir = _chdir( cubridmanager_path );

	char sFullName[1024];
	memset( sFullName, 0x00, sizeof( sFullName ) );

	CManageRegistry* jReg = new CManageRegistry("");

	if (jReg->GetJavaRootPath(java_root_path)) {
		sprintf( sFullName, "\"%s\\%s\" -vm \"%s\\bin\\javaw.exe\"", cubridmanager_path, "cubridmanager.exe", java_root_path);
	}
	else {
		sprintf( sFullName, "\"%s\\%s\"", cubridmanager_path, "cubridmanager.exe" );
	}

	delete jReg;

	int errorno = WinExec( sFullName, SW_SHOW );

	return true;
}

bool CUniToolManage::bCheckInstallVSQL()
{
	// TODO: Add your command handler code here
	CManageRegistry* cReg = new CManageRegistry( "Visual-SQL" );
	char* sRootPath = cReg->sGetItem( "ROOT_PATH" );
	delete cReg;

	if( !sRootPath )
		return false;

	delete sRootPath;
	return true;
}


bool CUniToolManage::bStartVSQL()
{
	// TODO: Add your command handler code here
	CManageRegistry* cReg = new CManageRegistry( "Visual-SQL" );
	char* sRootPath = cReg->sGetItem( "ROOT_PATH" );
	delete cReg;

	if( !sRootPath )
		return false;

	int dchdir = _chdir( sRootPath );

	char sFullName[1024];
	memset( sFullName, 0x00, sizeof( sFullName ) );
	sprintf( sFullName, "%s\\%s", sRootPath, "Visual-SQL.exe" );

	int errorno = WinExec( sFullName, SW_SHOW );

	delete sRootPath;

	return true;
}







