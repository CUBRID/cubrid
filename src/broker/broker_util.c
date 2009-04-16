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
 * broker_util.c - 
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <process.h>
#include <sys/timeb.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif

#ifdef V3_TEST
#include <sys/procfs.h>
#endif

#include "cas_common.h"
#include "broker_env_def.h"
#include "broker_util.h"
#include "broker_filename.h"

static char db_err_log_file[PATH_MAX];
static char as_pid_file_name[PATH_MAX] = "";

int
ut_access_log (int as_index, T_TIMEVAL * start, char error_flag,
	       int error_log_offset)
{
  FILE *fp;
  char *access_log = getenv (ACCESS_LOG_ENV_STR);
  char *script = getenv (PATH_INFO_ENV_STR);
  char *clt_ip = getenv (REMOTE_ADDR_ENV_STR);
  char *clt_appl = getenv (CLT_APPL_NAME_ENV_STR);
  struct tm ct1, ct2;
  time_t t1, t2;
  char *p;
  char err_str[4];
  T_TIMEVAL end;

  TIMEVAL_MAKE (&end);

  t1 = TIMEVAL_GET_SEC (start);
  t2 = TIMEVAL_GET_SEC (&end);
  ct1 = *localtime (&t1);
  ct2 = *localtime (&t2);
  ct1.tm_year += 1900;
  ct2.tm_year += 1900;

  if (access_log == NULL)
    return -1;
  fp = fopen (access_log, "a");
  if (fp == NULL)
    return -1;
  if (script == NULL)
    script = "-";
  if (clt_ip == NULL)
    clt_ip = "-";
  if (clt_appl == NULL)
    clt_appl = "-";
  for (p = clt_appl; *p; p++)
    {
      if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
	*p = '_';
    }

  if (clt_appl[0] == '\0')
    clt_appl = "-";

  if (error_flag == 1)
    sprintf (err_str, "ERR");
  else
    sprintf (err_str, "-");

#ifdef V3_TEST
  fprintf (fp,
	   "%d %s %s %s %d.%03d %d.%03d %02d/%02d/%02d %02d:%02d:%02d ~ %02d/%02d/%02d %02d:%02d:%02d %ld %s %d %d\n",
	   as_index + 1, clt_ip, clt_appl, script, TIMEVAL_GET_SEC (start),
	   TIMEVAL_GET_MSEC (start), TIMEVAL_GET_SEC (&end),
	   TIMEVAL_GET_MSEC (&end), ct1.tm_year, ct1.tm_mon + 1, ct1.tm_mday,
	   ct1.tm_hour, ct1.tm_min, ct1.tm_sec, ct2.tm_year, ct2.tm_mon + 1,
	   ct2.tm_mday, ct2.tm_hour, ct2.tm_min, ct2.tm_sec, getpid (),
	   err_str, error_log_offset, uts_size ());
#else
  fprintf (fp,
	   "%d %s %s %s %d.%03d %d.%03d %02d/%02d/%02d %02d:%02d:%02d ~ %02d/%02d/%02d %02d:%02d:%02d %d %s %d\n",
	   as_index + 1, clt_ip, clt_appl, script, TIMEVAL_GET_SEC (start),
	   TIMEVAL_GET_MSEC (start), TIMEVAL_GET_SEC (&end),
	   TIMEVAL_GET_MSEC (&end), ct1.tm_year, ct1.tm_mon + 1, ct1.tm_mday,
	   ct1.tm_hour, ct1.tm_min, ct1.tm_sec, ct2.tm_year, ct2.tm_mon + 1,
	   ct2.tm_mday, ct2.tm_hour, ct2.tm_min, ct2.tm_sec, (int) getpid (),
	   err_str, error_log_offset);
#endif

  fclose (fp);
  return ((TIMEVAL_GET_SEC (&end)) - (TIMEVAL_GET_SEC (start)));
}

