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
 * unloaddb.c - emits database object definitions in object loader format
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#if !defined (WINDOWS)
#include <sys/time.h>
#endif

#include "porting.h"
#include "authenticate.h"
#include "db.h"
#include "extract_schema.hpp"
#include "message_catalog.h"
#include "environment_variable.h"
#include "printer.hpp"
#include "schema_manager.h"
#include "locator_cl.h"
#include "unloaddb.h"
#include "utility.h"
#include "util_func.h"

#define MAX_PROCESS_COUNT (36)


char *database_name = NULL;
const char *output_dirname = NULL;
char *input_filename = NULL;
FILE *output_file = NULL;

#define DFLT_PRE_ALLOC_VARCHAR_SIZE (1024)	// 1024 characters
#define MAX_PRE_ALLOC_VARCHAR_SIZE  (DB_MAX_VARCHAR_PRECISION)
#define DFLT_REQ_DATASIZE           (100)	// 100 page size
#define MAX_REQ_DATA_PAGES          (1024)	//
#define MAX_THREAD_COUNT            (127)

int g_pre_alloc_varchar_size = DFLT_PRE_ALLOC_VARCHAR_SIZE;
int g_request_pages = DFLT_REQ_DATASIZE;

int page_size = 4096;
int cached_pages = 100;
int64_t est_size = 0;
char *hash_filename = NULL;
int debug_flag = 0;
bool verbose_flag = false;
bool latest_image_flag = false;
bool include_references = false;

bool required_class_only = false;
bool datafile_per_class = false;
LIST_MOPS *class_table = NULL;
DB_OBJECT **req_class_table = NULL;

char *output_prefix = NULL;
bool ignore_err_flag = false;

/*
 * unload_usage() - print an usage of the unload-utility
 *   return: void
 */
static void
unload_usage (const char *argv0)
{
  const char *exec_name;

  exec_name = basename ((char *) argv0);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_UNLOADDB, 60), exec_name);
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
}

/*
 * unloaddb - main function
 *    return: 0 if successful, non zero if error.
 *    argc(in): number of command line arguments
 *    argv(in): array containing command line arguments
 */
