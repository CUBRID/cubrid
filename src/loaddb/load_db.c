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
#include "schema_system_catalog_constants.h"
#include "utility.h"
#include "authenticate.h"
#include "ddl_log.h"

#include <fstream>
#include <thread>


#define fclose_and_init(fp) \
        do { \
          if ((fp)) { \
          fclose ((fp)); \
          (fp) = NULL; \
          } \
	} while (0)

const int LOAD_INDEX_MIN_SORT_BUFFER_PAGES = 8192;
const char *LOAD_INDEX_MIN_SORT_BUFFER_PAGES_STRING = "8192";
const char *LOADDB_LOG_FILENAME_SUFFIX = "loaddb.log";

using namespace cubload;

static FILE *loaddb_log_file;

int interrupt_query = false;
bool load_interrupted = false;

typedef struct t_schema_file_list_info T_SCHEMA_FILE_LIST_INFO;
struct t_schema_file_list_info
{
  char schema_file_name[PATH_MAX];
  FILE *schema_fp;
};

static int ldr_validate_object_file (const char *argv0, load_args * args);
static int ldr_get_start_line_no (std::string & file_name);
static FILE *ldr_check_file (std::string & file_name, int &error_code);
static int loaddb_internal (UTIL_FUNCTION_ARG * arg, int dba_mode);
static void ldr_exec_query_interrupt_handler (void);
static int ldr_exec_query_from_file (const char *file_name, FILE * input_stream, int *start_line, load_args * args);
static int ldr_compare_attribute_with_meta (char *table_name, char *meta, DB_ATTRIBUTE * attribute);
static int ldr_compare_storage_order (FILE * schema_file);
static void get_loaddb_args (UTIL_ARG_MAP * arg_map, load_args * args);
/* *INDENT-OFF* */
static void print_stats (std::vector<cubload::stats> &stats, cubload::load_args &args, int *status);
/* *INDENT-ON* */

static void ldr_server_load (load_args * args, int *exit_status, bool * interrupted);
static void register_signal_handlers ();
/* *INDENT-OFF* */
static int load_has_authorization (const std::string & class_name, DB_AUTH au_type);
/* *INDENT-ON* */
static int load_object_file (load_args * args, int *exit_status);
static void print_er_msg ();

static T_SCHEMA_FILE_LIST_INFO **ldr_check_file_list (std::string & file_name, int &num_files, int &error_code);
static void ldr_free_and_fclose (T_SCHEMA_FILE_LIST_INFO ** file_list, int num);
static int ldr_load_schema_file (FILE * schema_fp, int schema_file_start_line, load_args args);

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

/* *INDENT-OFF* */
void
print_stats (std::vector<cubload::stats> &stats, cubload::load_args &args, int *status)
{
  for (const cubload::stats &stat : stats)
    {
      if (!stat.log_message.empty ())
	{
	  print_log_msg (args.verbose, stat.log_message.c_str ());
	}

      if (!stat.error_message.empty ())
	{
	  /* Skip if syntax check only is enabled since we do not want to stop on error. */
	  if (!args.syntax_check)
	    {
	      *status = 3;
	      fprintf (stderr, "%s", stat.error_message.c_str ());
	    }
	}
    }
}
/* *INDENT-ON* */

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

  if (args->cs_mode)
    {
      if (sysprm_load_and_init (args->volume.c_str (), NULL, SYSPRM_IGNORE_INTL_PARAMS) == NO_ERROR)
	{
	  if (prm_get_integer_value (PRM_ID_HA_MODE))
	    {
	      if ((args->index_file.empty () == false
		   || args->schema_file.empty () == false
		   || args->schema_file_list.empty () == false || args->trigger_file.empty () == false))
		{
		  PRINT_AND_LOG_ERR_MSG ("In loaddb CS (-C, --CS-mode) in HA mode, only OBJECT loading is possible.\n");
		  return ER_FAILED;
		}
	    }
	}
      else
	{
	  PRINT_AND_LOG_ERR_MSG ("loaddb: Cannot load system parameters.\n");
	  return ER_FAILED;
	}
    }

  if (args->schema_file.empty () == false && args->schema_file_list.empty () == false)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
      load_usage (argv0);
      return ER_FAILED;
    }

  if (args->input_file.empty () && args->object_file.empty ())
    {
      /* if schema/index file are specified, process them only */
      if (args->schema_file.empty () && args->schema_file_list.empty () && args->index_file.empty ())
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

static FILE *
ldr_check_file (std::string & file_name, int &error_code)
{
  FILE *file_p = NULL;
  error_code = NO_ERROR;

  if (!file_name.empty ())
    {
      file_p = fopen (file_name.c_str (), "r");
      if (file_p == NULL)
	{
	  const char *msg_format = msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_BAD_INFILE);
	  print_log_msg (1, msg_format, file_name.c_str ());
	  util_log_write_errstr (msg_format, file_name.c_str ());
	  error_code = ER_FAILED;
	}
    }

  return file_p;
}

