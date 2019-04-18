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
 * load_db.c - Main for database loader
 */

#include "config.h"

#include "chartype.h"
#include "db.h"
#include "load_object.h"
#if defined (SA_MODE)
#include "load_sa_loader.hpp"
#endif // SA_MODE
#include "network_interface_cl.h"
#include "porting.h"
#include "schema_manager.h"
#include "transform.h"
#include "utility.h"

#include <fstream>
#include <thread>

#define LOAD_INDEX_MIN_SORT_BUFFER_PAGES 8192
#define LOAD_INDEX_MIN_SORT_BUFFER_PAGES_STRING "8192"
#define LOADDB_LOG_FILENAME_SUFFIX "loaddb.log"

using namespace cubload;

static int schema_file_start_line = 1;
static int index_file_start_line = 1;

static FILE *loaddb_log_file;

int interrupt_query = false;
bool load_interrupted = false;

static int ldr_validate_object_file (const char *argv0, load_args * args);
static int ldr_check_file_name_and_line_no (load_args * args);
static int loaddb_internal (UTIL_FUNCTION_ARG * arg, int dba_mode);
static void ldr_exec_query_interrupt_handler (void);
static int ldr_exec_query_from_file (const char *file_name, FILE * input_stream, int *start_line, load_args * args);
static int ldr_compare_attribute_with_meta (char *table_name, char *meta, DB_ATTRIBUTE * attribute);
static int ldr_compare_storage_order (FILE * schema_file);
static void get_loaddb_args (UTIL_ARG_MAP * arg_map, load_args * args);

static void ldr_server_load (load_args * args, int *status, bool * interrupted);
static void register_signal_handlers ();
static int load_object_file (load_args * args);
static void print_er_msg ();

/*
 * print_log_msg - print log message
 *    return: void
 *    verbose(in): if set also print the message to standard output
 *    fmt(in): string format
 *    ...(in): string format arguments
 */
void
print_log_msg (int verbose, const char *fmt, ...)
{
  va_list ap;

  if (verbose)
    {
      va_start (ap, fmt);
      vprintf (fmt, ap);
      fflush (stdout);
      va_end (ap);
    }

  if (loaddb_log_file != NULL)
    {
      va_start (ap, fmt);
      vfprintf (loaddb_log_file, fmt, ap);
      fflush (loaddb_log_file);
      va_end (ap);
    }
  else
    {
      assert (false);
    }
}

static void
print_er_msg ()
{
  if (!er_has_error ())
    {
      return;
    }

  fprintf (stderr, "%s\n", er_msg ());
}

/*
 * load_usage() - print an usage of the load-utility
 *   return: void
 */
static void
load_usage (const char *argv0)
{
  const char *exec_name;

  exec_name = basename ((char *) argv0);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_USAGE), exec_name);
}

/*
 * ldr_validate_object_file - check input file arguments
 *    return: NO_ERROR if successful, ER_FAILED if error
 */
static int
ldr_validate_object_file (const char *argv0, load_args * args)
{
  if (args->volume.empty ())
    {
      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_MISSING_DBNAME));
      load_usage (argv0);
      return ER_FAILED;
    }

  if (args->input_file.empty () && args->object_file.empty () && args->server_object_file.empty ())
    {
      /* if schema/index file are specified, process them only */
      if (args->schema_file.empty () && args->index_file.empty ())
	{
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
	  load_usage (argv0);
	  return ER_FAILED;
	}
    }
  else if (!args->input_file.empty () && !args->object_file.empty () && args->input_file != args->object_file)
    {
      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_GENERAL, MSGCAT_GENERAL_ARG_DUPLICATE),
			     "input-file");
      return ER_FAILED;
    }
  else
    {
      if (args->object_file.empty ())
	{
	  args->object_file = args->input_file;
	}
    }

  return NO_ERROR;
}

/*
 * ldr_check_file_name_and_line_no - parse schema file option
 *    return: void
 */
