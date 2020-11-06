/*
 * Copyright (C) 2008 Search Solution Corporation
 * Copyright (C) 2016 CUBRID Corporation
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
 * cas_log.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#if defined(WINDOWS)
#include <sys/timeb.h>
#include <process.h>
#include <io.h>
#else
#include <unistd.h>
#include <sys/time.h>
#include <sys/timeb.h>
#endif
#include <assert.h>

#include "porting.h"
#include "cas_common.h"
#include "cub_ddl_log.h"
#include "parse_tree.h"
#include "system_parameter.h"
#include "environment_variable.h"
#include "broker_config.h"

#define DDL_LOG_BUFFER_SIZE         (8192)
#define DDL_LOG_MSG 	            (256)
#define DDL_LOG_PATH    	    "log/ddl_audit"
#define DDL_LOG_LOADDB_FILE_PATH    "log/ddl_audit/loaddb"
#define FILE_BUFFER_SIZE            (1024)
#define TIME_STRING_SIZE            (16)
#define DDL_APP_NAME_LEN            (64)
#define DDL_TIME_LEN                (32)


typedef struct t_ddl_audit_handle T_DDL_AUDIT_HANDLE;
struct t_ddl_audit_handle
{
  char db_name[DB_MAX_IDENTIFIER_LENGTH];
  char user_name[DB_MAX_USER_LENGTH];
  char app_name[DDL_APP_NAME_LEN];
  char br_name[BROKER_NAME_LEN];
  int br_index;
  char ip_addr[16];
  int pid;
  char *sql_text;
  char stmt_type;
  char execute_start_time[DDL_TIME_LEN];
  char elapsed_time[DDL_TIME_LEN];
  int file_line_number;
  int err_code;
  char *err_msg;
  //char result;
  char msg[DDL_LOG_MSG];
  char file_name[PATH_MAX];
  char schema_file[PATH_MAX];
  char log_type;
  char loaddb_file_type;
  char log_filepath[PATH_MAX];
  int commit_count;
};
static T_DDL_AUDIT_HANDLE *ddl_audit_handle = NULL;

static int cub_make_ddl_log_filename (char *filename_buf, size_t buf_size, const char *app_name);
static int cub_make_schema_file_name (const char *file_full_path, char *dest_path, size_t buf_size);
static void cub_ddl_log_backup (const char *path);
#if defined(WINDOWS)
static void unix_style_path (char *path);
#endif /* WINDOWS */
static int cub_create_dir_log (const char *new_dir);
static int cub_create_log_mgs (char *msg);
static int cub_get_current_time (char *buf, size_t size);
static int cub_file_copy (char *src_file, char *dest_file);
static void cub_remove_char (char *string, char ch);
static int cub_is_ddl_type (int node_type);
static FILE *cub_ddl_log_open (char *app_name);
static int cub_get_time_string (char *buf, struct timeval *time_val);
static FILE *cub_fopen_and_lock (const char *path, const char *mode);

void
cub_ddl_log_init ()
{
  if (ddl_audit_handle != NULL)
    {
      FREE_MEM (ddl_audit_handle->sql_text);
      FREE_MEM (ddl_audit_handle->err_msg);
      FREE_MEM (ddl_audit_handle);
    }

  ddl_audit_handle = (T_DDL_AUDIT_HANDLE *) MALLOC (sizeof (T_DDL_AUDIT_HANDLE));
  if (ddl_audit_handle == NULL)
    {
      return;
    }

  memset (ddl_audit_handle, 0x00, sizeof (T_DDL_AUDIT_HANDLE));

  ddl_audit_handle->stmt_type = -1;
  ddl_audit_handle->log_type = DDL_LOG_ELAPSED_TIME;
  ddl_audit_handle->loaddb_file_type = LOADDB_FILE_TYPE_NONE;
}

