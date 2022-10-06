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
 * ddl_log.c -
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
#include <signal.h>
#endif
#include <assert.h>

#include "porting.h"
#include "cas_common.h"
#include "ddl_log.h"
#include "parse_tree.h"
#include "system_parameter.h"
#include "environment_variable.h"
#include "broker_config.h"
#include "util_func.h"
#include "password_log.h"

#define DDL_LOG_MSG 	            (256)
#define DDL_LOG_PATH    	    "log/ddl_audit"
#define DDL_LOG_LOADDB_FILE_PATH    "log/ddl_audit/loaddb"
#define DDL_LOG_CSQL_FILE_PATH      "log/ddl_audit/csql"
#define FILE_BUFFER_SIZE            (1024)
#define TIME_STRING_SIZE            (16)
#define DDL_TIME_LEN                (32)

#define SQL_TEXT_SIZE               (2048)
#define SQL_TEXT_SIZE_MAX           (8192)
#define SQL_TEXT_INCR               (1024)

typedef struct t_ddl_audit_handle T_DDL_AUDIT_HANDLE;
struct t_ddl_audit_handle
{
  char *sql_text;
  int alloc_size;		// Allocated size of sql_text  
  char zsql_text[SQL_TEXT_SIZE];

#ifdef _USE_ERR_MSG_IN_LOGDDL_
  char *err_msg;
#endif
  char db_name[DB_MAX_IDENTIFIER_LENGTH];
  char user_name[DB_MAX_USER_LENGTH];
  T_APP_NAME app_name;
  char br_name[BROKER_NAME_LEN];
  int br_index;
  char ip_addr[16];
  int pid;
  char stmt_type;
  char str_qry_exec_begin_time[DDL_TIME_LEN];
  struct timeval qry_exec_begin_time;
  char elapsed_time[DDL_TIME_LEN];
  int file_line_number;
  int err_code;
  char msg[DDL_LOG_MSG];
  char load_filename[PATH_MAX];
  char copy_filename[PATH_MAX];
  char copy_fullpath[PATH_MAX];
  char execute_type;
  T_LOADDB_FILE_TYPE loaddb_file_type;
  T_CSQL_INPUT_TYPE csql_input_type;
  char log_filepath[PATH_MAX];
  int commit_count;
  bool auto_commit_mode;
  bool jsp_mode;
};

static T_DDL_AUDIT_HANDLE ddl_audit_handle = { 0x00, };

static bool is_first_initialized = true;
static bool ddl_logging_enabled = false;
static UINT64 ddl_logging_size = 0ULL;

static void logddl_make_filename (char *filename_buf, size_t buf_size, T_APP_NAME app_name);
static int logddl_make_copy_filename (T_APP_NAME app_name, const char *file_full_path, char *copy_filename,
				      size_t buf_size);
static int logddl_make_copy_dir (T_APP_NAME app_name, char *copy_filename, char *copy_fullpath, size_t buf_size);
static void logddl_backup (const char *path);
#if defined(WINDOWS)
static void unix_style_path (char *path);
#endif /* WINDOWS */
static int logddl_create_dir (const char *new_dir);
static int logddl_create_log_msg (char *msg, int *pwd_offset_ptr);
static int logddl_get_current_date_time_string (char *buf, size_t size);
static int logddl_file_copy (char *src_file, char *dest_file);
static bool logddl_file_backup (T_APP_NAME app_name, T_CSQL_INPUT_TYPE csql_input_type);
#ifdef _USE_ERR_MSG_IN_LOGDDL_
static void logddl_remove_char (char *string, char ch);
#endif
static FILE *logddl_open (T_APP_NAME app_name);
static int logddl_get_time_string (char *buf, struct timeval *time_val);
static FILE *logddl_fopen_and_lock (const char *path, const char *mode);
static void logddl_set_elapsed_time (long sec, long msec);
static void logddl_timeval_diff (struct timeval *start, struct timeval *end, long *res_sec, long *res_msec);
static const char *logddl_get_app_name (T_APP_NAME app_name);

static bool is_executed_ddl_for_trans = false;
static bool is_executed_ddl_for_csql = false;

