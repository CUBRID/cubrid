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
 * broker_log_top.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

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
#include "broker_config.h"

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
static int get_args (int *argc, char **argv[]);
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
static void free_conf_allocated_argv ();
static int compare_by_brokername (const void *a, const void *b);
static int collect_log_files_from_conf (char ***out_argv);

T_LOG_TOP_MODE log_top_mode = MODE_PROC_TIME;
T_OUTPUT_MODE output_mode = OUTPUT_MERGE;
char from_conf_flag = 0;

char **conf_argv_allocated = NULL;
int conf_argc_allocated = 0;
char **virtual_new_argv = NULL;

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
  int total_files = 0;
  char **target_files = NULL;

  arg_start = get_args (&argc, &argv);
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

  total_files = (get_cnt > file_cnt) ? file_cnt : get_cnt;
  target_files = file_list;
#else
  total_files = argc - arg_start;
  target_files = &argv[arg_start];
#endif

  if (total_files <= 0)
    {
      goto main_finalize;
    }

  qsort (target_files, total_files, sizeof (char *), compare_by_brokername);

  if (mode_tran)
    {
      error = log_top_tran (total_files, target_files, 0);
    }
  else
    {
      error = log_top_query (total_files, target_files, 0);
    }

main_finalize:
#if defined(WINDOWS)
  if (file_list)
    {
      free_file_list (file_list, file_cnt);
    }
#endif

  free_conf_allocated_argv ();

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
  char splitdir[512] = "";
  char prev_prefix[256] = "";
  char curr_prefix[256] = "";
  char org_cwd[PATH_MAX];
  bool is_found_files = false;
#ifdef MT_MODE
  T_THREAD thrid;
  int j;
#endif

  if (getcwd (org_cwd, sizeof (org_cwd)) == NULL)
    {
      return -1;
    }

  if (output_mode == OUTPUT_SPLIT && make_splitdir (splitdir) < 0)
    {
      fprintf (stderr, "cannot make dir (%s).\n", splitdir);
      return -1;
    }

#ifdef MT_MODE
  if (output_mode == OUTPUT_SPLIT)
    {
      num_thread = 1;
    }
  query_info_mutex_init ();

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

      struct stat st;
#if defined(WINDOWS)
      if (stat (filename, &st) == 0 && (st.st_mode & _S_IFMT) == _S_IFDIR)
#else
      if (stat (filename, &st) == 0 && S_ISDIR (st.st_mode))
#endif
	{
	  /* skip if filename is directory */
	  continue;
	}

      if (output_mode == OUTPUT_SPLIT)
	{
#ifdef MT_MODE
	  process_flag = 0;
#endif
	  get_brokername_from_filename (filename, curr_prefix, sizeof (curr_prefix));
	  if (strlen (prev_prefix) > 0 && strcmp (curr_prefix, prev_prefix) != 0)
	    {
	      if (make_change_split_brokerdir (splitdir, prev_prefix) < 0)
		{
		  fprintf (stderr, "cannot make and change dir (%s/%s).\n", splitdir, prev_prefix);
		  return -1;
		}
	      fprintf (stdout, "Report files created: ./%s/%s/log_top.{q,res}\n", splitdir, prev_prefix);
	      query_info_print ();
	      query_info_clear_array ();
	      chdir (org_cwd);
	    }
	  strcpy (prev_prefix, curr_prefix);
	}

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
      else
	{
	  fprintf (stdout, "%s\n", get_basename (filename));
	  is_found_files = true;
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

  if (!is_found_files)
    {
      fprintf (stdout, "Result generation skipped: no analyzed files found.\n");
    }
  else if (output_mode == OUTPUT_SPLIT)
    {
      if (strlen (prev_prefix) > 0)
	{
	  if (make_change_split_brokerdir (splitdir, prev_prefix) < 0)
	    {
	      fprintf (stderr, "cannot make and change dir (%s/%s).\n", splitdir, prev_prefix);
	      return -1;
	    }
	  fprintf (stdout, "Report files created: ./%s/%s/log_top.{q,res}\n", splitdir, prev_prefix);
	  query_info_print ();
	  query_info_clear_array ();
	  chdir (org_cwd);
	}
    }
  else
    {
      fprintf (stdout, "Report files created: ./log_top.{q,res}\n");
      query_info_print ();
      query_info_clear_array ();
    }

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
get_args (int *argc, char **argv[])
{
  int c;
  int option_index = 0;
  int org_argc = *argc;
  char **org_argv = *argv;

  static struct option long_options[] = {
    {"from-conf", no_argument, 0, 'C'},
    {"split", no_argument, 0, 'S'},
    {0, 0, 0, 0}
  };

  if (org_argc < 2)
    {
      goto getargs_err;
    }

  while ((c = getopt_long (org_argc, org_argv, "tq:h:F:T:S", long_options, &option_index)) != EOF)
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
	  if (mode_max_handle_lower_bound > 0)
	    {
	      log_top_mode = MODE_MAX_HANDLE;
	    }
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
	case 'C':
	  if (strcmp (org_argv[optind - 1], "--from-conf") == 0)
	    {
	      from_conf_flag = 1;
	    }
	  else
	    {
	      goto getargs_err;
	    }
	  break;
	case 'S':
	  if (strcmp (org_argv[optind - 1], "--split") == 0 || strcmp (org_argv[optind - 1], "-S") == 0)
	    {
	      output_mode = OUTPUT_SPLIT;
	    }
	  else
	    {
	      goto getargs_err;
	    }
	  break;
	default:
	  goto getargs_err;
	}
    }

  if (!from_conf_flag)
    {
      if (optind < org_argc)
	{
	  return optind;
	}
    }
  else if (optind == org_argc)
    {
      int captured_cnt = collect_log_files_from_conf (&conf_argv_allocated);
      if (captured_cnt < 0)
	{
	  fprintf (stderr, "Unable to fetch log files via conf.");
	  return -1;
	}
      if (captured_cnt == 0)
	{
	  fprintf (stderr, "No log files found.");
	  return -1;
	}

      conf_argc_allocated = captured_cnt;
      int new_argc = optind + captured_cnt;
      char **new_argv = (char **) malloc (sizeof (char *) * (new_argc + 1));
      if (new_argv == NULL)
	{
	  fprintf (stderr, "Failed to allocate memory.");
	  return -1;
	}

      for (int i = 0; i < optind; i++)
	{
	  new_argv[i] = org_argv[i];
	}

      for (int i = 0; i < captured_cnt; i++)
	{
	  new_argv[optind + i] = conf_argv_allocated[i];
	}
      new_argv[new_argc] = NULL;

      virtual_new_argv = new_argv;
      *argc = new_argc;
      *argv = new_argv;

      return optind;
    }