void
cub_ddl_log_free ()
{
  if (ddl_audit_handle == NULL)
    {
      cub_ddl_log_init ();
      return;
    }

  FREE_MEM (ddl_audit_handle->sql_text);
  FREE_MEM (ddl_audit_handle->err_msg);

  memset (ddl_audit_handle, 0x00, sizeof (T_DDL_AUDIT_HANDLE));

  ddl_audit_handle->stmt_type = -1;
  ddl_audit_handle->log_type = DDL_LOG_ELAPSED_TIME;
  ddl_audit_handle->loaddb_file_type = LOADDB_FILE_TYPE_NONE;
}

void
cub_ddl_log_destroy ()
{
  if (ddl_audit_handle != NULL)
    {
      FREE_MEM (ddl_audit_handle->sql_text);
      FREE_MEM (ddl_audit_handle->err_msg);
      FREE_MEM (ddl_audit_handle);
    }
}

void
cub_ddl_log_app_name (const char *app_name)
{
  if (ddl_audit_handle == NULL || app_name == NULL)
    {
      return;
    }

  snprintf (ddl_audit_handle->app_name, sizeof (ddl_audit_handle->app_name), app_name);
}

void
cub_ddl_log_db_name (const char *db_name)
{
  char *pstr = NULL;
  if (ddl_audit_handle == NULL || db_name == NULL)
    {
      return;
    }

  snprintf (ddl_audit_handle->db_name, sizeof (ddl_audit_handle->db_name), db_name);

  pstr = (char *) strchr (ddl_audit_handle->db_name, '@');
  if (pstr != NULL)
    {
      *pstr = '\0';
    }
}

void
cub_ddl_log_user_name (const char *user_name)
{
  if (ddl_audit_handle == NULL || user_name == NULL)
    {
      return;
    }

  snprintf (ddl_audit_handle->user_name, sizeof (ddl_audit_handle->user_name), user_name);
}

void
cub_ddl_log_ip (const char *ip_addr)
{
  if (ddl_audit_handle == NULL || ip_addr == NULL)
    {
      return;
    }

  snprintf (ddl_audit_handle->ip_addr, sizeof (ddl_audit_handle->ip_addr), ip_addr);
}

void
cub_ddl_log_pid (const int pid)
{
  if (ddl_audit_handle == NULL)
    {
      return;
    }

  ddl_audit_handle->pid = pid;
}

void
cub_ddl_log_br_name (const char *br_name)
{
  if (ddl_audit_handle == NULL || br_name == NULL)
    {
      return;
    }
  snprintf (ddl_audit_handle->br_name, BROKER_NAME_LEN, br_name);
}

void
cub_ddl_log_br_index (const int index)
{
  if (ddl_audit_handle == NULL)
    {
      return;
    }
  ddl_audit_handle->br_index = index;
}

void
cub_ddl_log_sql_text (char *sql_text)
{
  if (ddl_audit_handle == NULL || sql_text == NULL)
    {
      return;
    }

  if (ddl_audit_handle->sql_text != NULL)
    {
      FREE_MEM (ddl_audit_handle->sql_text);
    }

  ALLOC_COPY (ddl_audit_handle->sql_text, sql_text);
}


void
cub_ddl_log_sql_text (char *sql_text, int len)
{
  if (ddl_audit_handle == NULL || sql_text == NULL || len < 0)
    {
      return;
    }

  if (ddl_audit_handle->sql_text != NULL)
    {
      FREE_MEM (ddl_audit_handle->sql_text);
    }

  ddl_audit_handle->sql_text = (char *) MALLOC (len);

  if (ddl_audit_handle->sql_text != NULL)
    {
      strncpy (ddl_audit_handle->sql_text, sql_text, len);
      ddl_audit_handle->sql_text[len - 1] = '\0';
    }
}


void
cub_ddl_log_stmt_type (int stmt_type)
{
  if (ddl_audit_handle == NULL)
    {
      return;
    }

  ddl_audit_handle->stmt_type = stmt_type;
}

