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
 * loaddb.c - Main for database loader
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <assert.h>

#if !defined (WINDOWS)
#include <unistd.h>
#include <sys/param.h>
#endif
#include "porting.h"
#include "db.h"
#include "utility.h"
#include "misc_string.h"
#include "loader.h"
#include "load_object.h"
#include "environment_variable.h"
#include "message_catalog.h"
#include "chartype.h"
#include "schema_manager.h"
#include "transform.h"
#include "server_interface.h"
#include "authenticate.h"
#include "dbi.h"
#include "network_interface_cl.h"
#include "util_func.h"

#if defined (SA_MODE)
extern bool locator_Dont_check_foreign_key;	/* from locator_sr.h */
#endif
#ifdef __cplusplus
extern "C"
{
#endif
  extern void do_loader_parse (FILE * fp);
#ifdef __cplusplus
}
#endif
#define LOADDB_INIT_DEBUG()
#define LOADDB_DEBUG_PRINTF(x)

#define LOAD_INDEX_MIN_SORT_BUFFER_PAGES 8192
#define LOAD_INDEX_MIN_SORT_BUFFER_PAGES_STRING "8192"

static const char *Volume = "";
static const char *Input_file = "";
static const char *Schema_file = "";
static const char *Index_file = "";
static const char *Object_file = "";
static const char *Error_file = "";
static const char *User_name = NULL;
static const char *Password = NULL;
static const char *Ignore_class_file = NULL;
static const char *Table_name = "";

static bool Syntax_check = false;
/* No syntax checking performed */
static bool Load_only = false;
static bool Verbose = false;
static int Verbose_commit = 0;
static int Estimated_size = 5000;
static bool Disable_statistics = false;
#if 0
static bool obsolete_Disable_statistics = false;
#endif
static int Periodic_commit = 0;
/* Don't ignore logging */
static int Ignore_logging = 0;
static int Interrupt_type = LDR_NO_INTERRUPT;
static int schema_file_start_line = 1;
static int index_file_start_line = 1;
static int compare_Storage_order = 0;

#define LOADDB_LOG_FILENAME_SUFFIX "loaddb.log"
static FILE *loaddb_log_file;

bool No_oid_hint = false;

/* The number of objects inserted if an interrupted occurred. */
int Total_objects_loaded = 0;

/* Jump buffer for loader to jump to if we have an interrupt */
static jmp_buf loaddb_jmp_buf;

int interrupt_query = false;
jmp_buf ldr_exec_query_status;

static int ldr_validate_object_file (FILE * outfp, const char *argv0);
static int ldr_check_file_name_and_line_no (void);
static void signal_handler (void);
static void loaddb_report_num_of_commits (int num_committed);
static void loaddb_get_num_of_inserted_objects (int num_objects);
#if defined (WINDOWS)
static int run_proc (char *path, char *cmd_line);
#endif /* WINDOWS */
static int loaddb_internal (UTIL_FUNCTION_ARG * arg, int dba_mode);
static void ldr_exec_query_interrupt_handler (void);
static int ldr_exec_query_from_file (const char *file_name, FILE * file, int *start_line, int commit_period);
static int get_ignore_class_list (const char *filename);
static void free_ignoreclasslist (void);
static int ldr_compare_attribute_with_meta (char *table_name, char *meta, DB_ATTRIBUTE * attribute);
static int ldr_compare_storage_order (FILE * schema_file);

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
 *    return: 0 if successful, 1 if error
 *    outfp(out): error message destination
 */
