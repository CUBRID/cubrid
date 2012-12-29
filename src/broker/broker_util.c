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
#include <assert.h>

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <process.h>
#include <sys/timeb.h>
#include <mstcpip.h>
#else
#include <unistd.h>
#include <sys/time.h>
#include <sys/timeb.h>
#include <netinet/tcp.h>
#endif

#ifdef V3_TEST
#include <sys/procfs.h>
#endif

#include "porting.h"
#include "cas_common.h"
#include "broker_env_def.h"
#include "broker_util.h"
#include "broker_filename.h"
#include "environment_variable.h"

static char db_err_log_file[PATH_MAX];
static char as_pid_file_name[PATH_MAX] = "";

#if defined (ENABLE_UNUSED_FUNCTION)
int
ut_access_log (int as_index, struct timeval *start_time, char error_flag,
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
  struct timeval end_time;

  gettimeofday (&end_time, NULL);

  t1 = start_time->tv_sec;
  t2 = end_time.tv_sec;
  if (localtime_r (&t1, &ct1) == NULL || localtime_r (&t2, &ct2) == NULL)
    {
      return -1;
    }
  ct1.tm_year += 1900;
  ct2.tm_year += 1900;

  if (access_log == NULL)
    return -1;
  fp = fopen (access_log, "a");
  if (fp == NULL)
    return -1;
  if (script == NULL)
    script = (char *) "-";
  if (clt_ip == NULL)
    clt_ip = (char *) "-";
  if (clt_appl == NULL)
    clt_appl = (char *) "-";
  for (p = clt_appl; *p; p++)
    {
      if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
	*p = '_';
    }

  if (clt_appl[0] == '\0')
    clt_appl = (char *) "-";

  if (error_flag == 1)
    sprintf (err_str, "ERR");
  else
    sprintf (err_str, "-");

#ifdef V3_TEST
  fprintf (fp,
	   "%d %s %s %s %d.%03d %d.%03d %02d/%02d/%02d %02d:%02d:%02d ~ "
	   "%02d/%02d/%02d %02d:%02d:%02d %d %s %d %d\n",
	   as_index + 1, clt_ip, clt_appl, script,
	   (int) start_time->tv_sec, (int) (start_time->tv_usec / 1000),
	   (int) end_time.tv_sec, (int) (end_time.tv_usec / 1000),
	   ct1.tm_year, ct1.tm_mon + 1, ct1.tm_mday, ct1.tm_hour, ct1.tm_min,
	   ct1.tm_sec, ct2.tm_year, ct2.tm_mon + 1, ct2.tm_mday, ct2.tm_hour,
	   ct2.tm_min, ct2.tm_sec,
	   (int) getpid (), err_str, error_file_offset, uts_size ());
#else
  fprintf (fp,
	   "%d %s %s %s %d.%03d %d.%03d %02d/%02d/%02d %02d:%02d:%02d ~ "
	   "%02d/%02d/%02d %02d:%02d:%02d %d %s %d\n",
	   as_index + 1, clt_ip, clt_appl, script,
	   (int) start_time->tv_sec, (int) (start_time->tv_usec / 1000),
	   (int) end_time.tv_sec, (int) (end_time.tv_usec / 1000),
	   ct1.tm_year, ct1.tm_mon + 1, ct1.tm_mday, ct1.tm_hour, ct1.tm_min,
	   ct1.tm_sec, ct2.tm_year, ct2.tm_mon + 1, ct2.tm_mday, ct2.tm_hour,
	   ct2.tm_min, ct2.tm_sec, (int) getpid (), err_str, -1);
#endif

  fclose (fp);
  return (end_time.tv_sec - start_time->tv_sec);
}
#endif /* ENABLE_UNUSED_FUNCTION */

#if defined (ENABLE_UNUSED_FUNCTION)
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
	    fprintf (fp, "[%d] file lock error. err = [%d], [%s]\n",
		     (int) getpid (), errno, strerror (errno));
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
#endif /* ENABLE_UNUSED_FUNCTION */

