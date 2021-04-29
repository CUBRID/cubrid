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
#include <ctype.h>

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
#include "porting.h"
#include "util_func.h"

char db_err_log_file[BROKER_PATH_MAX];

#if defined (ENABLE_UNUSED_FUNCTION)
int
ut_access_log (int as_index, struct timeval *start_time, char error_flag, int error_log_offset)
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
	   "%d %s %s %s %d.%03d %d.%03d %02d/%02d/%02d %02d:%02d:%02d ~ " "%02d/%02d/%02d %02d:%02d:%02d %d %s %d %d\n",
	   as_index + 1, clt_ip, clt_appl, script, (int) start_time->tv_sec, (int) (start_time->tv_usec / 1000),
	   (int) end_time.tv_sec, (int) (end_time.tv_usec / 1000), ct1.tm_year, ct1.tm_mon + 1, ct1.tm_mday,
	   ct1.tm_hour, ct1.tm_min, ct1.tm_sec, ct2.tm_year, ct2.tm_mon + 1, ct2.tm_mday, ct2.tm_hour, ct2.tm_min,
	   ct2.tm_sec, (int) getpid (), err_str, error_file_offset, uts_size ());
#else
  fprintf (fp,
	   "%d %s %s %s %d.%03d %d.%03d %02d/%02d/%02d %02d:%02d:%02d ~ " "%02d/%02d/%02d %02d:%02d:%02d %d %s %d\n",
	   as_index + 1, clt_ip, clt_appl, script, (int) start_time->tv_sec, (int) (start_time->tv_usec / 1000),
	   (int) end_time.tv_sec, (int) (end_time.tv_usec / 1000), ct1.tm_year, ct1.tm_mon + 1, ct1.tm_mday,
	   ct1.tm_hour, ct1.tm_min, ct1.tm_sec, ct2.tm_year, ct2.tm_mon + 1, ct2.tm_mday, ct2.tm_hour, ct2.tm_min,
	   ct2.tm_sec, (int) getpid (), err_str, -1);
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
	    fprintf (fp, "[%d] file lock error. err = [%d], [%s]\n", (int) getpid (), errno, strerror (errno));
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
ut_kill_process (int pid)
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
ut_kill_process (int pid)
{
  int i;

  if (pid > 0)
    {
      for (i = 0; i < 10; i++)
	{
	  if (kill (pid, SIGTERM) < 0)
	    {
	      return 0;
	    }
	  SLEEP_MILISEC (0, 30);
	  if (kill (pid, 0) < 0)
	    {
	      break;
	    }
	}
      if (i >= 10)
	{
	  kill (pid, SIGKILL);
	}
    }

  return 0;
}
#endif

int
ut_kill_broker_process (int pid, char *broker_name)
{
  ut_kill_process (pid);

  if (broker_name != NULL)
    {
      char tmp[BROKER_PATH_MAX];

      ut_get_broker_port_name (tmp, broker_name, BROKER_PATH_MAX);

      unlink (tmp);

      return 0;
    }
  return -1;
}

int
ut_kill_proxy_process (int pid, char *broker_name, int proxy_id)
{
  ut_kill_process (pid);

  if (broker_name != NULL && proxy_id >= 0)
    {
      char tmp[BROKER_PATH_MAX];

      ut_get_proxy_port_name (tmp, broker_name, proxy_id, BROKER_PATH_MAX);

      unlink (tmp);

      return 0;
    }
  return -1;
}

int
ut_kill_as_process (int pid, char *broker_name, int as_index, int shard_flag)
{
  ut_kill_process (pid);

  if (broker_name != NULL)
    {
      char tmp[BROKER_PATH_MAX];

      /*
       * shard_cas does not have unix-domain socket and pid lock file.
       * so, we need not delete socket and lock file.
       */
      if (shard_flag == OFF)
	{
	  ut_get_as_port_name (tmp, broker_name, as_index, BROKER_PATH_MAX);

	  unlink (tmp);
	}

      ut_get_as_pid_name (tmp, broker_name, as_index, BROKER_PATH_MAX);

      unlink (tmp);

      return 0;
    }
  return -1;
}

