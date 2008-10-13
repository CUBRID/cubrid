
#include "stdafx.h"
#include <stdio.h>
#include <wtypes.h>
#include <winnt.h>
#include <winsvc.h>
#include <winuser.h>
#include <windows.h>

#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <io.h>
#include <Tlhelp32.h>

void WriteLog( char* p_logfile, char* p_format, ... );
void GetCurDateTime( char* p_buf, char* p_form );
void SendMessage_Tray(int status);

void vKingCHStart(DWORD argc, LPTSTR* argv);
void vHandler(DWORD opcode);
void vSetStatus(DWORD dwState, DWORD dwAccept = SERVICE_ACCEPT_STOP|SERVICE_ACCEPT_PAUSE_CONTINUE);
void SetCUBRIDEnvVar();
SERVICE_STATUS_HANDLE g_hXSS; //서비스 환경 글로벌 핸들
DWORD	g_XSS;    //서비스의 현재 상태를 저장하는 변수
BOOL    g_bPause; //서비스가 중지인가 아닌가
HANDLE	g_hExitEvent; //서비스를 중지 시킬때 이벤트를 사용하여 쓰레드를 중지한다
BOOL g_isRunning = false;
#define		WM_SERVICE_STOP		WM_USER+1
#define		WM_SERVICE_START	WM_USER+2

#define		SERVICE_STATUS_STOP  0
#define 	SERVICE_STATUS_START 1

char sLogFile[256] = "CUBRIDService.log";

int checkCmautoProcess(int pid)
{
    HANDLE        hModuleSnap = NULL;
    MODULEENTRY32 me32        = {0};
    hModuleSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);

	if (hModuleSnap == (HANDLE)-1)
        return -1;

    me32.dwSize = sizeof(MODULEENTRY32);

    if(Module32First(hModuleSnap, &me32)) {
       do {
          if(_stricmp((const char*)me32.szModule, "cub_auto.exe") == 0) {
             CloseHandle (hModuleSnap); 
             return 1;
          }
       } while(Module32Next(hModuleSnap, &me32));
    }
    CloseHandle (hModuleSnap);
    return 0;
}

BOOL checkCmauto()
{
	char *envCMHome;
	char pidFile[1024];
	int  cmautoPid;
	FILE *fpCmautoPid;

	envCMHome = getenv("CUBRID");
	if (!envCMHome) return FALSE;

	sprintf(pidFile, "%s\\logs\\cub_auto.pid", envCMHome);

	for (int i=0 ; i<20 ; i++) {
		if (_access(pidFile, 0 /* F_OK */) == -1) {
			Sleep(500);
		}
		else {
			fpCmautoPid = fopen(pidFile, "r");
			if (fpCmautoPid) {
				fscanf(fpCmautoPid, "%d", &cmautoPid);
				fclose(fpCmautoPid);

				if (checkCmautoProcess(cmautoPid) != 0) {
					return TRUE;
				}
			}
			Sleep(500);
		}
	}

	return FALSE;
}

int main(int argc, char* argv[])
{
	SetCUBRIDEnvVar();

	SERVICE_TABLE_ENTRY stbl[] = 
	{
		{"CUBRIDService", (LPSERVICE_MAIN_FUNCTION)vKingCHStart },
		{NULL, NULL}
	};

	if(!StartServiceCtrlDispatcher(stbl))
	{
		WriteLog(sLogFile,"(%d)CUBRIDService가 서비스에 등록 되지 않았습니다.\n", GetLastError() );
		return 0;
	}

	return 1;
}



