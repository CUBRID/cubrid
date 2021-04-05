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



// 2002�� 10�� 19�� By KingCH
// _chdir(), getenv()�� �Լ��� ����ϱ� ����
#include <direct.h>
#include <stdlib.h>

bool CUniCASManage::bStartUniCAS()
{
  CManageRegistry *cReg = new CManageRegistry ( "CUBRIDCAS" );
  char *sPath = cReg->sGetItem ( "ROOT_PATH" );

  // 2002�� 10�� 18�� By KingCH
  // EasyManager Server�� Root Directory ������ �����´�.
  // Working Directory�� �ٲ㼭 ������ ��Ű��, Log�� ���� �޴��� ���� �ʴ´�.
  char *sWorkDir = getenv ( "CUBRID_BROKER" );


  // 2002�� 10�� 18�� By KingCH
  // UNITOOL_EMGR�� ȯ�� ������ ������, Register�� �о, sWorkDir�� Ȱ���Ѵ�.
  if ( !sWorkDir )
    {
      sWorkDir = sPath;
    }
  int dchdir = _chdir ( sWorkDir );

  delete cReg;

  if ( !sPath )
    {
      return false;
    }

  char sFullName[1024];
  memset ( sFullName, 0x00, sizeof ( sFullName ) );
  sprintf ( sFullName, "%s\\bin\\uc start", sPath );

  int dRes = WinExec ( sFullName, SW_HIDE );
  delete sPath;
  if ( dRes < 31 )
    {
      return false;
    }

  if ( bCheckUniCAS() )
    {
      return true;
    }
  return false;
}

bool CUniCASManage::bStopUniCAS()
{
  CManageRegistry *cReg = new CManageRegistry ( "UNICAS" );
  char *sPath = cReg->sGetItem ( "ROOT_PATH" );
  delete cReg;

  if ( !sPath )
    {
      return false;
    }

  char sFullName[1024];
  memset ( sFullName, 0x00, sizeof ( sFullName ) );
  sprintf ( sFullName, "%s\\bin\\uc stop", sPath );

  int dRes = WinExec ( sFullName, SW_HIDE );
  delete sPath;

  if ( dRes < 31 )
    {
      return false;
    }

  if ( bCheckUniCAS() )
    {
      return false;
    }
  return true;
}

bool CUniCASManage::bRestartUniCAS()
{
  CManageRegistry *cReg = new CManageRegistry ( "UNICAS" );
  char *sPath = cReg->sGetItem ( "ROOT_PATH" );
  delete cReg;

  if ( !sPath )
    {
      return false;
    }

  char sFullName[1024];
  memset ( sFullName, 0x00, sizeof ( sFullName ) );
  sprintf ( sFullName, "%s\\bin\\uc restart", sPath );

  int dRes = WinExec ( sFullName, SW_HIDE );
  delete sPath;
  if ( dRes < 31 )
    {
      return false;
    }

  if ( bCheckUniCAS() )
    {
      return true;
    }
  return false;
}



#include "Process.h"

bool CUniCASManage::bCheckUniCAS()
{
  CManageRegistry *cReg = new CManageRegistry ( "UNICAS" );
  char *sPath = cReg->sGetItem ( "ROOT_PATH" );
  delete cReg;

  if ( !sPath )
    {
      return false;
    }

  char sFullName[1024];
  memset ( sFullName, 0x00, sizeof ( sFullName ) );
  sprintf ( sFullName, "%s\\bin\\%s", sPath, "cas.exe" );

  CProcess *cProc = new CProcess();
  unsigned long lRes = cProc->FindProcess ( sFullName );
  delete cProc;

  if ( lRes <= 0 )
    {
      return false;
    }
  return true;
}


bool CUniCASManage::bInstallStatus()
{
  CManageRegistry *cReg = new CManageRegistry ( "UNICAS" );
  char *sPath = cReg->sGetItem ( "ROOT_PATH" );
  delete cReg;

  if ( !sPath || strlen ( sPath ) <= 0 )
    {
      return false;
    }

  return true;
}


bool CUniCASManage::bStatusUniCAS()
{
  CManageRegistry *cReg = new CManageRegistry ( "UNICAS" );
  char *sPath = cReg->sGetItem ( "ROOT_PATH" );
  delete cReg;

  if ( !sPath )
    {
      return false;
    }

  int dSize = strlen ( sPath ) + strlen ( "monitor.exe" );
  char *sFullName = new char[ dSize + 5 ];
  memset ( sFullName, 0x00, dSize + 5 );
  sprintf ( sFullName, "%s\\bin\\%s", sPath, "monitor.exe" );

  CCommonMethod *cComMeth = new CCommonMethod();
  char *sResult = cComMeth->sCatchResult ( sFullName );

  delete cComMeth;
  delete[] sFullName;
  delete sPath;

  if ( !sResult || strlen ( sResult ) <= 0 )
    {
      return false;
    }

  char *sTmp;

  // DB �̸��� �̿��Ͽ�, Linked List�� �����Ѵ�.
  sTmp = strstr ( sResult, "broker" );
  delete sResult;

  if ( !sTmp )
    {
      bUNICAS = false;
      return false;
    }

  bUNICAS = true;
  return true;
}












