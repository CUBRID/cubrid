/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA 
 *
 */

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
#include <sys/stat.h>


#define		CUBRID_UTIL_SERVICE			"service"
#define		CUBRID_UTIL_BROKER			"broker"
#define		CUBRID_UTIL_MANAGER			"manager"
#define		CUBRID_UTIL_SERVER			"server"

#define		CUBRID_COMMAND_START			"start"
#define		CUBRID_COMMAND_STOP			"stop"
#define		CUBRID_COMMAND_ON   			"on"
#define		CUBRID_COMMAND_OFF 			"off"

#define		SERVICE_CONTROL_BROKER_START	160
#define		SERVICE_CONTROL_BROKER_STOP	161
#define		SERVICE_CONTROL_BROKER_ON   	162
#define		SERVICE_CONTROL_BROKER_OFF 	163
#define		SERVICE_CONTROL_MANAGER_START	170
#define		SERVICE_CONTROL_MANAGER_STOP	171
#define		SERVICE_CONTROL_SERVER_START	180
#define		SERVICE_CONTROL_SERVER_STOP	181
#define		SERVICE_CONTROL_SERVICE_START	190
#define		SERVICE_CONTROL_SERVICE_STOP	191

void WriteLog (char *p_logfile, char *p_format, ...);
void GetCurDateTime (char *p_buf, char *p_form);

char sLogFile[256] = "CUBRIDService.log";
char sCmd[256] = "CUBRIDService.exe";
char sExecPath[1024] = "";

void vctrlService (void);
void vDelService (void);
void vStartService (void);
void vStopService (void);
void vPrintServiceStatus (void);

