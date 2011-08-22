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
 * cas_log.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#if defined(WINDOWS)
#include <sys/timeb.h>
#include <process.h>
#include <io.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif
#include <assert.h>

#include "cas_common.h"
#include "cas_log.h"
#include "cas_util.h"
#include "broker_config.h"
#include "cas.h"
#include "cas_execute.h"

#include "broker_env_def.h"
#include "broker_filename.h"
#include "broker_util.h"
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
#include "dbi.h"
#include "cas_db_inc.h"
#endif


static char *make_sql_log_filename (T_CUBRID_FILE_ID fid, char *filename_buf,
				    size_t buf_size, const char *br_name,
				    int as_index);
static void cas_log_backup (T_CUBRID_FILE_ID fid);

#if defined (ENABLE_UNUSED_FUNCTION)
static void cas_log_rename (int run_time, time_t cur_time, char *br_name,
			    int as_index);
#endif
static void cas_log_write_internal (FILE * fp, unsigned int seq_num,
				    bool do_flush, const char *fmt,
				    va_list ap);
static void cas_log_write2_internal (FILE * fp, bool do_flush,
				     const char *fmt, va_list ap);

static FILE *sql_log_open (char *log_file_name);

#ifdef CAS_ERROR_LOG
static int error_file_offset;
static char cas_log_error_flag;
#endif
static FILE *log_fp = NULL, *slow_log_fp = NULL;
static char log_filepath[PATH_MAX], slow_log_filepath[PATH_MAX];
static long saved_log_fpos = 0;

static char *
make_sql_log_filename (T_CUBRID_FILE_ID fid, char *filename_buf,
		       size_t buf_size, const char *br_name, int as_index)
{
  char dirname[PATH_MAX];

  assert (filename_buf != NULL);

  get_cubrid_file (fid, dirname);
  switch (fid)
    {
    case FID_SQL_LOG_DIR:
      snprintf (filename_buf, buf_size, "%s%s_%d.sql.log", dirname, br_name,
		(as_index) + 1);
      break;
    case FID_SLOW_LOG_DIR:
      snprintf (filename_buf, buf_size, "%s%s_%d.slow.log", dirname, br_name,
		(as_index) + 1);
      break;
    default:
      assert (0);
      snprintf (filename_buf, buf_size, "unknown.log");
      break;
    }
  return filename_buf;
}

void
cas_log_open (char *br_name, int as_index)
{
#ifndef LIBCAS_FOR_JSP
  if (log_fp != NULL)
    {
      cas_log_close (true);
    }

  if (as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      if (br_name != NULL)
	{
	  make_sql_log_filename (FID_SQL_LOG_DIR, log_filepath, PATH_MAX,
				 br_name, as_index);
	}

      /* note: in "a+" mode, output is always appended */
      log_fp = fopen (log_filepath, "r+");
      if (log_fp != NULL)
	{
	  fseek (log_fp, 0, SEEK_END);
	  saved_log_fpos = ftell (log_fp);
	}
      else
	{
	  log_fp = fopen (log_filepath, "w");
	  saved_log_fpos = 0;
	}
    }
  else
    {
      log_fp = NULL;
      saved_log_fpos = 0;
    }
  as_info->cas_log_reset = 0;
#endif /* LIBCAS_FOR_JSP */
}

void
cas_log_reset (char *br_name, int as_index)
{
#ifndef LIBCAS_FOR_JSP
  if (as_info->cas_log_reset)
    {
      if (log_fp != NULL)
	{
	  cas_log_close (true);
	}
      if ((as_info->cas_log_reset & CAS_LOG_RESET_REMOVE) != 0)
	{
	  unlink (log_filepath);
	}

      cas_log_open (br_name, as_index);
    }
#endif /* LIBCAS_FOR_JSP */
}

void
cas_log_close (bool flag)
{
#ifndef LIBCAS_FOR_JSP
  if (log_fp != NULL)
    {
      if (flag)
	{
	  fseek (log_fp, saved_log_fpos, SEEK_SET);
	  ftruncate (fileno (log_fp), saved_log_fpos);
	}
      fclose (log_fp);
      log_fp = NULL;
      saved_log_fpos = 0;
    }
#endif /* LIBCAS_FOR_JSP */
}