int
unloaddb (UTIL_FUNCTION_ARG * arg)
{
  char output_filename_schema[PATH_MAX * 2];
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  const char *exec_name = arg->command_name;
  char er_msg_file[PATH_MAX];
  int error;
  int status = 1;
  int i;
  char *user, *password;
  int au_save;
  EMIT_STORAGE_ORDER order;
  extract_context unload_context;

  bool do_objects = false;
  bool do_schema = false;
  bool is_main_process = true;
  bool enhanced_estimates = false;
  int thread_count = 1;
  int sampling_records = -1;

  db_set_use_utility_thread (true);

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      unload_usage (arg->argv0);
      return -1;
    }

  input_filename = utility_get_option_string_value (arg_map, UNLOAD_INPUT_CLASS_FILE_S, 0);
  include_references = utility_get_option_bool_value (arg_map, UNLOAD_INCLUDE_REFERENCE_S);
  required_class_only = utility_get_option_bool_value (arg_map, UNLOAD_INPUT_CLASS_ONLY_S);
  datafile_per_class = utility_get_option_bool_value (arg_map, UNLOAD_DATAFILE_PER_CLASS_S);
  est_size = utility_get_option_int_value (arg_map, UNLOAD_ESTIMATED_SIZE_S);
  cached_pages = utility_get_option_int_value (arg_map, UNLOAD_CACHED_PAGES_S);
  output_dirname = utility_get_option_string_value (arg_map, UNLOAD_OUTPUT_PATH_S, 0);
  do_schema = utility_get_option_bool_value (arg_map, UNLOAD_SCHEMA_ONLY_S);
  do_objects = utility_get_option_bool_value (arg_map, UNLOAD_DATA_ONLY_S);
  latest_image_flag = utility_get_option_bool_value (arg_map, UNLOAD_LATEST_IMAGE_S);
  output_prefix = utility_get_option_string_value (arg_map, UNLOAD_OUTPUT_PREFIX_S, 0);
  hash_filename = utility_get_option_string_value (arg_map, UNLOAD_HASH_FILE_S, 0);
  verbose_flag = utility_get_option_bool_value (arg_map, UNLOAD_VERBOSE_S);
  database_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  user = utility_get_option_string_value (arg_map, UNLOAD_USER_S, 0);
  password = utility_get_option_string_value (arg_map, UNLOAD_PASSWORD_S, 0);
  if (utility_get_option_bool_value (arg_map, UNLOAD_KEEP_STORAGE_ORDER_S))
    {
      order = FOLLOW_STORAGE_ORDER;
    }
  else
    {
      order = FOLLOW_ATTRIBUTE_ORDER;
    }

  g_pre_alloc_varchar_size = utility_get_option_int_value (arg_map, UNLOAD_STRING_BUFFER_SIZE_S);
  if (g_pre_alloc_varchar_size < 0 || g_pre_alloc_varchar_size > MAX_PRE_ALLOC_VARCHAR_SIZE)
    {
      fprintf (stderr, "\nThe number of '--%s' option ranges from 0 to %d.\n", UNLOAD_STRING_BUFFER_SIZE_L,
	       MAX_PRE_ALLOC_VARCHAR_SIZE);
      goto end;
    }

  g_request_pages = utility_get_option_int_value (arg_map, UNLOAD_REQUEST_PAGES_S);
  if (g_request_pages < 0 || g_request_pages > MAX_REQ_DATA_PAGES)
    {
      fprintf (stderr, "\nThe number of '--%s' option ranges from 0 to %d.\n", UNLOAD_REQUEST_PAGES_L,
	       MAX_REQ_DATA_PAGES);
      goto end;
    }

  if (verbose_flag)
    {
      enhanced_estimates = utility_get_option_bool_value (arg_map, UNLOAD_ENHANCED_ESTIMATES_S);
    }

  if (!do_schema)
    {
      sampling_records = utility_get_option_int_value (arg_map, UNLOAD_SAMPLING_TEST_S);
      if (sampling_records < -1)
	{
	  fprintf (stderr, "\nThe number of '--%s' option ranges from 0 to %d.\n", UNLOAD_SAMPLING_TEST_L, INT_MAX);
	  goto end;
	}

      thread_count = utility_get_option_int_value (arg_map, UNLOAD_THREAD_COUNT_S);
      if ((thread_count < 0) || (thread_count > MAX_THREAD_COUNT))
	{
	  fprintf (stderr, "\nThe number of '--%s' option ranges from 0 to %d.\n", UNLOAD_THREAD_COUNT_L,
		   MAX_THREAD_COUNT);
	  goto end;
	}
    }

  /* depreciated */
  utility_get_option_bool_value (arg_map, UNLOAD_USE_DELIMITER_S);

  status = 0;			// success
  if (database_name == NULL)
    {
      status = 1;
      /* TODO: print usage */
      util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
      goto end;
    }

  if (!output_prefix)
    {
      output_prefix = database_name;
    }

  /* create here the first filename to raise error early in case output file is incorrect */
  if (create_filename_schema (output_dirname, output_prefix, output_filename_schema,
			      sizeof (output_filename_schema)) != 0)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
      goto end;
    }

  if (!input_filename)
    {
      required_class_only = false;
    }
  if (required_class_only && include_references)
    {
      include_references = false;
      fprintf (stderr, "warning: '-ir' option is ignored.\n");
      fflush (stderr);
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", database_name, exec_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  sysprm_set_force (prm_get_name (PRM_ID_JAVA_STORED_PROCEDURE), "no");

  /*
   * Open db
   */
  if (user == NULL || user[0] == '\0')
    {
      user = (char *) "DBA";
    }

  error = db_restart_ex (exec_name, database_name, user, password, NULL, DB_CLIENT_TYPE_ADMIN_UTILITY);
  if (error == NO_ERROR)
    {
      /* pass */
    }
  else if (password == NULL && db_error_code () == ER_AU_INVALID_PASSWORD)
    {
      /* console input a password */
      password =
	getpass (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_UNLOADDB, UNLOADDB_MSG_PASSWORD_PROMPT));
      error = db_restart_ex (exec_name, database_name, user, password, NULL, DB_CLIENT_TYPE_ADMIN_UTILITY);
      if (error != NO_ERROR)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s: %s\n", exec_name, db_error_string (3));
	  status = error;
	  goto end;
	}
    }
  else if (error != NO_ERROR)
    {
      /* error */
      PRINT_AND_LOG_ERR_MSG ("%s: %s\n", exec_name, db_error_string (3));
      status = error;
      goto end;
    }

  ignore_err_flag = prm_get_bool_value (PRM_ID_UNLOADDB_IGNORE_ERROR);

  if (!status)
    {
      db_set_lock_timeout (prm_get_integer_value (PRM_ID_UNLOADDB_LOCK_TIMEOUT));
    }

  class_table = locator_get_all_mops (sm_Root_class_mop, DB_FETCH_READ, NULL);
  if (input_filename)
    {
      /* It may not be needed */
      if (locator_decache_all_lock_instances (sm_Root_class_mop) != NO_ERROR)
	{
	  util_log_write_errstr ("%s\n", db_error_string (3));
	  status = 1;
	  goto end;
	}
    }

  if (class_table == NULL)
    {
      util_log_write_errstr ("%s\n", db_error_string (3));
      status = 1;
      goto end;
    }

  req_class_table = (DB_OBJECT **) malloc (DB_SIZEOF (void *) * class_table->num);
  if (req_class_table == NULL)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_NO_MEM);
      status = 1;
      goto end;
    }

  for (i = 0; i < class_table->num; ++i)
    {
      req_class_table[i] = NULL;
    }

  if (get_requested_classes (input_filename, req_class_table) != 0)
    {
      util_log_write_errstr ("%s\n", db_error_string (3));
      status = 1;
      goto end;
    }

  if (input_filename)
    {
      int error;

      for (i = 0; req_class_table[i]; i++)
	{
	  error = au_fetch_class (req_class_table[i], NULL, AU_FETCH_READ, AU_SELECT);
	  if (error != NO_ERROR)
	    {
	      /* A required class is not granted. */
	      MOBJ object = NULL;

	      ws_find (req_class_table[i], &object);
	      if (object != NULL)
		{
		  PRINT_AND_LOG_ERR_MSG ("%s: %s\n", sm_ch_name (object), db_error_string (3));
		}
	      status = 1;
	    }
	}

      if (status != 0)
	{
	  goto end;
	}
    }

  if (!status && (do_schema || !do_objects))
    {
      char indexes_output_filename[PATH_MAX * 2];
      char trigger_output_filename[PATH_MAX * 2];

      if (create_filename_schema (output_dirname, output_prefix, output_filename_schema,
				  sizeof (output_filename_schema)) != 0)
	{
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
	  goto end;
	}

      /* 
       *  If you are in multi-process mode, this part must be done only in the first process.
       */
      if (create_filename_trigger (output_dirname, output_prefix, trigger_output_filename,
				   sizeof (trigger_output_filename)) != 0)
	{
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
	  goto end;
	}

      if (create_filename_indexes (output_dirname, output_prefix, indexes_output_filename,
				   sizeof (indexes_output_filename)) != 0)
	{
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
	  goto end;
	}

      /* do authorization as well in extractschema () */
      unload_context.do_auth = 1;
      unload_context.storage_order = order;
      unload_context.exec_name = exec_name;
      unload_context.output_prefix = output_prefix;
      unload_context.output_dirname = output_dirname;

      if (extract_classes_to_file (unload_context, output_filename_schema) != 0)
	{
	  status = 1;
	}

      if (!status && extract_triggers_to_file (unload_context, trigger_output_filename) != 0)
	{
	  status = 1;
	}

      if (!status && extract_indexes_to_file (unload_context, indexes_output_filename) != 0)
	{
	  status = 1;
	}

      unload_context.clear_schema_workspace ();
    }

  AU_SAVE_AND_ENABLE (au_save);
  if (!status && (do_objects || !do_schema))
    {
      unload_context.exec_name = exec_name;
      unload_context.output_prefix = output_prefix;
      unload_context.output_dirname = output_dirname;

      struct timeval startTime, endTime;
      double diffTime;
      if (sampling_records >= 0)
	{
	  gettimeofday (&startTime, NULL);
	}

      if (extract_objects (unload_context, output_dirname, thread_count, sampling_records, enhanced_estimates))
	{
	  status = 1;
	}

      if (sampling_records >= 0)
	{
	  gettimeofday (&endTime, NULL);
	  int elapsed_sec = 0, elapsed_usec = 0;

	  elapsed_sec = endTime.tv_sec - startTime.tv_sec;
	  elapsed_usec = endTime.tv_usec - startTime.tv_usec;
	  if (endTime.tv_usec < startTime.tv_usec)
	    {
	      elapsed_usec += 1000000;
	      elapsed_sec--;
	    }

	  fprintf (stdout, "Elapsed= %.6f sec\n", elapsed_sec + ((double) elapsed_usec / 1000000));
	}
    }
  AU_RESTORE (au_save);

  /* if an error occur, print error message */
  if (status)
    {
      if (db_error_code () != NO_ERROR)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s: %s\n", exec_name, db_error_string (3));
	}
    }

  /*
   * Shutdown db
   */
  error = db_shutdown ();
  if (error != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s: %s\n", exec_name, db_error_string (3));
      status = error;
    }

end:
  if (class_table)
    {
      locator_free_list_mops (class_table);
    }
  if (req_class_table)
    {
      free_and_init (req_class_table);
    }

  unload_context.clear_schema_workspace ();

  return status;
}