static int
ldr_check_file_name_and_line_no (load_args * args)
{
  char *p, *q;

  if (!args->schema_file.empty ())
    {
      /* *INDENT-OFF* */
      p = strchr (const_cast<char *> (args->schema_file.c_str ()), ':');
      /* *INDENT-ON* */
      if (p != NULL)
	{
	  for (q = p + 1; *q; q++)
	    {
	      if (!char_isdigit (*q))
		{
		  break;
		}
	    }
	  if (*q == 0)
	    {
	      schema_file_start_line = atoi (p + 1);
	      *p = 0;
	    }
	}
    }

  if (!args->index_file.empty ())
    {
      /* *INDENT-OFF* */
      p = strchr (const_cast<char *> (args->index_file.c_str ()), ':');
      /* *INDENT-ON* */
      if (p != NULL)
	{
	  for (q = p + 1; *q; q++)
	    {
	      if (!char_isdigit (*q))
		{
		  break;
		}
	    }
	  if (*q == 0)
	    {
	      index_file_start_line = atoi (p + 1);
	      *p = 0;
	    }
	}
    }

  return 0;
}

static char *
ldr_get_token (char *str, char **out_str, char start, char end)
{
  char *p = NULL;

  if (out_str)
    {
      *out_str = NULL;
    }

  if (str == NULL)
    {
      return NULL;
    }

  if (*str == start)
    {
      str++;
      p = str;
    }
  while (*str != end && *str != '\0')
    {
      str++;
    }

  if (*str != end)
    {
      return NULL;
    }

  *str = '\0';
  *out_str = p;
  return str + 1;
}

/*
 * ldr_compare_attribute_with_meta -
 *    return: NO_ERROR if successful, ER_FAILED otherwise
 */
static int
ldr_compare_attribute_with_meta (char *table_name, char *meta, DB_ATTRIBUTE * attribute)
{
  int error = NO_ERROR;
  char *p, *name_str, *type_str, *order_str, *storage_order_str, *shared_str;
  int type, order, storage_order, shared;

  if (meta == NULL || attribute == NULL)
    {
      return ER_FAILED;
    }

  p = meta;
  p = ldr_get_token (p, &name_str, '[', ']');
  p = ldr_get_token (p, &type_str, '(', ')');
  p = ldr_get_token (p, &order_str, '(', ')');
  p = ldr_get_token (p, &storage_order_str, '(', ')');
  p = ldr_get_token (p, &shared_str, '(', ')');
  if (name_str == NULL || type_str == NULL || order_str == NULL || storage_order_str == NULL)
    {
      print_log_msg (1, "\nCan not build meta: %s", table_name);
      error = ER_FAILED;
      goto compare_end;
    }
  if (parse_int (&type, type_str, 10) != NO_ERROR || parse_int (&order, order_str, 10) != NO_ERROR
      || parse_int (&storage_order, storage_order_str, 10) != NO_ERROR || (shared_str[0] != 'S'
									   && shared_str[0] != 'I'))
    {
      print_log_msg (1, "\nCan not build meta: %s", table_name);
      error = ER_FAILED;
      goto compare_end;
    }
  shared = shared_str[0] == 'S';

  if (type != db_attribute_type (attribute))
    {
      print_log_msg (1, "\nThe type of attribute (%s.%s)" " does not match with the meta information.", table_name,
		     db_attribute_name (attribute));
      error = ER_FAILED;
      goto compare_end;
    }
  if (order != db_attribute_order (attribute))
    {
      print_log_msg (1, "\nThe order of attribute (%s.%s)" " does not match with the meta information.", table_name,
		     db_attribute_name (attribute));
      error = ER_FAILED;
      goto compare_end;
    }
  if (storage_order != attribute->storage_order)
    {
      print_log_msg (1, "\nThe storage order of attribute (%s.%s)" " does not match with the meta information.",
		     table_name, db_attribute_name (attribute));
      error = ER_FAILED;
      goto compare_end;
    }
  if (shared != db_attribute_is_shared (attribute))
    {
      print_log_msg (1, "\nThe shared flag of attribute (%s.%s)" " does not match with the meta information.",
		     table_name, db_attribute_name (attribute));
      error = ER_FAILED;
      goto compare_end;
    }
  if (strcmp (name_str, db_attribute_name (attribute)) != 0)
    {
      print_log_msg (1, "\nThe name of attribute (%s.%s)" " does not match with the meta information.", table_name,
		     db_attribute_name (attribute));
      error = ER_FAILED;
      goto compare_end;
    }

compare_end:
  return error;
}

/*
 * ldr_compare_storage_order -
 *    return: NO_ERROR if successful, ER_FAILED otherwise
 *    compare_tables(in): two tables in a string (ex. foo:bar,apple:banana)
 */