static void
cas_log_backup (T_CUBRID_FILE_ID fid)
{
  char backup_filepath[PATH_MAX];
  char *filepath = NULL;

  assert (log_filepath[0] != '\0');

  switch (fid)
    {
    case FID_SQL_LOG_DIR:
      filepath = log_filepath;
      break;
    case FID_SLOW_LOG_DIR:
      filepath = slow_log_filepath;
      break;
    default:
      assert (0);
      return;
    }

  snprintf (backup_filepath, PATH_MAX, "%s.bak", filepath);
  unlink (backup_filepath);
  rename (filepath, backup_filepath);
}

#if defined (ENABLE_UNUSED_FUNCTION)
static void
cas_log_rename (int run_time, time_t cur_time, char *br_name, int as_index)
{
  char new_filepath[PATH_MAX];
  struct tm tmp_tm;

  assert (log_filepath[0] != '\0');

  localtime_r (&cur_time, &tmp_tm);
  tmp_tm.tm_year += 1900;

  snprintf (new_filepath, PATH_MAX, "%s.%02d%02d%02d%02d%02d.%d",
	    log_filepath, tmp_tm.tm_mon + 1, tmp_tm.tm_mday, tmp_tm.tm_hour,
	    tmp_tm.tm_min, tmp_tm.tm_sec, run_time);
  rename (log_filepath, new_filepath);
}
#endif /* ENABLE_UNUSED_FUNCTION */

void
cas_log_end (int mode, int run_time_sec, int run_time_msec)
{
#ifndef LIBCAS_FOR_JSP
  if (log_fp == NULL && as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      cas_log_open (shm_appl->broker_name, shm_as_index);
    }

  if (log_fp != NULL)
    {
      long log_file_size = 0;
      bool abandon = false;

      /* 'mode' will be either ALL, ERROR, or TIMEOUT */
      switch (mode)
	{
	case SQL_LOG_MODE_ALL:
	  /* if mode == ALL, write log regardless sql_log_mode */
	  break;
	case SQL_LOG_MODE_ERROR:
	  /* if mode == ERROR, write log if sql_log_mode == ALL || ERROR || NOTICE */
	  if (as_info->cur_sql_log_mode == SQL_LOG_MODE_NONE ||
	      as_info->cur_sql_log_mode == SQL_LOG_MODE_TIMEOUT)
	    {
	      abandon = true;
	    }
	  break;
	case SQL_LOG_MODE_TIMEOUT:
	  /* if mode == TIMEOUT, write log if sql_log_mode == ALL || TIMEOUT || NOTICE */
	  if (as_info->cur_sql_log_mode == SQL_LOG_MODE_NONE ||
	      as_info->cur_sql_log_mode == SQL_LOG_MODE_ERROR)
	    {
	      abandon = true;
	    }
	  /* if mode == TIMEOUT and sql_log_mode == TIMEOUT || NOTICE, check if it timed out */
	  else if (as_info->cur_sql_log_mode == SQL_LOG_MODE_TIMEOUT ||
		   as_info->cur_sql_log_mode == SQL_LOG_MODE_NOTICE)
	    {
	      /* check timeout */
	      if ((run_time_sec * 1000 + run_time_msec) <
		  shm_appl->long_transaction_time)
		{
		  abandon = true;
		}
	    }
	  break;
	  /* if mode == NONE, write log if sql_log_mode == ALL */
	case SQL_LOG_MODE_NONE:
	  if (as_info->cur_sql_log_mode != SQL_LOG_MODE_ALL)
	    {
	      abandon = true;
	    }
	  break;
	case SQL_LOG_MODE_NOTICE:
	default:
	  /* mode NOTICE or others are unexpected values; do not write log */
	  abandon = true;
	  break;
	}

      if (abandon)
	{
	  cas_log_write_internal (log_fp, 0, false, "END OF LOG\n\n", "");
	  fseek (log_fp, saved_log_fpos, SEEK_SET);
	}
      else
	{
	  if (run_time_sec >= 0 && run_time_msec >= 0)
	    {
	      cas_log_write (0, false, "*** elapsed time %d.%03d\n",
			     run_time_sec, run_time_msec);
	    }
	  saved_log_fpos = ftell (log_fp);

	  if ((saved_log_fpos / 1000) > shm_appl->sql_log_max_size)
	    {
	      cas_log_close (true);
	      cas_log_backup (FID_SQL_LOG_DIR);
	      cas_log_open (NULL, 0);
	    }
	  else
	    {
	      cas_log_write_internal (log_fp, 0, true, "END OF LOG\n\n", "");
	      fseek (log_fp, saved_log_fpos, SEEK_SET);
	    }
	}
    }
#endif /* LIBCAS_FOR_JSP */
}

