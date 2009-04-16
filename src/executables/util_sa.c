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
 * util_sa.c - Implementation for utilities that operate in standalone mode.
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "porting.h"
#include "chartype.h"
#include "error_manager.h"
#include "message_catalog.h"
#include "databases_file.h"
#include "environment_variable.h"
#include "system_parameter.h"
#include "boot_sr.h"
#include "db.h"
#include "authenticate.h"
#include "schema_manager.h"
#include "glo_class.h"
#include "heap_file.h"
#include "btree.h"
#include "extendible_hash.h"
#include "locator_sr.h"
#include "log_impl.h"
#include "xserver_interface.h"
#include "utility.h"
#include "transform.h"
#include "csql.h"
#include "locator_cl.h"
#include "network_interface_cl.h"

#define SR_CLASS_NAME           CT_SERIAL_NAME

#define SR_ATT_NAME             "name"
#define SR_ATT_OWNER            "owner"
#define SR_ATT_CURRENT_VAL      "current_val"
#define SR_ATT_INCREMENT_VAL    "increment_val"
#define SR_ATT_MAX_VAL          "max_val"
#define SR_ATT_MIN_VAL          "min_val"
#define SR_ATT_CYCLIC           "cyclic"
#define SR_ATT_STARTED          "started"
#define SR_ATT_CLASS_NAME       "class_name"
#define SR_ATT_ATT_NAME         "att_name"

#define MAX_LINE_LEN            4096

#define COMMENT_CHAR            '-'
#define COMMAND_USER            "user"
#define COMMAND_GROUP           "group"
#define COMMAND_MEMBERS         "members"
#define COMMAND_GROUPS          "groups"

#if defined(WINDOWS)
typedef int pid_t;
#define strtok_r	strtok_s
#endif

#define BO_DB_FULLNAME          (bo_Dbfullname)

static char bo_Dbfullname[PATH_MAX];

extern bool catcls_Enable;
extern char **environ;
extern int log_default_input_for_archive_log_location;

extern int catcls_compile_catalog_classes (THREAD_ENTRY * thread_p);

static int sr_define_serial_class (void);
static int parse_user_define_line (char *line, FILE * output_file);
static int parse_user_define_file (FILE * user_define_file,
				   FILE * output_file);
static int parse_up_to_date (char *up_to_date, struct tm *time_date);

#define LOCAL_CHECK_ERROR(EI) { \
  if ((EI) != NO_ERROR) { \
    er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, (EI), 0); \
    return (EI); \
  } \
}


/*
 * util_admin_usage - display an usage of this utility
 *
 * return:
 *
 * NOTE:
 */
void
util_admin_usage (const char *argv0)
{
  const char *exec_name;

  exec_name = basename ((char *) argv0);
  fprintf (stderr,
	   utility_get_generic_message (MSGCAT_UTIL_GENERIC_ADMIN_USAGE),
	   VERSION, exec_name, exec_name, exec_name);
}

/*
 * util_admin_version - display a version of this utility
 *
 * return:
 *
 * NOTE:
 */
void
util_admin_version (const char *argv0)
{
  const char *exec_name;

  exec_name = basename ((char *) argv0);
  fprintf (stderr, utility_get_generic_message (MSGCAT_UTIL_GENERIC_VERSION),
	   exec_name, VERSION);
}

/*
 * sr_define_serial_class()
 *   return: NO_ERROR if success
 */
static int
sr_define_serial_class ()
{
  DB_OBJECT *serial_class = NULL;
  DB_OBJECT *user;
  DB_VALUE value, num_value;
  char domain_str[32];
  const char *attr_list[] = { SR_ATT_NAME, NULL };
  DB_DATA_STATUS data_stat;
  int error_status = NO_ERROR;

  er_clear ();

  /* create db_serial class */
  if ((serial_class = db_create_class (SR_CLASS_NAME)) == NULL)
    LOCAL_CHECK_ERROR (er_errid ());

  /* add attributes */
  error_status = db_add_attribute (serial_class, SR_ATT_NAME,
				   db_get_type_name (DB_TYPE_STRING), NULL);
  LOCAL_CHECK_ERROR (error_status);

  error_status = db_add_attribute (serial_class, SR_ATT_OWNER,
				   AU_USER_CLASS_NAME, NULL);
  LOCAL_CHECK_ERROR (error_status);

  /* define serial's value domain */
  sprintf (domain_str, "numeric(%d, 0)", DB_MAX_NUMERIC_PRECISION);

  DB_MAKE_INTEGER (&value, 1);
  db_value_domain_init (&num_value, DB_TYPE_NUMERIC, DB_MAX_NUMERIC_PRECISION,
			0);
  (void) numeric_db_value_coerce_to_num (&value, &num_value, &data_stat);
  error_status = db_add_attribute (serial_class, SR_ATT_CURRENT_VAL,
				   domain_str, &num_value);
  LOCAL_CHECK_ERROR (error_status);

  error_status = db_add_attribute (serial_class, SR_ATT_INCREMENT_VAL,
				   domain_str, &num_value);
  LOCAL_CHECK_ERROR (error_status);

  error_status = db_add_attribute (serial_class, SR_ATT_MAX_VAL,
				   domain_str, NULL);
  LOCAL_CHECK_ERROR (error_status);

  error_status = db_add_attribute (serial_class, SR_ATT_MIN_VAL,
				   domain_str, NULL);
  LOCAL_CHECK_ERROR (error_status);

  DB_MAKE_INTEGER (&value, 0);
  error_status = db_add_attribute (serial_class, SR_ATT_CYCLIC,
				   "integer", &value);
  LOCAL_CHECK_ERROR (error_status);

  DB_MAKE_INTEGER (&value, 0);
  error_status = db_add_attribute (serial_class, SR_ATT_STARTED,
				   "integer", &value);
  LOCAL_CHECK_ERROR (error_status);

  error_status = db_add_attribute (serial_class, SR_ATT_CLASS_NAME,
				   db_get_type_name (DB_TYPE_STRING), NULL);
  LOCAL_CHECK_ERROR (error_status);

  error_status = db_add_attribute (serial_class, SR_ATT_ATT_NAME,
				   db_get_type_name (DB_TYPE_STRING), NULL);
  LOCAL_CHECK_ERROR (error_status);

  error_status = db_add_constraint (serial_class, DB_CONSTRAINT_PRIMARY_KEY,
				    NULL, (const char **) attr_list, 0);
  LOCAL_CHECK_ERROR (error_status);

  if (locator_has_heap (serial_class) == NULL)
    {
      return ER_FAILED;
    }

  /* find public user to grant */
  if ((user = db_find_user (AU_PUBLIC_USER_NAME)) == NULL)
    LOCAL_CHECK_ERROR (ERR_MM_FINDING_PUBLIC);

  db_grant (user, serial_class,
	    (DB_AUTH_SELECT | DB_AUTH_INSERT | DB_AUTH_UPDATE
	     | DB_AUTH_DELETE | DB_AUTH_ALTER), FALSE);

  return NO_ERROR;
}

