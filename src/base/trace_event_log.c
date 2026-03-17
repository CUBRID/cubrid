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
 * trace_event_log.c - trace/event log module (server)
 */

#ident "$Id$"

#if defined (WINDOWS)
// TODO: fix lseek
#include <io.h>
#endif /* WINDOWS */

#include "event_log.h"
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

#define LOG_FILE_DIR "server"

#define EVENT_LOG_FILE_PREFIX "_"
#define TRACE_LOG_FILE_PREFIX "_sql_trace_"

#define EVENT_LOG_FILE_SUFFIX ".event"
#define TRACE_LOG_FILE_SUFFIX ".log"
#define LATEST_SQL_TRACE_FILE "_sql_trace.log"

static FILE *event_Fp = NULL;
static FILE *trace_Fp = NULL;

static char event_log_file_path[PATH_MAX];
static char trace_log_file_path[PATH_MAX];

static FILE *trace_event_log_init (const char *db_name, char *log_file_path, char log_type);
static FILE *trace_event_file_open (const char *path, char log_type);
static FILE *trace_event_file_backup (FILE * fp, const char *path);
static FILE *trace_event_log_start (THREAD_ENTRY * thread_p, const char *log_name, const char log_type);
static void trace_event_log_print_client_info (int tran_index, int indent, FILE * log_fp);
static void trace_event_print_client_ids_info (CLIENTIDS * client_info, int indent);
static void trace_event_log_bind_values (THREAD_ENTRY * thread_p, FILE * log_fp, int tran_index, int bind_index);

/*
 * trace_log_init - Initialize trace log module
 *   return: none
 */
void
trace_log_init (const char *db_name)
{
  trace_Fp = trace_event_log_init (db_name, trace_log_file_path, 'T');
}

/*
 * event_log_init - Initialize event log module
 *   return: none
 */
void
event_log_init (const char *db_name)
{
  event_Fp = trace_event_log_init (db_name, event_log_file_path, 'E');
}

static FILE *
trace_event_log_init (const char *db_name, char *log_file_path, char log_type)
{
  char *s, *base_db_name;
  char local_db_name[DB_MAX_IDENTIFIER_LENGTH];
  time_t log_time;
  struct tm log_tm, *log_tm_p = &log_tm;
  char log_file_name[PATH_MAX];

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
      return NULL;
    }

  log_time = time (NULL);
#if defined (SERVER_MODE) && !defined (WINDOWS)
  log_tm_p = localtime_r (&log_time, &log_tm);
#else /* SERVER_MODE && !WINDOWS */
  log_tm_p = localtime (&log_time);
#endif /* SERVER_MODE && !WINDOWS */

  if (log_tm_p == NULL)
    {
      return NULL;
    }

  if (log_type == 'E')
    {
      snprintf (log_file_name, PATH_MAX - 1, "%s%c%s%s%04d%02d%02d_%02d%02d%s", LOG_FILE_DIR, PATH_SEPARATOR,
		base_db_name, EVENT_LOG_FILE_PREFIX, log_tm_p->tm_year + 1900, log_tm_p->tm_mon + 1, log_tm_p->tm_mday,
		log_tm_p->tm_hour, log_tm_p->tm_min, EVENT_LOG_FILE_SUFFIX);
    }
  else
    {
      snprintf (log_file_name, PATH_MAX - 1, "%s%c%s%s%04d%02d%02d_%02d%02d%s", LOG_FILE_DIR, PATH_SEPARATOR,
		base_db_name, TRACE_LOG_FILE_PREFIX, log_tm_p->tm_year + 1900, log_tm_p->tm_mon + 1, log_tm_p->tm_mday,
		log_tm_p->tm_hour, log_tm_p->tm_min, TRACE_LOG_FILE_SUFFIX);
    }

  envvar_logdir_file (log_file_path, PATH_MAX, log_file_name);

  /* event log file open */
  if (log_type == 'E')
    {
      return trace_event_file_open (log_file_path, log_type);
    }

  /* trace log delays file open until it actually writes to the log. */
  return NULL;
}

/*
 * event_file_open - Open event log file
 *   return: FILE *
 *   path(in): file path
 */
static FILE *
trace_event_file_open (const char *path, char log_type)
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
	  fp = trace_event_file_backup (fp, path);
	}
    }
  else
    {
      fp = fopen (path, "w");
    }