static void
cas_log_write_internal (FILE * fp, unsigned int seq_num, bool do_flush,
			const char *fmt, va_list ap)
{
  char buf[LINE_MAX], *p;
  int len, n;

  p = buf;
  len = LINE_MAX;
  n = ut_time_string (p);
  len -= n;
  p += n;
  if (len > 0)
    {
      n = snprintf (p, len, " (%u) ", seq_num);
      len -= n;
      p += n;
      if (len > 0)
	{
	  n = vsnprintf (p, len, fmt, ap);
	  len -= n;
	  p += n;
	}
    }
  fwrite (buf, (p - buf), 1, fp);

  if (do_flush == true)
    {
      fflush (fp);
    }
}

void
cas_log_write_nonl (unsigned int seq_num, bool unit_start, const char *fmt,
		    ...)
{
#ifndef LIBCAS_FOR_JSP
  if (log_fp == NULL && as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      cas_log_open (shm_appl->broker_name, shm_as_index);
    }

  if (log_fp != NULL)
    {
      va_list ap;

      if (unit_start)
	{
	  saved_log_fpos = ftell (log_fp);
	}
      va_start (ap, fmt);
      cas_log_write_internal (log_fp, seq_num,
			      (as_info->cur_sql_log_mode == SQL_LOG_MODE_ALL),
			      fmt, ap);
      va_end (ap);
    }
#endif /* LIBCAS_FOR_JSP */
}

void
cas_log_write (unsigned int seq_num, bool unit_start, const char *fmt, ...)
{
#ifndef LIBCAS_FOR_JSP
  if (log_fp == NULL && as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      cas_log_open (shm_appl->broker_name, shm_as_index);
    }

  if (log_fp != NULL)
    {
      va_list ap;

      if (unit_start)
	{
	  saved_log_fpos = ftell (log_fp);
	}
      va_start (ap, fmt);
      cas_log_write_internal (log_fp, seq_num,
			      (as_info->cur_sql_log_mode == SQL_LOG_MODE_ALL),
			      fmt, ap);
      va_end (ap);
      fputc ('\n', log_fp);
    }
#endif /* LIBCAS_FOR_JSP */
}

void
cas_log_write_and_end (unsigned int seq_num, bool unit_start, const char *fmt,
		       ...)
{
#ifndef LIBCAS_FOR_JSP
  if (log_fp == NULL && as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      cas_log_open (shm_appl->broker_name, shm_as_index);
    }

  if (log_fp != NULL)
    {
      va_list ap;

      if (unit_start)
	{
	  saved_log_fpos = ftell (log_fp);
	}
      va_start (ap, fmt);
      cas_log_write_internal (log_fp, seq_num,
			      (as_info->cur_sql_log_mode == SQL_LOG_MODE_ALL),
			      fmt, ap);
      va_end (ap);
      fputc ('\n', log_fp);
      cas_log_end (SQL_LOG_MODE_ALL, -1, -1);
    }
#endif /* LIBCAS_FOR_JSP */
}

