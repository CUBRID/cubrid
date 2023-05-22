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
 * cm_utils.c -
 */

#include "cm_utils.h"
#include "cm_portable.h"
#include "cm_defines.h"
#include "utility.h"
#include "environment_variable.h"

#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>

#if defined(WINDOWS)
#include <process.h>
#include <io.h>
#include <tlhelp32.h>
#else
#include "system_parameter.h"
#include <unistd.h>
#include <sys/wait.h>
#include <stdarg.h>
#include <sys/time.h>
#endif

static T_CMD_RESULT *new_cmd_result (void);
static void close_all_fds (int init_fd);

#if defined(WINDOWS)
static int is_master_start ();
static int gettimeofday (struct timeval *tp, void *tzp);
#endif

#define CUBRID_SERVER_LOCK_EXT     "_lgat__lock"

#if defined(WINDOWS)
int
kill (int pid, int signo)
{
  HANDLE phandle;

  phandle = OpenProcess (PROCESS_TERMINATE, FALSE, pid);
  if (phandle == NULL)
    {
      int error = GetLastError ();
      if (error == ERROR_ACCESS_DENIED)
	errno = EPERM;
      else
	errno = ESRCH;
      return -1;
    }

  if (signo == SIGTERM)
    TerminateProcess (phandle, 0);

  CloseHandle (phandle);
  return 0;
}
#endif

#if defined(WINDOWS)
int
run_child (const char *const argv[], int wait_flag, const char *stdin_file, char *stdout_file, char *stderr_file,
	   int *exit_status)
{
  int new_pid;
  STARTUPINFO start_info;
  PROCESS_INFORMATION proc_info;
  BOOL res;
  int i, cmd_arg_len;
  char cmd_arg[1024];
  BOOL inherit_flag = FALSE;
  HANDLE hStdIn = INVALID_HANDLE_VALUE;
  HANDLE hStdOut = INVALID_HANDLE_VALUE;
  HANDLE hStdErr = INVALID_HANDLE_VALUE;

  if (exit_status != NULL)
    *exit_status = 0;

  for (i = 0, cmd_arg_len = 0; argv[i]; i++)
    {
      cmd_arg_len += sprintf (cmd_arg + cmd_arg_len, "\"%s\" ", argv[i]);
    }

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
    CreateProcess (argv[0], cmd_arg, NULL, NULL, inherit_flag, CREATE_NO_WINDOW, NULL, NULL, &start_info, &proc_info);

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
int
run_child (const char *const argv[], int wait_flag, const char *stdin_file, char *stdout_file, char *stderr_file,
	   int *exit_status)
{
  int pid;

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

      close_all_fds (3);

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

      execv ((const char *) argv[0], (char *const *) argv);
      exit (0);
    }

  if (pid < 0)
    return -1;

  if (wait_flag)
    {
      int status = 0;
      waitpid (pid, &status, 0);
      if (exit_status != NULL)
	*exit_status = status;
      return 0;
    }
  else
    {
      return pid;
    }
}
#endif


static void
close_all_fds (int init_fd)
{
  int i;

  for (i = init_fd; i < 1024; i++)
    {
      close (i);
    }
}

char *
time_to_str (time_t t, const char *fmt, char *buf, int type)
{
  struct tm ltm;
  struct tm *tm_p;

  tm_p = localtime (&t);
  if (tm_p == NULL)
    {
      *buf = '\0';
      return buf;
    }
  ltm = *tm_p;

  if (type == TIME_STR_FMT_DATE)
    sprintf (buf, fmt, ltm.tm_year + 1900, ltm.tm_mon + 1, ltm.tm_mday);
  else if (type == TIME_STR_FMT_TIME)
    sprintf (buf, fmt, ltm.tm_hour, ltm.tm_min, ltm.tm_sec);
  else				/* TIME_STR_FMT_DATE_TIME */
    sprintf (buf, fmt, ltm.tm_year + 1900, ltm.tm_mon + 1, ltm.tm_mday, ltm.tm_hour, ltm.tm_min, ltm.tm_sec);
  return buf;
}

T_DB_SERVICE_MODE
uDatabaseMode (char *dbname, int *ha_mode)
{
  int pid, retval;

  retval = uIsDatabaseActive (dbname);
  if (retval != 0)
    {
      if (ha_mode != NULL)
	{
	  *ha_mode = (retval == 2) ? 1 : 0;
	}
      return DB_SERVICE_MODE_CS;
    }

  pid = get_db_server_pid (dbname);
  if (pid > 0)
    {
      if (kill (pid, 0) >= 0)
	{
	  return DB_SERVICE_MODE_SA;
	}
    }
  return DB_SERVICE_MODE_NONE;
}