void
logddl_init (T_APP_NAME app_name)
{
  if (!is_first_initialized)
    {
      ddl_audit_handle.sql_text[0] = '\0';
#ifdef _USE_ERR_MSG_IN_LOGDDL_
      FREE_MEM (ddl_audit_handle.err_msg);
#endif
    }

  memset (&ddl_audit_handle, 0x00, sizeof (T_DDL_AUDIT_HANDLE));

  ddl_audit_handle.stmt_type = -1;
  ddl_audit_handle.execute_type = LOGDDL_RUN_EXECUTE_FUNC;
  ddl_audit_handle.loaddb_file_type = LOADDB_FILE_TYPE_NONE;
  ddl_audit_handle.csql_input_type = CSQL_INPUT_TYPE_NONE;

  ddl_audit_handle.app_name = app_name;
  if (is_first_initialized)
    {
      is_first_initialized = false;
      ddl_audit_handle.sql_text = ddl_audit_handle.zsql_text;
      ddl_audit_handle.alloc_size = SQL_TEXT_SIZE;
    }
}

void
logddl_free (bool all_free)
{
  if (is_first_initialized)
    {
      logddl_init (APP_NAME_NONE);
      return;
    }

  if (ddl_logging_enabled == false)
    {
      return;
    }

  if (ddl_audit_handle.alloc_size > SQL_TEXT_SIZE_MAX)
    {
      FREE_MEM (ddl_audit_handle.sql_text);
      ddl_audit_handle.sql_text = ddl_audit_handle.zsql_text;
      ddl_audit_handle.alloc_size = SQL_TEXT_SIZE;
    }
  ddl_audit_handle.sql_text[0] = '\0';
#ifdef _USE_ERR_MSG_IN_LOGDDL_
  FREE_MEM (ddl_audit_handle.err_msg);
#endif

  ddl_audit_handle.stmt_type = -1;
  ddl_audit_handle.execute_type = LOGDDL_RUN_EXECUTE_FUNC;
  ddl_audit_handle.loaddb_file_type = LOADDB_FILE_TYPE_NONE;
  ddl_audit_handle.str_qry_exec_begin_time[0] = '\0';
  ddl_audit_handle.elapsed_time[0] = '\0';
  ddl_audit_handle.file_line_number = 0;
  ddl_audit_handle.err_code = 0;
  ddl_audit_handle.msg[0] = '\0';

  if (all_free)
    {
      is_executed_ddl_for_trans = false;
      is_executed_ddl_for_csql = false;

      ddl_audit_handle.auto_commit_mode = false;
      ddl_audit_handle.jsp_mode = false;
      ddl_audit_handle.csql_input_type = CSQL_INPUT_TYPE_NONE;
      ddl_audit_handle.load_filename[0] = '\0';
      ddl_audit_handle.copy_filename[0] = '\0';
      ddl_audit_handle.copy_fullpath[0] = '\0';
      ddl_audit_handle.commit_count = 0;
    }
}

void
logddl_destroy ()
{
  if (!is_first_initialized)
    {
      if (ddl_audit_handle.sql_text != ddl_audit_handle.zsql_text)
	{
	  FREE_MEM (ddl_audit_handle.sql_text);
	}
#ifdef _USE_ERR_MSG_IN_LOGDDL_
      FREE_MEM (ddl_audit_handle.err_msg);
#endif
      is_first_initialized = true;
    }
}

void
logddl_check_ddl_audit_param ()
{
  ddl_logging_enabled = prm_get_bool_value (PRM_ID_DDL_AUDIT_LOG);
  if (ddl_logging_enabled)
    {
      ddl_audit_handle.pid = (int) getpid ();
    }
}

void
logddl_set_db_name (const char *db_name)
{
  char *pstr = NULL;
  int len;
  if (ddl_logging_enabled && db_name)
    {
      pstr = (char *) strchr (db_name, '@');
      len = pstr ? (int) (pstr - db_name) : (int) strlen (db_name);
      if (len >= (int) sizeof (ddl_audit_handle.db_name))
	{
	  len = sizeof (ddl_audit_handle.db_name) - 1;
	}

      memcpy (ddl_audit_handle.db_name, db_name, len);
      ddl_audit_handle.db_name[len] = '\0';

      logddl_make_filename (ddl_audit_handle.log_filepath, PATH_MAX, ddl_audit_handle.app_name);
    }
}

void
logddl_set_user_name (const char *user_name)
{
  if (ddl_logging_enabled && user_name)
    {
      snprintf (ddl_audit_handle.user_name, sizeof (ddl_audit_handle.user_name), user_name);
    }
}

void
logddl_set_ip (const char *ip_addr)
{
  if (ddl_logging_enabled && ip_addr)
    {
      snprintf (ddl_audit_handle.ip_addr, sizeof (ddl_audit_handle.ip_addr), ip_addr);
    }
}

void
logddl_set_broker_info (const int index, const char *br_name)
{
  if (ddl_logging_enabled)
    {
      ddl_audit_handle.br_index = index;
      if (br_name)
	{
	  snprintf (ddl_audit_handle.br_name, BROKER_NAME_LEN, br_name);
	}
      else
	{
	  ddl_audit_handle.br_name[0] = '\0';
	}

      logddl_make_filename (ddl_audit_handle.log_filepath, PATH_MAX, ddl_audit_handle.app_name);
    }
}

