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

// CASManage.h: interface for the CUniCASManage class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_UNICASMANAGE_H__359FF1E2_13DD_4BE7_BADC_D9E7C0E4848A__INCLUDED_)
#define AFX_UNICASMANAGE_H__359FF1E2_13DD_4BE7_BADC_D9E7C0E4848A__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

class CUniCASManage
{
private:
  bool bUNICAS;
public:
  CUniCASManage ();
  virtual ~ CUniCASManage ();

  bool bStatusUniCAS ();
  bool bCheckUniCAS ();

  bool bStopUniCAS ();
  bool bStartUniCAS ();

  bool bRestartUniCAS ();

  bool bInstallStatus ();
};

#endif // !defined(AFX_UNICASMANAGE_H__359FF1E2_13DD_4BE7_BADC_D9E7C0E4848A__INCLUDED_)
