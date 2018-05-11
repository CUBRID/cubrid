/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */


/*
 * process_util.c - functions for process manipulation
 */

#include "process_util.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#if defined(WINDOWS)
#include <string>
#endif

int
create_child_process (const char *const argv[], int wait_flag, const char *stdin_file, char *stdout_file, char *stderr_file,
                      int *exit_status)
{
#if defined(WINDOWS)
  int new_pid;
  STARTUPINFO start_info;
  PROCESS_INFORMATION proc_info;
  BOOL res;
  int i, cmd_arg_len;
  std::string cmd_arg = "";
  char *cmd_arg_ptr;
  BOOL inherit_flag = FALSE;
  HANDLE hStdIn = INVALID_HANDLE_VALUE;
  HANDLE hStdOut = INVALID_HANDLE_VALUE;
  HANDLE hStdErr = INVALID_HANDLE_VALUE;

  if (exit_status != NULL)
    *exit_status = 0;

  for (i = 0, cmd_arg_len = 0; argv[i]; i++)
    {
      std::string arg = "";

      arg += "\"";
      arg += argv[i];
      arg += "\" ";

      cmd_arg += arg;
      cmd_arg_len += arg.size ();
    }

  cmd_arg_ptr = strdup (cmd_arg.c_str ());

  GetStartupInfo (&start_info);
  start_info.wShowWindow = SW_HIDE;

  if (stdin_file)
    {
      hStdIn = CreateFile (stdin_file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
      if (hStdIn != INVALID_HANDLE_VALUE)
	{
	  SetHandleInformation (hStdIn, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
	  start_info.dwFlags = STARTF_USESTDHANDLES;
	  start_info.hStdInput = hStdIn;
	  inherit_flag = TRUE;
	}
    }
  if (stdout_file)
    {
      hStdOut =
	CreateFile (stdout_file, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
      if (hStdOut != INVALID_HANDLE_VALUE)
	{
	  SetHandleInformation (hStdOut, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
	  start_info.dwFlags = STARTF_USESTDHANDLES;
	  start_info.hStdOutput = hStdOut;
	  inherit_flag = TRUE;
	}
    }
  if (stderr_file)
    {
      hStdErr =
	CreateFile (stderr_file, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
		    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
      if (hStdErr != INVALID_HANDLE_VALUE)
	{
	  SetHandleInformation (hStdErr, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
	  start_info.dwFlags = STARTF_USESTDHANDLES;
	  start_info.hStdError = hStdErr;
	  inherit_flag = TRUE;
	}
    }

  res =
    CreateProcess (argv[0], cmd_arg_ptr, NULL, NULL, inherit_flag, CREATE_NO_WINDOW, NULL, NULL, &start_info, &proc_info);
  free(cmd_arg_ptr);

  if (hStdIn != INVALID_HANDLE_VALUE)
    {
      CloseHandle (hStdIn);
    }
  if (hStdOut != INVALID_HANDLE_VALUE)
    {
      CloseHandle (hStdOut);
    }
  if (hStdErr != INVALID_HANDLE_VALUE)
    {
      CloseHandle (hStdErr);
    }

  if (res == FALSE)
    {
      return -1;
    }

  new_pid = proc_info.dwProcessId;

  if (wait_flag)
    {
      DWORD status = 0;
      WaitForSingleObject (proc_info.hProcess, INFINITE);
      GetExitCodeProcess (proc_info.hProcess, &status);
      if (exit_status != NULL)
	*exit_status = status;
      CloseHandle (proc_info.hProcess);
      CloseHandle (proc_info.hThread);
      return 0;
    }
  else
    {
      CloseHandle (proc_info.hProcess);
      CloseHandle (proc_info.hThread);
      return new_pid;
    }
}
#else
  int pid, rc;

  if (exit_status != NULL)
    *exit_status = 0;

  if (wait_flag)
    signal (SIGCHLD, SIG_DFL);
  else
    signal (SIGCHLD, SIG_IGN);
  pid = fork ();
  if (pid == 0)
    {
      FILE *fp;

      if (stdin_file != NULL)
	{
	  fp = fopen (stdin_file, "r");
	  if (fp != NULL)
	    {
	      dup2 (fileno (fp), 0);
	      fclose (fp);
	    }
	}
      if (stdout_file != NULL)
	{
	  unlink (stdout_file);
	  fp = fopen (stdout_file, "w");
	  if (fp != NULL)
	    {
	      dup2 (fileno (fp), 1);
	      fclose (fp);
	    }
	}
      if (stderr_file != NULL)
	{
	  unlink (stderr_file);
	  fp = fopen (stderr_file, "w");
	  if (fp != NULL)
	    {
	      dup2 (fileno (fp), 2);
	      fclose (fp);
	    }
	}

      rc = execv ((const char *) argv[0], (char *const *) argv);
      exit (1);
    }

  if (pid < 0)
    return -1;

  if (wait_flag)
    {
      int status = 0;
      waitpid (pid, &status, 0);
      if (exit_status != NULL)
	*exit_status = WEXITSTATUS(status);
      return 0;
    }
  else
    {
      return pid;
    }
}
#endif