void
logddl_set_sql_text (char *sql_text, int len)
{
  if (ddl_logging_enabled)
    {
      if (!sql_text || len <= 0)
	{
	  ddl_audit_handle.sql_text[0] = '\0';
	}
      else
	{
	  if (ddl_audit_handle.alloc_size <= len)
	    {
	      int new_size = ddl_audit_handle.alloc_size;
	      char *ptr;

	      while (new_size < len)
		{
		  new_size += SQL_TEXT_INCR;
		}

	      ptr = (char *) MALLOC (new_size + 1);
	      if (!ptr)
		{		/* It's a failure, but let's copy as much space as we can */
		  memcpy (ddl_audit_handle.sql_text, sql_text, ddl_audit_handle.alloc_size);

		  ddl_audit_handle.sql_text[ddl_audit_handle.alloc_size - 5] = ' ';
		  ddl_audit_handle.sql_text[ddl_audit_handle.alloc_size - 4] = '.';
		  ddl_audit_handle.sql_text[ddl_audit_handle.alloc_size - 3] = '.';
		  ddl_audit_handle.sql_text[ddl_audit_handle.alloc_size - 2] = '.';
		  ddl_audit_handle.sql_text[ddl_audit_handle.alloc_size - 1] = '\0';
		  return;
		}

	      if (ddl_audit_handle.sql_text != ddl_audit_handle.zsql_text)
		{
		  FREE_MEM (ddl_audit_handle.sql_text);
		}

	      ddl_audit_handle.sql_text = ptr;
	      ddl_audit_handle.alloc_size = new_size;
	    }

	  memcpy (ddl_audit_handle.sql_text, sql_text, len);
	  ddl_audit_handle.sql_text[len] = '\0';
	}
    }
}


bool
logddl_set_stmt_type (int stmt_type)
{
  if (ddl_logging_enabled)
    {
      ddl_audit_handle.stmt_type = stmt_type;

      if (logddl_is_ddl_type (stmt_type) == true)
	{
	  is_executed_ddl_for_trans = true;
	  is_executed_ddl_for_csql = true;
	  return true;
	}
    }

  return false;
}

void
logddl_set_loaddb_file_type (T_LOADDB_FILE_TYPE file_type)
{
  if (ddl_logging_enabled)
    {
      ddl_audit_handle.loaddb_file_type = file_type;
    }
}

void
logddl_set_csql_input_type (T_CSQL_INPUT_TYPE input_type)
{
  if (ddl_logging_enabled)
    {
      ddl_audit_handle.csql_input_type = input_type;
    }
}

void
logddl_set_load_filename (const char *load_filename)
{
  if (ddl_logging_enabled && load_filename)
    {
      strncpy (ddl_audit_handle.load_filename, load_filename, PATH_MAX);
    }
}

void
logddl_set_file_line (int file_line)
{
  if (ddl_logging_enabled)
    {
      ddl_audit_handle.file_line_number = file_line;
    }
}

#ifdef _USE_ERR_MSG_IN_LOGDDL_
void
logddl_set_err_msg (char *msg)
{
  if (ddl_logging_enabled && msg)
    {
      if (ddl_audit_handle.err_msg != NULL)
	{
	  FREE_MEM (ddl_audit_handle.err_msg);
	}

      ALLOC_COPY_STRLEN (ddl_audit_handle.err_msg, msg);
      logddl_remove_char (ddl_audit_handle.err_msg, '\n');
    }
}
#endif

void
logddl_set_err_code (int err_code)
{
  if (ddl_logging_enabled)
    {
      ddl_audit_handle.err_code = err_code;
    }
}

void
logddl_set_start_time (struct timeval *time_val)
{
  if (ddl_logging_enabled)
    {
      if (time_val == NULL)
	{
	  struct timeval time;
	  gettimeofday (&time, NULL);
	  memcpy (&ddl_audit_handle.qry_exec_begin_time, &time, sizeof (struct timeval));
	}
      else
	{
	  memcpy (&ddl_audit_handle.qry_exec_begin_time, time_val, sizeof (struct timeval));
	}

      logddl_get_time_string (ddl_audit_handle.str_qry_exec_begin_time, time_val);
    }
}

void
logddl_set_msg (const char *fmt, ...)
{
  va_list args;
  if (ddl_logging_enabled)
    {
      va_start (args, fmt);
      vsnprintf (ddl_audit_handle.msg, DDL_LOG_MSG, fmt, args);
      va_end (args);
    }
}