#if !defined (WINDOWS) && defined (SERVER_MODE)
  if (fp != NULL && log_type == 'E' /* event log */ )
    {
      er_file_create_link_to_current_log_file (path, EVENT_LOG_FILE_SUFFIX);
    }
  else
    {
      er_file_create_link_to_current_log_file (path, LATEST_SQL_TRACE_FILE);
    }
#endif /* !WINDOWS && SERVER_MODE */

  return fp;
}

/*
 * event_file_backup - backup event log file
 *   return: FILE *
 *   path(in): file path
 */
static FILE *
trace_event_file_backup (FILE * fp, const char *path)
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
 * event_log_final - Terminate the event log module
 *   return: none
 */
void
event_log_final (void)
{
  if (event_Fp != NULL)
    {
      (void) fclose (event_Fp);
    }
}

/*
 * event_log_start -
 *   return: log file pointer
 *   event_name(in):
 */
FILE *
event_log_start (THREAD_ENTRY * thread_p, const char *log_name)
{
  return trace_event_log_start (thread_p, log_name, 'E');
}

/*
 * trace_log_start -
 *   return: log file pointer
 *   event_name(in):
 */
FILE *
trace_log_start (THREAD_ENTRY * thread_p, const char *log_name)
{
  return trace_event_log_start (thread_p, log_name, 'T');
}