static int
ldr_compare_storage_order (FILE * schema_file)
{
  char *table_name;
  char *p, *colon, *comma;
  DB_ATTRIBUTE *attribute;
  DB_OBJECT *table_object;
  char line[LINE_MAX * 100];
  size_t line_size = LINE_MAX * 100;
  int error = NO_ERROR;

  if (schema_file == NULL)
    {
      return error;
    }

  if (fseek (schema_file, 0, SEEK_SET) == -1)
    {
      print_log_msg (1, "\nCan not build meta...");
      return ER_FAILED;
    }

  while (true)
    {
      p = fgets (line, (int) line_size - 1, schema_file);
      if (p == NULL)
	{
	  break;
	}

      if (strncmp (p, "-- !META! ", 10) != 0)
	{
	  continue;
	}
      p += 10;
      colon = strchr (p, ':');
      if (colon == NULL)
	{
	  print_log_msg (1, "\nInvalid meta: %s", line);
	  error = ER_FAILED;
	  goto compare_end;
	}

      *colon = '\0';
      p = ldr_get_token (p, &table_name, '[', ']');
      table_object = db_find_class (table_name);
      if (table_object == NULL)
	{
	  print_log_msg (1, "\nCan not find table: %s", table_name);
	  error = ER_FAILED;
	  goto compare_end;
	}

      p = colon + 1;
      comma = p;
      attribute = db_get_attributes (table_object);
      for (; attribute && comma; attribute = db_attribute_next (attribute))
	{
	  if (db_attribute_class (attribute) != table_object)
	    {
	      continue;
	    }

	  comma = strchr (p, ',');
	  if (comma)
	    {
	      *comma = '\0';
	    }
	  if (ldr_compare_attribute_with_meta (table_name, p, attribute) != NO_ERROR)
	    {
	      print_log_msg (1,
			     "\nThe table %s is not suitable to be"
			     " replicated since it is different from the original.", table_name);
	      error = ER_FAILED;
	      goto compare_end;
	    }
	  p = comma + 1;
	}

      if (attribute || comma)
	{
	  print_log_msg (1, "\nThe number of columns of %s is different" " from meta information.", table_name);
	  error = ER_FAILED;
	  goto compare_end;
	}
    }

compare_end:
  return error;
}

/*
 * loaddb_internal - internal main loaddb function
 *    return: NO_ERROR if successful, error code otherwise
 *    argc(in): argc of main
 *    argv(in): argv of main
 *    dba_mode(in):
 */
