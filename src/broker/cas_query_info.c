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
 * cas_query_info.c - 
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#ifdef MT_MODE
#include <pthread.h>
#endif

#include "cas_common.h"
#include "cas_query_info.h"
#include "broker_log_sql_list.h"
#include "broker_log_top.h"

#define LOG_TOP_RES_FILE	"log_top.res"
#define LOG_TOP_Q_FILE		"log_top.q"
#define LOG_TOP_TAG_FILE	"tags"
#define LOG_TOP_NE_FILE		"log_top.ne"

static int sort_func (const void *arg1, const void *arg2);
static char *time2str (int t, char *buf);

static T_QUERY_INFO *query_info_arr = NULL;
static int num_query_info = 0;
static T_QUERY_INFO *query_info_arr_ne = NULL;
static int num_query_info_ne = 0;

#ifdef MT_MODE
static T_MUTEX query_info_mutex;
#endif

#ifdef MT_MODE
void
query_info_mutex_init ()
{
  MUTEX_INIT (query_info_mutex);
}
#endif

void
query_info_init (T_QUERY_INFO * qi)
{
  memset (qi, 0, sizeof (T_QUERY_INFO));
  qi->min = 9999999;
  qi->max = -1;
}

void
query_info_clear (T_QUERY_INFO * qi)
{
  FREE_MEM (qi->sql);
  FREE_MEM (qi->cas_log);
  qi->start_date[0] = '\0';
}

void
query_info_print ()
{
  int i;
  char buf[1024];
  FILE *fp_res, *fp_q;
#ifdef TEST
  FILE *fp_tag, *fp_ne;
#endif
  char minstr[64], maxstr[64], avgstr[64];
  int xml_found;

#ifdef MT_MODE
  MUTEX_LOCK (query_info_mutex);
#endif

  fp_res = fopen (LOG_TOP_RES_FILE, "w");
  fp_q = fopen (LOG_TOP_Q_FILE, "w");
#ifdef TEST
  fp_tag = fopen (LOG_TOP_TAG_FILE, "w");
  fp_ne = fopen (LOG_TOP_NE_FILE, "w");
#endif
  if (fp_res == NULL || fp_q == NULL
#ifdef TEST
      || fp_tag == NULL || fp_ne == NULL
#endif
    )
    {
      fprintf (stderr, "%s\n", strerror (errno));
      goto query_info_print_end;
    }

  qsort (query_info_arr, num_query_info, sizeof (T_QUERY_INFO), sort_func);

  if (log_top_mode == MODE_PROC_TIME)
    fprintf (fp_res, "%8s %8s %9s %9s %10s\n", "", "max", "min", "avg",
	     "cnt(err)");
  else
    fprintf (fp_res, "%8s %8s %10s\n", "", "max", "cnt");

  fprintf (fp_res, "-----------------------------------------------------\n");

  for (i = 0; i < num_query_info; i++)
    {
      sprintf (buf, "[Q%d]", i + 1);
      if (log_top_mode == MODE_PROC_TIME)
	{
	  fprintf (fp_res, "%-8s %9s %9s %9s %4d (%d)",
		   buf,
		   time2str (query_info_arr[i].max, maxstr),
		   time2str (query_info_arr[i].min, minstr),
		   time2str (query_info_arr[i].sum / query_info_arr[i].count,
			     avgstr), query_info_arr[i].count,
		   query_info_arr[i].err_count);
	}
      else
	{
	  fprintf (fp_res, "%-8s %8d %10d",
		   buf, query_info_arr[i].max, query_info_arr[i].count);
	}

      fprintf (fp_q, "%s-------------------------------------------\n", buf);

      xml_found = sql_info_write (query_info_arr[i].sql, buf, fp_q);
      if (xml_found)
	{
	  fprintf (fp_res, "%5s", "X");
	}

      fprintf (fp_res, "\n");

      fprintf (fp_q, "%s\n", query_info_arr[i].cas_log);

#ifdef TEST
      fprintf (fp_tag, "Q%d	%s	/^%s\n", i + 1, LOG_TOP_Q_FILE, buf);
#endif
    }

#ifdef TEST
  for (i = 0; i < num_query_info_ne; i++)
    {
      sprintf (buf, "[N%d]", i + 1);
      fprintf (fp_ne, "%s-------------------------------------------\n", buf);

      if (sql_info_write (query_info_arr_ne[i].sql, buf, fp_ne) < 0)
	{
	  break;
	}

      fprintf (fp_ne, "%s\n", query_info_arr_ne[i].cas_log);
    }
#endif

  fclose (fp_res);
  fclose (fp_q);
#ifdef TEST
  fclose (fp_tag);
  fclose (fp_ne);
#endif

#ifdef TEST
  sprintf (buf, "sort %s -o %s", LOG_TOP_TAG_FILE, LOG_TOP_TAG_FILE);
  system (buf);
#endif

query_info_print_end:
#ifdef MT_MODE
  MUTEX_UNLOCK (query_info_mutex);
#endif
  return;
}