static int
ldr_get_start_line_no (std::string & file_name)
{
  // default start from line no 1
  int line_no = 1;

  if (!file_name.empty ())
    {
      std::string::size_type p = file_name.find (':');
      if (p != std::string::npos)
	{
	  std::string::size_type q = p + 1;
	  for (; q != std::string::npos; ++q)
	    {
	      if (!char_isdigit (file_name[q]))
		{
		  break;
		}
	    }
	  if (file_name[q] == 0)
	    {
	      /* *INDENT-OFF* */
	      try
		{
		  line_no = std::stoi (file_name.substr (p + 1));
		}
	      catch (...)
		{
		  // parse failed, fallback to default value
		}
	      /* *INDENT-ON* */

	      // remove line no from file name
	      file_name.resize (p);
	    }
	}
    }

  return line_no;
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
  FILE *schema_file = NULL;
  T_SCHEMA_FILE_LIST_INFO **schema_file_list = NULL;
  FILE *index_file = NULL;
  FILE *trigger_file = NULL;
  FILE *error_file = NULL;
  FILE *object_file = NULL;

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
  int num_schema_file_list = 0;
  int schema_file_start_line = 1, index_file_start_line = 1, trigger_file_start_line = 1;

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
      if (strcasecmp (args.user_name.c_str (), "DBA") == 0 && args.no_user_specified_name)
	{
	  db_set_client_type (DB_CLIENT_TYPE_ADMIN_LOADDB_COMPAT);
	}
      else
	{
	  db_set_client_type (DB_CLIENT_TYPE_LOADDB_UTILITY);
	}
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
      /*  This can only be reached through loaddb_dba().
       *  However, loaddb_dba() is no longer a valid function.
       */
      assert (false);
      /* Comment out the code below (But, do not delete it).
       * When activating, you should consider a new value to replace DB_CLIENT_TYPE_ADMIN_UTILITY.
       */
#if 0
      /* if we're in the protected DBA mode, just login without authorization */
      AU_DISABLE_PASSWORDS ();
      db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
      (void) db_login ("DBA", NULL);
      error = db_restart (arg->command_name, true, args.volume.c_str ());
#endif
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

  logddl_init (APP_NAME_LOADDB);
  logddl_check_ddl_audit_param ();
  logddl_set_db_name (args.volume.c_str ());
  logddl_set_user_name (args.user_name.c_str ());

  /* disable trigger actions to be fired */
  db_disable_trigger ();

  schema_file_start_line = ldr_get_start_line_no (args.schema_file);
  index_file_start_line = ldr_get_start_line_no (args.index_file);
  trigger_file_start_line = ldr_get_start_line_no (args.trigger_file);

  /* check if schema/index/object files exist */
  schema_file = ldr_check_file (args.schema_file, error);
  if (error != NO_ERROR && schema_file == NULL)
    {
      status = 2;
      goto error_return;
    }

  schema_file_list = ldr_check_file_list (args.schema_file_list, num_schema_file_list, error);
  if (error != NO_ERROR && schema_file_list == NULL)
    {
      status = 2;
      goto error_return;
    }

  index_file = ldr_check_file (args.index_file, error);
  if (error != NO_ERROR && index_file == NULL)
    {
      status = 2;
      goto error_return;
    }
  trigger_file = ldr_check_file (args.trigger_file, error);
  if (error != NO_ERROR && trigger_file == NULL)
    {
      status = 2;
      goto error_return;
    }
  object_file = ldr_check_file (args.object_file, error);
  if (error != NO_ERROR && object_file == NULL)
    {
      status = 2;
      goto error_return;
    }
  else if (object_file != NULL)
    {
      fclose_and_init (object_file);
    }

  if (!args.ignore_class_file.empty ())
    {
      int retval = args.parse_ignore_class_file ();
      if (retval < 0)
	{
	  if (retval != ER_FILE_UNKNOWN_FILE)
	    {
	      // To keep compatibility we need to continue even though the ignore-classes file does not exist.
	      status = 2;
	      goto error_return;
	    }
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
      fclose_and_init (error_file);
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

#if defined(CS_MODE)
  if (args.load_only)
    {
      /* This is the default behavior. It is changed from the old one so we notify the user. */
      print_log_msg (1, "\n--load-only parameter is not supported on Client-Server mode. ");
      print_log_msg (1, "The default behavior of loaddb is loading without checking the file.\n");
    }
#endif

  /* if multiload schema file is specified, do schema loading */
  if (schema_file_list != NULL && num_schema_file_list > 0)
    {
      int start_line;
      for (int i = 0; i < num_schema_file_list; i++)
	{
	  print_log_msg (1, "\nStart %s file loading.\n", schema_file_list[i]->schema_file_name);

	  start_line = 1;

	  if (schema_file_list[i]->schema_fp != NULL)
	    {
	      status = ldr_load_schema_file (schema_file_list[i]->schema_fp, start_line, args);

	      if (status != 0)
		{
		  goto error_return;
		}

	      fclose_and_init (schema_file_list[i]->schema_fp);
	    }
	}
    }

  /* if schema file is specified, do schema loading */
  if (schema_file != NULL)
    {
      print_log_msg (1, "\nStart schema loading.\n");

      status = ldr_load_schema_file (schema_file, schema_file_start_line, args);

      if (status != 0)
	{
	  goto error_return;
	}

      fclose_and_init (schema_file);
    }

  if (!args.object_file.empty ())
    {
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

      if (interrupted || status != 0)
	{
	  // failed
	  db_end_session ();
	  db_shutdown ();
	  goto error_return;
	}
    }

  /* if index file is specified, do index creation */
  if (index_file != NULL)
    {
      print_log_msg (1, "\nStart index loading.\n");
      logddl_set_loaddb_file_type (LOADDB_FILE_TYPE_INDEX);
      logddl_set_load_filename (args.index_file.c_str ());
      if (ldr_exec_query_from_file (args.index_file.c_str (), index_file, &index_file_start_line, &args) != NO_ERROR)
	{
	  print_log_msg (1, "\nError occurred during index loading." "\nAborting current transaction...");
	  msg_format = "Error occurred during index loading." "Aborting current transaction...\n";
	  util_log_write_errstr (msg_format);
	  status = 3;
	  db_end_session ();
	  db_shutdown ();
	  print_log_msg (1, " done.\n\nRestart loaddb with '-%c %s:%d' option\n", LOAD_INDEX_FILE_S,
			 args.index_file.c_str (), index_file_start_line);
	  logddl_write_end ();
	  goto error_return;
	}

      /* update catalog statistics */
      AU_DISABLE (au_save);
      sm_update_catalog_statistics (CT_INDEX_NAME, STATS_WITH_FULLSCAN);
      sm_update_catalog_statistics (CT_INDEXKEY_NAME, STATS_WITH_FULLSCAN);
      AU_ENABLE (au_save);

      print_log_msg (1, "Index loading from %s finished.\n", args.index_file.c_str ());
      db_commit_transaction ();

      fclose_and_init (index_file);

      logddl_set_err_code (error);
      logddl_write_end ();
    }

  if (trigger_file != NULL)
    {
      print_log_msg (1, "\nStart trigger loading.\n");
      logddl_set_loaddb_file_type (LOADDB_FILE_TYPE_TRIGGER);
      logddl_set_load_filename (args.trigger_file.c_str ());
      if (ldr_exec_query_from_file (args.trigger_file.c_str (), trigger_file, &trigger_file_start_line, &args) !=
	  NO_ERROR)
	{
	  print_log_msg (1, "\nError occurred during trigger loading." "\nAborting current transaction...");
	  msg_format = "Error occurred during trigger loading." "Aborting current transaction...\n";
	  util_log_write_errstr (msg_format);
	  status = 3;
	  db_end_session ();
	  db_shutdown ();
	  print_log_msg (1, " done.\n\nRestart loaddb with '--%s %s:%d' option\n", LOAD_TRIGGER_FILE_L,
			 args.trigger_file.c_str (), trigger_file_start_line);
	  logddl_write_end ();
	  goto error_return;
	}

      /* update catalog statistics */
      AU_DISABLE (au_save);
      sm_update_catalog_statistics (CT_TRIGGER_NAME, STATS_WITH_FULLSCAN);
      AU_ENABLE (au_save);

      print_log_msg (1, "Trigger loading from %s finished.\n", args.trigger_file.c_str ());
      db_commit_transaction ();

      fclose_and_init (trigger_file);

      logddl_set_err_code (error);
      logddl_write_end ();
    }

  print_log_msg ((int) args.verbose, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_CLOSING));
  (void) db_end_session ();
  (void) db_shutdown ();

  fclose_and_init (loaddb_log_file);

  logddl_destroy ();

  return status;

error_return:
  if (schema_file != NULL)
    {
      fclose_and_init (schema_file);
    }
  if (index_file != NULL)
    {
      fclose_and_init (index_file);
    }
  if (trigger_file != NULL)
    {
      fclose_and_init (trigger_file);
    }
  if (loaddb_log_file != NULL)
    {
      fclose_and_init (loaddb_log_file);
    }
  if (schema_file_list != NULL)
    {
      ldr_free_and_fclose (schema_file_list, num_schema_file_list);
    }

  logddl_destroy ();
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
  int last_statement_line_no = 0;	// tracks line no of the last successfully executed stmt. -1 for failed ones.
  int check_line_no = true;
  PT_NODE *statement = NULL;

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

      stmt_cnt = db_parse_one_statement (session);
      if (stmt_cnt > 0)
	{
	  stmt_id = db_compile_statement (session);
	  last_statement_line_no = db_get_line_of_statement (session, stmt_id);
	}

      // Any error occured during compilation, report it!
      if (stmt_cnt <= 0 || stmt_id <= 0)
	{
	  DB_SESSION_ERROR *session_error;
	  int line, col;

	  session_error = db_get_errors (session);
	  if (session_error != NULL)
	    {
	      do
		{
		  session_error = db_get_next_error (session_error, &line, &col);
		  if (line >= 0)
		    {
		      // We need -1 here since start_line will offset the output.
		      print_log_msg (1, "In %s line %d,\n", file_name, line + (*start_line) - 1);
		      print_log_msg (1, "ERROR: %s \n", db_error_string (3));
		      assert (er_errid () != NO_ERROR);
		      error = er_errid ();
		      logddl_set_file_line (line);
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
	  logddl_set_file_line (last_statement_line_no);
	  break;
	}
      executed_cnt++;
      error = db_query_end (res);
      if (error < 0)
	{
	  print_log_msg (1, "ERROR: %s\n", db_error_string (3));
	  db_close_session (session);
	  logddl_set_file_line (last_statement_line_no);
	  break;
	}

      if (stmt_type == CUBRID_STMT_COMMIT_WORK
	  || (args->periodic_commit && (executed_cnt % args->periodic_commit == 0)))
	{
	  db_commit_transaction ();
	  print_log_msg (args->verbose_commit, "%8d statements executed. Commit transaction at line %d\n", executed_cnt,
			 last_statement_line_no);
	  *start_line = last_statement_line_no + 1;
	}
      print_log_msg ((int) args->verbose, "Total %8d statements executed.\r", executed_cnt);
      fflush (stdout);
    }

end:
  if (error < 0)
    {
      db_abort_transaction ();
      logddl_set_err_code (error);
      logddl_set_commit_count ((executed_cnt / args->periodic_commit) * args->periodic_commit);
    }
  else
    {
      *start_line = last_statement_line_no + 1;
      print_log_msg (1, "Total %8d statements executed.\n", executed_cnt);
      logddl_set_msg ("Total %8d statements executed.", executed_cnt);
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
  char *trigger_file = utility_get_option_string_value (arg_map, LOAD_TRIGGER_FILE_S, 0);
  char *object_file = utility_get_option_string_value (arg_map, LOAD_DATA_FILE_S, 0);
  char *error_file = utility_get_option_string_value (arg_map, LOAD_ERROR_CONTROL_FILE_S, 0);
  char *table_name = utility_get_option_string_value (arg_map, LOAD_TABLE_NAME_S, 0);
  char *ignore_class_file = utility_get_option_string_value (arg_map, LOAD_IGNORE_CLASS_S, 0);
  char *schema_file_list = utility_get_option_string_value (arg_map, LOAD_SCHEMA_FILE_LIST_S, 0);

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
  args->cs_mode = utility_get_option_bool_value (arg_map, LOAD_CS_MODE_S);

  /* *INDENT-OFF* */
  if (args->periodic_commit == 0)
    {
      // We set the periodic commit to a default value.
      args->periodic_commit = load_args::PERIODIC_COMMIT_DEFAULT_VALUE;
    }
  /* *INDENT-ON* */
  args->no_oid_hint = utility_get_option_bool_value (arg_map, LOAD_NO_OID_S);
  args->schema_file = schema_file ? schema_file : empty;
  args->index_file = index_file ? index_file : empty;
  args->trigger_file = trigger_file ? trigger_file : empty;
  args->object_file = object_file ? object_file : empty;
  args->error_file = error_file ? error_file : empty;
  args->ignore_logging = utility_get_option_bool_value (arg_map, LOAD_IGNORE_LOGGING_S);
  args->compare_storage_order = utility_get_option_bool_value (arg_map, LOAD_COMPARE_STORAGE_ORDER_S);
  args->table_name = table_name ? table_name : empty;
  args->ignore_class_file = ignore_class_file ? ignore_class_file : empty;
  args->no_user_specified_name = utility_get_option_bool_value (arg_map, LOAD_NO_USER_SPECIFIED_NAME_S);
  args->schema_file_list = schema_file_list ? schema_file_list : empty;
}

static void
ldr_server_load (load_args * args, int *exit_status, bool * interrupted)
{
  register_signal_handlers ();

  int error_code = loaddb_init (*args);
  if (error_code != NO_ERROR)
    {
      print_er_msg ();
      *exit_status = 3;
      return;
    }

  error_code = load_object_file (args, exit_status);
  if (error_code != NO_ERROR)
    {
      loaddb_interrupt ();
      print_er_msg ();
      *exit_status = 3;
    }

  /* *INDENT-OFF* */
  cubload::stats last_stat;
  load_status status;
  /* *INDENT-ON* */

  do
    {
      if (load_interrupted)
	{
	  *interrupted = true;
	  *exit_status = 3;
	  break;
	}

      error_code = loaddb_fetch_status (status);
      if (error_code != NO_ERROR)
	{
	  loaddb_interrupt ();
	  print_er_msg ();
	  *exit_status = 3;
	  break;
	}

      print_stats (status.get_load_stats (), *args, exit_status);
      if (!status.get_load_stats ().empty ())
	{
	  last_stat = status.get_load_stats ().back ();
	}

      /* *INDENT-OFF* */
      std::this_thread::sleep_for (std::chrono::milliseconds (100));
      /* *INDENT-ON* */
    }
  while (!(status.is_load_completed () || status.is_load_failed ()) && *exit_status != 3);

  if (load_interrupted)
    {
      print_log_msg (1, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_SIG1));
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_LINE),
	       last_stat.current_line.load ());
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_INTERRUPTED_ABORT));
    }

  if (args->syntax_check)
    {
      if (!last_stat.error_message.empty ())
	{
	  fprintf (stderr, "%s", last_stat.error_message.c_str ());
	}

      print_log_msg (1,
		     msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_OBJECTS_SYNTAX_CHECKED),
		     last_stat.rows_committed, last_stat.rows_failed);
    }
  else
    {
      print_log_msg (1, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_INSERT_AND_FAIL_COUNT),
		     last_stat.rows_committed, last_stat.rows_failed);
    }

  if (!load_interrupted && !status.is_load_failed () && !args->syntax_check && error_code == NO_ERROR
      && !args->disable_statistics)
    {
      // Update class statistics
      if (args->verbose)
	{
	  print_log_msg (1,
			 msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_UPDATING_STATISTICS));
	}
      error_code = loaddb_update_stats (args->verbose);
      if (error_code != NO_ERROR)
	{
	  print_er_msg ();
	  *exit_status = 3;
	}
      else			// NO_ERROR
	{
	  // Fetch the latest stats.
	  error_code = loaddb_fetch_status (status);
	  if (error_code != NO_ERROR)
	    {
	      print_er_msg ();
	      *exit_status = 3;
	    }
	  else			// NO_ERROR
	    {
	      // Print these stats.
	      print_stats (status.get_load_stats (), *args, exit_status);
	    }
	}
    }

  // Destroy the session.
  error_code = loaddb_destroy ();
  if (error_code != NO_ERROR)
    {
      print_er_msg ();
      *exit_status = 3;
    }

  if (load_interrupted)
    {
      print_log_msg (1, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_LAST_COMMITTED_LINE),
		     last_stat.last_committed_line);
    }
}