static void
cas_log_write2_internal (FILE * fp, bool do_flush, const char *fmt,
			 va_list ap)
{
  char buf[LINE_MAX], *p;
  int len, n;

  p = buf;
  len = LINE_MAX;
  n = vsnprintf (p, len, fmt, ap);
  len -= n;
  p += n;
  fwrite (buf, (p - buf), 1, fp);

  if (do_flush == true)
    {
      fflush (fp);
    }
}

void
cas_log_write2_nonl (const char *fmt, ...)
{
#ifndef LIBCAS_FOR_JSP
  if (log_fp == NULL && as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      cas_log_open (shm_appl->broker_name, shm_as_index);
    }

  if (log_fp != NULL)
    {
      va_list ap;

      va_start (ap, fmt);
      cas_log_write2_internal (log_fp, (as_info->cur_sql_log_mode ==
					SQL_LOG_MODE_ALL), fmt, ap);
      va_end (ap);
    }
#endif /* LIBCAS_FOR_JSP */
}

void
cas_log_write2 (const char *fmt, ...)
{
#ifndef LIBCAS_FOR_JSP
  if (log_fp == NULL && as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      cas_log_open (shm_appl->broker_name, shm_as_index);
    }

  if (log_fp != NULL)
    {
      va_list ap;

      va_start (ap, fmt);
      cas_log_write2_internal (log_fp, (as_info->cur_sql_log_mode ==
					SQL_LOG_MODE_ALL), fmt, ap);
      va_end (ap);
      fputc ('\n', log_fp);
    }
#endif /* LIBCAS_FOR_JSP */
}

void
cas_log_write_value_string (char *value, int size)
{
#ifndef LIBCAS_FOR_JSP
  if (log_fp == NULL && as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      cas_log_open (shm_appl->broker_name, shm_as_index);
    }

  if (log_fp != NULL)
    {
      fwrite (value, size, 1, log_fp);
    }
#endif /* LIBCAS_FOR_JSP */
}

void
cas_log_write_query_string (char *query, int size)
{
#ifndef LIBCAS_FOR_JSP
  if (log_fp == NULL && as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      cas_log_open (shm_appl->broker_name, shm_as_index);
    }

  if (log_fp != NULL && query != NULL)
    {
      char *s;

      for (s = query; *s; s++)
	{
	  if (*s == '\n' || *s == '\r')
	    {
	      fputc (' ', log_fp);
	    }
	  else
	    {
	      fputc (*s, log_fp);
	    }
	}
      fputc ('\n', log_fp);
    }
#endif /* LIBCAS_FOR_JSP */
}

#if !defined (NDEBUG)
void
cas_log_debug (const char *file_name, const int line_no, const char *fmt, ...)
{
#if 0
#ifndef LIBCAS_FOR_JSP
  if (log_fp != NULL)
    {
      char buf[LINE_MAX], *p;
      int len, n;
      va_list ap;

      va_start (ap, fmt);
      p = buf;
      len = LINE_MAX;
      n = ut_time_string (p);
      len -= n;
      p += n;
      if (len > 0)
	{
	  n = snprintf (p, len, " (debug) file %s line %d ",
			file_name, line_no);
	  len -= n;
	  p += n;
	  if (len > 0)
	    {
	      n = vsnprintf (p, len, fmt, ap);
	      len -= n;
	      p += n;
	    }
	}
      fwrite (buf, (p - buf), 1, log_fp);
      fputc ('\n', log_fp);
      va_end (ap);
    }
#endif /* LIBCAS_FOR_JSP */
#endif
}
#endif

#ifdef CAS_ERROR_LOG

