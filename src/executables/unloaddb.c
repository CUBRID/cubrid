/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

/*
 * unloaddb.c - emits database object definitions in object loader format
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include "authenticate.h"
#include "db.h"
#include "message_catalog.h"
#include "environment_variable.h"
#include "schema_manager.h"
#include "locator_cl.h"
#include "unloaddb.h"
#include "load_object.h"
#include "utility.h"

char *database_name = NULL;
const char *output_dirname = NULL;
char *input_filename = NULL;
FILE *output_file = NULL;
TEXT_OUTPUT object_output = { NULL, NULL, 0, 0, NULL };
TEXT_OUTPUT *obj_out = &object_output;
int page_size = 4096;
int cached_pages = 100;
int est_size = 0;
char *hash_filename = NULL;
int debug_flag = 0;
bool verbose_flag = false;
bool include_references = false;

bool required_class_only = false;
LIST_MOPS *class_table = NULL;
DB_OBJECT **req_class_table = NULL;

int lo_count = 0;

char *output_prefix = NULL;
bool do_schema = false;
bool do_objects = false;
bool delimited_id_flag = false;
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
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_UNLOADDB, 60), exec_name);
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
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  const char *exec_name = arg->command_name;
  int error;
  int status = 0;
  int i;

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      unload_usage (arg->argv0);
      return -1;
    }

  input_filename =
    utility_get_option_string_value (arg_map, UNLOAD_INPUT_CLASS_FILE_S, 0);
  include_references =
    utility_get_option_bool_value (arg_map, UNLOAD_INCLUDE_REFERENCE_S);
  required_class_only =
    utility_get_option_bool_value (arg_map, UNLOAD_INPUT_CLASS_ONLY_S);
  lo_count = utility_get_option_int_value (arg_map, UNLOAD_LO_COUNT_S);
  est_size = utility_get_option_int_value (arg_map, UNLOAD_ESTIMATED_SIZE_S);
  cached_pages =
    utility_get_option_int_value (arg_map, UNLOAD_CACHED_PAGES_S);
  output_dirname =
    utility_get_option_string_value (arg_map, UNLOAD_OUTPUT_PATH_S, 0);
  do_schema = utility_get_option_bool_value (arg_map, UNLOAD_SCHEMA_ONLY_S);
  do_objects = utility_get_option_bool_value (arg_map, UNLOAD_DATA_ONLY_S);
  output_prefix =
    utility_get_option_string_value (arg_map, UNLOAD_OUTPUT_PREFIX_S, 0);
  hash_filename =
    utility_get_option_string_value (arg_map, UNLOAD_HASH_FILE_S, 0);
  verbose_flag = utility_get_option_bool_value (arg_map, UNLOAD_VERBOSE_S);
  delimited_id_flag =
    utility_get_option_bool_value (arg_map, UNLOAD_USE_DELIMITER_S);
  database_name =
    utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);

  if (database_name == NULL)
    {
      status = 1;
      /* TODO: print usage */
      goto end;
    }

  if (!output_prefix)
    output_prefix = database_name;

  /*
   * Open db
   */
  AU_DISABLE_PASSWORDS ();
  if ((error = db_login ("dba", NULL)) ||
      (error = db_restart (arg->argv0, true, database_name)))
    {
      (void) fprintf (stderr, "%s: %s\n\n", exec_name, db_error_string (3));
      status = error;
    }
  else
    {
      ignore_err_flag = PRM_UNLOADDB_IGNORE_ERROR;

      if (db_set_isolation (TRAN_REP_CLASS_REP_INSTANCE) != NO_ERROR)
	status = 1;

      if (!status)
	{
	  db_set_lock_timeout (PRM_UNLOADDB_LOCK_TIMEOUT);
	}
      if (!input_filename)
	required_class_only = false;
      if (required_class_only && include_references)
	{
	  include_references = false;
	  fprintf (stdout, "warning: '-ir' option is ignored.\n");
	  fflush (stdout);
	}

      if (input_filename)
	{
	  class_table =
	    locator_get_all_mops (sm_Root_class_mop, DB_FETCH_READ);

	  /* It may not be needed */
	  if (locator_decache_all_lock_instances (sm_Root_class_mop) !=
	      NO_ERROR)
	    {
	      status = 1;
	      goto end;
	    }
	}
      else
	/* lock Root class with IS_LOCK instead of S_LOCK */
	class_table = locator_get_all_mops (sm_Root_class_mop, DB_FETCH_READ);

      if (class_table == NULL)
	{
	  status = 1;
	  goto end;
	}

      if ((req_class_table =
	   (DB_OBJECT **) malloc (DB_SIZEOF (void *) *
				  class_table->num)) == NULL)
	{
	  status = 1;
	  goto end;
	}
      for (i = 0; i < class_table->num; ++i)
	{
	  req_class_table[i] = NULL;
	}
      if (get_requested_classes (input_filename, req_class_table) != 0)
	{
	  status = 1;
	  goto end;
	}
      if (!status && (do_schema || !do_objects))
	if (extractschema ((char *) exec_name, 1))	/* do authorization as well */
	  {
	    status = 1;
	  }

      if (!status && (do_objects || !do_schema))
	if (extractobjects ((char *) exec_name))
	  {
	    status = 1;
	  }

      /* if an error occur, print error message */
      if (status)
	{
	  if (db_error_code () != NO_ERROR)
	    {
	      fprintf (stderr, "%s: %s\n\n", exec_name, db_error_string (3));
	    }
	}
      /*
       * Shutdown db
       */
      if ((error = db_shutdown ()))
	{
	  (void) fprintf (stderr, "%s: %s\n\n",
			  exec_name, db_error_string (3));
	  status = error;
	}
    }

end:
  if (class_table)
    locator_free_list_mops (class_table);
  if (req_class_table)
    free_and_init (req_class_table);
  return status;
}