/*
 * parse_user_define_line() - parse user information
 *   return: 0 if success
 *   line_buffer(in)
 *   output_file(in)
 */
static int
parse_user_define_line (char *line_buffer, FILE * output_file)
{
  const char *delim = " \t\n";
  char *save_ptr, *token;
  bool group_type;
  int exists;
  MOP database_object, user;

  /* A first word must be the "user" keyword. */
  token = strtok_r (line_buffer, delim, &save_ptr);
  if (token != NULL && strcasecmp (token, COMMAND_USER) == 0)
    {
      /* A second word must be an user's name. */
      token = strtok_r (NULL, delim, &save_ptr);
      if (token == NULL || (user = au_add_user (token, &exists)) == NULL)
	{
	  fprintf (output_file, msgcat_message (MSGCAT_CATALOG_UTILS,
						MSGCAT_UTIL_SET_CREATEDB,
						CREATEDB_MSG_MISSING_USER),
		   token);
	  return ER_GENERIC_ERROR;
	}

      token = strtok_r (NULL, delim, &save_ptr);

      if (token == NULL)
	{
	  /* GROUPS or MEMBERS keyword could be omitted */
	  return NO_ERROR;
	}
      else if (strcasecmp (token, COMMAND_GROUPS) == 0)
	{
	  group_type = true;
	}
      else if (strcasecmp (token, COMMAND_MEMBERS) == 0)
	{
	  group_type = false;
	}
      else
	{
	  fprintf (output_file, msgcat_message (MSGCAT_CATALOG_UTILS,
						MSGCAT_UTIL_SET_CREATEDB,
						CREATEDB_MSG_UNKNOWN_CMD),
		   token);
	  return ER_GENERIC_ERROR;
	}

      /* loop for the list of user's name */
      while ((token = strtok_r (NULL, delim, &save_ptr)) != NULL)
	{
	  database_object = au_find_user (token);
	  if (database_object == NULL)
	    {
	      /* could not found the user */
	      fprintf (output_file, msgcat_message (MSGCAT_CATALOG_UTILS,
						    MSGCAT_UTIL_SET_CREATEDB,
						    CREATEDB_MSG_MISSING_USER),
		       token);
	      return ER_GENERIC_ERROR;
	    }
	  else
	    {
	      if (group_type)
		{
		  au_add_member (database_object, user);
		}
	      else
		{
		  au_add_member (user, database_object);
		}
	    }
	}
    }
  else
    {
      if (token != NULL)
	{
	  fprintf (output_file, msgcat_message (MSGCAT_CATALOG_UTILS,
						MSGCAT_UTIL_SET_CREATEDB,
						CREATEDB_MSG_UNKNOWN_CMD),
		   token);
	  return ER_GENERIC_ERROR;
	}
    }
  return NO_ERROR;
}

/*
 * parse_user_define_file() - read user information file and parse it
 *   return: 0 if success
 *   user_define_file(in)
 *   output_file(in)
 *
 * Note: user definition file format :
 *            USER  name GROUPS group1 group2 . . .
 *            USER  name MEMBERS user1 user2 . . .
 */
static int
parse_user_define_file (FILE * user_define_file, FILE * output_file)
{
  int status = NO_ERROR;
  int line_number = 1;
  char line_buffer[MAX_LINE_LEN];

  while (fgets (line_buffer, MAX_LINE_LEN, user_define_file) != NULL)
    {
      if (strlen (line_buffer) == 0 || line_buffer[0] == COMMENT_CHAR)
	{
	  line_number++;
	  continue;
	}
      status = parse_user_define_line (line_buffer, output_file);
      if (status != NO_ERROR)
	{
	  fprintf (stderr, "parse error at line: %d\n", line_number);
	  return status;
	}
      line_number++;
    }
  return status;
}