static int
loaddb_internal (UTIL_FUNCTION_ARG * arg, int dba_mode)
{
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  int error = NO_ERROR;
  /* set to static to avoid compiler warning (clobbered by longjump) */
  static FILE *schema_file = NULL;
  static FILE *index_file = NULL;
  FILE *error_file = NULL;

  char *passwd;
  int status = 0;
  /* set to static to avoid compiler warning (clobbered by longjump) */
  static bool interrupted = false;
  int au_save = 0;
  extern bool obt_Enable_autoincrement;
  char log_file_name[PATH_MAX];
  const char *msg_format;
  obt_Enable_autoincrement = false;
  load_args args;

  /* *INDENT-OFF* */
  static std::ifstream object_file;
  /* *INDENT-ON* */

  get_loaddb_args (arg_map, &args);

  if (ldr_validate_object_file (arg->argv0, &args) != NO_ERROR)
    {
      status = 1;
      goto error_return;
    }

  /* error message log file */
  sprintf (log_file_name, "%s_%s.err", args.volume.c_str (), arg->command_name);
  er_init (log_file_name, ER_NEVER_EXIT);

  if (!args.index_file.empty () && prm_get_integer_value (PRM_ID_SR_NBUFFERS) < LOAD_INDEX_MIN_SORT_BUFFER_PAGES)
    {
      sysprm_set_force (prm_get_name (PRM_ID_SR_NBUFFERS), LOAD_INDEX_MIN_SORT_BUFFER_PAGES_STRING);
    }

  sysprm_set_force (prm_get_name (PRM_ID_JAVA_STORED_PROCEDURE), "no");

  /* open loaddb log file */
  sprintf (log_file_name, "%s_%s", args.volume.c_str (), LOADDB_LOG_FILENAME_SUFFIX);
  loaddb_log_file = fopen (log_file_name, "w+");
  if (loaddb_log_file == NULL)
    {
      PRINT_AND_LOG_ERR_MSG ("Cannot open log file %s\n", log_file_name);
      status = 2;
      goto error_return;
    }

  /* login */
  if (!args.user_name.empty () || !dba_mode)
    {
      (void) db_login (args.user_name.c_str (), args.password.c_str ());
      error = db_restart (arg->command_name, true, args.volume.c_str ());
      if (error != NO_ERROR)
	{
	  if (error == ER_AU_INVALID_PASSWORD)
	    {
	      /* prompt for password and try again */
	      passwd =
		getpass (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_PASSWORD_PROMPT));
	      if (!strlen (passwd))
		{
		  passwd = NULL;
		}
	      (void) db_login (args.user_name.c_str (), passwd);
	      error = db_restart (arg->command_name, true, args.volume.c_str ());
	    }
	}
    }
  else
    {
      /* if we're in the protected DBA mode, just login without authorization */
      AU_DISABLE_PASSWORDS ();
      db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
      (void) db_login ("DBA", NULL);
      error = db_restart (arg->command_name, true, args.volume.c_str ());
    }

  if (error != NO_ERROR)
    {
      if (er_errid () < ER_FAILED)
	{
	  // an error was set.
	  print_log_msg (1, "%s\n", db_error_string (3));
	  util_log_write_errstr ("%s\n", db_error_string (3));
	}
      else
	{
	  PRINT_AND_LOG_ERR_MSG ("Cannot restart database %s\n", args.volume.c_str ());
	}
      status = 3;
      goto error_return;
    }

  /* disable trigger actions to be fired */
  db_disable_trigger ();

  /* check if schema/index/object files exist */
  ldr_check_file_name_and_line_no (&args);

  if (!args.schema_file.empty ())
    {
      schema_file = fopen (args.schema_file.c_str (), "r");
      if (schema_file == NULL)
	{
	  msg_format = msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_BAD_INFILE);
	  print_log_msg (1, msg_format, args.schema_file.c_str ());
	  util_log_write_errstr (msg_format, args.schema_file.c_str ());
	  status = 2;
	  goto error_return;
	}
    }
  if (!args.index_file.empty ())
    {
      index_file = fopen (args.index_file.c_str (), "r");
      if (index_file == NULL)
	{
	  msg_format = msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_BAD_INFILE);
	  print_log_msg (1, msg_format, args.index_file.c_str ());
	  util_log_write_errstr (msg_format, args.index_file.c_str ());
	  status = 2;
	  goto error_return;
	}
    }
  if (!args.object_file.empty ())
    {
      /* *INDENT-OFF* */
      object_file.open (args.object_file, std::fstream::in | std::fstream::binary);
      /* *INDENT-ON* */

      if (!object_file.is_open () || !object_file.good ())
	{
	  msg_format = msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_BAD_INFILE);
	  print_log_msg (1, msg_format, args.object_file.c_str ());
	  util_log_write_errstr (msg_format, args.object_file.c_str ());
	  status = 2;
	  goto error_return;
	}

      object_file.close ();
    }

  if (!args.ignore_class_file.empty ())
    {
      int retval = args.parse_ignore_class_file ();
      if (retval < 0)
	{
	  status = 2;
	  goto error_return;
	}
    }

  /* Disallow syntax only and load only options together */
  if (args.load_only && args.syntax_check)
    {
      msg_format = msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_INCOMPATIBLE_ARGS);
      print_log_msg (1, msg_format, "--" LOAD_LOAD_ONLY_L, "--" LOAD_CHECK_ONLY_L);
      util_log_write_errstr (msg_format, "--" LOAD_LOAD_ONLY_L, "--" LOAD_CHECK_ONLY_L);
      status = 1;		/* parsing error */
      goto error_return;
    }

  if (!args.error_file.empty ())
    {
      if (args.syntax_check)
	{
	  msg_format = msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_INCOMPATIBLE_ARGS);
	  print_log_msg (1, msg_format, "--" LOAD_ERROR_CONTROL_FILE_L, "--" LOAD_CHECK_ONLY_L);
	  util_log_write_errstr (msg_format, "--" LOAD_ERROR_CONTROL_FILE_L, "--" LOAD_CHECK_ONLY_L);
	  status = 1;		/* parsing error */
	  goto error_return;
	}
      error_file = fopen_ex (args.error_file.c_str (), "rt");
      if (error_file == NULL)
	{
	  msg_format = msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_BAD_INFILE);
	  print_log_msg (1, msg_format, args.error_file.c_str ());
	  util_log_write_errstr (msg_format, args.error_file.c_str ());
	  status = 2;
	  goto error_return;
	}
      er_filter_fileset (error_file);
      fclose (error_file);
      get_ignored_errors (args.m_ignored_errors);
    }

  /* check if no log option can be applied */
  if (error || (args.ignore_logging != 0 && locator_log_force_nologging () != NO_ERROR))
    {
      /* couldn't log in */
      print_log_msg (1, "%s\n", db_error_string (3));
      util_log_write_errstr ("%s\n", db_error_string (3));
      status = 3;
      db_end_session ();
      db_shutdown ();
      goto error_return;
    }

  if (args.load_only)
    {
      /* This is the default behavior. It is changed from the old one so we notify the user. */
      print_log_msg (1, "\n--load-only is deprecated. To check the object file \
                         for any syntax errors use --data-file-check-only.\n");
    }

  /* if schema file is specified, do schema loading */
  if (schema_file != NULL)
    {
      print_log_msg (1, "\nStart schema loading.\n");

      /*
       * CUBRID 8.2 should be compatible with earlier versions of CUBRID.
       * Therefore, we do not perform user authentication when the loader
       * is executing by DBA group user.
       */
      if (au_is_dba_group_member (Au_user))
	{
	  AU_DISABLE (au_save);
	}

      if (ldr_exec_query_from_file (args.schema_file.c_str (), schema_file, &schema_file_start_line, &args) != 0)
	{
	  print_log_msg (1, "\nError occurred during schema loading." "\nAborting current transaction...");
	  msg_format = "Error occurred during schema loading." "Aborting current transaction...\n";
	  util_log_write_errstr (msg_format);
	  status = 3;
	  db_end_session ();
	  db_shutdown ();
	  print_log_msg (1, " done.\n\nRestart loaddb with '-%c %s:%d' option\n", LOAD_SCHEMA_FILE_S,
			 args.schema_file.c_str (), schema_file_start_line);
	  goto error_return;
	}

      if (au_is_dba_group_member (Au_user))
	{
	  AU_ENABLE (au_save);
	}

      print_log_msg (1, "Schema loading from %s finished.\n", args.schema_file.c_str ());

      /* update catalog statistics */
      AU_DISABLE (au_save);
      sm_update_all_catalog_statistics (STATS_WITH_FULLSCAN);
      AU_ENABLE (au_save);

      print_log_msg (1, "Statistics for Catalog classes have been updated.\n\n");

      if (args.compare_storage_order)
	{
	  if (ldr_compare_storage_order (schema_file) != NO_ERROR)
	    {
	      status = 3;
	      db_end_session ();
	      db_shutdown ();
	      print_log_msg (1, "\nAborting current transaction...\n");
	      goto error_return;
	    }
	}

      db_commit_transaction ();
      fclose (schema_file);
      schema_file = NULL;
    }

  if (args.syntax_check)
    {
      print_log_msg (1, "\nStart object syntax checking.\n");
    }
  else
    {
      print_log_msg (1, "\nStart object loading.\n");
    }
