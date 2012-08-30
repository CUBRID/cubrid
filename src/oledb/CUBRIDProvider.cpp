/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */

// CUBRIDProvider.cpp : Implementation of DLL Exports.

#include "stdafx.h"
#include "resource.h"

// ATLTRACE 메시지를 파일로 기록하고 싶지 않으면 다음을 주석처리한다.
#define ENABLE_LOGGING

#define LOGGING_FILENAME			"c:\\CUBRIDtrace.txt"
#define DEFAULT_TRACE_LEVEL			3

// The module attribute causes DllMain, DllRegisterServer and DllUnregisterServer to be automatically implemented for you
[ module(dll, uuid = "{2B22247F-E9F7-47e8-A9B0-79E8039DCFC8}",
		 name = "CUBRIDProvider",
		 helpstring = "CUBRIDProvider 1.0 Type Library",
		 resource_name = "IDR_UNIPROVIDER") ]
class CCUBRIDProviderModule
{
#ifndef _DEBUG
public:
	CCUBRIDProviderModule()
	{
		cci_init();
	}
	~CCUBRIDProviderModule()
	{
	}
#endif
#ifdef _DEBUG
private:
	HANDLE m_hLogFile;
	int m_fWarnMode, m_fErrorMode, m_fAssertMode;
public:
	CCUBRIDProviderModule()
	{
		CTrace::s_trace.SetLevel(DEFAULT_TRACE_LEVEL);
		cci_init();
#ifdef ENABLE_LOGGING
		m_fWarnMode = ::_CrtSetReportMode(_CRT_WARN, _CRTDBG_REPORT_MODE);
		m_fErrorMode = ::_CrtSetReportMode(_CRT_ERROR, _CRTDBG_REPORT_MODE);
		m_fAssertMode = ::_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_REPORT_MODE);
		m_hLogFile = ::CreateFile(LOGGING_FILENAME, GENERIC_WRITE, FILE_SHARE_READ,
				NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

		::_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE | m_fWarnMode);
		::_CrtSetReportFile(_CRT_WARN, m_hLogFile);
		::_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | m_fErrorMode);
		::_CrtSetReportFile(_CRT_ERROR, m_hLogFile);
		::_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | m_fAssertMode);
		::_CrtSetReportFile(_CRT_ASSERT, m_hLogFile);
#endif
	}
	~CCUBRIDProviderModule()
	{
#ifdef ENABLE_LOGGING
		::_CrtSetReportMode(_CRT_WARN, m_fWarnMode);
		::_CrtSetReportMode(_CRT_ERROR, m_fErrorMode);
		::_CrtSetReportMode(_CRT_ASSERT, m_fAssertMode);
		::CloseHandle(m_hLogFile);
#endif
	}
#endif
};