/*
 * createdb() - createdb main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
createdb (UTIL_FUNCTION_ARG * arg)
{
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  int status;
  FILE *output_file = NULL;
  FILE *user_define_file = NULL;

  const char *output_file_name;
  const char *program_name;
  const char *database_name;
  const char *volume_path;
  const char *log_path;
  const char *host_name;
  bool overwrite;
  bool verbose;
  const char *comment;
  int volume_page_count;
  const char *init_file_name;
  const char *volume_spec_file_name;
  const char *user_define_file_name;
  int page_size;
  int log_page_count;

  output_file_name =
    utility_get_option_string_value (arg_map, CREATE_OUTPUT_FILE_S, 0);
  program_name = arg->command_name;
  database_name =
    utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  volume_path =
    utility_get_option_string_value (arg_map, CREATE_FILE_PATH_S, 0);
  log_path = utility_get_option_string_value (arg_map, CREATE_LOG_PATH_S, 0);
  host_name =
    utility_get_option_string_value (arg_map, CREATE_SERVER_NAME_S, 0);
  overwrite = utility_get_option_bool_value (arg_map, CREATE_REPLACE_S);
  verbose = utility_get_option_bool_value (arg_map, CREATE_VERBOSE_S);
  comment = utility_get_option_string_value (arg_map, CREATE_COMMENT_S, 0);
  init_file_name =
    utility_get_option_string_value (arg_map,
				     CREATE_CSQL_INITIALIZATION_FILE_S, 0);
  volume_page_count = utility_get_option_int_value (arg_map, CREATE_PAGES_S);
  volume_spec_file_name =
    utility_get_option_string_value (arg_map, CREATE_MORE_VOLUME_FILE_S, 0);
  user_define_file_name =
    utility_get_option_string_value (arg_map, CREATE_USER_DEFINITION_FILE_S,
				     0);
  page_size = utility_get_option_int_value (arg_map, CREATE_PAGE_SIZE_S);
  log_page_count =
    utility_get_option_int_value (arg_map, CREATE_LOG_PAGE_COUNT_S);

  sysprm_set_to_default ("data_buffer_pages");
  sysprm_set_force ("max_plan_cache_entries", "-1");

  if (database_name == 0 || database_name[0] == 0 ||
      utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_create_usage;
    }

  if (check_database_name (database_name))
    {
      goto error_exit;
    }

  if (output_file_name == 0 || output_file_name[0] == 0)
    {
      output_file = stdout;
    }
  else
    {
      output_file = fopen (output_file_name, "w");
    }

  if (output_file == NULL)
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_CREATEDB,
				       CREATEDB_MSG_BAD_OUTPUT),
	       output_file_name);
      goto error_exit;
    }

  if (volume_page_count < 100)
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_CREATEDB,
				       CREATEDB_MSG_FAILURE));
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_CREATEDB,
				       CREATEDB_MSG_FEW_PAGES));
      fclose (output_file);
      goto error_exit;
    }

  if (user_define_file_name != 0)
    {
      user_define_file = fopen (user_define_file_name, "r");
      if (user_define_file == NULL)
	{
	  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
					   MSGCAT_UTIL_SET_CREATEDB,
					   CREATEDB_MSG_BAD_USERFILE),
		   user_define_file_name);
	  goto error_exit;
	}
    }

  fprintf (output_file, msgcat_message (MSGCAT_CATALOG_UTILS,
					MSGCAT_UTIL_SET_CREATEDB,
					CREATEDB_MSG_CREATING),
	   volume_page_count);

  /* error message log file */
  sprintf (er_msg_file, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);

  db_login ("dba", NULL);
  status = db_init (program_name, true, database_name, volume_path,
		    NULL, log_path, host_name, overwrite, comment,
		    volume_page_count, volume_spec_file_name, page_size,
		    log_page_count);

  if (status != NO_ERROR)
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_CREATEDB,
				       CREATEDB_MSG_FAILURE));
      fprintf (stderr, "%s\n", db_error_string (3));
      goto error_exit;
    }

  if (sr_define_serial_class () != NO_ERROR)
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_CREATEDB,
				       CREATEDB_MSG_FAILURE));
      fprintf (stderr, "%s\n", db_error_string (3));
      db_shutdown ();
      goto error_exit;
    }
  esm_define_esm_classes ();
  sm_mark_system_classes ();

  lang_set_national_charset (NULL);
  if (verbose)
    {
      au_dump_to_file (output_file);
    }
  if (!tf_Metaclass_class.n_variable)
    {
      tf_compile_meta_classes ();
    }
  if ((catcls_Enable != true)
      && (catcls_compile_catalog_classes (NULL) != NO_ERROR))
    {
      db_shutdown ();
      goto error_exit;
    }
  if (catcls_Enable == true)
    {
      if (sm_force_write_all_classes () != NO_ERROR
	  || au_force_write_new_auth () != NO_ERROR)
	{
	  db_shutdown ();
	  goto error_exit;
	}
    }
  if (sm_update_all_catalog_statistics () != NO_ERROR)
    {
      db_shutdown ();
      goto error_exit;
    }

  db_commit_transaction ();

  if (user_define_file != NULL)
    {
      if (parse_user_define_file (user_define_file, output_file) != NO_ERROR)
	{
	  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
					   MSGCAT_UTIL_SET_CREATEDB,
					   CREATEDB_MSG_BAD_USERFILE),
		   user_define_file_name);
	  db_shutdown ();
	  goto error_exit;
	}
      fclose (user_define_file);
    }
  db_commit_transaction ();
  db_shutdown ();

  if (output_file != stdout)
    {
      fclose (output_file);
    }

  if (init_file_name != NULL)
    {
      CSQL_ARGUMENT csql_arg;
      memset (&csql_arg, 0, sizeof (CSQL_ARGUMENT));
      csql_arg.auto_commit = true;
      csql_arg.db_name = database_name;
      csql_arg.in_file_name = init_file_name;
      csql (arg->command_name, &csql_arg);
    }

  return EXIT_SUCCESS;

print_create_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_CREATEDB,
				   CREATEDB_MSG_USAGE),
	   basename (arg->argv0));
error_exit:
  if (output_file != stdout && output_file != NULL)
    {
      fclose (output_file);
    }
  if (user_define_file != NULL)
    {
      fclose (user_define_file);
    }
  return EXIT_FAILURE;
}

/*
 * deletedb() - deletedb main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
deletedb (UTIL_FUNCTION_ARG * arg)
{
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  FILE *output_file = NULL;
  const char *output_file_name;
  const char *database_name;
  bool force_delete;

  output_file_name =
    utility_get_option_string_value (arg_map, DELETE_OUTPUT_FILE_S, 0);
  database_name =
    utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  force_delete =
    utility_get_option_bool_value (arg_map, DELETE_DELETE_BACKUP_S);

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_delete_usage;
    }

  sysprm_set_to_default ("data_buffer_pages");

  if (output_file_name == NULL)
    {
      output_file = stdout;
    }
  else
    {
      output_file = fopen (output_file_name, "w");
    }

  if (output_file == NULL)
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_GENERIC,
				       MSGCAT_UTIL_GENERIC_BAD_OUTPUT_FILE),
	       output_file_name);
      goto error_exit;
    }

  if (check_database_name (database_name))
    {
      goto error_exit;
    }

  /* error message log file */
  sprintf (er_msg_file, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("dba", NULL);
  if (boot_delete (database_name, force_delete) != NO_ERROR)
    {
      fprintf (stderr, "%s\n", db_error_string (3));
      goto error_exit;
    }

  if (output_file != stdout)
    {
      fclose (output_file);
    }
  return EXIT_SUCCESS;

print_delete_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_DELETEDB,
				   DELETEDB_MSG_USAGE),
	   basename (arg->argv0));