int
ut_set_keepalive (int sock)
{
  int optval, optlen;

  optlen = sizeof (optval);
  optval = 1;			/* true for SO_KEEPALIVE */
  setsockopt (sock, SOL_SOCKET, SO_KEEPALIVE, (const char *) &optval, optlen);

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

  res = CreateProcess (cmd, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &start_info, &proc_info);

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
  char path[BROKER_PATH_MAX];

  chdir (envvar_bindir_file (path, BROKER_PATH_MAX, ""));
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
as_pid_file_create (char *br_name, int as_index)
{
  FILE *fp;
  char as_pid_file_name[BROKER_PATH_MAX];

  ut_get_as_pid_name (as_pid_file_name, br_name, as_index, BROKER_PATH_MAX);

  fp = fopen (as_pid_file_name, "w");
  if (fp)
    {
      fprintf (fp, "%d\n", (int) getpid ());
      fclose (fp);
    }
}

void
as_db_err_log_set (char *br_name, int proxy_index, int shard_id, int shard_cas_id, int as_index, int shard_flag)
{
  char buf[BROKER_PATH_MAX];

  if (shard_flag == ON)
    {
      sprintf (db_err_log_file, "CUBRID_ERROR_LOG=%s%s_%d_%d_%d.err",
	       get_cubrid_file (FID_CUBRID_ERR_DIR, buf, BROKER_PATH_MAX), br_name, proxy_index + 1, shard_id,
	       shard_cas_id + 1);
    }
  else
    {
      sprintf (db_err_log_file, "CUBRID_ERROR_LOG=%s%s_%d.err",
	       get_cubrid_file (FID_CUBRID_ERR_DIR, buf, BROKER_PATH_MAX), br_name, as_index + 1);
    }

  putenv (db_err_log_file);
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
      util_get_second_and_ms_since_epoch (&sec, &millisec);
    }
  else
    {
      sec = time_val->tv_sec;
      millisec = time_val->tv_usec / 1000;
    }

  tm_p = localtime_r (&sec, &tm);
  tm.tm_mon++;

  buf[0] = ((tm.tm_year % 100) / 10) + '0';
  buf[1] = (tm.tm_year % 10) + '0';
  buf[2] = '-';
  buf[3] = (tm.tm_mon / 10) + '0';
  buf[4] = (tm.tm_mon % 10) + '0';
  buf[5] = '-';
  buf[6] = (tm.tm_mday / 10) + '0';
  buf[7] = (tm.tm_mday % 10) + '0';
  buf[8] = ' ';
  buf[9] = (tm.tm_hour / 10) + '0';
  buf[10] = (tm.tm_hour % 10) + '0';
  buf[11] = ':';
  buf[12] = (tm.tm_min / 10) + '0';
  buf[13] = (tm.tm_min % 10) + '0';
  buf[14] = ':';
  buf[15] = (tm.tm_sec / 10) + '0';
  buf[16] = (tm.tm_sec % 10) + '0';
  buf[17] = '.';
  buf[20] = (millisec % 10) + '0';
  millisec /= 10;
  buf[19] = (millisec % 10) + '0';
  millisec /= 10;
  buf[18] = (millisec % 10) + '0';
  buf[21] = '\0';

  return 21;
}

