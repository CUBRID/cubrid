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
#if defined(WINDOWS)
#include <io.h>
#endif

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
#include "locale_support.h"
#include "tz_support.h"
#include "tz_compile.h"
#include "boot_cl.h"
#include "tsc_timer.h"
#if defined(WINDOWS)
#include "wintcp.h"
#else
#include <dlfcn.h>
#endif

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
extern int log_default_input_for_archive_log_location;

extern int catcls_compile_catalog_classes (THREAD_ENTRY * thread_p);
extern int catcls_get_db_collation (THREAD_ENTRY * thread_p, LANG_COLL_COMPAT ** db_collations, int *coll_cnt);

static int parse_user_define_line (char *line, FILE * output_file);
static int parse_user_define_file (FILE * user_define_file, FILE * output_file);
static int parse_up_to_date (char *up_to_date, struct tm *time_date);
static int print_backup_info (char *database_name, BO_RESTART_ARG * restart_arg);
static int synccoll_check (const char *db_name, int *db_obs_coll_cnt, int *new_sys_coll_cnt);

static int delete_all_ha_apply_info (void);
static int insert_ha_apply_info (char *database_name, char *master_host_name, INT64 database_creation, INT64 pageid,
				 int offset);
static int delete_all_slave_ha_apply_info (char *database_name, char *master_host_name);

static bool check_ha_db_and_node_list (char *database_name, char *source_host_name);


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
  fprintf (stderr, utility_get_generic_message (MSGCAT_UTIL_GENERIC_ADMIN_USAGE), PRODUCT_STRING, exec_name, exec_name,
	   exec_name);
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
  fprintf (stderr, utility_get_generic_message (MSGCAT_UTIL_GENERIC_VERSION), exec_name, PRODUCT_STRING);
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
	  fprintf (output_file,
		   msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CREATEDB, CREATEDB_MSG_MISSING_USER), token);
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
	  fprintf (output_file,
		   msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CREATEDB, CREATEDB_MSG_UNKNOWN_CMD), token);
	  return ER_GENERIC_ERROR;
	}

      /* loop for the list of user's name */
      while ((token = strtok_r (NULL, delim, &save_ptr)) != NULL)
	{
	  database_object = au_find_user (token);
	  if (database_object == NULL)
	    {
	      /* could not found the user */
	      fprintf (output_file,
		       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CREATEDB, CREATEDB_MSG_MISSING_USER),
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
	  fprintf (output_file,
		   msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CREATEDB, CREATEDB_MSG_UNKNOWN_CMD), token);
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

static void
make_valid_page_size (int *v)
{
  int pow_size;

  if (*v < IO_MIN_PAGE_SIZE)
    {
      *v = IO_MIN_PAGE_SIZE;
      return;
    }

  if (*v > IO_MAX_PAGE_SIZE)
    {
      *v = IO_MAX_PAGE_SIZE;
      return;
    }

  pow_size = IO_MIN_PAGE_SIZE;
  if ((*v & (*v - 1)) != 0)
    {
      while (pow_size < *v)
	{
	  pow_size *= 2;
	}
      *v = pow_size;
    }
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
  const char *lob_path;
  const char *host_name;
  bool overwrite;
  bool verbose;
  const char *comment;
  const char *init_file_name;
  const char *volume_spec_file_name;
  const char *user_define_file_name;
  const char *cubrid_charset;

  int db_volume_pages;
  int db_page_size;
  UINT64 db_volume_size;
  int log_volume_pages;
  int log_page_size;
  UINT64 log_volume_size;
  char *db_volume_str;
  char *db_page_str;
  char *log_volume_str;
  char *log_page_str;
  TZ_DATA *tzd;

  char required_size[16];

  database_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  cubrid_charset = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 1);

  if (database_name == 0 || database_name[0] == 0 || cubrid_charset == 0 || cubrid_charset[0] == 0
      || utility_get_option_string_table_size (arg_map) != 2)
    {
      goto print_create_usage;
    }

  /* initialize time zone data */
  if (tz_load () != NO_ERROR)
    {
      goto error_exit;
    }

  if (sysprm_load_and_init (database_name, NULL) != NO_ERROR)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_SERVICE_PROPERTY_FAIL);
      goto error_exit;
    }

  output_file_name = utility_get_option_string_value (arg_map, CREATE_OUTPUT_FILE_S, 0);
  program_name = arg->command_name;
  volume_path = utility_get_option_string_value (arg_map, CREATE_FILE_PATH_S, 0);
  log_path = utility_get_option_string_value (arg_map, CREATE_LOG_PATH_S, 0);
  lob_path = utility_get_option_string_value (arg_map, CREATE_LOB_PATH_S, 0);
  host_name = utility_get_option_string_value (arg_map, CREATE_SERVER_NAME_S, 0);
  overwrite = utility_get_option_bool_value (arg_map, CREATE_REPLACE_S);
  verbose = utility_get_option_bool_value (arg_map, CREATE_VERBOSE_S);
  comment = utility_get_option_string_value (arg_map, CREATE_COMMENT_S, 0);
  init_file_name = utility_get_option_string_value (arg_map, CREATE_CSQL_INITIALIZATION_FILE_S, 0);
  volume_spec_file_name = utility_get_option_string_value (arg_map, CREATE_MORE_VOLUME_FILE_S, 0);
  user_define_file_name = utility_get_option_string_value (arg_map, CREATE_USER_DEFINITION_FILE_S, 0);

  db_page_size = utility_get_option_int_value (arg_map, CREATE_PAGE_SIZE_S);
  if (db_page_size != -1)
    {
      util_print_deprecated ("--" CREATE_PAGE_SIZE_L);
    }
  else
    {
      db_page_size = IO_PAGESIZE;
    }

  db_page_str = utility_get_option_string_value (arg_map, CREATE_DB_PAGE_SIZE_S, 0);
  if (db_page_str != NULL)
    {
      UINT64 v;

      if (util_size_string_to_byte (&v, db_page_str) != NO_ERROR)
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message
				 (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CREATEDB, CREATEDB_MSG_INVALID_SIZE),
				 CREATE_DB_PAGE_SIZE_L, db_page_str);
	  goto error_exit;
	}
      db_page_size = (int) v;
    }
  make_valid_page_size (&db_page_size);

  db_volume_str = utility_get_option_string_value (arg_map, CREATE_DB_VOLUME_SIZE_S, 0);
  if (db_volume_str == NULL)
    {
      db_volume_size = prm_get_bigint_value (PRM_ID_DB_VOLUME_SIZE);
    }
  else
    {
      if (util_size_string_to_byte (&db_volume_size, db_volume_str) != NO_ERROR)
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message
				 (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CREATEDB, CREATEDB_MSG_INVALID_SIZE),
				 CREATE_DB_VOLUME_SIZE_L, db_volume_str);
	  goto error_exit;
	}
    }

  db_volume_pages = utility_get_option_int_value (arg_map, CREATE_PAGES_S);
  if (db_volume_pages != -1)
    {
      util_print_deprecated ("--" CREATE_PAGES_L);
    }
  else
    {
      db_volume_pages = (int) (db_volume_size / db_page_size);
    }
  db_volume_size = (UINT64) db_volume_pages *(UINT64) db_page_size;

  log_page_str = utility_get_option_string_value (arg_map, CREATE_LOG_PAGE_SIZE_S, 0);
  if (log_page_str == NULL)
    {
      log_page_size = db_page_size;
    }
  else
    {
      UINT64 v;

      if (util_size_string_to_byte (&v, log_page_str) != NO_ERROR)
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message
				 (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CREATEDB, CREATEDB_MSG_INVALID_SIZE),
				 CREATE_LOG_PAGE_SIZE_L, log_page_str);
	  goto error_exit;
	}
      log_page_size = (int) v;
    }
  make_valid_page_size (&log_page_size);

  log_volume_str = utility_get_option_string_value (arg_map, CREATE_LOG_VOLUME_SIZE_S, 0);
  if (log_volume_str == NULL)
    {
      log_volume_size = prm_get_bigint_value (PRM_ID_LOG_VOLUME_SIZE);
    }
  else
    {
      if (util_size_string_to_byte (&log_volume_size, log_volume_str) != NO_ERROR)
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message
				 (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CREATEDB, CREATEDB_MSG_INVALID_SIZE),
				 CREATE_LOG_VOLUME_SIZE_L, log_volume_str);
	  goto error_exit;
	}
    }

  log_volume_pages = utility_get_option_int_value (arg_map, CREATE_LOG_PAGE_COUNT_S);
  if (log_volume_pages != -1)
    {
      util_print_deprecated ("--" CREATE_LOG_PAGE_COUNT_L);
    }
  else
    {
      log_volume_pages = (int) (log_volume_size / log_page_size);
    }

  if (check_new_database_name (database_name))
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
      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CREATEDB, CREATEDB_MSG_BAD_OUTPUT),
			     output_file_name);
      goto error_exit;
    }

  if (sysprm_check_range (prm_get_name (PRM_ID_DB_VOLUME_SIZE), &db_volume_size) != NO_ERROR)
    {
      UINT64 min, max;
      char min_buf[64], max_buf[64], vol_buf[64];

      if (sysprm_get_range (prm_get_name (PRM_ID_DB_VOLUME_SIZE), &min, &max) != NO_ERROR)
	{
	  goto error_exit;
	}
      util_byte_to_size_string (min_buf, 64, min);
      util_byte_to_size_string (max_buf, 64, max);
      if (db_volume_str != NULL)
	{
	  int len;
	  len = strlen (db_volume_str);
	  if (char_isdigit (db_volume_str[len - 1]))
	    {
	      snprintf (vol_buf, 64, "%sB", db_volume_str);
	    }
	  else
	    {
	      snprintf (vol_buf, 64, "%s", db_volume_str);
	    }
	}
      else
	{
	  util_byte_to_size_string (vol_buf, 64, db_volume_size);
	}
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CREATEDB, CREATEDB_MSG_FAILURE));

      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CREATEDB, CREATEDB_MSG_BAD_RANGE),
			     prm_get_name (PRM_ID_DB_VOLUME_SIZE), vol_buf, min_buf, max_buf);
      goto error_exit;
    }
  if (sysprm_check_range (prm_get_name (PRM_ID_LOG_VOLUME_SIZE), &log_volume_size) != NO_ERROR)
    {
      UINT64 min, max;
      char min_buf[64], max_buf[64], vol_buf[64];

      if (sysprm_get_range (prm_get_name (PRM_ID_LOG_VOLUME_SIZE), &min, &max) != NO_ERROR)
	{
	  goto error_exit;
	}
      util_byte_to_size_string (min_buf, 64, min);
      util_byte_to_size_string (max_buf, 64, max);
      if (log_volume_str != NULL)
	{
	  int len;
	  len = strlen (log_volume_str);
	  if (char_isdigit (log_volume_str[len - 1]))
	    {
	      snprintf (vol_buf, 64, "%sB", log_volume_str);
	    }
	  else
	    {
	      snprintf (vol_buf, 64, "%s", log_volume_str);
	    }
	}
      else
	{
	  util_byte_to_size_string (vol_buf, 64, log_volume_size);
	}
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CREATEDB, CREATEDB_MSG_FAILURE));
      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CREATEDB, CREATEDB_MSG_BAD_RANGE),
			     prm_get_name (PRM_ID_LOG_VOLUME_SIZE), vol_buf, min_buf, max_buf);

      goto error_exit;
    }

  if (user_define_file_name != 0)
    {
      user_define_file = fopen (user_define_file_name, "r");
      if (user_define_file == NULL)
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message
				 (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CREATEDB, CREATEDB_MSG_BAD_USERFILE),
				 user_define_file_name);
	  goto error_exit;
	}
    }

  util_byte_to_size_string (er_msg_file, sizeof (er_msg_file), db_volume_size);
  /* total amount of disk space of database is db volume size + log_volume_size + temp_log_volume_size */
  util_byte_to_size_string (required_size, sizeof (required_size), db_volume_size + (UINT64) (log_volume_size * 2));
  fprintf (output_file, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CREATEDB, CREATEDB_MSG_CREATING),
	   er_msg_file, cubrid_charset, required_size);

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  /* tuning system parameters */
  sysprm_set_force (prm_get_name (PRM_ID_PB_NBUFFERS), "1024");
  sysprm_set_force (prm_get_name (PRM_ID_XASL_CACHE_MAX_ENTRIES), "-1");
  sysprm_set_force (prm_get_name (PRM_ID_JAVA_STORED_PROCEDURE), "no");

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);

  db_login ("DBA", NULL);
  status =
    db_init (program_name, true, database_name, volume_path, NULL, log_path, lob_path, host_name, overwrite, comment,
	     volume_spec_file_name, db_volume_pages, db_page_size, log_volume_pages, log_page_size, cubrid_charset);

  if (status != NO_ERROR)
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CREATEDB, CREATEDB_MSG_FAILURE));
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }

  sm_mark_system_classes ();

  (void) lang_db_put_charset ();

  tzd = tz_get_data ();
  if (put_timezone_checksum (tzd->checksum) != NO_ERROR)
    {
      goto error_exit;
    }

  if (verbose)
    {
      au_dump_to_file (output_file);
    }
  if (!tf_Metaclass_class.mc_n_variable)
    {
      tf_compile_meta_classes ();
    }
  if ((catcls_Enable != true) && (catcls_compile_catalog_classes (NULL) != NO_ERROR))
    {
      util_log_write_errstr ("%s\n", db_error_string (3));
      db_shutdown ();
      goto error_exit;
    }
  if (catcls_Enable == true)
    {
      if (sm_force_write_all_classes () != NO_ERROR || au_force_write_new_auth () != NO_ERROR)
	{
	  util_log_write_errstr ("%s\n", db_error_string (3));
	  db_shutdown ();
	  goto error_exit;
	}
    }
  if (sm_update_all_catalog_statistics (STATS_WITH_FULLSCAN) != NO_ERROR)
    {
      util_log_write_errstr ("%s\n", db_error_string (3));
      db_shutdown ();
      goto error_exit;
    }

  db_commit_transaction ();

  if (user_define_file != NULL)
    {
      if (parse_user_define_file (user_define_file, output_file) != NO_ERROR)
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message
				 (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CREATEDB, CREATEDB_MSG_BAD_USERFILE),
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
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CREATEDB, CREATEDB_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

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

  database_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_delete_usage;
    }

  output_file_name = utility_get_option_string_value (arg_map, DELETE_OUTPUT_FILE_S, 0);
  force_delete = utility_get_option_bool_value (arg_map, DELETE_DELETE_BACKUP_S);

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_delete_usage;
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  if (check_database_name (database_name))
    {
      goto error_exit;
    }

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
      PRINT_AND_LOG_ERR_MSG (msgcat_message
			     (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_GENERIC, MSGCAT_UTIL_GENERIC_BAD_OUTPUT_FILE),
			     output_file_name);

      goto error_exit;
    }

  /* tuning system parameters */
  sysprm_set_force (prm_get_name (PRM_ID_PB_NBUFFERS), "1024");
  sysprm_set_force (prm_get_name (PRM_ID_JAVA_STORED_PROCEDURE), "no");

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("DBA", NULL);
  if (boot_delete (database_name, force_delete) != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }

  er_final (ER_ALL_FINAL);
  if (output_file != stdout)
    {
      fclose (output_file);
    }
  return EXIT_SUCCESS;

