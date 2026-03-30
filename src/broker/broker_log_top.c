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
#include <sys/stat.h>
#include <time.h>
#if !defined(WINDOWS)
#include <unistd.h>
#include <dirent.h>
#else
#include <io.h>
#include <windows.h>
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
static int split_run_from_args (int argc, char *argv[], int arg_start);
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

static const char *batch_log_dir = NULL;
static const char *batch_broker_name = NULL;
static int batch_split_output = 0;
static int from_conf_mode = 0;
#define BROKER_LOG_TOP_MAX_PATH 2048
#define BROKER_LOG_TOP_MAX_FILES 4096
#define BROKER_LOG_TOP_MAX_DIRS 128

static int broker_log_top_force_single_thread = 0;

static int batch_discover_log_dirs (char *out_dirs[], int max_dirs);
static void batch_normalize_path (const char *in, char *out, size_t out_len);
static int batch_gather_files_exact (const char *broker, const char *dir, char *out_files[], int max_files);
static int batch_gather_files_pattern (const char *pattern, const char *dir, char *out_files[], int max_files);
static int batch_is_pattern (const char *s);
static int batch_get_brokers_from_dir (const char *dir, char *out_brokers[], int max_brokers);
static void batch_broker_to_safe (const char *name, char *out, size_t out_len);
static int batch_mkdir_out (const char *path);
static int batch_run (void);

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

  if (batch_log_dir != NULL)
    {
      error = batch_run ();
      return error;
    }

  if (batch_split_output)
    {
      error = split_run_from_args (argc, argv, arg_start);
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

  if (mode_tran)
    error = log_top_tran (get_cnt, file_list, 0);
  else
    error = log_top_query (get_cnt, file_list, 0);

  free_file_list (file_list, file_cnt);
#else
  if (mode_tran)
    error = log_top_tran (argc, argv, arg_start);
  else
    error = log_top_query (argc, argv, arg_start);
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

#if !defined(WINDOWS)
static int
batch_match_pattern (const char *broker_prefix, const char *pattern)
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
batch_normalize_path (const char *in, char *out, size_t out_len)
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
batch_discover_log_dirs (char *out_dirs[], int max_dirs)
{
  FILE *fp;
  char conf_path[BROKER_LOG_TOP_MAX_PATH];
  char line[512];
  const char *env_conf;
  const char *cubrid;
  int count = 0;
  char norm[BROKER_LOG_TOP_MAX_PATH];
  char seen[BROKER_LOG_TOP_MAX_DIRS][BROKER_LOG_TOP_MAX_PATH];
  int nseen = 0;
  int i;

  env_conf = getenv ("CUBRID_BROKER_CONF_FILE");
  if (env_conf && env_conf[0])
    snprintf (conf_path, sizeof (conf_path), "%s", env_conf);
  else
    {
      cubrid = getenv ("CUBRID");
      if (!cubrid || !cubrid[0])
	{
	  fprintf (stderr, "Error: CUBRID environment variable not set.\n");
	  return -1;
	}
      snprintf (conf_path, sizeof (conf_path), "%s/conf/cubrid_broker.conf", cubrid);
    }

  fp = fopen (conf_path, "r");
  if (!fp)
    {
      fprintf (stderr, "Error: cannot open broker config: %s\n", conf_path);
      return -1;
    }

  while (fgets (line, sizeof (line), fp) && count < max_dirs)
    {
      char *eq, *val, *p;
      for (p = line; *p && (*p == ' ' || *p == '\t'); p++)
	;
      if (strncasecmp (p, "LOG_DIR", 7) == 0)
	{
	  p += 7;
	}
      else if (strncasecmp (p, "SLOW_LOG_DIR", 12) == 0)
	{
	  p += 12;
	}
      else
	{
	  continue;
	}
      while (*p == ' ' || *p == '\t')
	p++;
      if (*p != '=')
	continue;
      p++;
      while (*p == ' ' || *p == '\t')
	p++;
      val = p;
      while (*val && *val != '#' && *val != '\n' && *val != '\r')
	val++;
      while (val > p && (val[-1] == ' ' || val[-1] == '\t' || val[-1] == '\n' || val[-1] == '\r'))
	val--;
      *val = '\0';
      if (p[0] == '\0')
	continue;
      batch_normalize_path (p, norm, sizeof (norm));
      if (norm[0] == '\0')
	continue;
      for (i = 0; i < nseen; i++)
	if (strcmp (seen[i], norm) == 0)
	  break;
      if (i < nseen)
	continue;
      if (nseen < BROKER_LOG_TOP_MAX_DIRS)
	{
	  snprintf (seen[nseen], sizeof (seen[0]), "%.2047s", norm);
	  nseen++;
	}
      out_dirs[count] = (char *) MALLOC (strlen (norm) + 1);
      if (out_dirs[count])
	{
	  strcpy (out_dirs[count], norm);
	  count++;
	}
    }
  fclose (fp);
  return count;
}

static int
batch_is_pattern (const char *s)
{
  return strchr (s, '*') != NULL || strchr (s, '?') != NULL || strchr (s, '[') != NULL;
}

#if !defined(WINDOWS)
static int
batch_gather_files_exact (const char *broker, const char *dir, char *out_files[], int max_files)
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
      if (elen >= 8 && strcmp (e->d_name + elen - 8, ".sql.log") == 0)
	;
      else if (elen >= 12 && strcmp (e->d_name + elen - 12, ".sql.log.bak") == 0)
	;
      else if (elen >= 9 && strcmp (e->d_name + elen - 9, ".slow.log") == 0)
	;
      else
	continue;
      snprintf (path, sizeof (path), "%s/%s", dir, e->d_name);
      out_files[n] = (char *) MALLOC (strlen (path) + 1);
      if (out_files[n])
	{
	  strcpy (out_files[n], path);
	  n++;
	}
    }
  closedir (d);
  return n;
}

