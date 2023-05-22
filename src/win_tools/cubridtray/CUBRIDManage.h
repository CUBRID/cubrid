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

// CUBRIDManage.h: interface for the CCUBRIDManage class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_CUBRIDMANAGE_H__DDE22E18_7BDE_4FBA_8EF5_B3CFEEBE2DF4__INCLUDED_)
#define AFX_CUBRIDMANAGE_H__DDE22E18_7BDE_4FBA_8EF5_B3CFEEBE2DF4__INCLUDED_



#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <time.h>


typedef struct _db_name_
{
  unsigned int dNum;
//      char* sName;
  char sName[50];
  bool bStart;
  struct _db_name_ *next;
} DBNAME_t, *DBNAMEPtr_t;




class CCUBRIDManage
{
private:
  DBNAMEPtr_t pStopDBList;
  DBNAMEPtr_t pStartDBList;

  char sCatchResStr[5120];



  bool bCatchResult (char *sCmd);
  char *sCatchResult (char *sCmd);
  DBNAMEPtr_t pMakeList (unsigned int dNum, char *sName);
  DBNAMEPtr_t pMakeList (DBNAMEPtr_t pParent, unsigned int dNum, char *sName);
  char *sGetName (char *sStr);
  bool bCompareDB (char *sDBName, DBNAMEPtr_t pDBList);

  bool bCUBRID;
  bool bMASTER;

  DBNAMEPtr_t pGetDBListProcess ();
  DBNAMEPtr_t pGetDBListFile ();
  DBNAMEPtr_t pCompareDB ();

  DBNAMEPtr_t pFileDBList;
  DBNAMEPtr_t pExecuteDBList;

  // ORDBLIST.txt�� ���������� �˻��� ���� �ð� ������ ������ �ð� ������ ��´�.
  time_t pPreTimeFileList;
  time_t pCurTimeFileList;

  // DB Process�� ���������� �˻��� ���� �ð� ������ ������ �ð� ������ ��´�.
  time_t pPreTimeProcessList;
  time_t pCurTimeProcessList;

  bool bCheckRefreshDBList ();
  bool bRefreshDBList ();

  DBNAMEPtr_t pCheckExecuteDBList ();
public:
    CCUBRIDManage ();
    virtual ~ CCUBRIDManage ();


  bool bStatusMaster ();
  bool bCheckMaster ();
  bool bStatusServer ();
  bool bCheckServer ();


  bool bStartCUBRID (char *sdbname);
  bool bStartMaster ();

  bool bStopCUBRID (char *sdbname);
  bool bStopMaster ();



  DBNAMEPtr_t pGetStartDBList ();
  DBNAMEPtr_t pGetStopDBList ();


  bool bInstallStatus ();


  bool bDestoryDBList (DBNAMEPtr_t pDBList);


  DBNAMEPtr_t pReqStopDBList ();
  DBNAMEPtr_t pReqStartDBList ();

};

#endif // !defined(AFX_CUBRIDMANAGE_H__DDE22E18_7BDE_4FBA_8EF5_B3CFEEBE2DF4__INCLUDED_)
