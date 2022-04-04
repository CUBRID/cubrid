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
#include <sys/stat.h>

static int proc_execute (const char *file, char *args[], bool wait_child, bool close_output, int *out_pid);

void WriteLog (char *p_logfile, char *p_format, ...);
void GetCurDateTime (char *p_buf, char *p_form);
void SendMessage_Tray (int status);

void vKingCHStart (DWORD argc, LPTSTR *argv);
void vHandler (DWORD opcode);
void vSetStatus (DWORD dwState, DWORD dwAccept = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PAUSE_CONTINUE);
void SetCUBRIDEnvVar ();
char *read_string_value_in_registry (HKEY key, char *sub_key, char *name);
SERVICE_STATUS_HANDLE g_hXSS;	// Global handle for service environments
DWORD g_XSS;			// Variable that saves the current state of the service
BOOL g_bPause;			// Store whether the service is stopped or not
HANDLE g_hExitEvent;		// When stopping a service, use an event to stop a thread
BOOL g_isRunning = false;
#define		WM_SERVICE_STOP		WM_USER+1
#define		WM_SERVICE_START	WM_USER+2

#define		SERVICE_CONTROL_BROKER_START	160
#define		SERVICE_CONTROL_BROKER_STOP	161
#define		SERVICE_CONTROL_BROKER_ON   	162
#define		SERVICE_CONTROL_BROKER_OFF 	163
#define		SERVICE_CONTROL_GATEWAY_START	220
#define		SERVICE_CONTROL_GATEWAY_STOP	221
#define		SERVICE_CONTROL_GATEWAY_ON   	222
#define		SERVICE_CONTROL_GATEWAY_OFF 	223
#define		SERVICE_CONTROL_SHARD_START 	200
#define		SERVICE_CONTROL_SHARD_STOP 	201
#define		SERVICE_CONTROL_MANAGER_START	170
#define		SERVICE_CONTROL_MANAGER_STOP	171
#define		SERVICE_CONTROL_SERVER_START	180
#define		SERVICE_CONTROL_SERVER_STOP	181
#define		SERVICE_CONTROL_SERVICE_START	190
#define		SERVICE_CONTROL_SERVICE_STOP	191
#define		SERVICE_CONTROL_JAVASP_START	210
#define		SERVICE_CONTROL_JAVASP_STOP		211

#define		CUBRID_UTIL_CUBRID		"cubrid.exe"
#define		CUBRID_UTIL_SERVICE		"service"
#define		CUBRID_UTIL_BROKER		"broker"
#define		CUBRID_UTIL_GATEWAY		"gateway"
#define		CUBRID_UTIL_SHARD		"shard"
#define		CUBRID_UTIL_MANAGER		"manager"
#define		CUBRID_UTIL_SERVER		"server"
#define 	CUBRID_UTIL_JAVASP 		"javasp"

#define		CUBRID_COMMAND_START		"start"
#define		CUBRID_COMMAND_STOP		"stop"
#define		CUBRID_COMMAND_ON   		"on"
#define		CUBRID_COMMAND_OFF 		"off"

#define		SERVICE_STATUS_STOP  0
#define 	SERVICE_STATUS_START 1

char sLogFile[256] = "CUBRIDService.log";

int
main (int argc, char *argv[])
{
  SetCUBRIDEnvVar ();

  SERVICE_TABLE_ENTRY stbl[] =
  {
    {"CUBRIDService", (LPSERVICE_MAIN_FUNCTION) vKingCHStart},
    {NULL, NULL}
  };

  if (!StartServiceCtrlDispatcher (stbl))
    {
      WriteLog (sLogFile, "StartServiceCtrlDispatcher : error (%d)\n", GetLastError ());
      return 0;
    }

  return 1;
}

