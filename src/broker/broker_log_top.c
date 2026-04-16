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
#include <signal.h>
#include <sys/stat.h>
#include <time.h>
#if !defined(WINDOWS)
#include <unistd.h>
#include <dirent.h>
#include <sys/wait.h>
#else
#include <direct.h>
#include <io.h>
#include <windows.h>
#endif

#if !defined(WINDOWS)
#define LOG_TOP_CHDIR(D)  chdir (D)
#else
#define LOG_TOP_CHDIR(D)  _chdir (D)
#endif

#ifdef MT_MODE
#include <pthread.h>
#endif

#include "cubrid_getopt.h"
#include "cas_common.h"
#include "cas_query_info.h"
#include "broker_log_time.h"
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
static int log_top_query_split_paths (int nfiles, char **paths);
static int log_top_fork_tran_query (int argc, char **argv, int arg_start, volatile int *qerr);
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

static const char *log_top_multi_log_dir = NULL;
static const char *log_top_multi_broker_name = NULL;
#define BROKER_LOG_TOP_MAX_PATH 2048
#define BROKER_LOG_TOP_MAX_FILES 4096
#define BROKER_LOG_TOP_MAX_DIRS 128

static int broker_log_top_force_single_thread = 0;

static void log_top_free_heap_files (char **tab, int n);
static int log_top_restore_cwd (const char *saved_cwd);
static int log_top_discover_log_dirs (char *out_dirs[], int max_dirs);
static void log_top_normalize_path (const char *in, char *out, size_t out_len);
static size_t log_top_cas_log_suffix_len (const char *basename);
static const char *log_top_path_to_basename (const char *path);
static int log_top_broker_key_from_basename (const char *basename, char *out, size_t outlen);
static int log_top_gather_files_exact (const char *broker, const char *dir, char *out_files[], int max_files);
static int log_top_is_pattern (const char *s);
static int log_top_get_brokers_from_dir (const char *dir, char *out_brokers[], int max_brokers);
static void log_top_broker_to_safe (const char *name, char *out, size_t out_len);
static int log_top_mkdir_out (const char *path);
static int log_top_multi_run (void);