#if defined (SA_MODE)
  ldr_sa_load (&args, &status, &interrupted);
#else // !SA_MODE = CS_MODE
  ldr_server_load (&args, &status, &interrupted);
#endif // !SA_MODE = CS_MODE

  /* if index file is specified, do index creation */
  if (!interrupted && index_file != NULL)
    {
      print_log_msg (1, "\nStart index loading.\n");
      if (ldr_exec_query_from_file (args.index_file.c_str (), index_file, &index_file_start_line, &args) != 0)
	{
	  print_log_msg (1, "\nError occurred during index loading." "\nAborting current transaction...");
	  msg_format = "Error occurred during index loading." "Aborting current transaction...\n";
	  util_log_write_errstr (msg_format);
	  status = 3;
	  db_end_session ();
	  db_shutdown ();
	  print_log_msg (1, " done.\n\nRestart loaddb with '-%c %s:%d' option\n", LOAD_INDEX_FILE_S,
			 args.index_file.c_str (), index_file_start_line);
	  goto error_return;
	}

      /* update catalog statistics */
      AU_DISABLE (au_save);
      sm_update_catalog_statistics (CT_INDEX_NAME, STATS_WITH_FULLSCAN);
      sm_update_catalog_statistics (CT_INDEXKEY_NAME, STATS_WITH_FULLSCAN);
      AU_ENABLE (au_save);

      print_log_msg (1, "Index loading from %s finished.\n", args.index_file.c_str ());
      db_commit_transaction ();
    }

  if (index_file != NULL)
    {
      fclose (index_file);
      index_file = NULL;
    }

  print_log_msg ((int) args.verbose, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_CLOSING));
  (void) db_end_session ();
  (void) db_shutdown ();

  fclose (loaddb_log_file);

  return status;

