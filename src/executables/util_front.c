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
 * util_front.c : a front-end of old utilities for compatibility
 */

#ident "$Id$"

#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>
#include <unistd.h>
#include "utility.h"
#include "error_code.h"

typedef struct
{
  const char *old_arg;
  const char *long_arg;
} ARG_MAP_TABLE;

static ARG_MAP_TABLE ua_Create_map[] = {
  {"-p", "--" CREATE_PAGES_L},
  {"-c", "--" CREATE_COMMENT_L},
  {"-f", "--" CREATE_FILE_PATH_L},
  {"-l", "--" CREATE_LOG_PATH_L},
  {"-s", "--" CREATE_SERVER_NAME_L},
  {"-r", "--" CREATE_REPLACE_L},
  {"-mv", "--" CREATE_MORE_VOLUME_FILE_L},
  {"-u", "--" CREATE_USER_DEFINITION_FILE_L},
  {"-i", "--" CREATE_CSQL_INITIALIZATION_FILE_L},
  {"-o", "--" CREATE_OUTPUT_FILE_L},
  {"-v", "--" CREATE_VERBOSE_L},
  {"-n", "--" CREATE_CHARSET_L},
  {"-lp", "--" CREATE_LOG_PAGE_COUNT_L},
  {"-ps", "--" CREATE_PAGE_SIZE_L},
  {0, 0}
};

static ARG_MAP_TABLE ua_Rename_map[] = {
  {"-e", "--" RENAME_EXTENTED_VOLUME_PATH_L},
  {"-tf", "--" RENAME_CONTROL_FILE_L},
  {"-f", "--" RENAME_DELETE_BACKUP_L},
  {0, 0}
};

static ARG_MAP_TABLE ua_Copy_map[] = {
  {"-s", "--" COPY_SERVER_NAME_L},
  {"-f", "--" COPY_FILE_PATH_L},
  {"-l", "--" COPY_LOG_PATH_L},
  {"-e", "--" COPY_EXTENTED_VOLUME_PATH_L},
  {"-tf", "--" COPY_CONTROL_FILE_L},
  {"-r", "--" COPY_REPLACE_L},
  {"-m", "--" COPY_DELETE_SOURCE_L},
  {"-B", "--" COPY_LOB_PATH_L},
  {0, 0}
};

static ARG_MAP_TABLE ua_Delete_map[] = {
  {"-o", "--" DELETE_OUTPUT_FILE_L},
  {"-f", "--" DELETE_DELETE_BACKUP_L},
  {0, 0}
};

static ARG_MAP_TABLE ua_Backup_map[] = {
  {"-l", "--" BACKUP_DESTINATION_PATH_L},
  {"-r", "--" BACKUP_REMOVE_ARCHIVE_L},
  {"-lv", "--" BACKUP_LEVEL_L},
  {"-o", "--" BACKUP_OUTPUT_FILE_L},
  {"-sa", "--" BACKUP_SA_MODE_L},
  {"-cs", "--" BACKUP_CS_MODE_L},
  {"-nc", "--" BACKUP_NO_CHECK_L},
  {"-mt", "--" BACKUP_THREAD_COUNT_L},
  {"-zip", "--" BACKUP_COMPRESS_L},
  {"-nozip", "--" BACKUP_NO_COMPRESS_L},
  {"-ni", "--" BACKUP_EXCEPT_ACTIVE_LOG_L},
  {"-c", (char *) -1},
  {0, 0}
};

static ARG_MAP_TABLE ua_Restore_map[] = {
  {"-d", "--" RESTORE_UP_TO_DATE_L},
  {"-t", "--" RESTORE_LIST_L},
  {"-l", "--" RESTORE_BACKUP_FILE_PATH_L},
  {"-lv", "--" RESTORE_LEVEL_L},
  {"-p", "--" RESTORE_PARTIAL_RECOVERY_L},
  {"-o", "--" RESTORE_OUTPUT_FILE_L},
  {"-n", "--" RESTORE_USE_DATABASE_LOCATION_PATH_L},
  {0, 0}
};

static ARG_MAP_TABLE ua_Addvol_map[] = {
  {"-n", "--" ADDVOL_VOLUME_NAME_L},
  {"-f", "--" ADDVOL_FILE_PATH_L},
  {"-c", "--" ADDVOL_COMMENT_L},
  {"-pu", "--" ADDVOL_PURPOSE_L},
  {"-sa", "--" ADDVOL_SA_MODE_L},
  {"-cs", "--" ADDVOL_CS_MODE_L},
  {0, 0}
};

