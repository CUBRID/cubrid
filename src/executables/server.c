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
 * server.c - server main
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#if defined(WINDOWS)
#include <process.h>
#include <winsock2.h>
#include <windows.h>
#include <dbgHelp.h>
#include <assert.h>

#else /* WINDOWS */
#include <sys/resource.h>
#include <unistd.h>
#include <limits.h>
#endif /* WINDOWS */

#include "porting.h"
#include "system_parameter.h"
#include "connection_error.h"
#include "network.h"
#include "environment_variable.h"
#include "boot_sr.h"
#include "system_parameter.h"
#include "perf_monitor.h"
#include "util_func.h"
#if defined(WINDOWS)
#include "wintcp.h"
#else /* !defined (WINDOWS) */
#include "tcp.h"
#include "heartbeat.h"
#include "log_impl.h"
#endif /* !defined (WINDOWS) */

#if defined(WINDOWS)
LONG WINAPI CreateMiniDump (struct _EXCEPTION_POINTERS *pException, char *db_name);
#else /* WINDOWS */
static void register_fatal_signal_handler (int signo);
static void crash_handler (int signo, siginfo_t * siginfo, void *dummyp);
#if !defined (NDEBUG)
static void abort_handler (int signo, siginfo_t * siginfo, void *dummyp);
#endif /* !NDEBUG */
#endif /* WINDOWS */

static const char *database_name = "";

static char executable_path[PATH_MAX];

#if !defined(WINDOWS)
/*
 * unmask_signal(): unmask the given signal
 *
 *   returns: 0 for SUCCESS, -1 for otherwise
 *   signo(IN): signo to handle
 *
 */

static int
unmask_signal (int signo)
{
  sigset_t sigset;

  sigemptyset (&sigset);
  sigaddset (&sigset, signo);
  return sigprocmask (SIG_UNBLOCK, &sigset, NULL);
}

/*
 * register_fatal_signal_hander(): register the handler of the given signal
 *
 *   returns: none
 *   signo(IN): signo to handle
 *
 */

static void
register_fatal_signal_handler (int signo)
{
  struct sigaction act;

  act.sa_handler = NULL;
  act.sa_sigaction = crash_handler;
  sigemptyset (&act.sa_mask);
  act.sa_flags = 0;
  act.sa_flags |= SA_SIGINFO;
  sigaction (signo, &act, NULL);
}

#if !defined (NDEBUG)
static void
register_abort_signal_handler (int signo)
{
  struct sigaction act;

  act.sa_handler = NULL;
  act.sa_sigaction = abort_handler;
  sigemptyset (&act.sa_mask);
  act.sa_flags = 0;
  act.sa_flags |= SA_SIGINFO;
  sigaction (signo, &act, NULL);
}
#endif /* !NDEBUG */
#endif /* !WINDOWS */

#if defined(WINDOWS)