error_exit:
  if (output_file != stdout && output_file != NULL)
    {
      fclose (output_file);
    }
  return EXIT_FAILURE;
}

static int
parse_up_to_date (char *date_string, struct tm *time_data)
{
  int status;
  int date_index;
  char *save_ptr, *token;
  const char *delim = "-:";

  status = NO_ERROR;
  date_index = 0;
  token = strtok_r (date_string, delim, &save_ptr);
  while (status == NO_ERROR && token != NULL)
    {
      switch (date_index)
	{
	case 0:		// month-day
	  time_data->tm_mday = atoi (token);
	  if (time_data->tm_mday < 1 || time_data->tm_mday > 31)
	    {
	      status = ER_GENERIC_ERROR;
	    }
	  break;
	case 1:		// month
	  time_data->tm_mon = atoi (token) - 1;
	  if (time_data->tm_mon < 0 || time_data->tm_mon > 11)
	    {
	      status = ER_GENERIC_ERROR;
	    }
	  break;
	case 2:		// year
	  time_data->tm_year = atoi (token) - 1900;
	  if (time_data->tm_year < 0)
	    {
	      status = ER_GENERIC_ERROR;
	    }
	  break;
	case 3:		// hour
	  time_data->tm_hour = atoi (token);
	  if (time_data->tm_hour < 0 || time_data->tm_hour > 23)
	    {
	      status = ER_GENERIC_ERROR;
	    }
	  break;
	case 4:		// minute
	  time_data->tm_min = atoi (token);
	  if (time_data->tm_min < 0 || time_data->tm_min > 59)
	    {
	      status = ER_GENERIC_ERROR;
	    }
	  break;
	case 5:		// second
	  time_data->tm_sec = atoi (token);
	  if (time_data->tm_sec < 0 || time_data->tm_sec > 59)
	    {
	      status = ER_GENERIC_ERROR;
	    }
	  break;
	default:
	  status = ER_GENERIC_ERROR;
	  break;
	}
      date_index++;
      token = strtok_r (NULL, delim, &save_ptr);
    }

  return date_index != 6 ? ER_GENERIC_ERROR : status;
}

/*
 * restoredb() - restoredb main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
restoredb (UTIL_FUNCTION_ARG * arg)
{
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  int status;
  struct tm time_data;
  char *up_to_date;
  char *database_name;
  bool partial_recovery;
  bool set_replication;
  BO_RESTART_ARG restart_arg;

  database_name =
    utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  up_to_date =
    utility_get_option_string_value (arg_map, RESTORE_UP_TO_DATE_S, 0);
  partial_recovery =
    utility_get_option_bool_value (arg_map, RESTORE_PARTIAL_RECOVERY_S);
  set_replication =
    utility_get_option_bool_value (arg_map, RESTORE_REPLICATION_MODE_S);
  restart_arg.printtoc =
    utility_get_option_bool_value (arg_map, RESTORE_LIST_S);
  restart_arg.stopat = -1;
  restart_arg.backuppath =
    utility_get_option_string_value (arg_map, RESTORE_BACKUP_FILE_PATH_S, 0);
  restart_arg.level = utility_get_option_int_value (arg_map, RESTORE_LEVEL_S);
  restart_arg.verbose_file =
    utility_get_option_string_value (arg_map, RESTORE_OUTPUT_FILE_S, 0);
  restart_arg.newvolpath =
    utility_get_option_bool_value (arg_map,
				   RESTORE_USE_DATABASE_LOCATION_PATH_S);
  restart_arg.restore_upto_bktime = false;

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_restore_usage;
    }

  if (up_to_date != NULL && strlen (up_to_date) > 0)
    {
      if (strcasecmp (up_to_date, "backuptime") == 0)
	{
	  restart_arg.restore_upto_bktime = true;
	}
      else
	{
	  status = parse_up_to_date (up_to_date, &time_data);
	  restart_arg.stopat = mktime (&time_data);
	  if (status != NO_ERROR || restart_arg.stopat < 0)
	    {
	      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
					       MSGCAT_UTIL_SET_RESTOREDB,
					       RESTOREDB_MSG_BAD_DATE));
	      goto error_exit;
	    }
	}
    }
  else
    {
      restart_arg.stopat = time (NULL);
    }

  /* error message log file */
  sprintf (er_msg_file, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("dba", NULL);
  if (partial_recovery == true)
    {
      log_default_input_for_archive_log_location = 1;
    }
  status = boot_restart_from_backup (true, database_name, &restart_arg);
  if (status == NULL_TRAN_INDEX)
    {
      fprintf (stderr, "%s\n", db_error_string (3));
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_RESTOREDB,
				       RESTOREDB_MSG_FAILURE));
      goto error_exit;
    }
  else
    {
      if (set_replication == true)
	{
	  LOG_LSA *final_restored_lsa;
	  final_restored_lsa = log_get_final_restored_lsa ();
	  fprintf (stdout, "Last_lsa: %d|%d\n",
		   final_restored_lsa->pageid, final_restored_lsa->offset);
	}
      boot_shutdown_server (true);
    }

  return EXIT_SUCCESS;

print_restore_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_RESTOREDB,
				   RESTOREDB_MSG_USAGE),
	   basename (arg->argv0));
error_exit:
  return EXIT_FAILURE;
}

