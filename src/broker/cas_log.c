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

#include "porting.h"
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

#if defined(WINDOWS)
typedef int mode_t;
#endif /* WINDOWS */

#define CAS_LOG_BUFFER_SIZE (8192)
#define SQL_LOG_BUFFER_SIZE 163840
#define ACCESS_LOG_IS_DENIED_TYPE(T)  ((T)==ACL_REJECTED)

static const char *get_access_log_type_string (ACCESS_LOG_TYPE type);
static char cas_log_buffer[CAS_LOG_BUFFER_SIZE];	/* 8K buffer */
static char sql_log_buffer[SQL_LOG_BUFFER_SIZE];

static char *make_sql_log_filename (T_CUBRID_FILE_ID fid, char *filename_buf,
				    size_t buf_size, const char *br_name);
static void cas_log_backup (T_CUBRID_FILE_ID fid);
static void cas_log_write_and_set_savedpos (FILE * log_fp, const char *fmt,
					    ...);


#if defined (ENABLE_UNUSED_FUNCTION)
static void cas_log_rename (int run_time, time_t cur_time, char *br_name,
			    int as_index);
#endif
static void cas_log_write_internal (FILE * fp, struct timeval *log_time,
				    unsigned int seq_num, bool do_flush,
				    const char *fmt, va_list ap);
static void cas_log_write2_internal (FILE * fp, bool do_flush,
				     const char *fmt, va_list ap);

static FILE *access_log_open (char *log_file_name);
static bool cas_log_begin_hang_check_time (void);
static void cas_log_end_hang_check_time (bool is_prev_time_set);
static void
cas_log_write_query_string_internal (char *query, int size, bool newline);

#ifdef CAS_ERROR_LOG
static int error_file_offset;
static char cas_log_error_flag;
#endif
static FILE *log_fp = NULL, *slow_log_fp = NULL;
static char log_filepath[BROKER_PATH_MAX], slow_log_filepath[BROKER_PATH_MAX];
static long saved_log_fpos = 0;

static size_t cas_fwrite (const void *ptr, size_t size, size_t nmemb,
			  FILE * stream);
static long int cas_ftell (FILE * stream);
static int cas_fseek (FILE * stream, long int offset, int whence);
static FILE *cas_fopen (const char *path, const char *mode);
#if defined (WINDOWS)
static FILE *cas_fopen_and_lock (const char *path, const char *mode);
#endif
static int cas_fclose (FILE * fp);
static int cas_ftruncate (int fd, off_t length);
static int cas_fflush (FILE * stream);
static int cas_fileno (FILE * stream);
static int cas_fprintf (FILE * stream, const char *format, ...);
static int cas_fputc (int c, FILE * stream);
static int cas_unlink (const char *pathname);
static int cas_rename (const char *oldpath, const char *newpath);
static int cas_mkdir (const char *pathname, mode_t mode);
static void access_log_backup (char *access_log_file, struct tm *ct);

static char *
make_sql_log_filename (T_CUBRID_FILE_ID fid, char *filename_buf,
		       size_t buf_size, const char *br_name)
{
#ifndef LIBCAS_FOR_JSP
  char dirname[BROKER_PATH_MAX];

  assert (filename_buf != NULL);

  get_cubrid_file (fid, dirname, BROKER_PATH_MAX);
  switch (fid)
    {
    case FID_SQL_LOG_DIR:
      if (cas_shard_flag == ON)
	{
	  snprintf (filename_buf, buf_size, "%s%s_%d_%d_%d.sql.log", dirname,
		    br_name, shm_proxy_id + 1, shm_shard_id,
		    shm_shard_cas_id + 1);
	}
      else
	{
	  snprintf (filename_buf, buf_size, "%s%s_%d.sql.log", dirname,
		    br_name, shm_as_index + 1);
	}
      break;
    case FID_SLOW_LOG_DIR:
      if (cas_shard_flag == ON)
	{
	  snprintf (filename_buf, buf_size, "%s%s_%d_%d_%d.slow.log", dirname,
		    br_name, shm_proxy_id + 1, shm_shard_id,
		    shm_shard_cas_id + 1);
	}
      else
	{
	  snprintf (filename_buf, buf_size, "%s%s_%d.slow.log", dirname,
		    br_name, shm_as_index + 1);
	}
      break;
    default:
      assert (0);
      snprintf (filename_buf, buf_size, "unknown.log");
      break;
    }
  return filename_buf;
#endif /* LIBCAS_FOR_JSP */
  return NULL;
}