char *
ut_get_ipv4_string (char *ip_str, int len, const unsigned char *ip_addr)
{
  assert (ip_addr != NULL);
  assert (ip_str != NULL);
  assert (len >= 16);		/* xxx.xxx.xxx.xxx\0 */

  snprintf (ip_str, len, "%d.%d.%d.%d", (unsigned char) ip_addr[0], (unsigned char) ip_addr[1],
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
      if (*ready_flag != 0)
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

void
ut_get_broker_port_name (char *port_name, char *broker_name, int len)
{
  char dir_name[BROKER_PATH_MAX];

  get_cubrid_file (FID_SOCK_DIR, dir_name, BROKER_PATH_MAX);

  if (snprintf (port_name, len, "%s%s.B", dir_name, broker_name) < 0)
    {
      assert (false);
      port_name[0] = '\0';
    }
}

void
ut_get_proxy_port_name (char *port_name, char *broker_name, int proxy_id, int len)
{
  char dir_name[BROKER_PATH_MAX];

  get_cubrid_file (FID_SOCK_DIR, dir_name, BROKER_PATH_MAX);

  if (snprintf (port_name, len, "%s%s.P%d", dir_name, broker_name, proxy_id + 1) < 0)
    {
      assert (false);
      port_name[0] = '\0';
    }
}

void
ut_get_as_port_name (char *port_name, char *broker_name, int as_id, int len)
{
  char dir_name[BROKER_PATH_MAX];

  get_cubrid_file (FID_SOCK_DIR, dir_name, BROKER_PATH_MAX);

  if (snprintf (port_name, len, "%s%s.%d", dir_name, broker_name, as_id + 1) < 0)
    {
      assert (false);
      port_name[0] = '\0';
    }
}

double
ut_size_string_to_kbyte (const char *size_str, const char *default_unit)
{
  double val;
  char *end;
  char *unit;

  if (size_str == NULL || default_unit == NULL)
    {
      assert (false);
    }

  val = strtod (size_str, &end);
  if (end == (char *) size_str)
    {
      return -1.0;
    }

  if (isalpha (*end))
    {
      unit = end;
    }
  else
    {
      unit = (char *) default_unit;
    }

  if (strcasecmp (unit, "b") == 0)
    {
      /* byte */
      val = val / ONE_K;
    }
  else if ((strcasecmp (unit, "k") == 0) || (strcasecmp (unit, "kb") == 0))
    {
      /* kilo */
    }
  else if ((strcasecmp (unit, "m") == 0) || (strcasecmp (unit, "mb") == 0))
    {
      /* mega */
      val = val * ONE_K;
    }
  else if ((strcasecmp (unit, "g") == 0) || (strcasecmp (unit, "gb") == 0))
    {
      /* giga */
      val = val * ONE_M;
    }
  else
    {
      return -1.0;
    }

  if (val > INT_MAX)		/* spec */
    {
      return -1.0;
    }

  return val;
}

double
ut_time_string_to_sec (const char *time_str, const char *default_unit)
{
  double val;
  char *end;
  char *unit;

  if (time_str == NULL || default_unit == NULL)
    {
      assert (false);
    }

  val = strtod (time_str, &end);
  if (end == (char *) time_str)
    {
      return -1.0;
    }

  if (isalpha (*end))
    {
      unit = end;
    }
  else
    {
      unit = (char *) default_unit;
    }

  if ((strcasecmp (unit, "ms") == 0) || (strcasecmp (unit, "msec") == 0))
    {
      /* millisecond */
      val = val / ONE_SEC;
    }
  else if ((strcasecmp (unit, "s") == 0) || (strcasecmp (unit, "sec") == 0))
    {
      /* second */
    }
  else if (strcasecmp (unit, "min") == 0)
    {
      /* minute */
      val = val * ONE_MIN / ONE_SEC;
    }
  else if (strcasecmp (unit, "h") == 0)
    {
      /* hours */
      val = val * ONE_HOUR / ONE_SEC;
    }
  else
    {
      return -1.0;
    }

  if (val > INT_MAX)		/* spec */
    {
      return -1.0;
    }

  return val;
}

void
ut_get_as_pid_name (char *pid_name, char *br_name, int as_index, int len)
{
  char dir_name[BROKER_PATH_MAX];

  get_cubrid_file (FID_AS_PID_DIR, dir_name, BROKER_PATH_MAX);

  if (snprintf (pid_name, len, "%s%s_%d.pid", dir_name, br_name, as_index + 1) < 0)
    {
      assert (false);
      pid_name[0] = '\0';
    }
}
