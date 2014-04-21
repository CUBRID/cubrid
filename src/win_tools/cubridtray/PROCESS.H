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