getargs_err:
  fprintf (stderr, "Usage)\n"
	   "%s [-t] [-F <from date>] [-T <to date>] [-S|--split] <log_file> ... \n"
	   "   or\n" "%s [-t] [-F <from date>] [-T <to date>] [-S|--split] --from-conf \n", org_argv[0], org_argv[0]);
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

static void
free_conf_allocated_argv ()
{
  if (conf_argv_allocated)
    {
      for (int i = 0; i < conf_argc_allocated; i++)
	{
	  if (conf_argv_allocated[i])
	    {
	      free (conf_argv_allocated[i]);
	    }
	}
      free (conf_argv_allocated);
      conf_argv_allocated = NULL;
    }
  if (virtual_new_argv)
    {
      free (virtual_new_argv);
      virtual_new_argv = NULL;
    }
}

void
get_brokername_from_filename (const char *filename, char *prefix, int max_len)
{
  const char *basename = get_basename (filename);

  strncpy (prefix, basename, max_len - 1);
  prefix[max_len - 1] = '\0';

  int name_len = strlen (prefix);

  const char *suffixes[] = {
    SUFFIX_SLOW_LOG_BAK,
    SUFFIX_SQL_LOG_BAK,
    SUFFIX_SLOW_LOG,
    SUFFIX_SQL_LOG
  };
  int suffix_cnt = sizeof (suffixes) / sizeof (suffixes[0]);
  int matched_suffix_len = 0;

  for (int i = 0; i < suffix_cnt; i++)
    {
      int s_len = strlen (suffixes[i]);
      if (name_len >= s_len)
	{
	  if (strcmp (&prefix[name_len - s_len], suffixes[i]) == 0)
	    {
	      matched_suffix_len = s_len;
	      break;
	    }
	}
    }

  if (matched_suffix_len > 0 && name_len > matched_suffix_len)
    {
      int search_idx = name_len - matched_suffix_len - 1;
      while (search_idx >= 0 && prefix[search_idx] >= '0' && prefix[search_idx] <= '9')
	{
	  search_idx--;
	}

      if (search_idx >= 0 && prefix[search_idx] == '_')
	{
	  prefix[search_idx] = '\0';
	  return;
	}
    }

  strncpy (prefix, PREFIX_UNKNOWN, max_len - 1);
  prefix[max_len - 1] = '\0';
}