print_delete_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_DELETEDB, DELETEDB_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

error_exit:
  er_final (ER_ALL_FINAL);
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
	case 0:		/* month-day */
	  time_data->tm_mday = atoi (token);
	  if (time_data->tm_mday < 1 || time_data->tm_mday > 31)
	    {
	      status = ER_GENERIC_ERROR;
	    }
	  break;
	case 1:		/* month */
	  time_data->tm_mon = atoi (token) - 1;
	  if (time_data->tm_mon < 0 || time_data->tm_mon > 11)
	    {
	      status = ER_GENERIC_ERROR;
	    }
	  break;
	case 2:		/* year */
	  time_data->tm_year = atoi (token) - 1900;
	  if (time_data->tm_year < 0)
	    {
	      status = ER_GENERIC_ERROR;
	    }
	  break;
	case 3:		/* hour */
	  time_data->tm_hour = atoi (token);
	  if (time_data->tm_hour < 0 || time_data->tm_hour > 23)
	    {
	      status = ER_GENERIC_ERROR;
	    }
	  break;
	case 4:		/* minute */
	  time_data->tm_min = atoi (token);
	  if (time_data->tm_min < 0 || time_data->tm_min > 59)
	    {
	      status = ER_GENERIC_ERROR;
	    }
	  break;
	case 5:		/* second */
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

static int
print_backup_info (char *database_name, BO_RESTART_ARG * restart_arg)
{
  char from_volbackup[PATH_MAX];
  int error_code = NO_ERROR;
  DB_INFO *dir = NULL, *db;

  error_code = cfg_read_directory (&dir, false);
  if (error_code != NO_ERROR)
    {
      goto exit;
    }

  db = cfg_find_db_list (dir, database_name);
  if (db == NULL)
    {
      error_code = ER_BO_UNKNOWN_DATABASE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, database_name);
      goto exit;
    }

  COMPOSE_FULL_NAME (BO_DB_FULLNAME, sizeof (BO_DB_FULLNAME), db->pathname, database_name);

  error_code =
    fileio_get_backup_volume (NULL, BO_DB_FULLNAME, db->logpath, restart_arg->backuppath, restart_arg->level,
			      from_volbackup);
  if (error_code != NO_ERROR)
    {
      goto exit;
    }

  error_code = fileio_list_restore (NULL, BO_DB_FULLNAME, from_volbackup, restart_arg->level, restart_arg->newvolpath);
exit:
  if (dir != NULL)
    {
      cfg_free_directory (dir);
    }

  return error_code;
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
  int status, error_code;
  struct tm time_data;
  char *up_to_date;
  char *database_name;
  bool partial_recovery;
  BO_RESTART_ARG restart_arg;

  database_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  up_to_date = utility_get_option_string_value (arg_map, RESTORE_UP_TO_DATE_S, 0);
  partial_recovery = utility_get_option_bool_value (arg_map, RESTORE_PARTIAL_RECOVERY_S);
  restart_arg.printtoc = utility_get_option_bool_value (arg_map, RESTORE_LIST_S);
  restart_arg.stopat = -1;
  restart_arg.backuppath = utility_get_option_string_value (arg_map, RESTORE_BACKUP_FILE_PATH_S, 0);
  restart_arg.level = utility_get_option_int_value (arg_map, RESTORE_LEVEL_S);
  restart_arg.verbose_file = utility_get_option_string_value (arg_map, RESTORE_OUTPUT_FILE_S, 0);
  restart_arg.newvolpath = utility_get_option_bool_value (arg_map, RESTORE_USE_DATABASE_LOCATION_PATH_S);
  restart_arg.restore_upto_bktime = false;
  restart_arg.restore_slave = false;

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
	      PRINT_AND_LOG_ERR_MSG (msgcat_message
				     (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_RESTOREDB, RESTOREDB_MSG_BAD_DATE));
	      goto error_exit;
	    }
	}
    }
  else
    {
      restart_arg.stopat = time (NULL);
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  if (restart_arg.printtoc)
    {
      error_code = print_backup_info (database_name, &restart_arg);
      if (error_code != NO_ERROR)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
	  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_RESTOREDB, RESTOREDB_MSG_FAILURE));
	  goto error_exit;
	}

      return EXIT_SUCCESS;
    }

  sysprm_set_force (prm_get_name (PRM_ID_JAVA_STORED_PROCEDURE), "no");

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("DBA", NULL);
  if (partial_recovery == true)
    {
      log_default_input_for_archive_log_location = 1;
    }
  status = boot_restart_from_backup (true, database_name, &restart_arg);
  if (status == NULL_TRAN_INDEX)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_RESTOREDB, RESTOREDB_MSG_FAILURE));
      goto error_exit;
    }
  else
    {
      boot_shutdown_server (ER_ALL_FINAL);
    }

  return EXIT_SUCCESS;

print_restore_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_RESTOREDB, RESTOREDB_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

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

  src_db_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  dest_db_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 1);
  if (src_db_name == NULL || dest_db_name == NULL)
    {
      goto print_rename_usage;
    }

  ext_path = utility_get_option_string_value (arg_map, RENAME_EXTENTED_VOLUME_PATH_S, 0);
  control_file_name = utility_get_option_string_value (arg_map, RENAME_CONTROL_FILE_S, 0);
  force_delete = utility_get_option_bool_value (arg_map, RENAME_DELETE_BACKUP_S);

  if (utility_get_option_string_table_size (arg_map) != 2)
    {
      goto print_rename_usage;
    }

  if (check_database_name (src_db_name) || check_new_database_name (dest_db_name))
    {
      goto error_exit;
    }
  else if (ext_path && access (ext_path, F_OK) == -1)
    {
      PRINT_AND_LOG_ERR_MSG (msgcat_message
			     (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_RENAMEDB, RENAMEDB_VOLEXT_PATH_INVALID));
      goto error_exit;
    }
  else if (control_file_name && access (control_file_name, F_OK) == -1)
    {
      PRINT_AND_LOG_ERR_MSG (msgcat_message
			     (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_RENAMEDB, RENAMEDB_VOLS_TOFROM_PATHS_FILE_INVALID));
      goto error_exit;
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", src_db_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  /* tuning system parameters */
  sysprm_set_force (prm_get_name (PRM_ID_PB_NBUFFERS), "1024");
  sysprm_set_force (prm_get_name (PRM_ID_JAVA_STORED_PROCEDURE), "no");

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("DBA", NULL);
  if (db_restart (arg->command_name, TRUE, src_db_name) != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }
  else
    {
      if (boot_soft_rename
	  (src_db_name, dest_db_name, NULL, NULL, NULL, ext_path, control_file_name, FALSE, TRUE,
	   force_delete) != NO_ERROR)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
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
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_RENAMEDB, RENAMEDB_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

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
  char lob_path_buf[PATH_MAX];
  const char *server_name;
  const char *db_path;
  const char *log_path;
  const char *db_name;
  DB_INFO *dir = NULL, *db;
  bool cfg_added = false;

  db_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (db_name == NULL)
    {
      goto print_install_usage;
    }

  server_name = utility_get_option_string_value (arg_map, INSTALL_SERVER_NAME_S, 0);
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

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", db_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  db = cfg_find_db (db_name);
  if (db != NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_DATABASE_EXISTS, 1, db_name);
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      cfg_free_directory (db);
      goto error_exit;
    }

  /* have to add it to the config before calling boot_restart */
  if (cfg_read_directory (&dir, 1) != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }

  db = cfg_add_db (&dir, db_name, db_path, log_path, NULL, server_name);
  if (db == NULL)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }
  if (db->lobpath == NULL)
    {
      /* assign the data volume directory */
      snprintf (lob_path_buf, sizeof (lob_path_buf), "%s%s%clob", LOB_PATH_DEFAULT_PREFIX, db->pathname,
		PATH_SEPARATOR);
      lob_path_buf[PATH_MAX - 1] = '\0';
      db->lobpath = strdup (lob_path_buf);
    }

  cfg_write_directory (dir);

  cfg_added = true;

  sysprm_set_force (prm_get_name (PRM_ID_JAVA_STORED_PROCEDURE), "no");

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("DBA", NULL);
  if (db_restart (arg->command_name, TRUE, db_name) != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }

  if (boot_soft_rename (NULL, db_name, db_path, log_path, server_name, NULL, NULL, FALSE, FALSE, TRUE) != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      db_shutdown ();
      goto error_exit;
    }

  db_commit_transaction ();
  db_shutdown ();

  cfg_free_directory (dir);
  return EXIT_SUCCESS;

print_install_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_INSTALLDB, INSTALLDB_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

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
  const char *lob_path;
  char lob_pathbuf[PATH_MAX];
  const char *ext_path;
  const char *control_file_name;
  bool overwrite, delete_src, copy_lob_path;

  src_db_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  dest_db_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 1);
  if (src_db_name == NULL || dest_db_name == NULL)
    {
      goto print_copy_usage;
    }

  server_name = utility_get_option_string_value (arg_map, COPY_SERVER_NAME_S, 0);
  db_path = utility_get_option_string_value (arg_map, COPY_FILE_PATH_S, 0);
  log_path = utility_get_option_string_value (arg_map, COPY_LOG_PATH_S, 0);
  lob_path = utility_get_option_string_value (arg_map, COPY_LOB_PATH_S, 0);
  ext_path = utility_get_option_string_value (arg_map, COPY_EXTENTED_VOLUME_PATH_S, 0);
  control_file_name = utility_get_option_string_value (arg_map, COPY_CONTROL_FILE_S, 0);
  overwrite = utility_get_option_bool_value (arg_map, COPY_REPLACE_S);
  delete_src = utility_get_option_bool_value (arg_map, COPY_DELETE_SOURCE_S);
  copy_lob_path = utility_get_option_bool_value (arg_map, COPY_COPY_LOB_PATH_S);

  if (utility_get_option_string_table_size (arg_map) != 2)
    {
      goto print_copy_usage;
    }

  if (lob_path != NULL && copy_lob_path == true)
    {
      goto print_copy_usage;
    }
  if (delete_src == true && lob_path == NULL)
    {
      copy_lob_path = true;
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", src_db_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  if (check_database_name (src_db_name) || check_new_database_name (dest_db_name))
    {
      goto error_exit;
    }

  if (strcmp (src_db_name, dest_db_name) == 0)
    {
      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_COPYDB, COPYDB_MSG_IDENTICAL));
      goto error_exit;
    }

  /* tuning system parameters */
  sysprm_set_force (prm_get_name (PRM_ID_PB_NBUFFERS), "1024");
  sysprm_set_force (prm_get_name (PRM_ID_JAVA_STORED_PROCEDURE), "no");

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("DBA", NULL);

  if (db_restart (arg->command_name, TRUE, src_db_name) != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }

  if (copy_lob_path)
    {
      const char *s = boot_get_lob_path ();
      if (*s != '\0')
	{
	  lob_path = strcpy (lob_pathbuf, s);
	}
    }
  if (boot_copy
      (src_db_name, dest_db_name, db_path, log_path, lob_path, server_name, ext_path, control_file_name,
       overwrite) != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }
  if (delete_src)
    {
      boot_delete (src_db_name, true);
    }
  er_final (ER_ALL_FINAL);

  return EXIT_SUCCESS;

print_copy_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_COPYDB, COPYDB_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

error_exit:
  er_final (ER_ALL_FINAL);

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
  if (db_name == NULL)
    {
      goto print_optimize_usage;
    }

  class_name = utility_get_option_string_value (arg_map, OPTIMIZE_CLASS_NAME_S, 0);

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
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", db_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  sysprm_set_force (prm_get_name (PRM_ID_JAVA_STORED_PROCEDURE), "no");

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("DBA", NULL);
  if (db_restart (arg->command_name, TRUE, db_name) != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }

  if (class_name != NULL && class_name[0] != 0)
    {
      if ((class_mop = db_find_class (class_name)) == NULL
	  || sm_update_statistics (class_mop, STATS_WITH_SAMPLING) != NO_ERROR)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
	  db_shutdown ();
	  goto error_exit;
	}
    }
  else
    {
      if (sm_update_all_statistics (STATS_WITH_SAMPLING) != NO_ERROR)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
	  db_shutdown ();
	  goto error_exit;
	}
    }
  db_commit_transaction ();
  db_shutdown ();

  return EXIT_SUCCESS;

