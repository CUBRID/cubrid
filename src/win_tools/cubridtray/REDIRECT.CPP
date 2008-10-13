// Redirect.cpp : implementation file
//

#include "stdafx.h"
#include "Redirect.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// This class is build from the following MSDN article.
// Q190351 HOWTO: Spawn Console Processes with Redirected Standard Handles.

/////////////////////////////////////////////////////////////////////////////
// API Function

BOOL WINAPI IsWinNT()
{
  // get windows version
  DWORD WindowsVersion = GetVersion();
  DWORD WindowsMajorVersion = (DWORD)(LOBYTE(LOWORD(WindowsVersion)));
  DWORD WindowsMinorVersion = (DWORD)(HIBYTE(LOWORD(WindowsVersion)));

  // Running on WIN9x ?
  if (WindowsVersion >= 0x80000000) return FALSE;
  
  // Running on NT
  return TRUE;
}

/////////////////////////////////////////////////////////////////////////////
// CRedirect class

CRedirect::CRedirect()
{
	// Initialisation.
	m_hStdIn = NULL;
	m_hStdOut = NULL;
	m_hStdErr = NULL;
	m_hStdInWrite = NULL;
	m_hStdOutRead = NULL;
	m_hStdErrRead = NULL;
	m_hChildProcess = NULL;
	m_hStdOutThread = NULL;
	m_hStdErrThread = NULL;
	m_hProcessThread = NULL;
	m_hExitEvent = NULL;
	m_bRunThread = FALSE;
}

CRedirect::~CRedirect()
{
	TerminateChildProcess();
}

// Create standard handles, try to start child from command line.

BOOL CRedirect::StartChildProcess(LPCSTR lpszCmdLine, BOOL bShowChildWindow)
{
	HANDLE hProcess = ::GetCurrentProcess();

	// Set up the security attributes struct.
	SECURITY_ATTRIBUTES sa;
	::ZeroMemory(&sa, sizeof(SECURITY_ATTRIBUTES));
	sa.nLength= sizeof(SECURITY_ATTRIBUTES);
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle = TRUE;

	HANDLE hStdInWriteTmp, hStdOutReadTmp, hStdErrReadTmp;

	// Create the child stdin pipe.
	VERIFY(::CreatePipe(&m_hStdIn, &hStdInWriteTmp, &sa, 0));

	// Create the child stdout pipe.
	VERIFY(::CreatePipe(&hStdOutReadTmp, &m_hStdOut, &sa, 0));

	// Create the child stderr pipe.
	VERIFY(::CreatePipe(&hStdErrReadTmp, &m_hStdErr, &sa, 0));

	// Create new stdin write, stdout and stderr read handles.
	// Set the properties to FALSE. Otherwise, the child inherits the
	// properties and, as a result, non-closeable handles to the pipes
	// are created.
	
	VERIFY(::DuplicateHandle(hProcess, hStdInWriteTmp,
		hProcess, &m_hStdInWrite, 0, FALSE, DUPLICATE_SAME_ACCESS));

	VERIFY(::DuplicateHandle(hProcess, hStdOutReadTmp,
		hProcess, &m_hStdOutRead, 0, FALSE, DUPLICATE_SAME_ACCESS));

	VERIFY(::DuplicateHandle(hProcess, hStdErrReadTmp,
		hProcess, &m_hStdErrRead, 0, FALSE, DUPLICATE_SAME_ACCESS));

	// Close inheritable copies of the handles you do not want to be
	// inherited.

	VERIFY(::CloseHandle(hStdInWriteTmp));
	VERIFY(::CloseHandle(hStdOutReadTmp));
	VERIFY(::CloseHandle(hStdErrReadTmp));

	// Start child process with redirected stdout, stdin & stderr
	m_hChildProcess = PrepAndLaunchRedirectedChild(lpszCmdLine,
		m_hStdOut, m_hStdIn, m_hStdErr, bShowChildWindow);

	if (m_hChildProcess == NULL)
	{
		TCHAR lpszBuffer[BUFFER_SIZE];
		sprintf(lpszBuffer, "Unable to start %s\n", lpszCmdLine);
		OnChildStdOutWrite(lpszBuffer);

		// close all handles and return FALSE
		VERIFY(::CloseHandle(m_hStdIn));
		m_hStdIn = NULL;
		VERIFY(::CloseHandle(m_hStdOut));
		m_hStdOut = NULL;
		VERIFY(::CloseHandle(m_hStdErr));
		m_hStdErr = NULL;

		return FALSE;
	}

	DWORD dwThreadID;
	m_bRunThread = TRUE;

	// Create Exit event
	m_hExitEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);
	VERIFY(m_hExitEvent != NULL);

	// Launch the thread that read the child stdout.
	m_hStdOutThread = ::CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)staticStdOutThread,
		(LPVOID)this, 0, &dwThreadID);
	VERIFY(m_hStdOutThread != NULL);

	// Launch the thread that read the child stderr.
	m_hStdErrThread = ::CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)staticStdErrThread,
		(LPVOID)this, 0, &dwThreadID);
	VERIFY(m_hStdErrThread != NULL);

	// Launch the thread that monitoring the child process.
	m_hProcessThread = ::CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)staticProcessThread,
		(LPVOID)this, 0, &dwThreadID);
	VERIFY(m_hProcessThread != NULL);

	// Virtual function to notify derived class that the child is started.
	OnChildStarted(lpszCmdLine);

	return TRUE;
}

