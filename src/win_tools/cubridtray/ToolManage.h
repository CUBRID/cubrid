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

// ToolManage.h: interface for the CUniToolManage class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_UNITOOLMANAGE_H__058665D6_5812_4FE1_8EE2_647FC440052E__INCLUDED_)
#define AFX_UNITOOLMANAGE_H__058665D6_5812_4FE1_8EE2_647FC440052E__INCLUDED_



#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000


//#include "unitray_comm.h"


class CUniToolManage
{
public:
  CUniToolManage ();
  virtual ~ CUniToolManage ();

  char *cubridmanager_path;

  bool bStartVSQL ();
  bool bStartEasyManage ();

  bool bCheckInstallVSQL ();
  bool bCheckInstallEasyManage ();
};

#endif // !defined(AFX_UNITOOLMANAGE_H__058665D6_5812_4FE1_8EE2_647FC440052E__INCLUDED_)