void vKingCHStart(DWORD argc, LPTSTR* argv)
{
	g_hXSS = RegisterServiceCtrlHandlerA("CUBRIDService",
		(LPHANDLER_FUNCTION)vHandler);

	if(g_hXSS ==0)
	{
		WriteLog(sLogFile,"(%d)CUBRIDService가 서비스 컨트롤 핸들을 설치할 수 없습니다\n", GetLastError() );
		return ;
	}

	//서비스가 시작 중임을 알린다
	vSetStatus(SERVICE_START_PENDING);
	g_bPause = FALSE; //서비스가 시작되었으니 FALSE 로 초기화

	//이벤트를 만든다
	g_hExitEvent = CreateEventA(NULL, TRUE, FALSE, "XServiceExitEvent");

	//서비스를 실행하고
	vSetStatus(SERVICE_RUNNING);

    g_isRunning = true;

	char command[100];

	sprintf(command,"%s\\bin\\cubrid service start", getenv("CUBRID"));
	WinExec(command,SW_HIDE);

	SendMessage_Tray(SERVICE_STATUS_START);

	while (1)
    {
		Sleep(2000);

		if ( !g_isRunning )
        {
            break;
        }
    }

	//서비스를 멈춘다
	vSetStatus(SERVICE_STOPPED);
}



void vSetStatus(DWORD dwState, DWORD dwAccept)
{
	SERVICE_STATUS ss;

	ss.dwServiceType				= SERVICE_WIN32_OWN_PROCESS;
	ss.dwCurrentState				= dwState;
	ss.dwControlsAccepted			= dwAccept;
	ss.dwWin32ExitCode				= 0;
	ss.dwServiceSpecificExitCode	= 0;
	ss.dwCheckPoint					= 0;
	ss.dwWaitHint					= 0;

	//현재 상태 보관
	g_XSS = dwState;
	SetServiceStatus(g_hXSS, &ss);
}	


void vHandler(DWORD opcode)
{
	if(opcode == g_XSS)
	{
		WriteLog(sLogFile,"(%d)CUBRIDService가 현재 상태와 같은 제어 코드일 경우는 처리할 필요가 없다.\n");
		return;
	}

	switch(opcode)
	{
	case SERVICE_CONTROL_PAUSE:
		vSetStatus(SERVICE_PAUSE_PENDING,0);
		g_bPause = TRUE;
		vSetStatus(SERVICE_PAUSED);
		break;
	case SERVICE_CONTROL_CONTINUE:
		vSetStatus(SERVICE_CONTINUE_PENDING, 0);
		g_bPause = FALSE;
		vSetStatus(SERVICE_RUNNING);
		break;
	case SERVICE_CONTROL_STOP:

		char command[100];

		sprintf(command,"%s\\bin\\cubrid service stop", getenv("CUBRID"));
		WinExec(command,SW_HIDE);

		g_isRunning = false;

		SendMessage_Tray(SERVICE_STATUS_STOP);
		vSetStatus(SERVICE_STOP_PENDING, 0);
		//쓰레드를 실행중이면 멈춘다
		SetEvent(g_hExitEvent);
		vSetStatus(SERVICE_STOPPED);
		break;
	case SERVICE_CONTROL_INTERROGATE:
	default:
		vSetStatus(g_XSS);
		break;
	}
}

void WriteLog( char* p_logfile, char* p_format, ... ) 
{
	va_list str;
	char    old_logfile[256];
	char  	cur_time[25];
	long    f_size;
	long    f_pos;
	FILE*   logfile_fd;


#define _MAX_LOGFILE_SIZE_	102400


	while( 1 )
	{
		/* Prepare Log File */
		if( p_logfile == NULL )
			logfile_fd = stderr;
		else
			logfile_fd = fopen( p_logfile , "a+" );

	
		if( logfile_fd == NULL )
		{
			fprintf(stderr,"WriteLog:Can't open logfile [%s][%d]\n",
                                       p_logfile, errno );
			return;
		}
		else
		{
			f_pos  = ftell( logfile_fd );

			fseek( logfile_fd, 0, SEEK_END );
			f_size = ftell( logfile_fd );

			fseek( logfile_fd, f_pos, SEEK_SET );

			/* If LogFile grows too long */
			if( f_size > _MAX_LOGFILE_SIZE_ )
			{
				fclose( logfile_fd );
	
				strcpy( old_logfile, p_logfile );
				GetCurDateTime( cur_time,"%Y%m%d%H:%M:%S" );
				strcat( old_logfile, "." );
				strcat( old_logfile, cur_time);
				strcat( old_logfile, ".OLD"  );

				rename( p_logfile, old_logfile );
			}
			else
				break;
		}
	}
	/* while End ! */


#ifndef __DEBUG
	GetCurDateTime( cur_time,"%Y%m%d %H:%M:%S" );
	fprintf( logfile_fd, "[%s] ", cur_time );
#endif



	va_start( str, p_format );

	vfprintf( logfile_fd, p_format, str );

	va_end( str );

	if( p_logfile != NULL )
		fclose( logfile_fd );


}