error_return:
  if (schema_file != NULL)
    {
      fclose (schema_file);
    }
  if (object_file.is_open ())
    {
      object_file.close ();
    }
  if (index_file != NULL)
    {
      fclose (index_file);
    }
  if (loaddb_log_file != NULL)
    {
      fclose (loaddb_log_file);
    }

  return status;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * loaddb_dba - loaddb in DBA mode
 *    return:  NO_ERROR if successful, error code otherwise
 *    argc(in): argc in main
 *    argv(in): argv in main
 */
int
loaddb_dba (UTIL_FUNCTION_ARG * arg)
{
  return loaddb_internal (arg, 1);
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * loaddb_user - loaddb in user mode
 *    return:  NO_ERROR if successful, error code otherwise
 *    argc(in): argc in main
 *    argv(in): argv in main
 */
int
loaddb_user (UTIL_FUNCTION_ARG * arg)
{
  return loaddb_internal (arg, 0);
}

/*
 * ldr_exec_query_interrupt_handler - signal handler registered via
 * util_arm_signal_handlers
 *    return: void
 */
static void
ldr_exec_query_interrupt_handler (void)
{
  interrupt_query = true;

  log_set_interrupt (true);
}

/*
 * ldr_exec_query_from_file - execute queries from file
 *    return: 0 if successful, non-zero otherwise
 *    file_name(in): file path
 *    file(in): FILE *
 *    start_line(in): start line
 *    commit_period(in): commit period
 */
static int
ldr_exec_query_from_file (const char *file_name, FILE * input_stream, int *start_line, load_args * args)
{
  DB_SESSION *session = NULL;
  DB_QUERY_RESULT *res = NULL;
  int error = NO_ERROR;
  int stmt_cnt, stmt_id = 0, stmt_type;
  int executed_cnt = 0;
  int parser_start_line_no;
  int parser_end_line_no = 1;
  int check_line_no = true;

  if ((*start_line) > 1)
    {
      int line_count = *start_line - 1;

      do
	{
	  int c = fgetc (input_stream);
	  if (c == EOF)
	    {
	      print_log_msg (1,
			     msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_UNREACHABLE_LINE),
			     file_name, *start_line);
	      error = ER_GENERIC_ERROR;
	      goto end;
	    }
	  else if (c == '\n')
	    {
	      line_count--;
	    }
	}
      while (line_count > 0);
    }

  check_line_no = false;
  session = db_make_session_for_one_statement_execution (input_stream);
  if (session == NULL)
    {
      print_log_msg (1, "ERROR: %s\n", db_error_string (3));
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto end;
    }

  util_arm_signal_handlers (&ldr_exec_query_interrupt_handler, &ldr_exec_query_interrupt_handler);

  while (true)
    {
      if (interrupt_query)
	{
	  if (er_errid () != ER_INTERRUPTED)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
	    }
	  error = er_errid ();
	  db_close_session (session);
	  goto end;
	}
      parser_start_line_no = parser_end_line_no;

      stmt_cnt = db_parse_one_statement (session);
      if (stmt_cnt > 0)
	{
	  db_get_parser_line_col (session, &parser_end_line_no, NULL);
	  stmt_id = db_compile_statement (session);
	}

      if (stmt_cnt <= 0 || stmt_id <= 0)
	{
	  DB_SESSION_ERROR *session_error;
	  int line, col;
	  if ((session_error = db_get_errors (session)) != NULL)
	    {
	      do
		{
		  session_error = db_get_next_error (session_error, &line, &col);
		  if (line >= 0)
		    {
		      print_log_msg (1, "In %s line %d,\n", file_name, line + (*start_line));
		      print_log_msg (1, "ERROR: %s \n", db_error_string (3));
		      assert (er_errid () != NO_ERROR);
		      error = er_errid ();
		    }
		}
	      while (session_error);
	    }
	  db_close_session (session);
	  break;
	}

      stmt_type = db_get_statement_type (session, stmt_id);

      res = (DB_QUERY_RESULT *) NULL;
      error = db_execute_statement (session, stmt_id, &res);

      if (error < 0)
	{
	  print_log_msg (1, "ERROR: %s\n", db_error_string (3));
	  db_close_session (session);
	  break;
	}
      executed_cnt++;
      error = db_query_end (res);
      if (error < 0)
	{
	  print_log_msg (1, "ERROR: %s\n", db_error_string (3));
	  db_close_session (session);
	  break;
	}

      if (stmt_type == CUBRID_STMT_COMMIT_WORK
	  || (args->periodic_commit && (executed_cnt % args->periodic_commit == 0)))
	{
	  db_commit_transaction ();
	  print_log_msg (args->verbose_commit, "%8d statements executed. Commit transaction at line %d\n", executed_cnt,
			 parser_end_line_no);
	  *start_line = parser_end_line_no + 1;
	}
      print_log_msg ((int) args->verbose, "Total %8d statements executed.\r", executed_cnt);
      fflush (stdout);
    }