#if defined (ENABLE_UNUSED_FUNCTION)
void
cas_error_log (int err_code, char *err_msg_str, int client_ip_addr)
{
#ifndef LIBCAS_FOR_JSP
  FILE *fp;
  char *err_log_file = getenv (ERROR_LOG_ENV_STR);
  char *script_file = getenv (PATH_INFO_ENV_STR);
  time_t t = time (NULL);
  struct tm ct1;
  char err_code_str[12];
  char *lastcmd = "";
  char *ip_str;

  localtime_r (&t, &ct1);
  ct1.tm_year += 1900;

  fp = sql_log_open (err_log_file);
  if (fp == NULL)
    {
      return;
    }

#ifdef CAS_ERROR_LOG
  error_file_offset = ftell (fp);
#endif

  if (script_file == NULL)
    script_file = "";
  sprintf (err_code_str, "%d", err_code);
  ip_str = ut_uchar2ipstr ((unsigned char *) (&client_ip_addr));

  fprintf (fp, "[%d] %s %s %d/%d/%d %d:%d:%d %d\n%s:%s\ncmd:%s\n",
	   (int) getpid (), ip_str, script_file,
	   ct1.tm_year, ct1.tm_mon + 1, ct1.tm_mday,
	   ct1.tm_hour, ct1.tm_min, ct1.tm_sec,
	   (int) (strlen (err_code_str) + strlen (err_msg_str) + 1),
	   err_code_str, err_msg_str, lastcmd);
  fclose (fp);

  cas_log_error_flag = 1;
#endif /* LIBCAS_FOR_JSP */
}
#endif /* ENABLE_UNUSED_FUNCTION */
#endif

int
cas_access_log (struct timeval *start_time, int as_index, int client_ip_addr,
		char *dbname, char *dbuser, bool accepted)
{
#ifndef LIBCAS_FOR_JSP
  FILE *fp;
  char *access_log_file = getenv (ACCESS_LOG_ENV_STR);
  char *script = NULL;
  char *clt_ip;
  char *clt_appl = NULL;
  struct tm ct1, ct2;
  time_t t1, t2;
  char *p;
  char err_str[4];
  struct timeval end_time;

  gettimeofday (&end_time, NULL);

  t1 = start_time->tv_sec;
  t2 = end_time.tv_sec;
#if defined (WINDOWS)
  if (localtime_s (&ct1, &t1) != 0 || localtime_s (&ct2, &t2) != 0)
#else /* !WINDOWS */
  if (localtime_r (&t1, &ct1) == NULL || localtime_r (&t2, &ct2) == NULL)
#endif /* !WINDOWS */
    {
      return -1;
    }
  ct1.tm_year += 1900;
  ct2.tm_year += 1900;

  fp = sql_log_open (access_log_file);
  if (fp == NULL)
    {
      return -1;
    }

  if (script == NULL)
    script = (char *) "-";
  if (clt_appl == NULL || clt_appl[0] == '\0')
    clt_appl = (char *) "-";
  clt_ip = ut_uchar2ipstr ((unsigned char *) (&client_ip_addr));

  for (p = clt_appl; *p; p++)
    {
      if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
	*p = '_';
    }

#ifdef CAS_ERROR_LOG
  if (error_file_offset >= 0)
    sprintf (err_str, "ERR");
  else
#endif
    sprintf (err_str, "-");

#ifdef CAS_ERROR_LOG
  fprintf (fp,
	   "%d %s %s %s %d.%03d %d.%03d %02d/%02d/%02d %02d:%02d:%02d ~ "
	   "%02d/%02d/%02d %02d:%02d:%02d %d %s %d %s %s %s\n",
	   as_index + 1, clt_ip, clt_appl, script,
	   (int) start_time->tv_sec, (int) (start_time->tv_usec / 1000),
	   (int) end_time.tv_sec, (int) (end_time.tv_usec / 1000),
	   ct1.tm_year, ct1.tm_mon + 1, ct1.tm_mday, ct1.tm_hour, ct1.tm_min,
	   ct1.tm_sec, ct2.tm_year, ct2.tm_mon + 1, ct2.tm_mday, ct2.tm_hour,
	   ct2.tm_min, ct2.tm_sec,
	   (int) getpid (), err_str, error_file_offset, dbname, dbuser,
	   ((accepted) ? "" : " : rejected"));
#else
  fprintf (fp,
	   "%d %s %s %s %d.%03d %d.%03d %02d/%02d/%02d %02d:%02d:%02d ~ "
	   "%02d/%02d/%02d %02d:%02d:%02d %d %s %d %s %s %s\n",
	   as_index + 1, clt_ip, clt_appl, script,
	   (int) start_time->tv_sec, (int) (start_time->tv_usec / 1000),
	   (int) end_time.tv_sec, (int) (end_time.tv_usec / 1000),
	   ct1.tm_year, ct1.tm_mon + 1, ct1.tm_mday, ct1.tm_hour, ct1.tm_min,
	   ct1.tm_sec, ct2.tm_year, ct2.tm_mon + 1, ct2.tm_mday, ct2.tm_hour,
	   ct2.tm_min, ct2.tm_sec, (int) getpid (), err_str, -1, dbname,
	   dbuser, ((accepted) ? "" : " : rejected"));
#endif

  fclose (fp);
  return (end_time.tv_sec - start_time->tv_sec);
#else /* LIBCAS_FOR_JSP */
  return 0;
#endif /* !LIBCAS_FOR_JSP */
}

