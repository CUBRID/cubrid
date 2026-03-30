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

<<<<<<< HEAD
=======

>>>>>>> b61c37829 ([CBRD-26610] Update cas_query_info logic)
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
<<<<<<< HEAD
#endif /*  */
=======
#endif
>>>>>>> b61c37829 ([CBRD-26610] Update cas_query_info logic)

#include "cas_common.h"
#include "cas_query_info.h"
#include "broker_log_sql_list.h"
#include "broker_log_top.h"

#define LOG_TOP_RES_FILE	"log_top.res"
#define LOG_TOP_Q_FILE		"log_top.q"
#define LOG_TOP_TAG_FILE	"tags"
#define LOG_TOP_NE_FILE		"log_top.ne"
<<<<<<< HEAD
static int sort_func (const void *arg1, const void *arg2);
static char *time2str (int t, char *buf);
static T_QUERY_INFO *query_info_arr = NULL;
static int num_query_info = 0;

#ifdef TEST
static T_QUERY_INFO *query_info_arr_ne = NULL;
static int num_query_info_ne = 0;

#endif /*  */

#ifdef MT_MODE
static T_MUTEX query_info_mutex;

#endif /*  */
=======

static int sort_func (const void *arg1, const void *arg2);
static char *time2str (int t, char *buf);

static T_QUERY_INFO *query_info_arr = NULL;
static int num_query_info = 0;

#ifdef TEST
static T_QUERY_INFO *query_info_arr_ne = NULL;
static int num_query_info_ne = 0;
#endif

#ifdef MT_MODE
static T_MUTEX query_info_mutex;
#endif
>>>>>>> b61c37829 ([CBRD-26610] Update cas_query_info logic)

#ifdef MT_MODE
void
query_info_mutex_init ()
{
  MUTEX_INIT (query_info_mutex);
}
<<<<<<< HEAD
#endif /*  */
=======
#endif

>>>>>>> b61c37829 ([CBRD-26610] Update cas_query_info logic)
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
  FREE_MEM (qi->organized_sql);
  FREE_MEM (qi->cas_log);
  qi->start_date[0] = '\0';
<<<<<<< HEAD
} void