#if 0
static ARG_MAP_TABLE ua_Delvol_map[] = {
  {"-i", "--" DELVOL_VOLUME_ID_L},
  {"-f", "--" DELVOL_FORCE_L},
  {"-c", "--" DELVOL_CLEAR_CACHE_L},
  {"-p", "--" DELVOL_DBA_PASSWORD_L},
  {"-sa", "--" DELVOL_SA_MODE_L},
  {"-cs", "--" DELVOL_CS_MODE_L},
  {0, 0}
};
#endif

static ARG_MAP_TABLE ua_Space_map[] = {
  {"-o", "--" SPACE_OUTPUT_FILE_L},
  {"-sa", "--" SPACE_SA_MODE_L},
  {"-cs", "--" SPACE_CS_MODE_L},
  {0, 0}
};

static ARG_MAP_TABLE ua_Lock_map[] = {
  {"-o", "--" LOCK_OUTPUT_FILE_L},
  {0, 0}
};

static ARG_MAP_TABLE ua_Optimize_map[] = {
  {"-c", "--" OPTIMIZE_CLASS_NAME_L},
  {0, 0}
};

static ARG_MAP_TABLE ua_Install_map[] = {
  {"-s", "--" INSTALL_SERVER_NAME_L},
  {"-f", "--" INSTALL_FILE_PATH_L},
  {"-l", "--" INSTALL_LOG_PATH_L},
  {0, 0}
};

static ARG_MAP_TABLE ua_Diag_map[] = {
  {"-d", "--" DIAG_DUMP_TYPE_L},
  {0, 0}
};

static ARG_MAP_TABLE ua_Check_map[] = {
  {"-sa", "--" CHECK_SA_MODE_L},
  {"-cs", "--" CHECK_CS_MODE_L},
  {"-r", "--" CHECK_REPAIR_L},
  {0, 0}
};

static ARG_MAP_TABLE ua_Tranlist_map[] = {
#if defined(NEED_PRIVILEGE_PASSWORD)
  {"-u", "--" TRANLIST_USER_L},
  {"-p", "--" TRANLIST_PASSWORD_L},
#endif
  {"-s", "--" TRANLIST_SUMMARY_L},
  {"-k", "--" TRANLIST_SORT_KEY_L},
  {"-r", "--" TRANLIST_REVERSE_L},
  {0, 0}
};

static ARG_MAP_TABLE ua_Killtran_map[] = {
  {"-t", "--" KILLTRAN_KILL_TRANSACTION_INDEX_L},
  {"-u", "--" KILLTRAN_KILL_USER_NAME_L},
  {"-h", "--" KILLTRAN_KILL_HOST_NAME_L},
  {"-pg", "--" KILLTRAN_KILL_PROGRAM_NAME_L},
  {"-s", "--" KILLTRAN_KILL_SQL_ID_L},
  {"-p", "--" KILLTRAN_DBA_PASSWORD_L},
  {"-d", "--" KILLTRAN_DISPLAY_INFORMATION_L},
  {"-q", "--" KILLTRAN_DISPLAY_QUERY_INFO_L},
  {"-v", "--" KILLTRAN_FORCE_L},
  {0, 0}
};

static ARG_MAP_TABLE ua_Load_map[] = {
  {"-u", "--" LOAD_USER_L},
  {"-p", "--" LOAD_PASSWORD_L},
  {"-s", "--" LOAD_CHECK_ONLY_L},
  {"-l", "--" LOAD_LOAD_ONLY_L},
  {"-e", "--" LOAD_ESTIMATED_SIZE_L},
  {"-v", "--" LOAD_VERBOSE_L},
  {"-ns", "--" LOAD_NO_STATISTICS_L},
  {"-c", "--" LOAD_PERIODIC_COMMIT_L},
  {"-noref", "--" LOAD_NO_OID_L},
  {"-sf", "--" LOAD_SCHEMA_FILE_L},
  {"-if", "--" LOAD_INDEX_FILE_L},
  {"-tf", "--" LOAD_TRIGGER_FILE_L},
  {"-of", "--" LOAD_DATA_FILE_L},
  {"-ef", "--" LOAD_ERROR_CONTROL_FILE_L},
  {"-nl", "--" LOAD_IGNORE_LOGGING_L},
  {"-vc", (char *) -1},
  {0, 0}
};

