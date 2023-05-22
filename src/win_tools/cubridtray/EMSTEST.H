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
#include "resource.h"       // main symbols

class CTrayApp : public CWinApp
{
public:
	bool Get_DisplayLog();
	bool Get_AutoStop();
	bool Get_AutoStart();
	void Set_DisplayLog(bool);
	void Set_AutoStop(bool);
	void Set_AutoStart(bool);
	CTrayApp();

protected:
	bool m_bDisplayLog;
	bool m_bSvrAutostop;
	bool m_bSvrAutostart;

protected:
	//{{AFX_VIRTUAL(CTrayApp)
	public:
	virtual BOOL InitInstance();
	virtual int ExitInstance();
	//}}AFX_VIRTUAL

	//{{AFX_MSG(CTrayApp)
	afx_msg void OnAppAbout();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

  BOOL bDisplayingAboutBox;
};