static int
ldr_validate_object_file (FILE * outfp, const char *argv0)
{
  if (Volume == NULL)
    {
      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_MISSING_DBNAME));
      load_usage (argv0);
      return 1;
    }

  if (Input_file[0] == 0 && Object_file[0] == 0)
    {
      /* if schema/index file are specified, process them only */
      if (Schema_file[0] == 0 && Index_file[0] == 0)
	{
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
	  load_usage (argv0);
	  return 1;
	}
      else
	{
	  return 0;
	}
    }
  else if (Input_file[0] != 0 && Object_file[0] != 0 && strcmp (Input_file, Object_file) != 0)
    {
      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_GENERAL, MSGCAT_GENERAL_ARG_DUPLICATE),
			     "input-file");
      return 1;
    }
  else
    {
      if (Object_file[0] == 0)
	{
	  Object_file = Input_file;
	}
      return 0;
    }
}

/*
 * ldr_check_file_name_and_line_no - parse schema file option
 *    return: void
 */
static int
ldr_check_file_name_and_line_no (void)
{
  char *p, *q;

  if (Schema_file[0] != 0)
    {
      p = (char *) strchr (Schema_file, ':');
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

  if (Index_file[0] != 0)
    {
      p = (char *) strchr (Index_file, ':');
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


/*
 * signal_handler - signal handler registered via util_arm_signal_handlers
 *    return: void
 */
static void
signal_handler (void)
{
  LOADDB_DEBUG_PRINTF (("Signal caught : interrupt flag : %d\n", Interrupt_type));

  /* Flag the loader that that an interrupt has occurred. */
  ldr_interrupt_has_occurred (Interrupt_type);

  print_log_msg (1, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_SIG1));
}

/*
 * loaddb_report_num_of_commits - report number of commits
 *    return: void
 *    num_committed(in): number of commits
 *
 * Note:
 *    registered as a callback function the loader will call this function
 *    to report the number of insertion that have taken place if the '-vc',
 *    verbose commit parameter was specified.
 */
static void
loaddb_report_num_of_commits (int num_committed)
{
  print_log_msg (Verbose_commit,
		 msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_COMMITTED_INSTANCES),
		 num_committed);
}

/*
 * loaddb_get_num_of_inserted_objects - set of object inserted value to
 * Total_objects_loaded global variable
 *    return: void
 *    num_objects(in): number of inserted object to set
 */
static void
loaddb_get_num_of_inserted_objects (int num_objects)
{
  Total_objects_loaded = num_objects;
}

#if defined (WINDOWS)
/*
 * run_proc - run a process with a given command_line
 *    return: 0 for success, non-zero otherwise
 *    path(in): progarm path
 *    cmd_line(in): command line
 */
static int
run_proc (char *path, char *cmd_line)
{
  STARTUPINFO start_info;
  PROCESS_INFORMATION proc_info;
  BOOL status;

  GetStartupInfo (&start_info);
  start_info.wShowWindow = SW_HIDE;
  start_info.dwFlags = STARTF_USESTDHANDLES;
  start_info.hStdInput = GetStdHandle (STD_INPUT_HANDLE);
  start_info.hStdOutput = GetStdHandle (STD_OUTPUT_HANDLE);
  start_info.hStdError = GetStdHandle (STD_ERROR_HANDLE);

  status = CreateProcess (path, cmd_line, NULL, NULL, true, 0, NULL, NULL, &start_info, &proc_info);
  if (status == false)
    {
      LPVOID lpMsgBuf;
      if (FormatMessage
	  (FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
	   GetLastError (), MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR) & lpMsgBuf, 0, NULL))
	{
	  printf ("%s\n", (const char *) lpMsgBuf);
	  LocalFree (lpMsgBuf);
	}

      return -1;
    }

  WaitForSingleObject (proc_info.hProcess, INFINITE);
  CloseHandle (proc_info.hProcess);
  CloseHandle (proc_info.hThread);
  return 0;
}
#endif /* WINDOWS */

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
  /* set to static to avoid copiler warning (clobbered by longjump) */
  static FILE *schema_file = NULL;
  static FILE *index_file = NULL;
  static FILE *object_file = NULL;
  FILE *error_file = NULL;
  int status = 0;
  int errors = 0;
  int objects = 0;
  int defaults = 0;
  int fails = 0;

  int ldr_init_ret = NO_ERROR;
  int lastcommit = 0;
  char *passwd;
  /* set to static to avoid copiler warning (clobbered by longjump) */
  static int interrupted = false;
  int au_save = 0;
  extern bool obt_Enable_autoincrement;
  char log_file_name[PATH_MAX];
  const char *msg_format;

  LOADDB_INIT_DEBUG ();
  obt_Enable_autoincrement = false;

  Volume = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  Input_file = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 1);
  User_name = utility_get_option_string_value (arg_map, LOAD_USER_S, 0);
  Password = utility_get_option_string_value (arg_map, LOAD_PASSWORD_S, 0);
  Syntax_check = utility_get_option_bool_value (arg_map, LOAD_CHECK_ONLY_S);
  Load_only = utility_get_option_bool_value (arg_map, LOAD_LOAD_ONLY_S);
  Estimated_size = utility_get_option_int_value (arg_map, LOAD_ESTIMATED_SIZE_S);
  Verbose = utility_get_option_bool_value (arg_map, LOAD_VERBOSE_S);
  Disable_statistics = utility_get_option_bool_value (arg_map, LOAD_NO_STATISTICS_S);
  Periodic_commit = utility_get_option_int_value (arg_map, LOAD_PERIODIC_COMMIT_S);
  Verbose_commit = Periodic_commit > 0;
  No_oid_hint = utility_get_option_bool_value (arg_map, LOAD_NO_OID_S);
  Schema_file = utility_get_option_string_value (arg_map, LOAD_SCHEMA_FILE_S, 0);
  Index_file = utility_get_option_string_value (arg_map, LOAD_INDEX_FILE_S, 0);
  Object_file = utility_get_option_string_value (arg_map, LOAD_DATA_FILE_S, 0);
  Error_file = utility_get_option_string_value (arg_map, LOAD_ERROR_CONTROL_FILE_S, 0);
  Ignore_logging = utility_get_option_bool_value (arg_map, LOAD_IGNORE_LOGGING_S);
  Table_name = utility_get_option_string_value (arg_map, LOAD_TABLE_NAME_S, 0);

  Ignore_class_file = utility_get_option_string_value (arg_map, LOAD_IGNORE_CLASS_S, 0);
  compare_Storage_order = utility_get_option_bool_value (arg_map, LOAD_COMPARE_STORAGE_ORDER_S);

  Input_file = Input_file ? Input_file : "";
  Schema_file = Schema_file ? Schema_file : "";
  Index_file = Index_file ? Index_file : "";
  Object_file = Object_file ? Object_file : "";
  Error_file = Error_file ? Error_file : "";
  Table_name = Table_name ? Table_name : "";

  if (ldr_validate_object_file (stderr, arg->argv0))
    {
      status = 1;
      goto error_return;
    }

  /* error message log file */
  sprintf (log_file_name, "%s_%s.err", Volume, arg->command_name);
  er_init (log_file_name, ER_NEVER_EXIT);

  if (Index_file[0] != '\0' && prm_get_integer_value (PRM_ID_SR_NBUFFERS) < LOAD_INDEX_MIN_SORT_BUFFER_PAGES)
    {
      sysprm_set_force (prm_get_name (PRM_ID_SR_NBUFFERS), LOAD_INDEX_MIN_SORT_BUFFER_PAGES_STRING);
    }

  sysprm_set_force (prm_get_name (PRM_ID_JAVA_STORED_PROCEDURE), "no");

  /* open loaddb log file */
  sprintf (log_file_name, "%s_%s", Volume, LOADDB_LOG_FILENAME_SUFFIX);
  loaddb_log_file = fopen (log_file_name, "w+");
  if (loaddb_log_file == NULL)
    {
      PRINT_AND_LOG_ERR_MSG ("Cannot open log file %s\n", log_file_name);
      status = 2;
      goto error_return;
    }

  /* login */
  if (User_name != NULL || !dba_mode)
    {
      (void) db_login (User_name, Password);
      error = db_restart (arg->command_name, true, Volume);
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
	      (void) db_login (User_name, passwd);
	      error = db_restart (arg->command_name, true, Volume);
	    }
	}
    }
  else
    {
      /* if we're in the protected DBA mode, just login without authorization */
      AU_DISABLE_PASSWORDS ();
      db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
      (void) db_login ("DBA", NULL);
      error = db_restart (arg->command_name, true, Volume);
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
	  PRINT_AND_LOG_ERR_MSG ("Cannot restart database %s\n", Volume);
	}
      status = 3;
      goto error_return;
    }

  /* disable trigger actions to be fired */
  db_disable_trigger ();

  /* check if schema/index/object files exist */
  ldr_check_file_name_and_line_no ();

  if (Schema_file[0] != 0)
    {
      schema_file = fopen (Schema_file, "r");
      if (schema_file == NULL)
	{
	  msg_format = msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_BAD_INFILE);
	  print_log_msg (1, msg_format, Schema_file);
	  util_log_write_errstr (msg_format, Schema_file);
	  status = 2;
	  goto error_return;
	}
    }
  if (Index_file[0] != 0)
    {
      index_file = fopen (Index_file, "r");
      if (index_file == NULL)
	{
	  msg_format = msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_BAD_INFILE);
	  print_log_msg (1, msg_format, Index_file);
	  util_log_write_errstr (msg_format, Index_file);
	  status = 2;
	  goto error_return;
	}
    }
  if (Object_file[0] != 0)
    {
      object_file = fopen_ex (Object_file, "rb");	/* keep out ^Z */

      if (object_file == NULL)
	{
	  msg_format = msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_BAD_INFILE);
	  print_log_msg (1, msg_format, Object_file);
	  util_log_write_errstr (msg_format, Object_file);
	  status = 2;
	  goto error_return;
	}
    }

  if (Ignore_class_file)
    {
      int retval;
      retval = get_ignore_class_list (Ignore_class_file);

      if (retval < 0)
	{
	  status = 2;
	  goto error_return;
	}
    }

  /* Disallow syntax only and load only options together */
  if (Load_only && Syntax_check)
    {
      msg_format = msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_INCOMPATIBLE_ARGS);
      print_log_msg (1, msg_format, "--" LOAD_LOAD_ONLY_L, "--" LOAD_CHECK_ONLY_L);
      util_log_write_errstr (msg_format, "--" LOAD_LOAD_ONLY_L, "--" LOAD_CHECK_ONLY_L);
      status = 1;		/* parsing error */
      goto error_return;
    }

  if (Error_file[0] != 0)
    {
      if (Syntax_check)
	{
	  msg_format = msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_INCOMPATIBLE_ARGS);
	  print_log_msg (1, msg_format, "--" LOAD_ERROR_CONTROL_FILE_L, "--" LOAD_CHECK_ONLY_L);
	  util_log_write_errstr (msg_format, "--" LOAD_ERROR_CONTROL_FILE_L, "--" LOAD_CHECK_ONLY_L);
	  status = 1;		/* parsing error */
	  goto error_return;
	}
      error_file = fopen_ex (Error_file, "rt");
      if (error_file == NULL)
	{
	  msg_format = msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_BAD_INFILE);
	  print_log_msg (1, msg_format, Error_file);
	  util_log_write_errstr (msg_format, Error_file);
	  status = 2;
	  goto error_return;
	}
      er_filter_fileset (error_file);
      fclose (error_file);
    }

  /* check if no log option can be applied */
  if (error || (Ignore_logging != 0 && locator_log_force_nologging () != NO_ERROR))
    {
      /* couldn't log in */
      print_log_msg (1, "%s\n", db_error_string (3));
      util_log_write_errstr ("%s\n", db_error_string (3));
      status = 3;
      db_shutdown ();
      goto error_return;
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

      if (ldr_exec_query_from_file (Schema_file, schema_file, &schema_file_start_line, Periodic_commit) != 0)
	{
	  print_log_msg (1, "\nError occurred during schema loading." "\nAborting current transaction...");
	  msg_format = "Error occurred during schema loading." "Aborting current transaction...\n";
	  util_log_write_errstr (msg_format);
	  status = 3;
	  db_shutdown ();
	  print_log_msg (1, " done.\n\nRestart loaddb with '-%c %s:%d' option\n", LOAD_SCHEMA_FILE_S, Schema_file,
			 schema_file_start_line);
	  goto error_return;
	}

      if (au_is_dba_group_member (Au_user))
	{
	  AU_ENABLE (au_save);
	}

      print_log_msg (1, "Schema loading from %s finished.\n", Schema_file);

      /* update catalog statistics */
      AU_DISABLE (au_save);
      sm_update_all_catalog_statistics (STATS_WITH_FULLSCAN);
      AU_ENABLE (au_save);

      print_log_msg (1, "Statistics for Catalog classes have been updated.\n\n");

      if (compare_Storage_order)
	{
	  if (ldr_compare_storage_order (schema_file) != NO_ERROR)
	    {
	      status = 3;
	      db_shutdown ();
	      print_log_msg (1, "\nAborting current transaction...\n");
	      goto error_return;
	    }
	}

      db_commit_transaction ();
      fclose (schema_file);
      schema_file = NULL;
    }


  /* if index file is specified, do index creation */

  if (object_file != NULL)
    {
#if defined (SA_MODE)
      locator_Dont_check_foreign_key = true;
#endif
      print_log_msg (1, "\nStart object loading.\n");
      ldr_init (Verbose);

      /* set the flag to indicate what type of interrupts to raise If logging has been disabled set commit flag. If
       * logging is enabled set abort flag. */

      if (Ignore_logging)
	{
	  Interrupt_type = LDR_STOP_AND_COMMIT_INTERRUPT;
	}
      else
	{
	  Interrupt_type = LDR_STOP_AND_ABORT_INTERRUPT;
	}

      if (Periodic_commit)
	{
	  /* register the post commit function */
	  ldr_register_post_commit_handler (&loaddb_report_num_of_commits, NULL);
	}

      /* Check if we need to perform syntax checking. */
      if (!Load_only)
	{
	  print_log_msg ((int) Verbose,
			 msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_CHECKING));
	  if (Table_name[0] != '\0')
	    {
	      ldr_init_ret = ldr_init_class_spec (Table_name);
	    }
	  do_loader_parse (object_file);
	  ldr_stats (&errors, &objects, &defaults, &lastcommit, &fails);
	}
      else
	{
	  errors = 0;
	}

      if (errors)
	{
	  print_log_msg (1, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_ERROR_COUNT),
			 errors);
	}
      else if (ldr_init_ret == NO_ERROR && !Syntax_check)
	{
	  /* now do it for real if there were no errors and we aren't doing a simple syntax check */
	  ldr_start (Periodic_commit);
	  fclose (object_file);
	  object_file = fopen_ex (Object_file, "rb");	/* keep out ^Z */
	  if (object_file != NULL)
	    {
	      print_log_msg ((int) Verbose,
			     msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_INSERTING));

	      /* make sure signals are caught */
	      util_arm_signal_handlers (signal_handler, signal_handler);

	      /* register function to call and jmp environment to longjmp to after aborting or committing. */
	      ldr_register_post_interrupt_handler (&loaddb_get_num_of_inserted_objects, &loaddb_jmp_buf);

	      if (setjmp (loaddb_jmp_buf) != 0)
		{

		  /* We have had an interrupt, the transaction should have been already been aborted or committed by
		   * the loader. If Total_objects_loaded is -1 an error occurred during rollback or commit. */
		  if (Total_objects_loaded != -1)
		    {
		      print_log_msg (1,
				     msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB,
						     LOADDB_MSG_OBJECT_COUNT), Total_objects_loaded);
		    }
		  ldr_stats (&errors, &objects, &defaults, &lastcommit, &fails);
		  if (lastcommit > 0)
		    {
		      print_log_msg (1,
				     msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB,
						     LOADDB_MSG_LAST_COMMITTED_LINE), lastcommit);
		    }
		  interrupted = true;
		  status = 3;
		}
	      else
		{
		  if (Table_name[0] != '\0')
		    {
		      ldr_init_class_spec (Table_name);
		    }
		  do_loader_parse (object_file);
		  ldr_stats (&errors, &objects, &defaults, &lastcommit, &fails);
		  if (errors)
		    {
		      if (lastcommit > 0)
			{
			  print_log_msg (1,
					 msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB,
							 LOADDB_MSG_LAST_COMMITTED_LINE), lastcommit);
			}

		      util_log_write_errstr ("%s\n", db_error_string (3));
		      /* 
		       * don't allow the transaction to be committed at
		       * this point, note that if we ever move to a scheme
		       * where we write directly to the heap without the
		       * transaction context, we will have to unwind the
		       * changes made if errors are detected !
		       */
		      db_abort_transaction ();
		    }
		  else
		    {
		      if (objects || fails)
			{
			  print_log_msg (1,
					 msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB,
							 LOADDB_MSG_INSERT_AND_FAIL_COUNT), objects, fails);
			}

		      if (defaults)
			{
			  print_log_msg (1,
					 msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB,
							 LOADDB_MSG_DEFAULT_COUNT), defaults);
			}
		      print_log_msg ((int) Verbose,
				     msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB,
						     LOADDB_MSG_COMMITTING));

		      /* commit the transaction and then update statistics */
		      if (!db_commit_transaction ())
			{
			  if (!Disable_statistics)
			    {
			      if (Verbose)
				{
				  print_log_msg (1,
						 msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB,
								 LOADDB_MSG_UPDATING_STATISTICS));
				}
			      if (!ldr_update_statistics ())
				{
				  /* 
				   * would it be faster to update statistics
				   * before the first commit and just have a
				   * single commit ?
				   */
				  print_log_msg ((int) Verbose,
						 msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB,
								 LOADDB_MSG_COMMITTING));
				  (void) db_commit_transaction ();
				}
			    }
			}
		    }
		}
	    }
	}

      ldr_final ();
      if (object_file != NULL)
	{
	  fclose (object_file);
	  object_file = NULL;
	}
    }

  /* create index */
  if (!interrupted && index_file != NULL)
    {
      print_log_msg (1, "\nStart index loading.\n");
      if (ldr_exec_query_from_file (Index_file, index_file, &index_file_start_line, Periodic_commit) != 0)
	{
	  print_log_msg (1, "\nError occurred during index loading." "\nAborting current transaction...");
	  msg_format = "Error occurred during index loading." "Aborting current transaction...\n";
	  util_log_write_errstr (msg_format);
	  status = 3;
	  db_shutdown ();
	  print_log_msg (1, " done.\n\nRestart loaddb with '-%c %s:%d' option\n", LOAD_INDEX_FILE_S, Index_file,
			 index_file_start_line);
	  goto error_return;
	}

      /* update catalog statistics */
      AU_DISABLE (au_save);
      sm_update_catalog_statistics (CT_INDEX_NAME, STATS_WITH_FULLSCAN);
      sm_update_catalog_statistics (CT_INDEXKEY_NAME, STATS_WITH_FULLSCAN);
      AU_ENABLE (au_save);

      print_log_msg (1, "Index loading from %s finished.\n", Index_file);
      db_commit_transaction ();
    }

  if (index_file != NULL)
    {
      fclose (index_file);
      index_file = NULL;
    }

  print_log_msg ((int) Verbose, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_CLOSING));
  (void) db_shutdown ();

  free_ignoreclasslist ();

  fclose (loaddb_log_file);

  return status;