static int
batch_gather_files_pattern (const char *pattern, const char *dir, char *out_files[], int max_files)
{
  DIR *d;
  struct dirent *e;
  char path[BROKER_LOG_TOP_MAX_PATH];
  char broker_prefix[256];
  char *last_underscore;
  int n = 0;
  size_t elen;

  d = opendir (dir);
  if (!d)
    return 0;

  while ((e = readdir (d)) != NULL && n < max_files)
    {
      size_t copy_len;
      elen = strlen (e->d_name);
      if (elen < 9)
	continue;
      if (strcmp (e->d_name + elen - 8, ".sql.log") != 0)
	continue;
      copy_len = elen - 8;
      if (copy_len >= sizeof (broker_prefix))
	copy_len = sizeof (broker_prefix) - 1;
      memcpy (broker_prefix, e->d_name, copy_len);
      broker_prefix[copy_len] = '\0';
      last_underscore = strrchr (broker_prefix, '_');
      if (!last_underscore)
	continue;
      *last_underscore = '\0';
      if (!batch_match_pattern (broker_prefix, pattern))
	continue;
      snprintf (path, sizeof (path), "%s/%s", dir, e->d_name);
      out_files[n] = (char *) MALLOC (strlen (path) + 1);
      if (out_files[n])
	{
	  strcpy (out_files[n], path);
	  n++;
	}
    }
  closedir (d);
  return n;
}

static int
batch_gather_files_pattern_suffix (const char *pattern, const char *dir, const char *suffix, size_t suffix_len,
				   char *out_files[], int max_files)
{
  DIR *d;
  struct dirent *e;
  char path[BROKER_LOG_TOP_MAX_PATH];
  char broker_prefix[256];
  char *last_underscore;
  int n = 0;
  size_t elen;

  d = opendir (dir);
  if (!d)
    return 0;

  while ((e = readdir (d)) != NULL && n < max_files)
    {
      elen = strlen (e->d_name);
      if (elen <= suffix_len)
	continue;
      if (strcmp (e->d_name + elen - suffix_len, suffix) != 0)
	continue;
      {
	size_t copy_len = elen - suffix_len;
	if (copy_len >= sizeof (broker_prefix))
	  copy_len = sizeof (broker_prefix) - 1;
	memcpy (broker_prefix, e->d_name, copy_len);
	broker_prefix[copy_len] = '\0';
      }
      last_underscore = strrchr (broker_prefix, '_');
      if (!last_underscore)
	continue;
      *last_underscore = '\0';
      if (!batch_match_pattern (broker_prefix, pattern))
	continue;
      snprintf (path, sizeof (path), "%s/%s", dir, e->d_name);
      out_files[n] = (char *) MALLOC (strlen (path) + 1);
      if (out_files[n])
	{
	  strcpy (out_files[n], path);
	  n++;
	}
    }
  closedir (d);
  return n;
}

static int
batch_gather_files_pattern_all (const char *pattern, const char *dir, char *out_files[], int max_files)
{
  int n = 0;
  n += batch_gather_files_pattern_suffix (pattern, dir, ".sql.log", 8, out_files, max_files);
  n += batch_gather_files_pattern_suffix (pattern, dir, ".sql.log.bak", 12, out_files + n, max_files - n);
  n += batch_gather_files_pattern_suffix (pattern, dir, ".slow.log", 9, out_files + n, max_files - n);
  return n;
}
#else
static int
batch_gather_files_exact (const char *broker, const char *dir, char *out_files[], int max_files)
{
  HANDLE h;
  WIN32_FIND_DATAA fd;
  char pattern[BROKER_LOG_TOP_MAX_PATH];
  char path[BROKER_LOG_TOP_MAX_PATH];
  int n = 0;
  const char *suffixes[] = { "*.sql.log", "*.sql.log.bak", "*.slow.log", NULL };

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
	  if (out_files[n])
	    {
	      strcpy (out_files[n], path);
	      n++;
	    }
	}
      while (FindNextFileA (h, &fd) && n < max_files);
      FindClose (h);
    }
  return n;
}