static ARG_MAP_TABLE ua_Unload_map[] = {
  {"-i", "--" UNLOAD_INPUT_CLASS_FILE_L},
  {"-ir", "--" UNLOAD_INCLUDE_REFERENCE_L},
  {"-io", "--" UNLOAD_INPUT_CLASS_ONLY_L},
  {"-loc", "--" UNLOAD_LO_COUNT_L},
  {"-e", "--" UNLOAD_ESTIMATED_SIZE_L},
  {"-n", "--" UNLOAD_CACHED_PAGES_L},
  {"-od", "--" UNLOAD_OUTPUT_PATH_L},
  {"-so", "--" UNLOAD_SCHEMA_ONLY_L},
  {"-oo", "--" UNLOAD_DATA_ONLY_L},
  {"-li", "--" UNLOAD_LATEST_IMAGE_L},
  {"-p", "--" UNLOAD_OUTPUT_PREFIX_L},
  {"-f", "--" UNLOAD_HASH_FILE_L},
  {"-v", "--" UNLOAD_VERBOSE_L},
  {"-di", "--" UNLOAD_USE_DELIMITER_L},
  {"-sa", "--" UNLOAD_SA_MODE_L},
  {"-cs", "--" UNLOAD_CS_MODE_L},
  {"-dpc", "--" UNLOAD_DATAFILE_PER_CLASS_L},
  {0, 0}
};

static ARG_MAP_TABLE ua_Compact_map[] = {
  {"-v", "--" COMPACT_VERBOSE_L},
  {0, 0}
};

static ARG_MAP_TABLE ua_Sqlx_map[] = {
  {"-sa", "--" CSQL_SA_MODE_L},
  {"-cs", "--" CSQL_CS_MODE_L},
  {"-u", "--" CSQL_USER_L},
  {"-p", "--" CSQL_PASSWORD_L},
  {"-e", "--" CSQL_ERROR_CONTINUE_L},
  {"-i", "--" CSQL_INPUT_FILE_L},
  {"-o", "--" CSQL_OUTPUT_FILE_L},
  {"-s", "--" CSQL_SINGLE_LINE_L},
  {"-c", "--" CSQL_COMMAND_L},
  {"-lo", "--" CSQL_LINE_OUTPUT_L},
  {"-noac", "--" CSQL_NO_AUTO_COMMIT_L},
  {"-nopager", "--" CSQL_NO_PAGER_L},
  {"-nosl", "--" CSQL_NO_SINGLE_LINE_L},
  {"-co", (char *) -1},
  {0, 0}
};

static ARG_MAP_TABLE us_Commdb_map[] = {
  {"-P", "--" COMMDB_SERVER_LIST_L},
  {"-O", "--" COMMDB_ALL_LIST_L},
  {"-S", "--" COMMDB_SHUTDOWN_SERVER_L},
  {"-I", "--" COMMDB_SHUTDOWN_SERVER_L},
  {"-A", "--" COMMDB_SHUTDOWN_ALL_L},
  {"-h", "--" COMMDB_HOST_L},
  {0, 0}
};

static ARG_MAP_TABLE ua_Acldb_map[] = {
  {"-r", "--" ACLDB_RELOAD_L},
  {0, 0}
};

typedef struct
{
  const char *app_name;
  const char *util_name;
  ARG_MAP_TABLE *match_table;
} UTIL_MAP_TABLE;

UTIL_MAP_TABLE ua_Util_table[] = {
  {"createdb", UTIL_OPTION_CREATEDB, ua_Create_map},
  {"renamedb", UTIL_OPTION_RENAMEDB, ua_Rename_map},
  {"copydb", UTIL_OPTION_COPYDB, ua_Copy_map},
  {"deletedb", UTIL_OPTION_DELETEDB, ua_Delete_map},
  {"backupdb", UTIL_OPTION_BACKUPDB, ua_Backup_map},
  {"restoredb", UTIL_OPTION_RESTOREDB, ua_Restore_map},
  {"addvoldb", UTIL_OPTION_ADDVOLDB, ua_Addvol_map},
#if 0
  {"delvoldb", UTIL_OPTION_DELVOLDB, ua_Delvol_map},
#endif
  {"spacedb", UTIL_OPTION_SPACEDB, ua_Space_map},
  {"lockdb", UTIL_OPTION_LOCKDB, ua_Lock_map},
  {"optimizedb", UTIL_OPTION_OPTIMIZEDB, ua_Optimize_map},
  {"installdb", UTIL_OPTION_INSTALLDB, ua_Install_map},
  {"diagdb", UTIL_OPTION_DIAGDB, ua_Diag_map},
  {"checkdb", UTIL_OPTION_CHECKDB, ua_Check_map},
  {"killtran", UTIL_OPTION_KILLTRAN, ua_Killtran_map},
  {"loaddb", UTIL_OPTION_LOADDB, ua_Load_map},
  {"unloaddb", UTIL_OPTION_UNLOADDB, ua_Unload_map},
  {"compactdb", UTIL_OPTION_COMPACTDB, ua_Compact_map},
  {"acldb", UTIL_OPTION_ACLDB, ua_Acldb_map},
  {"tranlist", UTIL_OPTION_TRANLIST, ua_Tranlist_map},
  {0, 0, 0}
};

