#include <StdAfx.h>
#include <afxtempl.h>
#include "Filename.h"
#include "env.h"
#include "lang.h"
#include "io.h"

/////////////////////////////////////////////////////////////////////////////////////////
//
//
extern	CLang		theLang;

CEnv::CEnv()
{
	m_errmsg.Empty();
	m_unicas.Empty();
	m_unicas_driver.Empty();
	m_cubrid.Empty();
	m_cubrid_databases.Empty();
	m_cubrid_databases_driver.Empty();
	m_cubrid_driver.Empty();
}

bool CEnv::CheckOS()
{
OSVERSIONINFO	info;
info.dwOSVersionInfoSize = sizeof(info);

	GetVersionEx(&info);

	if (info.dwPlatformId != VER_PLATFORM_WIN32_NT) {
		m_errmsg = MSG_OS_ERROR;
		return false;
	} 

	return true;
}

bool CEnv::GetEnviornment()
{
CFilename	cubrid, cubrid_databases, unicas;
	SetCUBRIDEnvVar();

	if (!getenv(ENV_CUBRID)) {
		m_errmsg.Format(MSG_ENV_ERROR, ENV_CUBRID);
		return false;
	} else if (!getenv(ENV_CUBRID_DATABASES)) {
		m_errmsg.Format(MSG_ENV_ERROR, ENV_CUBRID_DATABASES);
		return false;
	} else {
		/* ENV_CUBRID_MSG_LANG is an optional parameter */                           
		if (getenv(ENV_CUBRID_MSG_LANG)) {                                           
			theLang.ReadMessage(getenv(ENV_CUBRID), getenv(ENV_CUBRID_MSG_LANG));
		} else {                                                                     
			theLang.ReadMessage(getenv(ENV_CUBRID), "en_US");                    
		}                                                                            
	}

	cubrid.Format("%s", getenv(ENV_CUBRID));
	cubrid.TrimRight("\\");
	cubrid.MakeLower();
	cubrid.GetDriver(m_cubrid_driver, m_cubrid);

	cubrid_databases.Format("%s", getenv(ENV_CUBRID_DATABASES));
	cubrid_databases.TrimRight("\\");
	cubrid_databases.MakeLower();
	cubrid_databases.GetDriver(m_cubrid_databases_driver, m_cubrid_databases);

	unicas.Format("%s", getenv(ENV_CUBRID));
	unicas.TrimRight("\\");
	unicas.MakeLower();
	unicas.GetDriver(m_unicas_driver, m_unicas);

	if (!CheckCUBRID()) {
		m_errmsg.Format(theLang.GetMessage(MSG_INSTALL_ERROR), "CUBRID");
		return false;
	}
	if (!CheckUniCAS()) {
		m_errmsg.Format(theLang.GetMessage(MSG_INSTALL_ERROR), "CUBRIDCAS");
		return false;
	}

	return true;
}

CString CEnv::GetCUBRID()
{
	return (m_cubrid_driver + ":" + m_cubrid);
}

CString CEnv::GetCUBRID_DATABASES()
{
	return (m_cubrid_databases_driver + ":" + m_cubrid_databases);
}

CString CEnv::GetUniCAS()
{
	return (m_unicas_driver + ":" + m_unicas);
}

bool CEnv::CheckCUBRID()
{
CString	CUBRID = GetCUBRID() + "\\bin\\";
CString	tmpStr;

	tmpStr = CUBRID + CMD_CUBRID_RELEASE;
	if (_access(LPCSTR(tmpStr), 0)) return false;

	tmpStr = CUBRID + CMD_MASTER;
	if (_access(LPCSTR(tmpStr), 0)) return false;

	tmpStr = CUBRID + CMD_SERVER;
	if (_access(LPCSTR(tmpStr), 0)) return false;

	return true;
}

bool CEnv::CheckUniCAS()
{
	CString	unicas = GetUniCAS() + "\\";
	CString	tmpStr;

	tmpStr = unicas + DIR_UC_BIN;
	if (_access(LPCSTR(tmpStr), 0)) return false;
	tmpStr = unicas + DIR_UC_CONF;
	if (_access(LPCSTR(tmpStr), 0)) return false;

	return true;
}

CString	CEnv::GetErrMsg()
{
	return	m_errmsg;
}