void
vKingCHStart (DWORD argc, LPTSTR *argv)
{
  char *args[5];
  char command[100];

  g_hXSS = RegisterServiceCtrlHandlerA ("CUBRIDService", (LPHANDLER_FUNCTION) vHandler);

  if (g_hXSS == 0)
    {
      WriteLog (sLogFile, "RegisterServiceCtrlHandlerA : error (%d)\n", GetLastError ());
      return;
    }

  vSetStatus (SERVICE_START_PENDING);
  g_bPause = FALSE;

  g_hExitEvent = CreateEventA (NULL, TRUE, FALSE, "XServiceExitEvent");

  SendMessage_Tray (SERVICE_STATUS_START);

  if (argc < 2)
    {
      sprintf (command, "%s\\bin\\%s", getenv ("CUBRID"), CUBRID_UTIL_CUBRID);

      args[0] = command;
      args[1] = CUBRID_UTIL_SERVICE;
      args[2] = CUBRID_COMMAND_START;
      args[3] = "--for-windows-service";
      args[4] = NULL;
      proc_execute (command, args, true, true, NULL);
    }

  vSetStatus (SERVICE_RUNNING);

  g_isRunning = true;

  while (1)
    {
      Sleep (2000);

      if (!g_isRunning)
	{
	  break;
	}
    }

  vSetStatus (SERVICE_STOPPED);
}

void
vSetStatus (DWORD dwState, DWORD dwAccept)
{
  SERVICE_STATUS ss;

  ss.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  ss.dwCurrentState = dwState;
  ss.dwControlsAccepted = dwAccept;
  ss.dwWin32ExitCode = 0;
  ss.dwServiceSpecificExitCode = 0;
  ss.dwCheckPoint = 0;
  ss.dwWaitHint = 0;

  // Saves Current state
  g_XSS = dwState;
  SetServiceStatus (g_hXSS, &ss);
}

