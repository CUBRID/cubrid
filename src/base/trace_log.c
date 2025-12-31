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
 * trace_log.c - trace log module (server)
 */

#ident "$Id$"

#if defined (WINDOWS)
// TODO: fix lseek
#include <io.h>
#endif /* WINDOWS */

#include "trace_log.h"

#include "config.h"
#include "critical_section.h"
#include "dbtype.h"
#include "error_manager.h"
#include "environment_variable.h"
#include "log_impl.h"
#include "query_executor.h"
#include "object_primitive.h"
#include "porting.h"
#include "system_parameter.h"
#include "xasl_cache.h"

#include <assert.h>

#if defined (WINDOWS)
#include <process.h>
#endif /* WINDOWS */
#include <stdio.h>
#if !defined (WINDOWS)
#include <sys/time.h>
#endif /* WINDOWS */
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define TRACE_LOG_FILE_DIR "server"
#define TRACE_LOG_FILE_SUFFIX ".log"

static FILE *trace_Fp = NULL;
static char trace_log_file_path[PATH_MAX];

static FILE *trace_file_open (const char *path);
static FILE *trace_file_backup (FILE * fp, const char *path);
static void trace_log_print_client_ids_info (CLIENTIDS * client_info, int indent);

/*
 * trace_init - Initialize trace log module
 *   return: none
 */
void
trace_log_init (const char *db_name)
{
  char *s, *base_db_name;
  char local_db_name[DB_MAX_IDENTIFIER_LENGTH];
  time_t log_time;
  struct tm log_tm, *log_tm_p = &log_tm;
  char trace_log_name[PATH_MAX];

  assert (db_name != NULL);

  strncpy (local_db_name, db_name, DB_MAX_IDENTIFIER_LENGTH);
  local_db_name[DB_MAX_IDENTIFIER_LENGTH - 1] = '\0';
  s = strchr (local_db_name, '@');
  if (s)
    {
      *s = '\0';
    }

  base_db_name = basename ((char *) local_db_name);
  if (base_db_name == NULL)
    {
      return;
    }

  log_time = time (NULL);
#if defined (SERVER_MODE) && !defined (WINDOWS)
  log_tm_p = localtime_r (&log_time, &log_tm);
#else /* SERVER_MODE && !WINDOWS */
  log_tm_p = localtime (&log_time);
#endif /* SERVER_MODE && !WINDOWS */

  if (log_tm_p == NULL)
    {
      return;
    }

  snprintf (trace_log_name, PATH_MAX - 1, "%s%c%s_sql_trace_%04d%02d%02d_%02d%02d%s", TRACE_LOG_FILE_DIR,
	    PATH_SEPARATOR, base_db_name, log_tm_p->tm_year + 1900, log_tm_p->tm_mon + 1, log_tm_p->tm_mday,
	    log_tm_p->tm_hour, log_tm_p->tm_min, TRACE_LOG_FILE_SUFFIX);

  envvar_logdir_file (trace_log_file_path, PATH_MAX, trace_log_name);
  trace_Fp = trace_file_open (trace_log_file_path);
}

/*
 * trace_file_open - Open trace log file
 *   return: FILE *
 *   path(in): file path
 */
static FILE *
trace_file_open (const char *path)
{
  FILE *fp;
  char dir[PATH_MAX], *tpath;

  assert (path != NULL);

  tpath = strdup (path);
  if (tpath == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) strlen (path));
      return NULL;
    }

  while (1)
    {
      if (cub_dirname_r (tpath, dir, PATH_MAX) > 0 && access (dir, F_OK) < 0)
	{
	  if (mkdir (dir, 0777) < 0 && errno == ENOENT)
	    {
	      free_and_init (tpath);

	      tpath = strdup (dir);
	      if (tpath == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) strlen (dir));
		  return NULL;
		}

	      continue;
	    }
	}

      break;
    }

  free_and_init (tpath);

  fp = fopen (path, "r+");
  if (fp != NULL)
    {
      fseek (fp, 0, SEEK_END);
      if (ftell (fp) > prm_get_integer_value (PRM_ID_ER_LOG_SIZE))
	{
	  fp = trace_file_backup (fp, path);
	}
    }
  else
    {
      fp = fopen (path, "w");
    }

#if !defined (WINDOWS) && defined (SERVER_MODE)
  if (fp != NULL)
    {
      er_file_create_link_to_current_log_file (path, TRACE_LOG_FILE_SUFFIX);
    }
#endif /* !WINDOWS && SERVER_MODE */

  return fp;
}

/*
 * trace_file_backup - backup trace log file
 *   return: FILE *
 *   path(in): file path
 */
static FILE *
trace_file_backup (FILE * fp, const char *path)
{
  char backup_file[PATH_MAX];

  assert (fp != NULL);
  assert (path != NULL);

  fclose (fp);
  if (snprintf (backup_file, PATH_MAX, "%s.bak", path) >= PATH_MAX)
    {
      assert_release (false);
      backup_file[PATH_MAX - 1] = '\0';
    }
  (void) unlink (backup_file);
  (void) rename (path, backup_file);

  return fopen (path, "w");
}

/*
 * trace_log_final - Terminate the trace log module
 *   return: none
 */
void
trace_log_final (void)
{
  if (trace_Fp != NULL)
    {
      (void) fclose (trace_Fp);
    }
}

/*
 * trace_log_start -
 *   return: log file pointer
 *   trace_name(in):
 */
