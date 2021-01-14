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
 * broker_log_sql_list.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>

#include "cas_common.h"
#include "broker_log_sql_list.h"
#include "broker_log_util.h"
#include "log_top_string.h"

#define IS_NEXT_QUERY(STR)	(strncmp(STR, "=================", 10) == 0)

#define LINE_BUF_SIZE	10000

static void sql_info_init (T_SQL_INFO * sql_info);
static int sql_info_add (const char *sql, char *sql_tag);
static int comp_func (const void *arg1, const void *arg2);
static void sql_change_comp_form (char *src, char *dest);

static T_SQL_INFO *sql_list = NULL;
static int num_sql_list = 0;

int
sql_list_make (char *list_file)
{
  FILE *fp = NULL;
  char *linebuf;
  T_STRING *sql_buf = NULL;
  T_STRING *linebuf_tstr = NULL;
  int lineno = 1;
  char sql_tag[LINE_BUF_SIZE];

  sql_buf = t_string_make (1);
  linebuf_tstr = t_string_make (1000);
  if (sql_buf == NULL || linebuf_tstr == NULL)
    {
      fprintf (stderr, "malloc error\n");
      goto error;
    }

  fp = fopen (list_file, "r");
  if (fp == NULL)
    {
      fprintf (stderr, "%s:%s\n", list_file, strerror (errno));
      goto error;
    }

  if (ut_get_line (fp, linebuf_tstr, &linebuf, &lineno) <= 0)
    goto error;
  if (!IS_NEXT_QUERY (linebuf))
    {
      fprintf (stderr, "%s,%d:file format error\n", list_file, lineno);
      goto error;
    }

  while (1)
    {
      if (ut_get_line (fp, linebuf_tstr, &linebuf, &lineno) <= 0)
	break;

      if (linebuf[strlen (linebuf) - 1] == '\n')
	linebuf[strlen (linebuf) - 1] = '\0';

      strcpy (sql_tag, linebuf);

      t_string_clear (sql_buf);
      while (1)
	{
	  if (ut_get_line (fp, linebuf_tstr, &linebuf, &lineno) <= 0)
	    break;

	  if (IS_NEXT_QUERY (linebuf))
	    {
	      break;
	    }

	  if (t_string_add (sql_buf, linebuf, (int) strlen (linebuf)) < 0)
	    {
	      fprintf (stderr, "malloc error\n");
	      goto error;
	    }
	}

      sql_change_comp_form (t_string_str (sql_buf), t_string_str (sql_buf));

      if (sql_info_add (t_string_str (sql_buf), sql_tag) < 0)
	{
	  goto error;
	}
    }

  fclose (fp);

  qsort (sql_list, num_sql_list, sizeof (T_SQL_INFO), comp_func);

  t_string_free (sql_buf);
  t_string_free (linebuf_tstr);
  return 0;

error:
  t_string_free (sql_buf);
  t_string_free (linebuf_tstr);
  if (fp)
    fclose (fp);
  return -1;
}

int
sql_info_write (char *src_sql, char *q_name, FILE * fp)
{
  int i;
  char *sql;
  T_SQL_INFO tmp_sql_info;
  T_SQL_INFO *search_p;

  if (sql_list == NULL)
    return 0;

  sql = strdup (src_sql);
  if (sql == NULL)
    {
      fprintf (stderr, "sql_info_write():%s\n", strerror (errno));
      return 0;
    }
  sql_change_comp_form (sql, sql);

  tmp_sql_info.sql = sql;

  search_p = (T_SQL_INFO *) bsearch (&tmp_sql_info, sql_list, num_sql_list, sizeof (T_SQL_INFO), comp_func);

  FREE_MEM (sql);

  if (search_p == NULL)
    return 0;

  for (i = 0; i < search_p->num_file; i++)
    {
      fprintf (fp, "%s\n", search_p->filename[i]);
    }

  return 1;
}

static int
sql_info_add (const char *sql, char *sql_tag)
{
  int si_idx = -1;
  int i;

  for (i = 0; i < num_sql_list; i++)
    {
      if (strcmp (sql_list[i].sql, sql) == 0)
	{
	  si_idx = i;
	  break;
	}
    }

  if (si_idx == -1)
    {
      sql_list = (T_SQL_INFO *) REALLOC (sql_list, sizeof (T_SQL_INFO) * (num_sql_list + 1));
      if (sql_list == NULL)
	{
	  fprintf (stderr, "%s\n", strerror (errno));
	  return -1;
	}

      si_idx = num_sql_list;
      sql_info_init (&sql_list[si_idx]);
      sql_list[si_idx].sql = strdup (sql);
      if (sql_list[si_idx].sql == NULL)
	{
	  fprintf (stderr, "%s\n", strerror (errno));
	  return -1;
	}
      num_sql_list++;
    }

  sql_list[si_idx].filename =
    (char **) REALLOC (sql_list[si_idx].filename, sizeof (char *) * (sql_list[si_idx].num_file + 1));
  if (sql_list[si_idx].filename == NULL)
    {
      fprintf (stderr, "%s\n", strerror (errno));
      return -1;
    }

  sql_list[si_idx].filename[sql_list[si_idx].num_file] = strdup (sql_tag);
  if (sql_list[si_idx].filename[sql_list[si_idx].num_file] == NULL)
    {
      fprintf (stderr, "%s\n", strerror (errno));
      return -1;
    }
  sql_list[si_idx].num_file++;
  return 0;
}

static void
sql_info_init (T_SQL_INFO * sql_info)
{
  memset (sql_info, 0, sizeof (T_SQL_INFO));
}

static int
comp_func (const void *arg1, const void *arg2)
{
  return (strcmp (((T_SQL_INFO *) arg1)->sql, ((T_SQL_INFO *) arg2)->sql));
}

static void
sql_change_comp_form (char *src, char *dest)
{
  int write_space_flag = 0;
  char *p, *q;

  for (p = src, q = dest; *p; p++)
    {
      if (*p == '\r' || *p == '\n' || *p == '\t' || *p == ' ')
	{
	  if (write_space_flag)
	    {
	      *q++ = ' ';
	      write_space_flag = 0;
	    }
	}
      else
	{
	  write_space_flag = 1;
	  *q++ = *p;
	}
    }
  *q = '\0';

  ut_trim (dest);
}