void
cub_ddl_log_loaddb_file_type (char file_type)
{
  if (ddl_audit_handle == NULL)
    {
      return;
    }

  ddl_audit_handle->loaddb_file_type = file_type;
}

void
cub_ddl_log_file_name (const char *file_name)
{
  if (ddl_audit_handle == NULL || file_name == NULL)
    {
      return;
    }

  strncpy (ddl_audit_handle->file_name, file_name, PATH_MAX);
}

void
cub_ddl_log_file_line (int file_line)
{
  if (ddl_audit_handle == NULL)
    {
      return;
    }

  ddl_audit_handle->file_line_number = file_line;
}

void
cub_ddl_log_err_msg (char *msg)
{
  if (ddl_audit_handle == NULL || msg == NULL)
    {
      return;
    }

  if (ddl_audit_handle->err_msg != NULL)
    {
      FREE_MEM (ddl_audit_handle->err_msg);
    }

  ALLOC_COPY (ddl_audit_handle->err_msg, msg);
  cub_remove_char (ddl_audit_handle->err_msg, '\n');
}

void
cub_ddl_log_err_code (int err_code)
{
  if (ddl_audit_handle == NULL)
    {
      return;
    }

  ddl_audit_handle->err_code = err_code;
}

void
cub_ddl_log_start_time (struct timeval *time_val)
{
  if (ddl_audit_handle == NULL)
    {
      return;
    }

  ddl_audit_handle->execute_start_time[0] = '\0';
  cub_get_time_string (ddl_audit_handle->execute_start_time, time_val);
}

void
cub_ddl_log_elapsed_time (long sec, long msec)
{
  if (ddl_audit_handle == NULL)
    {
      return;
    }

  snprintf (ddl_audit_handle->elapsed_time, 20, "elapsed time %ld.%03ld", sec, msec);
}

void
cub_ddl_log_msg (const char *fmt, ...)
{
  va_list args;
  if (ddl_audit_handle == NULL)
    {
      return;
    }

  va_start (args, fmt);
  vsnprintf (ddl_audit_handle->msg, DDL_LOG_MSG, fmt, args);
  va_end (args);
}

void
cub_ddl_log_type (char type)
{
  if (ddl_audit_handle == NULL)
    {
      return;
    }

  ddl_audit_handle->log_type = type;
}

void 
cub_ddl_log_commit_count (int count)
{
  if (ddl_audit_handle == NULL)
    {
      return;
    }
  ddl_audit_handle->commit_count = count;
}

void
cub_ddl_log_execute_result (T_SRV_HANDLE * srv_handle)
{
  if (srv_handle == NULL)
    {
      return;
    }

  for (int i = 0; i < srv_handle->num_q_result; i++)
    {
      if (cub_is_ddl_type (srv_handle->q_result[i].stmt_type) == TRUE)
	{
	  cub_ddl_log_stmt_type (srv_handle->q_result[i].stmt_type);
	  break;
	}
      else
	{
	  cub_ddl_log_stmt_type (srv_handle->q_result[i].stmt_type);
	}
    }
}

static int
cub_file_copy (char *src_file, char *dest_file)
{
  char buf[FILE_BUFFER_SIZE] = { 0 };
  size_t size;
  int retval = 0;

  if (src_file == NULL || dest_file == NULL)
    {
      return -1;
    }

  FILE *fsource = fopen (src_file, "r");
  if (fsource == NULL)
    {
      return -1;
    }

  FILE *fdest = fopen (dest_file, "w");
  if (fdest == NULL)
    {
      if (fsource != NULL)
	{
	  fclose (fsource);
	}
      return -1;
    }

  while (retval == 0)
    {
      size = fread (buf, 1, FILE_BUFFER_SIZE, fsource);

      if (size < 1 || size > FILE_BUFFER_SIZE)
	{
	  if (feof (fsource))
	    {
	      break;
	    }
	  else
	    {
	      retval = ferror (fsource);
	    }
	}
      else
	{
	  if (fwrite (buf, 1, size, fdest) != size)
	    {
	      retval = ferror (fdest);
	      break;
	    }
	}
    }

  if (fsource != NULL)
    {
      fclose (fsource);
    }

  if (fdest != NULL)
    {
      fclose (fdest);
    }

  return retval;
}