print_optimize_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_OPTIMIZEDB, OPTIMIZEDB_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

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
  DIAGDUMP_LOG,
  DIAGDUMP_HEAP,
  DIAGDUMP_END_OF_OPTION
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
  const char *output_file = NULL;
  FILE *outfp = NULL;
  bool is_emergency = false;
  DIAGDUMP_TYPE diag;

  db_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (db_name == NULL)
    {
      goto print_diag_usage;
    }

  is_emergency = utility_get_option_bool_value (arg_map, DIAG_EMERGENCY_S);
  if (is_emergency)
    {
      sysprm_set_force (prm_get_name (PRM_ID_DISABLE_VACUUM), "yes");
      sysprm_set_force (prm_get_name (PRM_ID_FORCE_RESTART_TO_SKIP_RECOVERY), "yes");
    }

  output_file = utility_get_option_string_value (arg_map, DIAG_OUTPUT_FILE_S, 0);
  if (output_file == NULL)
    {
      outfp = stdout;
    }
  else
    {
      outfp = fopen (output_file, "w");
      if (outfp == NULL)
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_DIAGDB, DIAGDB_MSG_BAD_OUTPUT),
				 output_file);
	  goto error_exit;
	}
    }

  diag = utility_get_option_int_value (arg_map, DIAG_DUMP_TYPE_S);

  if (diag != DIAGDUMP_LOG && utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_diag_usage;
    }

  if (check_database_name (db_name))
    {
      goto error_exit;
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", db_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  sysprm_set_force (prm_get_name (PRM_ID_JAVA_STORED_PROCEDURE), "no");

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("DBA", NULL);
  if (db_restart (arg->command_name, TRUE, db_name) != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }

  if (diag < DIAGDUMP_ALL || diag >= DIAGDUMP_END_OF_OPTION)
    {
      goto print_diag_usage;
    }

  if (diag == DIAGDUMP_ALL || diag == DIAGDUMP_FILE_TABLES)
    {
      /* this dumps the allocated file stats */
      fprintf (outfp, "\n*** DUMP OF FILE STATISTICS ***\n");
      (void) file_tracker_dump (NULL, outfp);
    }

  if (diag == DIAGDUMP_ALL || diag == DIAGDUMP_FILE_CAPACITIES)
    {
      /* this dumps the allocated file stats */
      fprintf (outfp, "\n*** DUMP OF FILE DESCRIPTIONS ***\n");
      (void) file_tracker_dump_all_capacities (NULL, outfp);
    }

  if (diag == DIAGDUMP_ALL || diag == DIAGDUMP_HEAP_CAPACITIES)
    {
      /* this dumps lower level info about capacity of all heaps */
      fprintf (outfp, "\n*** DUMP CAPACITY OF ALL HEAPS ***\n");
      (void) file_tracker_dump_all_heap_capacities (NULL, outfp);
    }

  if (diag == DIAGDUMP_ALL || diag == DIAGDUMP_INDEX_CAPACITIES)
    {
      /* this dumps lower level info about capacity of all indices */
      fprintf (outfp, "\n*** DUMP CAPACITY OF ALL INDICES ***\n");
      (void) file_tracker_dump_all_btree_capacities (NULL, outfp);
    }

  if (diag == DIAGDUMP_ALL || diag == DIAGDUMP_CLASSNAMES)
    {
      /* this dumps the known classnames */
      fprintf (outfp, "\n*** DUMP CLASSNAMES ***\n");
      locator_dump_class_names (NULL, outfp);
    }

  if (diag == DIAGDUMP_ALL || diag == DIAGDUMP_DISK_BITMAPS)
    {
      /* this dumps lower level info about the disk */
      fprintf (outfp, "\n*** DUMP OF DISK STATISTICS ***\n");
      disk_dump_all (NULL, outfp);
    }

  if (diag == DIAGDUMP_ALL || diag == DIAGDUMP_CATALOG)
    {
      /* this dumps the content of catalog */
      fprintf (outfp, "\n*** DUMP OF CATALOG ***\n");
      catalog_dump (NULL, outfp, 1);
    }

  if (diag == DIAGDUMP_ALL || diag == DIAGDUMP_LOG)
    {
      /* this dumps the content of log */
      int isforward, dump_npages, desired_tranid;
      LOG_PAGEID start_logpageid;
      long long int s;

      if (diag == DIAGDUMP_ALL || utility_get_option_string_table_size (arg_map) == 1)
	{
	  char yn[2];
	  do
	    {
	      printf ("\n");
	      printf ("isforward (1 or 0) ? ");
	      scanf ("%d", &isforward);
	      printf ("start_logpageid (-1 for the first/last page) ? ");
	      scanf ("%lld", &s);
	      start_logpageid = s;
	      printf ("dump_npages (-1 for all pages) ? ");
	      scanf ("%d", &dump_npages);
	      printf ("desired_tranid (-1 for all transactions) ? ");
	      scanf ("%d", &desired_tranid);
	      printf ("log_dump(%d, %lld, %d, %d) (y/n) ? ", isforward, (long long int) start_logpageid, dump_npages,
		      desired_tranid);
	      scanf ("%1s", yn);
	    }
	  while (yn[0] != 'y');
	}
      else if (utility_get_option_string_table_size (arg_map) == 5)
	{
	  const char *cp;
	  start_logpageid = isforward = dump_npages = desired_tranid = 0;

	  cp = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 1);
	  if (cp != NULL)
	    {
	      isforward = atoi (cp);
	    }

	  cp = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 2);
	  if (cp != NULL)
	    {
	      start_logpageid = atoll (cp);
	    }

	  cp = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 3);
	  if (cp != NULL)
	    {
	      dump_npages = atoi (cp);
	    }

	  cp = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 4);
	  if (cp != NULL)
	    {
	      desired_tranid = atoi (cp);
	    }
	}
      else
	{
	  goto print_diag_usage;
	}
      fprintf (outfp, "\n*** DUMP OF LOG ***\n");
      xlog_dump (NULL, outfp, isforward, start_logpageid, dump_npages, desired_tranid);
    }

  if (diag == DIAGDUMP_ALL || diag == DIAGDUMP_HEAP)
    {
      bool dump_records;
      /* this dumps the contents of all heaps */
      dump_records = utility_get_option_bool_value (arg_map, DIAG_DUMP_RECORDS_S);
      fprintf (outfp, "\n*** DUMP OF ALL HEAPS ***\n");
      (void) file_tracker_dump_all_heap (NULL, outfp, dump_records);
    }

  db_shutdown ();

  fflush (outfp);
  if (output_file != NULL && outfp != NULL && outfp != stdout)
    {
      fclose (outfp);
    }

  return EXIT_SUCCESS;

print_diag_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_DIAGDB, DIAGDB_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

error_exit:
  if (output_file != NULL && outfp != NULL && outfp != stdout)
    {
      fclose (outfp);
    }

  return EXIT_FAILURE;
}

/*
 * patchdb() - patchdb main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
patchdb (UTIL_FUNCTION_ARG * arg)
{
  char er_msg_file[PATH_MAX];
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  const char *db_name;
  const char *db_locale = NULL;
  bool recreate_log;

  db_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (db_name == NULL)
    {
      goto print_patch_usage;
    }

  recreate_log = utility_get_option_bool_value (arg_map, PATCH_RECREATE_LOG_S);

  if (recreate_log == true)
    {
      db_locale = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 1);
      if (db_locale == NULL)
	{
	  goto print_patch_usage;
	}
    }

  if (utility_get_option_string_table_size (arg_map) != (recreate_log ? 2 : 1))
    {
      goto print_patch_usage;
    }

  if (check_database_name (db_name))
    {
      goto error_exit;
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", db_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  if (boot_emergency_patch (db_name, recreate_log, 0, db_locale, NULL) != NO_ERROR)
    {
      fprintf (stderr, "emergency patch fail:%s\n", db_error_string (3));
      er_final (ER_ALL_FINAL);
      goto error_exit;
    }

  return EXIT_SUCCESS;

print_patch_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_PATCHDB, PATCHDB_MSG_USAGE),
	   basename (arg->argv0));
error_exit:
  return EXIT_FAILURE;
}

#if defined(ENABLE_UNUSED_FUNCTION )
/*
 * estimatedb_data() - estimatedb_data main routine
 *   return: EXIT_SUCCES/EXIT_FAILURE
 */
int
estimatedb_data (UTIL_FUNCTION_ARG * arg)
{
  /* todo: remove me */
}
#endif /* ENABLE_UNUSED_FUNCTION */

#if defined(ENABLE_UNUSED_FUNCTION )
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

  num_instance = atoi (utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0));
  num_diffkeys = atoi (utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 1));
  avg_key_size = atoi (utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 2));
  key_type = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 3);

  sysprm_load_and_init (NULL, NULL);
  (void) db_set_page_size (IO_DEFAULT_PAGE_SIZE, IO_DEFAULT_PAGE_SIZE);
  if (num_instance <= num_diffkeys)
    {
      num_instance = num_diffkeys;
    }
  /* Initialize domain area */
  tp_init ();

  /* Initialize tsc-timer */
  tsc_init ();

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
		      /* Do not override any information in Avg_key_size with precision information. Just make sure the 
		       * input makes sense for these cases.  */
		      if (avg_key_size > domain->precision)
			{
			  /* Does not make sense to have avg_key_size bigger than precision - inform user of error.
			   * This is illegal input since the char precision defaults to 1. estimatedb_index 100000 1000 
			   * 5 char */
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
		       * Avg_key_size and TP_DOMAIN_TYPE(domain) to really
		       * compute the correct average key size that we
		       * need to estimate the total number of pages.
		       */
		      npages =
			btree_estimate_total_numpages (NULL, num_diffkeys, avg_key_size, num_instance, &blt_npages,
						       &blt_wrs_npages);

		      fprintf (stdout,
			       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_ESTIMATEDB_INDEX,
					       ESTIMATEDB_INDEX_MSG_INPUT));
		      fprintf (stdout,
			       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_ESTIMATEDB_INDEX,
					       ESTIMATEDB_INDEX_MSG_INSTANCES), num_instance);
		      fprintf (stdout,
			       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_ESTIMATEDB_INDEX,
					       ESTIMATEDB_INDEX_MSG_NUMBER_KEYS), num_diffkeys);
		      fprintf (stdout,
			       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_ESTIMATEDB_INDEX,
					       ESTIMATEDB_INDEX_MSG_AVG_KEYSIZE), avg_key_size);
		      fprintf (stdout,
			       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_ESTIMATEDB_INDEX,
					       ESTIMATEDB_INDEX_MSG_KEYTYPE), key_type);
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
      fprintf (stderr,
	       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_ESTIMATEDB_INDEX, ESTIMATEDB_INDEX_BAD_KEYTYPE),
	       key_type);
      break;
    case -2:
      fprintf (stderr,
	       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_ESTIMATEDB_INDEX, ESTIMATEDB_INDEX_BAD_KEYLENGTH),
	       avg_key_size, domain->precision);
      break;
    case 1:
      fprintf (stderr,
	       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_ESTIMATEDB_INDEX, ESTIMATEDB_INDEX_BAD_ARGUMENTS));
      break;
    default:
      fprintf (stdout,
	       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_ESTIMATEDB_INDEX, ESTIMATEDB_INDEX_MSG_NPAGES),
	       npages);
      fprintf (stdout,
	       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_ESTIMATEDB_INDEX, ESTIMATEDB_INDEX_MSG_BLT_NPAGES),
	       blt_npages);
      fprintf (stdout,
	       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_ESTIMATEDB_INDEX,
			       ESTIMATEDB_INDEX_MSG_BLT_WRS_NPAGES), blt_wrs_npages);
      break;
    }
  return status;

print_estimate_index_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_ESTIMATEDB_INDEX, ESTIMATEDB_INDEX_MSG_USAGE),
	   basename (arg->argv0));
  return EXIT_FAILURE;
}
#endif /* ENABLE_UNUSED_FUNCTION */

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
  int num_hosts;

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_alterdbhost_usage;
    }

  db_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (db_name == NULL)
    {
      goto print_alterdbhost_usage;
    }

  host_name = utility_get_option_string_value (arg_map, ALTERDBHOST_HOST_S, 0);
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
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_UNABLE_TO_FIND_HOSTNAME, 0);
	  goto error;
	}
#else
      strcpy (host_name_buf, "localhost");
#endif
      host_name = host_name_buf;
    }

  er_init (NULL, ER_NEVER_EXIT);

  /* get the database directory information in write mode */
  if (cfg_maycreate_get_directory_filename (dbtxt_label) == NULL
#if !defined(WINDOWS) || !defined(DONT_USE_MANDATORY_LOCK_IN_WINDOWS)
/* Temporary fix for NT file locking problem */
      || (dbtxt_vdes = fileio_mount (NULL, dbtxt_label, dbtxt_label, LOG_DBTXT_VOLID, 2, true)) == NULL_VOLDES
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
  db = cfg_find_db_list (dir, db_name);
  if (db == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_UNKNOWN_DATABASE, 1, db_name);
      goto error;
    }

  /* Compose the full name of the database and find location of logs */
  log_prefix = fileio_get_base_file_name (db_name);
  COMPOSE_FULL_NAME (BO_DB_FULLNAME, sizeof (BO_DB_FULLNAME), db->pathname, db_name);

  /* System is not restarted. Read the header from disk */
  logpb_initialize_log_names (NULL, BO_DB_FULLNAME, db->logpath, log_prefix);

  /* Avoid setting errors at this moment related to existance of files. */
  if (fileio_is_volume_exist (log_Name_active) == false)
    {
      goto error;
    }

  if ((log_vdes =
       fileio_mount (NULL, BO_DB_FULLNAME, log_Name_active, LOG_DBLOG_ACTIVE_VOLID, true, false)) == NULL_VOLDES)
    {
      goto error;
    }

  if (db->hosts != NULL)
    {
      cfg_free_hosts (db->hosts);
    }
  db->hosts = cfg_get_hosts (host_name, &num_hosts, false);

  /* Dismount lgat */
  fileio_dismount (NULL, log_vdes);

#if defined(WINDOWS) && !defined(DONT_USE_MANDATORY_LOCK_IN_WINDOWS)
  /* must unlock this before we can open it again for writing */
  if (dbtxt_vdes != NULL_VOLDES)
    {
      fileio_dismount (NULL, dbtxt_vdes);
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
      fileio_dismount (NULL, dbtxt_vdes);
    }

  return EXIT_SUCCESS;