void
cas_log_query_info_init (int id, char is_only_query_plan)
{
#if !defined(LIBCAS_FOR_JSP) && !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
  char *plan_dump_filename;

  plan_dump_filename = cas_log_query_plan_file (id);
  unlink (plan_dump_filename);
  db_query_plan_dump_file (plan_dump_filename);

  if (is_only_query_plan)
    {
      set_optimization_level (514);
    }
  else
    {
      set_optimization_level (513);
    }
#endif /* !LIBCAS_FOR_JSP && !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
}

char *
cas_log_query_plan_file (int id)
{
#ifndef LIBCAS_FOR_JSP
  static char plan_file_name[PATH_MAX];
  char dirname[PATH_MAX];
  get_cubrid_file (FID_CAS_TMP_DIR, dirname);
  snprintf (plan_file_name, PATH_MAX - 1, "%s/%d.%d.plan", dirname,
	    (int) getpid (), id);
  return plan_file_name;
#else /* LIBCAS_FOR_JSP */
  return NULL;
#endif /* !LIBCAS_FOR_JSP */
}

static FILE *
sql_log_open (char *log_file_name)
{
  FILE *fp;
  int log_file_len = 0;
  int ret;
  int tmp_dirlen = 0;
  char *tmp_dirname;
  char *tmp_filename;

  if (log_file_name == NULL)
    return NULL;

  fp = fopen (log_file_name, "a");
  if (fp == NULL)
    {
      if (errno == ENOENT)
	{
	  tmp_filename = strdup (log_file_name);
	  if (tmp_filename == NULL)
	    {
	      return NULL;
	    }
	  tmp_dirname = dirname (tmp_filename);
	  ret = mkdir (tmp_dirname, 0777);
	  free (tmp_filename);
	  if (ret == 0)
	    {
	      fp = fopen (log_file_name, "a");
	      if (fp == NULL)
		{
		  return NULL;
		}
	    }
	  else
	    {
	      return NULL;
	    }
	}
      else
	{
	  return NULL;
	}
    }
  return fp;
}

void
cas_slow_log_open (char *br_name, int as_index)
{
#ifndef LIBCAS_FOR_JSP
  if (slow_log_fp != NULL)
    {
      cas_slow_log_close ();
    }

  if (as_info->cur_slow_log_mode != SLOW_LOG_MODE_OFF)
    {
      if (br_name != NULL)
	{
	  make_sql_log_filename (FID_SLOW_LOG_DIR, slow_log_filepath,
				 PATH_MAX, br_name, as_index);
	}

      /* note: in "a+" mode, output is always appended */
      slow_log_fp = fopen (slow_log_filepath, "a+");
      if (slow_log_fp == NULL)
	{
	  assert (0);
	}
    }
  else
    {
      slow_log_fp = NULL;
    }
  as_info->cas_slow_log_reset = 0;
#endif /* LIBCAS_FOR_JSP */
}

