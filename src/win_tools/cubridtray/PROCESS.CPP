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
#include <StdAfx.h>
#include <afxtempl.h>
#include "Process.h"

/////////////////////////////////////////////////////////////////////////////////////////
//
//

CProcess::CProcess()
{
	m_psapi = LoadLibrary("PSAPI.DLL");

	if (!m_psapi) return;

	EnumProcesses =(ENUMPROCESSES)GetProcAddress(
			(HINSTANCE)m_psapi, "EnumProcesses");

	GetModuleFileName = (GETMODULEFILENAME)GetProcAddress(
			(HINSTANCE)m_psapi, "GetModuleFileNameExA");

	GetModuleBaseName = (GETMODULEBASENAME)GetProcAddress(
			(HINSTANCE)m_psapi, "GetModuleBaseNameA");

	EnumProcessModules = (ENUMPROCESSMODULES)GetProcAddress(
			(HINSTANCE)m_psapi, "EnumProcessModules");

	if (!EnumProcesses	|| 
		!GetModuleFileName || 
		!GetModuleBaseName	||
		!EnumProcessModules  )
        return;
}

CProcess::~CProcess()
{
	FreeLibrary((HINSTANCE)m_psapi);
}

DWORD CProcess::FindProcess(CString process_name)
{
DWORD process_ids[MAXPROCESS];
DWORD num_processes;

    if (!EnumProcesses ||
		!GetModuleFileName ||
		!GetModuleBaseName ||
		!EnumProcessModules) return 0;

    int success = EnumProcesses(process_ids, sizeof(process_ids), &num_processes);
    num_processes /= sizeof(process_ids[0]);
    if ( !success ) return 0;

	char* sTmpName = ( char* )LPCTSTR( process_name );

    for ( unsigned i=0; i<num_processes; i++) {
		HANDLE process = OpenProcess(
            PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, 
            FALSE, 
            process_ids[i]);

        HMODULE modules[MAXPROCESS];
        DWORD num_modules;
        char file_name[MAX_PATH];

        EnumProcessModules(process, modules, sizeof(modules), &num_modules);
        num_modules /= sizeof(modules[0]);

        if (GetModuleFileName(process, modules[0], file_name, sizeof(file_name))) 
        {
			CString	filename = file_name;
			filename.MakeLower();

			if( process_name.CompareNoCase( file_name ) == 0 )
				return process_ids[i];
        }
        CloseHandle(process);
    }

	return 0;
}




DWORD CProcess::dFindProcess(char* process_name)
{
DWORD process_ids[MAXPROCESS];
DWORD num_processes;

    if (!EnumProcesses ||
		!GetModuleFileName ||
		!GetModuleBaseName ||
		!EnumProcessModules) return 0;

    int success = EnumProcesses(process_ids, sizeof(process_ids), &num_processes);
    num_processes /= sizeof(process_ids[0]);
    if ( !success ) return 0;

    for ( unsigned i=0; i<num_processes; i++) {
		HANDLE process = OpenProcess(
            PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, 
            FALSE, 
            process_ids[i]);

        HMODULE modules[MAXPROCESS];
        DWORD num_modules;
        char file_name[MAX_PATH];

        EnumProcessModules(process, modules, sizeof(modules), &num_modules);
        num_modules /= sizeof(modules[0]);

        if (GetModuleFileName(process, modules[0], file_name, sizeof(file_name))) 
        {
			CString	filename = file_name;
			filename.MakeLower();

			if( strcmp( process_name, file_name ) == 0 )
				return process_ids[i];
        }
        CloseHandle(process);
    }

	return 0;
}


void CProcess::KillProcess(DWORD process_id)
{
HANDLE	process;

	process = OpenProcess(PROCESS_TERMINATE, 0, process_id);
	TerminateProcess(process, (unsigned) -1);
}