void
cas_log_open (char *br_name)
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
	  if (as_info->cas_log_reset == CAS_LOG_RESET_REOPEN)
	    {
	      set_cubrid_file (FID_SQL_LOG_DIR, shm_appl->log_dir);
	    }

	  make_sql_log_filename (FID_SQL_LOG_DIR, log_filepath,
				 BROKER_PATH_MAX, br_name);
	}

      /* note: in "a+" mode, output is always appended */
      log_fp = cas_fopen (log_filepath, "r+");
      if (log_fp != NULL)
	{
	  cas_fseek (log_fp, 0, SEEK_END);
	  saved_log_fpos = cas_ftell (log_fp);
	}
      else
	{
	  log_fp = cas_fopen (log_filepath, "w");
	  saved_log_fpos = 0;
	}

      if (log_fp)
	{
	  setvbuf (log_fp, sql_log_buffer, _IOFBF, SQL_LOG_BUFFER_SIZE);
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
cas_log_reset (char *br_name)
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
	  cas_unlink (log_filepath);
	}

      if (as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
	{
	  cas_log_open (br_name);
	}
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
	  cas_fseek (log_fp, saved_log_fpos, SEEK_SET);
	  cas_ftruncate (cas_fileno (log_fp), saved_log_fpos);
	}
      cas_fclose (log_fp);
      log_fp = NULL;
      saved_log_fpos = 0;
    }
#endif /* LIBCAS_FOR_JSP */
}

static void
cas_log_backup (T_CUBRID_FILE_ID fid)
{
  char backup_filepath[BROKER_PATH_MAX];
  char *filepath = NULL;

  switch (fid)
    {
    case FID_SQL_LOG_DIR:
      assert (log_filepath[0] != '\0');
      filepath = log_filepath;
      break;
    case FID_SLOW_LOG_DIR:
      assert (slow_log_filepath[0] != '\0');
      filepath = slow_log_filepath;
      break;
    default:
      assert (0);
      return;
    }

  snprintf (backup_filepath, BROKER_PATH_MAX, "%s.bak", filepath);
  cas_unlink (backup_filepath);
  cas_rename (filepath, backup_filepath);
}

static void
cas_log_write_and_set_savedpos (FILE * log_fp, const char *fmt, ...)
{
  va_list ap;

  if (log_fp == NULL)
    {
      return;
    }

  va_start (ap, fmt);
  cas_log_write_internal (log_fp, NULL, 0, false, fmt, ap);
  va_end (ap);

  cas_fseek (log_fp, saved_log_fpos, SEEK_SET);

  return;
}

#if defined (ENABLE_UNUSED_FUNCTION)
static void
cas_log_rename (int run_time, time_t cur_time, char *br_name, int as_index)
{
  char new_filepath[BROKER_PATH_MAX];
  struct tm tmp_tm;

  assert (log_filepath[0] != '\0');

  localtime_r (&cur_time, &tmp_tm);
  tmp_tm.tm_year += 1900;

  snprintf (new_filepath, BROKER_PATH_MAX, "%s.%02d%02d%02d%02d%02d.%d",
	    log_filepath, tmp_tm.tm_mon + 1, tmp_tm.tm_mday, tmp_tm.tm_hour,
	    tmp_tm.tm_min, tmp_tm.tm_sec, run_time);
  cas_rename (log_filepath, new_filepath);
}
#endif /* ENABLE_UNUSED_FUNCTION */