error:
  PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));

  /* Deallocate any allocated structures */
  if (dir != NULL)
    {
      cfg_free_directory (dir);
    }

  if (dbtxt_vdes != NULL_VOLDES)
    {
      fileio_dismount (NULL, dbtxt_vdes);
    }

  return EXIT_FAILURE;

print_alterdbhost_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_ALTERDBHOST, ALTERDBHOST_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

error_exit:
  return EXIT_FAILURE;
}

/*
 * genlocale() - generate locales binary files
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
genlocale (UTIL_FUNCTION_ARG * arg)
{
  char *locale_str = NULL;
  char *input_path = NULL;
  bool is_scan_locales = false;
  bool is_verbose = false;
  LOCALE_FILE *lf = NULL;
  LOCALE_FILE *curr_lf = NULL;
  int count_loc = 0, i;
  int start_lf_pos = -1;
  int end_lf_pos = -1;
  char er_msg_file[PATH_MAX];
  int str_count = 0;
  UTIL_ARG_MAP *arg_map = NULL;
  LOCALE_DATA **ld = NULL;

  int err_status = EXIT_SUCCESS;

  assert (arg != NULL);

  arg_map = arg->arg_map;

  if (!arg->valid_arg)
    {
      goto print_genlocale_usage;
    }

  str_count = utility_get_option_string_table_size (arg_map);
  if (str_count == 0)
    {
      is_scan_locales = true;
      locale_str = NULL;
    }
  else if (str_count == 1)
    {
      locale_str = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
      if (locale_str == NULL)
	{
	  goto print_genlocale_usage;
	}
    }
  else
    {
      goto print_genlocale_usage;
    }

  is_verbose = utility_get_option_bool_value (arg_map, GENLOCALE_VERBOSE_S);

  input_path = utility_get_option_string_value (arg_map, GENLOCALE_INPUT_PATH_S, 0);
  if (input_path != NULL && is_scan_locales)
    {
      goto print_genlocale_usage;
    }

  /* initialization of language module for built-in locales and collations, we don't care about environment here */
  lang_init_builtin ();

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s.err", arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  if (locale_get_cfg_locales (&lf, &count_loc, false) != NO_ERROR)
    {
      err_status = EXIT_FAILURE;
      goto exit;
    }

  if (is_scan_locales)
    {
      if (is_verbose)
	{
	  printf ("\n\nFound %d locale files\n\n", count_loc);
	}
      start_lf_pos = 0;
      end_lf_pos = count_loc;
    }
  else
    {
      assert (locale_str != NULL);

      for (i = 0; i < count_loc; i++)
	{
	  if (strcmp (locale_str, lf[i].locale_name) == 0)
	    {
	      curr_lf = &(lf[i]);
	      start_lf_pos = i;
	      end_lf_pos = i + 1;
	      break;
	    }
	}

      if (curr_lf == NULL)
	{
	  LOG_LOCALE_ERROR (msgcat_message
			    (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_GENLOCALE, GENLOCALE_MSG_INVALID_LOCALE), ER_LOC_GEN,
			    true);
	  err_status = EXIT_FAILURE;
	  goto exit;
	}

      if (input_path != NULL)
	{
	  if (curr_lf->ldml_file != NULL)
	    {
	      free (curr_lf->ldml_file);
	    }

	  curr_lf->ldml_file = strdup (input_path);
	  if (curr_lf->ldml_file == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) strlen (input_path));
	      err_status = EXIT_FAILURE;
	      goto exit;
	    }
	}
    }

  ld = (LOCALE_DATA **) malloc (count_loc * sizeof (LOCALE_DATA *));
  if (ld == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) strlen (input_path));
      err_status = EXIT_FAILURE;
      goto exit;
    }
  memset (ld, 0, count_loc * sizeof (LOCALE_DATA *));


  for (i = start_lf_pos; i < end_lf_pos; i++)
    {
      ld[i] = (LOCALE_DATA *) malloc (sizeof (LOCALE_DATA));
      if (ld[i] == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) strlen (input_path));
	  err_status = EXIT_FAILURE;
	  goto exit;
	}
      memset (ld[i], 0, sizeof (LOCALE_DATA));
    }

  for (i = start_lf_pos; i < end_lf_pos; i++)
    {
      if (locale_check_and_set_default_files (&(lf[i]), false) != NO_ERROR)
	{
	  err_status = EXIT_FAILURE;
	  goto exit;
	}

      if (is_verbose)
	{
	  printf ("*********************************\n");
	  printf ("Compile locale:\n");
	  printf ("Locale string: %s\n", lf[i].locale_name);
	  printf ("Input LDML: %s\n", lf[i].ldml_file);
	  printf ("Output Library: %s\n", lf[i].lib_file);
	}

      if (locale_compile_locale (&(lf[i]), ld[i], is_verbose) != NO_ERROR)
	{
	  err_status = EXIT_FAILURE;
	  goto exit;
	}

      if (!is_scan_locales)
	{
	  break;
	}
    }

  locale_mark_duplicate_collations (ld, start_lf_pos, end_lf_pos, is_verbose);

  if (locale_prepare_C_file () != NO_ERROR || locale_save_all_to_C_file (ld, start_lf_pos, end_lf_pos, lf) != NO_ERROR)
    {
      err_status = EXIT_FAILURE;
      goto exit;
    }

exit:

  /* Deallocate any allocated structures */
  for (i = 0; i < count_loc; i++)
    {
      if (lf[i].locale_name != NULL)
	{
	  free_and_init (lf[i].locale_name);
	}
      if (lf[i].ldml_file != NULL)
	{
	  free_and_init (lf[i].ldml_file);
	}
      if (lf[i].lib_file != NULL)
	{
	  free_and_init (lf[i].lib_file);
	}
      if (ld != NULL && ld[i] != NULL)
	{
	  locale_destroy_data (ld[i]);
	  free (ld[i]);
	}
    }

  if (ld != NULL)
    {
      free (ld);
    }

  locale_free_shared_data ();

  assert (lf != NULL);
  free_and_init (lf);

  if (err_status != EXIT_SUCCESS && er_errid () != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
    }

  return err_status;

print_genlocale_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_GENLOCALE, GENLOCALE_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

  return EXIT_FAILURE;
}

/*
 * dumplocale() - generate locales binary files
 *   return: error status
 */
int
dumplocale (UTIL_FUNCTION_ARG * arg)
{
  char *locale_str = NULL;
  char *input_path = NULL;
  char *alphabet_type = NULL;
  LANG_LOCALE_DATA lld;
  void *loclib_handle = NULL;
  bool is_scan_locales = false;
  LOCALE_FILE *lf = NULL;
  LOCALE_FILE lf_one;
  int dl_settings = 0;
  int str_count, i, count_loc, loc_index;
  int start_value = 0;
  int end_value = 0;
  UTIL_ARG_MAP *arg_map = NULL;
  int err_status = NO_ERROR;

  assert (arg != NULL);
  arg_map = arg->arg_map;

  if (!arg->valid_arg)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
      goto print_dumplocale_usage;
    }

  str_count = utility_get_option_string_table_size (arg_map);

  locale_str = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);

  input_path = utility_get_option_string_value (arg_map, DUMPLOCALE_INPUT_PATH_S, 0);

  if (utility_get_option_bool_value (arg_map, DUMPLOCALE_CALENDAR_S))
    {
      dl_settings |= DUMPLOCALE_IS_CALENDAR;
    }

  if (utility_get_option_bool_value (arg_map, DUMPLOCALE_NUMBERING_S))
    {
      dl_settings |= DUMPLOCALE_IS_NUMBERING;
    }

  alphabet_type = utility_get_option_string_value (arg_map, DUMPLOCALE_ALPHABET_S, 0);
  if (alphabet_type != NULL)
    {
      if (strcmp (alphabet_type, DUMPLOCALE_ALPHABET_LOWER_S) == 0
	  || strcmp (alphabet_type, DUMPLOCALE_ALPHABET_LOWER_L) == 0)
	{
	  dl_settings |= DUMPLOCALE_IS_ALPHABET;
	  dl_settings |= DUMPLOCALE_IS_ALPHABET_LOWER;
	  dl_settings &= ~DUMPLOCALE_IS_ALPHABET_UPPER;
	}
      else if (strcmp (alphabet_type, DUMPLOCALE_ALPHABET_UPPER_S) == 0
	       || strcmp (alphabet_type, DUMPLOCALE_ALPHABET_UPPER_L) == 0)
	{
	  dl_settings |= DUMPLOCALE_IS_ALPHABET;
	  dl_settings &= ~DUMPLOCALE_IS_ALPHABET_LOWER;
	  dl_settings |= DUMPLOCALE_IS_ALPHABET_UPPER;
	}
      else if (strcmp (alphabet_type, DUMPLOCALE_ALPHABET_ALL_CASING) == 0)
	{
	  dl_settings |= DUMPLOCALE_IS_ALPHABET;
	  dl_settings |= DUMPLOCALE_IS_ALPHABET_LOWER;
	  dl_settings |= DUMPLOCALE_IS_ALPHABET_UPPER;
	}
    }

  alphabet_type = NULL;
  /* check if --identifier-alphabet is set. */
  alphabet_type = utility_get_option_string_value (arg_map, DUMPLOCALE_IDENTIFIER_ALPHABET_S, 0);
  if (alphabet_type != NULL)
    {
      if (strcmp (alphabet_type, DUMPLOCALE_ALPHABET_LOWER_S) == 0
	  || strcmp (alphabet_type, DUMPLOCALE_ALPHABET_LOWER_L) == 0)
	{
	  dl_settings |= DUMPLOCALE_IS_IDENTIFIER_ALPHABET;
	  dl_settings |= DUMPLOCALE_IS_IDENTIFIER_ALPHABET_LOWER;
	  dl_settings &= ~DUMPLOCALE_IS_IDENTIFIER_ALPHABET_UPPER;
	}
      else if (strcmp (alphabet_type, DUMPLOCALE_ALPHABET_UPPER_S) == 0
	       || strcmp (alphabet_type, DUMPLOCALE_ALPHABET_UPPER_L) == 0)
	{
	  dl_settings |= DUMPLOCALE_IS_IDENTIFIER_ALPHABET;
	  dl_settings &= ~DUMPLOCALE_IS_IDENTIFIER_ALPHABET_LOWER;
	  dl_settings |= DUMPLOCALE_IS_IDENTIFIER_ALPHABET_UPPER;
	}
      else if (strcmp (alphabet_type, DUMPLOCALE_ALPHABET_ALL_CASING) == 0)
	{
	  dl_settings |= DUMPLOCALE_IS_IDENTIFIER_ALPHABET;
	  dl_settings |= DUMPLOCALE_IS_IDENTIFIER_ALPHABET_LOWER;
	  dl_settings |= DUMPLOCALE_IS_IDENTIFIER_ALPHABET_UPPER;
	}
    }

  if (utility_get_option_bool_value (arg_map, DUMPLOCALE_COLLATION_S))
    {
      dl_settings |= DUMPLOCALE_IS_COLLATION_CP_ORDER;
    }

  if (utility_get_option_bool_value (arg_map, DUMPLOCALE_WEIGHT_ORDER_S))
    {
      dl_settings |= DUMPLOCALE_IS_COLLATION_WEIGHT_ORDER;
    }
  start_value = utility_get_option_int_value (arg_map, DUMPLOCALE_START_VALUE_S);
  end_value = utility_get_option_int_value (arg_map, DUMPLOCALE_END_VALUE_S);

  if (utility_get_option_bool_value (arg_map, DUMPLOCALE_NORMALIZATION_S))
    {
      dl_settings |= DUMPLOCALE_IS_NORMALIZATION;
    }

  if (utility_get_option_bool_value (arg_map, DUMPLOCALE_CONSOLE_CONV_S))
    {
      dl_settings |= DUMPLOCALE_IS_TEXT_CONV;
    }

  /* Check command line arguments for incompatibilities. */
  if (locale_str != NULL && input_path != NULL)
    {
      err_status = ER_LOC_GEN;
      LOG_LOCALE_ERROR (msgcat_message
			(MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_DUMPLOCALE, DUMPLOCALE_MSG_INCOMPAT_INPUT_SEL),
			ER_LOC_GEN, true);
      goto print_dumplocale_usage;
    }
  if (start_value > end_value)
    {
      err_status = ER_LOC_GEN;
      LOG_LOCALE_ERROR (msgcat_message
			(MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_DUMPLOCALE, DUMPLOCALE_MSG_INVALID_CP_RANGE), ER_LOC_GEN,
			true);
      goto print_dumplocale_usage;
    }

  /* Start the dumping process. */
  /* Prepare the locale file(s) to be dumped. */
  memset (&lf_one, 0, sizeof (LOCALE_FILE));
  memset (&lld, 0, sizeof (LANG_LOCALE_DATA));

  if (locale_str != NULL)
    {
      /* Find binary file corresponding to the selected locale. */
      count_loc = 1;
      lf = &lf_one;
      if (locale_str == NULL || strlen (locale_str) > LOC_LOCALE_STR_SIZE)
	{
	  err_status = ER_LOC_INIT;
	  LOG_LOCALE_ERROR (msgcat_message
			    (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_DUMPLOCALE, DUMPLOCALE_MSG_INVALID_LOCALE),
			    err_status, true);
	  goto error;
	}
      lf->locale_name = malloc (strlen (locale_str) + 1);
      if (lf->locale_name == NULL)
	{
	  err_status = ER_LOC_INIT;
	  LOG_LOCALE_ERROR ("memory allocation failed", err_status, true);
	  goto error;
	}
      strcpy (lf->locale_name, locale_str);
      err_status = locale_check_and_set_default_files (lf, true);
    }
  else
    {
      /* Parse cubrid_locales.txt and select all locale files. */
      err_status = locale_get_cfg_locales (&lf, &count_loc, false);
    }

  if (err_status != NO_ERROR)
    {
      goto error;
    }

  if (input_path != NULL)
    {
      /* set lib file in all locales */
      for (i = 0; i < count_loc; i++)
	{
	  if (lf[i].lib_file != NULL)
	    {
	      free_and_init (lf[i].lib_file);
	    }

	  lf[i].lib_file = strdup (input_path);
	  if (lf[i].lib_file == NULL)
	    {
	      err_status = ER_LOC_INIT;
	      LOG_LOCALE_ERROR ("memory allocation failed", err_status, true);
	      goto error;
	    }
	}
    }

  /* Do the actual dumping. */
  for (loc_index = 0; loc_index < count_loc; loc_index++)
    {
      err_status = locale_check_and_set_default_files (&(lf[loc_index]), true);
      if (err_status != NO_ERROR)
	{
	  goto error;
	}

      memset (&lld, 0, sizeof (LANG_LOCALE_DATA));

      err_status = lang_load_library (lf[loc_index].lib_file, &loclib_handle);

      if (err_status != NO_ERROR)
	{
	  goto error;
	}

      err_status = lang_locale_data_load_from_lib (&lld, loclib_handle, &(lf[loc_index]), true);
      if (err_status != NO_ERROR)
	{
	  goto error;
	}
      err_status = locale_dump (&lld, &(lf[loc_index]), dl_settings, start_value, end_value);
      if (err_status != NO_ERROR)
	{
	  goto error;
	}

      if (((dl_settings & DUMPLOCALE_IS_COLLATION_CP_ORDER) != 0)
	  || ((dl_settings & DUMPLOCALE_IS_COLLATION_WEIGHT_ORDER) != 0))
	{
	  err_status =
	    locale_dump_lib_collations (loclib_handle, &(lf[loc_index]), dl_settings, start_value, end_value);

	  if (err_status != NO_ERROR)
	    {
	      goto error;
	    }
	}

      if (lld.txt_conv != NULL && lld.txt_conv->init_conv_func == NULL)
	{
	  free (lld.txt_conv);
	  lld.txt_conv = NULL;
	}

#if defined(WINDOWS)
      FreeLibrary (loclib_handle);
#else
      dlclose (loclib_handle);
#endif
    }

