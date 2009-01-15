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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

/*
 * server.c - server main
 */

#ident "$Id$"

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#if defined(WINDOWS)
#include <process.h>
#include <winsock2.h>
#include <windows.h>
#include <dbgHelp.h>

#else /* WINDOWS */
#include <unistd.h>
#endif /* WINDOWS */
#if defined(LINUX)
#include <linux/limits.h>
#endif

#include "porting.h"
#include "system_parameter.h"
#include "connection_error.h"
#include "network.h"
#include "environment_variable.h"
#include "boot_sr.h"
#include "system_parameter.h"
#include "perf_monitor.h"

#if defined(WINDOWS)
LONG WINAPI CreateMiniDump (struct _EXCEPTION_POINTERS *pException,
			    char *db_name);
#else /* WINDOWS */
static void register_fatal_signal_handler (int signo);
static void crash_handler (int signo, siginfo_t * siginfo, void *dummyp);
#endif /* WINDOWS */

const char *database_name = "";

#define EXECUTABLE_BIN_DIR      "bin"
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
#endif /* !WINDOWS */

#if defined(WINDOWS)

LONG WINAPI
CreateMiniDump (struct _EXCEPTION_POINTERS *pException, char *db_name)
{
  STARTUPINFO si;
  PROCESS_INFORMATION pi;
  BOOL fSuccess;
  char cmd_line[PATH_MAX];
  TCHAR DumpPath[MAX_PATH] = { 0, };
  SYSTEMTIME SystemTime;
  HANDLE FileHandle;
  char *cubid_env;

  GetLocalTime (&SystemTime);

  sprintf (DumpPath, "%s\\%s\\%d-%d-%d %d_%d_%d.dmp",
	   envvar_root (),
	   EXECUTABLE_BIN_DIR,
	   SystemTime.wYear,
	   SystemTime.wMonth,
	   SystemTime.wDay,
	   SystemTime.wHour, SystemTime.wMinute, SystemTime.wSecond);

  FileHandle = CreateFile (DumpPath,
			   GENERIC_WRITE,
			   FILE_SHARE_WRITE,
			   NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

  if (FileHandle != INVALID_HANDLE_VALUE)
    {
      MINIDUMP_EXCEPTION_INFORMATION MiniDumpExceptionInfo;
      BOOL Success;

      MiniDumpExceptionInfo.ThreadId = GetCurrentThreadId ();
      MiniDumpExceptionInfo.ExceptionPointers = pException;
      MiniDumpExceptionInfo.ClientPointers = FALSE;

      Success = MiniDumpWriteDump (GetCurrentProcess (),
				   GetCurrentProcessId (),
				   FileHandle,
				   MiniDumpNormal,
				   (pException) ? &MiniDumpExceptionInfo :
				   NULL, NULL, NULL);
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

  if (signo != SIGABRT && siginfo != NULL && siginfo->si_code <= 0)
    {
      register_fatal_signal_handler (signo);
      return;
    }

  if (os_set_signal_handler (signo, SIG_DFL) == SIG_ERR)
    return;

  if (!BO_ISSERVER_RESTARTED () || !PRM_AUTO_RESTART_SERVER)
    return;

  close_diag_mgr ();

  pid = fork ();
  if (pid == 0)
    {
      char *installed_path;
      char err_log[PATH_MAX];
      int ppid;
      int fd, fd_max;
#if defined (sun)
      struct rlimit rlp;
#endif

#if defined (sun)
      if (getrlimit (RLIMIT_NOFILE, &rlp) == 0)
	fd_max = MIN (1024, rlp.rlim_cur);
#elif defined(HPUX)
      fd_max = _POSIX_OPEN_MAX;
#elif defined(OPEN_MAX)
      fd_max = OPEN_MAX;
#else
      fd_max = sysconf (_SC_OPEN_MAX);
#endif

      for (fd = 3; fd < fd_max; fd++)
	close (fd);

      ppid = getppid ();
      while (1)
	{
	  if (kill (ppid, 0) < 0)
	    break;
	  sleep (1);
	}

      unmask_signal (signo);

      if (PRM_ER_LOG_FILE != NULL)
	{
	  snprintf (err_log, PATH_MAX, "%s.%d", PRM_ER_LOG_FILE, ppid);
	  rename (PRM_ER_LOG_FILE, err_log);
	}

      execl (executable_path, executable_path, database_name, NULL);
      exit (0);
    }
}
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
  char *bn;
  int ret_val;
#if !defined(WINDOWS)
  sigset_t sigurg_mask;
#endif

#if defined(WINDOWS)
  __try
  {
#else /* WINDOWS */
  register_fatal_signal_handler (SIGABRT);
  register_fatal_signal_handler (SIGILL);
  register_fatal_signal_handler (SIGFPE);
  register_fatal_signal_handler (SIGBUS);
  register_fatal_signal_handler (SIGSEGV);
  register_fatal_signal_handler (SIGSYS);
#endif /* WINDOWS */

#if !defined(WINDOWS)
  /* Block SIGURG signal except oob-handler thread */
  sigemptyset (&sigurg_mask);
  sigaddset (&sigurg_mask, SIGURG);
  sigprocmask (SIG_BLOCK, &sigurg_mask, NULL);
#endif /* !WINDOWS */

  if (argc < 2)
    {
      fprintf (stderr, "Usage: server databasename\n");
      return 1;
    }

  /* save executable path */
  bn = basename (argv[0]);
#if defined (WINDOWS)
  (void) snprintf (executable_path, PATH_MAX, "%s\\%s\\%s", envvar_root (),
		   EXECUTABLE_BIN_DIR, bn);


#else /* WINDOWS */
  (void) snprintf (executable_path, PATH_MAX, "%s/%s/%s", envvar_root (),
		   EXECUTABLE_BIN_DIR, bn);
#endif /* !WINDOWS */

  ret_val = net_server_start (argv[1]);

#if defined(WINDOWS)
} __except (CreateMiniDump (GetExceptionInformation (), argv[1]))
{
}
#endif

return ret_val;
}