int
get_db_server_pid (char *dbname)
{
  char srv_lock_file[1024];
  FILE *fp;
  int pid = -1;
  size_t file_len;

  if (uRetrieveDBLogDirectory (dbname, srv_lock_file) != ERR_NO_ERROR)
    {
      return -1;
    }
  file_len = strlen (srv_lock_file);
  snprintf (srv_lock_file + file_len, sizeof (srv_lock_file) - file_len - 1, "/%s%s", dbname, CUBRID_SERVER_LOCK_EXT);
  if ((fp = fopen (srv_lock_file, "r")) != NULL)
    {
      if (fscanf (fp, "%*s %d", &pid) < 1)
	pid = -1;
      fclose (fp);
    }
  return pid;
}

int
uStringEqual (const char *str1, const char *str2)
{
  if (str1 == NULL || str2 == NULL)
    return 0;
  if (strcmp (str1, str2) == 0)
    return 1;
  return 0;
}

int
uIsDatabaseActive (char *dbn)
{
  T_SERVER_STATUS_RESULT *cmd_res;
  int retval = 0;

  if (dbn == NULL)
    return 0;
  cmd_res = cmd_server_status ();
  if (cmd_res != NULL)
    {
      retval = uIsDatabaseActive2 (cmd_res, dbn);
      cmd_servstat_result_free (cmd_res);
    }
  return retval;
}

int
uIsDatabaseActive2 (T_SERVER_STATUS_RESULT * cmd_res, char *dbn)
{
  T_SERVER_STATUS_INFO *info;
  int i;

  if (cmd_res == NULL)
    return 0;

  info = (T_SERVER_STATUS_INFO *) cmd_res->result;
  for (i = 0; i < cmd_res->num_result; i++)
    {
      if (strcmp (info[i].db_name, dbn) == 0)
	{
	  return (info[i].ha_mode != 0) ? 2 : 1;
	}
    }
  return 0;
}

#define new_servstat_result()       (T_SERVER_STATUS_RESULT*) new_cmd_result()
static void read_server_status_output (T_SERVER_STATUS_RESULT * res, char *out_file);

T_SERVER_STATUS_RESULT *
cmd_server_status (void)
{
  T_SERVER_STATUS_RESULT *res;
  char out_file[PATH_MAX];
  char cmd_name[PATH_MAX];
  const char *argv[5];
  char tmpfile[PATH_MAX];

  res = new_servstat_result ();
  if (res == NULL)
    {
      return NULL;
    }

#ifdef	WINDOWS
  if (is_master_start () != 0)
    {
      return res;
    }
#endif

  if (make_temp_filename (tmpfile, "DBMT_util_001.", sizeof (tmpfile)) < 0)
    {
      cmd_result_free (res);
      return NULL;
    }
  (void) envvar_tmpdir_file (out_file, PATH_MAX, tmpfile);
  (void) envvar_bindir_file (cmd_name, PATH_MAX, UTIL_CUBRID);

  argv[0] = cmd_name;
  argv[1] = PRINT_CMD_SERVER;
  argv[2] = PRINT_CMD_STATUS;
  argv[3] = NULL;

  run_child (argv, 1, NULL, out_file, NULL, NULL);	/* cubrid server status */

  read_server_status_output (res, out_file);

  unlink (out_file);
  return res;
}

#if defined(WINDOWS)
static int
is_master_start ()
{
  HANDLE h_proc_snap;
  PROCESSENTRY32 pe32;
  int retval = -1;

  /* Take a snapshot of all processes in the system. */
  h_proc_snap = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
  if (h_proc_snap == INVALID_HANDLE_VALUE)
    {
      return -1;
    }

  /* Set the size of the structure before using it. */
  pe32.dwSize = sizeof (PROCESSENTRY32);

  /* Retrieve information about the first process, and return -1 if unsuccessful. */
  if (Process32First (h_proc_snap, &pe32) == 0)
    {
      retval = -1;
      goto func_clean_return;
    }

  /* Now walk the snapshot of processes, and find cub_master.exe. */
  do
    {
      if (strcasecmp (pe32.szExeFile, UTIL_MASTER_NAME) == 0)
	{
	  retval = 0;
	  break;
	}

    }
  while (Process32Next (h_proc_snap, &pe32));

func_clean_return:
  CloseHandle (h_proc_snap);
  return retval;
}
#endif