/*
 * get_long_arg_by_old_arg() - get new argument with long format
 *   return: argument string with long format
 *   old_arg(in): old argument string
 *   match_table(in): a mapping table of arguments
 */
static char *
get_long_arg_by_old_arg (char *old_arg, ARG_MAP_TABLE * match_table)
{
  int i;
  for (i = 0; match_table[i].old_arg != 0; i++)
    {
      if (strcmp (old_arg, match_table[i].old_arg) == 0)
	{
	  return (char *) match_table[i].long_arg;
	}
    }
  return NULL;
}

/*
 * make_argv() - convert old arguments to new arguments with long format
 *   return:
 *   argc(in): size of the array
 *   argv(out): an array of new arguments
 *   match_table(in): a mapping table of arguments
 */
static int
convert_argv (int argc, char **argv, ARG_MAP_TABLE * match_table)
{
  int i, idx;
  char *new_arg;

  for (i = 0, idx = 0; i < argc && argv[i] != NULL; i++)
    {
      new_arg = get_long_arg_by_old_arg (argv[i], match_table);
      if (new_arg == NULL && argv[i][0] == '-')
	{
	  fprintf (stderr, "invalid option: %s\n", argv[i]);
	  return ER_FAILED;
	}
      else if (new_arg == (char *) -1)
	{
	  continue;
	}
      else if (new_arg != NULL)
	{
	  argv[idx++] = new_arg;
	}
      else
	{
	  argv[idx++] = argv[i];
	}
    }

  argv[idx] = 0;
  return NO_ERROR;
}

/*
 * main() - csql main module.
 *   return: EXIT_SUCCESS or EXIT_FAILURE
 */
int
main (int argc, char **argv)
{
  char *program_name = basename (argv[0]);
  int i, argc_index = 0;
  char **admin_argv = NULL;

  /* validate the number of arguments to avoid klocwork's error message */
  if (argc < 0 || argc > 1024)
    {
      goto failure;
    }

  if (strcmp (program_name, UTIL_SQLX_NAME) == 0)
    {
      admin_argv = (char **) malloc ((argc + 1) * sizeof (char *));
      if (admin_argv == NULL)
	{
	  goto failure;
	}

      memcpy (admin_argv, argv, argc * sizeof (char *));
      if (convert_argv (argc, &admin_argv[1], ua_Sqlx_map) != NO_ERROR)
	{
	  goto failure;
	}

      if (execvp (UTIL_CSQL_NAME, admin_argv) == -1)
	{
	  perror ("execvp");
	}
    }
  else if (strcmp (program_name, UTIL_OLD_COMMDB_NAME) == 0)
    {
      admin_argv = (char **) malloc ((argc + 1) * sizeof (char *));
      if (admin_argv == NULL)
	{
	  goto failure;
	}

      memcpy (admin_argv, argv, argc * sizeof (char *));
      if (convert_argv (argc, &admin_argv[1], us_Commdb_map) != NO_ERROR)
	{
	  goto failure;
	}

      if (execvp (UTIL_COMMDB_NAME, argv) == -1)
	{
	  perror ("execvp");
	}
    }
  else
    {
      for (i = 0; ua_Util_table[i].app_name != 0; i++)
	{
	  if (strcmp (ua_Util_table[i].app_name, program_name) == 0)
	    {
	      admin_argv = (char **) malloc ((argc + 2) * sizeof (char *));
	      if (admin_argv == NULL)
		{
		  goto failure;
		}

	      admin_argv[argc_index++] = (char *) UTIL_CUBRID;
	      admin_argv[argc_index++] = (char *) ua_Util_table[i].util_name;
	      memcpy (&admin_argv[argc_index], &argv[1], (argc - 1) * sizeof (char *));
	      if (convert_argv (argc, &admin_argv[argc_index], ua_Util_table[i].match_table) != NO_ERROR)
		{
		  goto failure;
		}
	      break;
	    }
	}

      if (argc_index == 0)
	{
	  goto failure;
	}

      if (execvp (UTIL_ADMIN_NAME, admin_argv) == -1)
	{
	  perror ("execvp");
	}
    }

  if (admin_argv)
    {
      free (admin_argv);
    }
  return EXIT_SUCCESS;

failure:
  fprintf (stderr, "This utility is deprecated. Use 'cubrid' utility.\n");
  if (admin_argv)
    {
      free (admin_argv);
    }
  return EXIT_FAILURE;
}