static int
batch_gather_files_pattern (const char *pattern, const char *dir, char *out_files[], int max_files)
{
  HANDLE h;
  WIN32_FIND_DATAA fd;
  char search[BROKER_LOG_TOP_MAX_PATH];
  char path[BROKER_LOG_TOP_MAX_PATH];
  int n = 0;
  size_t plen = strlen (pattern);

  snprintf (search, sizeof (search), "%s\\%s_*.sql.log", dir, pattern);
  h = FindFirstFileA (search, &fd);
  if (h == INVALID_HANDLE_VALUE)
    return 0;
  do
    {
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
	continue;
      snprintf (path, sizeof (path), "%s\\%s", dir, fd.cFileName);
      out_files[n] = (char *) MALLOC (strlen (path) + 1);
      if (out_files[n])
	{
	  strcpy (out_files[n], path);
	  n++;
	}
    }
  while (FindNextFileA (h, &fd) && n < max_files);
  FindClose (h);
  return n;
}

static int
batch_gather_files_pattern_all (const char *pattern, const char *dir, char *out_files[], int max_files)
{
  int n = 0;
  n += batch_gather_files_pattern (pattern, dir, out_files, max_files);
  /* Windows: batch_gather_files_pattern only gets .sql.log; add .bak and .slow via FindFirstFile */
  {
    HANDLE h;
    WIN32_FIND_DATAA fd;
    char search[BROKER_LOG_TOP_MAX_PATH];
    char path[BROKER_LOG_TOP_MAX_PATH];
    snprintf (search, sizeof (search), "%s\\%s_*.sql.log.bak", dir, pattern);
    h = FindFirstFileA (search, &fd);
    if (h != INVALID_HANDLE_VALUE)
      {
	do
	  {
	    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
	      continue;
	    if (n >= max_files)
	      break;
	    snprintf (path, sizeof (path), "%s\\%s", dir, fd.cFileName);
	    out_files[n] = (char *) MALLOC (strlen (path) + 1);
	    if (out_files[n])
	      strcpy (out_files[n], path), n++;
	  }
	while (FindNextFileA (h, &fd));
	FindClose (h);
      }
    snprintf (search, sizeof (search), "%s\\%s_*.slow.log", dir, pattern);
    h = FindFirstFileA (search, &fd);
    if (h != INVALID_HANDLE_VALUE)
      {
	do
	  {
	    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
	      continue;
	    if (n >= max_files)
	      break;
	    snprintf (path, sizeof (path), "%s\\%s", dir, fd.cFileName);
	    out_files[n] = (char *) MALLOC (strlen (path) + 1);
	    if (out_files[n])
	      strcpy (out_files[n], path), n++;
	  }
	while (FindNextFileA (h, &fd));
	FindClose (h);
      }
  }
  return n;
}

static int
batch_get_brokers_from_dir (const char *dir, char *out_brokers[], int max_brokers)
{
  HANDLE h;
  WIN32_FIND_DATAA fd;
  char pattern[BROKER_LOG_TOP_MAX_PATH];
  char seen[256][128];
  char broker[256];
  char *last_underscore;
  size_t elen, suffix_len;
  int nseen = 0;
  int n = 0;
  int i;
  const char *patterns[] = { "*_*.sql.log", "*_*.sql.log.bak", "*_*.slow.log", NULL };

  for (i = 0; patterns[i] && n < max_brokers; i++)
    {
      snprintf (pattern, sizeof (pattern), "%s\\%s", dir, patterns[i]);
      h = FindFirstFileA (pattern, &fd);
      if (h == INVALID_HANDLE_VALUE)
	continue;
      do
	{
	  if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
	    continue;
	  elen = strlen (fd.cFileName);
	  if (strcmp (patterns[i], "*_*.sql.log") == 0)
	    suffix_len = 8;
	  else if (strcmp (patterns[i], "*_*.sql.log.bak") == 0)
	    suffix_len = 12;
	  else
	    suffix_len = 9;
	  if (elen <= suffix_len)
	    continue;
	  {
	    size_t copy_len = elen - suffix_len;
	    if (copy_len >= sizeof (broker))
	      copy_len = sizeof (broker) - 1;
	    memcpy (broker, fd.cFileName, copy_len);
	    broker[copy_len] = '\0';
	  }
	  last_underscore = strrchr (broker, '_');
	  if (!last_underscore || (size_t) (last_underscore - broker) >= sizeof (broker) - 1)
	    continue;
	  *last_underscore = '\0';
	  {
	    int j;
	    for (j = 0; j < nseen; j++)
	      if (strcmp (seen[j], broker) == 0)
		break;
	    if (j < nseen)
	      continue;
	  }
	  if (nseen < 256)
	    {
	      snprintf (seen[nseen], sizeof (seen[0]), "%.127s", broker);
	      nseen++;
	    }
	  if (n >= max_brokers)
	    continue;
	  out_brokers[n] = (char *) MALLOC (strlen (broker) + 1);
	  if (out_brokers[n])
	    {
	      strcpy (out_brokers[n], broker);
	      n++;
	    }
	}
      while (FindNextFileA (h, &fd));
      FindClose (h);
    }
  return n;
}
#endif