#if defined(WINDOWS)
int
ut_kill_process (int pid, char *br_name, int proxy_index, int shard_index,
		 int as_index)
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
ut_kill_process (int pid, char *br_name, int proxy_index, int shard_index,
		 int as_index)
{
  int i;

  if (pid > 0)
    {
#if defined(CUBRID_SHARD)
      for (i = 0; i < 10; i++)
#else /* CUBRID_SHARD */
      for (i = 0; i < 100; i++)
#endif /* !CUBRID_SHARD */
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
#if defined(CUBRID_SHARD)
      if (i >= 10)
#else /* CUBRID_SHARD */
      if (i >= 100)
#endif /* !CUBRID_SHARD */
	{
	  kill (pid, SIGKILL);
	}
    }

clear_sock_file:
#if defined(CUBRID_SHARD)
  if (br_name != NULL && (proxy_index < 0 && shard_index < 0 && as_index < 0))
    {
      char tmp[PATH_MAX], dirname[PATH_MAX];

      get_cubrid_file (FID_SOCK_DIR, dirname, PATH_MAX);
      snprintf (tmp, PATH_MAX - 1, "%s%s.B", dirname, br_name);
      unlink (tmp);
    }

  if (br_name != NULL && (proxy_index >= 0 && as_index < 0))
    {
      char tmp[PATH_MAX], dirname[PATH_MAX];

      get_cubrid_file (FID_SOCK_DIR, dirname, PATH_MAX);
      snprintf (tmp, PATH_MAX - 1, "%s%s.P%d", dirname, br_name,
		proxy_index + 1);
      unlink (tmp);
    }
#endif

  if (br_name != NULL && as_index >= 0)
    {
      char tmp[PATH_MAX], dirname[PATH_MAX];

#if !defined(CUBRID_SHARD)
      /* Broker don't use CAS unix socket at CUBRID_SHARD */
      get_cubrid_file (FID_SOCK_DIR, dirname, PATH_MAX);
      snprintf (tmp, PATH_MAX - 1, "%s%s.%d", dirname, br_name, as_index + 1);
      unlink (tmp);
#endif /* !CUBRID_SHARD */

      get_cubrid_file (FID_AS_PID_DIR, dirname, PATH_MAX);
#if defined(CUBRID_SHARD)
      snprintf (tmp, PATH_MAX - 1, "%s%s_%d_%d_%d.pid", dirname, br_name,
		proxy_index + 1, shard_index, as_index + 1);
#else
      snprintf (tmp, PATH_MAX - 1, "%s%s_%d.pid", dirname, br_name,
		as_index + 1);
#endif /* CUBRID_SHARD */
      unlink (tmp);
    }

  return 0;
}
#endif

int
ut_set_keepalive (int sock, int keepalivetime)
{
  int optval, optlen;
#if defined(WINDOWS)
  /* WinSock structure for KeepAlive timing settings */
  struct tcp_keepalive settings;
  DWORD bytesReturned;
  WSAOVERLAPPED overlapped;
#endif

  optlen = sizeof (optval);
  optval = 1;			/* true for SO_KEEPALIVE */
  setsockopt (sock, SOL_SOCKET, SO_KEEPALIVE, &optval, optlen);

#if defined(WINDOWS)
  overlapped.hEvent = NULL;
  settings.onoff = 1;
  settings.keepalivetime = keepalivetime * 1000;
  optlen = sizeof (struct tcp_keepalive);
  WSAIoctl (sock, SIO_KEEPALIVE_VALS, &settings, optlen, NULL, 0,
	    &bytesReturned, &overlapped, NULL);
#else
  optval = keepalivetime;
  setsockopt (sock, SOL_TCP, TCP_KEEPIDLE, &optval, optlen);
#endif

  return 0;
}

#if defined(WINDOWS)
int
run_child (const char *appl_name)
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
ut_cd_work_dir (void)
{
  char path[PATH_MAX];

  chdir (envvar_bindir_file (path, PATH_MAX, ""));
}

void
ut_cd_root_dir (void)
{
  chdir (envvar_root ());
}

#ifdef V3_TEST
int
uts_size (void)
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
as_pid_file_create (char *br_name, int proxy_index, int shard_id,
		    int as_index)
{
  FILE *fp;
  char buf[PATH_MAX];

#if defined(CUBRID_SHARD)
  sprintf (as_pid_file_name, "%s%s_%d_%d_%d.pid",
	   get_cubrid_file (FID_AS_PID_DIR, buf, PATH_MAX), br_name,
	   proxy_index + 1, shard_id, as_index + 1);
#else
  sprintf (as_pid_file_name, "%s%s_%d.pid",
	   get_cubrid_file (FID_AS_PID_DIR, buf, PATH_MAX), br_name,
	   as_index + 1);
#endif
  fp = fopen (as_pid_file_name, "w");
  if (fp)
    {
      fprintf (fp, "%d\n", (int) getpid ());
      fclose (fp);
    }
}