void
cas_log_end (int mode, int run_time_sec, int run_time_msec)
{
#ifndef LIBCAS_FOR_JSP
  if (log_fp == NULL && as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      cas_log_open (shm_appl->broker_name);
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
	  cas_log_write_and_set_savedpos (log_fp, "%s", "END OF LOG\n\n");
	}
      else
	{
	  if (run_time_sec >= 0 && run_time_msec >= 0)
	    {
	      cas_log_write (0, false, "*** elapsed time %d.%03d\n",
			     run_time_sec, run_time_msec);
	    }
	  saved_log_fpos = cas_ftell (log_fp);

	  if ((saved_log_fpos / 1000) > shm_appl->sql_log_max_size)
	    {
	      cas_log_close (true);
	      cas_log_backup (FID_SQL_LOG_DIR);
	      cas_log_open (NULL);
	    }
	  else
	    {
	      cas_log_write_and_set_savedpos (log_fp, "%s", "END OF LOG\n\n");
	    }
	}
    }
#endif /* LIBCAS_FOR_JSP */
}

static void
cas_log_write_internal (FILE * fp, struct timeval *log_time,
			unsigned int seq_num, bool do_flush,
			const char *fmt, va_list ap)
{
  char *buf, *p;
  int len, n;

  p = buf = cas_log_buffer;
  len = CAS_LOG_BUFFER_SIZE;
  n = ut_time_string (p, log_time);
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
	  if (n >= len)
	    {
	      /* string is truncated and trailing '\0' is included */
	      n = len - 1;
	    }
	  len -= n;
	  p += n;
	}
    }

  cas_fwrite (buf, (p - buf), 1, fp);

  if (do_flush == true)
    {
      cas_fflush (fp);
    }
}

void
cas_log_write_nonl (unsigned int seq_num, bool unit_start, const char *fmt,
		    ...)
{
#ifndef LIBCAS_FOR_JSP
  if (log_fp == NULL && as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      cas_log_open (shm_appl->broker_name);
    }

  if (log_fp != NULL)
    {
      va_list ap;

      if (unit_start)
	{
	  saved_log_fpos = cas_ftell (log_fp);
	}
      va_start (ap, fmt);
      cas_log_write_internal (log_fp, NULL, seq_num,
			      (as_info->cur_sql_log_mode == SQL_LOG_MODE_ALL),
			      fmt, ap);
      va_end (ap);
    }
#endif /* LIBCAS_FOR_JSP */
}

static void
cas_log_query_cancel (int dummy, ...)
{
#ifndef LIBCAS_FOR_JSP
  va_list ap;
  const char *fmt;
  char buf[LINE_MAX];
  bool log_mode;
  struct timeval tv;

  if (log_fp == NULL || query_cancel_flag != 1)
    {
      return;
    }

  tv.tv_sec = query_cancel_time / 1000;
  tv.tv_usec = (query_cancel_time % 1000) * 1000;

  if (as_info->clt_version >= CAS_PROTO_MAKE_VER (PROTOCOL_V1))
    {
      char ip_str[16];
      ut_get_ipv4_string (ip_str, 16, as_info->cas_clt_ip);
      fmt = "query_cancel client ip %s port %u";
      snprintf (buf, LINE_MAX, fmt, ip_str, as_info->cas_clt_port);
    }
  else
    {
      snprintf (buf, LINE_MAX, "query_cancel");
    }

  log_mode = as_info->cur_sql_log_mode == SQL_LOG_MODE_ALL;
  va_start (ap, dummy);
  cas_log_write_internal (log_fp, &tv, 0, log_mode, buf, ap);
  va_end (ap);
  cas_fputc ('\n', log_fp);

  query_cancel_flag = 0;
#endif /* LIBCAS_FOR_JSP */
}

