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
// ManageRegistry.h: interface for the CManageRegistry class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_MANAGEREGISTRY_H__0DF55211_DBE2_401B_BE1D_EC00E4D9DCCC__INCLUDED_)
#define AFX_MANAGEREGISTRY_H__0DF55211_DBE2_401B_BE1D_EC00E4D9DCCC__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

class CManageRegistry  
{
private:
	char	sKeyPath[1024];
	HKEY	hKeyReg;
	char*	sResultName;

	bool bOpenRegistry();
public:
	bool GetJavaRootPath(char *path);
	CManageRegistry( char* sprodname );
	virtual ~CManageRegistry();

	char* sGetItem( char* sItemName );

	char* sGetString( char* sItemName, char* sCmdName );
	int   dGetString( char* sItemName, char* sCmdName );
	bool  bGetString( char* sString );

};


#endif // !defined(AFX_MANAGEREGISTRY_H__0DF55211_DBE2_401B_BE1D_EC00E4D9DCCC__INCLUDED_)