LONG WINAPI
CreateMiniDump (struct _EXCEPTION_POINTERS *pException, char *db_name)
{
  STARTUPINFO si;
  PROCESS_INFORMATION pi;
  BOOL fSuccess;
  char cmd_line[PATH_MAX];
  TCHAR DumpFile[MAX_PATH] = { 0, };
  TCHAR DumpPath[MAX_PATH] = { 0, };
  SYSTEMTIME SystemTime;
  HANDLE FileHandle;

  GetLocalTime (&SystemTime);

  sprintf (DumpFile, "%d-%d-%d %d_%d_%d.dmp", SystemTime.wYear, SystemTime.wMonth, SystemTime.wDay, SystemTime.wHour,
	   SystemTime.wMinute, SystemTime.wSecond);
  envvar_bindir_file (DumpPath, MAX_PATH, DumpFile);

  FileHandle = CreateFile (DumpPath, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

  if (FileHandle != INVALID_HANDLE_VALUE)
    {
      MINIDUMP_EXCEPTION_INFORMATION MiniDumpExceptionInfo;
      BOOL Success;

      MiniDumpExceptionInfo.ThreadId = GetCurrentThreadId ();
      MiniDumpExceptionInfo.ExceptionPointers = pException;
      MiniDumpExceptionInfo.ClientPointers = FALSE;

      Success =
	MiniDumpWriteDump (GetCurrentProcess (), GetCurrentProcessId (), FileHandle, MiniDumpNormal,
			   (pException) ? &MiniDumpExceptionInfo : NULL, NULL, NULL);
    }

  CloseHandle (FileHandle);

  /* restart cub_server.exe */
  GetStartupInfo (&si);

  snprintf (cmd_line, PATH_MAX, "\"%s\" \"%s\"", executable_path, db_name);

  fSuccess = CreateProcess (executable_path, cmd_line,
			    /* Default process security attrs */
			    NULL,
			    /* Default thread security attrs */
			    NULL,
			    /* Don't inherit handles */
			    FALSE,
			    /* normal priority */
			    0,
			    /* Use the same environment as parent */
			    NULL,
			    /* Launch in the current directory */
			    NULL,
			    /* start up information */
			    &si, &pi);

  return EXCEPTION_EXECUTE_HANDLER;
}

#else /* WINDOWS */

/*
 * crash_handler(): kill the server and spawn the new server process
 *
 *   returns: none
 *   signo(IN): signo to handle
 *   siginfo(IN): siginfo struct
 *   dummyp(IN): this argument will not be used,
 *               but remains to cope with its function prototype.
 *
 */

static void
crash_handler (int signo, siginfo_t * siginfo, void *dummyp)
{
  int pid;

  _er_log_debug (ARG_FILE_LINE, "cub_server(pid=%d) received the signal(signo=%d).", getpid (), signo);

  if (os_set_signal_handler (signo, SIG_DFL) == SIG_ERR)
    {
      return;
    }

  if (!BO_IS_SERVER_RESTARTED () || !prm_get_bool_value (PRM_ID_AUTO_RESTART_SERVER))
    {
      return;
    }

  _er_log_debug (ARG_FILE_LINE, "cub_server(pid=%d) will be restarted.", getpid ());

  signal (SIGCHLD, SIG_IGN);

  pid = fork ();
  if (pid == 0)			/* child process */
    {
      char err_log[PATH_MAX];
      int ppid;
      int fd, fd_max;

      signal (SIGCHLD, SIG_DFL);

      fd_max = css_get_max_socket_fds ();

      for (fd = 3; fd < fd_max; fd++)
	{
	  close (fd);
	}

      ppid = getppid ();
      while (1)
	{
	  if (kill (ppid, 0) < 0)
	    {
	      if (errno != ESRCH)
		{
		  kill (ppid, SIGKILL);
		}

	      break;
	    }

	  sleep (1);
	}

      unmask_signal (signo);

      if (prm_get_string_value (PRM_ID_ER_LOG_FILE) != NULL)
	{
	  snprintf (err_log, PATH_MAX, "%s.%d", prm_get_string_value (PRM_ID_ER_LOG_FILE), ppid);
	  rename (prm_get_string_value (PRM_ID_ER_LOG_FILE), err_log);
	}

      execl (executable_path, fileio_get_base_file_name (executable_path), database_name, NULL);
      exit (0);
    }
  else				/* parent process or fork error */
    {
      exit (-1);
    }
}

#if !defined (NDEBUG)
static void
abort_handler (int signo, siginfo_t * siginfo, void *dummyp)
{
  int *local_clients_pid = NULL;
  int i, num_clients, client_pid;

  if (os_set_signal_handler (signo, SIG_DFL) == SIG_ERR)
    {
      return;
    }

  if (!BO_IS_SERVER_RESTARTED ())
    {
      return;
    }

  num_clients = logtb_collect_local_clients (&local_clients_pid);

  for (i = 0; i < num_clients; i++)
    {
      client_pid = local_clients_pid[i];
      if (client_pid == 0)
	{
	  /* reached the end. */
	  break;
	}

      assert (client_pid > 0);

      kill (client_pid, SIGABRT);
    }

  if (local_clients_pid != NULL)
    {
      free (local_clients_pid);
    }

  /* abort the server itself */
  abort ();
}
#endif /* !NDEBUG */
#endif /* WINDOWS */

/*
 * main(): server's main function
 *
 *   returns: 0 for SUCCESS, non-zero for ERROR
 *
 */
int
main (int argc, char **argv)
{
  char *binary_name;
  int ret_val = 0;

#if defined(WINDOWS)
  FreeConsole ();

  __try
#else /* WINDOWS */
#if !defined (NDEBUG)
  register_abort_signal_handler (SIGABRT);
#else
  register_fatal_signal_handler (SIGABRT);
#endif
  register_fatal_signal_handler (SIGILL);
  register_fatal_signal_handler (SIGFPE);
  register_fatal_signal_handler (SIGBUS);
  register_fatal_signal_handler (SIGSEGV);
  register_fatal_signal_handler (SIGSYS);
#endif /* WINDOWS */

  {				/* to make indent happy */
    if (argc < 2)
      {
	PRINT_AND_LOG_ERR_MSG ("Usage: server databasename\n");
	return 1;
      }

    fprintf (stdout, "\nThis may take a long time depending on the amount " "of recovery works to do.\n");

    /* save executable path */
    binary_name = basename (argv[0]);
    (void) envvar_bindir_file (executable_path, PATH_MAX, binary_name);
    /* save database name */
    database_name = argv[1];

#if !defined(WINDOWS)
    hb_set_exec_path (executable_path);
    hb_set_argv (argv);

    /* create a new session */
    setsid ();
#endif

    ret_val = net_server_start (database_name);

  }
#if defined(WINDOWS)
  __except (CreateMiniDump (GetExceptionInformation (), argv[1]))
  {
  }
#endif
  return ret_val;
}