/*
 * renamedb() - renamedb main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
renamedb (UTIL_FUNCTION_ARG * arg)
{
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *src_db_name;
  const char *dest_db_name;
  const char *ext_path;
  const char *control_file_name;
  bool force_delete;

  src_db_name =
    utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  dest_db_name =
    utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 1);
  ext_path =
    utility_get_option_string_value (arg_map, RENAME_EXTENTED_VOLUME_PATH_S,
				     0);
  control_file_name =
    utility_get_option_string_value (arg_map, RENAME_CONTROL_FILE_S, 0);
  force_delete =
    utility_get_option_bool_value (arg_map, RENAME_DELETE_BACKUP_S);

  if (utility_get_option_string_table_size (arg_map) != 2)
    {
      goto print_rename_usage;
    }

  sysprm_set_to_default ("data_buffer_pages");

  if (check_database_name (src_db_name) || check_database_name (dest_db_name))
    {
      goto error_exit;
    }
  else if (ext_path && access (ext_path, F_OK) == -1)
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_COPYDB,
				       COPYDB_VOLEXT_PATH_INVALID));
      goto error_exit;
    }
  else if (control_file_name && access (control_file_name, F_OK) == -1)
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_COPYDB,
				       COPYDB_VOLS_TOFROM_PATHS_FILE_INVALID));
      goto error_exit;
    }

  /* error message log file */
  sprintf (er_msg_file, "%s_%s.err", src_db_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("dba", NULL);
  if (db_restart (arg->command_name, TRUE, src_db_name) != NO_ERROR)
    {
      fprintf (stdout, "%s\n", db_error_string (3));
      goto error_exit;
    }
  else
    {
      if (boot_soft_rename (src_db_name, dest_db_name, NULL, NULL, NULL,
			    ext_path, control_file_name, FALSE, TRUE,
			    force_delete) != NO_ERROR)
	{
	  fprintf (stdout, "%s\n", db_error_string (3));
	  db_shutdown ();
	  goto error_exit;
	}
      else
	{
	  db_commit_transaction ();
	  db_shutdown ();
	}
    }
  return EXIT_SUCCESS;

print_rename_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_RENAMEDB,
				   RENAMEDB_MSG_USAGE),
	   basename (arg->argv0));
error_exit:
  return EXIT_FAILURE;
}

/*
 * installdb() - installb main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
installdb (UTIL_FUNCTION_ARG * arg)
{
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *server_name;
  const char *db_path;
  const char *log_path;
  const char *db_name;
  DB_INFO *dir = NULL, *db;
  bool cfg_added = false;

  db_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  server_name =
    utility_get_option_string_value (arg_map, INSTALL_SERVER_NAME_S, 0);
  db_path = utility_get_option_string_value (arg_map, INSTALL_FILE_PATH_S, 0);
  log_path = utility_get_option_string_value (arg_map, INSTALL_LOG_PATH_S, 0);

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_install_usage;
    }

  if (check_database_name (db_name))
    {
      goto error_exit;
    }

  db = cfg_find_db (db_name);
  if (db != NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_DATABASE_EXISTS, 1,
	      db_name);
      fprintf (stderr, "%s\n", db_error_string (3));
      cfg_free_directory (db);
      goto error_exit;
    }

  /* have to add it to the config before calling boot_restart */
  if (cfg_read_directory (&dir, 1) != NO_ERROR)
    {
      fprintf (stderr, "%s\n", db_error_string (3));
      goto error_exit;
    }

  db = cfg_add_db (&dir, db_name, db_path, log_path, server_name);
  if (db == NULL)
    {
      fprintf (stderr, "%s\n", db_error_string (3));
      goto error_exit;
    }

  cfg_write_directory (dir);

  cfg_added = true;

  /* error message log file */
  sprintf (er_msg_file, "%s_%s.err", db_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("dba", NULL);
  if (db_restart (arg->command_name, TRUE, db_name) != NO_ERROR)
    {
      fprintf (stderr, "%s\n", db_error_string (3));
      goto error_exit;
    }

  if (boot_soft_rename (NULL, db_name, db_path, log_path, server_name,
			NULL, NULL, FALSE, FALSE, TRUE) != NO_ERROR)
    {
      fprintf (stderr, "%s\n", db_error_string (3));
      db_shutdown ();
      goto error_exit;
    }

  db_commit_transaction ();
  db_shutdown ();

  cfg_free_directory (dir);
  return EXIT_SUCCESS;

print_install_usage:
  fprintf (stderr,
	   msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_INSTALLDB,
			   INSTALLDB_MSG_USAGE), basename (arg->argv0));
error_exit:
  if (cfg_added)
    {
      /* got an error, have to back out the addition to the directory */
      cfg_delete_db (&dir, db_name);
      cfg_write_directory (dir);
    }
  if (dir != NULL)
    {
      cfg_free_directory (dir);
    }
  return EXIT_FAILURE;
}

/*
 * copydb() - copydb main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
copydb (UTIL_FUNCTION_ARG * arg)
{
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *src_db_name;
  const char *dest_db_name;
  const char *server_name;
  const char *db_path;
  const char *log_path;
  const char *ext_path;
  const char *control_file_name;
  bool overwrite;
  bool move;

  src_db_name =
    utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  dest_db_name =
    utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 1);
  server_name =
    utility_get_option_string_value (arg_map, COPY_SERVER_NAME_S, 0);
  db_path = utility_get_option_string_value (arg_map, COPY_FILE_PATH_S, 0);
  log_path = utility_get_option_string_value (arg_map, COPY_LOG_PATH_S, 0);
  ext_path =
    utility_get_option_string_value (arg_map, COPY_EXTENTED_VOLUME_PATH_S, 0);
  control_file_name =
    utility_get_option_string_value (arg_map, COPY_CONTROL_FILE_S, 0);
  overwrite = utility_get_option_bool_value (arg_map, COPY_REPLACE_S);
  move = utility_get_option_bool_value (arg_map, COPY_DELETE_SOURCE_S);

  if (utility_get_option_string_table_size (arg_map) != 2)
    {
      goto print_copy_usage;
    }

  sysprm_set_to_default ("data_buffer_pages");

  if (check_database_name (src_db_name) || check_database_name (dest_db_name))
    {
      goto error_exit;
    }

  if (strcmp (src_db_name, dest_db_name) == 0)
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_COPYDB,
				       COPYDB_MSG_IDENTICAL));
      goto error_exit;
    }

  /* error message log file */
  sprintf (er_msg_file, "%s_%s.err", src_db_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("dba", NULL);

  if (db_restart (arg->command_name, TRUE, src_db_name) != NO_ERROR)
    {
      fprintf (stderr, "%s\n", db_error_string (3));
      goto error_exit;
    }

  if (boot_copy (src_db_name, dest_db_name, db_path, log_path, server_name,
		 ext_path, control_file_name, overwrite) != NO_ERROR)
    {
      fprintf (stdout, "%s\n", db_error_string (3));
      goto error_exit;
    }
  if (move)
    {
      boot_delete (src_db_name, true);
    }
  return EXIT_SUCCESS;

print_copy_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_COPYDB, COPYDB_MSG_USAGE),
	   basename (arg->argv0));
