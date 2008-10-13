// CUBRIDService.cpp : Defines the entry point for the console application.
//

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

void WriteLog( char* p_logfile, char* p_format, ... );
void GetCurDateTime( char* p_buf, char* p_form );

char sLogFile[256] = "CUBRIDService.log";
char sCmd[256] = "CUBRIDService.exe";
char sExecPath[1024] = "";


void vctrlService(void);
void vDelService(void);
void vStartService(void);
void vStopService(void);


int _tmain(int argc, char* argv[])
{
	// Install a Service if -i switch used
	if (argc == 2) {
		if (_stricmp(argv[1], "-u") == 0) {
			vDelService();
		}
		else if (_stricmp(argv[1], "-start") == 0) {
			vStartService();
		}
		else if (_stricmp(argv[1], "-stop") == 0) {
			vStopService();
		}
		else WriteLog(sLogFile, "Invalid Argument.\n");
	}
	else if (argc == 3) { // service 등록시 CUBRIDService.exe의 디렉토리 위치가 입력되어야 함.
		if (_stricmp(argv[1], "-i") == 0) {
			if (strlen(argv[2]) > 0) strcpy_s(sExecPath, argv[2]);

			vctrlService();
		}
		else WriteLog(sLogFile, "Invalid Argument.\n");
	}
	else WriteLog(sLogFile, "Invalid Argument.\n");

    return 0;
}


void vctrlService(void)
{
	char ServiceFilePath[1024] = "";

	SC_HANDLE scmHandle = OpenSCManager ( NULL, NULL, SC_MANAGER_ALL_ACCESS );

	if (scmHandle == NULL) // Perform error handling.
	{
		WriteLog(sLogFile, "(%d)서비스 매니저와 연결하는데, 오류가 발생하였습니다.\n", GetLastError() ); 
		return;
	}

	sprintf_s(ServiceFilePath, "%s\\%s", sExecPath, sCmd);

	SC_HANDLE scHandle = CreateServiceA (
			scmHandle, 
			"CUBRIDService",
            "CUBRIDService",
			SERVICE_ALL_ACCESS,
            SERVICE_WIN32_OWN_PROCESS|SERVICE_INTERACTIVE_PROCESS,
            SERVICE_AUTO_START,
            SERVICE_ERROR_NORMAL,
            ServiceFilePath,
			NULL,
			NULL,
			NULL,
			NULL,
			NULL );

	if ( scHandle == NULL ) // Process error
	{
		WriteLog(sLogFile, "(%d)서비스 매니저에 CUBRIDService.exe를 등록하는데, 오류가 발생하였습니다.\n", GetLastError() ); 
		return;
	}


	CloseServiceHandle(scHandle);
	CloseServiceHandle(scmHandle);

	return;
}


void vDelService(void)
{
	SC_HANDLE scmHandle = OpenSCManager (NULL, NULL, SC_MANAGER_ALL_ACCESS);

	if (scmHandle == NULL) // Perform error handling.
	{
		WriteLog(sLogFile, "(%d)서비스 매니저와 연결하는데, 오류가 발생하였습니다.\n", GetLastError() );
		return;
	}

	SC_HANDLE scHandle = OpenServiceA( scmHandle, "CUBRIDService", SERVICE_ALL_ACCESS );

	SERVICE_STATUS ss;

	ss.dwServiceType				= SERVICE_WIN32_OWN_PROCESS;
	ss.dwCurrentState				= SERVICE_STOP_PENDING;
	ss.dwControlsAccepted			= 0;
	ss.dwWin32ExitCode				= 0;
	ss.dwServiceSpecificExitCode	= 0;
	ss.dwCheckPoint					= 0;
	ss.dwWaitHint					= 0;


	ControlService(scHandle,SERVICE_CONTROL_STOP,&ss);
	DeleteService(scHandle);

	return;
}


void vStopService(void)
{
	SC_HANDLE scmHandle = OpenSCManager (NULL, NULL, SC_MANAGER_ALL_ACCESS);

	if (scmHandle == NULL) // Perform error handling.
	{
		WriteLog(sLogFile, "(%d)서비스 매니저와 연결하는데, 오류가 발생하였습니다.\n", GetLastError() );
		return;
	}

	SC_HANDLE scHandle = OpenServiceA( scmHandle, "CUBRIDService", SERVICE_ALL_ACCESS );

	SERVICE_STATUS ss;

	ss.dwServiceType				= SERVICE_WIN32_OWN_PROCESS;
	ss.dwCurrentState				= SERVICE_STOP_PENDING;
	ss.dwControlsAccepted			= 0;
	ss.dwWin32ExitCode				= 0;
	ss.dwServiceSpecificExitCode	= 0;
	ss.dwCheckPoint					= 0;
	ss.dwWaitHint					= 0;


	ControlService(scHandle,SERVICE_CONTROL_STOP,&ss);
	CloseServiceHandle(scHandle);

	return;
}

void vStartService(void)
{
	SC_HANDLE scmHandle = OpenSCManager (NULL, NULL, SC_MANAGER_ALL_ACCESS);

	if (scmHandle == NULL) // Perform error handling.
	{
		WriteLog(sLogFile, "(%d)서비스 매니저와 연결하는데, 오류가 발생하였습니다.\n", GetLastError() );
		return;
	}

	SC_HANDLE scHandle = OpenServiceA( scmHandle, "CUBRIDService", SERVICE_ALL_ACCESS );

	StartService(scHandle,0,NULL);

	CloseServiceHandle(scHandle);

	return;
}







void WriteLog( char* p_logfile, char* p_format, ... ) 
{
	va_list str;
	char    old_logfile[256];
	char  	cur_time[25];
	long    f_size;
	long    f_pos;
	FILE*   logfile_fd = NULL;
	errno_t err;


#define _MAX_LOGFILE_SIZE_	102400


	while( 1 )
	{
		if( p_logfile == NULL )
			logfile_fd = stderr;
		else
			err = fopen_s(&logfile_fd, p_logfile , "a+" );

	
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

			if( f_size > _MAX_LOGFILE_SIZE_ )
			{
				fclose( logfile_fd );
	
				strcpy_s( old_logfile, p_logfile );
				GetCurDateTime( cur_time,"%Y%m%d%H:%M:%S" );
				strcat_s( old_logfile, "." );
				strcat_s( old_logfile, cur_time);
				strcat_s( old_logfile, ".OLD"  );

				rename( p_logfile, old_logfile );
			}
			else
				break;
		}
	}


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

	localtime_s(l_time, &c_time );

	strftime( p_buf, 24 , p_form, l_time );
	
}