#if !defined(WINDOWS)
static int
batch_get_brokers_from_dir (const char *dir, char *out_brokers[], int max_brokers)
{
  DIR *d;
  struct dirent *e;
  char seen[256][128];
  int nseen = 0;
  int n = 0;
  int i;
  char broker[256];
  char *last_underscore;
  size_t suffix_len;

  d = opendir (dir);
  if (!d)
    return 0;

  while ((e = readdir (d)) != NULL)
    {
      size_t elen = strlen (e->d_name);
      if (elen < 4)
	continue;
      if (elen >= 8 && strcmp (e->d_name + elen - 8, ".sql.log") == 0)
	suffix_len = 8;
      else if (elen >= 12 && strcmp (e->d_name + elen - 12, ".sql.log.bak") == 0)
	suffix_len = 12;
      else if (elen >= 9 && strcmp (e->d_name + elen - 9, ".slow.log") == 0)
	suffix_len = 9;
      else
	continue;
      {
	size_t copy_len = elen - suffix_len;
	if (copy_len >= sizeof (broker))
	  copy_len = sizeof (broker) - 1;
	memcpy (broker, e->d_name, copy_len);
	broker[copy_len] = '\0';
      }
      last_underscore = strrchr (broker, '_');
      if (!last_underscore || (size_t) (last_underscore - broker) >= sizeof (broker) - 1)
	continue;
      *last_underscore = '\0';
      for (i = 0; i < nseen; i++)
	if (strcmp (seen[i], broker) == 0)
	  break;
      if (i < nseen)
	continue;
      if (nseen < 256)
	{
	  snprintf (seen[nseen], sizeof (seen[0]), "%.127s", broker);
	  nseen++;
	}
      if (n >= max_brokers)
	continue;
      out_brokers[n] = (char *) MALLOC (strlen (broker) + 1);
      if (out_brokers[n])
	{
	  strcpy (out_brokers[n], broker);
	  n++;
	}
    }
  closedir (d);
  return n;
}
#endif

static void
batch_broker_to_safe (const char *name, char *out, size_t out_len)
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
batch_mkdir_out (const char *path)
{
  return mkdir (path, 0755);
}
#else
static int
batch_mkdir_out (const char *path)
{
  return _mkdir (path);
}
#endif