static int
cub_make_schema_file_name (const char *file_full_path, char *dest_path, size_t buf_size)
{
  const char *env_root = NULL;
  char time[TIME_STRING_SIZE] = { 0 };
  const char *name_tmp = NULL;
  char *tpath = NULL;
  int retval = 0;

  if (ddl_audit_handle == NULL || file_full_path == NULL || dest_path == NULL || buf_size < 0)
    {
      return -1;
    }

  env_root = envvar_root ();
  cub_get_current_time (time, TIME_STRING_SIZE);

  name_tmp = strrchr (file_full_path, PATH_SEPARATOR);

#if defined(WINDOWS)
  {
    const char *nn_tmp = strrchr (file_full_path, PATH_SEPARATOR);
    if (name_tmp < nn_tmp)
      {
	name_tmp = nn_tmp;
      }
  }
#endif /* WINDOWS */
  if (name_tmp == NULL)
    {
      name_tmp = file_full_path;
    }
  else
    {
      name_tmp++;
    }

  retval = snprintf (ddl_audit_handle->schema_file, buf_size, "%s_%s_%d", name_tmp, time, getpid ());
  if (retval < 0)
    {
      assert (false);
      ddl_audit_handle->schema_file[0] = '\0';
      return retval;
    }

  retval =
    snprintf (dest_path, buf_size, "%s/%s/%s_%s_%d", env_root, DDL_LOG_LOADDB_FILE_PATH, name_tmp, time, getpid ());

  if (retval < 0)
    {
      assert (false);
      dest_path[0] = '\0';
      return retval;
    }
#if defined(WINDOWS)
  unix_style_path (dest_path);
#endif

  retval = cub_create_dir_log (dest_path);

  return retval;
}

static int
cub_make_ddl_log_filename (char *filename_buf, size_t buf_size, const char *app_name)
{
  const char *env_root = NULL;
  int retval = 0;

  assert (filename_buf != NULL);

  env_root = envvar_root ();

  if (strcmp (app_name, "cas") == 0)
    {
      retval =
	snprintf (filename_buf, buf_size, "%s/%s/%s_%d.ddl.log", env_root, DDL_LOG_PATH, ddl_audit_handle->br_name,
		  (ddl_audit_handle->br_index + 1));
    }
  else if (strcmp (app_name, "csql") == 0 || strcmp (app_name, "loaddb") == 0)
    {
      retval =
	snprintf (filename_buf, buf_size, "%s/%s/%s_%s_ddl.log", env_root, DDL_LOG_PATH, app_name,
		  ddl_audit_handle->db_name);
    }
  else
    {
      filename_buf[0] = '\0';
      return -1;
    }

  if (retval < 0)
    {
      assert (false);
      filename_buf[0] = '\0';
    }
#if defined(WINDOWS)
  unix_style_path (filename_buf);
#endif
  return retval;
}

static FILE *
cub_ddl_log_open (char *app_name)
{
  FILE *fp = NULL;
  char *tpath = NULL;
  int len;

  if (app_name != NULL)
    {
      len = cub_make_ddl_log_filename (ddl_audit_handle->log_filepath, PATH_MAX, app_name);

      if (ddl_audit_handle->log_filepath[0] == '\0' || len < 0)
	{
	  goto file_error;
	}

      if (cub_create_dir_log (ddl_audit_handle->log_filepath) < 0)
	{
	  goto file_error;
	}
    }
  else
    {
      goto file_error;
    }

  /* note: in "a+" mode, output is always appended */
  fp = cub_fopen_and_lock (ddl_audit_handle->log_filepath, "a+");

  return fp;

file_error:
  return NULL;
}

