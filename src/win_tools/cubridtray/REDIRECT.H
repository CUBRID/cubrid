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
// Redirect.h : header file
//

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

/////////////////////////////////////////////////////////////////////////////
// CRedirect class

#define BUFFER_SIZE 2048

class CRedirect
{
// Construction
public:
	CRedirect();
	~CRedirect();

	BOOL StartChildProcess(LPCSTR lpszCmdLine, BOOL bShowChildWindow = FALSE);
	BOOL IsChildRunning() const;
	void TerminateChildProcess();
	void WriteChildStdIn(LPCSTR lpszInput);
	HANDLE m_hChildProcess;

public:
	bool	m_errno;
	bool	m_run;

protected:
	HANDLE m_hExitEvent;

	// Child input(stdin) & output(stdout, stderr) pipes
	HANDLE m_hStdIn, m_hStdOut, m_hStdErr;
	// Parent output(stdin) & input(stdout) pipe
	HANDLE m_hStdInWrite, m_hStdOutRead, m_hStdErrRead;
	// stdout, stderr write threads
	HANDLE m_hStdOutThread, m_hStdErrThread;
	// Monitoring thread
	HANDLE m_hProcessThread;
	// Child process handle
//	HANDLE m_hChildProcess;

	HANDLE PrepAndLaunchRedirectedChild(LPCSTR lpszCmdLine,
		HANDLE hStdOut, HANDLE hStdIn, HANDLE hStdErr,
		BOOL bShowChildWindow);

	static BOOL m_bRunThread;
	static int staticStdOutThread(CRedirect *pRedirect)
		{ return pRedirect->StdOutThread(pRedirect->m_hStdOutRead); }
	static int staticStdErrThread(CRedirect *pRedirect)
		{ return pRedirect->StdErrThread(pRedirect->m_hStdErrRead); }
	static int staticProcessThread(CRedirect *pRedirect)
		{ return pRedirect->ProcessThread(); }
	int StdOutThread(HANDLE hStdOutRead);
	int StdErrThread(HANDLE hStdErrRead);
	int ProcessThread();

public:
	virtual void OnChildStarted(LPCSTR lpszCmdLine) {};
	virtual void OnChildStdOutWrite(LPCSTR lpszOutput) {};
	virtual void OnChildStdErrWrite(LPCSTR lpszOutput) {};
	virtual void OnChildTerminate() {};
};