void
cas_log_write (unsigned int seq_num, bool unit_start, const char *fmt, ...)
{
#ifndef LIBCAS_FOR_JSP
  if (log_fp == NULL && as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      cas_log_open (shm_appl->broker_name);
    }

  cas_log_query_cancel (0);

  if (log_fp != NULL)
    {
      va_list ap;

      if (unit_start)
	{
	  saved_log_fpos = cas_ftell (log_fp);
	}
      va_start (ap, fmt);
      cas_log_write_internal (log_fp, NULL, seq_num,
			      (as_info->cur_sql_log_mode == SQL_LOG_MODE_ALL),
			      fmt, ap);
      va_end (ap);
      cas_fputc ('\n', log_fp);
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
      cas_log_open (shm_appl->broker_name);
    }

  if (log_fp != NULL)
    {
      va_list ap;

      if (unit_start)
	{
	  saved_log_fpos = cas_ftell (log_fp);
	}
      va_start (ap, fmt);
      cas_log_write_internal (log_fp, NULL, seq_num,
			      (as_info->cur_sql_log_mode == SQL_LOG_MODE_ALL),
			      fmt, ap);
      va_end (ap);
      cas_fputc ('\n', log_fp);
      cas_log_end (SQL_LOG_MODE_ALL, -1, -1);
    }
#endif /* LIBCAS_FOR_JSP */
}

static void
cas_log_write2_internal (FILE * fp, bool do_flush, const char *fmt,
			 va_list ap)
{
  char *buf, *p;
  int len, n;

  p = buf = cas_log_buffer;
  len = CAS_LOG_BUFFER_SIZE;
  n = vsnprintf (p, len, fmt, ap);
  if (n >= len)
    {
      /* string is truncated and trailing '\0' is included */
      n = len - 1;
    }
  len -= n;
  p += n;

  cas_fwrite (buf, (p - buf), 1, fp);

  if (do_flush == true)
    {
      cas_fflush (fp);
    }
}

void
cas_log_write2_nonl (const char *fmt, ...)
{
#ifndef LIBCAS_FOR_JSP
  if (log_fp == NULL && as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      cas_log_open (shm_appl->broker_name);
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
      cas_log_open (shm_appl->broker_name);
    }

  if (log_fp != NULL)
    {
      va_list ap;

      va_start (ap, fmt);
      cas_log_write2_internal (log_fp, (as_info->cur_sql_log_mode ==
					SQL_LOG_MODE_ALL), fmt, ap);
      va_end (ap);
      cas_fputc ('\n', log_fp);
    }
#endif /* LIBCAS_FOR_JSP */
}

void
cas_log_write_value_string (char *value, int size)
{
#ifndef LIBCAS_FOR_JSP
  if (log_fp == NULL && as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      cas_log_open (shm_appl->broker_name);
    }

  if (log_fp != NULL)
    {
      cas_fwrite (value, size, 1, log_fp);
    }
#endif /* LIBCAS_FOR_JSP */
}

void
cas_log_write_query_string_nonl (char *query, int size)
{
  cas_log_write_query_string_internal (query, size, false);
}

void
cas_log_write_query_string (char *query, int size)
{
  cas_log_write_query_string_internal (query, size, true);
}

static void
cas_log_write_query_string_internal (char *query, int size, bool newline)
{
#ifndef LIBCAS_FOR_JSP
  if (log_fp == NULL && as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      cas_log_open (shm_appl->broker_name);
    }

  if (log_fp != NULL && query != NULL)
    {
      char *s;

      for (s = query; *s; s++)
	{
	  if (*s == '\n' || *s == '\r')
	    {
	      cas_fputc (' ', log_fp);
	    }
	  else
	    {
	      cas_fputc (*s, log_fp);
	    }
	}

      if (newline)
	{
	  fputc ('\n', log_fp);
	}
    }
#endif /* LIBCAS_FOR_JSP */
}