char *
trim (char *str)
{
  char *p;
  char *s;

  if (str == NULL)
    return (str);

  for (s = str;
       *s != '\0' && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r');
       s++)
    ;
  if (*s == '\0')
    {
      *str = '\0';
      return (str);
    }

  /* *s must be a non-white char */
  for (p = s; *p != '\0'; p++)
    ;
  for (p--; *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'; p--)
    ;
  *++p = '\0';

  if (s != str)
    memcpy (str, s, strlen (s) + 1);

  return (str);
}

int
ut_file_lock (char *lock_file)
{
  int fd;

  fd = open (lock_file, O_CREAT | O_EXCL, 0666);
  if (fd < 0)
    {
      if (errno == EEXIST)
	return -1;
#ifdef LOCK_FILE_DEBUG
      {
	FILE *fp;
	fp = fopen ("uts_file_lock.log", "a");
	if (fp != NULL)
	  {
	    fprintf (fp, "[%ld] file lock error. err = [%d], [%s]\n",
		     getpid (), errno, strerror (errno));
	    fclose (fp);
	  }
      }
#endif
      return 0;
    }
  close (fd);
  return 0;
}

void
ut_file_unlock (char *lock_file)
{
  unlink (lock_file);
}

char *
ut_timestamp_to_str (time_t ts)
{
  static char time_str_buf[32];
  struct tm ct;

  ct = *localtime (&ts);
  ct.tm_year += 1900;

  sprintf (time_str_buf, "%02d/%02d/%02d %02d:%02d:%02d",
	   ct.tm_year, ct.tm_mon + 1, ct.tm_mday,
	   ct.tm_hour, ct.tm_min, ct.tm_sec);

  return time_str_buf;
}

#ifdef WIN32
int
ut_kill_process (int pid, char *br_name, int as_index)
{
  HANDLE phandle;

  if (pid <= 0)
    {
      return 0;
    }

  phandle = OpenProcess (PROCESS_TERMINATE, FALSE, pid);
  if (phandle)
    {
      TerminateProcess (phandle, 0);
      CloseHandle (phandle);
      return 0;
    }
  return -1;
}
#else
int
ut_kill_process (int pid, char *br_name, int as_index)
{
  int i;

  if (pid > 0)
    {
      for (i = 0; i < 100; i++)
	{
	  if (kill (pid, SIGTERM) < 0)
	    {
	      goto clear_sock_file;
	    }
	  SLEEP_MILISEC (0, 30);
	  if (kill (pid, 0) < 0)
	    {
	      break;
	    }
	}
      if (i == 100)
	{
	  kill (pid, SIGKILL);
	}
    }

clear_sock_file:

  if (br_name != NULL && as_index >= 0)
    {
      char tmp[PATH_MAX];

      get_cubrid_file (FID_SOCK_DIR, tmp);
      sprintf (tmp, "%s%s.%d", tmp, br_name, as_index + 1);
      unlink (tmp);

      get_cubrid_file (FID_AS_PID_DIR, tmp);
      sprintf (tmp, "%s%s_%d.pid", tmp, br_name, as_index + 1);
      unlink (tmp);
    }

  return 0;
}
#endif

#ifdef WIN32
int
run_child (char *appl_name)
{
  int new_pid;
  char cwd[1024];
  char cmd[1024];
  STARTUPINFO start_info;
  PROCESS_INFORMATION proc_info;
  BOOL res;

  memset (cwd, 0, sizeof (cwd));
  getcwd (cwd, sizeof (cwd));

  GetStartupInfo (&start_info);

  sprintf (cmd, "%s/%s.exe", cwd, appl_name);

  res = CreateProcess (cmd, NULL, NULL, NULL, FALSE,
		       0, NULL, NULL, &start_info, &proc_info);

  if (res == FALSE)
    {
      return 0;
    }

  new_pid = proc_info.dwProcessId;

  CloseHandle (proc_info.hProcess);
  CloseHandle (proc_info.hThread);

  return new_pid;
}
#endif

void
ut_cd_work_dir ()
{
  char *p;

  p = getenv_cubrid_broker ();

  if (p)
    {
      chdir (p);
      chdir ("bin");
    }
}

#ifdef V3_TEST
int
uts_size ()
{
  int procfd;
  struct prpsinfo info;
  const char *procdir = "/proc";
  char pname[128];
  int pid = getpid ();

  sprintf (pname, "%s/%05d", procdir, pid);

retry:
  if ((procfd = open (pname, O_RDONLY)) == -1)
    {
      return -1;
    }

  if (ioctl (procfd, PIOCPSINFO, (char *) &info) == -1)
    {
      int saverr = errno;

      close (procfd);
      if (saverr == EAGAIN)
	goto retry;
      if (saverr != ENOENT)
	;
      return 1;
    }

  close (procfd);

  return (info.pr_bysize);
}
#endif

void
as_pid_file_create (char *br_name, int as_index)
{
  FILE *fp;
  char buf[PATH_MAX];

  sprintf (as_pid_file_name, "%s%s_%d.pid",
	   get_cubrid_file (FID_AS_PID_DIR, buf), br_name, as_index + 1);
  fp = fopen (as_pid_file_name, "w");
  if (fp)
    {
      fprintf (fp, "%d\n", (int) getpid ());
      fclose (fp);
    }
}

void
as_db_err_log_set (char *br_name, int as_index)
{
  char buf[PATH_MAX];

  sprintf (db_err_log_file, "CUBRID_ERROR_LOG=%s%s_%d.err",
	   get_cubrid_file (FID_CUBRID_ERR_DIR, buf), br_name, as_index + 1);
  putenv (db_err_log_file);
}

int
as_get_my_as_info (char *br_name, int *as_index)
{
  char *p, *q, *dir_p;

  p = getenv (PORT_NAME_ENV_STR);
  if (p == NULL)
    return -1;

  q = strrchr (p, '.');
  if (q == NULL)
    return -1;

  dir_p = strrchr (p, '/');
  if (dir_p == NULL)
    return -1;

  *q = '\0';
  strcpy (br_name, dir_p + 1);
  *as_index = atoi (q + 1) - 1;
  *q = '.';

  return 0;
}