end:
  if (error < 0)
    {
      db_abort_transaction ();
    }
  else
    {
      *start_line = parser_end_line_no + 1;
      print_log_msg (1, "Total %8d statements executed.\n", executed_cnt);
      fflush (stdout);
      db_commit_transaction ();
    }
  return error;
}

static void
get_loaddb_args (UTIL_ARG_MAP * arg_map, load_args * args)
{
  assert (arg_map != NULL && args != NULL);

  std::string empty;

  char *volume = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  char *input_file = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 1);
  char *user_name = utility_get_option_string_value (arg_map, LOAD_USER_S, 0);
  char *password = utility_get_option_string_value (arg_map, LOAD_PASSWORD_S, 0);
  char *schema_file = utility_get_option_string_value (arg_map, LOAD_SCHEMA_FILE_S, 0);
  char *index_file = utility_get_option_string_value (arg_map, LOAD_INDEX_FILE_S, 0);
  char *object_file = utility_get_option_string_value (arg_map, LOAD_DATA_FILE_S, 0);
  char *server_object_file = utility_get_option_string_value (arg_map, LOAD_SERVER_DATA_FILE_S, 0);
  char *error_file = utility_get_option_string_value (arg_map, LOAD_ERROR_CONTROL_FILE_S, 0);
  char *table_name = utility_get_option_string_value (arg_map, LOAD_TABLE_NAME_S, 0);
  char *ignore_class_file = utility_get_option_string_value (arg_map, LOAD_IGNORE_CLASS_S, 0);

  args->volume = volume ? volume : empty;
  args->input_file = input_file ? input_file : empty;
  args->user_name = user_name ? user_name : empty;
  args->password = password ? password : empty;
  args->syntax_check = utility_get_option_bool_value (arg_map, LOAD_CHECK_ONLY_S);
  args->load_only = utility_get_option_bool_value (arg_map, LOAD_LOAD_ONLY_S);
  args->estimated_size = utility_get_option_int_value (arg_map, LOAD_ESTIMATED_SIZE_S);
  args->verbose = utility_get_option_bool_value (arg_map, LOAD_VERBOSE_S);
  args->disable_statistics = utility_get_option_bool_value (arg_map, LOAD_NO_STATISTICS_S);
  args->periodic_commit = utility_get_option_int_value (arg_map, LOAD_PERIODIC_COMMIT_S);
  args->verbose_commit = args->periodic_commit > 0;
  args->no_oid_hint = utility_get_option_bool_value (arg_map, LOAD_NO_OID_S);
  args->schema_file = schema_file ? schema_file : empty;
  args->index_file = index_file ? index_file : empty;
  args->object_file = object_file ? object_file : empty;
  args->server_object_file = server_object_file ? server_object_file : empty;
  args->error_file = error_file ? error_file : empty;
  args->ignore_logging = utility_get_option_bool_value (arg_map, LOAD_IGNORE_LOGGING_S);
  args->compare_storage_order = utility_get_option_bool_value (arg_map, LOAD_COMPARE_STORAGE_ORDER_S);
  args->table_name = table_name ? table_name : empty;
  args->ignore_class_file = ignore_class_file ? ignore_class_file : empty;
}