FILE *
trace_log_start (THREAD_ENTRY * thread_p, const char *trace_name)
{
  time_t er_time;
#if defined (SERVER_MODE) && !defined (WINDOWS)
  struct tm er_tm;
#endif
  struct tm *er_tm_p = NULL;
  struct timeval tv;
  char time_array[256];
  const char *log_file_name = trace_log_file_path;

  csect_enter (thread_p, CSECT_TRACE_LOG_FILE, INF_WAIT);

  /* If file is not exist, it will recreate *log_fh file. */
  if (trace_Fp == NULL || access (log_file_name, F_OK) == -1)
    {
      if (trace_Fp != NULL)
	{
	  (void) fclose (trace_Fp);
	}

      trace_Fp = trace_file_open (log_file_name);
      if (trace_Fp == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  csect_exit (thread_p, CSECT_TRACE_LOG_FILE);
	  return NULL;
	}
    }
  else if (ftell (trace_Fp) > prm_get_integer_value (PRM_ID_ER_LOG_SIZE))
    {
      (void) fflush (trace_Fp);

      trace_Fp = trace_file_backup (trace_Fp, log_file_name);
      if (trace_Fp == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  csect_exit (thread_p, CSECT_TRACE_LOG_FILE);
	  return NULL;
	}
    }

  er_time = time (NULL);
#if defined (SERVER_MODE) && !defined (WINDOWS)
  er_tm_p = localtime_r (&er_time, &er_tm);
#else /* SERVER_MODE && !WINDOWS */
  er_tm_p = localtime (&er_time);
#endif /* SERVER_MODE && !WINDOWS */

  if (er_tm_p == NULL)
    {
      strcpy (time_array, "00/00/00 00:00:00.000");
    }
  else
    {
      gettimeofday (&tv, NULL);
      snprintf (time_array + strftime (time_array, 128, "%m/%d/%y %H:%M:%S", er_tm_p), 255, ".%03ld",
		tv.tv_usec / 1000);
    }

  fprintf (trace_Fp, "%s - %s\n", time_array, trace_name);

  return trace_Fp;
}

/*
 * trace_log_end -
 *   return:
 */
void
trace_log_end (THREAD_ENTRY * thread_p)
{
  assert (csect_check_own (thread_p, CSECT_TRACE_LOG_FILE) == 1);

  if (trace_Fp == NULL)
    {
      return;
    }

  fflush (trace_Fp);
  csect_exit (thread_p, CSECT_TRACE_LOG_FILE);
}

/*
 * trace_log_print_client_ids_info -
 *   return:
 */
static void
trace_log_print_client_ids_info (CLIENTIDS * client_info, int indent)
{
  if (trace_Fp == NULL)
    {
      return;
    }

  if (client_info->client_type < 0)
    {
      return;
    }

  if (indent > 0)
    {
      fprintf (trace_Fp, "%*c", indent, ' ');
    }
  fprintf (trace_Fp, "client: %s@%s|%s(%d)\n", client_info->get_db_user (), client_info->get_host_name (),
	   client_info->get_program_name (), client_info->process_id);
}

/*
 * trace_log_print_client_info -
 *   return:
 *   tran_index(in):
 *   indent(in):
 */
void
trace_log_print_client_info (int tran_index, int indent)
{
  const char *prog, *user, *host;
  int pid;

  if (trace_Fp == NULL)
    {
      return;
    }

  logtb_find_client_name_host_pid (tran_index, &prog, &user, &host, &pid);

  if (indent > 0)
    {
      fprintf (trace_Fp, "%*c", indent, ' ');
    }
  fprintf (trace_Fp, "tran index: %d\n", tran_index);
  if (indent > 0)
    {
      fprintf (trace_Fp, "%*c", indent, ' ');
    }
  fprintf (trace_Fp, "client: %s@%s|%s(%d)\n", user, host, prog, pid);
}

/*
 * trace_log_sql_without_user_oid
 *   print sql without user oid for trace log
 *   return: none
*/
void
trace_log_sql_without_user_oid (FILE * fp, const char *format, int indent, const char *hash_text)
{
  /* start from user=0|0|0, length = 10 */
  int i, start = strlen (hash_text) - 10;
  char *k;

  for (i = start; i >= 0; i--)
    {
      if ((k = strstr ((char *) hash_text, "user=")) != NULL)
	{
	  /* cut the hash_text to exclude "user=" */
	  *k = 0;
	  break;
	}
    }

  if (format)
    {
      fprintf (fp, format, indent, ' ', hash_text);
    }
  else
    {
      fprintf (fp, "%s\n", hash_text);
    }

  /* if "user=" was found then restore it */
  if (k != NULL)
    {
      *k = 'u';
    }
}

/*
 * trace_log_bind_values -
 *   return:
 *   log_fp(in):
 *   tran_index(in):
 *   bind_index(in):
 */
void
trace_log_bind_values (THREAD_ENTRY * thread_p, FILE * log_fp, int tran_index, int bind_index)
{
  LOG_TDES *tdes;
  int i, indent = 2;
  char *val_str;

  if (bind_index < 0)
    {
      return;
    }

  tdes = LOG_FIND_TDES (tran_index);

  if (tdes == NULL || tdes->bind_history[bind_index].vals == NULL)
    {
      return;
    }

  for (i = 0; i < tdes->bind_history[bind_index].size; i++)
    {
      val_str = pr_valstring (&tdes->bind_history[bind_index].vals[i]);
      fprintf (log_fp, "%*cbind: %s\n", indent, ' ', (val_str == NULL) ? "(null)" : val_str);

      if (val_str != NULL)
	{
	  db_private_free (thread_p, val_str);
	}
    }
}