void
logddl_set_execute_type (char exe_type)
{
  if (ddl_logging_enabled)
    {
      ddl_audit_handle.execute_type = exe_type;
    }
}

void
logddl_set_commit_count (int count)
{
  if (ddl_logging_enabled)
    {
      ddl_audit_handle.commit_count = count;
    }
}

void
logddl_set_commit_mode (bool mode)
{
  if (ddl_logging_enabled)
    {
      ddl_audit_handle.auto_commit_mode = mode;
    }
}

void
logddl_set_jsp_mode (bool mode)
{
  if (ddl_logging_enabled)
    {
      ddl_audit_handle.jsp_mode = mode;
    }
}

bool
logddl_get_jsp_mode ()
{
  return ddl_audit_handle.jsp_mode;
}

static void
logddl_set_elapsed_time (long sec, long msec)
{
  if (ddl_logging_enabled)
    {
      snprintf (ddl_audit_handle.elapsed_time, 20, "elapsed time %ld.%03ld", sec, msec);
    }
}

static bool
logddl_file_backup (T_APP_NAME app_name, T_CSQL_INPUT_TYPE csql_input_type)
{
  if ((app_name == APP_NAME_LOADDB) || (app_name == APP_NAME_CSQL && csql_input_type == CSQL_INPUT_TYPE_FILE))
    {
      if (strlen (ddl_audit_handle.copy_filename) == 0)
	{
	  if (logddl_make_copy_filename
	      (app_name, ddl_audit_handle.load_filename, ddl_audit_handle.copy_filename, PATH_MAX) < 0)
	    {
	      return false;
	    }
	}

      if (strlen (ddl_audit_handle.copy_fullpath) == 0)
	{
	  if (logddl_make_copy_dir
	      (app_name, ddl_audit_handle.copy_filename, ddl_audit_handle.copy_fullpath, PATH_MAX) < 0)
	    {
	      return false;
	    }
	}
      logddl_file_copy (ddl_audit_handle.load_filename, ddl_audit_handle.copy_fullpath);
    }

  return true;
}