int
_tmain (int argc, char *argv[])
{
  bool rc;

  // Install a Service if -i switch used
  if (argc == 2)
    {
      if (_stricmp (argv[1], "-u") == 0)
	{
	  vDelService ();
	}
      else if (_stricmp (argv[1], "-start") == 0)
	{
	  vStartService ();
	}
      else if (_stricmp (argv[1], "-stop") == 0)
	{
	  vStopService ();
	}
      else if (_stricmp (argv[1], "-status") == 0)
	{
	  vPrintServiceStatus ();
	}
      else
	WriteLog (sLogFile, "Invalid Argument.\n");
    }
  else if (argc == 3)
    {
      if (_stricmp (argv[1], "-i") == 0)
	{
	  if (strlen (argv[2]) > 0)
	    strcpy_s (sExecPath, argv[2]);

	  vctrlService ();
	}
      else
	{
	  SERVICE_STATUS ss;
	  int service_control_code;

	  SC_HANDLE scmHandle =
	    OpenSCManager (NULL, NULL, SC_MANAGER_ALL_ACCESS);

	  if (scmHandle == NULL)	// Perform error handling.
	    {
	      WriteLog (sLogFile,
			"(%d)Cannot connect to the Windows Service Control Manager.\n",
			GetLastError ());
	      return 0;
	    }

	  SC_HANDLE scHandle =
	    OpenServiceA (scmHandle, "CUBRIDService", SERVICE_ALL_ACCESS);

	  if (_stricmp (argv[1], CUBRID_UTIL_SERVICE) == 0)
	    {
	      if (_stricmp (argv[2], CUBRID_COMMAND_START) == 0)
		{
		  service_control_code = SERVICE_CONTROL_SERVICE_START;
		}
	      else if (_stricmp (argv[2], CUBRID_COMMAND_STOP) == 0)
		{
		  service_control_code = SERVICE_CONTROL_SERVICE_STOP;
		}
	      else
		{
		  WriteLog (sLogFile, "Invalid Argument.\n");
		  CloseServiceHandle (scHandle);
		  CloseServiceHandle (scmHandle);
		  return 0;
		}
	    }
	  else if (_stricmp (argv[1], CUBRID_UTIL_BROKER) == 0)
	    {
	      if (_stricmp (argv[2], CUBRID_COMMAND_START) == 0)
		{
		  service_control_code = SERVICE_CONTROL_BROKER_START;
		}
	      else if (_stricmp (argv[2], CUBRID_COMMAND_STOP) == 0)
		{
		  service_control_code = SERVICE_CONTROL_BROKER_STOP;
		}
	      else
		{
		  WriteLog (sLogFile, "Invalid Argument.\n");
		  CloseServiceHandle (scHandle);
		  CloseServiceHandle (scmHandle);
		  return 0;
		}
	    }
	  else if (_stricmp (argv[1], CUBRID_UTIL_MANAGER) == 0)
	    {
	      if (_stricmp (argv[2], CUBRID_COMMAND_START) == 0)
		{
		  service_control_code = SERVICE_CONTROL_MANAGER_START;
		}
	      else if (_stricmp (argv[2], CUBRID_COMMAND_STOP) == 0)
		{
		  service_control_code = SERVICE_CONTROL_MANAGER_STOP;
		}
	      else
		{
		  WriteLog (sLogFile, "Invalid Argument.\n");
		  CloseServiceHandle (scHandle);
		  CloseServiceHandle (scmHandle);
		  return 0;
		}
	    }
	  else
	    {
	      WriteLog (sLogFile, "Invalid Argument.\n");
	      CloseServiceHandle (scHandle);
	      CloseServiceHandle (scmHandle);
	      return 0;
	    }

	  ControlService (scHandle, SERVICE_CONTROL_INTERROGATE, &ss);
	  if (ss.dwCurrentState == SERVICE_STOPPED)
	    {
	      LPCTSTR argv[2] =
		{ "CUBRIDService", "--dont-start-cubrid-process" };

	      if (service_control_code == SERVICE_CONTROL_SERVICE_STOP)
		{
		  CloseServiceHandle (scHandle);
		  CloseServiceHandle (scmHandle);
		  return 0;
		}

	      StartService (scHandle, 2, argv);
	      Sleep (2000);

	      do
		{
		  ControlService (scHandle, SERVICE_CONTROL_INTERROGATE, &ss);
		  Sleep (100);
		}
	      while (ss.dwCurrentState == SERVICE_START_PENDING);
	    }

	  // send control code
	  rc = ControlService (scHandle, service_control_code, &ss);
	  if (!rc && ss.dwCurrentState == SERVICE_RUNNING
	    && GetLastError () == ERROR_SERVICE_REQUEST_TIMEOUT)
	    {
	      if (!ControlService (scHandle, SERVICE_CONTROL_INTERROGATE, &ss))
		{
		  WriteLog (sLogFile,
		      "ControlService error. check status manually.\n");
		}
	    }
	  CloseServiceHandle (scHandle);
	  CloseServiceHandle (scmHandle);
	}
    }
  else if (argc == 4)
    {
      if (_stricmp (argv[1], CUBRID_UTIL_SERVER) == 0 ||
          _stricmp (argv[1], CUBRID_UTIL_BROKER) == 0)
	{
	  SERVICE_STATUS ss;
	  int service_control_code;
	  SERVICE_DESCRIPTION service_description;

	  SC_HANDLE scmHandle =
	    OpenSCManager (NULL, NULL, SC_MANAGER_ALL_ACCESS);

	  if (scmHandle == NULL)	// Perform error handling.
	    {
	      WriteLog (sLogFile,
			"(%d)Cannot connect to the Windows Service Control Manager.\n",
			GetLastError ());
	      return 0;
	    }

	  SC_HANDLE scHandle =
	    OpenServiceA (scmHandle, "CUBRIDService", SERVICE_ALL_ACCESS);
	  if (_stricmp (argv[1], CUBRID_UTIL_SERVER) == 0 &&
	      _stricmp (argv[2], CUBRID_COMMAND_START) == 0)
	    {
	      service_control_code = SERVICE_CONTROL_SERVER_START;
	    }
	  else if (_stricmp (argv[1], CUBRID_UTIL_SERVER) == 0 &&
	           _stricmp (argv[2], CUBRID_COMMAND_STOP) == 0)
	    {
	      service_control_code = SERVICE_CONTROL_SERVER_STOP;
	    }
	  else if (_stricmp (argv[1], CUBRID_UTIL_BROKER) == 0 &&
	           _stricmp (argv[2], CUBRID_COMMAND_ON) == 0)
	    {
	      service_control_code = SERVICE_CONTROL_BROKER_ON;
	    }
	  else if (_stricmp (argv[1], CUBRID_UTIL_BROKER) == 0 &&
	           _stricmp (argv[2], CUBRID_COMMAND_OFF) == 0)
	    {
	      service_control_code = SERVICE_CONTROL_BROKER_OFF;
	    }
	  else
	    {
	      WriteLog (sLogFile, "Invalid Argument.\n");
	      CloseServiceHandle (scHandle);
	      CloseServiceHandle (scmHandle);
	      return 0;
	    }

	  ControlService (scHandle, SERVICE_CONTROL_INTERROGATE, &ss);
	  if (ss.dwCurrentState == SERVICE_STOPPED)
	    {
	      LPCTSTR argv[2] =
		{ "CUBRIDService", "--dont-start-cubrid-process" };
	      StartService (scHandle, 2, argv);
	      Sleep (2000);

	      do
		{
		  ControlService (scHandle, SERVICE_CONTROL_INTERROGATE, &ss);
		  Sleep (100);
		}
	      while (ss.dwCurrentState == SERVICE_START_PENDING);
	    }

	  // register server name
	  service_description.lpDescription = TEXT (argv[3]);

	  if (ChangeServiceConfig2 (scHandle,
				    SERVICE_CONFIG_DESCRIPTION,
				    (LPVOID) & service_description) == 0)
	    {
	      WriteLog (sLogFile, "ChangeServiceConfig error.\n");
	      CloseServiceHandle (scHandle);
	      CloseServiceHandle (scmHandle);
	      return 0;
	    }

	  // send control code
	  rc = ControlService (scHandle, service_control_code, &ss);
	  if (!rc && ss.dwCurrentState == SERVICE_RUNNING
	    && GetLastError () == ERROR_SERVICE_REQUEST_TIMEOUT)
	    {
	      if (!ControlService (scHandle, SERVICE_CONTROL_INTERROGATE, &ss))
		{
		  WriteLog (sLogFile,
		      "ControlService error. check status manually.\n");
		}
	    }
	  CloseServiceHandle (scHandle);
	  CloseServiceHandle (scmHandle);
	}
    }
  else
    WriteLog (sLogFile, "Invalid Argument.\n");

  return 0;
}