static void
register_signal_handlers ()
{
  /* *INDENT-OFF* */
  static SIG_HANDLER sig_handler = [] ()
  {
    load_interrupted = true;
    loaddb_interrupt ();
  };
  /* *INDENT-ON* */

  // register handlers for SIGINT and SIGQUIT signals
  util_arm_signal_handlers (sig_handler, sig_handler);
}

static int
load_has_authorization (const std::string & class_name, DB_AUTH au_type)
{
  // au_fetch_class
  DB_OBJECT *usr = db_get_user ();
  if (au_is_dba_group_member (usr))
    {
      // return early, no need to check dba if authorized
      return NO_ERROR;
    }

  int error_code = NO_ERROR;
  DB_OBJECT *class_mop = db_find_class (class_name.c_str ());
  if (class_mop != NULL)
    {
      DB_OBJECT *owner = db_get_owner (class_mop);
      if (owner == usr)
	{
	  // return early, no need to check owner if authorized
	  return NO_ERROR;
	}
    }
  else
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }

  error_code = au_check_authorization (class_mop, au_type);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      // promote from warning to error severity
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
    }
  return error_code;
}

static int
load_object_file (load_args * args, int *exit_status)
{
  if (!args->table_name.empty ())
    {
      int error_code = load_has_authorization (args->table_name, AU_INSERT);
      // user not authorized to insert in class
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  /* *INDENT-OFF* */
  batch_handler b_handler = [&] (const batch &batch) -> int
  {
    int error_code = NO_ERROR;
    bool use_temp_batch = false;
    bool is_batch_accepted = false;
    do
      {
	load_status status;
	error_code = loaddb_load_batch (batch, use_temp_batch, is_batch_accepted, status);
	if (error_code != NO_ERROR)
	  {
	    return error_code;
	  }
	use_temp_batch = true; // don't upload batch again while retrying

	print_stats (status.get_load_stats (), *args, exit_status);
      }
    while (!is_batch_accepted);

    return error_code;
  };

  class_handler c_handler = [] (const batch &batch, bool &is_ignored) -> int
  {
    std::string class_name;
    int error_code = loaddb_install_class (batch, is_ignored, class_name);

    if (error_code != NO_ERROR)
      {
	return error_code;
      }

    if (!is_ignored && !class_name.empty ())
      {
	error_code = load_has_authorization (class_name, AU_INSERT);
      }

    return error_code;
  };
  /* *INDENT-ON* */

  // here we are sure that object_file exists since it was validated by loaddb_internal function
  return split (args->periodic_commit, args->object_file, c_handler, b_handler);
}

static T_SCHEMA_FILE_LIST_INFO **
ldr_check_file_list (std::string & file_name, int &num_files, int &error_code)
{
  FILE *schema_fp;
  T_SCHEMA_FILE_LIST_INFO *schema_object_file = NULL;
  T_SCHEMA_FILE_LIST_INFO **schema_info = NULL;
  T_SCHEMA_FILE_LIST_INFO **new_schema_info = NULL;
  char buffer[PATH_MAX] = { 0, };
  std::string read_file_name = "";
  std::string schema_info_fullpath = "";
  size_t seperator_pos = 0;
#if defined(WINDOWS)
  size_t last_slash = 0;
#endif

  error_code = NO_ERROR;

  schema_fp = ldr_check_file (file_name, error_code);
  if (error_code != NO_ERROR && schema_fp == NULL)
    {
      goto error_return;
    }
  else if (schema_fp == NULL)
    {
      return NULL;
    }

  while (fgets ((char *) buffer, LINE_MAX, schema_fp) != NULL)
    {
      schema_info_fullpath = file_name;
      trim (buffer);

      if (buffer[0] == '\0')
	{
	  continue;
	}

      if (schema_info == NULL)
	{
	  schema_info = (T_SCHEMA_FILE_LIST_INFO **) malloc (sizeof (T_SCHEMA_FILE_LIST_INFO *));
	  if (schema_info == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		      sizeof (T_SCHEMA_FILE_LIST_INFO *));
	      goto error_return;
	    }
	}
      else
	{
	  new_schema_info =
	    (T_SCHEMA_FILE_LIST_INFO **) realloc (schema_info, sizeof (T_SCHEMA_FILE_LIST_INFO *) * (num_files + 1));
	  if (new_schema_info == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		      sizeof (T_SCHEMA_FILE_LIST_INFO *) * (num_files + 1));
	      goto error_return;
	    }

	  schema_info = new_schema_info;
	}

      schema_object_file = (T_SCHEMA_FILE_LIST_INFO *) malloc (sizeof (T_SCHEMA_FILE_LIST_INFO));
      if (schema_object_file == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (T_SCHEMA_FILE_LIST_INFO));
	  goto error_return;
	}

      schema_info[num_files] = schema_object_file;

      num_files++;

      strcpy (schema_object_file->schema_file_name, buffer);

      seperator_pos = schema_info_fullpath.find_last_of (PATH_SEPARATOR);

      if (seperator_pos == std::string::npos)
	{
	  seperator_pos = 0;
	}