void
cas_slow_log_reset (char *br_name, int as_index)
{
#ifndef LIBCAS_FOR_JSP
  if (as_info->cas_slow_log_reset)
    {
      if (slow_log_fp != NULL)
	{
	  cas_slow_log_close ();
	}
      if ((as_info->cas_slow_log_reset & CAS_LOG_RESET_REMOVE) != 0)
	{
	  unlink (slow_log_filepath);
	}

      cas_slow_log_open (br_name, as_index);
    }
#endif /* LIBCAS_FOR_JSP */
}

void
cas_slow_log_close ()
{
#ifndef LIBCAS_FOR_JSP
  if (slow_log_fp != NULL)
    {
      fclose (slow_log_fp);
      slow_log_fp = NULL;
    }
#endif /* LIBCAS_FOR_JSP */
}

void
cas_slow_log_end ()
{
#ifndef LIBCAS_FOR_JSP
  if (slow_log_fp == NULL && as_info->cur_slow_log_mode != SLOW_LOG_MODE_OFF)
    {
      cas_slow_log_open (shm_appl->broker_name, shm_as_index);
    }

  if (slow_log_fp != NULL)
    {
      long slow_log_fpos;
      slow_log_fpos = ftell (slow_log_fp);

      if ((slow_log_fpos / 1000) > shm_appl->sql_log_max_size)
	{
	  cas_slow_log_close ();
	  cas_log_backup (FID_SLOW_LOG_DIR);
	  cas_slow_log_open (NULL, 0);
	}
      else
	{
	  fputc ('\n', slow_log_fp);
	  fflush (slow_log_fp);
	}
    }
#endif /* LIBCAS_FOR_JSP */
}

void
cas_slow_log_write (unsigned int seq_num, bool unit_start, const char *fmt,
		    ...)
{
#ifndef LIBCAS_FOR_JSP
  if (slow_log_fp == NULL && as_info->cur_slow_log_mode != SLOW_LOG_MODE_OFF)
    {
      cas_slow_log_open (shm_appl->broker_name, shm_as_index);
    }

  if (slow_log_fp != NULL)
    {
      va_list ap;

      va_start (ap, fmt);
      cas_log_write_internal (slow_log_fp, seq_num, false, fmt, ap);
      va_end (ap);
    }
#endif /* LIBCAS_FOR_JSP */
}

void
cas_slow_log_write2 (const char *fmt, ...)
{
#ifndef LIBCAS_FOR_JSP
  if (slow_log_fp == NULL && as_info->cur_slow_log_mode != SLOW_LOG_MODE_OFF)
    {
      cas_slow_log_open (shm_appl->broker_name, shm_as_index);
    }

  if (slow_log_fp != NULL)
    {
      va_list ap;

      va_start (ap, fmt);
      cas_log_write2_internal (slow_log_fp, false, fmt, ap);
      va_end (ap);
    }
#endif /* LIBCAS_FOR_JSP */
}

void
cas_slow_log_write_value_string (char *value, int size)
{
#ifndef LIBCAS_FOR_JSP
  if (slow_log_fp == NULL && as_info->cur_slow_log_mode != SLOW_LOG_MODE_OFF)
    {
      cas_slow_log_open (shm_appl->broker_name, shm_as_index);
    }

  if (slow_log_fp != NULL)
    {
      fwrite (value, size, 1, slow_log_fp);
    }
#endif /* LIBCAS_FOR_JSP */
}

void
cas_slow_log_write_query_string (char *query, int size)
{
#ifndef LIBCAS_FOR_JSP
  if (slow_log_fp == NULL && as_info->cur_slow_log_mode != SLOW_LOG_MODE_OFF)
    {
      cas_slow_log_open (shm_appl->broker_name, shm_as_index);
    }

  if (slow_log_fp != NULL && query != NULL)
    {
      char *s;

      for (s = query; *s; s++)
	{
	  if (*s == '\n' || *s == '\r')
	    {
	      fputc (' ', slow_log_fp);
	    }
	  else
	    {
	      fputc (*s, slow_log_fp);
	    }
	}
      fputc ('\n', slow_log_fp);
    }
#endif /* LIBCAS_FOR_JSP */
}
