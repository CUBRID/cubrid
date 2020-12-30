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
#if !defined(__Process)

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// process.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// 
//Windows NT Functions
typedef BOOL (WINAPI *ENUMPROCESSES)(
  DWORD * lpidProcess,  // array to receive the process identifiers
  DWORD cb,             // size of the array
  DWORD * cbNeeded      // receives the number of bytes returned
);

typedef BOOL (WINAPI *ENUMPROCESSMODULES)(
  HANDLE hProcess,      // handle to the process
  HMODULE * lphModule,  // array to receive the module handles
  DWORD cb,             // size of the array
  LPDWORD lpcbNeeded    // receives the number of bytes returned
);

typedef DWORD (WINAPI *GETMODULEFILENAME)( 
  HANDLE hProcess,		// handle to the process
  HMODULE hModule,		// handle to the module
  LPTSTR lpstrFileName,	// array to receive filename
  DWORD nSize			// size of filename array.
);

typedef DWORD (WINAPI *GETMODULEBASENAME)( 
  HANDLE hProcess,		// handle to the process
  HMODULE hModule,		// handle to the module
  LPTSTR lpstrFileName,	// array to receive base name of module
  DWORD nSize			// size of module name array.
);

#define	MAXPROCESS		2048

class CProcess 
{
public:
    HANDLE m_psapi; 

    ENUMPROCESSES       EnumProcesses;
    GETMODULEFILENAME   GetModuleFileName;
    ENUMPROCESSMODULES  EnumProcessModules;  
	GETMODULEBASENAME	GetModuleBaseName;

public:
	CProcess();
	~CProcess();
	DWORD	FindProcess(CString process_name);
	DWORD	dFindProcess(char* process_name);
	void	KillProcess(DWORD process_id);
};

#endif