#if defined(WINDOWS)
      last_slash = schema_info_fullpath.find_last_of ('/');

      if (last_slash == std::string::npos)
	{
	  last_slash = 0;
	}

      if (last_slash > seperator_pos)
	{
	  seperator_pos = last_slash;
	}
#endif

      if (seperator_pos > 0)
	{
	  schema_info_fullpath = schema_info_fullpath.substr (0, seperator_pos + 1);
	  read_file_name = schema_info_fullpath + buffer;
	}
      else
	{
	  read_file_name = buffer;
	}

      schema_object_file->schema_fp = ldr_check_file (read_file_name, error_code);
      if (error_code != NO_ERROR && schema_object_file->schema_fp == NULL)
	{
	  goto error_return;
	}
    }

  if (schema_info == NULL)
    {
      const char *msg_format =
	msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_EMPTY_SCHEMA_FILE_LIST);
      print_log_msg (1, msg_format, file_name.c_str ());
      util_log_write_errstr (msg_format, file_name.c_str ());
      goto error_return;
    }

  if (schema_fp != NULL)
    {
      fclose_and_init (schema_fp);
    }

  return schema_info;

error_return:

  if (schema_fp != NULL)
    {
      fclose_and_init (schema_fp);
    }

  ldr_free_and_fclose (schema_info, num_files);

  error_code = ER_FAILED;
  num_files = 0;
  return NULL;
}

