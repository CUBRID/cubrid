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
 * broker_log_top.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#if !defined(WINDOWS)
#include <unistd.h>
#endif

#ifdef MT_MODE
#include <pthread.h>
#endif

#include "cubrid_getopt.h"
#include "cas_common.h"
#include "cas_query_info.h"
#include "broker_log_time.h"
#include "broker_log_sql_list.h"
#include "log_top_string.h"
#include "broker_log_top.h"
#include "broker_log_util.h"

#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

#define MAX_SRV_HANDLE		3000
#define CLIENT_MSG_BUF_SIZE	1024
#define CONNECT_MSG_BUF_SIZE	1024

#ifdef MT_MODE
typedef struct t_work_msg T_WORK_MSG;
struct t_work_msg
{
  FILE *fp;
  char *filename;
};
#endif

static int log_top_query (int argc, char *argv[], int arg_start);
static int log_top (FILE * fp, char *filename, long start_offset, long end_offset);
static int log_execute (T_QUERY_INFO * qi, char *linebuf, char **query_p);
static int get_args (int argc, char *argv[]);
#if defined(WINDOWS)
static int get_file_count (int argc, char *argv[], int arg_start);
static int get_file_list (char *list[], int size, int argc, char *argv[], int arg_start);
static char **alloc_file_list (int size);
static void free_file_list (char **list, int size);
#endif
#ifdef MT_MODE
static void *thr_main (void *arg);
#endif
static int read_multi_line_sql (FILE * fp, T_STRING * t_str, char **linebuf, int *lineno, T_STRING * sql_buf,
				T_STRING * cas_log_buf);
static int read_execute_end_msg (char *msg_p, int *res_code, int *runtime_msec);
static int read_bind_value (FILE * fp, T_STRING * t_str, char **linebuf, int *lineno, T_STRING * cas_log_buf);
static int search_offset (FILE * fp, char *string, long *offset, bool start);
static char *organize_query_string (const char *sql);

T_LOG_TOP_MODE log_top_mode = MODE_PROC_TIME;

static char *sql_info_file = NULL;
static int mode_max_handle_lower_bound;
static char mode_tran = 0;
static char from_date[128] = "";
static char to_date[128] = "";

#ifdef MT_MODE
static int num_thread = 5;
static int process_flag = 1;
static T_WORK_MSG *work_msg;
#endif
int
main (int argc, char *argv[])
{
  int arg_start;
  int error = 0;
#if defined(WINDOWS)
  int file_cnt = -1;
  int get_cnt = 0;
  char **file_list = NULL;
#endif


  arg_start = get_args (argc, argv);
  if (arg_start < 0)
    {
      return -1;
    }

#if defined(WINDOWS)
  file_cnt = get_file_count (argc, argv, arg_start);
  if (file_cnt <= 0)
    {
      return -1;
    }

  file_list = alloc_file_list (file_cnt);
  if (file_list == NULL)
    {
      return -1;
    }

  get_cnt = get_file_list (file_list, file_cnt, argc, argv, arg_start);
  if (get_cnt > file_cnt)
    {
      get_cnt = file_cnt;
    }

  if (mode_tran)
    {
      error = log_top_tran (get_cnt, file_list, 0);
    }
  else
    {
      error = log_top_query (get_cnt, file_list, 0);
    }

  free_file_list (file_list, file_cnt);
#else
  if (mode_tran)
    {
      error = log_top_tran (argc, argv, arg_start);
    }
  else
    {
      error = log_top_query (argc, argv, arg_start);
    }
#endif

  return error;
}

#if defined(WINDOWS)
int
get_file_count (int argc, char *argv[], int arg_start)
{
  int i;
  int count = 0;
  HANDLE handle;
  WIN32_FIND_DATA find_data;

  for (i = arg_start; i < argc; i++)
    {
      handle = FindFirstFile (argv[i], &find_data);
      if (handle == INVALID_HANDLE_VALUE)
	{
	  fprintf (stderr, "No such file or directory[%s]\n", argv[i]);
	  return -1;
	}
      do
	{
	  /* skip directory */
	  if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
	    {
	      count++;
	    }
	}
      while (FindNextFile (handle, &find_data));

      FindClose (handle);
    }

  return count;
}

