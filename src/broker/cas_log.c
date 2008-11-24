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
 * cas_log.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#ifdef WIN32
#include <sys/timeb.h>
#include <process.h>
#include <io.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif

#include "cas_common.h"
#include "cas_log.h"
#include "cas_util.h"
#include "broker_config.h"
#include "cas.h"

#include "broker_env_def.h"
#include "broker_filename.h"

#ifdef WIN32
#include "cas_db_inc.h"
#endif

#define MAKE_SQL_LOG_FILENAME(FILENAME, BR_NAME, AS_INDEX)	\
	do {							\
	  get_cubrid_file(FID_SQL_LOG_DIR, FILENAME);		\
	  sprintf(FILENAME, "%s%s_%d.sql.log", FILENAME, BR_NAME, (AS_INDEX) + 1); \
	} while (0)

static void sql_log_rename (int run_time, time_t cur_time, char *br_name,
			    int as_index);
static void sql_log_backup (char *br_name, int as_index);
static FILE *sql_log_open (char *log_file_name);

#ifdef CAS_ERROR_LOG
static int error_file_offset;
static char cas_log_error_flag;
#endif
static FILE *log_fp = NULL;

static FILE *log_fp_qp, *log_fp_qh;
static int saved_fd1;

void
cas_log_init (T_TIMEVAL * start_time)
{
#ifdef CAS_ERROR_LOG
  error_file_offset = -1;
  cas_log_error_flag = 0;
#endif
  TIMEVAL_MAKE (start_time);
}

void
cas_log_open (char *br_name, int as_index)
{
#ifndef LIBCAS_FOR_JSP
  char sql_log_file[PATH_MAX];

  if (sql_log_mode)
    {
      MAKE_SQL_LOG_FILENAME (sql_log_file, br_name, as_index);
      log_fp = fopen (sql_log_file,
		      (sql_log_mode & SQL_LOG_MODE_APPEND) ? "a" : "w");
    }
  else
    {
      log_fp = NULL;
    }
  shm_appl->as_info[shm_as_index].cas_log_reset = 0;
#endif
}

void
cas_log_reset (char *br_name, int as_index)
{
#ifndef LIBCAS_FOR_JSP
  if (shm_appl->as_info[shm_as_index].cas_log_reset)
    {
      if (log_fp != NULL)
	{
	  fclose (log_fp);
	  log_fp = NULL;
	}

      if (shm_appl->as_info[shm_as_index].
	  cas_log_reset & CAS_LOG_RESET_REMOVE)
	{
	  char sql_log_filename[PATH_MAX];
	  MAKE_SQL_LOG_FILENAME (sql_log_filename, br_name, as_index);
	  unlink (sql_log_filename);
	}

      cas_log_open (br_name, as_index);
    }

#endif
}

void
cas_log_end (T_TIMEVAL * start_time, char *br_name, int as_index,
	     int sql_log_time, char *sql_log2_file, int sql_log_max_size,
	     int close_flag)
{
#ifndef LIBCAS_FOR_JSP
  if (log_fp)
    {
      T_TIMEVAL cur_time;
      int run_time_sec = -1, run_time_msec;
      int log_file_size = 0;

      if (sql_log2_file != NULL && sql_log2_file[0] != '\0')
	{
	  fprintf (log_fp, "%s\n", sql_log2_file);
	}

      if (start_time != NULL)
	{
	  TIMEVAL_MAKE (&cur_time);
	  ut_timeval_diff (start_time, &cur_time, &run_time_sec,
			   &run_time_msec);

	  fprintf (log_fp, "*** %d.%03d\n\n", run_time_sec, run_time_msec);
	  log_file_size = ftell (log_fp);
	}

      if (sql_log_mode & SQL_LOG_MODE_APPEND)
	{
	  log_file_size /= 1000;
	  if ((sql_log_max_size > 0) && (log_file_size > sql_log_max_size))
	    {
	      fclose (log_fp);
	      log_fp = NULL;
	      sql_log_backup (br_name, as_index);
	    }
	}
      else
	{
	  fclose (log_fp);
	  log_fp = NULL;
	  if (run_time_sec >= sql_log_time)
	    {
	      sql_log_rename (run_time_sec, TIMEVAL_GET_SEC (&cur_time),
			      br_name, as_index);
	    }
	}

      if (close_flag == TRUE)
	{
	  if (log_fp != NULL)
	    {
	      fclose (log_fp);
	      log_fp = NULL;
	    }
	}
      else
	{
	  if (log_fp == NULL)
	    {
	      sql_log_mode = shm_appl->sql_log_mode;
	      if (!(sql_log_mode & SQL_LOG_MODE_ON))
		{
		  sql_log_mode = SQL_LOG_MODE_OFF;
		}
	      cas_log_open (br_name, as_index);
	    }
	  else
	    {
	      fflush (log_fp);
	      if (!(shm_appl->sql_log_mode & SQL_LOG_MODE_ON))
		{
		  sql_log_mode = SQL_LOG_MODE_OFF;
		  fclose (log_fp);
		  log_fp = NULL;
		}
	    }
	  TIMEVAL_MAKE (start_time);
	}
    }
  else
    {
      if (close_flag == FALSE)
	{
	  sql_log_mode = shm_appl->sql_log_mode;
	  if (!(sql_log_mode & SQL_LOG_MODE_ON))
	    {
	      sql_log_mode = SQL_LOG_MODE_OFF;
	    }
	  cas_log_open (br_name, as_index);
	}
    }
#endif
}