query_info_reset (void)
{
  int i;

#ifdef MT_MODE
  MUTEX_LOCK (query_info_mutex);

#endif /*  */
  if (query_info_arr != NULL)

    {
      for (i = 0; i < num_query_info; i++)

=======
}

void
query_info_reset (void)
{
  int i;
#ifdef MT_MODE
  MUTEX_LOCK (query_info_mutex);
#endif

  if (query_info_arr != NULL)
    {
      for (i = 0; i < num_query_info; i++)
>>>>>>> b61c37829 ([CBRD-26610] Update cas_query_info logic)
	{
	  query_info_clear (&query_info_arr[i]);
	}
      FREE_MEM (query_info_arr);
      num_query_info = 0;
    }

#ifdef TEST
  if (query_info_arr_ne != NULL)
<<<<<<< HEAD

    {
      for (i = 0; i < num_query_info_ne; i++)

=======
    {
      for (i = 0; i < num_query_info_ne; i++)
>>>>>>> b61c37829 ([CBRD-26610] Update cas_query_info logic)
	{
	  query_info_clear (&query_info_arr_ne[i]);
	}
      FREE_MEM (query_info_arr_ne);
      num_query_info_ne = 0;
    }
<<<<<<< HEAD

#endif /*  */

#ifdef MT_MODE
  MUTEX_UNLOCK (query_info_mutex);

#endif /*  */
=======
#endif

#ifdef MT_MODE
  MUTEX_UNLOCK (query_info_mutex);
#endif
>>>>>>> b61c37829 ([CBRD-26610] Update cas_query_info logic)
}

void
query_info_print (void)
{
  int i;
  char buf[1024];
  FILE *fp_res, *fp_q;
<<<<<<< HEAD

#ifdef TEST
  FILE *fp_tag, *fp_ne;

#endif /*  */
=======
#ifdef TEST
  FILE *fp_tag, *fp_ne;
#endif
>>>>>>> b61c37829 ([CBRD-26610] Update cas_query_info logic)
  char minstr[64], maxstr[64], avgstr[64];
  int xml_found;

#ifdef MT_MODE
  MUTEX_LOCK (query_info_mutex);
<<<<<<< HEAD

#endif /*  */
  fp_res = fopen (LOG_TOP_RES_FILE, "w");
  fp_q = fopen (LOG_TOP_Q_FILE, "w");

#ifdef TEST
  fp_tag = fopen (LOG_TOP_TAG_FILE, "w");
  fp_ne = fopen (LOG_TOP_NE_FILE, "w");

#endif /*  */
  if (fp_res == NULL || fp_q == NULL
#ifdef TEST
      || fp_tag == NULL || fp_ne == NULL
#endif /*  */
    )

=======
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
>>>>>>> b61c37829 ([CBRD-26610] Update cas_query_info logic)
    {
      fprintf (stderr, "%s\n", strerror (errno));
      goto query_info_print_end;
    }
<<<<<<< HEAD
  qsort (query_info_arr, num_query_info, sizeof (T_QUERY_INFO), sort_func);
  if (log_top_mode == MODE_PROC_TIME)
    fprintf (fp_res, "%8s %8s %9s %9s %10s\n", "", "max", "min", "avg", "cnt(err)");

  else
    fprintf (fp_res, "%8s %8s %10s\n", "", "max", "cnt");
  fprintf (fp_res, "-----------------------------------------------------\n");
  for (i = 0; i < num_query_info; i++)

    {
      sprintf (buf, "[Q%d]", i + 1);
      if (log_top_mode == MODE_PROC_TIME)

	{
	  fprintf (fp_res, "%-8s %9s %9s %9s %4d (%d)", buf, time2str (query_info_arr[i].max, maxstr),
		   time2str (query_info_arr[i].min, minstr),
		   time2str (query_info_arr[i].sum / query_info_arr[i].count, avgstr), query_info_arr[i].count,
		   query_info_arr[i].err_count);
	}

      else

	{
	  fprintf (fp_res, "%-8s %8d %10d", buf, query_info_arr[i].max, query_info_arr[i].count);
	}
      fprintf (fp_q, "%s-------------------------------------------\n", buf);
      xml_found = sql_info_write (query_info_arr[i].sql, buf, fp_q);
      if (xml_found)

	{
	  fprintf (fp_res, "%5s", "X");
	}
      fprintf (fp_res, "\n");
=======

  qsort (query_info_arr, num_query_info, sizeof (T_QUERY_INFO), sort_func);

  if (log_top_mode == MODE_PROC_TIME)
    fprintf (fp_res, "%8s %8s %9s %9s %10s\n", "", "max", "min", "avg", "cnt(err)");
  else
    fprintf (fp_res, "%8s %8s %10s\n", "", "max", "cnt");

  fprintf (fp_res, "-----------------------------------------------------\n");

  for (i = 0; i < num_query_info; i++)
    {
      sprintf (buf, "[Q%d]", i + 1);
      if (log_top_mode == MODE_PROC_TIME)
	{
	  fprintf (fp_res, "%-8s %9s %9s %9s %4d (%d)", buf, time2str (query_info_arr[i].max, maxstr),
		   time2str (query_info_arr[i].min, minstr), time2str (query_info_arr[i].sum / query_info_arr[i].count,
								       avgstr), query_info_arr[i].count,
		   query_info_arr[i].err_count);
	}
      else
	{
	  fprintf (fp_res, "%-8s %8d %10d", buf, query_info_arr[i].max, query_info_arr[i].count);
	}

      fprintf (fp_q, "%s-------------------------------------------\n", buf);

      xml_found = sql_info_write (query_info_arr[i].sql, buf, fp_q);
      if (xml_found)
	{
	  fprintf (fp_res, "%5s", "X");
	}

      fprintf (fp_res, "\n");

>>>>>>> b61c37829 ([CBRD-26610] Update cas_query_info logic)
      fwrite (query_info_arr[i].cas_log, query_info_arr[i].cas_log_len, 1, fp_q);
      fprintf (fp_q, "\n");

#ifdef TEST
      fprintf (fp_tag, "Q%d	%s	/^%s\n", i + 1, LOG_TOP_Q_FILE, buf);
<<<<<<< HEAD

#endif /*  */
=======
#endif
>>>>>>> b61c37829 ([CBRD-26610] Update cas_query_info logic)
    }

#ifdef TEST
  for (i = 0; i < num_query_info_ne; i++)
<<<<<<< HEAD

    {
      sprintf (buf, "[N%d]", i + 1);
      fprintf (fp_ne, "%s-------------------------------------------\n", buf);
      if (sql_info_write (query_info_arr_ne[i].sql, buf, fp_ne) < 0)

	{
	  break;
	}
      fprintf (fp_ne, "%s\n", query_info_arr_ne[i].cas_log);
    }

#endif /*  */
  fclose (fp_res);
  fclose (fp_q);

#ifdef TEST
  fclose (fp_tag);
  fclose (fp_ne);

#endif /*  */
=======
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
>>>>>>> b61c37829 ([CBRD-26610] Update cas_query_info logic)

#ifdef TEST
  sprintf (buf, "sort %s -o %s", LOG_TOP_TAG_FILE, LOG_TOP_TAG_FILE);
  system (buf);
<<<<<<< HEAD

#endif /*  */
query_info_print_end:
#ifdef MT_MODE
  MUTEX_UNLOCK (query_info_mutex);

#endif /*  */
=======
#endif

query_info_print_end:
#ifdef MT_MODE
  MUTEX_UNLOCK (query_info_mutex);
#endif
>>>>>>> b61c37829 ([CBRD-26610] Update cas_query_info logic)
  return;
}

int
query_info_add (T_QUERY_INFO * qi, int exec_time, int execute_res, char *filename, int lineno, char *end_date)
{
  int qi_idx = -1;
  int i;
  int retval;
<<<<<<< HEAD
=======

>>>>>>> b61c37829 ([CBRD-26610] Update cas_query_info logic)
  if (check_log_time (qi->start_date, end_date) < 0)
    return 0;

#ifdef MT_MODE
  MUTEX_LOCK (query_info_mutex);
<<<<<<< HEAD

#endif /*  */
=======
#endif
>>>>>>> b61c37829 ([CBRD-26610] Update cas_query_info logic)

#if 0
  if (qi->sql == NULL)
    goto query_info_add_end;
<<<<<<< HEAD

#endif /*  */
  for (i = 0; i < num_query_info; i++)

    {
      if (strcmp (query_info_arr[i].organized_sql, qi->organized_sql) == 0)

=======
#endif

  for (i = 0; i < num_query_info; i++)
    {
      if (strcmp (query_info_arr[i].organized_sql, qi->organized_sql) == 0)
>>>>>>> b61c37829 ([CBRD-26610] Update cas_query_info logic)
	{
	  qi_idx = i;
	  break;
	}
    }
<<<<<<< HEAD
  if (qi_idx == -1)

    {
      query_info_arr = (T_QUERY_INFO *) REALLOC (query_info_arr, sizeof (T_QUERY_INFO) * (num_query_info + 1));
      if (query_info_arr == NULL)

=======

  if (qi_idx == -1)
    {
      query_info_arr = (T_QUERY_INFO *) REALLOC (query_info_arr, sizeof (T_QUERY_INFO) * (num_query_info + 1));
      if (query_info_arr == NULL)
>>>>>>> b61c37829 ([CBRD-26610] Update cas_query_info logic)
	{
	  fprintf (stderr, "%s\n", strerror (errno));
	  retval = -1;
	  goto query_info_add_end;
	}
      qi_idx = num_query_info;
      query_info_init (&query_info_arr[qi_idx]);
      query_info_arr[qi_idx].sql = strdup (qi->sql);
      query_info_arr[qi_idx].organized_sql = strdup (qi->organized_sql);
      num_query_info++;
    }
<<<<<<< HEAD
  if (exec_time < query_info_arr[qi_idx].min)

=======

  if (exec_time < query_info_arr[qi_idx].min)
>>>>>>> b61c37829 ([CBRD-26610] Update cas_query_info logic)
    {
      query_info_arr[qi_idx].min = exec_time;
    }
  if (exec_time > query_info_arr[qi_idx].max)
<<<<<<< HEAD

=======
>>>>>>> b61c37829 ([CBRD-26610] Update cas_query_info logic)
    {
      query_info_arr[qi_idx].max = exec_time;
      FREE_MEM (query_info_arr[qi_idx].cas_log);
      query_info_arr[qi_idx].cas_log = (char *) MALLOC (strlen (filename) + qi->cas_log_len + 20);
      if (query_info_arr[qi_idx].cas_log == NULL)
<<<<<<< HEAD

=======
>>>>>>> b61c37829 ([CBRD-26610] Update cas_query_info logic)
	{
	  fprintf (stderr, "%s\n", strerror (errno));
	  retval = -1;
	  goto query_info_add_end;
	}
      sprintf (query_info_arr[qi_idx].cas_log, "%s:%d\n", filename, lineno);
      query_info_arr[qi_idx].cas_log_len = (int) strlen (query_info_arr[qi_idx].cas_log);
      memcpy (query_info_arr[qi_idx].cas_log + query_info_arr[qi_idx].cas_log_len, qi->cas_log, qi->cas_log_len);
      query_info_arr[qi_idx].cas_log_len += qi->cas_log_len;
    }
  query_info_arr[qi_idx].count++;
  query_info_arr[qi_idx].sum += exec_time;
  if (execute_res < 0)
<<<<<<< HEAD

=======
>>>>>>> b61c37829 ([CBRD-26610] Update cas_query_info logic)
    {
      query_info_arr[qi_idx].err_count++;
    }
  retval = 0;
<<<<<<< HEAD
query_info_add_end:
#ifdef MT_MODE
  MUTEX_UNLOCK (query_info_mutex);

#endif /*  */
=======

query_info_add_end:

#ifdef MT_MODE
  MUTEX_UNLOCK (query_info_mutex);
#endif
>>>>>>> b61c37829 ([CBRD-26610] Update cas_query_info logic)
  return retval;
}

int
query_info_add_ne (T_QUERY_INFO * qi, char *end_date)
{
<<<<<<< HEAD

#ifndef TEST
  /* query_info_arr_ne is used only in TEST mode */
  return 0;

#else /*  */
  int qi_idx = -1;
  int i;
  int retval;
  if (check_log_time (qi->start_date, end_date) < 0)

=======
#ifndef TEST
  /* query_info_arr_ne is used only in TEST mode */
  return 0;
#else
  int qi_idx = -1;
  int i;
  int retval;

  if (check_log_time (qi->start_date, end_date) < 0)
>>>>>>> b61c37829 ([CBRD-26610] Update cas_query_info logic)
    {
      return 0;
    }

#ifdef MT_MODE
  MUTEX_LOCK (query_info_mutex);
<<<<<<< HEAD

#endif /*  */
  for (i = 0; i < num_query_info; i++)

    {
      if (strcmp (query_info_arr[i].organized_sql, qi->organized_sql) == 0)

=======
#endif

  for (i = 0; i < num_query_info; i++)
    {
      if (strcmp (query_info_arr[i].organized_sql, qi->organized_sql) == 0)
>>>>>>> b61c37829 ([CBRD-26610] Update cas_query_info logic)
	{
	  retval = 0;
	  goto query_info_add_ne_end;
	}
    }
<<<<<<< HEAD
  for (i = 0; i < num_query_info_ne; i++)

    {
      if (strcmp (query_info_arr_ne[i].organized_sql, qi->organized_sql) == 0)

=======

  for (i = 0; i < num_query_info_ne; i++)
    {
      if (strcmp (query_info_arr_ne[i].organized_sql, qi->organized_sql) == 0)
>>>>>>> b61c37829 ([CBRD-26610] Update cas_query_info logic)
	{
	  qi_idx = i;
	  break;
	}
    }
<<<<<<< HEAD
  if (qi_idx == -1)

    {
      query_info_arr_ne = (T_QUERY_INFO *) REALLOC (query_info_arr_ne, sizeof (T_QUERY_INFO) * (num_query_info_ne + 1));
      if (query_info_arr_ne == NULL)

=======

  if (qi_idx == -1)
    {
      query_info_arr_ne = (T_QUERY_INFO *) REALLOC (query_info_arr_ne, sizeof (T_QUERY_INFO) * (num_query_info_ne + 1));
      if (query_info_arr_ne == NULL)
>>>>>>> b61c37829 ([CBRD-26610] Update cas_query_info logic)
	{
	  fprintf (stderr, "%s\n", strerror (errno));
	  retval = -1;
	  goto query_info_add_ne_end;
	}
      qi_idx = num_query_info_ne;
      query_info_init (&query_info_arr_ne[qi_idx]);
      query_info_arr_ne[qi_idx].sql = strdup (qi->sql);
      query_info_arr_ne[qi_idx].organized_sql = strdup (qi->organized_sql);
      num_query_info_ne++;
    }
<<<<<<< HEAD
  FREE_MEM (query_info_arr_ne[qi_idx].cas_log);
  query_info_arr_ne[qi_idx].cas_log = strdup (qi->cas_log);
  if (query_info_arr_ne[qi_idx].cas_log == NULL)

=======

  FREE_MEM (query_info_arr_ne[qi_idx].cas_log);
  query_info_arr_ne[qi_idx].cas_log = strdup (qi->cas_log);
  if (query_info_arr_ne[qi_idx].cas_log == NULL)
>>>>>>> b61c37829 ([CBRD-26610] Update cas_query_info logic)
    {
      fprintf (stderr, "%s\n", strerror (errno));
      retval = -1;
      goto query_info_add_ne_end;
    }
<<<<<<< HEAD
  retval = 0;
query_info_add_ne_end:
#ifdef MT_MODE
  MUTEX_UNLOCK (query_info_mutex);

#endif /*  */
  return retval;

=======

  retval = 0;

query_info_add_ne_end:
#ifdef MT_MODE
  MUTEX_UNLOCK (query_info_mutex);
#endif
  return retval;
>>>>>>> b61c37829 ([CBRD-26610] Update cas_query_info logic)
#endif /* define TEST */
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
<<<<<<< HEAD

  else
    sprintf (buf, "%d.%03d", sec, msec);
=======
  else
    sprintf (buf, "%d.%03d", sec, msec);

>>>>>>> b61c37829 ([CBRD-26610] Update cas_query_info logic)
  return buf;
}
