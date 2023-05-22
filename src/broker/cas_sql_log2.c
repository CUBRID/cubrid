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
 * cas_sql_log2.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>

#if !defined(WINDOWS)
#include <unistd.h>
#include <sys/time.h>
#endif

#include "cas_common.h"
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
#include "dbi.h"
#endif
#include "cas_execute.h"
#include "cas_sql_log2.h"
#include "broker_filename.h"
#include "broker_util.h"

#if !defined(WINDOWS)
static char sql_log2_file[256] = "";
static FILE *sql_log2_fp = NULL;
static int log_count = 0;
static int saved_fd1;
#endif

void
sql_log2_init (char *br_name, int index, int sql_log_value, bool log_reuse_flag)
{
#if !defined(WINDOWS)
  char filename[BROKER_PATH_MAX], dirname[BROKER_PATH_MAX];

  if (!sql_log_value)
    {
      return;
    }

  if (log_reuse_flag == false || sql_log2_file[0] == '\0')
    {
      sprintf (sql_log2_file, "%s/%s.%d.%d.%d", get_cubrid_file (FID_SQL_LOG2_DIR, dirname, BROKER_PATH_MAX), br_name,
	       index + 1, (int) time (NULL), log_count++);
    }
  get_cubrid_file (FID_SQL_LOG_DIR, dirname, BROKER_PATH_MAX);
  if (snprintf (filename, sizeof (filename) - 1, "%s%s", dirname, sql_log2_file) < 0)
    {
      sql_log2_file[0] = '\0';
      return;
    }

  sql_log2_fp = fopen (filename, "a");
  if (sql_log2_fp == NULL)
    {
      sql_log2_file[0] = '\0';
    }
#endif
}

char *
sql_log2_get_filename ()
{
#if defined(WINDOWS)
  return "";
#else
  return (sql_log2_file == NULL) ? (char *) "" : (char *) sql_log2_file;
#endif
}

void
sql_log2_dup_stdout ()
{
#if !defined(WINDOWS)
  if (sql_log2_fp)
    {
      saved_fd1 = dup (1);
      dup2 (fileno (sql_log2_fp), 1);
    }
#endif
}

void
sql_log2_restore_stdout ()
{
#if !defined(WINDOWS)
  if (sql_log2_fp)
    {
      dup2 (saved_fd1, 1);
      close (saved_fd1);
    }
#endif
}

void
sql_log2_end (bool reset_filename_flag)
{
#if !defined(WINDOWS)
  if (sql_log2_fp)
    {
      fclose (sql_log2_fp);
    }
  sql_log2_fp = NULL;
  if (reset_filename_flag == true)
    sql_log2_file[0] = '\0';
#endif
}

void
sql_log2_flush ()
{
#if !defined(WINDOWS)
  fflush (stdout);
#endif
}

void
sql_log2_write (const char *fmt, ...)
{
#if !defined(WINDOWS)
  va_list ap;
  time_t t;
  struct tm lt;
  struct timeval tv;

  if (sql_log2_fp)
    {
      gettimeofday (&tv, NULL);
      t = tv.tv_sec;
      localtime_r (&t, &lt);
      va_start (ap, fmt);
      fprintf (sql_log2_fp, "%02d/%02d %02d:%02d:%02d.%03ld ", lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min,
	       lt.tm_sec, tv.tv_usec / 1000);
      vfprintf (sql_log2_fp, fmt, ap);
      fprintf (sql_log2_fp, "\n");
      fflush (sql_log2_fp);
      va_end (ap);
    }
#endif
}

void
sql_log2_append_file (char *file_name)
{
#if !defined(WINDOWS)
  FILE *in_fp;
  size_t read_len;
  char read_buf[1024];

  if (sql_log2_fp == NULL)
    return;

  in_fp = fopen (file_name, "r");
  if (in_fp == NULL)
    return;

  fflush (sql_log2_fp);
  while ((read_len = fread (read_buf, 1, sizeof (read_buf), in_fp)) > 0)
    {
      if (read_len > sizeof (read_buf))
	{
	  read_len = sizeof (read_buf);
	}
      fwrite (read_buf, 1, read_len, sql_log2_fp);
    }
  fclose (in_fp);
  fflush (sql_log2_fp);
#endif
}