error_exit:
  return EXIT_FAILURE;
}

/*
 * optimizedb() - optimizedb main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
optimizedb (UTIL_FUNCTION_ARG * arg)
{
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *db_name;
  const char *class_name;
  int status;
  DB_OBJECT *class_mop;

  db_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  class_name =
    utility_get_option_string_value (arg_map, OPTIMIZE_CLASS_NAME_S, 0);

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_optimize_usage;
    }

  status = NO_ERROR;

  if (check_database_name (db_name))
    {
      goto error_exit;
    }

  /* error message log file */
  sprintf (er_msg_file, "%s_%s.err", db_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("dba", NULL);
  if (db_restart (arg->command_name, TRUE, db_name) != NO_ERROR)
    {
      fprintf (stderr, "%s\n", db_error_string (3));
      goto error_exit;
    }

  if (class_name != NULL && class_name[0] != 0)
    {
      if ((class_mop = db_find_class (class_name)) == NULL ||
	  sm_update_statistics (class_mop) != NO_ERROR)
	{
	  fprintf (stderr, "%s\n", db_error_string (3));
	  db_shutdown ();
	  goto error_exit;
	}
    }
  else
    {
      if (sm_update_all_statistics () != NO_ERROR)
	{
	  fprintf (stderr, "%s\n", db_error_string (3));
	  db_shutdown ();
	  goto error_exit;
	}
    }
  db_commit_transaction ();
  db_shutdown ();

  return EXIT_SUCCESS;

print_optimize_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_OPTIMIZEDB,
				   OPTIMIZEDB_MSG_USAGE),
	   basename (arg->argv0));
error_exit:
  return EXIT_FAILURE;
}

typedef enum
{
  DIAGDUMP_ALL = -1,
  DIAGDUMP_FILE_TABLES = 1,
  DIAGDUMP_FILE_CAPACITIES,
  DIAGDUMP_HEAP_CAPACITIES,
  DIAGDUMP_INDEX_CAPACITIES,
  DIAGDUMP_CLASSNAMES,
  DIAGDUMP_DISK_BITMAPS,
  DIAGDUMP_CATALOG,
  DIAGDUMP_LOG
} DIAGDUMP_TYPE;

/*
 * diagdb() - diagdb main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
diagdb (UTIL_FUNCTION_ARG * arg)
{
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *db_name;
  DIAGDUMP_TYPE diag;

  db_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  diag = utility_get_option_int_value (arg_map, DIAG_DUMP_TYPE_S);

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_diag_usage;
    }

  if (check_database_name (db_name))
    {
      goto error_exit;
    }

  /* error message log file */
  sprintf (er_msg_file, "%s_%s.err", db_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("dba", NULL);
  if (db_restart (arg->command_name, TRUE, db_name) != NO_ERROR)
    {
      fprintf (stdout, "%s\n", db_error_string (3));
      goto error_exit;
    }

  if (diag < DIAGDUMP_ALL || diag > DIAGDUMP_LOG)
    {
      diag = DIAGDUMP_ALL;
    }

  if (diag == DIAGDUMP_ALL || diag == DIAGDUMP_FILE_TABLES)
    {
      /* this dumps the allocated file stats */
      printf ("\n*** DUMP OF FILE STATISTICS ***\n");
      file_tracker_dump (NULL, stdout);
    }

  if (diag == DIAGDUMP_ALL || diag == DIAGDUMP_FILE_CAPACITIES)
    {
      /* this dumps the allocated file stats */
      printf ("\n*** DUMP OF FILE DESCRIPTIONS ***\n");
      file_dump_all_capacities (NULL, stdout);
    }

  if (diag == DIAGDUMP_ALL || diag == DIAGDUMP_HEAP_CAPACITIES)
    {
      /* this dumps lower level info about capacity of all heaps */
      printf ("\n*** DUMP CAPACITY OF ALL HEAPS ***\n");
      heap_dump_all_capacities (NULL, stdout);
    }

  if (diag == DIAGDUMP_ALL || diag == DIAGDUMP_INDEX_CAPACITIES)
    {
      /* this dumps lower level info about capacity of
       * all indices */
      printf ("\n*** DUMP CAPACITY OF ALL INDICES ***\n");
      btree_dump_capacity_all (NULL, stdout);
    }

  if (diag == DIAGDUMP_ALL || diag == DIAGDUMP_CLASSNAMES)
    {
      /* this dumps the known classnames */
      printf ("\n*** DUMP CLASSNAMES ***\n");
      locator_dump_class_names (NULL, stdout);
    }

  if (diag == DIAGDUMP_ALL || diag == DIAGDUMP_DISK_BITMAPS)
    {
      /* this dumps lower level info about the disk */
      printf ("\n*** DUMP OF DISK STATISTICS ***\n");
      disk_dump_all (NULL, stdout);
    }

  if (diag == DIAGDUMP_ALL || diag == DIAGDUMP_CATALOG)
    {
      /* this dumps the content of catalog */
      printf ("\n*** DUMP OF CATALOG ***\n");
      catalog_dump (NULL, stdout, 1);
    }

  if (diag == DIAGDUMP_ALL || diag == DIAGDUMP_LOG)
    {
      /* this dumps the content of log */
      int isforward, start_logpageid, dump_npages, desired_tranid;
      char yn[2];
      do
	{
	  printf ("\n");
	  printf ("isforward (1 or 0) ? ");
	  scanf ("%d", &isforward);
	  printf ("start_logpageid (-1 for the first/last page) ? ");
	  scanf ("%d", &start_logpageid);
	  printf ("dump_npages (-1 for all pages) ? ");
	  scanf ("%d", &dump_npages);
	  printf ("desired_tranid (-1 for all transactions) ? ");
	  scanf ("%d", &desired_tranid);
	  printf ("log_dump(%d, %d, %d, %d) (y/n) ? ",
		  isforward, start_logpageid, dump_npages, desired_tranid);
	  scanf ("%1s", yn);
	}
      while (yn[0] != 'y');
      printf ("\n*** DUMP OF LOG ***\n");
      xlog_dump (NULL, stdout, isforward, start_logpageid, dump_npages,
		 desired_tranid);
    }
  db_shutdown ();

  return EXIT_SUCCESS;

print_diag_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_DIAGDB, DIAGDB_MSG_USAGE),
	   basename (arg->argv0));