void CEnv::SetCUBRIDEnvVar()
{
#define BUF_LENGTH 1024

	DWORD dwBufLength = BUF_LENGTH;
	TCHAR sEnvCUBRID[BUF_LENGTH];
	TCHAR sEnvCUBRID_CAS[BUF_LENGTH];
	TCHAR sEnvCUBRID_MANAGER[BUF_LENGTH];
	TCHAR sEnvCUBRID_DATABASES[BUF_LENGTH];
	TCHAR sEnvCUBRID_MODE[BUF_LENGTH];
	TCHAR sEnvPath[BUF_LENGTH];

	char szKey[BUF_LENGTH] = "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment";
	char EnvString[BUF_LENGTH];
	HKEY hKey;
	LONG nResult;

	nResult = RegOpenKeyEx(HKEY_LOCAL_MACHINE, szKey, 0, KEY_QUERY_VALUE, &hKey);
	if (nResult != ERROR_SUCCESS) return;

#ifdef _DEBUG
	FILE *debugfd = fopen("C:\\CUBRIDTray.log", "w+");
#endif

	dwBufLength = BUF_LENGTH;
	nResult = RegQueryValueEx(hKey, TEXT("CUBRID"), NULL, NULL, (LPBYTE)sEnvCUBRID, &dwBufLength);
	if (nResult == ERROR_SUCCESS) {
		// set CUBRID Environment variable.
		strcpy(EnvString, "CUBRID=");
		strcat(EnvString, sEnvCUBRID);
		_putenv(EnvString);

#ifdef _DEBUG
		if (debugfd) {
			fprintf(debugfd, "$CUBRID = %s\n", getenv("CUBRID"));
		}
#endif
	}

	dwBufLength = BUF_LENGTH;
	nResult = RegQueryValueEx(hKey, TEXT("CUBRID_BROKER"), NULL, NULL, (LPBYTE)sEnvCUBRID_CAS, &dwBufLength);
	if (nResult == ERROR_SUCCESS) {
		// set CUBRID_BROKER Environment variable.
		strcpy(EnvString, "CUBRID_BROKER=");
		strcat(EnvString, sEnvCUBRID_CAS);
		_putenv(EnvString);
#ifdef _DEBUG
		if (debugfd) {
			fprintf(debugfd, "$CUBRID_BROKER = %s\n", getenv("CUBRID_BROKER"));
		}
#endif
	}

	dwBufLength = BUF_LENGTH;
	nResult = RegQueryValueEx(hKey, TEXT("CUBRID_MANAGER"), NULL, NULL, (LPBYTE)sEnvCUBRID_MANAGER, &dwBufLength);
	if (nResult == ERROR_SUCCESS) {
		// set CUBRID Environment variable.
		strcpy(EnvString, "CUBRID_MANAGER=");
		strcat(EnvString, sEnvCUBRID_MANAGER);
		_putenv(EnvString);
#ifdef _DEBUG
		if (debugfd) {
			fprintf(debugfd, "$CUBRID_MANAGER = %s\n", getenv("CUBRID_MANAGER"));
		}
#endif
	}

	dwBufLength = BUF_LENGTH;
	nResult = RegQueryValueEx(hKey, TEXT("CUBRID_DATABASES"), NULL, NULL, (LPBYTE)sEnvCUBRID_DATABASES, &dwBufLength);
	if (nResult == ERROR_SUCCESS) {
		// set CUBRID Environment variable.
		strcpy(EnvString, "CUBRID_DATABASES=");
		strcat(EnvString, sEnvCUBRID_DATABASES);
		_putenv(EnvString);
#ifdef _DEBUG
		if (debugfd) {
			fprintf(debugfd, "$CUBRID_DATABASES = %s\n", getenv("CUBRID_DATABASES"));
		}
#endif
	}

	dwBufLength = BUF_LENGTH;
	nResult = RegQueryValueEx(hKey, TEXT("CUBRID_MODE"), NULL, NULL, (LPBYTE)sEnvCUBRID_MODE, &dwBufLength);
	if (nResult == ERROR_SUCCESS) {
		// set CUBRID Environment variable.
		strcpy(EnvString, "CUBRID_MODE=");
		strcat(EnvString, sEnvCUBRID_MODE);
		_putenv(EnvString);
#ifdef _DEBUG
		if (debugfd) {
			fprintf(debugfd, "$CUBRID_MODE = %s\n", getenv("CUBRID_MODE"));
		}
#endif
	}

	dwBufLength = BUF_LENGTH;
	nResult = RegQueryValueEx(hKey, TEXT("Path"), NULL, NULL, (LPBYTE)sEnvPath, &dwBufLength);
	if (nResult == ERROR_SUCCESS) {
		// set CUBRID Environment variable.
		strcpy(EnvString, "Path=");
		strcat(EnvString, sEnvPath);
		_putenv(EnvString);
#ifdef _DEBUG
		if (debugfd) {
			fprintf(debugfd, "Path = %s\n", getenv("Path"));
		}
#endif
	}

#ifdef _DEBUG
	if (debugfd) fclose(debugfd);
#endif

	RegCloseKey(hKey);
}