void
vctrlService (void)
{
  char ServiceFilePath[1024] = "";

  SC_HANDLE scmHandle = OpenSCManager (NULL, NULL, SC_MANAGER_ALL_ACCESS);

  if (scmHandle == NULL)	// Perform error handling.
    {
      WriteLog (sLogFile,
		"(%d)Cannot connect to the Windows Service Control Manager.\n",
		GetLastError ());
      return;
    }

  sprintf_s (ServiceFilePath, "%s\\%s", sExecPath, sCmd);

  SC_HANDLE scHandle = CreateServiceA (scmHandle,
				       "CUBRIDService",
				       "CUBRIDService",
				       SERVICE_ALL_ACCESS,
				       SERVICE_WIN32_OWN_PROCESS |
				       SERVICE_INTERACTIVE_PROCESS,
				       SERVICE_AUTO_START,
				       SERVICE_ERROR_NORMAL,
				       ServiceFilePath,
				       NULL,
				       NULL,
				       NULL,
				       NULL,
				       NULL);

  if (scHandle == NULL)		// Process error
    {
      WriteLog (sLogFile,
		"(%d)Cannot add a Windows Service for CUBRID to the Windows Service Control Manager.\n",
		GetLastError ());
      return;
    }

  CloseServiceHandle (scHandle);
  CloseServiceHandle (scmHandle);

  return;
}


void
vDelService (void)
{
  SC_HANDLE scmHandle = OpenSCManager (NULL, NULL, SC_MANAGER_ALL_ACCESS);

  if (scmHandle == NULL)	// Perform error handling.
    {
      WriteLog (sLogFile,
		"(%d)Cannot connect to the Windows Service Control Manager.\n",
		GetLastError ());
      return;
    }

  SC_HANDLE scHandle =
    OpenServiceA (scmHandle, "CUBRIDService", SERVICE_ALL_ACCESS);

  SERVICE_STATUS ss;

  ss.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  ss.dwCurrentState = SERVICE_STOP_PENDING;
  ss.dwControlsAccepted = 0;
  ss.dwWin32ExitCode = 0;
  ss.dwServiceSpecificExitCode = 0;
  ss.dwCheckPoint = 0;
  ss.dwWaitHint = 0;


  ControlService (scHandle, SERVICE_CONTROL_STOP, &ss);
  DeleteService (scHandle);

  return;
}