// Check if the child process is running. 
// On NT/2000 the handle must have PROCESS_QUERY_INFORMATION access.

BOOL CRedirect::IsChildRunning() const
{
	DWORD dwExitCode;
    if (m_hChildProcess == NULL) return FALSE;
	::GetExitCodeProcess(m_hChildProcess, &dwExitCode);
	return (dwExitCode == STILL_ACTIVE) ? TRUE: FALSE;
}

void CRedirect::TerminateChildProcess()
{
	// Tell the threads to exit and wait for process thread to die.
	m_bRunThread = FALSE;
	::SetEvent(m_hExitEvent);
	Sleep(500);

	// Check the process thread.
	if (m_hProcessThread != NULL)
	{
		VERIFY(::WaitForSingleObject(m_hProcessThread, 1000) != WAIT_TIMEOUT);
		m_hProcessThread = NULL;
	}

	// Close all child handles first.
	if (m_hStdIn != NULL)
		VERIFY(::CloseHandle(m_hStdIn));
	m_hStdIn = NULL;
	if (m_hStdOut != NULL)
		VERIFY(::CloseHandle(m_hStdOut));
	m_hStdOut = NULL;
	if (m_hStdErr != NULL)
		VERIFY(::CloseHandle(m_hStdErr));
	m_hStdErr = NULL;
    Sleep(100);

	// Close all parent handles.
	if (m_hStdInWrite != NULL)
		VERIFY(::CloseHandle(m_hStdInWrite));
	m_hStdInWrite = NULL;
	if (m_hStdOutRead != NULL)
		VERIFY(::CloseHandle(m_hStdOutRead));
	m_hStdOutRead = NULL;
	if (m_hStdErrRead != NULL)
		VERIFY(::CloseHandle(m_hStdErrRead));
	m_hStdErrRead = NULL;
    Sleep(100);

    // Stop the stdout read thread.
	if (m_hStdOutThread != NULL)
	{
		if (!::IsWinNT())
			::TerminateThread(m_hStdOutThread, 1);
		VERIFY(::WaitForSingleObject(m_hStdOutThread, 1000) != WAIT_TIMEOUT);
		m_hStdOutThread = NULL;
	}

    // Stop the stderr read thread.
	if (m_hStdErrThread != NULL)
	{
		if (!::IsWinNT())
			::TerminateThread(m_hStdErrThread, 1);
		VERIFY(::WaitForSingleObject(m_hStdErrThread, 1000) != WAIT_TIMEOUT);
		m_hStdErrThread = NULL;
	}
    Sleep(100);

	// Stop the child process if not already stopped.
	// It's not the best solution, but it is a solution.
	// On Win98 it may crash the system if the child process is the COMMAND.COM.
	// The best way is to terminate the COMMAND.COM process with an "exit" command.

	if (IsChildRunning())
    {
		VERIFY(::TerminateProcess(m_hChildProcess, 1));
		VERIFY(::WaitForSingleObject(m_hChildProcess, 1000) != WAIT_TIMEOUT);
    }
	m_hChildProcess = NULL;

	// cleanup the exit event
	if (m_hExitEvent != NULL)
		VERIFY(::CloseHandle(m_hExitEvent));
	m_hExitEvent = NULL;
}

// Launch the process that you want to redirect.