void
cas_log_write_client_ip (const unsigned char *ip_addr)
{
#ifndef LIBCAS_FOR_JSP
  char client_ip_str[16];

  if (ip_addr != NULL && *((int *) ip_addr) != 0)
    {
      ut_get_ipv4_string (client_ip_str, sizeof (client_ip_str), ip_addr);
      cas_log_write_and_end (0, false, "CLIENT IP %s", client_ip_str);
    }
#endif
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
      cas_fwrite (buf, (p - buf), 1, log_fp);
      cas_fputc ('\n', log_fp);
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
  char *err_log_file = shm_appl->error_log_file;
  char *script_file = getenv (PATH_INFO_ENV_STR);
  time_t t = time (NULL);
  struct tm ct1;
  char err_code_str[12];
  char *lastcmd = "";
  char *ip_str;

  localtime_r (&t, &ct1);
  ct1.tm_year += 1900;

  fp = access_log_open (err_log_file);
  if (fp == NULL)
    {
      return;
    }

#ifdef CAS_ERROR_LOG
  error_file_offset = cas_ftell (fp);
#endif

  if (script_file == NULL)
    script_file = "";
  sprintf (err_code_str, "%d", err_code);
  ip_str = ut_uchar2ipstr ((unsigned char *) (&client_ip_addr));

  cas_fprintf (fp, "[%d] %s %s %d/%d/%d %d:%d:%d %d\n%s:%s\ncmd:%s\n",
	       (int) getpid (), ip_str, script_file,
	       ct1.tm_year, ct1.tm_mon + 1, ct1.tm_mday,
	       ct1.tm_hour, ct1.tm_min, ct1.tm_sec,
	       (int) (strlen (err_code_str) + strlen (err_msg_str) + 1),
	       err_code_str, err_msg_str, lastcmd);
  cas_fclose (fp);

  cas_log_error_flag = 1;
#endif /* LIBCAS_FOR_JSP */
}
#endif /* ENABLE_UNUSED_FUNCTION */
#endif

int
cas_access_log (struct timeval *start_time, int as_index, int client_ip_addr,
		char *dbname, char *dbuser, ACCESS_LOG_TYPE log_type)
{
#ifndef LIBCAS_FOR_JSP
  FILE *fp;
  char *access_log_file = shm_appl->access_log_file;
  char clt_ip_str[16];
  struct tm ct1, ct2;
  time_t t1, t2;
  struct timeval end_time;
  char log_file_buf[PATH_MAX];
  const char *print_format =
    "%d %s %04d/%02d/%02d %02d:%02d:%02d %s %s %s %s\n";
  char session_id_buf[16];

  gettimeofday (&end_time, NULL);

  t1 = start_time->tv_sec;
  t2 = end_time.tv_sec;
  if (localtime_r (&t1, &ct1) == NULL || localtime_r (&t2, &ct2) == NULL)
    {
      return -1;
    }
  ct1.tm_year += 1900;
  ct2.tm_year += 1900;

  if (ACCESS_LOG_IS_DENIED_TYPE (log_type))
    {
      int n;
      n = snprintf (log_file_buf, PATH_MAX, "%s%s",
		    shm_appl->access_log_file,
		    ACCESS_LOG_DENIED_FILENAME_POSTFIX);
      if (n >= PATH_MAX)
	{
	  return -1;
	}

      access_log_file = log_file_buf;
    }

  fp = access_log_open (access_log_file);
  if (fp == NULL)
    {
      return -1;
    }

  fseek (fp, 0, SEEK_END);
  if ((ftell (fp) / ONE_K) > shm_appl->access_log_max_size)
    {
      time_t cur_time = time (NULL);
      struct tm ct;

      if (localtime_r (&cur_time, &ct) != NULL)
	{
	  ct.tm_year += 1900;

	  cas_fclose (fp);

	  access_log_backup (access_log_file, &ct);

	  fp = access_log_open (access_log_file);
	  if (fp == NULL)
	    {
	      return -1;
	    }
	}
    }

  ut_get_ipv4_string (clt_ip_str, sizeof (clt_ip_str),
		      (unsigned char *) (&client_ip_addr));

  session_id_buf[0] = '\0';

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
  if (!ACCESS_LOG_IS_DENIED_TYPE (log_type))
    {
      sprintf (session_id_buf, "%u", db_get_session_id ());
    }
#endif

  cas_fprintf (fp, print_format, as_index + 1, clt_ip_str,
	       ct1.tm_year, ct1.tm_mon + 1, ct1.tm_mday, ct1.tm_hour,
	       ct1.tm_min, ct1.tm_sec, dbname, dbuser,
	       get_access_log_type_string (log_type), session_id_buf);

  cas_fclose (fp);
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
  cas_unlink (plan_dump_filename);
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
  static char plan_file_name[BROKER_PATH_MAX];
  char dirname[BROKER_PATH_MAX];
  get_cubrid_file (FID_CAS_TMP_DIR, dirname, BROKER_PATH_MAX);
  snprintf (plan_file_name, BROKER_PATH_MAX - 1, "%s/%d.%d.plan", dirname,
	    (int) getpid (), id);
  return plan_file_name;
#else /* LIBCAS_FOR_JSP */
  return NULL;
#endif /* !LIBCAS_FOR_JSP */
}