static int
batch_run (void)
{
  char *dirs[BROKER_LOG_TOP_MAX_PATH];
  char *files[BROKER_LOG_TOP_MAX_FILES];
  char *brokers[512];
  char norm[BROKER_LOG_TOP_MAX_PATH];
  char parent_out_base[BROKER_LOG_TOP_MAX_PATH];
  char subdir[BROKER_LOG_TOP_MAX_PATH];
  char broker_safe[256];
  char cwd_buf[BROKER_LOG_TOP_MAX_PATH];
  int ndirs = 0;
  int d = 0, i = 0;
  int nfiles = 0;
  int error = 0;
  struct stat st;
  time_t now;
  struct tm tm_buf;
  const char *env_out_base;

  memset (&tm_buf, 0, sizeof (tm_buf));

  if (getcwd (cwd_buf, sizeof (cwd_buf)) == NULL)
    {
      fprintf (stderr, "Error: cannot get current directory\n");
      return -1;
    }

  /* 배치 모드는 안정성이 우선: MT_MODE 사용 시에도 단일 스레드로 실행 */
  broker_log_top_force_single_thread = 1;

  if (strcmp (batch_log_dir, ".") == 0 && batch_broker_name != NULL)
    {
      char *files[BROKER_LOG_TOP_MAX_FILES];
      int nfiles;
      int j;

      nfiles = batch_is_pattern (batch_broker_name)
	? batch_gather_files_pattern_all (batch_broker_name, cwd_buf, files, BROKER_LOG_TOP_MAX_FILES)
	: batch_gather_files_exact (batch_broker_name, cwd_buf, files, BROKER_LOG_TOP_MAX_FILES);
      if (nfiles == 0)
	{
	  fprintf (stderr, "Info: no files for broker '%s' in current directory.\n", batch_broker_name);
	  return -1;
	}
      fprintf (stdout, "%s\n", batch_broker_name);
      query_info_reset ();
      if (mode_tran)
	error = log_top_tran (nfiles, files, 0);
      else
	error = log_top_query (nfiles, files, 0);
      for (j = 0; j < nfiles; j++)
	FREE_MEM (files[j]);
      return error;
    }

  if (batch_split_output)
    {
      env_out_base = getenv ("OUT_BASE");
      if (env_out_base && env_out_base[0])
	{
	  snprintf (parent_out_base, sizeof (parent_out_base), "%s", env_out_base);
	}
      else
	{
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
	  strftime (parent_out_base, sizeof (parent_out_base), "broker_log_top_%y%m%d_%H%M", &tm_buf);
	}

      if (batch_mkdir_out (parent_out_base) < 0 && errno != EEXIST)
	{
	  fprintf (stderr, "Error: cannot create output directory: %s\n", parent_out_base);
	  return -1;
	}
    }

  if (strcasecmp (batch_log_dir, "LOG_DIR") == 0)
    {
      ndirs = batch_discover_log_dirs (dirs, BROKER_LOG_TOP_MAX_DIRS);
      if (ndirs <= 0)
	{
	  fprintf (stderr, "Error: no valid LOG_DIR/SLOW_LOG_DIR found in cubrid_broker.conf\n");
	  return -1;
	}
    }
  else
    {
      batch_normalize_path (batch_log_dir, norm, sizeof (norm));
      if (stat (norm, &st) < 0 || !S_ISDIR (st.st_mode))
	{
	  fprintf (stderr, "Error: directory does not exist: %s\n", norm);
	  return -1;
	}
      dirs[0] = (char *) MALLOC (strlen (norm) + 1);
      if (!dirs[0])
	return -1;
      strcpy (dirs[0], norm);
      ndirs = 1;
    }

  /* -O merge(기본)일 때: 출력 상위 디렉터리 없이 한 번에 분석, CWD에 결과 1세트 */
  if (!batch_split_output)
    {
      char *all_files[BROKER_LOG_TOP_MAX_FILES];
      int total_files = 0;
      int j, dir_idx;

      memset (all_files, 0, sizeof (all_files));

      if (batch_broker_name != NULL)
	{
	  for (dir_idx = 0; dir_idx < ndirs && total_files < BROKER_LOG_TOP_MAX_FILES; dir_idx++)
	    {
	      int got;
	      if (dirs[dir_idx] == NULL)
		continue;
	      got = batch_is_pattern (batch_broker_name)
		? batch_gather_files_pattern_all (batch_broker_name, dirs[dir_idx],
						  all_files + total_files,
						  BROKER_LOG_TOP_MAX_FILES - total_files)
		: batch_gather_files_exact (batch_broker_name, dirs[dir_idx],
					    all_files + total_files, BROKER_LOG_TOP_MAX_FILES - total_files);
	      total_files += got;
	    }
	}
      else
	{
	  for (dir_idx = 0; dir_idx < ndirs && total_files < BROKER_LOG_TOP_MAX_FILES; dir_idx++)
	    {
	      char *dir_brokers[512];
	      int dir_nb;
	      int bi;

	      if (dirs[dir_idx] == NULL)
		continue;

	      memset (dir_brokers, 0, sizeof (dir_brokers));
	      dir_nb = batch_get_brokers_from_dir (dirs[dir_idx], dir_brokers, 512);
	      for (bi = 0; bi < dir_nb && total_files < BROKER_LOG_TOP_MAX_FILES; bi++)
		{
		  int got;
		  if (dir_brokers[bi] == NULL)
		    continue;
		  got = batch_gather_files_exact (dir_brokers[bi], dirs[dir_idx],
						  all_files + total_files, BROKER_LOG_TOP_MAX_FILES - total_files);
		  total_files += got;
		  FREE_MEM (dir_brokers[bi]);
		  dir_brokers[bi] = NULL;
		}
	    }
	}

      if (total_files == 0)
	{
	  fprintf (stderr, "Info: no SQL log files found.\n");
	  error = -1;
	  goto batch_cleanup_dirs;
	}

      query_info_reset ();
      if (mode_tran)
	error = log_top_tran (total_files, all_files, 0);
      else
	error = log_top_query (total_files, all_files, 0);

      for (j = 0; j < total_files; j++)
	FREE_MEM (all_files[j]);

    batch_cleanup_dirs:
      if (strcasecmp (batch_log_dir, "LOG_DIR") == 0)
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
      return error;
    }

  if (batch_broker_name)
    {
      for (d = 0; d < ndirs && !error; d++)
	{
	  if (batch_is_pattern (batch_broker_name))
	    {
	      nfiles = batch_gather_files_pattern_all (batch_broker_name, dirs[d], files, BROKER_LOG_TOP_MAX_FILES);
	      if (nfiles == 0)
		{
		  fprintf (stderr, "Info: [%s] no files matching pattern '%s', skipping.\n", dirs[d],
			   batch_broker_name);
		  goto next_dir;
		}
	      batch_broker_to_safe (batch_broker_name, broker_safe, sizeof (broker_safe));
	      snprintf (subdir, sizeof (subdir), "%.1023s/%.255s", parent_out_base, broker_safe);
	      if (batch_mkdir_out (subdir) < 0 && errno != EEXIST)
		{
		  fprintf (stderr, "Error: cannot create output directory: %s\n", subdir);
		  error = -1;
		  goto next_dir;
		}
#if !defined(WINDOWS)
	      if (chdir (subdir) < 0)
#else
	      if (_chdir (subdir) < 0)
#endif
		{
		  fprintf (stderr, "Error: cannot chdir to %s\n", subdir);
		  error = -1;
		  goto next_dir;
		}
	      fprintf (stdout, "%s\n", batch_broker_name);
	      query_info_reset ();
	      if (mode_tran)
		error = log_top_tran (nfiles, files, 0);
	      else
		error = log_top_query (nfiles, files, 0);
	      chdir (cwd_buf);
	      for (i = 0; i < nfiles; i++)
		FREE_MEM (files[i]);
	    }
	  else
	    {
	      nfiles = batch_gather_files_exact (batch_broker_name, dirs[d], files, BROKER_LOG_TOP_MAX_FILES);
	      if (nfiles == 0)
		{
		  fprintf (stderr, "Info: [%s] no files for broker '%s', skipping.\n", dirs[d], batch_broker_name);
		  goto next_dir;
		}
	      snprintf (subdir, sizeof (subdir), "%.1023s/%.255s", parent_out_base, batch_broker_name);
	      if (batch_mkdir_out (subdir) < 0 && errno != EEXIST)
		{
		  fprintf (stderr, "Error: cannot create output directory: %s\n", subdir);
		  error = -1;
		  goto next_dir;
		}
#if !defined(WINDOWS)
	      if (chdir (subdir) < 0)
#else
	      if (_chdir (subdir) < 0)
#endif
		{
		  fprintf (stderr, "Error: cannot chdir to %s\n", subdir);
		  error = -1;
		  goto next_dir;
		}
	      fprintf (stdout, "%s\n", batch_broker_name);
	      query_info_reset ();
	      if (mode_tran)
		error = log_top_tran (nfiles, files, 0);
	      else
		error = log_top_query (nfiles, files, 0);
	      chdir (cwd_buf);
	      for (i = 0; i < nfiles; i++)
		FREE_MEM (files[i]);
	    }
	next_dir:
	  if (strcasecmp (batch_log_dir, "LOG_DIR") == 0)
	    {
	      FREE_MEM (dirs[d]);
	      dirs[d] = NULL;
	    }
	}
    }
  else
    {
      char *all_brokers[512];
      int total_brokers = 0;
      int bi, dir_idx, k;

      memset (all_brokers, 0, sizeof (all_brokers));

      for (dir_idx = 0; dir_idx < ndirs && total_brokers < 512; dir_idx++)
	{
	  char *dir_brokers[512];
	  int dir_nb;

	  memset (dir_brokers, 0, sizeof (dir_brokers));
	  if (dirs[dir_idx] == NULL)
	    continue;
	  dir_nb = batch_get_brokers_from_dir (dirs[dir_idx], dir_brokers, 512);

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
	}

      if (total_brokers == 0)
	{
	  fprintf (stderr, "Info: no brokers found in any directory.\n");
	  /* LOG_DIR 모드인 경우, dirs[]는 아래 정리 루틴에서 한 번에 해제된다. */
	  return 0;
	}

      for (i = 0; i < total_brokers && !error; i++)
	{
	  int total_files = 0;
	  char *all_files[BROKER_LOG_TOP_MAX_FILES];
	  int j, dir_idx;
	  int num_files = 0;

	  memset (all_files, 0, sizeof (all_files));

	  if (all_brokers[i] == NULL)
	    continue;

	  for (dir_idx = 0; dir_idx < ndirs && total_files < BROKER_LOG_TOP_MAX_FILES; dir_idx++)
	    {
	      if (dirs[dir_idx] == NULL)
		continue;
	      int dir_files = batch_gather_files_exact (all_brokers[i], dirs[dir_idx],
							all_files + total_files,
							BROKER_LOG_TOP_MAX_FILES - total_files);
	      total_files += dir_files;
	    }
	  num_files = total_files;

	  if (total_files == 0)
	    {
	      FREE_MEM (all_brokers[i]);
	      all_brokers[i] = NULL;
	      continue;
	    }

	  snprintf (subdir, sizeof (subdir), "%.1023s/%.255s", parent_out_base, all_brokers[i]);
	  if (batch_mkdir_out (subdir) < 0 && errno != EEXIST)
	    {
	      fprintf (stderr, "Error: cannot create output directory: %s\n", subdir);
	      FREE_MEM (all_brokers[i]);
	      all_brokers[i] = NULL;
	      for (j = 0; j < num_files; j++)
		FREE_MEM (all_files[j]);
	      error = -1;
	      continue;
	    }
#if !defined(WINDOWS)
	  if (chdir (subdir) < 0)
#else
	  if (_chdir (subdir) < 0)
#endif
	    {
	      fprintf (stderr, "Error: cannot chdir to %s\n", subdir);
	      FREE_MEM (all_brokers[i]);
	      all_brokers[i] = NULL;
	      for (j = 0; j < num_files; j++)
		FREE_MEM (all_files[j]);
	      error = -1;
	      continue;
	    }
	  fprintf (stdout, "%s\n", all_brokers[i]);
	  query_info_reset ();
	  if (mode_tran)
	    error = log_top_tran (total_files, all_files, 0);
	  else
	    error = log_top_query (total_files, all_files, 0);
	  chdir (cwd_buf);
	  for (j = 0; j < num_files; j++)
	    FREE_MEM (all_files[j]);
	  FREE_MEM (all_brokers[i]);
	  all_brokers[i] = NULL;
	}
    }

  if (strcasecmp (batch_log_dir, "LOG_DIR") == 0)
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

  return error;
}