error_return:
  if (schema_file != NULL)
    {
      fclose (schema_file);
    }
  if (object_file != NULL)
    {
      fclose (object_file);
    }
  if (index_file != NULL)
    {
      fclose (index_file);
    }

  if (loaddb_log_file != NULL)
    {
      fclose (loaddb_log_file);
    }

  free_ignoreclasslist ();

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
ldr_exec_query_from_file (const char *file_name, FILE * input_stream, int *start_line, int commit_period)
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

  while (1)
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

      if (stmt_type == CUBRID_STMT_COMMIT_WORK || (commit_period && (executed_cnt % commit_period == 0)))
	{
	  db_commit_transaction ();
	  print_log_msg (Verbose_commit, "%8d statements executed. Commit transaction at line %d\n", executed_cnt,
			 parser_end_line_no);
	  *start_line = parser_end_line_no + 1;
	}
      print_log_msg ((int) Verbose, "Total %8d statements executed.\r", executed_cnt);
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

static int
get_ignore_class_list (const char *inputfile_name)
{
  int inc_unit = 128;
  int list_size;
  FILE *input_file = NULL;
  char buffer[DB_MAX_IDENTIFIER_LENGTH], buffer_scan_format[16];
  char class_name[DB_MAX_IDENTIFIER_LENGTH];

  if (ignore_class_list != NULL)
    {
      free_ignoreclasslist ();
    }

  if (inputfile_name == NULL)
    {
      return 0;
    }

  input_file = fopen (inputfile_name, "r");
  if (input_file == NULL)
    {
      perror (inputfile_name);
      util_log_write_errid (MSGCAT_UTIL_GENERIC_FILEOPEN_ERROR, inputfile_name);
      return 1;
    }

  ignore_class_list = (char **) malloc (sizeof (char *) * inc_unit);
  if (ignore_class_list == NULL)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_NO_MEM);
      goto error;
    }

  memset (ignore_class_list, '\0', inc_unit);

  list_size = inc_unit;
  ignore_class_num = 0;

  snprintf (buffer_scan_format, sizeof (buffer_scan_format), "%%%ds\n", (int) (sizeof (buffer) - 1));

  while (fgets ((char *) buffer, DB_MAX_IDENTIFIER_LENGTH, input_file) != NULL)
    {
      if ((strchr (buffer, '\n') - buffer) >= 1)
	{
	  if (ignore_class_num >= list_size)
	    {
	      ignore_class_list = (char **) realloc (ignore_class_list, sizeof (char *) * (list_size + inc_unit));
	      if (ignore_class_list == NULL)
		{
		  util_log_write_errid (MSGCAT_UTIL_GENERIC_NO_MEM);
		  goto error;
		}

	      memset (ignore_class_list + list_size, '\0', inc_unit);
	      list_size = list_size + inc_unit;
	    }

	  sscanf ((char *) buffer, buffer_scan_format, (char *) class_name);
	  ignore_class_list[ignore_class_num] = strdup (class_name);
	  if (ignore_class_list[ignore_class_num] == NULL)
	    {
	      util_log_write_errid (MSGCAT_UTIL_GENERIC_NO_MEM);
	      goto error;
	    }

	  ignore_class_num++;
	}
    }

  fclose (input_file);

  return 0;

error:
  free_ignoreclasslist ();
  fclose (input_file);

  return -1;
}

static void
free_ignoreclasslist (void)
{
  int i = 0;

  if (ignore_class_list != NULL)
    {
      for (i = 0; i < ignore_class_num; i++)
	{
	  if (*(ignore_class_list + i) != NULL)
	    {
	      free (*(ignore_class_list + i));
	    }
	}
      free (ignore_class_list);
      ignore_class_list = NULL;
    }
  ignore_class_num = 0;
}