void
cub_ddl_log_write_end ()
{
  if (ddl_audit_handle == NULL)
    {
      return;
    }

  if (prm_get_bool_value (PRM_ID_DDL_AUDIT_LOG) == FALSE)
    {
      goto ddl_log_free;
    }

  if (strcmp (ddl_audit_handle->app_name, "loaddb") == 0)
    {
      if (ddl_audit_handle->loaddb_file_type != LOADDB_FILE_TYPE_SCHEMA)
	{
	  goto ddl_log_free;
	}
    }
  else
    {
      if (cub_is_ddl_type (ddl_audit_handle->stmt_type) == FALSE)
	{
	  goto ddl_log_free;
	}
    }

  if (ddl_audit_handle->log_type & DDL_LOG_ELAPSED_TIME)
    {
      cub_ddl_log_write ();
    }

ddl_log_free:
  cub_ddl_log_free ();
}

void
cub_ddl_log_write ()
{
  FILE *fp = NULL;
  char buf[DDL_LOG_BUFFER_SIZE] = { 0 };
  char dest_path[PATH_MAX] = { 0 };
  int len = 0;
  int ret = 0;

  if (ddl_audit_handle == NULL)
    {
      return;
    }

  if (prm_get_bool_value (PRM_ID_DDL_AUDIT_LOG) == FALSE)
    {
      return;
    }

  if (strcmp (ddl_audit_handle->app_name, "loaddb") == 0)
    {
      if (ddl_audit_handle->loaddb_file_type != LOADDB_FILE_TYPE_SCHEMA)
	{
	  return;
	}
    }
  else
    {
      if (cub_is_ddl_type (ddl_audit_handle->stmt_type) == FALSE)
	{
	  return;
	}
    }

  fp = cub_ddl_log_open (ddl_audit_handle->app_name);

  if (fp != NULL)
    {
      if (strcmp (ddl_audit_handle->app_name, "loaddb") == 0)
	{
	  if (cub_make_schema_file_name (ddl_audit_handle->file_name, dest_path, PATH_MAX) < 0)
	    {
	      goto write_error;
	    }
	}

      len = cub_create_log_mgs (buf);
      if (len < 0)
	{
	  goto write_error;
	}

      if (fwrite (buf, 1, len, fp) != len)
	{
	  goto write_error;
	}

      if (strcmp (ddl_audit_handle->app_name, "loaddb") == 0)
	{
	  cub_file_copy (ddl_audit_handle->file_name, dest_path);
	}

      if ((UINT64) ftell (fp) > prm_get_bigint_value (PRM_ID_DDL_AUDIT_LOG_SIZE))
	{
	  fclose (fp);
	  cub_ddl_log_backup (ddl_audit_handle->log_filepath);
	  fp = NULL;
	}
    }

write_error:
  if (fp != NULL)
    {
      fclose (fp);
      fp = NULL;
    }
}