static int log_top_out_merge = 1;
static int log_top_multi_explicit_dirs_flag = 0;
static int log_top_multi_explicit_ndirs = 0;
static char *log_top_multi_explicit_dirs_buf[BROKER_LOG_TOP_MAX_DIRS];
static const char log_top_multi_explicit_sentinel[] = "__EXPLICIT__";

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

  if (log_top_multi_log_dir != NULL)
    {
      error = log_top_multi_run ();
      return error;
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

  if (!log_top_out_merge && get_cnt > 0)
    error = log_top_query_split_paths (get_cnt, file_list);
  else
    {
      volatile int mqerr = 0;
      if (log_top_fork_tran_query (get_cnt, file_list, 0, &mqerr) == 0)
	error = (int) mqerr;
      else
	error = 0;
    }

  free_file_list (file_list, file_cnt);
#else
  if (!log_top_out_merge && arg_start < argc)
    error = log_top_query_split_paths (argc - arg_start, argv + arg_start);
  else
    {
      volatile int mqerr = 0;
      if (log_top_fork_tran_query (argc, argv, arg_start, &mqerr) == 0)
	error = (int) mqerr;
      else
	error = 0;
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

#if !defined(WINDOWS)
static int
log_top_match_pattern (const char *broker_prefix, const char *pattern)
{
  size_t plen = strlen (pattern);
  size_t blen = strlen (broker_prefix);
  if (plen > 0 && pattern[plen - 1] == '*')
    {
      if (plen == 1)
	return 1;
      if (blen < plen - 1)
	return 0;
      return strncmp (broker_prefix, pattern, plen - 1) == 0 ? 1 : 0;
    }
  return (blen == plen && strncmp (broker_prefix, pattern, plen) == 0) ? 1 : 0;
}
#endif

static void
log_top_free_heap_files (char **tab, int n)
{
  int j;

  if (tab == NULL || n <= 0)
    {
      return;
    }
  for (j = 0; j < n; j++)
    {
      FREE_MEM (tab[j]);
    }
}

static int
log_top_restore_cwd (const char *saved_cwd)
{
  if (saved_cwd == NULL)
    {
      fprintf (stderr, "Error: cannot restore working directory (invalid path).\n");
      return -1;
    }
  if (LOG_TOP_CHDIR ((char *) saved_cwd) < 0)
    {
      fprintf (stderr, "Error: cannot restore working directory to %s: %s\n", saved_cwd, strerror (errno));
      return -1;
    }
  return 0;
}

static void
log_top_free_dirs (char **dirs, int ndirs, int is_conf_multi)
{
  int d;

  if (is_conf_multi)
    {
      for (d = 0; d < ndirs; d++)
	{
	  if (dirs[d] != NULL)
	    {
	      FREE_MEM (dirs[d]);
	      dirs[d] = NULL;
	    }
	}
    }
  else
    {
      if (dirs[0] != NULL)
	{
	  FREE_MEM (dirs[0]);
	  dirs[0] = NULL;
	}
    }
}

static void
log_top_normalize_path (const char *in, char *out, size_t out_len)
{
  const char *cubrid;
  char buf[BROKER_LOG_TOP_MAX_PATH];
  size_t i, j;

  if (out_len < 2)
    return;
  out[0] = '\0';

  if (in[0] == '/' || (in[0] && in[1] == ':'))
    {
      snprintf (out, out_len, "%s", in);
      for (i = 0; out[i]; i++)
	if (out[i] == '\\')
	  out[i] = '/';
      j = strlen (out);
      while (j > 1 && out[j - 1] == '/')
	out[--j] = '\0';
      return;
    }

  cubrid = getenv ("CUBRID");
  if (!cubrid || !cubrid[0])
    {
      snprintf (out, out_len, "%s", in);
      return;
    }
  snprintf (buf, sizeof (buf), "%s/%s", cubrid, in[0] == '.' && in[1] == '/' ? in + 2 : in);
  for (i = 0; buf[i]; i++)
    if (buf[i] == '\\')
      buf[i] = '/';
  while (i > 1 && buf[i - 1] == '/')
    buf[--i] = '\0';
  snprintf (out, out_len, "%s", buf);
}

static int
log_top_discover_log_dirs (char *out_dirs[], int max_dirs)
{
  T_BROKER_INFO br_info[MAX_BROKER_NUM];
  int num_broker = 0;
  int master_shm_id = 0;
  int count = 0;
  int i, k;
  char norm[BROKER_LOG_TOP_MAX_PATH];
  const char *env_conf;
  const char *conf_file = NULL;

  memset (br_info, 0, sizeof (br_info));

  env_conf = getenv ("CUBRID_BROKER_CONF_FILE");
  if (env_conf && env_conf[0])
    {
      conf_file = env_conf;
    }

  if (broker_config_read (conf_file, br_info, &num_broker, &master_shm_id, NULL, 0, NULL, NULL, NULL, NULL) < 0)
    {
      fprintf (stderr, "Error: failed to read broker configuration via broker_config_read().\n");
      return -1;
    }

  for (k = 0; k < num_broker && count < max_dirs; k++)
    {
      const char *dirs_to_add[2] = { br_info[k].log_dir, br_info[k].slow_log_dir };
      int di;

      for (di = 0; di < 2 && count < max_dirs; di++)
	{
	  const char *p = dirs_to_add[di];
	  if (p == NULL || p[0] == '\0')
	    continue;

	  log_top_normalize_path (p, norm, sizeof (norm));
	  if (norm[0] == '\0')
	    continue;

	  for (i = 0; i < count; i++)
	    {
	      if (strcmp (out_dirs[i], norm) == 0)
		{
		  break;
		}
	    }
	  if (i < count)
	    continue;

	  out_dirs[count] = (char *) MALLOC (strlen (norm) + 1);
	  if (out_dirs[count] == NULL)
	    {
	      fprintf (stderr, "Error: out of memory while collecting broker log directories.\n");
	      for (i = 0; i < count; i++)
		{
		  FREE_MEM (out_dirs[i]);
		}
	      return -1;
	    }
	  strcpy (out_dirs[count], norm);
	  count++;
	}
    }

  return count;
}

static int
log_top_is_pattern (const char *s)
{
  return strchr (s, '*') != NULL || strchr (s, '?') != NULL || strchr (s, '[') != NULL;
}

static size_t
log_top_cas_log_suffix_len (const char *basename)
{
  const char *p;
  const char *best = NULL;
  size_t elen;

  if (basename == NULL)
    {
      return 0;
    }

  elen = strlen (basename);

  p = strstr (basename, ".sql.log");
  if (p != NULL && (best == NULL || p < best))
    {
      best = p;
    }

  p = strstr (basename, ".slow.log");
  if (p != NULL && (best == NULL || p < best))
    {
      best = p;
    }

  if (best == NULL)
    {
      return 0;
    }

  return elen - (size_t) (best - basename);
}

#if !defined(WINDOWS)
static int
log_top_gather_files_exact (const char *broker, const char *dir, char *out_files[], int max_files)
{
  DIR *d;
  struct dirent *e;
  char path[BROKER_LOG_TOP_MAX_PATH];
  int n = 0;
  size_t blen = strlen (broker);

  d = opendir (dir);
  if (!d)
    return 0;

  while ((e = readdir (d)) != NULL && n < max_files)
    {
      size_t elen = strlen (e->d_name);
      if (elen < blen + 2)
	continue;
      if (strncmp (e->d_name, broker, blen) != 0 || e->d_name[blen] != '_')
	continue;
      if (log_top_cas_log_suffix_len (e->d_name) == 0)
	continue;
      snprintf (path, sizeof (path), "%s/%s", dir, e->d_name);
      out_files[n] = (char *) MALLOC (strlen (path) + 1);
      if (out_files[n] == NULL)
	{
	  fprintf (stderr, "Error: out of memory while collecting log file path under %s.\n", dir);
	  log_top_free_heap_files (out_files, n);
	  closedir (d);
	  return -1;
	}
      strcpy (out_files[n], path);
      n++;
    }
  closedir (d);
  return n;
}

static int
log_top_gather_files_pattern_all (const char *pattern, const char *dir, char *out_files[], int max_files)
{
  DIR *d;
  struct dirent *e;
  char path[BROKER_LOG_TOP_MAX_PATH];
  char broker_prefix[256];
  char *last_underscore;
  int n = 0;
  size_t elen, suffix_len, copy_len;

  d = opendir (dir);
  if (!d)
    return 0;

  while ((e = readdir (d)) != NULL && n < max_files)
    {
      elen = strlen (e->d_name);
      suffix_len = log_top_cas_log_suffix_len (e->d_name);
      if (suffix_len == 0)
	continue;

      copy_len = elen - suffix_len;
      if (copy_len >= sizeof (broker_prefix))
	copy_len = sizeof (broker_prefix) - 1;
      memcpy (broker_prefix, e->d_name, copy_len);
      broker_prefix[copy_len] = '\0';

      last_underscore = strrchr (broker_prefix, '_');
      if (!last_underscore)
	continue;
      *last_underscore = '\0';

      if (!log_top_match_pattern (broker_prefix, pattern))
	continue;

      snprintf (path, sizeof (path), "%s/%s", dir, e->d_name);
      out_files[n] = (char *) MALLOC (strlen (path) + 1);
      if (out_files[n] == NULL)
	{
	  fprintf (stderr, "Error: out of memory while collecting log file path under %s.\n", dir);
	  log_top_free_heap_files (out_files, n);
	  closedir (d);
	  return -1;
	}
      strcpy (out_files[n], path);
      n++;
    }
  closedir (d);
  return n;
}
#else
static int
log_top_gather_files_exact (const char *broker, const char *dir, char *out_files[], int max_files)
{
  HANDLE h;
  WIN32_FIND_DATAA fd;
  char pattern[BROKER_LOG_TOP_MAX_PATH];
  char path[BROKER_LOG_TOP_MAX_PATH];
  int n = 0;
  const char *suffixes[] = { "*.sql.log*", "*.slow.log*", NULL };

  for (int i = 0; suffixes[i] && n < max_files; i++)
    {
      snprintf (pattern, sizeof (pattern), "%s\\%s_%s", dir, broker, suffixes[i]);
      h = FindFirstFileA (pattern, &fd);
      if (h == INVALID_HANDLE_VALUE)
	continue;
      do
	{
	  if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
	    continue;
	  snprintf (path, sizeof (path), "%s\\%s", dir, fd.cFileName);
	  out_files[n] = (char *) MALLOC (strlen (path) + 1);
	  if (out_files[n] == NULL)
	    {
	      fprintf (stderr, "Error: out of memory while collecting log file path under %s.\n", dir);
	      FindClose (h);
	      return -1;
	    }
	  strcpy (out_files[n], path);
	  n++;
	}
      while (FindNextFileA (h, &fd) && n < max_files);
      FindClose (h);
    }
  return n;
}

static int
log_top_gather_files_pattern_all (const char *pattern, const char *dir, char *out_files[], int max_files)
{
  HANDLE h;
  WIN32_FIND_DATAA fd;
  char search[BROKER_LOG_TOP_MAX_PATH];
  char path[BROKER_LOG_TOP_MAX_PATH];
  int n = 0;
  int si;
  const char *suffixes[] = { "*.sql.log*", "*.slow.log*", NULL };

  for (si = 0; suffixes[si] && n < max_files; si++)
    {
      snprintf (search, sizeof (search), "%s\\%s_%s", dir, pattern, suffixes[si]);
      h = FindFirstFileA (search, &fd);
      if (h == INVALID_HANDLE_VALUE)
	continue;
      do
	{
	  if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
	    continue;
	  if (n >= max_files)
	    break;
	  snprintf (path, sizeof (path), "%s\\%s", dir, fd.cFileName);
	  out_files[n] = (char *) MALLOC (strlen (path) + 1);
	  if (out_files[n] == NULL)
	    {
	      fprintf (stderr, "Error: out of memory while collecting log file path under %s.\n", dir);
	      FindClose (h);
	      return -1;
	    }
	  strcpy (out_files[n], path);
	  n++;
	}
      while (FindNextFileA (h, &fd));
      FindClose (h);
    }
  return n;
}

static int
log_top_get_brokers_from_dir (const char *dir, char *out_brokers[], int max_brokers)
{
  HANDLE h;
  WIN32_FIND_DATAA fd;
  char pattern[BROKER_LOG_TOP_MAX_PATH];
  char seen[256][256];
  char broker[256];
  int nseen = 0;
  int n = 0;
  int i;
  const char *patterns[] = { "*_*.sql.log*", "*_*.slow.log*", NULL };

  for (i = 0; patterns[i] && n < max_brokers; i++)
    {
      snprintf (pattern, sizeof (pattern), "%s\\%s", dir, patterns[i]);
      h = FindFirstFileA (pattern, &fd);
      if (h == INVALID_HANDLE_VALUE)
	continue;
      do
	{
	  int j;
	  if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
	    continue;
	  if (log_top_broker_key_from_basename (fd.cFileName, broker, sizeof (broker)) != 0)
	    continue;
	  for (j = 0; j < nseen; j++)
	    if (strcmp (seen[j], broker) == 0)
	      break;
	  if (j < nseen)
	    continue;
	  if (nseen < 256)
	    {
	      snprintf (seen[nseen], sizeof (seen[0]), "%.255s", broker);
	      nseen++;
	    }
	  if (n >= max_brokers)
	    continue;
	  out_brokers[n] = (char *) MALLOC (strlen (broker) + 1);
	  if (out_brokers[n] == NULL)
	    {
	      fprintf (stderr, "Error: out of memory while collecting broker names under %s.\n", dir);
	      FindClose (h);
	      return -1;
	    }
	  strcpy (out_brokers[n], broker);
	  n++;
	}
      while (FindNextFileA (h, &fd));
      FindClose (h);
    }
  return n;
}
#endif

#if !defined(WINDOWS)
static int
log_top_get_brokers_from_dir (const char *dir, char *out_brokers[], int max_brokers)
{
  DIR *d;
  struct dirent *e;
  char seen[256][256];
  int nseen = 0;
  int n = 0;
  int i;
  char broker[256];

  d = opendir (dir);
  if (!d)
    return 0;

  while ((e = readdir (d)) != NULL && n < max_brokers)
    {
      if (log_top_broker_key_from_basename (e->d_name, broker, sizeof (broker)) != 0)
	continue;
      for (i = 0; i < nseen; i++)
	if (strcmp (seen[i], broker) == 0)
	  break;
      if (i < nseen)
	continue;
      if (nseen < 256)
	{
	  snprintf (seen[nseen], sizeof (seen[0]), "%.255s", broker);
	  nseen++;
	}
      if (n >= max_brokers)
	continue;
      out_brokers[n] = (char *) MALLOC (strlen (broker) + 1);
      if (out_brokers[n] == NULL)
	{
	  fprintf (stderr, "Error: out of memory while collecting broker names under %s.\n", dir);
	  closedir (d);
	  return -1;
	}
      strcpy (out_brokers[n], broker);
      n++;
    }
  closedir (d);
  return n;
}
#endif

static void
log_top_broker_to_safe (const char *name, char *out, size_t out_len)
{
  size_t i, j;
  if (out_len < 2)
    return;
  for (i = 0, j = 0; name[i] && j < out_len - 1; i++)
    {
      if (name[i] == '*' || name[i] == '?' || name[i] == '[')
	out[j++] = '_';
      else
	out[j++] = name[i];
    }
  out[j] = '\0';
}

#if !defined(WINDOWS)
static int
log_top_mkdir_out (const char *path)
{
  return mkdir (path, 0755);
}
#else
static int
log_top_mkdir_out (const char *path)
{
  return _mkdir (path);
}
#endif

static int
log_top_multi_dir_is_conf_multi (void)
{
  return log_top_multi_explicit_dirs_flag != 0
    || (log_top_multi_log_dir != NULL && strcasecmp (log_top_multi_log_dir, "LOG_DIR") == 0);
}

static int
log_top_fill_parent_out_base (char *parent_out_base, size_t parent_sz)
{
  time_t now;
  struct tm tm_buf;

  memset (&tm_buf, 0, sizeof (tm_buf));
  const char *env_out_base = getenv ("OUT_BASE");

  if (env_out_base && env_out_base[0])
    {
      snprintf (parent_out_base, parent_sz, "%s", env_out_base);
      return 0;
    }
  now = time (NULL);
#if !defined(WINDOWS)
  if (localtime_r (&now, &tm_buf) == NULL)
#else
  if (localtime_s (&tm_buf, &now) != 0)
#endif
    {
      fprintf (stderr, "Error: cannot get local time\n");
      return -1;
    }
  strftime (parent_out_base, parent_sz, "broker_log_top_%y%m%d_%H%M", &tm_buf);
  return 0;
}

static int
log_top_fork_tran_query (int argc, char **argv, int arg_start, volatile int *qerr)
{
#if !defined(WINDOWS)
  int pipefd[2];
  pid_t pid;
  int status;
  ssize_t rw;
  int err_buf;

  if (pipe (pipefd) < 0)
    {
      fprintf (stderr, "Error: pipe failed: %s\n", strerror (errno));
      *qerr = -1;
      return 0;
    }

  pid = fork ();
  if (pid < 0)
    {
      fprintf (stderr, "Error: fork failed: %s\n", strerror (errno));
      close (pipefd[0]);
      close (pipefd[1]);
      *qerr = -1;
      return 0;
    }

  if (pid == 0)
    {
      int err_run;

      close (pipefd[0]);
      if (mode_tran)
	{
	  err_run = log_top_tran (argc, argv, arg_start);
	}
      else
	{
	  err_run = log_top_query (argc, argv, arg_start);
	}
      rw = write (pipefd[1], &err_run, sizeof (err_run));
      if (rw != (ssize_t) sizeof (err_run))
	{
	  close (pipefd[1]);
	  _exit (124);
	}
      close (pipefd[1]);
      _exit (0);
    }

  close (pipefd[1]);
  if (waitpid (pid, &status, 0) < 0)
    {
      fprintf (stderr, "Error: waitpid failed: %s\n", strerror (errno));
      close (pipefd[0]);
      *qerr = -1;
      return 0;
    }

  if (WIFSIGNALED (status))
    {
      if (WTERMSIG (status) == SIGSEGV)
	{
	  fprintf (stderr, "Warning: segmentation fault during processing, skipping.\n");
	}
      else
	{
	  fprintf (stderr, "Warning: child terminated by signal %d.\n", WTERMSIG (status));
	}
      close (pipefd[0]);
      *qerr = 0;
      return 1;
    }

  if (!WIFEXITED (status))
    {
      close (pipefd[0]);
      *qerr = -1;
      return 0;
    }

  if (WEXITSTATUS (status) != 0)
    {
      close (pipefd[0]);
      *qerr = -1;
      return 0;
    }

  rw = read (pipefd[0], &err_buf, sizeof (err_buf));
  close (pipefd[0]);
  if (rw != (ssize_t) sizeof (err_buf))
    {
      *qerr = -1;
      return 0;
    }
  *qerr = err_buf;
  return 0;
#else
  if (mode_tran)
    {
      *qerr = log_top_tran (argc, argv, arg_start);
    }
  else
    {
      *qerr = log_top_query (argc, argv, arg_start);
    }
  return 0;
#endif
}

static void
log_top_multi_run_one_broker (const char *parent_out_base, const char *subdir_for_mkdir,
			      int use_safe_mkdir, const char *stdout_label, char **files, int nfiles, char *saved_cwd,
			      volatile int *error_io)
{
  char subdir[BROKER_LOG_TOP_MAX_PATH];
  char broker_safe[256];
  const char *mkcomp = subdir_for_mkdir;
  volatile int i;
  volatile int qerr = 0;

  if (use_safe_mkdir)
    {
      log_top_broker_to_safe (subdir_for_mkdir, broker_safe, sizeof (broker_safe));
      mkcomp = broker_safe;
    }
  snprintf (subdir, sizeof (subdir), "%.1023s/%.255s", parent_out_base, mkcomp);
  if (log_top_mkdir_out (subdir) < 0 && errno != EEXIST)
    {
      fprintf (stderr, "Error: cannot create output directory: %s\n", subdir);
      log_top_free_heap_files (files, nfiles);
      *error_io = -1;
      return;
    }
  if (LOG_TOP_CHDIR (subdir) < 0)
    {
      fprintf (stderr, "Error: cannot chdir to %s\n", subdir);
      log_top_free_heap_files (files, nfiles);
      *error_io = -1;
      return;
    }
  fprintf (stdout, "%s\n", stdout_label);
  query_info_reset ();
  if (log_top_fork_tran_query (nfiles, files, 0, &qerr) == 0)
    {
      if (log_top_restore_cwd (saved_cwd) < 0)
	{
	  *error_io = -1;
	}
      else if (qerr != 0)
	{
	  *error_io = qerr;
	}
      for (i = 0; i < nfiles; i++)
	{
	  FREE_MEM (files[i]);
	}
    }
  else
    {
      if (log_top_restore_cwd (saved_cwd) < 0)
	{
	  *error_io = -1;
	}
      log_top_free_heap_files (files, nfiles);
    }
}

static int
log_top_finish_force_single_thread (int val)
{
  broker_log_top_force_single_thread = 0;
  return val;
}

static int
log_top_multi_run (void)
{
  char *dirs[BROKER_LOG_TOP_MAX_DIRS];
  volatile char **files = NULL;
  char norm[BROKER_LOG_TOP_MAX_PATH];
  char parent_out_base[BROKER_LOG_TOP_MAX_PATH];
  char cwd_buf[BROKER_LOG_TOP_MAX_PATH];
  volatile char *saved_cwd = NULL;
  volatile int ndirs = 0;
  volatile int d = 0, i = 0;
  volatile int nfiles = 0;
  int error = 0;
  struct stat st;

  if (log_top_multi_log_dir == NULL)
    {
      return -1;
    }

  saved_cwd = (volatile char *) getcwd (cwd_buf, sizeof (cwd_buf));
  if (saved_cwd == NULL)
    {
      fprintf (stderr, "Error: cannot get current directory\n");
      return -1;
    }

  broker_log_top_force_single_thread = 1;

  if (strcmp (log_top_multi_log_dir, ".") == 0 && log_top_multi_broker_name != NULL)
    {
      int dot_file_count;
      volatile int qerr_dot = 0;

      files = (volatile char **) MALLOC (sizeof (char *) * BROKER_LOG_TOP_MAX_FILES);
      if (files == NULL)
	{
	  fprintf (stderr, "Error: memory allocation failed\n");
	  return log_top_finish_force_single_thread (-1);
	}
      memset ((void *) files, 0, sizeof (char *) * BROKER_LOG_TOP_MAX_FILES);
      dot_file_count = log_top_is_pattern (log_top_multi_broker_name)
	? log_top_gather_files_pattern_all (log_top_multi_broker_name, cwd_buf, (char **) files,
					    BROKER_LOG_TOP_MAX_FILES)
	: log_top_gather_files_exact (log_top_multi_broker_name, cwd_buf, (char **) files, BROKER_LOG_TOP_MAX_FILES);
      if (dot_file_count < 0)
	{
	  fprintf (stderr, "Error: failed to gather log files for broker '%s'.\n", log_top_multi_broker_name);
	  {
	    char **fp = (char **) files;
	    FREE_MEM (fp);
	    files = (volatile char **) fp;
	  }
	  return log_top_finish_force_single_thread (-1);
	}
      if (dot_file_count == 0)
	{
	  fprintf (stderr, "Info: no files for broker '%s' in current directory.\n", log_top_multi_broker_name);
	  {
	    char **fp = (char **) files;
	    FREE_MEM (fp);
	    files = (volatile char **) fp;
	  }
	  return log_top_finish_force_single_thread (-1);
	}
      fprintf (stdout, "%s\n", log_top_multi_broker_name);
      query_info_reset ();
      if (log_top_fork_tran_query (dot_file_count, (char **) files, 0, &qerr_dot) == 0)
	{
	  error = (int) qerr_dot;
	}
      else
	{
	  error = 0;
	}
      log_top_free_heap_files ((char **) files, dot_file_count);
      {
	char **fp = (char **) files;
	FREE_MEM (fp);
	files = (volatile char **) fp;
      }
      return log_top_finish_force_single_thread (error);
    }

  if (log_top_fill_parent_out_base (parent_out_base, sizeof (parent_out_base)) < 0)
    {
      return log_top_finish_force_single_thread (-1);
    }

  if (log_top_mkdir_out (parent_out_base) < 0 && errno != EEXIST)
    {
      fprintf (stderr, "Error: cannot create output directory: %s\n", parent_out_base);
      return log_top_finish_force_single_thread (-1);
    }

  if (log_top_multi_explicit_dirs_flag)
    {
      ndirs = log_top_multi_explicit_ndirs;
      for (d = 0; d < ndirs; d++)
	{
	  dirs[d] = log_top_multi_explicit_dirs_buf[d];
	}
    }
  else if (strcasecmp (log_top_multi_log_dir, "LOG_DIR") == 0)
    {
      ndirs = log_top_discover_log_dirs (dirs, BROKER_LOG_TOP_MAX_DIRS);
      if (ndirs < 0)
	{
	  fprintf (stderr, "Error: failed to read LOG_DIR / SLOW_LOG_DIR entries from broker configuration.\n");
	  return log_top_finish_force_single_thread (-1);
	}
      if (ndirs == 0)
	{
	  fprintf (stderr, "Error: no valid LOG_DIR or SLOW_LOG_DIR found in cubrid_broker.conf\n");
	  return log_top_finish_force_single_thread (-1);
	}
    }
  else
    {
      log_top_normalize_path (log_top_multi_log_dir, norm, sizeof (norm));
      if (stat (norm, &st) < 0 || !S_ISDIR (st.st_mode))
	{
	  fprintf (stderr, "Error: directory does not exist: %s\n", norm);
	  return log_top_finish_force_single_thread (-1);
	}
      dirs[0] = (char *) MALLOC (strlen (norm) + 1);
      if (!dirs[0])
	{
	  return log_top_finish_force_single_thread (-1);
	}
      strcpy (dirs[0], norm);
      ndirs = 1;
    }

  files = (volatile char **) MALLOC (sizeof (char *) * BROKER_LOG_TOP_MAX_FILES);
  if (files == NULL)
    {
      fprintf (stderr, "Error: memory allocation failed\n");
      log_top_free_dirs (dirs, ndirs, log_top_multi_dir_is_conf_multi ());
      return log_top_finish_force_single_thread (-1);
    }
  memset ((void *) files, 0, sizeof (char *) * BROKER_LOG_TOP_MAX_FILES);

  if (log_top_multi_broker_name)
    {
      for (d = 0; d < ndirs && !error; d++)
	{
	  if (log_top_is_pattern (log_top_multi_broker_name))
	    {
	      nfiles = log_top_gather_files_pattern_all (log_top_multi_broker_name, dirs[d], (char **) files,
							 BROKER_LOG_TOP_MAX_FILES);
	      if (nfiles < 0)
		{
		  fprintf (stderr, "Error: failed to gather log files matching '%s' under [%s].\n",
			   log_top_multi_broker_name, dirs[d]);
		  error = -1;
		  break;
		}
	      if (nfiles == 0)
		{
		  fprintf (stderr, "Info: [%s] no files matching pattern '%s', skipping.\n", dirs[d],
			   log_top_multi_broker_name);
		  goto next_dir;
		}
	      log_top_multi_run_one_broker (parent_out_base, log_top_multi_broker_name, 1,
					    log_top_multi_broker_name, (char **) files, nfiles, (char *) saved_cwd,
					    &error);
	      nfiles = 0;
	    }
	  else
	    {
	      nfiles = log_top_gather_files_exact (log_top_multi_broker_name, dirs[d], (char **) files,
						   BROKER_LOG_TOP_MAX_FILES);
	      if (nfiles < 0)
		{
		  fprintf (stderr, "Error: failed to gather log files for broker '%s' under [%s].\n",
			   log_top_multi_broker_name, dirs[d]);
		  error = -1;
		  break;
		}
	      if (nfiles == 0)
		{
		  fprintf (stderr, "Info: [%s] no files for broker '%s', skipping.\n", dirs[d],
			   log_top_multi_broker_name);
		  goto next_dir;
		}
	      log_top_multi_run_one_broker (parent_out_base, log_top_multi_broker_name, 0,
					    log_top_multi_broker_name, (char **) files, nfiles, (char *) saved_cwd,
					    &error);
	      nfiles = 0;
	    }
	next_dir:
	  if (log_top_multi_dir_is_conf_multi ())
	    {
	      FREE_MEM (dirs[d]);
	      dirs[d] = NULL;
	    }
	}
    }
  else
    {
      char *all_brokers[512];
      char **all_files;
      volatile int total_brokers = 0;
      int bi, dir_idx, k;

      memset (all_brokers, 0, sizeof (all_brokers));

      all_files = (char **) MALLOC (sizeof (char *) * BROKER_LOG_TOP_MAX_FILES);
      if (all_files == NULL)
	{
	  fprintf (stderr, "Error: memory allocation failed\n");
	  log_top_free_dirs (dirs, ndirs, log_top_multi_dir_is_conf_multi ());
	  {
	    char **fp = (char **) files;
	    FREE_MEM (fp);
	    files = (volatile char **) fp;
	  }
	  return log_top_finish_force_single_thread (-1);
	}
      memset (all_files, 0, sizeof (char *) * BROKER_LOG_TOP_MAX_FILES);

      for (dir_idx = 0; dir_idx < ndirs && total_brokers < 512 && error == 0; dir_idx++)
	{
	  char *dir_brokers[512];
	  int dir_nb;

	  memset (dir_brokers, 0, sizeof (dir_brokers));
	  if (dirs[dir_idx] == NULL)
	    continue;
	  dir_nb = log_top_get_brokers_from_dir (dirs[dir_idx], dir_brokers, 512);
	  if (dir_nb < 0)
	    {
	      fprintf (stderr, "Error: failed to list brokers under [%s].\n", dirs[dir_idx]);
	      error = -1;
	      break;
	    }

	  for (bi = 0; bi < dir_nb && total_brokers < 512; bi++)
	    {
	      int found = 0;
	      if (dir_brokers[bi] == NULL)
		continue;
	      for (k = 0; k < total_brokers; k++)
		{
		  if (all_brokers[k] != NULL && strcmp (all_brokers[k], dir_brokers[bi]) == 0)
		    {
		      found = 1;
		      FREE_MEM (dir_brokers[bi]);
		      dir_brokers[bi] = NULL;
		      break;
		    }
		}
	      if (!found)
		{
		  all_brokers[total_brokers] = dir_brokers[bi];
		  total_brokers++;
		}
	    }
	  for (; bi < dir_nb; bi++)
	    {
	      if (dir_brokers[bi] != NULL)
		{
		  FREE_MEM (dir_brokers[bi]);
		  dir_brokers[bi] = NULL;
		}
	    }
	}

      if (error != 0)
	{
	  int zb;
	  for (zb = 0; zb < total_brokers && zb < 512; zb++)
	    {
	      if (all_brokers[zb] != NULL)
		{
		  FREE_MEM (all_brokers[zb]);
		  all_brokers[zb] = NULL;
		}
	    }
	  log_top_free_dirs (dirs, ndirs, log_top_multi_dir_is_conf_multi ());
	  FREE_MEM (all_files);
	  {
	    char **fp = (char **) files;
	    FREE_MEM (fp);
	    files = (volatile char **) fp;
	  }
	  return log_top_finish_force_single_thread (-1);
	}

      if (total_brokers == 0)
	{
	  fprintf (stderr, "Info: no brokers found in any directory.\n");
	  log_top_free_dirs (dirs, ndirs, log_top_multi_dir_is_conf_multi ());
	  FREE_MEM (all_files);
	  {
	    char **fp = (char **) files;
	    FREE_MEM (fp);
	    files = (volatile char **) fp;
	  }
	  return log_top_finish_force_single_thread (0);
	}

      for (i = 0; i < total_brokers && !error; i++)
	{
	  volatile int total_files = 0;
	  int dir_idx;
	  int num_files = 0;

	  memset (all_files, 0, sizeof (char *) * BROKER_LOG_TOP_MAX_FILES);

	  if (all_brokers[i] == NULL)
	    continue;

	  for (dir_idx = 0; dir_idx < ndirs && total_files < BROKER_LOG_TOP_MAX_FILES && error == 0; dir_idx++)
	    {
	      int dir_files;

	      if (dirs[dir_idx] == NULL)
		continue;
	      dir_files = log_top_gather_files_exact (all_brokers[i], dirs[dir_idx],
						      all_files + total_files, BROKER_LOG_TOP_MAX_FILES - total_files);
	      if (dir_files < 0)
		{
		  fprintf (stderr, "Error: failed to gather log files for broker '%s' under [%s].\n",
			   all_brokers[i], dirs[dir_idx]);
		  error = -1;
		  break;
		}
	      total_files += dir_files;
	    }
	  num_files = total_files;

	  if (total_files == 0)
	    {
	      FREE_MEM (all_brokers[i]);
	      all_brokers[i] = NULL;
	      continue;
	    }

	  log_top_multi_run_one_broker (parent_out_base, all_brokers[i], 0, all_brokers[i], all_files, num_files,
					(char *) saved_cwd, &error);
	  memset (all_files, 0, sizeof (char *) * BROKER_LOG_TOP_MAX_FILES);
	  FREE_MEM (all_brokers[i]);
	  all_brokers[i] = NULL;
	}

      for (k = 0; k < total_brokers; k++)
	{
	  if (all_brokers[k] != NULL)
	    {
	      FREE_MEM (all_brokers[k]);
	      all_brokers[k] = NULL;
	    }
	}
      FREE_MEM (all_files);
    }

  if (files != NULL)
    {
      char **fp = (char **) files;
      FREE_MEM (fp);
      files = (volatile char **) fp;
    }

  log_top_free_dirs (dirs, ndirs, log_top_multi_dir_is_conf_multi ());

  return log_top_finish_force_single_thread (error);
}

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
  int use_mt = 0;
  T_THREAD thrid;
  int j;
#endif

#ifdef MT_MODE
  if (!broker_log_top_force_single_thread && num_thread > 1)
    {
      use_mt = 1;
    }
#endif

#ifdef MT_MODE
  if (use_mt)
    {
      process_flag = 1;
      work_msg = MALLOC (sizeof (T_WORK_MSG) * num_thread);
      if (work_msg == NULL)
	{
	  fprintf (stderr, "malloc error\n");
	  return -1;
	}
      memset (work_msg, 0, sizeof (T_WORK_MSG) * num_thread);

      for (i = 0; i < num_thread; i++)
	THREAD_BEGIN (thrid, thr_main, (void *) i);
    }
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
      if (use_mt)
	{
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
	}
      else
	{
	  error = log_top (fp, filename, start_offset, end_offset);
	  fclose (fp);
	  if (error == LT_INVAILD_VERSION)
	    {
	      return error;
	    }
	}
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
  if (use_mt)
    {
      process_flag = 0;
    }
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

typedef struct lt_split_sort lt_split_sort;
struct lt_split_sort
{
  char *path;
  char key[256];
};

static int
lt_split_sort_cmp (const void *a, const void *b)
{
  return strcmp (((const lt_split_sort *) a)->key, ((const lt_split_sort *) b)->key);
}

static int
log_top_basename_is_cas_sql_slow_log (const char *basename)
{
  return log_top_cas_log_suffix_len (basename) != 0;
}

static const char *
log_top_path_to_basename (const char *path)
{
  const char *p;

  p = strrchr (path, '/');
#if defined(WINDOWS)
  {
    const char *p2 = strrchr (path, '\\');
    if (p2 != NULL && (p == NULL || p2 > p))
      p = p2;
  }
#endif
  return (p != NULL) ? p + 1 : path;
}

static int
log_top_path_is_cas_sql_slow_log (const char *path)
{
  return log_top_basename_is_cas_sql_slow_log (log_top_path_to_basename (path));
}

static int
log_top_broker_key_from_basename (const char *basename, char *out, size_t outlen)
{
  size_t elen = strlen (basename);
  size_t suffix_len;
  char tmp[512];
  char *lastu;

  suffix_len = log_top_cas_log_suffix_len (basename);
  if (suffix_len == 0)
    {
      snprintf (out, outlen, "%s", basename);
      return -1;
    }
  if (elen <= suffix_len)
    {
      snprintf (out, outlen, "%s", basename);
      return -1;
    }
  if (elen - suffix_len >= sizeof (tmp))
    {
      snprintf (out, outlen, "%s", basename);
      return -1;
    }
  memcpy (tmp, basename, elen - suffix_len);
  tmp[elen - suffix_len] = '\0';
  lastu = strrchr (tmp, '_');
  if (lastu == NULL || lastu == tmp)
    {
      snprintf (out, outlen, "%s", basename);
      return -1;
    }
  *lastu = '\0';
  snprintf (out, outlen, "%.255s", tmp);
  return 0;
}

static void
log_top_broker_key_from_path (const char *path, char *out, size_t outlen)
{
  (void) log_top_broker_key_from_basename (log_top_path_to_basename (path), out, outlen);
}

static int
log_top_query_split_paths (int nfiles, char **paths)
{
  lt_split_sort *tab = NULL;
  char parent_out_base[BROKER_LOG_TOP_MAX_PATH];
  char cwd_buf[BROKER_LOG_TOP_MAX_PATH];
  char *saved_cwd;
  char subdir[BROKER_LOG_TOP_MAX_PATH];
  char broker_safe[256];
  int i, k;
  int nvalid;
  volatile int error = 0;
  int g_start, g_end;
  volatile char **argv_small = NULL;

  if (nfiles <= 0 || paths == NULL)
    return -1;

  nvalid = 0;
  for (i = 0; i < nfiles; i++)
    {
      if (log_top_path_is_cas_sql_slow_log (paths[i]))
	nvalid++;
      else
	fprintf (stderr, "Info: skipping (not a CAS sql/slow log): %s\n", paths[i]);
    }
  if (nvalid == 0)
    {
      fprintf (stderr,
	       "Error: no CAS sql/slow log files (.sql.log, .sql.log.bak, .slow.log, .slow.log.bak) in arguments.\n");
      return -1;
    }

  tab = (lt_split_sort *) MALLOC ((size_t) nvalid * sizeof (lt_split_sort));
  if (tab == NULL)
    {
      fprintf (stderr, "Error: memory allocation failed\n");
      return -1;
    }
  for (i = 0, k = 0; i < nfiles; i++)
    {
      if (!log_top_path_is_cas_sql_slow_log (paths[i]))
	continue;
      tab[k].path = paths[i];
      log_top_broker_key_from_path (paths[i], tab[k].key, sizeof (tab[k].key));
      k++;
    }
  qsort (tab, (size_t) nvalid, sizeof (tab[0]), lt_split_sort_cmp);

  saved_cwd = getcwd (cwd_buf, sizeof (cwd_buf));
  if (saved_cwd == NULL)
    {
      fprintf (stderr, "Error: cannot get current directory\n");
      FREE_MEM (tab);
      return -1;
    }

  if (log_top_fill_parent_out_base (parent_out_base, sizeof (parent_out_base)) < 0)
    {
      FREE_MEM (tab);
      return -1;
    }
  if (log_top_mkdir_out (parent_out_base) < 0 && errno != EEXIST)
    {
      fprintf (stderr, "Error: cannot create output directory: %s\n", parent_out_base);
      FREE_MEM (tab);
      return -1;
    }

  broker_log_top_force_single_thread = 1;

  g_start = 0;
  while (g_start < nvalid)
    {
      volatile int qerr = 0;

      g_end = g_start + 1;
      while (g_end < nvalid && strcmp (tab[g_end].key, tab[g_start].key) == 0)
	g_end++;

      argv_small = (volatile char **) MALLOC ((size_t) (g_end - g_start) * sizeof (char *));
      if (argv_small == NULL)
	{
	  fprintf (stderr, "Error: memory allocation failed\n");
	  error = -1;
	  break;
	}
      memset ((void *) argv_small, 0, (size_t) (g_end - g_start) * sizeof (char *));
      for (k = 0; k < g_end - g_start; k++)
	argv_small[k] = tab[g_start + k].path;

      log_top_broker_to_safe (tab[g_start].key, broker_safe, sizeof (broker_safe));
      snprintf (subdir, sizeof (subdir), "%.1023s/%.255s", parent_out_base, broker_safe);

      if (log_top_mkdir_out (subdir) < 0 && errno != EEXIST)
	{
	  fprintf (stderr, "Error: cannot create output directory: %s\n", subdir);
	  error = -1;
	  {
	    char **fp = (char **) argv_small;
	    FREE_MEM (fp);
	    argv_small = (volatile char **) fp;
	  }
	  break;
	}
      if (LOG_TOP_CHDIR (subdir) < 0)
	{
	  fprintf (stderr, "Error: cannot chdir to %s\n", subdir);
	  error = -1;
	  {
	    char **fp = (char **) argv_small;
	    FREE_MEM (fp);
	    argv_small = (volatile char **) fp;
	  }
	  break;
	}

      fprintf (stdout, "%s\n", tab[g_start].key);
      query_info_reset ();
      if (log_top_fork_tran_query (g_end - g_start, (char **) argv_small, 0, &qerr) == 0)
	{
	  if (log_top_restore_cwd (saved_cwd) < 0)
	    {
	      error = -1;
	    }
	  else if (qerr != 0)
	    {
	      error = qerr;
	    }
	}
      else
	{
	  if (log_top_restore_cwd (saved_cwd) < 0)
	    {
	      error = -1;
	    }
	}
      {
	char **fp = (char **) argv_small;
	FREE_MEM (fp);
	argv_small = (volatile char **) fp;
      }
      g_start = g_end;
    }

  FREE_MEM (tab);
  return log_top_finish_force_single_thread ((int) error);
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
	  log_top (work_msg[self_index].fp, work_msg[self_index].filename, -1, -1);
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
    {
      *query_p = *query_p + 1;
    }

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
  int option_index = 0;

  log_top_multi_explicit_dirs_flag = 0;
  log_top_out_merge = 1;

  static struct option long_options[] = {
    {"from-conf", no_argument, 0, 'C'},
    {0, 0, 0, 0}
  };

  while ((c = getopt_long (argc, argv, "tq:h:F:T:O:C", long_options, &option_index)) != EOF)
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
	case 'C':
	  log_top_multi_log_dir = "LOG_DIR";
	  break;
	case 'O':
	  if (optarg == NULL)
	    {
	      goto getargs_err;
	    }
	  if (strcmp (optarg, "merge") != 0 && strcmp (optarg, "split") != 0)
	    {
	      fprintf (stderr, "Error: invalid -O value: %s (allowed: merge|split)\n", optarg);
	      goto getargs_err;
	    }
	  log_top_out_merge = (strcmp (optarg, "merge") == 0);
	  break;
	default:
	  goto getargs_err;
	}
    }

  if (mode_max_handle_lower_bound > 0)
    {
      log_top_mode = MODE_MAX_HANDLE;
    }

  if (!log_top_out_merge && log_top_multi_log_dir == NULL && optind < argc)
    {
      struct stat st;
      char norm[BROKER_LOG_TOP_MAX_PATH];
      int dir_idx;
      int n_remain = argc - optind;
      int all_dir = 1;

      for (dir_idx = 0; dir_idx < n_remain; dir_idx++)
	{
	  log_top_normalize_path (argv[optind + dir_idx], norm, sizeof (norm));
	  if (stat (norm, &st) != 0 || !S_ISDIR (st.st_mode))
	    {
	      all_dir = 0;
	      break;
	    }
	}
      if (all_dir && n_remain >= 1 && n_remain <= BROKER_LOG_TOP_MAX_DIRS)
	{
	  for (dir_idx = 0; dir_idx < n_remain; dir_idx++)
	    {
	      log_top_normalize_path (argv[optind + dir_idx], norm, sizeof (norm));
	      log_top_multi_explicit_dirs_buf[dir_idx] = (char *) MALLOC (strlen (norm) + 1);
	      if (log_top_multi_explicit_dirs_buf[dir_idx] == NULL)
		{
		  while (dir_idx > 0)
		    {
		      dir_idx--;
		      FREE_MEM (log_top_multi_explicit_dirs_buf[dir_idx]);
		      log_top_multi_explicit_dirs_buf[dir_idx] = NULL;
		    }
		  goto getargs_err;
		}
	      strcpy (log_top_multi_explicit_dirs_buf[dir_idx], norm);
	    }
	  log_top_multi_explicit_dirs_flag = 1;
	  log_top_multi_explicit_ndirs = n_remain;
	  optind += n_remain;
	  log_top_multi_log_dir = log_top_multi_explicit_sentinel;
	  log_top_multi_broker_name = NULL;
	  return optind;
	}
    }

  if (optind < argc && optind + 1 == argc)
    {
      const char *arg = argv[optind];
      if (strchr (arg, '/') == NULL && log_top_cas_log_suffix_len (arg) == 0)
	{
	  log_top_multi_broker_name = arg;
	  if (log_top_multi_log_dir == NULL)
	    {
	      log_top_multi_log_dir = ".";
	    }
	}
    }

  if (log_top_multi_log_dir != NULL && optind < argc)
    {
      goto getargs_err;
    }

  if (log_top_multi_log_dir != NULL)
    {
      return optind;
    }

  if (optind < argc)
    {
      return optind;
    }

  goto getargs_err;

getargs_err:
  fprintf (stderr,
	   "%s [-t] [-F <from date>] [-T <to date>] [-O <merge | split>] <log_file> ...\n"
	   "or\n" "%s [-t] [-F <from date>] [-T <to date>] --from-conf\n", argv[0], argv[0]);
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