int
get_file_list (char *list[], int size, int argc, char *argv[], int arg_start)
{
  int i;
  int index = 0;
  HANDLE handle;
  WIN32_FIND_DATA find_data;
  char *slash_pos, *pos1, *pos2;
  char prefix[MAX_PATH] = { 0 };

  assert (list != NULL);

  for (i = arg_start; i < argc; i++)
    {
      handle = FindFirstFile (argv[i], &find_data);
      if (handle == INVALID_HANDLE_VALUE)
	{
	  continue;
	}

      /* find the prefix of the matched file */
      pos1 = strrchr (argv[i], '\\');
      pos2 = strrchr (argv[i], '/');
      slash_pos = MAX (pos1, pos2);
      if (slash_pos != NULL)
	{
	  strncpy (prefix, argv[i], MAX_PATH);
	  prefix[slash_pos - argv[i] + 1] = '\0';
	}

      do
	{
	  /* skip directory */
	  if (index < size && !(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
	    {
	      assert (list[index] != NULL);
	      if (slash_pos != NULL)
		{
		  snprintf (list[index], MAX_PATH, "%s%s", prefix, find_data.cFileName);
		}
	      else
		{
		  strncpy (list[index], find_data.cFileName, MAX_PATH);
		}
	      index++;
	    }
	}
      while (FindNextFile (handle, &find_data));

      FindClose (handle);
    }

  return index;
}

char **
alloc_file_list (int size)
{
  int i, j;
  char **file_list = NULL;

  assert (size > 0);

  file_list = (char **) MALLOC (sizeof (char *) * size);
  if (file_list == NULL)
    {
      fprintf (stderr, "fail memory allocation\n");
      return NULL;
    }

  for (i = 0; i < size; i++)
    {
      file_list[i] = (char *) MALLOC (MAX_PATH);

      if (file_list[i] == NULL)
	{
	  fprintf (stderr, "fail memory allocation\n");
	  for (j = 0; j < i; j++)
	    {
	      FREE_MEM (file_list[j]);
	    }
	  FREE_MEM (file_list);
	  return NULL;
	}
    }

  return file_list;
}

void
free_file_list (char **list, int size)
{
  int i;
  assert (list != NULL);

  for (i = 0; i < size; i++)
    {
      if (list[i] == NULL)
	{
	  break;
	}
      FREE_MEM (list[i]);
    }
  FREE_MEM (list);
}
#endif

int
get_file_offset (char *filename, long *start_offset, long *end_offset)
{
  FILE *fp;

  if (!start_offset || !end_offset)
    {
      return -1;
    }

  fp = fopen (filename, "r");
  if (fp == NULL)
    {
      return -1;
    }

  if (from_date[0] == '\0' || search_offset (fp, from_date, start_offset, true) < 0)
    {
      *start_offset = -1;
    }

  if (to_date[0] == '\0' || search_offset (fp, to_date, end_offset, false) < 0)
    {
      *end_offset = -1;
    }

  fclose (fp);
  return 0;
}

int
check_log_time (char *start_date, char *end_date)
{
  if (from_date[0])
    {
      if (strncmp (end_date, from_date, DATE_STR_LEN) < 0)
	return -1;
    }
  if (to_date[0])
    {
      if (strncmp (to_date, start_date, DATE_STR_LEN) < 0)
	return -1;
    }

  return 0;
}

static int
log_top_query (int argc, char *argv[], int arg_start)
{
  FILE *fp;
  char *filename;
  int i;
  int error = 0;
  long start_offset, end_offset;
#ifdef MT_MODE
  T_THREAD thrid;
  int j;
#endif

#ifdef MT_MODE
  query_info_mutex_init ();
#endif

#ifdef MT_MODE
  work_msg = MALLOC (sizeof (T_WORK_MSG) * num_thread);
  if (work_msg == NULL)
    {
      fprintf (stderr, "malloc error\n");
      return -1;
    }
  memset (work_msg, 0, sizeof (T_WORK_MSG *) * num_thread);

  for (i = 0; i < num_thread; i++)
    THREAD_BEGIN (thrid, thr_main, (void *) i);
#endif

  for (i = arg_start; i < argc; i++)
    {
      filename = argv[i];
      fprintf (stdout, "%s\n", filename);

#if defined(WINDOWS)
      fp = fopen (filename, "rb");
#else
      fp = fopen (filename, "r");
#endif
      if (fp == NULL)
	{
	  fprintf (stderr, "%s[%s]\n", strerror (errno), filename);
#ifdef MT_MODE
	  process_flag = 0;
#endif
	  return -1;
	}

      if (get_file_offset (filename, &start_offset, &end_offset) < 0)
	{
	  start_offset = end_offset = -1;
	}

#ifdef MT_MODE
      while (1)
	{
	  for (j = 0; j < num_thread; j++)
	    {
	      if (work_msg[j].filename == NULL)
		{
		  work_msg[j].fp = fp;
		  work_msg[j].filename = filename;
		  break;
		}
	    }
	  if (j == num_thread)
	    SLEEP_MILISEC (1, 0);
	  else
	    break;
	}
#else
      error = log_top (fp, filename, start_offset, end_offset);
      fclose (fp);
      if (error == LT_INVAILD_VERSION)
	{
	  return error;
	}
#endif
    }

#ifdef MT_MODE
  process_flag = 0;
#endif

  if (sql_info_file != NULL)
    {
      fprintf (stdout, "read sql info file...\n");
      if (sql_list_make (sql_info_file) < 0)
	{
	  return -1;
	}
    }

  fprintf (stdout, "print results...\n");
  query_info_print ();

  return 0;
}

#ifdef MT_MODE
static void *
thr_main (void *arg)
{
  int self_index = (int) arg;

  while (process_flag)
    {
      if (work_msg[self_index].filename == NULL)
	{
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  log_top (work_msg[self_index].fp, work_msg[self_index].filename);
	  fclose (work_msg[self_index].fp);
	  work_msg[self_index].fp = NULL;
	  work_msg[self_index].filename = NULL;
	}
    }
  return NULL;
}
#endif

static int
log_top (FILE * fp, char *filename, long start_offset, long end_offset)
{
  char *linebuf = NULL;
  T_QUERY_INFO query_info_buf[MAX_SRV_HANDLE];
  char client_msg_buf[CLIENT_MSG_BUF_SIZE];
  char connect_msg_buf[CONNECT_MSG_BUF_SIZE];
  T_STRING *cas_log_buf = NULL;
  T_STRING *sql_buf = NULL;
  T_STRING *linebuf_tstr = NULL;
  char prepare_buf[128];
  int i;
  char *msg_p;
  int lineno = 0;
  int log_type = 0;
  char read_flag = 1;
  char cur_date[DATE_STR_LEN + 1];
  char start_date[DATE_STR_LEN + 1];
  start_date[0] = '\0';

  for (i = 0; i < MAX_SRV_HANDLE; i++)
    {
      query_info_init (&query_info_buf[i]);
    }

  cas_log_buf = t_string_make (1);

  sql_buf = t_string_make (1);
  linebuf_tstr = t_string_make (1000);
  if (cas_log_buf == NULL || sql_buf == NULL || linebuf_tstr == NULL)
    {
      fprintf (stderr, "malloc error\n");
      goto log_top_err;
    }

  memset (client_msg_buf, 0, sizeof (client_msg_buf));
  memset (connect_msg_buf, 0, sizeof (connect_msg_buf));
  t_string_clear (cas_log_buf);
  t_string_clear (sql_buf);
  memset (prepare_buf, 0, sizeof (prepare_buf));

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

      if (read_flag)
	{
	  if (ut_get_line (fp, linebuf_tstr, &linebuf, &lineno) <= 0)
	    {
	      break;
	    }
	}
      read_flag = 1;

      log_type = is_cas_log (linebuf);
      if (log_type == CAS_LOG_BEGIN_WITH_MONTH)
	{
	  fprintf (stderr, "invaild version of log file\n");
	  t_string_free (cas_log_buf);
	  t_string_free (sql_buf);
	  t_string_free (linebuf_tstr);
	  return LT_INVAILD_VERSION;
	}
      else if (log_type != CAS_LOG_BEGIN_WITH_YEAR)
	{
	  continue;
	}

      if (strncmp (linebuf + 23, "END OF LOG", 10) == 0)
	{
	  break;
	}

      GET_CUR_DATE_STR (cur_date, linebuf);
      if (start_date[0] == '\0')
	{
	  strcpy (start_date, cur_date);
	}

      msg_p = get_msg_start_ptr (linebuf);
      if (strncmp (msg_p, "execute", 7) == 0 || strncmp (msg_p, "execute_all", 11) == 0
	  || strncmp (msg_p, "execute_call", 12) == 0 || strncmp (msg_p, "execute_batch", 13) == 0)
	{
	  int qi_idx;
	  char *query_p;
	  int end_block_flag = 0;

	  /* 
	   * execute log format:
	   * <execute_cmd> srv_h_id <handle_id> <query_string>
	   * bind <bind_index> : <TYPE> <VALUE>
	   * <execute_cmd> [error:]<res> tuple <tuple_count> time <runtime_msec>
	   * <execute_cmd>:
	   *      execute, execute_all or execute_call
	   *
	   * ex)
	   * execute srv_h_id 1 select 'a' from db_root
	   * bind 1 : VARCHAR test str
	   * execute 0 tuple 1 time 0.004
	   */
	  qi_idx = log_execute (query_info_buf, linebuf, &query_p);
	  if (qi_idx < 0 || query_p == NULL)
	    goto log_top_err;

	  t_string_clear (sql_buf);
	  t_string_clear (cas_log_buf);

	  t_string_add (sql_buf, query_p, strlen (query_p));
	  t_string_add (cas_log_buf, linebuf, strlen (linebuf));

	  if (read_multi_line_sql (fp, linebuf_tstr, &linebuf, &lineno, sql_buf, cas_log_buf) < 0)
	    {
	      break;
	    }
	  if (read_bind_value (fp, linebuf_tstr, &linebuf, &lineno, cas_log_buf) < 0)
	    {
	      break;
	    }

	  msg_p = get_msg_start_ptr (linebuf);

	  /* skip query_cancel */
	  if (strncmp (msg_p, "query_cancel", 12) == 0)
	    {
	      if (ut_get_line (fp, linebuf_tstr, &linebuf, &lineno) <= 0)
		{
		  break;
		}
	    }

	  if (strncmp (msg_p, "execute", 7) != 0)
	    {
	      while (1)
		{
		  if (ut_get_line (fp, linebuf_tstr, &linebuf, &lineno) <= 0)
		    {
		      break;
		    }

		  msg_p = get_msg_start_ptr (linebuf);
		  if (strncmp (msg_p, "***", 3) == 0)
		    {
		      end_block_flag = 1;
		      if (ut_get_line (fp, linebuf_tstr, &linebuf, &lineno) <= 0)
			{
			  /* ut_get_line error, just break; */
			  break;
			}
		      break;
		    }
		}
	    }

	  if (end_block_flag == 1)
	    {
	      continue;
	    }

	  query_info_buf[qi_idx].sql = (char *) REALLOC (query_info_buf[qi_idx].sql, t_string_len (sql_buf) + 1);

	  strcpy (query_info_buf[qi_idx].sql, ut_trim (t_string_str (sql_buf)));
	  query_info_buf[qi_idx].organized_sql = organize_query_string (query_info_buf[qi_idx].sql);

	  msg_p = get_msg_start_ptr (linebuf);
	  GET_CUR_DATE_STR (cur_date, linebuf);

	  strcpy (query_info_buf[qi_idx].start_date, start_date);

	  if (log_top_mode == MODE_MAX_HANDLE)
	    {
	      if (qi_idx >= mode_max_handle_lower_bound)
		{
		  if (query_info_add (&query_info_buf[qi_idx], qi_idx + 1, 0, filename, lineno, cur_date) < 0)
		    {
		      goto log_top_err;
		    }
		}
	    }
	  else
	    {
	      int execute_res, runtime;

	      /* set cas_log to query info */
	      if (t_string_add (cas_log_buf, linebuf, strlen (linebuf)) < 0)
		{
		  goto log_top_err;
		}

	      query_info_buf[qi_idx].cas_log =
		(char *) REALLOC (query_info_buf[qi_idx].cas_log, t_string_len (cas_log_buf) + 1);

	      memcpy (query_info_buf[qi_idx].cas_log, t_string_str (cas_log_buf), t_string_len (cas_log_buf));

	      query_info_buf[qi_idx].cas_log_len = t_string_len (cas_log_buf);


	      /* read execute info & if fail add to query_info_arr_ne */
	      if (read_execute_end_msg (msg_p, &execute_res, &runtime) < 0)
		{
		  if (query_info_add_ne (&query_info_buf[qi_idx], cur_date) < 0)
		    {
		      goto log_top_err;
		    }

		  read_flag = 0;
		  continue;
		}

	      /* add to query_info_arr */
	      if (query_info_add (&query_info_buf[qi_idx], runtime, execute_res, filename, lineno, cur_date) < 0)
		{
		  goto log_top_err;
		}
	    }
	}
      start_date[0] = '\0';
    }

  for (i = 0; i < MAX_SRV_HANDLE; i++)
    {
      query_info_clear (&query_info_buf[i]);
    }

  t_string_free (cas_log_buf);
  t_string_free (sql_buf);
  t_string_free (linebuf_tstr);
  return LT_NO_ERROR;

log_top_err:
  t_string_free (cas_log_buf);
  t_string_free (sql_buf);
  t_string_free (linebuf_tstr);
  return LT_OTHER_ERROR;
}

static int
log_execute (T_QUERY_INFO * qi, char *linebuf, char **query_p)
{
  char *p;
  int exec_h_id;

  p = strstr (linebuf, "srv_h_id ");
  if (p == NULL)
    {
      fprintf (stderr, "log error[%s]\n", linebuf);
      return -1;
    }
  exec_h_id = atoi (p + 9);
  *query_p = strchr (p + 9, ' ');
  if (*query_p)
    *query_p = *query_p + 1;

  if (exec_h_id <= 0 || exec_h_id > MAX_SRV_HANDLE)
    {
      fprintf (stderr, "log error. exec id = %d\n", exec_h_id);
      return -1;
    }
  exec_h_id--;

  return exec_h_id;
}

static int
get_args (int argc, char *argv[])
{
  int c;

  while ((c = getopt (argc, argv, "tq:h:F:T:")) != EOF)
    {
      switch (c)
	{
	case 't':
	  mode_tran = 1;
	  break;
	case 'q':
	  sql_info_file = optarg;
	  break;
	case 'h':
	  mode_max_handle_lower_bound = atoi (optarg);
	  break;
	case 'F':
	  if (str_to_log_date_format (optarg, from_date) < 0)
	    {
	      goto date_format_err;
	    }
	  break;
	case 'T':
	  if (str_to_log_date_format (optarg, to_date) < 0)
	    {
	      goto date_format_err;
	    }
	  break;
	default:
	  goto getargs_err;
	}
    }

  if (mode_max_handle_lower_bound > 0)
    log_top_mode = MODE_MAX_HANDLE;

  if (optind < argc)
    return optind;

getargs_err:
  fprintf (stderr, "%s [-t] [-F <from date>] [-T <to date>] <log_file> ...\n", argv[0]);
  return -1;
date_format_err:
  fprintf (stderr, "invalid date. valid date format is yy-mm-dd hh:mm:ss.\n");
  return -1;
}

static int
read_multi_line_sql (FILE * fp, T_STRING * t_str, char **linebuf, int *lineno, T_STRING * sql_buf,
		     T_STRING * cas_log_buf)
{
  while (1)
    {
      if (ut_get_line (fp, t_str, linebuf, lineno) <= 0)
	{
	  return -1;
	}

      if (is_cas_log (*linebuf) == CAS_LOG_BEGIN_WITH_YEAR)
	{
	  return 0;
	}

      if (t_string_add (sql_buf, *linebuf, strlen (*linebuf)) < 0)
	{
	  fprintf (stderr, "malloc error\n");
	  return -1;
	}
      if (t_string_add (cas_log_buf, *linebuf, strlen (*linebuf)) < 0)
	{
	  fprintf (stderr, "malloc error\n");
	  return -1;
	}
    }
}

static int
read_bind_value (FILE * fp, T_STRING * t_str, char **linebuf, int *lineno, T_STRING * cas_log_buf)
{
  char *msg_p;
  char is_bind_value;
  int linebuf_len;

  do
    {
      is_bind_value = 0;

      if (is_cas_log (*linebuf) == CAS_LOG_BEGIN_WITH_YEAR)
	{
	  msg_p = get_msg_start_ptr (*linebuf);
	  if (strncmp (msg_p, "bind ", 5) == 0)
	    is_bind_value = 1;
	}
      else
	{
	  is_bind_value = 1;
	}
      if (is_bind_value)
	{
	  linebuf_len = t_string_len (t_str);
	  if (t_string_add (cas_log_buf, *linebuf, linebuf_len) < 0)
	    {
	      return -1;
	    }
	}
      else
	{
	  return 0;
	}

      if (ut_get_line (fp, t_str, linebuf, lineno) <= 0)
	{
	  return -1;
	}
    }
  while (1);
}

static int
read_execute_end_msg (char *msg_p, int *res_code, int *runtime_msec)
{
  char *p, *next_p;
  int sec, msec;
  int tuple_count;
  int result = 0;
  int val;

  p = strchr (msg_p, ' ');
  if (p == NULL)
    {
      return -1;
    }
  p++;
  if (strncmp (p, "error:", 6) == 0)
    {
      p += 6;
    }

  result = str_to_int32 (&val, &next_p, p, 10);
  if (result != 0)
    {
      return -1;
    }
  *res_code = val;

  p = next_p + 1;
  if (strncmp (p, "tuple ", 6) != 0)
    {
      return -1;
    }

  p += 6;

  result = str_to_int32 (&val, &next_p, p, 10);
  if (result != 0)
    {
      return -1;
    }
  tuple_count = val;

  p = next_p + 1;
  if (strncmp (p, "time ", 5) != 0)
    {
      return -1;
    }
  p += 5;

  sscanf (p, "%d.%d", &sec, &msec);
  *runtime_msec = sec * 1000 + msec;

  return 0;
}

static int
search_offset (FILE * fp, char *string, long *offset, bool start)
{
  off_t start_ptr = 0;
  off_t end_ptr = 0;
  off_t cur_ptr;
  off_t old_start_ptr = 0;
  bool old_start_saved = false;
  long tmp_offset = -1;
  struct stat stat_buf;
  char *linebuf = NULL;
  int line_no = 0;
  T_STRING *linebuf_tstr = NULL;
  int ret_val;

  assert (offset != NULL);

  *offset = -1;

  if (fstat (fileno (fp), &stat_buf) < 0)
    {
      return -1;
    }

  end_ptr = stat_buf.st_size;

  linebuf_tstr = t_string_make (1000);
  if (linebuf_tstr == NULL)
    {
      return -1;
    }

  cur_ptr = 0;

  while (true)
    {
      if (fseek (fp, cur_ptr, SEEK_SET) < 0)
	{
	  goto error;
	}

      while (ut_get_line (fp, linebuf_tstr, &linebuf, &line_no) > 0)
	{
	  if (is_cas_log (linebuf) == CAS_LOG_BEGIN_WITH_YEAR)
	    {
	      break;
	    }
	  cur_ptr = ftell (fp);

	  if (cur_ptr >= end_ptr)
	    {
	      tmp_offset = old_start_saved ? old_start_ptr : start_ptr;
	      goto end_loop;
	    }
	}

      ret_val = strncmp (linebuf, string, DATE_STR_LEN);

      if (ret_val < 0)
	{
	  old_start_saved = true;
	  old_start_ptr = start_ptr;
	  start_ptr = ftell (fp);
	}

      if (ret_val >= 0)
	{
	  if (ret_val == 0 && old_start_saved)
	    {
	      tmp_offset = start_ptr;
	      goto end_loop;
	    }
	  else
	    {
	      old_start_saved = false;
	      end_ptr = cur_ptr;
	    }
	}

      cur_ptr = start_ptr + (end_ptr - start_ptr) / 2;
      if (cur_ptr <= start_ptr)
	{
	  tmp_offset = start_ptr;
	  goto end_loop;
	}
    }

end_loop:
  if (fseek (fp, tmp_offset, SEEK_SET) < 0)
    {
      goto error;
    }

  while (ut_get_line (fp, linebuf_tstr, &linebuf, &line_no) > 0)
    {
      if (start)
	{
	  /* the first line of the time */
	  if (strncmp (linebuf, string, DATE_STR_LEN) >= 0)
	    {
	      break;
	    }
	}
      else
	{
	  /* the last line of the time */
	  if (strncmp (linebuf, string, DATE_STR_LEN) > 0)
	    {
	      break;
	    }
	}
      tmp_offset = ftell (fp);
    }

  *offset = tmp_offset;
  t_string_free (linebuf_tstr);
  return 0;

error:
  t_string_free (linebuf_tstr);
  return -1;
}

static char *
organize_query_string (const char *sql)
{
  typedef enum
  {
    SQL_TOKEN_NONE = 0,
    SQL_TOKEN_DOUBLE_QUOTE,
    SQL_TOKEN_SINGLE_QUOTE,
    SQL_TOKEN_SQL_COMMENT,
    SQL_TOKEN_C_COMMENT,
    SQL_TOKEN_CPP_COMMENT
  } SQL_TOKEN;

  SQL_TOKEN token = SQL_TOKEN_NONE;
  int token_len = 0;
  char *p = NULL;
  const char *q = NULL;
  char *organized_sql = NULL;
  bool need_copy_token = true;

  organized_sql = (char *) malloc (strlen (sql) + 1);
  if (organized_sql == NULL)
    {
      return NULL;
    }

  p = organized_sql;
  q = sql;

  while (*q != '\0')
    {
      need_copy_token = true;
      token_len = 1;

      if (token == SQL_TOKEN_NONE)
	{
	  if (*q == '\'' && (q == sql || *(q - 1) != '\\'))
	    {
	      token = SQL_TOKEN_SINGLE_QUOTE;
	    }
	  else if (*q == '"' && (q == sql || *(q - 1) != '\\'))
	    {
	      token = SQL_TOKEN_DOUBLE_QUOTE;
	    }
	  else if (*q == '-' && *(q + 1) == '-')
	    {
	      need_copy_token = false;
	      token = SQL_TOKEN_SQL_COMMENT;
	      token_len = 2;
	    }
	  else if (*q == '/' && *(q + 1) == '*')
	    {
	      need_copy_token = false;
	      token = SQL_TOKEN_C_COMMENT;
	      token_len = 2;
	    }
	  else if (*q == '/' && *(q + 1) == '/')
	    {
	      need_copy_token = false;
	      token = SQL_TOKEN_CPP_COMMENT;
	      token_len = 2;
	    }
	}
      else
	{
	  need_copy_token = false;

	  if (token == SQL_TOKEN_SINGLE_QUOTE)
	    {
	      need_copy_token = true;

	      if (*q == '\'' && *(q - 1) != '\\')
		{
		  token = SQL_TOKEN_NONE;
		}
	    }
	  else if (token == SQL_TOKEN_DOUBLE_QUOTE)
	    {
	      need_copy_token = true;

	      if (*q == '"' && *(q - 1) != '\\')
		{
		  token = SQL_TOKEN_NONE;
		}
	    }
	  else if ((token == SQL_TOKEN_SQL_COMMENT || token == SQL_TOKEN_CPP_COMMENT) && *q == '\n')
	    {
	      token = SQL_TOKEN_NONE;
	    }
	  else if (token == SQL_TOKEN_C_COMMENT && *q == '*' && *(q + 1) == '/')
	    {
	      token = SQL_TOKEN_NONE;
	      token_len = 2;
	    }
	}

      if (need_copy_token)
	{
	  memcpy (p, q, token_len);
	  p += token_len;
	}

      q += token_len;
    }

  *p = '\0';

  return organized_sql;
}