void
cas_log_write (unsigned int seq_num, char print_new_line, const char *fmt, ...)
{
#ifndef LIBCAS_FOR_JSP
  if (log_fp)
    {
      va_list ap;
      time_t t;
      struct tm lt;
      T_TIMEVAL tv;

      TIMEVAL_MAKE (&tv);
      t = TIMEVAL_GET_SEC (&tv);
      lt = *localtime (&t);

      va_start (ap, fmt);
      fprintf (log_fp, "%02d/%02d %02d:%02d:%02d.%03d (%u) ",
	       lt.tm_mon + 1, lt.tm_mday,
	       lt.tm_hour, lt.tm_min, lt.tm_sec, TIMEVAL_GET_MSEC (&tv),
	       seq_num);
      vfprintf (log_fp, fmt, ap);
      if (print_new_line)
	fprintf (log_fp, "\n");
      fflush (log_fp);
      va_end (ap);
    }
#endif
}

void
cas_log_write2 (const char *fmt, ...)
{
#ifndef LIBCAS_FOR_JSP
  if (log_fp)
    {
      va_list ap;

      va_start (ap, fmt);
      vfprintf (log_fp, fmt, ap);
      va_end (ap);
    }
#endif
}

void
cas_log_write_query_string (char *query, char print_new_line)
{
#ifndef LIBCAS_FOR_JSP
  if (log_fp)
    {
      if (shm_appl->sql_log_single_line)
	{
	  char *buf;
	  ALLOC_COPY (buf, query);
	  if (buf)
	    {
	      char *p;
	      for (p = buf; *p; p++)
		{
		  if (*p == '\n' || *p == '\r')
		    *p = ' ';
		}

	      cas_log_write2 ("%s%s", buf, print_new_line ? "\n" : "");
	      FREE_MEM (buf);
	    }
	}
      else
	{
	  cas_log_write2 ("%s%s", query, print_new_line ? "\n" : "");
	}
    }
#endif
}

#ifdef CAS_ERROR_LOG
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

  ct1 = *localtime (&t);
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
#endif
}
#endif

int
cas_access_log (T_TIMEVAL * start_time, int as_index, int client_ip_addr)
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
  T_TIMEVAL end;

  TIMEVAL_MAKE (&end);

  t1 = TIMEVAL_GET_SEC (start_time);
  t2 = TIMEVAL_GET_SEC (&end);
  ct1 = *localtime (&t1);
  ct2 = *localtime (&t2);
  ct1.tm_year += 1900;
  ct2.tm_year += 1900;

  fp = sql_log_open (access_log_file);
  if (fp == NULL)
    {
      return -1;
    }

  if (script == NULL)
    script = "-";
  if (clt_appl == NULL || clt_appl[0] == '\0')
    clt_appl = "-";
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

  fprintf (fp,
	   "%d %s %s %s %d.%03d %d.%03d %02d/%02d/%02d %02d:%02d:%02d ~ %02d/%02d/%02d %02d:%02d:%02d %d %s %d\n",
	   as_index + 1, clt_ip, clt_appl, script,
	   TIMEVAL_GET_SEC (start_time), TIMEVAL_GET_MSEC (start_time),
	   TIMEVAL_GET_SEC (&end), TIMEVAL_GET_MSEC (&end), ct1.tm_year,
	   ct1.tm_mon + 1, ct1.tm_mday, ct1.tm_hour, ct1.tm_min, ct1.tm_sec,
	   ct2.tm_year, ct2.tm_mon + 1, ct2.tm_mday, ct2.tm_hour, ct2.tm_min,
	   ct2.tm_sec,
#ifdef CAS_ERROR_LOG
	   (int) getpid (), err_str, error_file_offset);