static int
logddl_file_copy (char *src_file, char *dest_file)
{
  char buf[FILE_BUFFER_SIZE] = { 0 };
  size_t size;
  int retval = 0;

  if (src_file == NULL || dest_file == NULL)
    {
      return -1;
    }

  if (access (dest_file, F_OK) == 0)
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
	  if (fwrite (buf, sizeof (char), size, fdest) != size)
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
logddl_make_copy_filename (T_APP_NAME app_name, const char *file_full_path, char *copy_filename, size_t buf_size)
{
  const char *env_root = NULL;
  char time[TIME_STRING_SIZE] = { 0 };
  const char *name_tmp = NULL;
  int retval = 0;

  if (file_full_path == NULL || copy_filename == NULL || buf_size < 0)
    {
      return -1;
    }

  env_root = envvar_root ();
  logddl_get_current_date_time_string (time, TIME_STRING_SIZE);

  name_tmp = strrchr (file_full_path, PATH_SEPARATOR);

  if (name_tmp == NULL)
    {
      name_tmp = file_full_path;
    }
  else
    {
      name_tmp++;
    }

  retval = snprintf (copy_filename, buf_size, "%s_%s_%d", name_tmp, time, getpid ());
  if (retval < 0)
    {
      assert (false);
      copy_filename[0] = '\0';
      return retval;
    }

  return retval;
}

static int
logddl_make_copy_dir (T_APP_NAME app_name, char *copy_filename, char *copy_fullpath, size_t buf_size)
{
  const char *env_root = NULL;
  int retval = 0;

  if (copy_filename == NULL || copy_fullpath == NULL || buf_size < 0)
    {
      return -1;
    }

  env_root = envvar_root ();

  if (app_name == APP_NAME_CSQL)
    {
      retval = snprintf (copy_fullpath, buf_size, "%s/%s/%s", env_root, DDL_LOG_CSQL_FILE_PATH, copy_filename);
    }
  else if (app_name == APP_NAME_LOADDB)
    {
      retval = snprintf (copy_fullpath, buf_size, "%s/%s/%s", env_root, DDL_LOG_LOADDB_FILE_PATH, copy_filename);
    }
  else
    {
      retval = -1;
    }

  if (retval < 0)
    {
      assert (false);
      copy_fullpath[0] = '\0';
      return retval;
    }
  retval = logddl_create_dir (copy_fullpath);
  return retval;
}

static void
logddl_make_filename (char *filename_buf, size_t buf_size, T_APP_NAME app_name)
{
  const char *env_root = NULL;
  assert (filename_buf != NULL);

  env_root = envvar_root ();

  if (app_name == APP_NAME_CAS)
    {
      snprintf (filename_buf, buf_size, "%s/%s/%s_%d_ddl.log", env_root, DDL_LOG_PATH, ddl_audit_handle.br_name,
		(ddl_audit_handle.br_index + 1));
    }
  else if (app_name == APP_NAME_CSQL || app_name == APP_NAME_LOADDB)
    {
      snprintf (filename_buf, buf_size, "%s/%s/%s_%s_ddl.log", env_root, DDL_LOG_PATH,
		logddl_get_app_name (app_name), ddl_audit_handle.db_name);
    }
  else
    {
      filename_buf[0] = '\0';
    }
}

static FILE *
logddl_open (T_APP_NAME app_name)
{
  if (ddl_audit_handle.log_filepath[0] == '\0')
    {
      return NULL;
    }

  if (logddl_create_dir (ddl_audit_handle.log_filepath) < 0)
    {
      ddl_audit_handle.log_filepath[0] = '\0';
      return NULL;
    }

  /* note: in "a+" mode, output is always appended */
  return logddl_fopen_and_lock (ddl_audit_handle.log_filepath, "a+");
}

inline static bool
is_need_write ()
{
  if (ddl_audit_handle.app_name == APP_NAME_LOADDB)
    {
      if (ddl_audit_handle.loaddb_file_type != LOADDB_FILE_TYPE_SCHEMA
	  && ddl_audit_handle.loaddb_file_type != LOADDB_FILE_TYPE_INDEX
	  && ddl_audit_handle.loaddb_file_type != LOADDB_FILE_TYPE_TRIGGER)
	{
	  return false;
	}

      return true;
    }

  return logddl_is_ddl_type (ddl_audit_handle.stmt_type);
}

void
logddl_write_end ()
{
  if (ddl_logging_enabled == false)
    {
      return;
    }

  if (ddl_audit_handle.execute_type == LOGDDL_RUN_EXECUTE_FUNC)
    {
      logddl_write ();
    }

ddl_log_free:
  logddl_free (false);
}

void
logddl_write ()
{
  FILE *fp = NULL;
  char buf[DDL_LOG_BUFFER_SIZE] = { 0 };
  int len = 0;

  if (ddl_logging_enabled == false)
    {
      return;
    }

  if (is_need_write () == false)
    {
      return;
    }

  fp = logddl_open (ddl_audit_handle.app_name);

  if (fp != NULL)
    {
      if (logddl_file_backup (ddl_audit_handle.app_name, ddl_audit_handle.csql_input_type) == false)
	{
	  goto write_error;
	}

      len = logddl_create_log_msg (buf, NULL /* pwd_offset_ptr */ );
      if (len < 0 || fwrite (buf, sizeof (char), len, fp) != (size_t) len)
	{
	  goto write_error;
	}

      if (ddl_logging_size == 0ULL)
	{
	  ddl_logging_size = prm_get_bigint_value (PRM_ID_DDL_AUDIT_LOG_SIZE);
	}

      if ((UINT64) ftell (fp) > ddl_logging_size)
	{
	  fclose (fp);
	  logddl_backup (ddl_audit_handle.log_filepath);
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

void
logddl_write_tran_str (const char *fmt, ...)
{
  FILE *fp = NULL;
  char msg[DDL_LOG_BUFFER_SIZE] = { 0 };
  int len = 0;
  struct timeval time_val;
  va_list args;

  if (ddl_logging_enabled == false)
    {
      return;
    }

  if (is_executed_ddl_for_trans == false || ddl_audit_handle.auto_commit_mode == true)
    {
      goto write_error;
    }

  fp = logddl_open (ddl_audit_handle.app_name);

  if (fp != NULL)
    {
      va_start (args, fmt);
      vsnprintf (ddl_audit_handle.msg, DDL_LOG_MSG, fmt, args);
      va_end (args);

      gettimeofday (&time_val, NULL);
      logddl_get_time_string (ddl_audit_handle.str_qry_exec_begin_time, &time_val);

      if (logddl_file_backup (ddl_audit_handle.app_name, ddl_audit_handle.csql_input_type) == false)
	{
	  goto write_error;
	}

      if (ddl_audit_handle.app_name == APP_NAME_CAS)
	{
	  len = snprintf (msg, DDL_LOG_BUFFER_SIZE, "%s %s|%s|%s\n",
			  ddl_audit_handle.str_qry_exec_begin_time,
			  ddl_audit_handle.ip_addr, ddl_audit_handle.user_name, ddl_audit_handle.msg);
	}
      else if (ddl_audit_handle.app_name == APP_NAME_CSQL)
	{
	  if (ddl_audit_handle.csql_input_type == CSQL_INPUT_TYPE_FILE)
	    {
	      len = snprintf (msg, DDL_LOG_BUFFER_SIZE, "%s %d|%s|%s|%s\n",
			      ddl_audit_handle.str_qry_exec_begin_time,
			      ddl_audit_handle.pid, ddl_audit_handle.user_name, ddl_audit_handle.msg,
			      ddl_audit_handle.copy_filename);
	    }
	  else
	    {
	      len = snprintf (msg, DDL_LOG_BUFFER_SIZE, "%s %d|%s|%s\n",
			      ddl_audit_handle.str_qry_exec_begin_time,
			      ddl_audit_handle.pid, ddl_audit_handle.user_name, ddl_audit_handle.msg);
	    }
	}
      else
	{
	  goto write_error;
	}

      if (len >= DDL_LOG_BUFFER_SIZE)
	{
	  msg[DDL_LOG_BUFFER_SIZE - 2] = '\n';
	  len = DDL_LOG_BUFFER_SIZE;
	}

      if (len < 0 || fwrite (msg, sizeof (char), len, fp) != len)
	{
	  goto write_error;
	}

      if (ddl_logging_size == 0ULL)
	{
	  ddl_logging_size = prm_get_bigint_value (PRM_ID_DDL_AUDIT_LOG_SIZE);
	}
      if ((UINT64) ftell (fp) > ddl_logging_size)
	{
	  fclose (fp);
	  logddl_backup (ddl_audit_handle.log_filepath);
	  fp = NULL;
	}
    }

write_error:
  if (fp != NULL)
    {
      fclose (fp);
      fp = NULL;
    }

  is_executed_ddl_for_trans = false;
  logddl_free (false);
}

void
logddl_write_end_for_csql_fileinput (const char *fmt, ...)
{
  FILE *fp = NULL;
  char buf[DDL_LOG_BUFFER_SIZE] = { 0 };
  int len = 0;
  struct timeval time_val;
  va_list args;
  if (ddl_logging_enabled == false)
    {
      return;
    }

  if (ddl_audit_handle.app_name != APP_NAME_CSQL && ddl_audit_handle.csql_input_type != CSQL_INPUT_TYPE_FILE)
    {
      return;
    }

  if (is_executed_ddl_for_csql == false)
    {
      return;
    }

  fp = logddl_open (ddl_audit_handle.app_name);

  if (fp != NULL)
    {
      va_start (args, fmt);
      vsnprintf (ddl_audit_handle.msg, DDL_LOG_MSG, fmt, args);
      va_end (args);

      gettimeofday (&time_val, NULL);
      logddl_get_time_string (ddl_audit_handle.str_qry_exec_begin_time, &time_val);

      if (logddl_file_backup (APP_NAME_CSQL, CSQL_INPUT_TYPE_FILE) == false)
	{
	  goto write_error;
	}

      len = logddl_create_log_msg (buf, NULL);
      if (len < 0 || fwrite (buf, sizeof (char), len, fp) != (size_t) len)
	{
	  goto write_error;
	}

      if (ddl_logging_size == 0ULL)
	{
	  ddl_logging_size = prm_get_bigint_value (PRM_ID_DDL_AUDIT_LOG_SIZE);
	}
      if ((UINT64) ftell (fp) > ddl_logging_size)
	{
	  fclose (fp);
	  logddl_backup (ddl_audit_handle.log_filepath);
	  fp = NULL;
	}
    }

write_error:
  if (fp != NULL)
    {
      fclose (fp);
      fp = NULL;
    }

  logddl_free (true);
}

static int
logddl_create_log_msg (char *msg, int *pwd_offset_ptr)
{
  int retval = 0;
  char result[20] = { 0 };
  struct timeval exec_end, log_time;
  long elapsed_sec = 0;
  long elapsed_msec = 0;

  if (ddl_logging_enabled == false)
    {
      return -1;
    }

  gettimeofday (&exec_end, NULL);
  logddl_timeval_diff (&ddl_audit_handle.qry_exec_begin_time, &exec_end, &elapsed_sec, &elapsed_msec);
  logddl_set_elapsed_time (elapsed_sec, elapsed_msec);

  if (strlen (ddl_audit_handle.str_qry_exec_begin_time) == 0)
    {
      gettimeofday (&log_time, NULL);
      logddl_get_time_string (ddl_audit_handle.str_qry_exec_begin_time, &log_time);
    }

  if (ddl_audit_handle.app_name == APP_NAME_LOADDB)
    {
      if (ddl_audit_handle.err_code < 0)
	{
	  snprintf (result, sizeof (result), "ERROR:%d", ddl_audit_handle.err_code);
	  snprintf (ddl_audit_handle.msg, DDL_LOG_MSG, "Commited count %8d, Error line %d",
		    ddl_audit_handle.commit_count, ddl_audit_handle.file_line_number);
	}
      else
	{
	  strcpy (result, "OK");
	}

      retval = snprintf (msg, DDL_LOG_BUFFER_SIZE, "%s %d|%s|%s|%s|%s\n",
			 ddl_audit_handle.str_qry_exec_begin_time,
			 ddl_audit_handle.pid, ddl_audit_handle.user_name, result, ddl_audit_handle.msg,
			 ddl_audit_handle.copy_filename);
    }
  else if (ddl_audit_handle.app_name == APP_NAME_CSQL)
    {
      if (ddl_audit_handle.csql_input_type == CSQL_INPUT_TYPE_FILE)
	{
	  if (ddl_audit_handle.err_code < 0)
	    {
	      snprintf (result, sizeof (result), "ERROR:%d", ddl_audit_handle.err_code);
	      snprintf (ddl_audit_handle.msg, DDL_LOG_MSG, "Error line %d", ddl_audit_handle.file_line_number);
	      ddl_audit_handle.elapsed_time[0] = '\0';

	      retval = snprintf (msg, DDL_LOG_BUFFER_SIZE, "%s %d|%s|%s|%s|%s|%s\n",
				 ddl_audit_handle.str_qry_exec_begin_time,
				 ddl_audit_handle.pid,
				 ddl_audit_handle.user_name,
				 (ddl_audit_handle.auto_commit_mode) ? "autocommit mode on" : "autocommit mode off",
				 result, ddl_audit_handle.msg, ddl_audit_handle.copy_filename);
	    }
	  else
	    {
	      strcpy (result, "OK");
	      retval = snprintf (msg, DDL_LOG_BUFFER_SIZE, "%s %d|%s|%s|%s|%s|%s\n",
				 ddl_audit_handle.str_qry_exec_begin_time,
				 ddl_audit_handle.pid,
				 ddl_audit_handle.user_name,
				 (ddl_audit_handle.auto_commit_mode) ? "autocommit mode on" : "autocommit mode off",
				 result, ddl_audit_handle.msg, ddl_audit_handle.copy_filename);
	    }
	}
      else
	{
	  if (ddl_audit_handle.err_code < 0)
	    {
	      snprintf (result, sizeof (result), "ERROR:%d", ddl_audit_handle.err_code);
	      ddl_audit_handle.elapsed_time[0] = '\0';
	    }
	  else
	    {
	      strcpy (result, "OK");
	    }

	  retval = snprintf (msg, DDL_LOG_BUFFER_SIZE, "%s %d|%s|%s|%s|%s|%s\n",
			     ddl_audit_handle.str_qry_exec_begin_time,
			     ddl_audit_handle.pid,
			     ddl_audit_handle.user_name,
			     result, ddl_audit_handle.elapsed_time, ddl_audit_handle.msg, ddl_audit_handle.sql_text);
	}
    }
  else
    {
      if (ddl_audit_handle.err_code < 0)
	{
	  snprintf (result, sizeof (result), "ERROR:%d", ddl_audit_handle.err_code);
	}
      else
	{
	  strcpy (result, "OK");
	}

      if (ddl_audit_handle.execute_type == LOGDDL_RUN_EXECUTE_BATCH_FUNC)
	{
	  snprintf (ddl_audit_handle.elapsed_time, sizeof (ddl_audit_handle.elapsed_time), "elapsed time 0.000");
	}

      retval = snprintf (msg, DDL_LOG_BUFFER_SIZE, "%s %s|%s|%s|%s|%s|%s\n",
			 ddl_audit_handle.str_qry_exec_begin_time,
			 ddl_audit_handle.ip_addr,
			 ddl_audit_handle.user_name,
			 result, ddl_audit_handle.elapsed_time, ddl_audit_handle.msg, ddl_audit_handle.sql_text);
    }

  if (retval >= (DDL_LOG_BUFFER_SIZE - 1))
    {
      msg[DDL_LOG_BUFFER_SIZE - 2] = '\n';
      msg[DDL_LOG_BUFFER_SIZE - 1] = '\0';
      retval = DDL_LOG_BUFFER_SIZE;
    }
  return retval;
}

static void
logddl_backup (const char *path)
{
  char backup_file[PATH_MAX] = { 0 };
#if !defined(WINDOWS)
  sigset_t new_mask, old_mask;
#endif /* !WINDOWS */

  snprintf (backup_file, sizeof (backup_file), "%s.bak", path);

#if !defined(WINDOWS)
  sigfillset (&new_mask);
  sigdelset (&new_mask, SIGINT);
  sigdelset (&new_mask, SIGQUIT);
  sigdelset (&new_mask, SIGTERM);
  sigdelset (&new_mask, SIGHUP);
  sigdelset (&new_mask, SIGABRT);
  if (sigprocmask (SIG_SETMASK, &new_mask, &old_mask) == 0)
    {
      unlink (backup_file);
      rename (path, backup_file);
      sigprocmask (SIG_SETMASK, &old_mask, NULL);
    }
#else /* !WINDOWS */
  unlink (backup_file);
  rename (path, backup_file);
#endif
}

static FILE *
logddl_fopen_and_lock (const char *path, const char *mode)
{
#define MAX_RETRY_COUNT 100
  int retry_count = 0;
  FILE *ddl_log_fp = NULL;

retry:
  ddl_log_fp = fopen (path, mode);
  if (ddl_log_fp != NULL)
    {
      if (lockf (fileno (ddl_log_fp), F_TLOCK, 0) < 0)
	{
	  fclose (ddl_log_fp);
	  if (retry_count < MAX_RETRY_COUNT)
	    {
	      SLEEP_MILISEC (0, 10);
	      retry_count++;
	      goto retry;
	    }
	  ddl_log_fp = NULL;
	}
    }

  return ddl_log_fp;
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
logddl_create_dir (const char *new_dir)
{
  char *p, path[PATH_MAX] = { 0 };

  if (new_dir == NULL)
    return -1;

  strcpy (path, new_dir);
  trim (path);

#if defined(WINDOWS)
  unix_style_path (path);
#else
  if (access (dirname (path), F_OK) == 0)
    {
      return 0;
    }
  path[(int) strlen (path)] = '/';
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
logddl_get_current_date_time_string (char *buf, size_t size)
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
logddl_get_time_string (char *buf, struct timeval *time_val)
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
      /* current time */
      util_get_second_and_ms_since_epoch (&sec, &millisec);
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

#ifdef _USE_ERR_MSG_IN_LOGDDL_
static void
logddl_remove_char (char *string, char ch)
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
#endif

bool
logddl_is_ddl_type (int node_type)
{
  switch (node_type)
    {
    case PT_ALTER:
    case PT_ALTER_INDEX:
    case PT_ALTER_SERIAL:
    case PT_ALTER_SERVER:
    case PT_ALTER_STORED_PROCEDURE:
    case PT_ALTER_SYNONYM:
    case PT_ALTER_TRIGGER:
    case PT_ALTER_USER:
    case PT_CREATE_ENTITY:
    case PT_CREATE_INDEX:
    case PT_CREATE_SERIAL:
    case PT_CREATE_SERVER:
    case PT_CREATE_STORED_PROCEDURE:
    case PT_CREATE_SYNONYM:
    case PT_CREATE_TRIGGER:
    case PT_CREATE_USER:
    case PT_DROP:
    case PT_DROP_INDEX:
    case PT_DROP_SERIAL:
    case PT_DROP_SERVER:
    case PT_DROP_STORED_PROCEDURE:
    case PT_DROP_SYNONYM:
    case PT_DROP_TRIGGER:
    case PT_DROP_USER:
    case PT_GRANT:
    case PT_RENAME:
    case PT_RENAME_SERVER:
    case PT_RENAME_SYNONYM:
    case PT_REVOKE:
    case PT_REMOVE_TRIGGER:
    case PT_RENAME_TRIGGER:
    case PT_UPDATE_STATS:
    case PT_TRUNCATE:
      /* TODO: check it  */
      return true;
    default:
      break;
    }

  return false;
}

static void
logddl_timeval_diff (struct timeval *start, struct timeval *end, long *res_sec, long *res_msec)
{
  long sec, msec;

  assert (start != NULL && end != NULL && res_sec != NULL && res_msec != NULL);

  sec = end->tv_sec - start->tv_sec;
  msec = (end->tv_usec / 1000) - (start->tv_usec / 1000);
  if (msec < 0)
    {
      msec += 1000;
      sec--;
    }
  *res_sec = sec;
  *res_msec = msec;
}

static const char *
logddl_get_app_name (T_APP_NAME app_name)
{
  switch (app_name)
    {
    case APP_NAME_CAS:
      return "cas";
    case APP_NAME_CSQL:
      return "csql";
    case APP_NAME_LOADDB:
      return "loaddb";
    default:
      return "";
    }
}