static void
ldr_server_load (load_args * args, int *status, bool * interrupted)
{
  register_signal_handlers ();

  int error_code = loaddb_init (*args);
  if (error_code != NO_ERROR)
    {
      print_er_msg ();
      *status = 3;
      return;
    }

  error_code = load_object_file (args);
  if (error_code != NO_ERROR)
    {
      print_er_msg ();
      *status = 3;
    }

  stats stats;
  int prev_rows_committed = 0;
  do
    {
      if (load_interrupted)
	{
	  *interrupted = true;
	  *status = 3;
	  break;
	}

      error_code = loaddb_fetch_stats (&stats);
      if (error_code != NO_ERROR)
	{
	  print_er_msg ();
	  *status = 3;
	  break;
	}

      if (!stats.error_message.empty ())
	{
	  /* Skip if syntax check only is enabled since we do not want to stop on error. */
	  if (!args->syntax_check)
	    {
	      *status = 3;
	      fprintf (stderr, "%s", stats.error_message.c_str ());
	    }
	}
      else
	{
	  int curr_rows_committed = stats.rows_committed;
	  // log committed instances msg only there was a commit since last check
	  if (curr_rows_committed > prev_rows_committed)
	    {
	      /* Don't print this during syntax checking */
	      if (!args->syntax_check)
		{
		  char *committed_instances_msg = msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB,
								  LOADDB_MSG_COMMITTED_INSTANCES);
		  print_log_msg (args->verbose_commit, committed_instances_msg, curr_rows_committed);
		}

	      prev_rows_committed = curr_rows_committed;
	    }
	}

	/* *INDENT-OFF* */
	std::this_thread::sleep_for (std::chrono::milliseconds (100));
	/* *INDENT-ON* */
    }
  while (!(stats.is_completed || stats.is_failed) && *status != 3);

  // fetch latest stats before destroying the session
  loaddb_fetch_stats (&stats);

  if (load_interrupted)
    {
      print_log_msg (1, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_SIG1));
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_LINE),
	       stats.current_line.load ());
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_INTERRUPTED_ABORT));
    }

  if (args->syntax_check)
    {
      if (!stats.error_message.empty ())
	{
	  fprintf (stderr, "%s", stats.error_message.c_str ());
	}

      print_log_msg (1,
		     msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_OBJECTS_SYNTAX_CHECKED),
		     stats.rows_committed, stats.rows_failed);
    }
  else
    {
      print_log_msg (1, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_INSERT_AND_FAIL_COUNT),
		     stats.rows_committed, stats.rows_failed);
    }

  error_code = loaddb_destroy ();
  if (error_code != NO_ERROR)
    {
      print_er_msg ();
      *status = 3;
    }

  if (load_interrupted)
    {
      print_log_msg (1, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_LAST_COMMITTED_LINE),
		     stats.last_committed_line);
    }
}

static void
register_signal_handlers ()
{
  /* *INDENT-OFF* */
  SIG_HANDLER sig_handler = [] ()
  {
    load_interrupted = true;
    loaddb_interrupt ();
  };
  /* *INDENT-ON* */

  // register handlers for SIGINT and SIGQUIT signals
  util_arm_signal_handlers (sig_handler, sig_handler);
}

static int
load_object_file (load_args * args)
{
  int error_code = NO_ERROR;

  // first try to load directly on server
  if (!args->server_object_file.empty ())
    {
      error_code = loaddb_load_object_file ();
      if (error_code == NO_ERROR)
	{
	  // load was performed successfully by the server
	  return error_code;
	}
      else if (error_code == ER_FILE_UNKNOWN_FILE)
	{
	  if (args->object_file.empty ())
	    {
	      fprintf (stderr, "ERROR: file %s does not exists on the server machine\n",
		       args->server_object_file.c_str ());
	      return error_code;
	    }

	  // server data file does not exists on server file system, try to load client data file
	  fprintf (stderr,
		   "ERROR: file %s does not exists on the server machine, try to load %s from the client machine\n",
		   args->server_object_file.c_str (), args->object_file.c_str ());
	}
      else
	{
	  // there was an error while loading server data file on server, therefore exit
	  return error_code;
	}
    }

  /* *INDENT-OFF* */
  batch_handler b_handler = [] (const batch &batch) -> int
    {
      int ret = loaddb_load_batch (batch);
      delete &batch;

      return ret;
    };
  batch_handler c_handler = [] (const batch &batch) -> int
    {
      int ret = loaddb_install_class (batch);
      delete &batch;

      return ret;
    };
  /* *INDENT-ON* */

  // here we are sure that object_file exists since it was validated by loaddb_internal function
  return split (args->periodic_commit, args->object_file, c_handler, b_handler);
}