#else
	   (int) getpid (), err_str, -1);
#endif

  fclose (fp);
  return ((TIMEVAL_GET_SEC (&end)) - (TIMEVAL_GET_SEC (start_time)));
#else
  return 0;
#endif
}

int
cas_log_query_info_init (int id)
{
#ifndef LIBCAS_FOR_JSP
  char *log_file_query_plan, *log_file_query_histo;

  log_file_query_plan = cas_log_query_plan_file (id);
  log_file_query_histo = cas_log_query_histo_file (id);

  unlink (log_file_query_plan);
  unlink (log_file_query_histo);
#ifdef WIN32
  db_query_plan_dump_file (log_file_query_plan);
#else
  log_fp_qp = fopen (log_file_query_plan, "a");
  if (log_fp_qp == NULL)
    return -1;
  log_fp_qh = fopen (log_file_query_histo, "a");
  if (log_fp_qh == NULL)
    {
      fclose (log_fp_qp);
      return -1;
    }
  saved_fd1 = dup (1);
  dup2 (fileno (log_fp_qp), 1);
#endif
  return 0;
#else
  return 0;
#endif
}

void
cas_log_query_info_next ()
{
#ifndef LIBCAS_FOR_JSP
  fflush (stdout);
  dup2 (fileno (log_fp_qh), 1);
  fflush (log_fp_qp);
  fclose (log_fp_qp);
#endif
}

void
cas_log_query_info_end ()
{
#ifndef LIBCAS_FOR_JSP
  fflush (stdout);
  dup2 (saved_fd1, 1);
  close (saved_fd1);
  fflush (log_fp_qh);
  fclose (log_fp_qh);
#endif
}

char *
cas_log_query_plan_file (int id)
{
#ifndef LIBCAS_FOR_JSP
  static char plan_file_name[PATH_MAX];
  get_cubrid_file (FID_CAS_TMP_DIR, plan_file_name);
  sprintf (plan_file_name, "%s/%d.%d.plan", plan_file_name, (int) getpid (),
	   id);
  return plan_file_name;
#else
  return NULL;
#endif
}

char *
cas_log_query_histo_file (int id)
{
#ifndef LIBCAS_FOR_JSP
  static char histo_file_name[PATH_MAX];
  get_cubrid_file (FID_CAS_TMP_DIR, histo_file_name);
  sprintf (histo_file_name, "%s/%d.%d.histo", histo_file_name,
	   (int) getpid (), id);
  return (histo_file_name);
#else
  return NULL;
#endif
}


static void
sql_log_rename (int run_time, time_t cur_time, char *br_name, int as_index)
{
  char src_log[PATH_MAX], dest_log[PATH_MAX];
  struct tm tmp_tm;

  get_cubrid_file (FID_SQL_LOG_DIR, src_log);
  sprintf (src_log, "%s%s_%d.sql.log", src_log, br_name, as_index + 1);
  tmp_tm = *localtime (&cur_time);
  tmp_tm.tm_year += 1900;
  get_cubrid_file (FID_SQL_LOG_DIR, dest_log);
  sprintf (dest_log, "%s%02d%02d%02d%02d%02d.%d.%s_%d", dest_log,
	   tmp_tm.tm_mon + 1, tmp_tm.tm_mday,
	   tmp_tm.tm_hour, tmp_tm.tm_min, tmp_tm.tm_sec,
	   run_time, br_name, as_index + 1);
  rename (src_log, dest_log);
}

static void
sql_log_backup (char *br_name, int as_index)
{
  char src_filename[PATH_MAX];
  char dest_filename[PATH_MAX];

  MAKE_SQL_LOG_FILENAME (src_filename, br_name, as_index);
  sprintf (dest_filename, "%s.bak", src_filename);

  unlink (dest_filename);
  rename (src_filename, dest_filename);
}

static FILE *
sql_log_open (char *log_file_name)
{
  FILE *fp;
  int log_file_len = 0;
  int i;
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