error_exit:
  return EXIT_FAILURE;
}

/*
 * patchdb() - patchdb main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
patchdb (UTIL_FUNCTION_ARG * arg)
{
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  const char *db_name;
  bool recreate_log;

  db_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  recreate_log =
    utility_get_option_bool_value (arg_map, PATCH_RECREATE_LOG_S);

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_patch_usage;
    }

  if (check_database_name (db_name))
    {
      goto error_exit;
    }

  if (boot_emergency_patch (db_name, recreate_log) != NO_ERROR)
    {
      fprintf (stderr, "emergency patch fail.\n");
      goto error_exit;
    }

  return EXIT_SUCCESS;

print_patch_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_PATCHDB,
				   PATCHDB_MSG_USAGE), basename (arg->argv0));
error_exit:
  return EXIT_FAILURE;
}

/*
 * estimatedb_data() - estimatedb_data main routine
 *   return: EXIT_SUCCES/EXIT_FAILURE
 */
int
estimatedb_data (UTIL_FUNCTION_ARG * arg)
{
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  int num_instance;
  int avg_inst_size;
  int num_attr;
  int num_var_attr;
  int npages;

  if (utility_get_option_string_table_size (arg_map) != 4)
    {
      goto print_estimate_data_usage;
    }

  num_instance =
    atoi (utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0));
  avg_inst_size =
    atoi (utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 1));
  num_attr =
    atoi (utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 2));
  num_var_attr =
    atoi (utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 3));

  sysprm_load_and_init (NULL, NULL);
  db_set_page_size (IO_DEFAULT_PAGE_SIZE);
  npages = heap_estimate_num_pages_needed (NULL, num_instance, avg_inst_size,
					   num_attr, num_var_attr);
  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_ESTIMATEDB_DATA,
				   ESTIMATEDB_DATA_MSG_NPAGES), npages);
  return EXIT_SUCCESS;

print_estimate_data_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_ESTIMATEDB_DATA,
				   ESTIMATEDB_DATA_MSG_USAGE),
	   basename (arg->argv0));
  return EXIT_FAILURE;
}

/*
 * estimatedb_index() - restoredb main routine
 *   return: 0 : success
 *           non-zero : error
 */
int
estimatedb_index (UTIL_FUNCTION_ARG * arg)
{
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  int num_instance;
  int num_diffkeys;
  int avg_key_size;
  const char *key_type;

  int status = -1;
  int npages = 0;
  int blt_npages = 0;
  int blt_wrs_npages = 0;
  PR_TYPE *type;
  DB_DOMAIN *domain = (DB_DOMAIN *) 0;

  if (utility_get_option_string_table_size (arg_map) != 4)
    {
      goto print_estimate_index_usage;
    }

  num_instance =
    atoi (utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0));
  num_diffkeys =
    atoi (utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 1));
  avg_key_size =
    atoi (utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 2));
  key_type =
    utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 3);

  sysprm_load_and_init (NULL, NULL);
  db_set_page_size (IO_DEFAULT_PAGE_SIZE);
  if (num_instance <= num_diffkeys)
    {
      num_instance = num_diffkeys;
    }
  /* Initialize domain area */
  tp_init ();

  if (key_type != NULL)
    {
      domain = pt_string_to_db_domain (key_type, (const char *) NULL);
      if (domain)
	{
	  type = domain->type;
	  if (type != NULL)
	    {
	      if (tp_valid_indextype (type->id))
		{


		  switch (type->id)
		    {
		    case DB_TYPE_BIT:
		    case DB_TYPE_VARBIT:
		    case DB_TYPE_CHAR:
		    case DB_TYPE_VARCHAR:
		    case DB_TYPE_NCHAR:
		    case DB_TYPE_VARNCHAR:
		      /* Do not override any information in Avg_key_size with
		         precision information. Just make sure the input makes
		         sense for these cases.  */
		      if (avg_key_size > domain->precision)
			{
			  /* Does not make sense to have avg_key_size
			     bigger than precision - inform user of
			     error. This is illegal input since the char
			     precision defaults to 1.
			     estimatedb_index 100000 1000 5 char */
			  status = -2;
			}
		      break;
		    default:
		      break;
		    }

		  if (status != -2)
		    {
		      /*
		       * This will call pr_estimate_size which uses the
		       * Avg_key_size and domain->type->id to really
		       * compute the correct average key size that we
		       * need to estimate the total number of pages.
		       */
		      npages =
			btree_estimate_total_numpages (NULL, num_diffkeys,
						       avg_key_size, domain,
						       num_instance,
						       &blt_npages,
						       &blt_wrs_npages);

		      fprintf (stdout,
			       msgcat_message (MSGCAT_CATALOG_UTILS,
					       MSGCAT_UTIL_SET_ESTIMATEDB_INDEX,
					       ESTIMATEDB_INDEX_MSG_INPUT));
		      fprintf (stdout,
			       msgcat_message (MSGCAT_CATALOG_UTILS,
					       MSGCAT_UTIL_SET_ESTIMATEDB_INDEX,
					       ESTIMATEDB_INDEX_MSG_INSTANCES),
			       num_instance);
		      fprintf (stdout,
			       msgcat_message (MSGCAT_CATALOG_UTILS,
					       MSGCAT_UTIL_SET_ESTIMATEDB_INDEX,
					       ESTIMATEDB_INDEX_MSG_NUMBER_KEYS),
			       num_diffkeys);
		      fprintf (stdout,
			       msgcat_message (MSGCAT_CATALOG_UTILS,
					       MSGCAT_UTIL_SET_ESTIMATEDB_INDEX,
					       ESTIMATEDB_INDEX_MSG_AVG_KEYSIZE),
			       avg_key_size);
		      fprintf (stdout,
			       msgcat_message (MSGCAT_CATALOG_UTILS,
					       MSGCAT_UTIL_SET_ESTIMATEDB_INDEX,
					       ESTIMATEDB_INDEX_MSG_KEYTYPE),
			       key_type);
		      fflush (stdout);
		      status = 0;
		    }
		}
	    }
	}
    }

  /* Remove domain area */
  tp_final ();
  switch (status)
    {
    case -1:
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_ESTIMATEDB_INDEX,
				       ESTIMATEDB_INDEX_BAD_KEYTYPE),
	       key_type);
      break;
    case -2:
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_ESTIMATEDB_INDEX,
				       ESTIMATEDB_INDEX_BAD_KEYLENGTH),
	       avg_key_size, domain->precision);
      break;
    case 1:
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_ESTIMATEDB_INDEX,
				       ESTIMATEDB_INDEX_BAD_ARGUMENTS));
      break;
    default:
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_ESTIMATEDB_INDEX,
				       ESTIMATEDB_INDEX_MSG_NPAGES), npages);
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_ESTIMATEDB_INDEX,
				       ESTIMATEDB_INDEX_MSG_BLT_NPAGES),
	       blt_npages);
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_ESTIMATEDB_INDEX,
				       ESTIMATEDB_INDEX_MSG_BLT_WRS_NPAGES),
	       blt_wrs_npages);
      break;
    }
  return status;