error:
  for (i = 0; i < count_loc; i++)
    {
      if (lf[i].lib_file != NULL)
	{
	  free (lf[i].lib_file);
	}
      if (lf[i].ldml_file != NULL)
	{
	  free (lf[i].ldml_file);
	}
      if (lf[i].locale_name != NULL)
	{
	  free (lf[i].locale_name);
	}
    }

  if (lf != &lf_one)
    {
      free (lf);
      lf = NULL;
    }

  /* 
   * Text conversions having init_conv_func not NULL are built-in. 
   * They can't be deallocates, as they are static constants.
   */
  if (lld.txt_conv != NULL && lld.txt_conv->init_conv_func == NULL)
    {
      free (lld.txt_conv);
      lld.txt_conv = NULL;
    }

  return err_status;

print_dumplocale_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_DUMPLOCALE, DUMPLOCALE_MSG_USAGE),
	   basename (arg->argv0));

  return EXIT_FAILURE;
}

/*
 * synccolldb() - sync_collations main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
synccolldb (UTIL_FUNCTION_ARG * arg)
{
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *db_name;
  int status = EXIT_SUCCESS;
  bool is_check, is_force, is_sync = true;
  int db_obs_coll_cnt = 0;
  int new_sys_coll_cnt = 0;

  db_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (db_name == NULL)
    {
      goto print_sync_collations_usage;
    }

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_sync_collations_usage;
    }

  is_check = utility_get_option_bool_value (arg_map, SYNCCOLL_CHECK_S);
  is_force = utility_get_option_bool_value (arg_map, SYNCCOLL_FORCESYNC_S);

  if (is_check && is_force)
    {
      goto print_sync_collations_usage;
    }

  if (is_check || is_force)
    {
      is_sync = false;
    }

  if (check_database_name (db_name))
    {
      goto error_exit;
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", db_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  sysprm_set_force (prm_get_name (PRM_ID_JAVA_STORED_PROCEDURE), "no");

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("DBA", NULL);
  if (db_restart (arg->command_name, TRUE, db_name) != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }

  if (is_check || is_sync)
    {
      status = synccoll_check (db_name, &db_obs_coll_cnt, &new_sys_coll_cnt);
      if (status != EXIT_SUCCESS)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
	  goto exit;
	}

      if (!is_sync && (db_obs_coll_cnt != 0 || new_sys_coll_cnt != 0))
	{
	  /* return error if synchronization is required */
	  status = EXIT_FAILURE;
	  goto exit;
	}
    }

  if (is_sync || is_force)
    {
      char yn[2];

      if (!is_force && db_obs_coll_cnt == 0 && new_sys_coll_cnt == 0)
	{
	  /* message SYNCCOLLDB_MSG_SYNC_NOT_NEEDED displayed in 'synccoll_check' */
	  goto exit;
	}

      if (!is_force)
	{
	  fprintf (stdout,
		   msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SYNCCOLLDB, SYNCCOLLDB_MSG_SYNC_CONTINUE));
	  scanf ("%1s", yn);
	  if (yn[0] != 'y')
	    {
	      PRINT_AND_LOG_ERR_MSG (msgcat_message
				     (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SYNCCOLLDB, SYNCCOLLDB_MSG_SYNC_ABORT));
	      goto exit;
	    }
	}

      status = synccoll_force ();
      if (status != EXIT_SUCCESS)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
	  db_abort_transaction ();
	  fprintf (stdout,
		   msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SYNCCOLLDB, SYNCCOLLDB_MSG_SYNC_ABORT));
	  goto exit;
	}
      else
	{
	  db_commit_transaction ();
	  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SYNCCOLLDB, SYNCCOLLDB_MSG_SYNC_OK),
		   CT_COLLATION_NAME);
	}
    }

exit:
  db_shutdown ();

  return status;

print_sync_collations_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SYNCCOLLDB, SYNCCOLLDB_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

error_exit:
  return EXIT_FAILURE;
}

