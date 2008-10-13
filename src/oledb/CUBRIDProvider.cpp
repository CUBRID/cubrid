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