HANDLE CRedirect::PrepAndLaunchRedirectedChild(LPCSTR lpszCmdLine,
	HANDLE hStdOut, HANDLE hStdIn, HANDLE hStdErr,
	BOOL bShowChildWindow)
{
	HANDLE hProcess = ::GetCurrentProcess();

	PROCESS_INFORMATION pi;

	// Set up the start up info struct.
	STARTUPINFO si;
	::ZeroMemory(&si, sizeof(STARTUPINFO));
	si.cb = sizeof(STARTUPINFO);
	si.dwFlags = STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;
	si.hStdOutput = hStdOut;
	si.hStdInput  = hStdIn;
	si.hStdError  = hStdErr;
	
	// Use this if you want to show the child.
	si.wShowWindow = bShowChildWindow ? SW_SHOW: SW_HIDE;
	// Note that dwFlags must include STARTF_USESHOWWINDOW if you want to
	// use the wShowWindow flags.

	// Create the NULL security token for the process
	LPVOID lpSD = NULL;
	LPSECURITY_ATTRIBUTES lpSA = NULL;

	// On NT/2000 the handle must have PROCESS_QUERY_INFORMATION access.
	// This is made using an empty security descriptor. It is not the same
	// as using a NULL pointer for the security attribute!

	if (IsWinNT())
	{
		lpSD = ::GlobalAlloc(GPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
		VERIFY(::InitializeSecurityDescriptor(lpSD, SECURITY_DESCRIPTOR_REVISION));
		VERIFY(::SetSecurityDescriptorDacl(lpSD, -1, 0, 0));

		lpSA = (LPSECURITY_ATTRIBUTES)::GlobalAlloc(GPTR, sizeof(SECURITY_ATTRIBUTES));
		lpSA->nLength = sizeof(SECURITY_ATTRIBUTES);
		lpSA->lpSecurityDescriptor = lpSD;
		lpSA->bInheritHandle = TRUE;
	}

	// Try to spawn the process.
	BOOL bResult = ::CreateProcess(NULL, (char*)lpszCmdLine, lpSA, NULL, TRUE,
						CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi);

	// Cleanup memory allocation
	if (lpSA != NULL)
		::GlobalFree(lpSA);
	if (lpSD != NULL)
		::GlobalFree(lpSD);

	// Return if an error occurs.
	if (!bResult) return FALSE;

	// Close any unnecessary handles.
	VERIFY(::CloseHandle(pi.hThread));

	// Save global child process handle to cause threads to exit.
	return pi.hProcess;
}

BOOL CRedirect::m_bRunThread = TRUE;

// Thread to read the child stdout.

int CRedirect::StdOutThread(HANDLE hStdOutRead)
{
	DWORD nBytesRead;
	CHAR lpszBuffer[BUFFER_SIZE+1];

	while (m_bRunThread)
	{
		if (!::ReadFile(hStdOutRead, lpszBuffer, BUFFER_SIZE,
			&nBytesRead, NULL) || !nBytesRead)
		{
			if (::GetLastError() == ERROR_BROKEN_PIPE)
				break;			// pipe done - normal exit path.
			else
				ASSERT(FALSE);	// Something bad happened.
		}
		if (nBytesRead)
		{
			// Virtual function to notify derived class that
			// characters are writted to stdout.
			lpszBuffer[nBytesRead] = '\0';
			OnChildStdOutWrite(lpszBuffer);
		}
	}
	return 0;
}

// Thread to read the child stderr.

int CRedirect::StdErrThread(HANDLE hStdErrRead)
{
	DWORD nBytesRead;
	CHAR lpszBuffer[BUFFER_SIZE+1];

	while (m_bRunThread)
	{
		if (!::ReadFile(hStdErrRead, lpszBuffer, BUFFER_SIZE,
			&nBytesRead, NULL) || !nBytesRead)
		{
			if (::GetLastError() == ERROR_BROKEN_PIPE)
				break;			// pipe done - normal exit path.
			else
				ASSERT(FALSE);	// Something bad happened.
		}
		if (nBytesRead)
		{
			// Virtual function to notify derived class that
			// characters are writted to stderr.
			lpszBuffer[nBytesRead] = '\0';
			OnChildStdErrWrite(lpszBuffer);
		}
	}
	return 0;
}

// Thread to monitoring the child process.

int CRedirect::ProcessThread()
{
	HANDLE hWaitHandle[2];
	hWaitHandle[0] = m_hExitEvent;
	hWaitHandle[1] = m_hChildProcess;

	while (m_bRunThread)
	{
		switch (::WaitForMultipleObjects(2, hWaitHandle, FALSE, 1))
		{
			case WAIT_OBJECT_0 + 0:	// exit on event
				ASSERT(m_bRunThread == FALSE);
				break;

			case WAIT_OBJECT_0 + 1:	// child process exit
				ASSERT(m_bRunThread == TRUE);
				m_bRunThread = FALSE;
				break;
		}
	}
	// Virtual function to notify derived class that
	// child process is terminated.
	// Application must call TerminateChildProcess()
	// but not direcly from this thread!
	m_hProcessThread = NULL;
	OnChildTerminate();
	return 0;
}

// Function that write to the child stdin.

void CRedirect::WriteChildStdIn(LPCSTR lpszInput)
{
	DWORD nBytesWrote;
	DWORD Length = strlen(lpszInput);
	if (m_hStdInWrite != NULL && Length > 0)
	{
		if (!::WriteFile(m_hStdInWrite, lpszInput, Length, &nBytesWrote, NULL))
		{
			if (::GetLastError() == ERROR_NO_DATA)
				;				// Pipe was closed (do nothing).
			else
				ASSERT(FALSE);	// Something bad happened.
		}
	}
}