void
vHandler (DWORD opcode)
{
  char *args[6];
  char command[100];
  char *db_name;

  if (opcode == g_XSS)
    {
      return;
    }

  sprintf (command, "%s\\bin\\%s", getenv ("CUBRID"), CUBRID_UTIL_CUBRID);

  args[0] = command;

  if (opcode == SERVICE_CONTROL_SERVER_START ||
      opcode == SERVICE_CONTROL_SERVER_STOP ||
      opcode == SERVICE_CONTROL_JAVASP_START ||
      opcode == SERVICE_CONTROL_JAVASP_STOP ||
      opcode == SERVICE_CONTROL_BROKER_ON ||
      opcode == SERVICE_CONTROL_BROKER_OFF ||
      opcode == SERVICE_CONTROL_GATEWAY_ON || opcode == SERVICE_CONTROL_GATEWAY_OFF)
    {

      db_name = read_string_value_in_registry (HKEY_LOCAL_MACHINE,
		"SOFTWARE\\CUBRID\\CUBRID",
		"CUBRID_DBNAME_FOR_SERVICE");
      if (db_name == NULL)
	{
	  WriteLog (sLogFile, "read_string_value_in_registry : error \n");
	  return;
	}
      args[3] = db_name;
    }

  switch (opcode)
    {
    case SERVICE_CONTROL_PAUSE:
      vSetStatus (SERVICE_PAUSE_PENDING, 0);
      g_bPause = TRUE;
      vSetStatus (SERVICE_PAUSED);
      return;

    case SERVICE_CONTROL_CONTINUE:
      vSetStatus (SERVICE_CONTINUE_PENDING, 0);
      g_bPause = FALSE;
      vSetStatus (SERVICE_RUNNING);
      return;
    case SERVICE_CONTROL_SERVICE_START:
    {
      args[1] = CUBRID_UTIL_SERVICE;
      args[2] = CUBRID_COMMAND_START;
      args[3] = "--for-windows-service";
      args[4] = NULL;
    }
    break;
    case SERVICE_CONTROL_SERVICE_STOP:
    case SERVICE_CONTROL_STOP:
    {
      SendMessage_Tray (SERVICE_STATUS_STOP);
      vSetStatus (SERVICE_STOP_PENDING, 0);

      args[1] = CUBRID_UTIL_SERVICE;
      args[2] = CUBRID_COMMAND_STOP;
      args[3] = "--for-windows-service";
      args[4] = NULL;
    }
    break;

    case SERVICE_CONTROL_BROKER_START:
    {
      args[1] = CUBRID_UTIL_BROKER;
      args[2] = CUBRID_COMMAND_START;
      args[3] = "--for-windows-service";
      args[4] = NULL;
    }
    break;
    case SERVICE_CONTROL_BROKER_STOP:
    {
      args[1] = CUBRID_UTIL_BROKER;
      args[2] = CUBRID_COMMAND_STOP;
      args[3] = "--for-windows-service";
      args[4] = NULL;
    }
    break;
    case SERVICE_CONTROL_BROKER_ON:
    {
      args[1] = CUBRID_UTIL_BROKER;
      args[2] = CUBRID_COMMAND_ON;
      args[4] = "--for-windows-service";
      args[5] = NULL;
    }
    break;
    case SERVICE_CONTROL_BROKER_OFF:
    {
      args[1] = CUBRID_UTIL_BROKER;
      args[2] = CUBRID_COMMAND_OFF;
      args[4] = "--for-windows-service";
      args[5] = NULL;
    }
    break;
    case SERVICE_CONTROL_GATEWAY_START:
    {
      args[1] = CUBRID_UTIL_GATEWAY;
      args[2] = CUBRID_COMMAND_START;
      args[3] = "--for-windows-service";
      args[4] = NULL;
    }
    break;
    case SERVICE_CONTROL_GATEWAY_STOP:
    {
      args[1] = CUBRID_UTIL_GATEWAY;
      args[2] = CUBRID_COMMAND_STOP;
      args[3] = "--for-windows-service";
      args[4] = NULL;
    }
    break;
    case SERVICE_CONTROL_GATEWAY_ON:
    {
      args[1] = CUBRID_UTIL_GATEWAY;
      args[2] = CUBRID_COMMAND_ON;
      args[4] = "--for-windows-service";
      args[5] = NULL;
    }
    break;
    case SERVICE_CONTROL_GATEWAY_OFF:
    {
      args[1] = CUBRID_UTIL_GATEWAY;
      args[2] = CUBRID_COMMAND_OFF;
      args[4] = "--for-windows-service";
      args[5] = NULL;
    }
    break;
    case SERVICE_CONTROL_SHARD_START:
    {
      args[1] = CUBRID_UTIL_SHARD;
      args[2] = CUBRID_COMMAND_START;
      args[3] = "--for-windows-service";
      args[4] = NULL;
    }
    break;
    case SERVICE_CONTROL_SHARD_STOP:
    {
      args[1] = CUBRID_UTIL_SHARD;
      args[2] = CUBRID_COMMAND_STOP;
      args[3] = "--for-windows-service";
      args[4] = NULL;
    }
    break;
    case SERVICE_CONTROL_MANAGER_START:
    {
      args[1] = CUBRID_UTIL_MANAGER;
      args[2] = CUBRID_COMMAND_START;
      args[3] = "--for-windows-service";
      args[4] = NULL;
    }
    break;
    case SERVICE_CONTROL_MANAGER_STOP:
    {
      args[1] = CUBRID_UTIL_MANAGER;
      args[2] = CUBRID_COMMAND_STOP;
      args[3] = "--for-windows-service";
      args[4] = NULL;
    }
    break;
    case SERVICE_CONTROL_SERVER_START:
    {
      args[1] = CUBRID_UTIL_SERVER;
      args[2] = CUBRID_COMMAND_START;
      args[4] = "--for-windows-service";
      args[5] = NULL;
    }
    break;
    case SERVICE_CONTROL_SERVER_STOP:
    {
      args[1] = CUBRID_UTIL_SERVER;
      args[2] = CUBRID_COMMAND_STOP;
      args[4] = "--for-windows-service";
      args[5] = NULL;
    }
    break;
    case SERVICE_CONTROL_JAVASP_START:
    {
      args[1] = CUBRID_UTIL_JAVASP;
      args[2] = CUBRID_COMMAND_START;
      args[4] = "--for-windows-service";
      args[5] = NULL;
    }
    break;
    case SERVICE_CONTROL_JAVASP_STOP:
    {
      args[1] = CUBRID_UTIL_JAVASP;
      args[2] = CUBRID_COMMAND_STOP;
      args[4] = "--for-windows-service";
      args[5] = NULL;
    }
    break;
    default:
      vSetStatus (g_XSS);
      return;
    }

  proc_execute (command, args, true, true, NULL);

  if (opcode == SERVICE_CONTROL_SERVER_START ||
      opcode == SERVICE_CONTROL_SERVER_STOP ||
      opcode == SERVICE_CONTROL_JAVASP_START ||
      opcode == SERVICE_CONTROL_JAVASP_STOP ||
      opcode == SERVICE_CONTROL_BROKER_ON ||
      opcode == SERVICE_CONTROL_BROKER_OFF ||
      opcode == SERVICE_CONTROL_GATEWAY_ON || opcode == SERVICE_CONTROL_GATEWAY_OFF)
    {
      free (args[3]);
    }

  if (opcode == SERVICE_CONTROL_SERVICE_STOP || opcode == SERVICE_CONTROL_STOP)
    {
      g_isRunning = false;

      // Stop if thread is running
      SetEvent (g_hExitEvent);
      vSetStatus (SERVICE_STOPPED);
    }
}