int
query_info_add (T_QUERY_INFO * qi, int exec_time, int execute_res,
		char *filename, int lineno, char *end_date)
{
  int qi_idx = -1;
  int i;
  int retval;

  if (check_log_time (qi->start_date, end_date) < 0)
    return 0;

#ifdef MT_MODE
  MUTEX_LOCK (query_info_mutex);
#endif

#if 0
  if (qi->sql == NULL)
    goto query_info_add_end;
#endif

  for (i = 0; i < num_query_info; i++)
    {
      if (strcmp (query_info_arr[i].sql, qi->sql) == 0)
	{
	  qi_idx = i;
	  break;
	}
    }

  if (qi_idx == -1)
    {
      query_info_arr =
	(T_QUERY_INFO *) REALLOC (query_info_arr,
				  sizeof (T_QUERY_INFO) * (num_query_info +
							   1));
      if (query_info_arr == NULL)
	{
	  fprintf (stderr, "%s\n", strerror (errno));
	  retval = -1;
	  goto query_info_add_end;
	}
      qi_idx = num_query_info;
      query_info_init (&query_info_arr[qi_idx]);
      query_info_arr[qi_idx].sql = strdup (qi->sql);
      num_query_info++;
    }

  if (exec_time < query_info_arr[qi_idx].min)
    {
      query_info_arr[qi_idx].min = exec_time;
    }
  if (exec_time > query_info_arr[qi_idx].max)
    {
      query_info_arr[qi_idx].max = exec_time;
      FREE_MEM (query_info_arr[qi_idx].cas_log);
      query_info_arr[qi_idx].cas_log =
	(char *) MALLOC (strlen (filename) + strlen (qi->cas_log) + 20);
      if (query_info_arr[qi_idx].cas_log == NULL)
	{
	  fprintf (stderr, "%s\n", strerror (errno));
	  retval = -1;
	  goto query_info_add_end;
	}
      sprintf (query_info_arr[qi_idx].cas_log, "%s:%d\n%s", filename, lineno,
	       qi->cas_log);
    }
  query_info_arr[qi_idx].count++;
  query_info_arr[qi_idx].sum += exec_time;
  if (execute_res < 0)
    {
      query_info_arr[qi_idx].err_count++;
    }
  retval = 0;

query_info_add_end:

#ifdef MT_MODE
  MUTEX_UNLOCK (query_info_mutex);
#endif
  return retval;
}

int
query_info_add_ne (T_QUERY_INFO * qi, char *end_date)
{
  int qi_idx = -1;
  int i;
  int retval;

  if (check_log_time (qi->start_date, end_date) < 0)
    return 0;

#ifdef MT_MODE
  MUTEX_LOCK (query_info_mutex);
#endif

  for (i = 0; i < num_query_info; i++)
    {
      if (strcmp (query_info_arr[i].sql, qi->sql) == 0)
	{
	  retval = 0;
	  goto query_info_add_ne_end;
	}
    }

  for (i = 0; i < num_query_info_ne; i++)
    {
      if (strcmp (query_info_arr_ne[i].sql, qi->sql) == 0)
	{
	  qi_idx = i;
	  break;
	}
    }

  if (qi_idx == -1)
    {
      query_info_arr_ne =
	(T_QUERY_INFO *) REALLOC (query_info_arr_ne,
				  sizeof (T_QUERY_INFO) * (num_query_info_ne +
							   1));
      if (query_info_arr_ne == NULL)
	{
	  fprintf (stderr, "%s\n", strerror (errno));
	  retval = -1;
	  goto query_info_add_ne_end;
	}
      qi_idx = num_query_info_ne;
      query_info_init (&query_info_arr_ne[qi_idx]);
      query_info_arr_ne[qi_idx].sql = strdup (qi->sql);
      num_query_info_ne++;
    }

  FREE_MEM (query_info_arr_ne[qi_idx].cas_log);
  query_info_arr_ne[qi_idx].cas_log = strdup (qi->cas_log);
  if (query_info_arr_ne[qi_idx].cas_log == NULL)
    {
      fprintf (stderr, "%s\n", strerror (errno));
      retval = -1;
      goto query_info_add_ne_end;
    }

  retval = 0;

query_info_add_ne_end:
#ifdef MT_MODE
  MUTEX_UNLOCK (query_info_mutex);
#endif
  return retval;
}

static int
sort_func (const void *arg1, const void *arg2)
{
  return (((T_QUERY_INFO *) arg1)->max < ((T_QUERY_INFO *) arg2)->max);
}

static char *
time2str (int t, char *buf)
{
  int sec, msec;
  sec = t / 1000;
  msec = t % 1000;
  if (sec >= 60)
    sprintf (buf, "%d:%02d.%03d", sec / 60, sec % 60, msec);
  else
    sprintf (buf, "%d.%03d", sec, msec);

  return buf;
}
