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
 * broker_log_top_tran.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "cas_common.h"
#include "broker_log_top.h"
#include "log_top_string.h"
#include "broker_log_util.h"

#define TRAN_LOG_MAX_COUNT	1000
#define LOG_TOP_RES_FILE_TRAN	"log_top.t"

typedef struct t_log_info T_LOG_INFO;
struct t_log_info
{
  float runtime;
  char *logstr;
};

static int log_top (FILE * fp, char *filename, long start_offset,
		    long end_offset);
static int info_delete (T_LOG_INFO * info);
static void print_result (void);
static int info_add (T_LOG_INFO * info, char *start_date, char *end_date);

static int info_arr_size = 0;
static int info_arr_max_size = TRAN_LOG_MAX_COUNT;
static T_LOG_INFO info_arr[TRAN_LOG_MAX_COUNT + 1];

int
log_top_tran (int argc, char *argv[], int arg_start)
{
  int i;
  char *filename;
  FILE *fp;
  long start_offset, end_offset;

  info_arr_size = 0;

  for (i = arg_start; i < argc; i++)
    {
      filename = argv[i];
      fprintf (stdout, "%s\n", filename);

      fp = fopen (filename, "r");
      if (fp == NULL)
	{
	  fprintf (stderr, "%s[%s]\n", strerror (errno), filename);
	  return -1;
	}
      if (get_file_offset (filename, &start_offset, &end_offset) < 0)
	{
	  start_offset = end_offset = -1;
	}

      log_top (fp, filename, start_offset, end_offset);
      fclose (fp);
    }

  print_result ();

  return 0;
}

static int
log_top (FILE * fp, char *filename, long start_offset, long end_offset)
{
  char *linebuf;
  T_STRING *str_buf = NULL;
  T_STRING *linebuf_tstr = NULL;
  int lineno = 0;
  int is_first = 1;
  char fileinfo_str[1024];
  char cur_date[DATE_STR_LEN + 1];
  char start_date[DATE_STR_LEN + 1];

  start_date[0] = '\0';

  str_buf = t_string_make (1);
  linebuf_tstr = t_string_make (1000);
  if (str_buf == NULL || linebuf_tstr == NULL)
    {
      fprintf (stderr, "malloc error\n");
      goto error;
    }

  if (start_offset != -1)
    {
      fseek (fp, start_offset, SEEK_SET);
    }

  while (1)
    {
      if (end_offset != -1)
	{
	  if (ftell (fp) > end_offset)
	    {
	      break;
	    }
	}

      if (ut_get_line (fp, linebuf_tstr, &linebuf, &lineno) <= 0)
	break;

      if (IS_CAS_LOG_CMD (linebuf))
	{
	  if (strncmp (linebuf + 23, "END OF LOG", 10) == 0)
	    {
	      break;
	    }

	  GET_CUR_DATE_STR (cur_date, linebuf);
	  if (start_date[0] == '\0')
	    strcpy (start_date, cur_date);
	}

      if (is_first)
	{
	  if (linebuf[0] != '\n')
	    {
	      sprintf (fileinfo_str, "%s:%d\n", filename, lineno);
	      t_string_add (str_buf, fileinfo_str,
			    (int) strlen (fileinfo_str));
	      is_first = 0;
	    }
	}

      t_string_add (str_buf, linebuf, (int) strlen (linebuf));

      if (IS_CAS_LOG_CMD (linebuf) && (strncmp (linebuf + 23, "***", 3) == 0))
	{
	  float runtime;

	  if (sscanf (linebuf + 27, "%*s %*s %f", &runtime) == 1)
	    {
	      T_LOG_INFO tmpinfo;
	      char *log_str;
	      log_str = strdup (t_string_str (str_buf));
	      if (log_str == NULL)
		{
		  perror ("strdup");
		  goto error;
		}
	      tmpinfo.runtime = runtime;
	      tmpinfo.logstr = log_str;
	      info_add (&tmpinfo, start_date, cur_date);
	    }
	  t_string_clear (str_buf);
	  is_first = 1;
	  start_date[0] = '\0';
	}
    }

  t_string_free (str_buf);
  t_string_free (linebuf_tstr);

  return 0;

error:
  t_string_free (str_buf);
  t_string_free (linebuf_tstr);
  return -1;
}

static void
print_result (void)
{
  T_LOG_INFO temp[TRAN_LOG_MAX_COUNT + 1];
  int i;
  FILE *fp_t;

  fp_t = fopen (LOG_TOP_RES_FILE_TRAN, "w");
  if (fp_t == NULL)
    {
      fprintf (stderr, "%s\n", strerror (errno));
      return;
    }

  for (i = 0;; i++)
    {
      if (info_delete (&temp[i]) < 0)
	break;
    }

  for (i--; i >= 0; i--)
    {
#if 0
      fprintf (fp_t,
	       "-----------------------------------------------------\n");
#endif
      fprintf (fp_t, "%s\n", temp[i].logstr);
      FREE_MEM (temp[i].logstr);
    }

  fclose (fp_t);
}

static int
info_add (T_LOG_INFO * info, char *start_date, char *end_date)
{
  int i;
  T_LOG_INFO temp;

  if (check_log_time (start_date, end_date) < 0)
    return 0;

  if (info_arr_size == info_arr_max_size)
    {
      if (info_arr[1].runtime > info->runtime)
	{
	  FREE_MEM (info->logstr);
	  return 0;
	}
      info_delete (&temp);
      FREE_MEM (temp.logstr);
    }

  i = ++info_arr_size;
  while ((i != 1) && (info->runtime < info_arr[i / 2].runtime))
    {
      info_arr[i] = info_arr[i / 2];
      i /= 2;
    }
  info_arr[i] = *info;
  return 0;
}

static int
info_delete (T_LOG_INFO * info)
{
  T_LOG_INFO temp;
  int parent, child;

  if (info_arr_size <= 0)
    return -1;

  if (info)
    *info = info_arr[1];

  temp = info_arr[info_arr_size--];
  parent = 1;
  child = 2;
  while (child <= info_arr_size)
    {
      if ((child < info_arr_size)
	  && (info_arr[child].runtime > info_arr[child + 1].runtime))
	child++;
      if (temp.runtime < info_arr[child].runtime)
	break;
      info_arr[parent] = info_arr[child];
      parent = child;
      child *= 2;
    }
  info_arr[parent] = temp;
  return 0;
}