static FILE *
access_log_open (char *log_file_name)
{
  FILE *fp;
  int log_file_len = 0;
  int ret;
  int tmp_dirlen = 0;
  char *tmp_dirname;
  char *tmp_filename;

  if (log_file_name == NULL)
    return NULL;

#if defined (WINDOWS)
  fp = cas_fopen_and_lock (log_file_name, "a");
#else
  /* In case of Linux and solaris...,
   * Openning a file in append mode guarantees subsequent
   * write operations to occur at ent-of-file.
   * So we don't need to lock to the opened file.
   */
  fp = cas_fopen (log_file_name, "a");
#endif

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
	  ret = cas_mkdir (tmp_dirname, 0777);
	  free (tmp_filename);
	  if (ret == 0)
	    {
#if defined (WINDOWS)
	      fp = cas_fopen_and_lock (log_file_name, "a");
#else
	      fp = cas_fopen (log_file_name, "a");
#endif
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
cas_slow_log_open (char *br_name)
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
	  if (as_info->cas_slow_log_reset == CAS_LOG_RESET_REOPEN)
	    {
	      set_cubrid_file (FID_SLOW_LOG_DIR, shm_appl->slow_log_dir);
	    }

	  make_sql_log_filename (FID_SLOW_LOG_DIR, slow_log_filepath,
				 BROKER_PATH_MAX, br_name);
	}

      /* note: in "a+" mode, output is always appended */
      slow_log_fp = cas_fopen (slow_log_filepath, "a+");
    }
  else
    {
      slow_log_fp = NULL;
    }
  as_info->cas_slow_log_reset = 0;
#endif /* LIBCAS_FOR_JSP */
}

void
cas_slow_log_reset (char *br_name)
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
	  cas_unlink (slow_log_filepath);
	}

      if (as_info->cur_slow_log_mode != SLOW_LOG_MODE_OFF)
	{
	  cas_slow_log_open (br_name);
	}
    }
#endif /* LIBCAS_FOR_JSP */
}

void
cas_slow_log_close ()
{
#ifndef LIBCAS_FOR_JSP
  if (slow_log_fp != NULL)
    {
      cas_fclose (slow_log_fp);
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
      cas_slow_log_open (shm_appl->broker_name);
    }

  if (slow_log_fp != NULL)
    {
      long slow_log_fpos;
      slow_log_fpos = cas_ftell (slow_log_fp);

      if ((slow_log_fpos / 1000) > shm_appl->sql_log_max_size)
	{
	  cas_slow_log_close ();
	  cas_log_backup (FID_SLOW_LOG_DIR);
	  cas_slow_log_open (NULL);
	}
      else
	{
	  cas_fputc ('\n', slow_log_fp);
	  cas_fflush (slow_log_fp);
	}
    }
#endif /* LIBCAS_FOR_JSP */
}

void
cas_slow_log_write_and_end (struct timeval *log_time, unsigned int seq_num,
			    const char *fmt, ...)
{
#ifndef LIBCAS_FOR_JSP
  if (slow_log_fp == NULL && as_info->cur_slow_log_mode != SLOW_LOG_MODE_OFF)
    {
      cas_slow_log_open (shm_appl->broker_name);
    }

  if (slow_log_fp != NULL)
    {
      va_list ap;

      va_start (ap, fmt);
      cas_log_write_internal (slow_log_fp, log_time, seq_num, false, fmt, ap);
      va_end (ap);

      cas_slow_log_end ();
    }

#endif /* LIBCAS_FOR_JSP */
}