static void
read_server_status_output (T_SERVER_STATUS_RESULT * res, char *out_file)
{
  T_SERVER_STATUS_INFO *info;
  int num_info, num_alloc;
  char str_buf[512];
  char tmp_str[64], db_name[64];
  FILE *fp;

  fp = fopen (out_file, "r");
  if (fp == NULL)
    {
      return;
    }

  num_info = 0;
  num_alloc = 5;
  info = (T_SERVER_STATUS_INFO *) malloc (sizeof (T_SERVER_STATUS_INFO) * num_alloc);
  if (info == NULL)
    {
      fclose (fp);
      return;
    }

  while (fgets (str_buf, sizeof (str_buf), fp))
    {
      char *tmp_p;

      if (sscanf (str_buf, "%63s %63s", tmp_str, db_name) < 2)
	continue;
      if (strcmp (tmp_str, "@") == 0 || (strcmp (tmp_str, "Server") != 0 && strcmp (tmp_str, "HA-Server") != 0))
	continue;

      tmp_p = strchr (db_name, ',');
      if (tmp_p != NULL)
	*tmp_p = '\0';

      num_info++;
      if (num_info > num_alloc)
	{
	  num_alloc += 5;
	  T_SERVER_STATUS_INFO *const new_info
	    = (T_SERVER_STATUS_INFO *) realloc (info, sizeof (T_SERVER_STATUS_INFO) * num_alloc);
	  if (new_info == NULL)
	    {
	      fclose (fp);
	      return;
	    }
	  else
	    {
	      info = new_info;
	    }
	}
      strcpy (info[num_info - 1].db_name, db_name);
      info[num_info - 1].ha_mode = (strcmp (tmp_str, "HA-Server") == 0) ? 1 : 0;
    }
  fclose (fp);

  res->num_result = num_info;
  res->result = info;
}