void
WriteLog (char *p_logfile, char *p_format, ...)
{
  va_list str;
  char old_logfile[256];
  char cur_time[25];
  FILE *logfile_fd;
  struct _stat stat_buf;

#define _MAX_LOGFILE_SIZE_	102400

  if (p_logfile != NULL)
    {
      if ((_stat (p_logfile, &stat_buf) == 0) && (stat_buf.st_size >= _MAX_LOGFILE_SIZE_))
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
	  fprintf (stderr, "WriteLog:Can't open logfile [%s][%d]\n", p_logfile, errno);
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
  struct tm *l_time = NULL;


  time (&c_time);

  l_time = localtime (&c_time);

  strftime (p_buf, 24, p_form, l_time);

}

void
SetCUBRIDEnvVar ()
{
#define BUF_LENGTH 1024

  DWORD dwBufLength = BUF_LENGTH;
  TCHAR sEnvCUBRID[BUF_LENGTH];
  TCHAR sEnvCUBRID_DATABASES[BUF_LENGTH];
  TCHAR sEnvCUBRID_MODE[BUF_LENGTH];
  TCHAR sEnvPath[BUF_LENGTH];

  char szKey[BUF_LENGTH] = "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment";
  char EnvString[BUF_LENGTH];
  HKEY hKey;
  LONG nResult;

  nResult = RegOpenKeyExA (HKEY_LOCAL_MACHINE, szKey, 0, KEY_QUERY_VALUE, &hKey);
  if (nResult != ERROR_SUCCESS)
    {
      return;
    }

#ifdef _DEBUG
  FILE *debugfd = fopen ("C:\\CUBRIDService.log", "w+");
#endif

  dwBufLength = BUF_LENGTH;
  nResult = RegQueryValueEx (hKey, TEXT ("CUBRID"), NULL, NULL, (LPBYTE) sEnvCUBRID, &dwBufLength);
  if (nResult == ERROR_SUCCESS)
    {
      // set CUBRID Environment variable.
      strcpy (EnvString, "CUBRID=");
      strcat (EnvString, (const char *) sEnvCUBRID);
      _putenv (EnvString);

#ifdef _DEBUG
      if (debugfd)
	{
	  fprintf (debugfd, "$CUBRID = %s\n", getenv ("CUBRID"));
	}
#endif
    }

  dwBufLength = BUF_LENGTH;
  nResult = RegQueryValueEx (hKey, TEXT ("CUBRID_DATABASES"), NULL, NULL, (LPBYTE) sEnvCUBRID_DATABASES, &dwBufLength);
  if (nResult == ERROR_SUCCESS)
    {
      // set CUBRID Environment variable.
      strcpy (EnvString, "CUBRID_DATABASES=");
      strcat (EnvString, sEnvCUBRID_DATABASES);
      _putenv (EnvString);
#ifdef _DEBUG
      if (debugfd)
	{
	  fprintf (debugfd, "$CUBRID_DATABASES = %s\n", getenv ("CUBRID_DATABASES"));
	}
#endif
    }

  dwBufLength = BUF_LENGTH;
  nResult = RegQueryValueEx (hKey, TEXT ("CUBRID_MODE"), NULL, NULL, (LPBYTE) sEnvCUBRID_MODE, &dwBufLength);
  if (nResult == ERROR_SUCCESS)
    {
      // set CUBRID Environment variable.
      strcpy (EnvString, "CUBRID_MODE=");
      strcat (EnvString, sEnvCUBRID_MODE);
      _putenv (EnvString);
#ifdef _DEBUG
      if (debugfd)
	{
	  fprintf (debugfd, "$CUBRID_MODE = %s\n", getenv ("CUBRID_MODE"));
	}
#endif
    }

  dwBufLength = BUF_LENGTH;
  nResult = RegQueryValueEx (hKey, TEXT ("Path"), NULL, NULL, (LPBYTE) sEnvPath, &dwBufLength);
  if (nResult == ERROR_SUCCESS)
    {
      // set CUBRID Environment variable.
      strcpy (EnvString, "Path=");
      strcat (EnvString, sEnvPath);
      _putenv (EnvString);
#ifdef _DEBUG
      if (debugfd)
	{
	  fprintf (debugfd, "Path = %s\n", getenv ("Path"));
	}
#endif
    }

#ifdef _DEBUG
  if (debugfd)
    {
      fclose (debugfd);
    }
#endif

  RegCloseKey (hKey);
}

void
SendMessage_Tray (int status)
{
  HWND hTrayWnd;
  hTrayWnd = FindWindowA ("cubrid_tray", "cubrid_tray");

  if (hTrayWnd == NULL)
    {
      return;
    }

  if (status == SERVICE_STATUS_STOP)
    {
      PostMessage (hTrayWnd, WM_SERVICE_STOP, NULL, NULL);
    }
  else if (status == SERVICE_STATUS_START)
    {
      PostMessage (hTrayWnd, WM_SERVICE_START, NULL, NULL);
    }
}

static int
proc_execute (const char *file, char *args[], bool wait_child, bool close_output, int *out_pid)
{
  STARTUPINFO si;
  PROCESS_INFORMATION pi;
  int i, cmd_arg_len;
  char cmd_arg[1024];
  int ret_code = 0;
  bool inherited_handle = TRUE;

  if (out_pid)
    {
      *out_pid = 0;
    }

  for (i = 0, cmd_arg_len = 0; args[i]; i++)
    {
      cmd_arg_len += sprintf (cmd_arg + cmd_arg_len, "\"%s\" ", args[i]);
    }

  GetStartupInfo (&si);
  if (close_output)
    {
      si.dwFlags = si.dwFlags | STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
      si.hStdOutput = NULL;
      si.hStdError = NULL;
      inherited_handle = FALSE;
      si.wShowWindow = SW_HIDE;
    }

  if (!CreateProcess (file, cmd_arg, NULL, NULL, inherited_handle, 0, NULL, NULL, &si, &pi))
    {
      return -1;
    }

  if (wait_child)
    {
      DWORD status = 0;
      WaitForSingleObject (pi.hProcess, INFINITE);
      GetExitCodeProcess (pi.hProcess, &status);
      ret_code = status;
    }
  else
    {
      if (out_pid)
	{
	  *out_pid = pi.dwProcessId;
	}
    }

  CloseHandle (pi.hProcess);
  CloseHandle (pi.hThread);
  return ret_code;
}

char *
read_string_value_in_registry (HKEY key, char *sub_key, char *name)
{
  char *value = NULL;
  HKEY output_key = NULL;

  // open the key
  if (RegOpenKeyEx (key, sub_key, 0, KEY_READ, &output_key) == ERROR_SUCCESS)
    {
      DWORD buf_size = 0;

      if (RegQueryValueEx (output_key, name, NULL, NULL, NULL, &buf_size) == ERROR_SUCCESS)
	{
	  char *buf = (char *) malloc (buf_size * sizeof (char) + 1);
	  if (buf == NULL)
	    {
	      RegCloseKey (output_key);
	      return value;
	    }

	  if (RegQueryValueEx (output_key, name, NULL, NULL, (LPBYTE) buf, &buf_size) == ERROR_SUCCESS)
	    {
	      value = buf;
	    }
	  else
	    {
	      free (buf);
	    }
	}

      RegCloseKey (output_key);
    }

  return value;
}