void
cas_slow_log_write (struct timeval *log_time, unsigned int seq_num,
		    bool unit_start, const char *fmt, ...)
{
#ifndef LIBCAS_FOR_JSP
  if (slow_log_fp == NULL && as_info->cur_slow_log_mode != SLOW_LOG_MODE_OFF)
    {
      cas_slow_log_open (shm_appl->broker_name);
    }

  if (slow_log_fp != NULL)
    {
      va_list ap;

      va_start (ap, fmt);
      cas_log_write_internal (slow_log_fp, log_time, seq_num, false, fmt, ap);
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
      cas_slow_log_open (shm_appl->broker_name);
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
      cas_slow_log_open (shm_appl->broker_name);
    }

  if (slow_log_fp != NULL)
    {
      cas_fwrite (value, size, 1, slow_log_fp);
    }
#endif /* LIBCAS_FOR_JSP */
}

void
cas_slow_log_write_query_string (char *query, int size)
{
#ifndef LIBCAS_FOR_JSP
  if (slow_log_fp == NULL && as_info->cur_slow_log_mode != SLOW_LOG_MODE_OFF)
    {
      cas_slow_log_open (shm_appl->broker_name);
    }

  if (slow_log_fp != NULL && query != NULL)
    {
      char *s;

      for (s = query; *s; s++)
	{
	  if (*s == '\n' || *s == '\r')
	    {
	      cas_fputc (' ', slow_log_fp);
	    }
	  else
	    {
	      cas_fputc (*s, slow_log_fp);
	    }
	}
      cas_fputc ('\n', slow_log_fp);
    }
#endif /* LIBCAS_FOR_JSP */
}

static bool
cas_log_begin_hang_check_time (void)
{
#if !defined(LIBCAS_FOR_JSP)
  if (cas_shard_flag == OFF)
    {
#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
      bool is_prev_time_set = (as_info->claimed_alive_time > 0);
      if (!is_prev_time_set)
	{
	  set_hang_check_time ();
	}
      return is_prev_time_set;
#endif /* CAS_FOR_ORACLE || CAS_FOR_MYSQL */
    }
#endif /* !LIBCAS_FOR_JSP */
  return false;
}

static void
cas_log_end_hang_check_time (bool is_prev_time_set)
{
#if !defined(LIBCAS_FOR_JSP)
  if (cas_shard_flag == OFF)
    {
#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
      if (!is_prev_time_set)
	{
	  unset_hang_check_time ();
	}
#endif /* CAS_FOR_ORACLE || CAS_FOR_MYSQL */
    }
#endif /* !LIBCAS_FOR_JSP */
}

static size_t
cas_fwrite (const void *ptr, size_t size, size_t nmemb, FILE * stream)
{
  bool is_prev_time_set;
  size_t result;

  is_prev_time_set = cas_log_begin_hang_check_time ();
  result = fwrite (ptr, size, nmemb, stream);
  cas_log_end_hang_check_time (is_prev_time_set);

  return result;
}

static long int
cas_ftell (FILE * stream)
{
  return ftell (stream);
}

static int
cas_fseek (FILE * stream, long int offset, int whence)
{
  bool is_prev_time_set;
  int result;

  is_prev_time_set = cas_log_begin_hang_check_time ();
  result = fseek (stream, offset, whence);
  cas_log_end_hang_check_time (is_prev_time_set);

  return result;
}

static FILE *
cas_fopen (const char *path, const char *mode)
{
  bool is_prev_time_set;
  FILE *result;

  is_prev_time_set = cas_log_begin_hang_check_time ();
  result = fopen (path, mode);
  cas_log_end_hang_check_time (is_prev_time_set);

  return result;
}