char *
ut_trim (char *str)
{
  char *p;
  char *s;

  if (str == NULL)
    return (str);

  for (s = str; *s != '\0' && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r'); s++)
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
    memmove (str, s, strlen (s) + 1);

  return (str);
}

int
string_tokenize (char *str, char *tok[], int num_tok)
{
  int i;
  char *p;

  tok[0] = str;
  for (i = 1; i < num_tok; i++)
    {
      tok[i] = strpbrk (tok[i - 1], " \t");
      if (tok[i] == NULL)
	return -1;
      *(tok[i]) = '\0';
      p = (tok[i]) + 1;
      for (; *p && (*p == ' ' || *p == '\t'); p++)
	;
      if (*p == '\0')
	return -1;
      tok[i] = p;
    }
  p = strpbrk (tok[num_tok - 1], " \t");
  if (p != NULL)
    *p = '\0';

  return 0;
}

static T_CMD_RESULT *
new_cmd_result (void)
{
  T_CMD_RESULT *res;

  res = (T_CMD_RESULT *) malloc (sizeof (T_CMD_RESULT));
  if (res == NULL)
    return NULL;
  memset (res, 0, sizeof (T_CMD_RESULT));
  return res;
}

void
cmd_result_free (T_CMD_RESULT * res)
{
  if (res != NULL)
    {
      if (res->result != NULL)
	free (res->result);
      free (res);
    }
}

int
uRetrieveDBLogDirectory (char *dbname, char *target)
{
  int ret_val;
#ifdef	WINDOWS
  char temp_name[512];
#endif

  ret_val = uReadDBtxtFile (dbname, 3, target);
#ifdef	WINDOWS
  if (ret_val == ERR_NO_ERROR)
    {
      strcpy (temp_name, target);
      memset (target, '\0', strlen (target));
      if (GetLongPathName (temp_name, target, MAX_PATH) == 0)
	{
	  strcpy (target, temp_name);
	}
    }
#endif
  return ret_val;
}

#if defined(WINDOWS)
void
unix_style_path (char *path)
{
  char *p;
  for (p = path; *p; p++)
    {
      if (*p == '\\')
	*p = '/';
    }
}
#endif

int
uReadDBtxtFile (const char *dn, int idx, char *outbuf)
{
  char strbuf[PATH_MAX];
  FILE *dbf;
  char *value_p[4];
  int retval = ERR_DBDIRNAME_NULL;
  char *cubrid_database_path;
#if !defined (DO_NOT_USE_CUBRIDENV)
  cubrid_database_path = getenv (CUBRID_DATABASES_ENV);
#else
  cubrid_database_path = CUBRID_VARDIR;
#endif

  if (outbuf != NULL)
    outbuf[0] = '\0';

  if (cubrid_database_path == NULL)
    {
      return ERR_GENERAL_ERROR;
    }
  snprintf (strbuf, PATH_MAX, "%s/%s", cubrid_database_path, CUBRID_DATABASE_TXT);
  dbf = fopen (strbuf, "r");
  if (dbf == NULL)
    return ERR_DBDIRNAME_NULL;

  while (fgets (strbuf, sizeof (strbuf), dbf))
    {
      ut_trim (strbuf);
      if (string_tokenize (strbuf, value_p, 4) < 0)
	continue;
      if (strcmp (value_p[0], dn) == 0)
	{
	  if (outbuf != NULL)
	    {
	      strcpy (outbuf, value_p[idx]);
#if defined(WINDOWS)
	      unix_style_path (outbuf);
#endif
	    }
	  retval = ERR_NO_ERROR;
	  break;
	}
    }
  fclose (dbf);

  return retval;
}

int
make_version_info (char *cli_ver, int *major_ver, int *minor_ver)
{
  char *p;
  if (cli_ver == NULL)
    return 0;

  p = cli_ver;
  *major_ver = atoi (p);
  p = strchr (p, '.');
  if (p != NULL)
    *minor_ver = atoi (p + 1);
  else
    *minor_ver = 0;

  return 1;
}

int
cm_util_log_write_result (int error)
{
  return util_log_write_result (error);
}

int
cm_util_log_write_errid (int message_id, ...)
{
  int ret = 0;
  va_list arg_list;

  va_start (arg_list, message_id);
  ret = util_log_write_errid (message_id, arg_list);
  va_end (arg_list);

  return ret;
}

int
cm_util_log_write_errstr (const char *format, ...)
{
  int ret = 0;
  va_list arg_list;

  va_start (arg_list, format);
  ret = util_log_write_errstr (format, arg_list);
  va_end (arg_list);

  return ret;
}

int
cm_util_log_write_command (int argc, char *argv[])
{
  return util_log_write_command (argc, argv);
}

/*
 * Generate unique temp file name.
 * {prefix}{second}_{usec}_{random number: 1..997}
 */

int
make_temp_filename (char *tempfile, char *prefix, int size)
{
  struct timeval current_time;

  if (tempfile == NULL || prefix == NULL || size < 1)
    {
      return -1;
    }

  srand (time (NULL));
  if (gettimeofday (&current_time, NULL) < 0)
    {
      return -1;
    }

  snprintf (tempfile, size - 1, "%s%ld_%ld_%d", prefix, current_time.tv_sec, current_time.tv_usec, rand () % 997);

  return 0;
}

int
make_temp_filepath (char *tempfile, char *tempdir, char *prefix, int task_code, int size)
{
  struct timeval current_time;

  if (tempfile == NULL || tempdir == NULL || size < 1)
    {
      return -1;
    }

  srand (time (NULL));
  if (gettimeofday (&current_time, NULL) < 0)
    {
      return -1;
    }

  snprintf (tempfile, size - 1, "%s/%s_%03d_%ld_%d_%d", tempdir, prefix ? prefix : "", task_code,
	    current_time.tv_sec, current_time.tv_usec, rand () % 997);

  return 0;
}

#if defined (WINDOWS)
/* Number of 100 nanosecond units from 1/1/1601 to 1/1/1970 */
#define EPOCH_BIAS_IN_100NANOSECS 116444736000000000LL

/*
 * gettimeofday - Windows port of Unix gettimeofday(), from base/porting.c
 *   return: none
 *   tp(out): where time is stored
 *   tzp(in): unused
 */
static int
gettimeofday (struct timeval *tp, void *tzp)
{
/*
 * Rapid calculation divisor for 10,000,000
 * x/10000000 == x/128/78125 == (x>>7)/78125
 */
#define RAPID_CALC_DIVISOR 78125

  union
  {
    unsigned __int64 nsec100;	/* in 100 nanosecond units */
    FILETIME ft;
  } now;

  GetSystemTimeAsFileTime (&now.ft);

  /*
   * Optimization for sec = (long) (x / 10000000);
   * where "x" is number of 100 nanoseconds since 1/1/1970.
   */
  tp->tv_sec = (long) (((now.nsec100 - EPOCH_BIAS_IN_100NANOSECS) >> 7) / RAPID_CALC_DIVISOR);

  /*
   * Optimization for usec = (long) (x % 10000000) / 10;
   * Let c = x / b,
   * An alternative for MOD operation (x % b) is: (x - c * b),
   *   which consumes less time, specially, for a 64 bit "x".
   */
  tp->tv_usec =
    ((long) (now.nsec100 - EPOCH_BIAS_IN_100NANOSECS - (((unsigned __int64) (tp->tv_sec * RAPID_CALC_DIVISOR)) << 7))) /
    10;

  return 0;
}
#endif