/* chdir() 후에도 로그 파일을 열 수 있도록, split 모드에서는 절대 경로로 넘긴다. */
static int
split_make_absolute_path (const char *cwd, const char *path, char *out, size_t out_len)
{
#if !defined(WINDOWS)
  size_t cwd_len;
  size_t path_len;

  if (realpath (path, out) != NULL)
    {
      return 0;
    }
  path_len = strlen (path);
  if (path_len == 0 || path_len >= out_len)
    {
      return -1;
    }
  if (path[0] == '/')
    {
      memcpy (out, path, path_len + 1);
      return 0;
    }
  cwd_len = strlen (cwd);
  if (cwd_len == 0 || cwd_len + 1 + path_len >= out_len)
    {
      return -1;
    }
  memcpy (out, cwd, cwd_len);
  out[cwd_len] = '/';
  memcpy (out + cwd_len + 1, path, path_len + 1);
  return 0;
#else
  DWORD n;

  (void) cwd;
  n = GetFullPathNameA (path, (DWORD) out_len, out, NULL);
  if (n == 0 || n >= out_len)
    {
      return -1;
    }
  return 0;
#endif
}

static int
split_extract_broker_name (const char *path, char *broker, size_t broker_len)
{
  const char *fname;
  const char *suffix = NULL;
  size_t suffix_len = 0;
  size_t plen;
  char prefix[512];
  char *last_underscore;

  fname = strrchr (path, '/');
  if (fname == NULL)
    fname = strrchr (path, '\\');
  fname = (fname == NULL) ? path : (fname + 1);
  plen = strlen (fname);

  if (plen >= 8 && strcmp (fname + plen - 8, ".sql.log") == 0)
    {
      suffix = ".sql.log";
      suffix_len = 8;
    }
  else if (plen >= 12 && strcmp (fname + plen - 12, ".sql.log.bak") == 0)
    {
      suffix = ".sql.log.bak";
      suffix_len = 12;
    }
  else if (plen >= 9 && strcmp (fname + plen - 9, ".slow.log") == 0)
    {
      suffix = ".slow.log";
      suffix_len = 9;
    }
  else
    {
      return -1;
    }

  (void) suffix;
  if (plen <= suffix_len)
    return -1;
  if (plen - suffix_len >= sizeof (prefix))
    return -1;

  memcpy (prefix, fname, plen - suffix_len);
  prefix[plen - suffix_len] = '\0';
  last_underscore = strrchr (prefix, '_');
  if (last_underscore == NULL || last_underscore == prefix)
    return -1;
  *last_underscore = '\0';

  if (strlen (prefix) + 1u > broker_len)
    return -1;
  strcpy (broker, prefix);
  return 0;
}

