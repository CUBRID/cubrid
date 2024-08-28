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


/*
 * process_util.c - functions for process manipulation
 */

#include "process_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#if defined(WINDOWS)
#include <string>
#include <Windows.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#endif
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

int
create_child_process (const char *const argv[], int wait_flag, const char *stdin_file, char *stdout_file,
		      char *stderr_file, int *exit_status)
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
  BOOL rc;

  if (exit_status != NULL)
    {
      *exit_status = -1;
    }

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

      if (hStdIn == INVALID_HANDLE_VALUE)
	{
	  assert (false);
	  return 1;
	}

      SetHandleInformation (hStdIn, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
      start_info.dwFlags = STARTF_USESTDHANDLES;
      start_info.hStdInput = hStdIn;
      inherit_flag = TRUE;
    }
  if (stdout_file)
    {
      hStdOut =
	CreateFile (stdout_file, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
      if (hStdOut == INVALID_HANDLE_VALUE)
	{
	  assert (false);
	  return 1;
	}
      SetHandleInformation (hStdOut, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
      start_info.dwFlags = STARTF_USESTDHANDLES;
      start_info.hStdOutput = hStdOut;
      inherit_flag = TRUE;
    }
  if (stderr_file)
    {
      hStdErr =
	CreateFile (stderr_file, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
		    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
      if (hStdErr == INVALID_HANDLE_VALUE)
	{
	  assert (false);
	  return 1;
	}

      SetHandleInformation (hStdErr, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
      start_info.dwFlags = STARTF_USESTDHANDLES;
      start_info.hStdError = hStdErr;
      inherit_flag = TRUE;
    }

  res =
    CreateProcess (argv[0], cmd_arg_ptr, NULL, NULL, inherit_flag, CREATE_NO_WINDOW, NULL, NULL, &start_info,
		   &proc_info);
  free (cmd_arg_ptr);

  if (res == FALSE)
    {
      return 1;
    }

  if (hStdIn != INVALID_HANDLE_VALUE)
    {
      rc = CloseHandle (hStdIn);
      if (rc == FALSE)
	{
	  assert (false);
	  return 1;
	}
    }
  if (hStdOut != INVALID_HANDLE_VALUE)
    {
      rc = CloseHandle (hStdOut);
      if (rc == FALSE)
	{
	  assert (false);
	  return 1;
	}
    }
  if (hStdErr != INVALID_HANDLE_VALUE)
    {
      rc = CloseHandle (hStdErr);
      if (rc == FALSE)
	{
	  assert (false);
	  return 1;
	}
    }

  new_pid = proc_info.dwProcessId;

  if (wait_flag)
    {
      DWORD status = 0;

      status = WaitForSingleObject (proc_info.hProcess, INFINITE);
      if (status == WAIT_FAILED)
	{
	  assert (false);
	  return 1;
	}
      rc = GetExitCodeProcess (proc_info.hProcess, &status);
      if (rc == FALSE)
	{
	  assert (false);
	  return 1;
	}
      if (exit_status != NULL)
	{
	  *exit_status = status;
	}
      rc = CloseHandle (proc_info.hProcess);
      if (rc == FALSE)
	{
	  assert (false);
	  return 1;
	}
      rc = CloseHandle (proc_info.hThread);
      if (rc == FALSE)
	{
	  assert (false);
	  return 1;
	}
      return 0;
    }
  else
    {
      rc = CloseHandle (proc_info.hProcess);
      if (rc == FALSE)
	{
	  assert (false);
	  return 1;
	}
      rc = CloseHandle (proc_info.hThread);
      if (rc == FALSE)
	{
	  assert (false);
	  return 1;
	}
      return new_pid;
    }
}
#else
  int pid, rc;

  if (exit_status != NULL)
    {
      *exit_status = -1;
    }

  if (wait_flag)
    {
      if (signal (SIGCHLD, SIG_DFL) == SIG_ERR)
	{
	  assert (false);
	  return 1;
	}
    }
  else
    {
      if (signal (SIGCHLD, SIG_IGN) == SIG_ERR)
	{
	  assert (false);
	  return 1;
	}
    }

  pid = fork ();

  if (pid < 0)
    {
      return 1;
    }

  if (pid == 0)
    {
      FILE *fp;

      if (stdin_file != NULL)
	{
	  fp = fopen (stdin_file, "r");
	  if (fp == NULL)
	    {
	      assert (false);
	      return 1;
	    }

	  rc = dup2 (fileno (fp), 0);
	  if (rc == -1)
	    {
	      assert (false);
	      return 1;
	    }
	  rc = fclose (fp);
	  if (rc != 0)
	    {
	      assert (false);
	      return 1;
	    }
	}
      if (stdout_file != NULL)
	{
	  rc = unlink (stdout_file);
	  if (rc == -1)
	    {
	      assert (false);
	      return 1;
	    }
	  fp = fopen (stdout_file, "w");
	  if (fp == NULL)
	    {
	      assert (false);
	      return 1;
	    }
	  rc = dup2 (fileno (fp), 1);
	  if (rc == -1)
	    {
	      assert (false);
	      return 1;
	    }
	  rc = fclose (fp);
	  if (rc != 0)
	    {
	      assert (false);
	      return 1;
	    }
	}
      if (stderr_file != NULL)
	{
	  rc = unlink (stderr_file);
	  if (rc == -1)
	    {
	      assert (false);
	      return 1;
	    }
	  fp = fopen (stderr_file, "w");
	  if (fp == NULL)
	    {
	      assert (false);
	      return 1;
	    }
	  rc = dup2 (fileno (fp), 2);
	  if (rc == -1)
	    {
	      assert (false);
	      return 1;
	    }
	  rc = fclose (fp);
	  if (rc != 0)
	    {
	      assert (false);
	      return 1;
	    }
	}

      rc = execv ((const char *) argv[0], (char *const *) argv);
      assert (false);
      return rc;
    }

  if (wait_flag)
    {
      int status = 0;

      rc = waitpid (pid, &status, 0);
      if (rc == -1)
	{
	  assert (false);
	  return 1;
	}
      if (exit_status != NULL)
	{
	  if (WIFEXITED (status))
	    {
	      *exit_status = WEXITSTATUS (status);
	    }
	  else
	    {
	      *exit_status = -2;
	    }
	}
      return 0;
    }
  else
    {
      return pid;
    }
}
#endif