/*
 * synccoll_check() - sync_collations check
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
static int
synccoll_check (const char *db_name, int *db_obs_coll_cnt, int *new_sys_coll_cnt)
{
#define FILE_STMT_NAME "cubrid_synccolldb_"
#define QUERY_SIZE 1024

  LANG_COLL_COMPAT *db_collations = NULL;
  DB_QUERY_RESULT *query_result = NULL;
  const LANG_COLL_COMPAT *db_coll;
  LANG_COLLATION *lc;
  FILE *f_stmt = NULL;
  char f_stmt_name[PATH_MAX];
  int sys_coll_found[LANG_MAX_COLLATIONS] = { 0 };
  int sys_coll_found_cnt = 0;
  int i, db_coll_cnt;
  int status = EXIT_SUCCESS;
  int db_status;
  char *vclass_names = NULL;
  int vclass_names_used = 0;
  int vclass_names_alloced = 0;
  char *part_tables = NULL;
  int part_tables_used = 0;
  int part_tables_alloced = 0;
  bool need_manual_sync = false;

  assert (db_name != NULL);
  assert (db_obs_coll_cnt != NULL);
  assert (new_sys_coll_cnt != NULL);

  *db_obs_coll_cnt = 0;
  *new_sys_coll_cnt = 0;

  /* read all collations from DB : id, name, checksum */
  db_status = catcls_get_db_collation (NULL, &db_collations, &db_coll_cnt);
  if (db_status != NO_ERROR)
    {
      if (db_collations != NULL)
	{
	  db_private_free (NULL, db_collations);
	}
      status = EXIT_FAILURE;
      goto exit;
    }

  assert (db_collations != NULL);

  strcpy (f_stmt_name, FILE_STMT_NAME);
  strcat (f_stmt_name, db_name);
  strcat (f_stmt_name, ".sql");

  f_stmt = fopen (f_stmt_name, "wt");
  if (f_stmt == NULL)
    {
      fprintf (stderr,
	       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_GENERIC, MSGCAT_UTIL_GENERIC_BAD_OUTPUT_FILE),
	       f_stmt_name);
      goto exit;
    }

  for (i = 0; i < db_coll_cnt; i++)
    {
      DB_QUERY_ERROR query_error;
      char query[QUERY_SIZE];
      int j;
      bool is_obs_coll = false;
      bool check_atts = false;
      bool check_views = false;
      bool check_triggers = false;
      bool check_func_index = false;
      bool check_tables = false;

      db_coll = &(db_collations[i]);

      assert (db_coll->coll_id >= 0 && db_coll->coll_id < LANG_MAX_COLLATIONS);

      for (j = 0; j < LANG_MAX_COLLATIONS; j++)
	{
	  lc = lang_get_collation (j);
	  if (strcmp (db_coll->coll_name, lc->coll.coll_name) == 0)
	    {
	      sys_coll_found[j] = 1;
	      sys_coll_found_cnt++;
	      break;
	    }
	}

      /* check if same collation */
      lc = lang_get_collation (db_coll->coll_id);
      assert (lc != NULL);

      if (lc->coll.coll_id != db_coll->coll_id || lc->codeset != db_coll->codeset
	  || strcasecmp (lc->coll.checksum, db_coll->checksum) != 0)
	{
	  check_tables = true;
	  check_views = true;
	  check_atts = true;
	  check_triggers = true;
	  check_func_index = true;
	  is_obs_coll = true;
	}
      else if (strcmp (lc->coll.coll_name, db_coll->coll_name))
	{
	  check_tables = true;
	  check_views = true;
	  check_triggers = true;
	  is_obs_coll = true;
	  check_func_index = true;
	}

      if (is_obs_coll)
	{
	  (*db_obs_coll_cnt)++;
	  fprintf (stdout, "----------------------------------------\n");
	  fprintf (stdout, "----------------------------------------\n");
	  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SYNCCOLLDB, SYNCCOLLDB_MSG_OBS_COLL),
		   db_coll->coll_name, db_coll->coll_id);
	}

      if (check_tables)
	{
	  /* get table names having collation; do not include partition sub-classes */
	  sprintf (query,
		   "SELECT C.class_name, C.class_type " "FROM _db_class C " "WHERE C.collation_id = %d "
		   "AND NOT (C.class_name IN " "(SELECT P.class_of.class_name " "FROM _db_partition P WHERE "
		   "P.class_of.class_name " " = C.class_name AND P.pname IS NOT NULL))", db_coll->coll_id);

	  db_status = db_compile_and_execute_local (query, &query_result, &query_error);

	  if (db_status < 0)
	    {
	      status = EXIT_FAILURE;
	      goto exit;
	    }
	  else if (db_status > 0)
	    {
	      DB_VALUE class_name;
	      DB_VALUE ct;

	      fprintf (stdout, "----------------------------------------\n");
	      fprintf (stdout,
		       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SYNCCOLLDB, SYNCCOLLDB_MSG_CLASS_OBS_COLL),
		       db_coll->coll_name);

	      while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS)
		{
		  if (db_query_get_tuple_value (query_result, 0, &class_name) != NO_ERROR
		      || db_query_get_tuple_value (query_result, 1, &ct) != NO_ERROR)
		    {
		      status = EXIT_FAILURE;
		      goto exit;
		    }

		  assert (DB_VALUE_TYPE (&class_name) == DB_TYPE_STRING);
		  assert (DB_VALUE_TYPE (&ct) == DB_TYPE_INTEGER);

		  if (DB_GET_INTEGER (&ct) != 0)
		    {
		      continue;
		    }
		  fprintf (stdout, "%s\n", DB_GET_STRING (&class_name));

		  /* output query to fix schema */
		  snprintf (query, sizeof (query) - 1, "ALTER TABLE [%s] " "COLLATE utf8_bin;",
			    DB_GET_STRING (&class_name));
		  fprintf (f_stmt, "%s\n", query);
		  need_manual_sync = true;
		}
	    }

	  if (query_result != NULL)
	    {
	      db_query_end (query_result);
	      query_result = NULL;
	    }
	}

      if (check_atts)
	{
	  /* first drop foreign keys on attributes using collation; do not include partition sub-classes */
	  /* CLASS_NAME, INDEX_NAME */

	  sprintf (query,
		   "SELECT A.class_of.class_name, I.index_name " "from _db_attribute A, _db_index I, "
		   "_db_index_key IK, _db_domain D " "where D.object_of = A AND D.collation_id = %d AND "
		   "NOT (A.class_of.class_name IN (SELECT " "P.class_of.class_name " "FROM _db_partition P WHERE "
		   "P.class_of.class_name = " "A.class_of.class_name AND P.pname IS NOT NULL))"
		   "AND A.attr_name = IK.key_attr_name AND IK in I.key_attrs "
		   "AND I.is_foreign_key = 1 AND I.class_of = A.class_of", db_coll->coll_id);

	  db_status = db_compile_and_execute_local (query, &query_result, &query_error);

	  if (db_status < 0)
	    {
	      status = EXIT_FAILURE;
	      goto exit;
	    }
	  else if (db_status > 0)
	    {
	      DB_VALUE class_name;
	      DB_VALUE index_name;

	      fprintf (stdout, "----------------------------------------\n");
	      fprintf (stdout,
		       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SYNCCOLLDB, SYNCCOLLDB_MSG_FK_OBS_COLL),
		       db_coll->coll_name);

	      while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS)
		{
		  if (db_query_get_tuple_value (query_result, 0, &class_name) != NO_ERROR
		      || db_query_get_tuple_value (query_result, 1, &index_name) != NO_ERROR)
		    {
		      status = EXIT_FAILURE;
		      goto exit;
		    }
		  assert (DB_VALUE_TYPE (&class_name) == DB_TYPE_STRING);
		  assert (DB_VALUE_TYPE (&index_name) == DB_TYPE_STRING);
		  fprintf (stdout, "%s | %s\n", DB_GET_STRING (&class_name), DB_GET_STRING (&index_name));
		  snprintf (query, sizeof (query) - 1, "ALTER TABLE [%s] DROP FOREIGN KEY [%s];",
			    DB_GET_STRING (&class_name), DB_GET_STRING (&index_name));
		  fprintf (f_stmt, "%s\n", query);
		  need_manual_sync = true;
		}
	    }

	  if (query_result != NULL)
	    {
	      db_query_end (query_result);
	      query_result = NULL;
	    }


	  /* attributes having collation; do not include partition sub-classes */
	  /* CLASS_NAME, CLASS_TYPE, ATTR_NAME, ATTR_FULL_TYPE */
	  /* ATTR_FULL_TYPE = CHAR(20) */
	  /* or ENUM ('a', 'b') */

	  sprintf (query,
		   "SELECT A.class_of.class_name, " "A.class_of.class_type, A.attr_name, " "IF (D.data_type = 35,"
		   "CONCAT ('ENUM (', " "(SELECT GROUP_CONCAT(concat('''',EV.a,'''')) "
		   "FROM TABLE(D.enumeration) as EV(a)) ,  ')'), " "CONCAT (CASE D.data_type WHEN 4 THEN 'VARCHAR' "
		   "WHEN 25 THEN 'CHAR' WHEN 27 THEN 'NCHAR VARYING' " "WHEN 26 THEN 'NCHAR' WHEN 35 THEN 'ENUM' END, "
		   "IF (D.prec < 0 AND " "(D.data_type = 4 OR D.data_type = 27) ," "'', CONCAT ('(', D.prec,')')))), "
		   "CASE WHEN A.class_of.sub_classes IS NULL THEN 0 " "ELSE NVL((SELECT 1 FROM _db_partition p "
		   "WHERE p.class_of = A.class_of AND p.pname IS NULL AND "
		   "LOCATE(A.attr_name, TRIM(SUBSTRING(p.pexpr FROM 8 FOR "
		   "(POSITION(' FROM ' IN p.pexpr)-8)))) > 0 ), 0) " "END " "FROM _db_domain D,_db_attribute A "
		   "WHERE D.object_of = A AND D.collation_id = %d " "AND NOT (A.class_of.class_name IN "
		   "(SELECT P.class_of.class_name " "FROM _db_partition P WHERE " "P.class_of.class_name "
		   " = A.class_of.class_name AND P.pname IS NOT NULL)) " "ORDER BY A.class_of.class_name",
		   db_coll->coll_id);

	  db_status = db_compile_and_execute_local (query, &query_result, &query_error);

	  if (db_status < 0)
	    {
	      status = EXIT_FAILURE;
	      goto exit;
	    }
	  else if (db_status > 0)
	    {
	      DB_VALUE class_name;
	      DB_VALUE attr;
	      DB_VALUE ct;
	      DB_VALUE attr_data_type;
	      DB_VALUE has_part;

	      fprintf (stdout, "----------------------------------------\n");
	      fprintf (stdout,
		       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SYNCCOLLDB, SYNCCOLLDB_MSG_ATTR_OBS_COLL),
		       db_coll->coll_name);

	      while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS)
		{
		  bool add_to_part_tables = false;

		  if (db_query_get_tuple_value (query_result, 0, &class_name) != NO_ERROR
		      || db_query_get_tuple_value (query_result, 1, &ct) != NO_ERROR
		      || db_query_get_tuple_value (query_result, 2, &attr) != NO_ERROR
		      || db_query_get_tuple_value (query_result, 3, &attr_data_type) != NO_ERROR
		      || db_query_get_tuple_value (query_result, 4, &has_part) != NO_ERROR)
		    {
		      status = EXIT_FAILURE;
		      goto exit;
		    }

		  assert (DB_VALUE_TYPE (&class_name) == DB_TYPE_STRING);
		  assert (DB_VALUE_TYPE (&attr) == DB_TYPE_STRING);
		  assert (DB_VALUE_TYPE (&ct) == DB_TYPE_INTEGER);
		  assert (DB_VALUE_TYPE (&attr_data_type) == DB_TYPE_STRING);
		  assert (DB_VALUE_TYPE (&has_part) == DB_TYPE_INTEGER);

		  fprintf (stdout, "%s | %s %s\n", DB_GET_STRING (&class_name), DB_GET_STRING (&attr),
			   DB_GET_STRING (&attr_data_type));

		  /* output query to fix schema */
		  if (DB_GET_INTEGER (&ct) == 0)
		    {
		      if (DB_GET_INTEGER (&has_part) == 1)
			{
			  /* class is partitioned, remove partition; we cannot change the collation of an attribute
			   * having partitions */
			  fprintf (f_stmt, "ALTER TABLE [%s] REMOVE PARTITIONING;\n", DB_GET_STRING (&class_name));
			  add_to_part_tables = true;
			}

		      snprintf (query, sizeof (query) - 1, "ALTER TABLE [%s] " "MODIFY [%s] %s COLLATE utf8_bin;",
				DB_GET_STRING (&class_name), DB_GET_STRING (&attr), DB_GET_STRING (&attr_data_type));
		    }
		  else
		    {
		      snprintf (query, sizeof (query) - 1, "DROP VIEW [%s];", DB_GET_STRING (&class_name));

		      if (vclass_names == NULL || vclass_names_alloced <= vclass_names_used)
			{
			  if (vclass_names_alloced == 0)
			    {
			      vclass_names_alloced = 1 + DB_MAX_IDENTIFIER_LENGTH;
			    }
			  vclass_names = (char *) db_private_realloc (NULL, vclass_names, 2 * vclass_names_alloced);

			  if (vclass_names == NULL)
			    {
			      status = EXIT_FAILURE;
			      goto exit;
			    }
			  vclass_names_alloced *= 2;
			}

		      memcpy (vclass_names + vclass_names_used, DB_GET_STRING (&class_name),
			      DB_GET_STRING_SIZE (&class_name));
		      vclass_names_used += DB_GET_STRING_SIZE (&class_name);
		      memcpy (vclass_names + vclass_names_used, "\0", 1);
		      vclass_names_used += 1;
		    }
		  fprintf (f_stmt, "%s\n", query);
		  need_manual_sync = true;

		  if (add_to_part_tables)
		    {
		      if (part_tables == NULL || part_tables_alloced <= part_tables_used)
			{
			  if (part_tables_alloced == 0)
			    {
			      part_tables_alloced = 1 + DB_MAX_IDENTIFIER_LENGTH;
			    }
			  part_tables = (char *) db_private_realloc (NULL, part_tables, 2 * part_tables_alloced);

			  if (part_tables == NULL)
			    {
			      status = EXIT_FAILURE;
			      goto exit;
			    }
			  part_tables_alloced *= 2;
			}

		      memcpy (part_tables + part_tables_used, DB_GET_STRING (&class_name),
			      DB_GET_STRING_SIZE (&class_name));
		      part_tables_used += DB_GET_STRING_SIZE (&class_name);
		      memcpy (part_tables + part_tables_used, "\0", 1);
		      part_tables_used += 1;
		    }
		}
	      if (part_tables != NULL)
		{
		  char *curr_tbl = part_tables;
		  int tbl_size = strlen (curr_tbl);

		  fprintf (stdout, "----------------------------------------\n");
		  fprintf (stdout,
			   msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SYNCCOLLDB,
					   SYNCCOLLDB_MSG_PARTITION_OBS_COLL), db_coll->coll_name);

		  while (tbl_size > 0)
		    {
		      printf ("%s\n", curr_tbl);
		      curr_tbl += tbl_size + 1;
		      if (curr_tbl >= part_tables + part_tables_used)
			{
			  break;
			}
		      tbl_size = strlen (curr_tbl);
		    }
		}
	    }

	  if (query_result != NULL)
	    {
	      db_query_end (query_result);
	      query_result = NULL;
	    }
	}

      if (check_views)
	{
	  sprintf (query,
		   "SELECT class_of.class_name, spec " "FROM _db_query_spec " "WHERE LOCATE ('collate %s', spec) > 0",
		   db_coll->coll_name);

	  db_status = db_compile_and_execute_local (query, &query_result, &query_error);

	  if (db_status < 0)
	    {
	      status = EXIT_FAILURE;
	      goto exit;
	    }
	  else if (db_status > 0)
	    {
	      DB_VALUE view;
	      DB_VALUE query_spec;

	      fprintf (stdout, "----------------------------------------\n");
	      fprintf (stdout,
		       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SYNCCOLLDB, SYNCCOLLDB_MSG_VIEW_OBS_COLL),
		       db_coll->coll_name);

	      while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS)
		{
		  bool already_dropped = false;

		  if (db_query_get_tuple_value (query_result, 0, &view) != NO_ERROR
		      || db_query_get_tuple_value (query_result, 1, &query_spec) != NO_ERROR)
		    {
		      status = EXIT_FAILURE;
		      goto exit;
		    }

		  assert (DB_VALUE_TYPE (&view) == DB_TYPE_STRING);
		  assert (DB_VALUE_TYPE (&query_spec) == DB_TYPE_STRING);

		  fprintf (stdout, "%s | %s\n", DB_GET_STRING (&view), DB_GET_STRING (&query_spec));

		  /* output query to fix schema */
		  if (vclass_names != NULL)
		    {
		      char *search = vclass_names;
		      int view_name_size = DB_GET_STRING_SIZE (&view);

		      /* search if the view was already put in .SQL file */
		      while (search + view_name_size < vclass_names + vclass_names_used)
			{
			  if (memcmp (search, DB_GET_STRING (&view), view_name_size) == 0
			      && *(search + view_name_size) == '\0')
			    {
			      already_dropped = true;
			      break;
			    }

			  while (*search++ != '\0')
			    {
			      ;
			    }
			}
		    }

		  if (!already_dropped)
		    {
		      snprintf (query, sizeof (query) - 1, "DROP VIEW [%s];", DB_GET_STRING (&view));
		      fprintf (f_stmt, "%s\n", query);
		    }
		  need_manual_sync = true;
		}
	    }

	  if (query_result != NULL)
	    {
	      db_query_end (query_result);
	      query_result = NULL;
	    }
	}

      if (check_triggers)
	{
	  sprintf (query, "SELECT name, condition FROM db_trigger " "WHERE LOCATE ('collate %s', condition) > 0",
		   db_coll->coll_name);

	  db_status = db_compile_and_execute_local (query, &query_result, &query_error);

	  if (db_status < 0)
	    {
	      status = EXIT_FAILURE;
	      goto exit;
	    }
	  else if (db_status > 0)
	    {
	      DB_VALUE trig_name;
	      DB_VALUE trig_cond;

	      fprintf (stdout, "----------------------------------------\n");
	      fprintf (stdout,
		       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SYNCCOLLDB, SYNCCOLLDB_MSG_TRIG_OBS_COLL),
		       db_coll->coll_name);


	      while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS)
		{
		  if (db_query_get_tuple_value (query_result, 0, &trig_name) != NO_ERROR
		      || db_query_get_tuple_value (query_result, 1, &trig_cond) != NO_ERROR)
		    {
		      status = EXIT_FAILURE;
		      goto exit;
		    }

		  assert (DB_VALUE_TYPE (&trig_name) == DB_TYPE_STRING);
		  assert (DB_VALUE_TYPE (&trig_cond) == DB_TYPE_STRING);

		  fprintf (stdout, "%s | %s\n", DB_GET_STRING (&trig_name), DB_GET_STRING (&trig_cond));

		  /* output query to fix schema */
		  snprintf (query, sizeof (query) - 1, "DROP TRIGGER [%s];", DB_GET_STRING (&trig_name));
		  fprintf (f_stmt, "%s\n", query);
		  need_manual_sync = true;
		}
	    }

	  if (query_result != NULL)
	    {
	      db_query_end (query_result);
	      query_result = NULL;
	    }
	}

      if (check_func_index)
	{
	  /* Function indexes using collation; do not include partition sub-classes */
	  /* INDEX_NAME, FUNCTION_EXPRESSION, CLASS_NAME */
	  sprintf (query,
		   "SELECT index_of.index_name, func, " "index_of.class_of.class_name FROM "
		   "_db_index_key WHERE LOCATE ('%s', func) > 0 " "AND NOT (index_of.class_of.class_name IN "
		   "(SELECT P.class_of.class_name " "FROM _db_partition P WHERE " "P.class_of.class_name "
		   " = index_of.class_of.class_name " "AND P.pname IS NOT NULL)) ", db_coll->coll_name);

	  db_status = db_compile_and_execute_local (query, &query_result, &query_error);

	  if (db_status < 0)
	    {
	      status = EXIT_FAILURE;
	      goto exit;
	    }
	  else if (db_status > 0)
	    {
	      DB_VALUE index_name;
	      DB_VALUE func_expr;
	      DB_VALUE class_name;

	      fprintf (stdout, "----------------------------------------\n");
	      fprintf (stdout,
		       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SYNCCOLLDB, SYNCCOLLDB_MSG_FI_OBS_COLL),
		       db_coll->coll_name);


	      while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS)
		{
		  if (db_query_get_tuple_value (query_result, 0, &index_name) != NO_ERROR
		      || db_query_get_tuple_value (query_result, 1, &func_expr) != NO_ERROR
		      || db_query_get_tuple_value (query_result, 2, &class_name) != NO_ERROR)
		    {
		      status = EXIT_FAILURE;
		      goto exit;
		    }

		  assert (DB_VALUE_TYPE (&index_name) == DB_TYPE_STRING);
		  assert (DB_VALUE_TYPE (&func_expr) == DB_TYPE_STRING);
		  assert (DB_VALUE_TYPE (&class_name) == DB_TYPE_STRING);

		  fprintf (stdout, "%s | %s | %s\n", DB_GET_STRING (&class_name), DB_GET_STRING (&index_name),
			   DB_GET_STRING (&func_expr));

		  /* output query to fix schema */
		  snprintf (query, sizeof (query) - 1, "ALTER TABLE [%s] " "DROP INDEX [%s];",
			    DB_GET_STRING (&class_name), DB_GET_STRING (&index_name));
		  fprintf (f_stmt, "%s\n", query);
		  need_manual_sync = true;
		}
	    }

	  if (query_result != NULL)
	    {
	      db_query_end (query_result);
	      query_result = NULL;
	    }
	}
    }

  fprintf (stdout, "----------------------------------------\n");
  fprintf (stdout, "----------------------------------------\n");
  if (*db_obs_coll_cnt == 0)
    {
      fprintf (stdout,
	       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SYNCCOLLDB, SYNCCOLLDB_MSG_REPORT_DB_OBS_OK),
	       db_name);
    }
  else
    {
      fprintf (stdout,
	       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SYNCCOLLDB, SYNCCOLLDB_MSG_REPORT_DB_OBS_NOK),
	       *db_obs_coll_cnt);
      if (need_manual_sync)
	{
	  fprintf (stdout,
		   msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SYNCCOLLDB, SYNCCOLLDB_MSG_REPORT_SQL_FILE),
		   f_stmt_name);
	}
    }

  if (!need_manual_sync)
    {
      if (f_stmt != NULL)
	{
	  fclose (f_stmt);
	  f_stmt = NULL;
	}
      remove (f_stmt_name);
    }

  if (lang_collation_count () != sys_coll_found_cnt)
    {
      fprintf (stdout,
	       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SYNCCOLLDB, SYNCCOLLDB_MSG_REPORT_NEW_COLL),
	       lang_collation_count () - sys_coll_found_cnt);

      for (i = 0; i < LANG_MAX_COLLATIONS; i++)
	{
	  if (sys_coll_found[i] == 1)
	    {
	      continue;
	    }

	  lc = lang_get_collation (i);
	  if (lc->coll.coll_id == LANG_COLL_ISO_BINARY)
	    {
	      assert (i != 0);
	      continue;
	    }

	  assert (sys_coll_found[i] == 0);
	  /* system collation was not found in DB */
	  fprintf (stdout, "%s\n", lc->coll.coll_name);
	  (*new_sys_coll_cnt)++;
	}
      fprintf (stdout, "\n");
    }

  if (*db_obs_coll_cnt == 0 && *new_sys_coll_cnt == 0)
    {
      fprintf (stdout,
	       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SYNCCOLLDB, SYNCCOLLDB_MSG_REPORT_NOT_NEEDED));
    }
  else
    {
      fprintf (stdout,
	       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SYNCCOLLDB, SYNCCOLLDB_MSG_REPORT_SYNC_REQUIRED),
	       db_name);
    }

  fprintf (stdout, "----------------------------------------\n");
  fprintf (stdout, "----------------------------------------\n");