void
vStopService (void)
{
  SC_HANDLE scmHandle = OpenSCManager (NULL, NULL, SC_MANAGER_ALL_ACCESS);

  if (scmHandle == NULL)	// Perform error handling.
    {
      WriteLog (sLogFile,
		"(%d)Cannot connect to the Windows Service Control Manager.\n",
		GetLastError ());
      return;
    }

  SC_HANDLE scHandle =
    OpenServiceA (scmHandle, "CUBRIDService", SERVICE_ALL_ACCESS);

  SERVICE_STATUS ss;

  ss.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  ss.dwCurrentState = SERVICE_STOP_PENDING;
  ss.dwControlsAccepted = 0;
  ss.dwWin32ExitCode = 0;
  ss.dwServiceSpecificExitCode = 0;
  ss.dwCheckPoint = 0;
  ss.dwWaitHint = 0;

  ControlService (scHandle, SERVICE_CONTROL_STOP, &ss);

  Sleep (2000);

  do
    {
      ControlService (scHandle, SERVICE_CONTROL_INTERROGATE, &ss);
      Sleep (100);
    }
  while (ss.dwCurrentState == SERVICE_STOP_PENDING);

  CloseServiceHandle (scHandle);

  return;
}

void
vStartService (void)
{
  SERVICE_STATUS ss;
  SC_HANDLE scmHandle, scHandle;

  scmHandle = OpenSCManager (NULL, NULL, SC_MANAGER_ALL_ACCESS);

  if (scmHandle == NULL)
    {
      return;
    }

  scHandle = OpenServiceA (scmHandle, "CUBRIDService", SERVICE_ALL_ACCESS);

  StartService (scHandle, 0, NULL);
  Sleep (2000);

  do
    {
      ControlService (scHandle, SERVICE_CONTROL_INTERROGATE, &ss);
      Sleep (100);
    }
  while (ss.dwCurrentState == SERVICE_START_PENDING);

  CloseServiceHandle (scHandle);
  CloseServiceHandle (scmHandle);

  return;
}

void
vPrintServiceStatus ()
{
  SERVICE_STATUS ss;
  SC_HANDLE scmHandle, scHandle;

  scmHandle = OpenSCManager (NULL, NULL, SC_MANAGER_ALL_ACCESS);

  if (scmHandle == NULL)
    {
      return;
    }

  scHandle = OpenServiceA (scmHandle, "CUBRIDService", SERVICE_ALL_ACCESS);

  ControlService (scHandle, SERVICE_CONTROL_INTERROGATE, &ss);

  if (ss.dwCurrentState == SERVICE_STOPPED)
    {
      fprintf (stdout, "SERVICE_STOPPED\n");
    }
  else
    {
      fprintf (stdout, "SERVICE_RUNNING\n");
    }

  CloseServiceHandle (scHandle);
  CloseServiceHandle (scmHandle);

  return;
}

void
WriteLog (char *p_logfile, char *p_format, ...)
{
  va_list str;
  char old_logfile[256];
  char cur_time[25];
  FILE *logfile_fd = NULL;
  struct _stat stat_buf;

#define _MAX_LOGFILE_SIZE_	102400

  if (p_logfile != NULL)
    {
      if ((_stat (p_logfile, &stat_buf) == 0) &&
	  (stat_buf.st_size >= _MAX_LOGFILE_SIZE_))
	{
	  strcpy_s (old_logfile, p_logfile);
	  strcat_s (old_logfile, ".bak");

	  remove (old_logfile);

	  if (rename (p_logfile, old_logfile) != 0)
	    {
	      fprintf (stderr, "WriteLog:rename error\n");
	      return;
	    }
	}

      fopen_s (&logfile_fd, p_logfile, "a+");

      if (logfile_fd == NULL)
	{
	  fprintf (stderr, "WriteLog:Can't open logfile [%s][%d]\n",
		   p_logfile, errno);
	  return;
	}
    }
  else
    {
      logfile_fd = stderr;
    }

#ifndef __DEBUG
  GetCurDateTime (cur_time, "%Y%m%d %H:%M:%S");
  fprintf (logfile_fd, "[%s] ", cur_time);
#endif

  va_start (str, p_format);
  vfprintf (logfile_fd, p_format, str);
  va_end (str);

  if (p_logfile != NULL)
    {
      fclose (logfile_fd);
    }
}




void
GetCurDateTime (char *p_buf, char *p_form)
{
  time_t c_time;
  struct tm l_time = { 0 };

  time (&c_time);
  localtime_s (&l_time, &c_time);
  strftime (p_buf, 24, p_form, &l_time);
}