static int
cub_create_log_mgs (char *msg)
{
  int retval = 0;
  char result[20] = { 0 };

  if (ddl_audit_handle == NULL)
    {
      return -1;
    }

  if (strcmp (ddl_audit_handle->app_name, "loaddb") == 0)
    {
      if (ddl_audit_handle->err_code < 0)
	{
          snprintf (result, sizeof (result), "ERROR:%d", ddl_audit_handle->err_code);
          snprintf (ddl_audit_handle->msg , DDL_LOG_MSG, "Commited count %8d, Error line %d", 
                    ddl_audit_handle->commit_count,
                    ddl_audit_handle->file_line_number);
	}
      else
	{
	  strcpy (result, "OK");
	}

      retval = snprintf (msg, DDL_LOG_BUFFER_SIZE, "%s %d|%s|%s|%s|%s\n",
			 ddl_audit_handle->execute_start_time,
			 ddl_audit_handle->pid, ddl_audit_handle->user_name, result, ddl_audit_handle->msg, ddl_audit_handle->schema_file
			 
	);
    }
  else if (strcmp (ddl_audit_handle->app_name, "csql") == 0)
    {
      if (ddl_audit_handle->err_code < 0)
	{
	  snprintf (result, sizeof (result), "ERROR:%d", ddl_audit_handle->err_code);
	  ddl_audit_handle->elapsed_time[0] = '\0';
	}
      else
	{
	  strcpy (result, "OK");
	}

      retval = snprintf (msg, DDL_LOG_BUFFER_SIZE, "%s %d|%s|%s|%s|%s\n",
			 ddl_audit_handle->execute_start_time,
			 ddl_audit_handle->pid,
			 ddl_audit_handle->user_name,
			 result, ddl_audit_handle->elapsed_time, ddl_audit_handle->sql_text);
    }
  else
    {
      if (ddl_audit_handle->err_code < 0)
	{
	  snprintf (result, sizeof (result), "ERROR:%d", ddl_audit_handle->err_code);
	}
      else
	{
	  strcpy (result, "OK");
	}


      if (ddl_audit_handle->log_type & DDL_LOG_NO_ELAPSED_TIME)
	{
	  snprintf (ddl_audit_handle->elapsed_time, sizeof (ddl_audit_handle->elapsed_time), "elapsed time 0.000");
	}

      retval = snprintf (msg, DDL_LOG_BUFFER_SIZE, "%s %s|%s|%s|%s|%s|%s\n",
			 ddl_audit_handle->execute_start_time,
			 ddl_audit_handle->ip_addr,
			 ddl_audit_handle->user_name,
			 result, ddl_audit_handle->elapsed_time, ddl_audit_handle->msg, ddl_audit_handle->sql_text);

    }

  if (retval >= DDL_LOG_BUFFER_SIZE)
    {
      msg[DDL_LOG_BUFFER_SIZE - 2] = '\n';
      msg[DDL_LOG_BUFFER_SIZE - 1] = '\0';
      retval = DDL_LOG_BUFFER_SIZE;
    }
  return retval;
}

static void
cub_ddl_log_backup (const char *path)
{
  char backup_file[PATH_MAX] = { 0 };

  snprintf (backup_file, sizeof (backup_file), "%s.bak", path);

  unlink (backup_file);
  rename (path, backup_file);
}

static FILE *
cub_fopen_and_lock (const char *path, const char *mode)
{
#define MAX_RETRY_COUNT 100
  int retry_count = 0;
  FILE *ddl_log_fd = NULL;

retry:
  ddl_log_fd = fopen (path, mode);
  if (ddl_log_fd != NULL)
    {
      if (lockf (fileno (ddl_log_fd), F_TLOCK, 0) < 0)
	{
	  fclose (ddl_log_fd);
	  if (retry_count < MAX_RETRY_COUNT)
	    {
	      SLEEP_MILISEC (0, 10);
	      retry_count++;
	      goto retry;
	    }
	  ddl_log_fd = NULL;
	}
    }

  return ddl_log_fd;
}

#if defined(WINDOWS)
static void
unix_style_path (char *path)
{
  char *p = NULL;
  for (p = path; *p; p++)
    {
      if (*p == '\\')
	*p = '/';
    }
}
#endif /* WINDOWS */

static int
cub_create_dir_log (const char *new_dir)
{
  char *p, path[PATH_MAX] = { 0 };

  if (new_dir == NULL)
    return -1;

  strcpy (path, new_dir);
  trim (path);


#if defined(WINDOWS)
  unix_style_path (path);
#endif /* WINDOWS */

  p = path;
#if defined(WINDOWS)
  if (path[0] == '/')
    p = path + 1;
  else if (strlen (path) > 3 && path[2] == '/')
    p = path + 3;
#else /* WINDOWS */
  if (path[0] == '/')
    {
      p = path + 1;
    }
#endif /* WINDOWS */

  while (p != NULL)
    {
      p = strchr (p, '/');
      if (p == NULL)
	return 0;

      if (p != NULL)
	*p = '\0';

      if (access (path, F_OK) < 0)
	{
	  if (mkdir (path, 0777) < 0)
	    {
	      return -1;
	    }
	}
      if (p != NULL)
	{
	  *p = '/';
	  p++;
	}
    }
  return 0;
}