static FILE *
trace_event_log_start (THREAD_ENTRY * thread_p, const char *log_name, const char log_type)
{
  time_t er_time;
#if defined (SERVER_MODE) && !defined (WINDOWS)
  struct tm er_tm;
#endif
  struct tm *er_tm_p = NULL;
  struct timeval tv;
  char time_array[256];
  const char *log_file_name;
  int csect;
  FILE *log_Fp;

  if (log_type == 'E')
    {
      csect = CSECT_EVENT_LOG_FILE;
      log_Fp = event_Fp;
      log_file_name = event_log_file_path;
    }
  else
    {
      csect = CSECT_TRACE_LOG_FILE;
      log_Fp = trace_Fp;
      log_file_name = trace_log_file_path;
    }

  csect_enter (thread_p, csect, INF_WAIT);
  /* If file is not exist, it will recreate *log_fh file. */
  if (log_Fp == NULL || access (log_file_name, F_OK) == -1)
    {
      if (log_Fp != NULL)
	{
	  (void) fclose (log_Fp);
	}

      log_Fp = trace_event_file_open (log_file_name, log_type);
      if (log_Fp == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  csect_exit (thread_p, csect);
	  return NULL;
	}
    }
  else if (ftell (log_Fp) > prm_get_integer_value (PRM_ID_ER_LOG_SIZE))
    {
      (void) fflush (log_Fp);

      log_Fp = trace_event_file_backup (log_Fp, log_file_name);
      if (log_Fp == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  csect_exit (thread_p, csect);
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

  fprintf (log_Fp, "%s - %s\n", time_array, log_name);

  if (log_type == 'E')
    {
      event_Fp = log_Fp;
    }
  else
    {
      trace_Fp = log_Fp;
    }

  return log_Fp;
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
 * event_log_end -
 *   return:
 */
void
event_log_end (THREAD_ENTRY * thread_p)
{
  assert (csect_check_own (thread_p, CSECT_EVENT_LOG_FILE) == 1);

  if (event_Fp == NULL)
    {
      return;
    }

  fflush (event_Fp);
  csect_exit (thread_p, CSECT_EVENT_LOG_FILE);
}

/*
 * event_log_print_client_ids_info -
 *   return:
 */
static void
event_log_print_client_ids_info (CLIENTIDS * client_info, int indent)
{
  if (event_Fp == NULL)
    {
      return;
    }

  if (client_info->client_type < 0)
    {
      return;
    }

  if (indent > 0)
    {
      fprintf (event_Fp, "%*c", indent, ' ');
    }
  fprintf (event_Fp, "client: %s@%s|%s(%d)\n", client_info->get_db_user (), client_info->get_host_name (),
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
  trace_event_log_print_client_info (tran_index, indent, trace_Fp);
}

/*
 * event_log_print_client_info -
 *   return:
 *   tran_index(in):
 *   indent(in):
 */
void
event_log_print_client_info (int tran_index, int indent)
{
  trace_event_log_print_client_info (tran_index, indent, event_Fp);
}

static void
trace_event_log_print_client_info (int tran_index, int indent, FILE * log_Fp)
{
  const char *prog, *user, *host;
  int pid;

  if (log_Fp == NULL)
    {
      return;
    }

  logtb_find_client_name_host_pid (tran_index, &prog, &user, &host, &pid);

  if (indent > 0)
    {
      fprintf (log_Fp, "%*c", indent, ' ');
    }
  fprintf (log_Fp, "tran index: %d\n", tran_index);
  if (indent > 0)
    {
      fprintf (log_Fp, "%*c", indent, ' ');
    }
  fprintf (log_Fp, "client: %s@%s|%s(%d)\n", user, host, prog, pid);
}

/*
 * event_log_sql_without_user_oid
 *   print sql without user oid for event log
 *   return: none
*/
void
event_log_sql_without_user_oid (FILE * fp, const char *format, int indent, const char *hash_text)
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
 * event_log_sql_string -
 *   return:
 *   thread_p(in):
 *   log_fp(in):
 *   xasl_id(in):
 */
void
event_log_sql_string (THREAD_ENTRY * thread_p, FILE * log_fp, XASL_ID * xasl_id, int indent)
{
  XASL_CACHE_ENTRY *ent = NULL;

  assert (csect_check_own (thread_p, CSECT_EVENT_LOG_FILE) == 1);

  fprintf (log_fp, "%*csql: ", indent, ' ');

  if (XASL_ID_IS_NULL (xasl_id))
    {
      fprintf (log_fp, "%s\n", EVENT_EMPTY_QUERY);
      return;
    }

  if (xcache_find_sha1 (thread_p, &xasl_id->sha1, XASL_CACHE_SEARCH_GENERIC, &ent, NULL) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return;
    }

  if (ent != NULL && ent->sql_info.sql_hash_text != NULL)
    {
      event_log_sql_without_user_oid (log_fp, NULL, 0, ent->sql_info.sql_hash_text);
    }
  else
    {
      fprintf (log_fp, "%s\n", EVENT_EMPTY_QUERY);
    }

  if (ent != NULL)
    {
      xcache_unfix (thread_p, ent);
    }
}

/*
 * event_log_bind_values -
 *   return:
 *   log_fp(in):
 *   tran_index(in):
 *   bind_index(in):
 */
void
event_log_bind_values (THREAD_ENTRY * thread_p, FILE * log_fp, int tran_index, int bind_index)
{
  trace_event_log_bind_values (thread_p, log_fp, tran_index, bind_index);
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
  trace_event_log_bind_values (thread_p, log_fp, tran_index, bind_index);
}

static void
trace_event_log_bind_values (THREAD_ENTRY * thread_p, FILE * log_fp, int tran_index, int bind_index)
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

/*
 * event_log_log_flush_thr_wait -
 *   return:
 *   thread_p(in):
 *   flush_count(in): the num of flushed pages
 *   client_info(in): last log writer client info
 *   flush_time(in): total time spent for flushing
 *   flush_wait_time(in): time waiting for LWT to be finished
 *   writer_time(in): time spent by last LWT (normally same as flush wait time)
 */
void
event_log_log_flush_thr_wait (THREAD_ENTRY * thread_p, int flush_count, clientids * client_info, int flush_time,
			      int flush_wait_time, int writer_time)
{
  FILE *log_fp;
  int indent = 2;

  log_fp = event_log_start (thread_p, "LOG_FLUSH_THREAD_WAIT");
  if (log_fp == NULL)
    {
      return;
    }

  fprintf (log_fp, "%*ctotal flush count: %d page(s)\n", indent, ' ', flush_count);
  fprintf (log_fp, "%*ctotal flush time: %d ms\n", indent, ' ', flush_time);
  fprintf (log_fp, "%*ctime waiting for log writer: %d ms\n", indent, ' ', flush_wait_time);
  fprintf (log_fp, "%*clast log writer info\n", indent, ' ');

  indent *= 2;
  event_log_print_client_ids_info (client_info, indent);
  fprintf (log_fp, "%*ctime spent by last log writer: %d ms\n", indent, ' ', writer_time);

  event_log_end (thread_p);
}