exit:
  if (vclass_names != NULL)
    {
      db_private_free (NULL, vclass_names);
      vclass_names = NULL;
    }

  if (part_tables != NULL)
    {
      db_private_free (NULL, part_tables);
      part_tables = NULL;
    }

  if (f_stmt != NULL)
    {
      fclose (f_stmt);
      f_stmt = NULL;
    }

  if (query_result != NULL)
    {
      db_query_end (query_result);
      query_result = NULL;
    }
  if (db_collations != NULL)
    {
      db_private_free (NULL, db_collations);
    }

  return status;

#undef FILE_STMT_NAME
#undef QUERY_SIZE
}

/*
 * synccoll_force() - sync_collations force new collation into DB
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
synccoll_force (void)
{
  DB_OBJECT *class_mop;
  int status = EXIT_SUCCESS;
  int au_save;

  class_mop = db_find_class (CT_COLLATION_NAME);
  if (class_mop == NULL)
    {
      status = EXIT_FAILURE;
      return status;
    }

  AU_DISABLE (au_save);
  if (db_truncate_class (class_mop) != NO_ERROR)
    {
      AU_ENABLE (au_save);
      status = EXIT_FAILURE;
      return status;
    }

  if (boot_add_collations (class_mop) != NO_ERROR)
    {
      status = EXIT_FAILURE;
    }

  AU_ENABLE (au_save);
  return status;
}

/*
 * delete_all_ha_apply_info () - delete ha apply info
 *   return:
 */
static int
delete_all_ha_apply_info (void)
{
#define QUERY_BUF_SIZE		2048

  int res, au_save;
  DB_QUERY_RESULT *result;
  DB_QUERY_ERROR query_error;
  char query_buf[QUERY_BUF_SIZE];

  snprintf (query_buf, sizeof (query_buf), "DELETE FROM %s ;", CT_HA_APPLY_INFO_NAME);

  AU_DISABLE (au_save);

  res = db_execute (query_buf, &result, &query_error);
  if (res >= 0)
    {
      int error;

      error = db_query_end (result);
      if (error != NO_ERROR)
	{
	  res = error;
	}
    }

  AU_ENABLE (au_save);

  return res;
#undef QUERY_BUF_SIZE
}

/*
 * insert_ha_apply_info () - insert ha apply info
 *   return:
 */
static int
insert_ha_apply_info (char *database_name, char *master_host_name, INT64 database_creation, INT64 pageid, int offset)
{
#define APPLY_INFO_VALUES	15
#define QUERY_BUF_SIZE		2048

  int i, res, au_save;
  int in_value_idx;
  char *copy_log_base;
  char copy_log_base_buf[PATH_MAX];
  char log_path[PATH_MAX];
  char query_buf[QUERY_BUF_SIZE];
  DB_VALUE in_value[APPLY_INFO_VALUES];
  DB_DATETIME db_creation;
  DB_QUERY_RESULT *result;
  DB_QUERY_ERROR query_error;

  db_localdatetime (&database_creation, &db_creation);
  copy_log_base = prm_get_string_value (PRM_ID_HA_COPY_LOG_BASE);
  if (copy_log_base == NULL || *copy_log_base == '\0')
    {
      copy_log_base = envvar_get ("DATABASES");
      if (copy_log_base == NULL)
	{
	  return ER_FAILED;
	}
    }

  copy_log_base = realpath (copy_log_base, copy_log_base_buf);
  snprintf (log_path, PATH_MAX, "%s/%s_%s", copy_log_base, database_name, master_host_name);

  snprintf (query_buf, sizeof (query_buf), "INSERT INTO %s "	/* INSERT */
	    "( db_name, "	/* 1 */
	    "  db_creation_time, "	/* 2 */
	    "  copied_log_path, "	/* 3 */
	    "  committed_lsa_pageid, "	/* 4 */
	    "  committed_lsa_offset, "	/* 5 */
	    "  committed_rep_pageid, "	/* 6 */
	    "  committed_rep_offset, "	/* 7 */
	    "  append_lsa_pageid, "	/* 8 */
	    "  append_lsa_offset, "	/* 9 */
	    "  eof_lsa_pageid, "	/* 10 */
	    "  eof_lsa_offset, "	/* 11 */
	    "  final_lsa_pageid, "	/* 12 */
	    "  final_lsa_offset, "	/* 13 */
	    "  required_lsa_pageid, "	/* 14 */
	    "  required_lsa_offset, "	/* 15 */
	    "  log_record_time, "	/* 16 */
	    "  log_commit_time, "	/* 17 */
	    "  last_access_time, "	/* 18 */
	    "  status, "	/* 19 */
	    "  insert_counter, "	/* 20 */
	    "  update_counter, "	/* 21 */
	    "  delete_counter, "	/* 22 */
	    "  schema_counter, "	/* 23 */
	    "  commit_counter, "	/* 24 */
	    "  fail_counter, "	/* 25 */
	    "  start_time ) "	/* 26 */
	    " VALUES ( ?, "	/* 1. db_name */
	    "   ?, "		/* 2. db_creation_time */
	    "   ?, "		/* 3. copied_log_path */
	    "   ?, "		/* 4. committed_lsa_pageid */
	    "   ?, "		/* 5. committed_lsa_offset */
	    "   ?, "		/* 6. committed_rep_pageid */
	    "   ?, "		/* 7. committed_rep_offset */
	    "   ?, "		/* 8. append_lsa_pageid */
	    "   ?, "		/* 9. append_lsa_offset */
	    "   ?, "		/* 10. eof_lsa_pageid */
	    "   ?, "		/* 11. eof_lsa_offset */
	    "   ?, "		/* 12. final_lsa_pageid */
	    "   ?, "		/* 13. final_lsa_offset */
	    "   ?, "		/* 14. required_lsa_pageid */
	    "   ?, "		/* 15. required_lsa_offset */
	    "   NULL, "		/* 16. log_record_time */
	    "   NULL, "		/* 17. log_commit_time */
	    "   NULL, "		/* 18. last_access_time */
	    "   0, "		/* 19. status */
	    "   0, "		/* 20. insert_counter */
	    "   0, "		/* 21. update_counter */
	    "   0, "		/* 22. delete_counter */
	    "   0, "		/* 23. schema_counter */
	    "   0, "		/* 24. commit_counter */
	    "   0, "		/* 25. fail_counter */
	    "   NULL "		/* 26. start_time */
	    "   ) ;", CT_HA_APPLY_INFO_NAME);

  in_value_idx = 0;

  /* 1. db_name */
  db_make_varchar (&in_value[in_value_idx++], 255, database_name, strlen (database_name), LANG_SYS_CODESET,
		   LANG_SYS_COLLATION);

  /* 2. db_creation time */
  db_make_datetime (&in_value[in_value_idx++], &db_creation);

  /* 3. copied_log_path */
  db_make_varchar (&in_value[in_value_idx++], 4096, log_path, strlen (log_path), LANG_SYS_CODESET, LANG_SYS_COLLATION);

  /* 4 ~ 15. lsa */
  for (i = 0; i < 6; i++)
    {
      db_make_bigint (&in_value[in_value_idx++], pageid);
      db_make_int (&in_value[in_value_idx++], offset);
    }

  assert_release (in_value_idx == APPLY_INFO_VALUES);

  AU_DISABLE (au_save);

  res = db_execute_with_values (query_buf, &result, &query_error, in_value_idx, &in_value[0]);
  if (res >= 0)
    {
      int error;

      error = db_query_end (result);
      if (error != NO_ERROR)
	{
	  res = error;
	}
    }

  AU_ENABLE (au_save);

  for (i = 0; i < in_value_idx; i++)
    {
      db_value_clear (&in_value[i]);
    }

  return res;

#undef APPLY_INFO_VALUES
#undef QUERY_BUF_SIZE
}

/*
 * delete_all_slave_ha_apply_info () - delete slave ha apply info
 *   return:
 */
static int
delete_all_slave_ha_apply_info (char *database_name, char *master_host_name)
{
#define APPLY_INFO_VALUES	2
#define QUERY_BUF_SIZE		2048

  int res, au_save;
  int in_value_idx;
  char *copy_log_base;
  char copy_log_base_buf[PATH_MAX];
  char log_path[PATH_MAX];
  DB_VALUE in_value[APPLY_INFO_VALUES];
  DB_QUERY_RESULT *result;
  DB_QUERY_ERROR query_error;
  char query_buf[QUERY_BUF_SIZE];

  copy_log_base = prm_get_string_value (PRM_ID_HA_COPY_LOG_BASE);
  if (copy_log_base == NULL || *copy_log_base == '\0')
    {
      copy_log_base = envvar_get ("DATABASES");
      if (copy_log_base == NULL)
	{
	  return ER_FAILED;
	}
    }

  copy_log_base = realpath (copy_log_base, copy_log_base_buf);
  snprintf (log_path, PATH_MAX, "%s/%s_%s", copy_log_base, database_name, master_host_name);

  snprintf (query_buf, sizeof (query_buf), "DELETE FROM %s " "WHERE db_name = ? and copied_log_path <> ?",
	    CT_HA_APPLY_INFO_NAME);

  in_value_idx = 0;
  /* 1. db_name */
  db_make_varchar (&in_value[in_value_idx++], 255, database_name, strlen (database_name), LANG_SYS_CODESET,
		   LANG_SYS_COLLATION);

  /* 2. copied_log_path */
  db_make_varchar (&in_value[in_value_idx++], 4096, log_path, strlen (log_path), LANG_SYS_CODESET, LANG_SYS_COLLATION);

  AU_DISABLE (au_save);

  res = db_execute_with_values (query_buf, &result, &query_error, in_value_idx, &in_value[0]);
  if (res >= 0)
    {
      int error;

      error = db_query_end (result);
      if (error != NO_ERROR)
	{
	  res = error;
	}
    }

  AU_ENABLE (au_save);

  return res;
#undef APPLY_INFO_VALUES
#undef QUERY_BUF_SIZE
}

/*
 * check_ha_db_and_node_list () - check ha db list and ha node list
 *   return:
 */
static bool
check_ha_db_and_node_list (char *database_name, char *source_host_name)
{
  int i, j, status, num_nodes;
  char **dbs;
  HA_CONF ha_conf;
  HA_NODE_CONF *nc;

  memset ((void *) &ha_conf, 0, sizeof (HA_CONF));
  status = util_make_ha_conf (&ha_conf);
  if (status != NO_ERROR)
    {
      return false;
    }

  num_nodes = ha_conf.num_node_conf;
  dbs = ha_conf.db_names;
  nc = ha_conf.node_conf;

  for (i = 0; dbs[i] != NULL; i++)
    {
      if (strcmp (dbs[i], database_name) != 0)
	{
	  continue;
	}

      for (j = 0; j < num_nodes; j++)
	{
	  if (strcmp (nc[j].node_name, source_host_name) != 0)
	    {
	      continue;
	    }

	  if (util_is_localhost (nc[j].node_name))
	    {
	      continue;
	    }

	  util_free_ha_conf (&ha_conf);
	  return true;
	}
    }

  util_free_ha_conf (&ha_conf);
  return false;
}