static int
cub_get_current_time (char *buf, size_t size)
{
  struct tm at_tm;
  int len = 0;
  time_t t = time (NULL);

  localtime_r (&t, &at_tm);
  at_tm.tm_year += 1900;
  at_tm.tm_mon += 1;
  len =
    snprintf (buf, size, "%04d%02d%02d_%02d%02d%02d", at_tm.tm_year, at_tm.tm_mon, at_tm.tm_mday, at_tm.tm_hour,
	      at_tm.tm_min, at_tm.tm_sec);

  return len;
}

static int
cub_get_time_string (char *buf, struct timeval *time_val)
{
  struct tm tm, *tm_p;
  time_t sec;
  int millisec;

  if (buf == NULL)
    {
      return 0;
    }

  if (time_val == NULL)
    {
      struct timeb tb;

      /* current time */
      (void) ftime (&tb);
      sec = tb.time;
      millisec = tb.millitm;
    }
  else
    {
      sec = time_val->tv_sec;
      millisec = time_val->tv_usec / 1000;
    }

  tm_p = localtime_r (&sec, &tm);
  tm.tm_mon++;

  buf[0] = ((tm.tm_year % 100) / 10) + '0';
  buf[1] = (tm.tm_year % 10) + '0';
  buf[2] = '-';
  buf[3] = (tm.tm_mon / 10) + '0';
  buf[4] = (tm.tm_mon % 10) + '0';
  buf[5] = '-';
  buf[6] = (tm.tm_mday / 10) + '0';
  buf[7] = (tm.tm_mday % 10) + '0';
  buf[8] = ' ';
  buf[9] = (tm.tm_hour / 10) + '0';
  buf[10] = (tm.tm_hour % 10) + '0';
  buf[11] = ':';
  buf[12] = (tm.tm_min / 10) + '0';
  buf[13] = (tm.tm_min % 10) + '0';
  buf[14] = ':';
  buf[15] = (tm.tm_sec / 10) + '0';
  buf[16] = (tm.tm_sec % 10) + '0';
  buf[17] = '.';
  buf[20] = (millisec % 10) + '0';
  millisec /= 10;
  buf[19] = (millisec % 10) + '0';
  millisec /= 10;
  buf[18] = (millisec % 10) + '0';
  buf[21] = '\0';

  return 21;
}

static void
cub_remove_char (char *string, char ch)
{
  for (; *string != '\0'; string++)
    {
      if (*string == ch)
	{
	  strcpy (string, string + 1);
	  string--;
	}
    }
}

static int
cub_is_ddl_type (int node_type)
{
  switch (node_type)
    {
    case PT_ALTER:
    case PT_ALTER_INDEX:
    case PT_ALTER_SERIAL:
    case PT_ALTER_STORED_PROCEDURE:
    case PT_ALTER_TRIGGER:
    case PT_ALTER_USER:
    case PT_CREATE_ENTITY:
    case PT_CREATE_INDEX:
    case PT_CREATE_SERIAL:
    case PT_CREATE_STORED_PROCEDURE:
    case PT_CREATE_TRIGGER:
    case PT_CREATE_USER:
    case PT_DROP:
    case PT_DROP_INDEX:
    case PT_DROP_SERIAL:
    case PT_DROP_STORED_PROCEDURE:
    case PT_DROP_TRIGGER:
    case PT_DROP_USER:
    case PT_GRANT:
    case PT_RENAME:
    case PT_REVOKE:
    case PT_REMOVE_TRIGGER:
    case PT_RENAME_TRIGGER:
    case PT_UPDATE_STATS:
    case PT_TRUNCATE:
      return true;
    default:
      break;
    }

  return false;
}
