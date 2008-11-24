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
 * cas_sql_log2.c - 
 */

#ident "$Id$"

#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>

#ifndef WIN32
#include <unistd.h>
#include <sys/time.h>
#endif

#include "cas_common.h"
#include "dbi.h"
#include "cas_sql_log2.h"
#include "broker_filename.h"

#ifndef WIN32
static char sql_log2_file[256] = "";
static FILE *sql_log2_fp = NULL;
static int log_count = 0;
static int saved_fd1;
#endif

void
sql_log2_init (char *br_name, int index, int sql_log_value,
	       char log_reuse_flag)
{
#ifndef WIN32
  char filename[PATH_MAX];

  if (!sql_log_value)
    {
      return;
    }

  if (log_reuse_flag == FALSE || sql_log2_file[0] == '\0')
    {
      sprintf (sql_log2_file, "%s/%s.%d.%d.%d",
	       SQL_LOG2_DIR, br_name, index + 1, (int) time (NULL),
	       log_count++);
    }
  get_cubrid_file (FID_SQL_LOG_DIR, filename);
  strcat (filename, sql_log2_file);

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
#ifdef WIN32
  return "";
#else
  return sql_log2_file;
#endif
}

void
sql_log2_dup_stdout ()
{
#ifndef WIN32
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
#ifndef WIN32
  if (sql_log2_fp)
    {
      dup2 (saved_fd1, 1);
      close (saved_fd1);
    }
#endif
}

void
sql_log2_end (char reset_filename_flag)
{
#ifndef WIN32
  if (sql_log2_fp)
    {
      fclose (sql_log2_fp);
    }
  sql_log2_fp = NULL;
  if (reset_filename_flag == TRUE)
    sql_log2_file[0] = '\0';
#endif
}

void
sql_log2_flush ()
{
#ifndef WIN32
  fflush (stdout);
#endif
}

void
sql_log2_write (const char *fmt, ...)
{
#ifndef WIN32
  va_list ap;
  time_t t;
  struct tm lt;
  T_TIMEVAL tv;

  if (sql_log2_fp)
    {
      TIMEVAL_MAKE (&tv);
      t = TIMEVAL_GET_SEC (&tv);
      lt = *localtime (&t);

      va_start (ap, fmt);
      fprintf (sql_log2_fp, "%02d/%02d %02d:%02d:%02d.%03d ",
	       lt.tm_mon + 1, lt.tm_mday, lt.tm_hour,
	       lt.tm_min, lt.tm_sec, TIMEVAL_GET_MSEC (&tv));
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
#ifndef WIN32
  FILE *in_fp;
  int read_len;
  char read_buf[1024];

  if (sql_log2_fp == NULL)
    return;

  in_fp = fopen (file_name, "r");
  if (in_fp == NULL)
    return;

  fflush (sql_log2_fp);
  while ((read_len = fread (read_buf, 1, sizeof (read_buf), in_fp)) > 0)
    {
      fwrite (read_buf, 1, read_len, sql_log2_fp);
    }
  fclose (in_fp);
  fflush (sql_log2_fp);
#endif
}

void
set_optimization_level (int level)
{
  DB_QUERY_RESULT *result = NULL;
  DB_QUERY_ERROR error;
  char sql_stmt[64];

  sprintf (sql_stmt, "set optimization level = %d", level);
  db_execute (sql_stmt, &result, &error);
  if (result)
    db_query_end (result);
}