void GetCurDateTime( char* p_buf, char* p_form )
{
	time_t c_time;
	struct tm* l_time = NULL;


	time( &c_time );

	l_time = localtime( &c_time );

	strftime( p_buf, 24 , p_form, l_time );
	
}

void SetCUBRIDEnvVar()
{
#define BUF_LENGTH 1024

	DWORD dwBufLength = BUF_LENGTH;
	TCHAR sEnvCUBRID[BUF_LENGTH];
	TCHAR sEnvCUBRID_CAS[BUF_LENGTH];
	TCHAR sEnvCUBRID_MANAGER[BUF_LENGTH];
	TCHAR sEnvCUBRID_DATABASES[BUF_LENGTH];
	TCHAR sEnvCUBRID_LANG[BUF_LENGTH];
	TCHAR sEnvCUBRID_MODE[BUF_LENGTH];
	TCHAR sEnvPath[BUF_LENGTH];

	char szKey[BUF_LENGTH] = "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment";
	char EnvString[BUF_LENGTH];
	HKEY hKey;
	LONG nResult;

	nResult = RegOpenKeyExA(HKEY_LOCAL_MACHINE, szKey, 0, KEY_QUERY_VALUE, &hKey);
	if (nResult != ERROR_SUCCESS) return;

#ifdef _DEBUG
	FILE *debugfd = fopen("C:\\CUBRIDService.log", "w+");
#endif

	dwBufLength = BUF_LENGTH;
	nResult = RegQueryValueEx(hKey, TEXT("CUBRID"), NULL, NULL, (LPBYTE)sEnvCUBRID, &dwBufLength);
	if (nResult == ERROR_SUCCESS) {
		// set CUBRID Environment variable.
		strcpy(EnvString, "CUBRID=");
		strcat(EnvString, (const char*)sEnvCUBRID);
		_putenv(EnvString);

#ifdef _DEBUG
		if (debugfd) {
			fprintf(debugfd, "$CUBRID = %s\n", getenv("CUBRID"));
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
	nResult = RegQueryValueEx(hKey, TEXT("CUBRID_LANG"), NULL, NULL, (LPBYTE)sEnvCUBRID_LANG, &dwBufLength);
	if (nResult == ERROR_SUCCESS) {
		// set CUBRID Environment variable.
		strcpy(EnvString, "CUBRID_LANG=");
		strcat(EnvString, sEnvCUBRID_LANG);
		_putenv(EnvString);
#ifdef _DEBUG
		if (debugfd) {
			fprintf(debugfd, "$CUBRID_LANG = %s\n", getenv("CUBRID_LANG"));
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

void SendMessage_Tray(int status)
{
	HWND hTrayWnd;
	hTrayWnd = FindWindowA("cubrid_tray", "cubrid_tray");

	if (hTrayWnd == NULL){
		return;
	}

	if (status == SERVICE_STATUS_STOP) {
		PostMessage(hTrayWnd, WM_SERVICE_STOP, NULL, NULL);
	}
	else if (status == SERVICE_STATUS_START) {
		PostMessage(hTrayWnd, WM_SERVICE_START, NULL, NULL);
	}
}

