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

// Manager.h: interface for the CEasyManager class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_EASYMANAGER_H__B3EFB8FE_D30C_4287_A318_169F43DBE8EE__INCLUDED_)
#define AFX_EASYMANAGER_H__B3EFB8FE_D30C_4287_A318_169F43DBE8EE__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

//#include "ems.h"
//#include "OptionDialog.h"     // Added by ClassView
//#include "LogDialog.h"
//#include "resource.h"       // main symbols

class CEasyManager
{
private:
  bool bCheckEMSCMServer ();

public:
  bool bEasyManagerServerCheckOnly ();
  CEasyManager ();
  virtual ~ CEasyManager ();

  bool bInstallStatus ();

  bool bCheckEasyManagerServer ();

  bool bStartEasyManagerServer ();
  bool bStopEasyManagerServer ();
};

#endif // !defined(AFX_EASYMANAGER_H__B3EFB8FE_D30C_4287_A318_169F43DBE8EE__INCLUDED_)