print_estimate_index_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_ESTIMATEDB_INDEX,
				   ESTIMATEDB_INDEX_MSG_USAGE),
	   basename (arg->argv0));
  return EXIT_FAILURE;
}

/*
 * alterdbhost() - restoredb main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
alterdbhost (UTIL_FUNCTION_ARG * arg)
{
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  const char *db_name;
  const char *host_name;
  int dbtxt_vdes = NULL_VOLDES;
  int log_vdes = NULL_VOLDES;
  char dbtxt_label[PATH_MAX];
  char host_name_buf[MAXHOSTNAMELEN + 1];
  DB_INFO *db = NULL;
  DB_INFO *dir = NULL;
  const char *log_prefix;

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_alterdbhost_usage;
    }

  db_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  host_name =
    utility_get_option_string_value (arg_map, ALTERDBHOST_HOST_S, 0);

  if (check_database_name (db_name))
    {
      goto error_exit;
    }

  /* If a host was not given, assume the current host */
  if (host_name == NULL)
    {
#if 0				/* use Unix-domain socket for localhost */
      if (GETHOSTNAME (host_name_buf, MAXHOSTNAMELEN) != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_BO_UNABLE_TO_FIND_HOSTNAME, 0);
	  goto error;
	}
#else
      strcpy (host_name_buf, "localhost");
#endif
      host_name = host_name_buf;
    }

  /* get the database directory information in write mode */
  if (cfg_maycreate_get_directory_filename (dbtxt_label) == NULL
#if !defined(WINDOWS) || !defined(DONT_USE_MANDATORY_LOCK_IN_WINDOWS)
/* Temporary fix for NT file locking problem */
      || (dbtxt_vdes =
	  fileio_mount (dbtxt_label, dbtxt_label, LOG_DBTXT_VOLID, 2,
			true)) == NULL_VOLDES
#endif /* !WINDOWS || !DONT_USE_MANDATORY_LOCK_IN_WINDOWS */
    )
    {
      goto error;
    }

  if (dbtxt_vdes != NULL_VOLDES)
    {
      if (cfg_read_directory_ex (dbtxt_vdes, &dir, true) != NO_ERROR)
	{
	  goto error;
	}
    }
  else
    {
      if (cfg_read_directory (&dir, true) != NO_ERROR)
	{
	  goto error;
	}
    }

  if (dir != NULL && (db = cfg_find_db_list (dir, db_name)) == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_UNKNOWN_DATABASE, 1,
	      db_name);
      goto error;
    }

  /* Compose the full name of the database and find location of logs */
  log_prefix = fileio_get_base_file_name (db_name);
  COMPOSE_FULL_NAME (BO_DB_FULLNAME, db->pathname, db_name);

  /* System is not restarted. Read the header from disk */
  logpb_initialize_log_names (NULL, BO_DB_FULLNAME, db->logpath, log_prefix);

  /* Avoid setting errors at this moment related to existance of files. */
  if (fileio_is_volume_exist (log_Name_active) == false)
    {
      goto error;
    }

  if ((log_vdes =
       fileio_mount (BO_DB_FULLNAME, log_Name_active, LOG_DBLOG_ACTIVE_VOLID,
		     true, false)) == NULL_VOLDES)
    {
      goto error;
    }

  /* Now update the entry in the database table */
  if (db == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_UNKNOWN_DATABASE, 1,
	      db_name);
      goto error;
    }
  else
    {
      int num_hosts;
      if (db->hosts != NULL)
	{
	  cfg_free_hosts (db->hosts);
	}
      db->hosts = cfg_get_hosts (host_name, &num_hosts, false);
    }

  /* Dismount lgat */
  fileio_dismount (log_vdes);

#if defined(WINDOWS) && !defined(DONT_USE_MANDATORY_LOCK_IN_WINDOWS)
  /* must unlock this before we can open it again for writing */
  if (dbtxt_vdes != NULL_VOLDES)
    {
      fileio_dismount (dbtxt_vdes);
      dbtxt_vdes = NULL_VOLDES;
    }
#endif /* WINDOWS && !DONT_USE_MANDATORY_LOCK_IN_WINDOWS */
  if (dbtxt_vdes != NULL_VOLDES)
    {
      cfg_write_directory_ex (dbtxt_vdes, dir);
    }
  else
    {
      cfg_write_directory (dir);
    }

  cfg_free_directory (dir);
  if (dbtxt_vdes != NULL_VOLDES)
    {
      fileio_dismount (dbtxt_vdes);
    }

  return EXIT_SUCCESS;

error:
  fprintf (stdout, "%s\n", db_error_string (3));

  /* Deallocate any allocated structures */
  if (dir != NULL)
    {
      cfg_free_directory (dir);
    }

  if (dbtxt_vdes != NULL_VOLDES)
    {
      fileio_dismount (dbtxt_vdes);
    }

  return EXIT_FAILURE;

print_alterdbhost_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_ALTERDBHOST,
				   ALTERDBHOST_MSG_USAGE),
	   basename (arg->argv0));
error_exit:
  return EXIT_FAILURE;
}