static void
ldr_free_and_fclose (T_SCHEMA_FILE_LIST_INFO ** file_list, int num)
{
  int i;

  if (file_list != NULL)
    {
      for (i = 0; i < num; i++)
	{
	  if (file_list[i]->schema_fp != NULL)
	    {
	      fclose_and_init (file_list[i]->schema_fp);
	    }

	  if (file_list[i] != NULL)
	    {
	      free_and_init (file_list[i]);
	    }
	}
      free_and_init (file_list);
    }
}

static int
ldr_load_schema_file (FILE * schema_fp, int schema_file_start_line, load_args args)
{
  const char *msg_format = NULL;
  int status = 0;
  int au_save = 0;

  logddl_set_loaddb_file_type (LOADDB_FILE_TYPE_SCHEMA);
  logddl_set_load_filename (args.schema_file.c_str ());
  /*
   * CUBRID 8.2 should be compatible with earlier versions of CUBRID.
   * Therefore, we do not perform user authentication when the loader
   * is executing by DBA group user.
   */
  if (au_is_dba_group_member (Au_user))
    {
      AU_DISABLE (au_save);
    }

  if (ldr_exec_query_from_file (args.schema_file.c_str (), schema_fp, &schema_file_start_line, &args) != NO_ERROR)
    {
      print_log_msg (1, "\nError occurred during schema loading." "\nAborting current transaction...");
      msg_format = "Error occurred during schema loading." "Aborting current transaction...\n";
      util_log_write_errstr (msg_format);
      status = 3;
      db_end_session ();
      db_shutdown ();
      print_log_msg (1, " done.\n\nRestart loaddb with '-%c %s:%d' option\n", LOAD_SCHEMA_FILE_S,
		     args.schema_file.c_str (), schema_file_start_line);
      logddl_write_end ();
      return status;
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
      if (ldr_compare_storage_order (schema_fp) != NO_ERROR)
	{
	  status = 3;
	  db_end_session ();
	  db_shutdown ();
	  print_log_msg (1, "\nAborting current transaction...\n");
	  return status;
	}
    }

  db_commit_transaction ();

  logddl_write_end ();
  return status;
}