void
as_db_err_log_set (char *br_name, int proxy_index, int shard_id, int as_index)
{
  char buf[PATH_MAX];

#if defined(CUBRID_SHARD)
  sprintf (db_err_log_file, "CUBRID_ERROR_LOG=%s%s_%d_%d_%d.err",
	   get_cubrid_file (FID_CUBRID_ERR_DIR, buf, PATH_MAX), br_name,
	   proxy_index + 1, shard_id, as_index + 1);
#else
  sprintf (db_err_log_file, "CUBRID_ERROR_LOG=%s%s_%d.err",
	   get_cubrid_file (FID_CUBRID_ERR_DIR, buf, PATH_MAX), br_name,
	   as_index + 1);
#endif
  putenv (db_err_log_file);
}

int
as_get_my_as_info (char *br_name, int *as_index, int max_length)
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
  strncpy (br_name, dir_p + 1, max_length);
  *as_index = atoi (q + 1) - 1;
  *q = '.';

  return 0;
}

int
ut_time_string (char *buf, struct timeval *time_val)
{
  struct tm tm, *tm_p;
  time_t sec;
  int millisec;

  if (buf == NULL)
    {
      return 0;
    }

  if (time_val == NULL)
    {
      struct timeb tb;

      /* current time */
      (void) ftime (&tb);
      sec = tb.time;
      millisec = tb.millitm;
    }
  else
    {
      sec = time_val->tv_sec;
      millisec = time_val->tv_usec / 1000;
    }

  tm_p = localtime_r (&sec, &tm);
  tm.tm_mon++;

  buf[0] = (tm.tm_mon / 10) + '0';
  buf[1] = (tm.tm_mon % 10) + '0';
  buf[2] = '/';
  buf[3] = (tm.tm_mday / 10) + '0';
  buf[4] = (tm.tm_mday % 10) + '0';
  buf[5] = ' ';
  buf[6] = (tm.tm_hour / 10) + '0';
  buf[7] = (tm.tm_hour % 10) + '0';
  buf[8] = ':';
  buf[9] = (tm.tm_min / 10) + '0';
  buf[10] = (tm.tm_min % 10) + '0';
  buf[11] = ':';
  buf[12] = (tm.tm_sec / 10) + '0';
  buf[13] = (tm.tm_sec % 10) + '0';
  buf[14] = '.';
  buf[17] = (millisec % 10) + '0';
  millisec /= 10;
  buf[16] = (millisec % 10) + '0';
  millisec /= 10;
  buf[15] = (millisec % 10) + '0';
  buf[18] = '\0';

  return 18;
}

char *
ut_get_ipv4_string (char *ip_str, int len, const unsigned char *ip_addr)
{
  assert (ip_addr != NULL);
  assert (ip_str != NULL);
  assert (len >= 16);		/* xxx.xxx.xxx.xxx\0 */

  snprintf (ip_str, len, "%d.%d.%d.%d", (unsigned char) ip_addr[0],
	    (unsigned char) ip_addr[1],
	    (unsigned char) ip_addr[2], (unsigned char) ip_addr[3]);
  return (ip_str);
}

float
ut_get_avg_from_array (int array[], int size)
{
  int i, total = 0;
  for (i = 0; i < size; i++)
    {
      total += array[i];
    }

  return (float) total / size;
}

bool
ut_is_appl_server_ready (int pid, char *ready_flag)
{
  unsigned int i;

  for (i = 0; i < SERVICE_READY_WAIT_COUNT; i++)
    {
      if (*ready_flag == true)
	{
	  return true;
	}
      else
	{
#if defined(WINDOWS)
	  HANDLE h_process;

	  h_process = OpenProcess (PROCESS_QUERY_INFORMATION, FALSE, pid);
	  if (h_process != NULL)
	    {
	      CloseHandle (h_process);
	      SLEEP_MILISEC (0, 10);
	      continue;
	    }
	  else
	    {
	      return false;
	    }
#else /* WINDOWS */
	  if (kill (pid, 0) == 0)
	    {
	      SLEEP_MILISEC (0, 10);
	      continue;
	    }
	  else
	    {
	      return false;
	    }
#endif /* WINDOWS */
	}
    }

  return false;
}