const char *
get_basename (const char *path)
{
  const char *basename = strrchr (path, '/');
#if defined(WINDOWS)
  const char *basename_win = strrchr (path, '\\');

  if (basename == NULL || (basename_win != NULL && basename_win > basename))
    {
      basename = basename_win;
    }
#endif
  return (basename) ? basename + 1 : path;
}

int
make_splitdir (char *splitdir)
{
  time_t now = time (NULL);
  struct tm *t = localtime (&now);

  sprintf (splitdir, "broker_log_top_%02d%02d%02d_%02d%02d",
	   (t->tm_year % 100), t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min);

  if (mkdir (splitdir, 0755) != 0 && errno != EEXIST)
    {
      return -1;
    }

  return 0;
}

int
make_change_split_brokerdir (char *splitdir, char *broker)
{
  char tmpdir[PATH_MAX];

  snprintf (tmpdir, sizeof (tmpdir), "%s/%s", splitdir, broker);

  if (mkdir (tmpdir, 0755) != 0 && errno != EEXIST)
    {
      return -1;
    }

  if (chdir (tmpdir) != 0)
    {
      return -1;
    }

  return 0;
}

static int
compare_by_brokername (const void *a, const void *b)
{
  const char *path_a = *(const char **) a;
  const char *path_b = *(const char **) b;
  char prefix_a[256], prefix_b[256];

  get_brokername_from_filename (path_a, prefix_a, sizeof (prefix_a));
  get_brokername_from_filename (path_b, prefix_b, sizeof (prefix_b));

  bool is_a_unknown = (strcmp (prefix_a, PREFIX_UNKNOWN) == 0);
  bool is_b_unknown = (strcmp (prefix_b, PREFIX_UNKNOWN) == 0);

  if (is_a_unknown && is_b_unknown)
    {
      return 0;
    }
  else if (is_a_unknown)
    {
      return 1;
    }
  else if (is_b_unknown)
    {
      return -1;
    }

  return strcmp (prefix_a, prefix_b);
}

static int
collect_log_files_from_conf (char ***out_argv)
{
  T_BROKER_INFO br_info[MAX_BROKER_NUM];
  int num_broker, master_shm_id;
  int max_log_files = 0;
  int total_captured = 0;

  if (broker_config_read (NULL, br_info, &num_broker, &master_shm_id, NULL, 0, NULL, NULL, NULL, NULL) < 0)
    {
      return -1;
    }

  for (int i = 0; i < num_broker; i++)
    {
      max_log_files += br_info[i].appl_server_max_num;
    }
  max_log_files = (max_log_files * 2) * 2;

  char **allocated_files = (char **) malloc (sizeof (char *) * max_log_files);
  if (allocated_files == NULL)
    {
      return -1;
    }
  memset (allocated_files, 0, sizeof (char *) * max_log_files);

  int s1_len = strlen (SUFFIX_SQL_LOG);
  int s2_len = strlen (SUFFIX_SQL_LOG_BAK);

  for (int i = 0; i < num_broker && total_captured < max_log_files; i++)
    {
      char strict_prefix[256];

      snprintf (strict_prefix, sizeof (strict_prefix), "%s_", br_info[i].name);
      int prefix_len = strlen (strict_prefix);

      const char *target_dir = br_info[i].log_dir;
      if (target_dir == NULL || target_dir[0] == '\0')
	{
	  continue;
	}

      DIR *dir = opendir (target_dir);
      if (dir == NULL)
	{
	  continue;
	}

      struct dirent *entry;
      while ((entry = readdir (dir)) != NULL)
	{
	  if (strncmp (entry->d_name, strict_prefix, prefix_len) != 0)
	    {
	      continue;
	    }

	  if (total_captured >= max_log_files)
	    {
	      fprintf (stderr, "Log file collection has stopped : exceeded %d\n", max_log_files);
	      break;
	    }

	  int name_len = strlen (entry->d_name);

	  bool is_matched = false;
	  if ((name_len >= s1_len && strcmp (&entry->d_name[name_len - s1_len], SUFFIX_SQL_LOG) == 0)
	      || (name_len >= s2_len && strcmp (&entry->d_name[name_len - s2_len], SUFFIX_SQL_LOG_BAK) == 0))
	    {
	      is_matched = true;
	    }

	  if (is_matched)
	    {
	      char full_path[PATH_MAX];
	      snprintf (full_path, sizeof (full_path), "%s/%s", target_dir, entry->d_name);
	      allocated_files[total_captured] = strdup (full_path);
	      if (allocated_files[total_captured])
		{
		  total_captured++;
		}
	    }
	}
      closedir (dir);
    }

  if (total_captured == 0)
    {
      free (allocated_files);
      return 0;
    }
  *out_argv = allocated_files;

  return total_captured;
}