#if defined (WINDOWS)
static FILE *
cas_fopen_and_lock (const char *path, const char *mode)
{
#define MAX_RETRY_COUNT 100
  int retry_count;
  bool is_prev_time_set;
  FILE *result;

  retry_count = 0;
  is_prev_time_set = cas_log_begin_hang_check_time ();

retry:
  result = fopen (path, mode);
  if (result != NULL)
    {
      if (lockf (fileno (result), F_TLOCK, 0) < 0)
	{
	  fclose (result);
	  if (retry_count < MAX_RETRY_COUNT)
	    {
	      SLEEP_MILISEC (0, 10);
	      retry_count++;
	      goto retry;
	    }
	  result = NULL;
	}
    }
  cas_log_end_hang_check_time (is_prev_time_set);

  return result;
}
#endif

static int
cas_fclose (FILE * fp)
{
  bool is_prev_time_set;
  int result;

  is_prev_time_set = cas_log_begin_hang_check_time ();
  result = fclose (fp);
  cas_log_end_hang_check_time (is_prev_time_set);

  return result;
}

static int
cas_ftruncate (int fd, off_t length)
{
  bool is_prev_time_set;
  int result;

  is_prev_time_set = cas_log_begin_hang_check_time ();
  result = ftruncate (fd, length);
  cas_log_end_hang_check_time (is_prev_time_set);

  return result;
}

static int
cas_fflush (FILE * stream)
{
  bool is_prev_time_set;
  int result;

  is_prev_time_set = cas_log_begin_hang_check_time ();
  result = fflush (stream);
  cas_log_end_hang_check_time (is_prev_time_set);

  return result;

}

static int
cas_fileno (FILE * stream)
{
  bool is_prev_time_set;
  int result;

  is_prev_time_set = cas_log_begin_hang_check_time ();
  result = fileno (stream);
  cas_log_end_hang_check_time (is_prev_time_set);

  return result;
}

static int
cas_fprintf (FILE * stream, const char *format, ...)
{
  bool is_prev_time_set;
  int result;
  va_list ap;

  va_start (ap, format);

  is_prev_time_set = cas_log_begin_hang_check_time ();
  result = vfprintf (stream, format, ap);
  cas_log_end_hang_check_time (is_prev_time_set);

  va_end (ap);

  return result;
}

static int
cas_fputc (int c, FILE * stream)
{
  bool is_prev_time_set;
  int result;

  is_prev_time_set = cas_log_begin_hang_check_time ();
  result = fputc (c, stream);
  cas_log_end_hang_check_time (is_prev_time_set);

  return result;
}

static int
cas_unlink (const char *pathname)
{
  bool is_prev_time_set;
  int result;

  is_prev_time_set = cas_log_begin_hang_check_time ();
  result = unlink (pathname);
  cas_log_end_hang_check_time (is_prev_time_set);

  return result;
}

static int
cas_rename (const char *oldpath, const char *newpath)
{
  bool is_prev_time_set;
  int result;

  is_prev_time_set = cas_log_begin_hang_check_time ();
  result = rename (oldpath, newpath);
  cas_log_end_hang_check_time (is_prev_time_set);

  return result;
}

static int
cas_mkdir (const char *pathname, mode_t mode)
{
  bool is_prev_time_set;
  int result;

  is_prev_time_set = cas_log_begin_hang_check_time ();
  result = mkdir (pathname, mode);
  cas_log_end_hang_check_time (is_prev_time_set);

  return result;
}

static void
access_log_backup (char *access_log_file, struct tm *ct)
{
  char cmd_buf[BUFSIZ];

  sprintf (cmd_buf, "%s.%04d%02d%02d%02d%02d%02d",
	   access_log_file,
	   ct->tm_year, ct->tm_mon + 1, ct->tm_mday, ct->tm_hour, ct->tm_min,
	   ct->tm_sec);
  rename (access_log_file, cmd_buf);
}

static const char *
get_access_log_type_string (ACCESS_LOG_TYPE type)
{
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
  switch (type)
    {
    case NEW_CONNECTION:
      return "NEW";
    case CLIENT_CHANGED:
      return "OLD";
    case ACL_REJECTED:
      return "REJECT";
    default:
      assert (0);
      break;
    }
#endif

  return "";
}