/*
 * restoreslave() - restoreslave main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
restoreslave (UTIL_FUNCTION_ARG * arg)
{
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  DB_DATETIME datetime;
  bool init_ha_catalog;
  int status, error_code;
  char er_msg_file[PATH_MAX];
  char db_creation_time[LINE_MAX];
  char *database_name;
  char *source_state;
  char *master_host_name;
  BO_RESTART_ARG restart_arg;

  if (sysprm_load_and_init (NULL, NULL) != NO_ERROR)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_SERVICE_PROPERTY_FAIL);
      goto error_exit;
    }

  database_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  master_host_name = utility_get_option_string_value (arg_map, RESTORESLAVE_MASTER_HOST_NAME_S, 0);
  source_state = utility_get_option_string_value (arg_map, RESTORESLAVE_SOURCE_STATE_S, 0);

  if (master_host_name == NULL || source_state == NULL)
    {
      goto print_restoreslave_usage;
    }

  if (strcasecmp (source_state, "master") == 0)
    {
      init_ha_catalog = true;
    }
  else if (strcasecmp (source_state, "slave") == 0 || strcasecmp (source_state, "replica") == 0)
    {
      init_ha_catalog = false;
    }
  else
    {
      fprintf (stderr,
	       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_RESTORESLAVE, RESTORESLAVE_MSG_INVAILD_STATE),
	       source_state);
      goto error_exit;
    }

  restart_arg.restore_slave = true;
  restart_arg.printtoc = utility_get_option_bool_value (arg_map, RESTORESLAVE_LIST_S);
  restart_arg.backuppath = utility_get_option_string_value (arg_map, RESTORESLAVE_BACKUP_FILE_PATH_S, 0);
  restart_arg.level = 0;
  restart_arg.verbose_file = utility_get_option_string_value (arg_map, RESTORESLAVE_OUTPUT_FILE_S, 0);
  restart_arg.newvolpath = utility_get_option_bool_value (arg_map, RESTORESLAVE_USE_DATABASE_LOCATION_PATH_S);
  restart_arg.restore_upto_bktime = false;
  restart_arg.stopat = time (NULL);

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_restoreslave_usage;
    }

  if (check_ha_db_and_node_list (database_name, master_host_name) == false)
    {
      fprintf (stderr,
	       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_RESTORESLAVE, RESTORESLAVE_MSG_INVAILD_OPTIONS));
      goto error_exit;
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  if (restart_arg.printtoc)
    {
      error_code = print_backup_info (database_name, &restart_arg);
      if (error_code != NO_ERROR)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
	  fprintf (stderr,
		   msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_RESTORESLAVE, RESTORESLAVE_MSG_FAILURE));
	  goto error_exit;
	}

      return EXIT_SUCCESS;
    }

  sysprm_set_force (prm_get_name (PRM_ID_JAVA_STORED_PROCEDURE), "no");

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("DBA", NULL);

  status = boot_restart_from_backup (true, database_name, &restart_arg);
  if (status == NULL_TRAN_INDEX)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_RESTORESLAVE, RESTORESLAVE_MSG_FAILURE));
      goto error_exit;
    }
  else
    {
      boot_shutdown_server (ER_ALL_FINAL);
    }

  db_localdatetime (&restart_arg.db_creation, &datetime);
  db_datetime_to_string (db_creation_time, LINE_MAX, &datetime);

  if (db_restart (arg->command_name, TRUE, database_name) != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      fprintf (stderr,
	       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_RESTORESLAVE, RESTORESLAVE_MSG_HA_CATALOG_FAIL),
	       db_creation_time, restart_arg.restart_repl_lsa.pageid, restart_arg.restart_repl_lsa.offset);
      return EXIT_FAILURE;
    }

  if (init_ha_catalog)
    {
      error_code = delete_all_ha_apply_info ();
      if (error_code < 0)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
	  fprintf (stderr,
		   msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_RESTORESLAVE,
				   RESTORESLAVE_MSG_HA_CATALOG_FAIL), db_creation_time,
		   restart_arg.restart_repl_lsa.pageid, restart_arg.restart_repl_lsa.offset);
	  db_shutdown ();
	  return EXIT_FAILURE;
	}

      error_code =
	insert_ha_apply_info (database_name, master_host_name, restart_arg.db_creation,
			      restart_arg.restart_repl_lsa.pageid, (int) restart_arg.restart_repl_lsa.offset);
      if (error_code < 0)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
	  fprintf (stderr,
		   msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_RESTORESLAVE,
				   RESTORESLAVE_MSG_HA_CATALOG_FAIL), db_creation_time,
		   restart_arg.restart_repl_lsa.pageid, restart_arg.restart_repl_lsa.offset);
	  db_shutdown ();
	  return EXIT_FAILURE;
	}
    }
  else
    {
      error_code = delete_all_slave_ha_apply_info (database_name, master_host_name);
      if (error_code < 0)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
	  fprintf (stderr,
		   msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_RESTORESLAVE,
				   RESTORESLAVE_MSG_HA_CATALOG_FAIL), db_creation_time,
		   restart_arg.restart_repl_lsa.pageid, restart_arg.restart_repl_lsa.offset);
	  db_shutdown ();
	  return EXIT_FAILURE;
	}

    }
  db_commit_transaction ();
  db_shutdown ();

  return EXIT_SUCCESS;

print_restoreslave_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_RESTORESLAVE, RESTORESLAVE_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

error_exit:
  return EXIT_FAILURE;
}

/*
 * gen_tz() - generate time zone data as a C source file, to be compiled into
 *	      a shared library (DLL/so) using included makefile/build script
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
gen_tz (UTIL_FUNCTION_ARG * arg)
{
#define CHECKSUM_SIZE 32
  UTIL_ARG_MAP *arg_map = NULL;
  char *input_path = NULL;
  char *tz_gen_mode = NULL;
  TZ_GEN_TYPE tz_gen_type = TZ_GEN_TYPE_NEW;
  int exit_status = EXIT_SUCCESS;
  bool write_checksum = false;
  char checksum[CHECKSUM_SIZE + 1];
  bool need_db_shutdown = false;
  bool er_inited = false;
  DB_INFO *dir = NULL;
  DB_INFO *db_info_p = NULL;
  char *db_name = NULL;
  char er_msg_file[PATH_MAX];

  assert (arg != NULL);
  arg_map = arg->arg_map;

  tz_gen_mode = utility_get_option_string_value (arg_map, GEN_TZ_MODE_S, 0);
  if (tz_gen_mode == NULL)
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_GEN_TZ, GEN_TZ_MSG_USAGE),
	       basename (arg->argv0), basename (arg->argv0));
      goto exit;
    }

  if (strcasecmp (tz_gen_mode, "new") == 0)
    {
      tz_gen_type = TZ_GEN_TYPE_NEW;
    }
  else if (strcasecmp (tz_gen_mode, "extend") == 0)
    {
      tz_gen_type = TZ_GEN_TYPE_EXTEND;

      db_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
      if (db_name != NULL && check_database_name (db_name) != NO_ERROR)
	{
	  exit_status = EXIT_FAILURE;
	  goto exit;
	}
    }
  else if (strcasecmp (tz_gen_mode, "update") == 0)
    {
      tz_gen_type = TZ_GEN_TYPE_UPDATE;

      /* This is a temporary fix to show usages until we have the update option.
       * Please remove the following when you are going to implement it.
       */
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_GEN_TZ, GEN_TZ_MSG_INVALID_MODE));
      goto print_gen_tz_usage;
    }
  else
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_GEN_TZ, GEN_TZ_MSG_INVALID_MODE));
      goto print_gen_tz_usage;
    }

  input_path = utility_get_option_string_value (arg_map, GEN_TZ_INPUT_FOLDER_S, 0);

  if (input_path == NULL || strlen (input_path) == 0)
    {
      char inputpath_local[PATH_MAX] = { 0 };

      envvar_tzdata_dir_file (inputpath_local, sizeof (inputpath_local), "");
      input_path = inputpath_local;
    }

  /* error message log file */
  if (db_name != NULL)
    {
      snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", db_name, arg->command_name);
    }
  else
    {
      snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s.err", arg->command_name);
    }
  er_init (er_msg_file, ER_NEVER_EXIT);
  er_inited = true;

  memset (checksum, 0, sizeof (checksum));
  if (timezone_compile_data (input_path, tz_gen_type, checksum) != NO_ERROR)
    {
      exit_status = EXIT_FAILURE;
      goto exit;
    }

  if (tz_gen_type == TZ_GEN_TYPE_EXTEND && checksum[0] != '\0')
    {
      AU_DISABLE_PASSWORDS ();
      db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
      db_login ("DBA", NULL);

      if (db_name != NULL)
	{
	  dir = (DB_INFO *) calloc (1, sizeof (DB_INFO));
	  if (dir == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DB_INFO));
	      exit_status = EXIT_FAILURE;
	      goto exit;
	    }
	  dir->name = (char *) calloc (strlen (db_name) + 1, sizeof (char));
	  if (dir->name == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, strlen (db_name) + 1);
	      exit_status = EXIT_FAILURE;
	      goto exit;
	    }
	  strcpy (dir->name, db_name);
	  dir->next = NULL;
	}
      else if (cfg_read_directory (&dir, false) != NO_ERROR)
	{
	  exit_status = EXIT_FAILURE;
	  goto exit;
	}

      for (db_info_p = dir; db_info_p != NULL; db_info_p = db_info_p->next)
	{
	  if (db_restart (arg->command_name, TRUE, db_info_p->name) != NO_ERROR)
	    {
	      need_db_shutdown = true;
	      exit_status = EXIT_FAILURE;
	      goto exit;
	    }
	  if (put_timezone_checksum (checksum) != NO_ERROR)
	    {
	      need_db_shutdown = true;
	      exit_status = EXIT_FAILURE;
	      db_abort_transaction ();
	      goto exit;
	    }
	  else
	    {
	      /* write the new checksum in the database */
	      db_commit_transaction ();
	    }
	  db_shutdown ();
	}
    }

exit:
  if (dir != NULL)
    {
      cfg_free_directory (dir);
    }

  if (exit_status != EXIT_SUCCESS)
    {
      fprintf (stderr, "%s\n", db_error_string (3));

      if (need_db_shutdown == true)
	{
	  db_shutdown ();
	  er_inited = false;
	}
    }

  if (er_inited == true)
    {
      er_final (ER_ALL_FINAL);
    }

  return exit_status;

print_gen_tz_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_GEN_TZ, GEN_TZ_MSG_USAGE),
	   basename (arg->argv0), basename (arg->argv0));
  return EXIT_FAILURE;
#undef CHECKSUM_SIZE
}

/*
 * dump_tz() - display time zone data from CUBRID's timezone shared library
 *	       or from the generated and embedded timezone_list.c file
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
dump_tz (UTIL_FUNCTION_ARG * arg)
{
  long int zone_id = -1;
  UTIL_ARG_MAP *arg_map = NULL;
  int tz_gen_type = TZ_GEN_TYPE_NEW;
  int err_status = EXIT_SUCCESS;
  char *zone = NULL;
  char *str_next = NULL;
  bool is_dump_countries = false;
  bool is_dump_zone_list = false;
  bool is_dump_zone = false;
  bool is_dump_leap_sec = false;
  bool is_dump_summary = false;
  const TZ_DATA *tzd;

  assert (arg != NULL);
  arg_map = arg->arg_map;

  /* read and check arguments */
  is_dump_countries = utility_get_option_bool_value (arg_map, DUMP_TZ_COUNTRIES_S);
  is_dump_zone_list = utility_get_option_bool_value (arg_map, DUMP_TZ_ZONES_S);
  is_dump_leap_sec = utility_get_option_bool_value (arg_map, DUMP_TZ_LEAP_SEC_S);
  is_dump_summary = utility_get_option_bool_value (arg_map, DUMP_TZ_DUMP_SUM_S);
  zone = utility_get_option_string_value (arg_map, DUMP_TZ_ZONE_ID_S, 0);

  if ((zone == NULL) && (!is_dump_countries) && (!is_dump_zone_list) && (!is_dump_leap_sec) && (!is_dump_summary))
    {
      goto print_dump_tz_usage;
    }

  if (zone != NULL && *zone != '\0')
    {
      /* check if either a single zone or all zones should be dumped */
      if (strcasecmp (zone, "all") == 0)
	{
	  zone_id = -1;
	}
      else
	{
	  zone_id = strtol (zone, &str_next, 10);
	  /* check zone_id, str_next, and errno */
	  if (zone_id == 0 && *str_next != '\0')
	    {
	      goto print_dump_tz_usage;
	    }
	  if (errno == ERANGE)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ARG_OUT_OF_RANGE, 1, zone);
	      fprintf (stderr,
		       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_DUMP_TZ, DUMP_TZ_MSG_ID_OUT_OF_RANGE));
	      err_status = EXIT_FAILURE;
	      goto exit;
	    }
	}
      is_dump_zone = true;
    }

  if (tz_load () != NO_ERROR)
    {
      err_status = EXIT_FAILURE;
      goto exit;
    }
  tzd = tz_get_data ();
  assert (tzd != NULL);
  /* check if zone_id is valid for the loaded timezone library */
  if (is_dump_zone && zone_id < -1 && zone_id >= tzd->timezone_count)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ARG_OUT_OF_RANGE, 1, zone);
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_DUMP_TZ, DUMP_TZ_MSG_ID_OUT_OF_RANGE));
      err_status = EXIT_FAILURE;
      goto exit;
    }

  printf ("\nDump timezone data");

  if (!is_dump_countries && !is_dump_zone && !is_dump_zone_list && !is_dump_leap_sec)
    {
      tzc_dump_summary (tzd);
      goto exit;
    }

  if (is_dump_countries)
    {
      printf ("\n\nDumping countries...\n");
      tzc_dump_countries (tzd);
    }
  if (is_dump_zone_list)
    {
      printf ("\n\nDumping timezones...\n");
      tzc_dump_timezones (tzd);
    }
  if (is_dump_zone)
    {
      if (zone_id != -1)
	{
	  tzc_dump_one_timezone (tzd, zone_id);
	}
      else
	{
	  for (zone_id = 0; zone_id < tzd->timezone_count; zone_id++)
	    {
	      printf ("\n\n");
	      tzc_dump_one_timezone (tzd, zone_id);
	    }
	}
    }

  if (is_dump_leap_sec)
    {
      printf ("\n\nDumping leap seconds...\n");
      tzc_dump_leap_sec (tzd);
    }

exit:
  tz_unload ();

  return err_status;

print_dump_tz_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_DUMP_TZ, DUMP_TZ_MSG_USAGE),
	   basename (arg->argv0));
  return err_status;
}