static int
split_run_from_args (int argc, char *argv[], int arg_start)
{
  char cwd_buf[BROKER_LOG_TOP_MAX_PATH];
  char parent_out_base[BROKER_LOG_TOP_MAX_PATH];
  char subdir[BROKER_LOG_TOP_MAX_PATH];
  char broker_safe[256];
  char broker[256];
  char *brokers[512];
  char *files[BROKER_LOG_TOP_MAX_FILES];
  int nb = 0, i, j;
  int error = 0;
  time_t now;
  struct tm tm_buf;
  const char *env_out_base;

  memset (&tm_buf, 0, sizeof (tm_buf));
  memset (brokers, 0, sizeof (brokers));

  if (getcwd (cwd_buf, sizeof (cwd_buf)) == NULL)
    {
      fprintf (stderr, "Error: cannot get current directory\n");
      return -1;
    }

  for (i = arg_start; i < argc && nb < 512; i++)
    {
      int found = 0;
      int k;

      if (split_extract_broker_name (argv[i], broker, sizeof (broker)) != 0)
	continue;

      for (k = 0; k < nb; k++)
	{
	  if (brokers[k] != NULL && strcmp (brokers[k], broker) == 0)
	    {
	      found = 1;
	      break;
	    }
	}
      if (found)
	continue;

      brokers[nb] = (char *) MALLOC (strlen (broker) + 1);
      if (brokers[nb] == NULL)
	{
	  error = -1;
	  goto split_cleanup;
	}
      strcpy (brokers[nb], broker);
      nb++;
    }

  if (nb == 0)
    {
      fprintf (stderr, "Error: no valid broker log files.\n");
      return -1;
    }

  env_out_base = getenv ("OUT_BASE");
  if (env_out_base && env_out_base[0])
    {
      snprintf (parent_out_base, sizeof (parent_out_base), "%s", env_out_base);
    }
  else
    {
      now = time (NULL);
#if !defined(WINDOWS)
      if (localtime_r (&now, &tm_buf) == NULL)
#else
      if (localtime_s (&tm_buf, &now) != 0)
#endif
	{
	  fprintf (stderr, "Error: cannot get local time\n");
	  error = -1;
	  goto split_cleanup;
	}
      strftime (parent_out_base, sizeof (parent_out_base), "broker_log_top_%y%m%d_%H%M", &tm_buf);
    }

  if (batch_mkdir_out (parent_out_base) < 0 && errno != EEXIST)
    {
      fprintf (stderr, "Error: cannot create output directory: %s\n", parent_out_base);
      error = -1;
      goto split_cleanup;
    }

  for (i = 0; i < nb && !error; i++)
    {
      int nfiles = 0;
      char abs_buf[BROKER_LOG_TOP_MAX_PATH];
      int k;

      memset (files, 0, sizeof (files));

      for (j = arg_start; j < argc && nfiles < BROKER_LOG_TOP_MAX_FILES; j++)
	{
	  if (split_extract_broker_name (argv[j], broker, sizeof (broker)) != 0)
	    continue;
	  if (strcmp (broker, brokers[i]) != 0)
	    continue;
	  if (split_make_absolute_path (cwd_buf, argv[j], abs_buf, sizeof (abs_buf)) != 0)
	    {
	      fprintf (stderr, "Error: cannot resolve path: %s\n", argv[j]);
	      error = -1;
	      break;
	    }
	  files[nfiles] = (char *) MALLOC (strlen (abs_buf) + 1);
	  if (files[nfiles] == NULL)
	    {
	      fprintf (stderr, "malloc error\n");
	      error = -1;
	      break;
	    }
	  strcpy (files[nfiles], abs_buf);
	  nfiles++;
	}
      if (error != 0)
	{
	  for (k = 0; k < nfiles; k++)
	    {
	      FREE_MEM (files[k]);
	      files[k] = NULL;
	    }
	  break;
	}
      if (nfiles == 0)
	continue;

      batch_broker_to_safe (brokers[i], broker_safe, sizeof (broker_safe));
      snprintf (subdir, sizeof (subdir), "%.1023s/%.255s", parent_out_base, broker_safe);
      if (batch_mkdir_out (subdir) < 0 && errno != EEXIST)
	{
	  fprintf (stderr, "Error: cannot create output directory: %s\n", subdir);
	  error = -1;
	  for (k = 0; k < nfiles; k++)
	    {
	      FREE_MEM (files[k]);
	      files[k] = NULL;
	    }
	  break;
	}

#if !defined(WINDOWS)
      if (chdir (subdir) < 0)
#else
      if (_chdir (subdir) < 0)
#endif
	{
	  fprintf (stderr, "Error: cannot chdir to %s\n", subdir);
	  error = -1;
	  for (k = 0; k < nfiles; k++)
	    {
	      FREE_MEM (files[k]);
	      files[k] = NULL;
	    }
	  break;
	}

      query_info_reset ();
      if (mode_tran)
	error = log_top_tran (nfiles, files, 0);
      else
	error = log_top_query (nfiles, files, 0);

      for (k = 0; k < nfiles; k++)
	{
	  FREE_MEM (files[k]);
	  files[k] = NULL;
	}

#if !defined(WINDOWS)
      chdir (cwd_buf);
#else
      _chdir (cwd_buf);
#endif
    }

split_cleanup:
  for (i = 0; i < nb; i++)
    {
      if (brokers[i] != NULL)
	{
	  FREE_MEM (brokers[i]);
	  brokers[i] = NULL;
	}
    }
  return error;
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
  if (use_mt)
    {
      query_info_mutex_init ();
    }
#endif

#ifdef MT_MODE
  if (use_mt)
    {
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
  int option_index = 0;
  static struct option long_options[] = {
    {"from-conf", no_argument, 0, 'C'},
    {"output", required_argument, 0, 'O'},
    {0, 0, 0, 0}
  };

  while ((c = getopt_long (argc, argv, "tq:h:F:T:O:", long_options, &option_index)) != EOF)
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
	  from_conf_mode = 1;
	  batch_log_dir = "LOG_DIR";
	  break;
	case 'O':
	  if (optarg == NULL)
	    goto getargs_err;
	  if (strcasecmp (optarg, "merge") == 0)
	    batch_split_output = 0;
	  else if (strcasecmp (optarg, "split") == 0)
	    batch_split_output = 1;
	  else
	    {
	      fprintf (stderr, "invalid output mode: %s (use merge|split)\n", optarg);
	      goto getargs_err;
	    }
	  break;
	default:
	  goto getargs_err;
	}
    }

  if (mode_max_handle_lower_bound > 0)
    log_top_mode = MODE_MAX_HANDLE;

  if (from_conf_mode && optind < argc)
    {
      fprintf (stderr, "Error: broker name, directory, or log file arguments are not allowed with --from-conf.\n\n");
      fprintf (stderr, "Examples:\n");
      fprintf (stderr, "    %s -O merge --from-conf\n", argv[0]);
      fprintf (stderr, "    %s -O split --from-conf\n", argv[0]);
      fprintf (stderr, "    %s -F \"yy-mm-dd hh:mm:ss\" -T \"yy-mm-dd hh:mm:ss\" -O split --from-conf\n", argv[0]);
      return -1;
    }

  if (optind < argc && optind + 1 == argc)
    {
      const char *arg = argv[optind];
      size_t len = strlen (arg);
      if (strchr (arg, '/') == NULL
	  && strchr (arg, '\\') == NULL
	  && !(len >= 8 && strcmp (arg + len - 8, ".sql.log") == 0)
	  && !(len >= 12 && strcmp (arg + len - 12, ".sql.log.bak") == 0)
	  && !(len >= 9 && strcmp (arg + len - 9, ".slow.log") == 0))
	{
	  batch_broker_name = arg;
	  if (batch_log_dir == NULL)
	    batch_log_dir = ".";
	}
    }

  if (from_conf_mode || batch_log_dir != NULL)
    return optind;

  if (optind < argc)
    return optind;

  goto getargs_err;

getargs_err:
  fprintf (stderr, "%s [-t] [-F <from date>] [-T <to date>] [-O <merge | split>] <log_file> ...\n", argv[0]);
  fprintf (stderr, "or\n");
  fprintf (stderr, "%s [-t] [-F <from date>] [-T <to date>] [-O <merge | split>] <--from-conf>\n", argv[0]);
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
