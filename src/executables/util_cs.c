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
 * util_cs.c : Implementations of utilities that operate in both
 *             client/server and standalone modes.
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>

#include "utility.h"
#include "error_manager.h"
#include "message_catalog.h"
#include "system_parameter.h"
#include "environment_variable.h"
#include "databases_file.h"
#include "boot_cl.h"
#include "boot_sr.h"
#include "db.h"
#include "authenticate.h"
#include "server_interface.h"
#include "object_representation.h"
#include "transaction_cl.h"
#include "porting.h"
#include "network_interface_cl.h"
#include "connection_defs.h"
#include "log_writer.h"
#include "log_applier.h"
#include "log_lsa.hpp"
#include "schema_manager.h"
#include "locator_cl.h"
#include "dynamic_array.h"
#include "util_func.h"
#include "xasl.h"
#include "log_volids.hpp"
#include "tde.h"
#include "flashback_cl.h"
#include "connection_support.h"
#include "memory_monitor_cl.hpp"
#if !defined(WINDOWS)
#include "heartbeat.h"
#endif
#include "network_histogram.hpp"

#define PASSBUF_SIZE 12
#define SPACEDB_NUM_VOL_PURPOSE 2
#define MAX_KILLTRAN_INDEX_LIST_NUM  64
#define MAX_DELVOL_ID_LIST_NUM       64

#define VOL_PURPOSE_STRING(VOL_PURPOSE)		\
	    ((VOL_PURPOSE == DB_PERMANENT_DATA_PURPOSE) ? "PERMANENT DATA"	\
	    : (VOL_PURPOSE == DB_TEMPORARY_DATA_PURPOSE) ? "TEMPORARY DATA"     \
	    : "UNKNOWN")

#if defined(WINDOWS)
#define STDIN_FILENO  _fileno (stdin)
#endif

typedef enum
{
  SPACEDB_SIZE_UNIT_PAGE = 0,
  SPACEDB_SIZE_UNIT_MBYTES,
  SPACEDB_SIZE_UNIT_GBYTES,
  SPACEDB_SIZE_UNIT_TBYTES,
  SPACEDB_SIZE_UNIT_HUMAN_READABLE
} T_SPACEDB_SIZE_UNIT;

typedef enum
{
  TRANDUMP_SUMMARY,
  TRANDUMP_QUERY_INFO,
  TRANDUMP_FULL_INFO
} TRANDUMP_LEVEL;

typedef enum
{
  SORT_COLUMN_TYPE_INT,
  SORT_COLUMN_TYPE_FLOAT,
  SORT_COLUMN_TYPE_STR,
} SORT_COLUMN_TYPE;

static int tranlist_Sort_column = 0;
static bool tranlist_Sort_desc = false;

static bool is_Sigint_caught = false;
#if defined(WINDOWS)
static BOOL WINAPI intr_handler (int sig_no);
#else
static void intr_handler (int sig_no);
#endif

static void backupdb_sig_interrupt_handler (int sig_no);
STATIC_INLINE char *spacedb_get_size_str (char *buf, UINT64 num_pages, T_SPACEDB_SIZE_UNIT size_unit);
static void print_timestamp (FILE * outfp);
static int print_tran_entry (const ONE_TRAN_INFO * tran_info, TRANDUMP_LEVEL dump_level, bool full_sqltext);
static int tranlist_cmp_f (const void *p1, const void *p2);
static OID *util_get_class_oids_and_index_btid (dynamic_array * darray, const char *index_name, BTID * index_btid);

/*
 * backupdb() - backupdb main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
backupdb (UTIL_FUNCTION_ARG * arg)
{
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *database_name;
  const char *backup_path = NULL;
  bool remove_log_archives = false;
  int backup_level = FILEIO_BACKUP_FULL_LEVEL;
  const char *backup_verbose_file = NULL;
  bool no_check = false;
  bool check = true;
  int backup_num_threads;
  bool no_compress_flag;
  bool sa_mode;
  bool separate_keys;
  FILEIO_ZIP_METHOD backup_zip_method = FILEIO_ZIP_LZ4_METHOD;
  FILEIO_ZIP_LEVEL backup_zip_level = FILEIO_ZIP_LZ4_DEFAULT_LEVEL;
  bool skip_activelog = false;
  int sleep_msecs;
  struct stat st_buf;
  char real_pathbuf[PATH_MAX];
  char verbose_file_realpath[PATH_MAX];

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_backup_usage;
    }

  database_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_backup_usage;
    }

  backup_path = utility_get_option_string_value (arg_map, BACKUP_DESTINATION_PATH_S, 0);
  remove_log_archives = utility_get_option_bool_value (arg_map, BACKUP_REMOVE_ARCHIVE_S);
  backup_level = utility_get_option_int_value (arg_map, BACKUP_LEVEL_S);
  backup_verbose_file = utility_get_option_string_value (arg_map, BACKUP_OUTPUT_FILE_S, 0);
  no_check = utility_get_option_bool_value (arg_map, BACKUP_NO_CHECK_S);
  check = !no_check;
  backup_num_threads = utility_get_option_int_value (arg_map, BACKUP_THREAD_COUNT_S);
  no_compress_flag = utility_get_option_bool_value (arg_map, BACKUP_NO_COMPRESS_S);

  // BACKUP_EXCEPT_ACTIVE_LOG_S is obsoleted. This means backup will always include active log.
  skip_activelog = false;

  sleep_msecs = utility_get_option_int_value (arg_map, BACKUP_SLEEP_MSECS_S);
  sa_mode = utility_get_option_bool_value (arg_map, BACKUP_SA_MODE_S);
  separate_keys = utility_get_option_bool_value (arg_map, BACKUP_SEPARATE_KEYS_S);

  /* Range checking of input */
  if (backup_level < 0 || backup_level >= FILEIO_BACKUP_UNDEFINED_LEVEL)
    {
      goto print_backup_usage;
    }

  if (sa_mode && backup_num_threads > 1)
    {
      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_BACKUPDB,
					     BACKUPDB_INVALID_THREAD_NUM_OPT));
    }

  if (backup_num_threads < FILEIO_BACKUP_NUM_THREADS_AUTO)
    {
      goto print_backup_usage;
    }

  if (sleep_msecs < FILEIO_BACKUP_SLEEP_MSECS_AUTO)
    {
      goto print_backup_usage;
    }

  if (no_compress_flag)
    {
      backup_zip_method = FILEIO_ZIP_NONE_METHOD;
      backup_zip_level = FILEIO_ZIP_NONE_LEVEL;
    }

  /* extra validation */
  if (check_database_name (database_name))
    {
      goto error_exit;
    }

  if (backup_path != NULL)
    {
      memset (real_pathbuf, 0, sizeof (real_pathbuf));
      if (realpath (backup_path, real_pathbuf) != NULL)
	{
	  backup_path = real_pathbuf;
	}

      // accept directory and FIFO (named pipe) file as backup destination.
      if (stat (backup_path, &st_buf) != 0)
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_BACKUPDB,
						 BACKUPDB_INVALID_PATH));
	  goto error_exit;
	}
      else if (!S_ISDIR (st_buf.st_mode))
	{
#if !defined (WINDOWS)
	  // Unfortunately, Windows does not support FIFO file.
	  if (!S_ISFIFO (st_buf.st_mode))
#endif /* !WINDOWS */
	    {
	      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_BACKUPDB,
						     BACKUPDB_INVALID_PATH));
	      goto error_exit;
	    }
#if !defined (WINDOWS)
	  else if (separate_keys)	/* FIFO file and --separate_keys is exclusive */
	    {
	      PRINT_AND_LOG_ERR_MSG (msgcat_message
				     (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_BACKUPDB,
				      BACKUPDB_FIFO_KEYS_NOT_SUPPORTED));
	      goto error_exit;
	    }
#endif /* !WINDOWS */
	}
    }

  if (separate_keys)
    {
      util_log_write_warnstr (msgcat_message
			      (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_BACKUPDB, BACKUPDB_USING_SEPARATE_KEYS));
    }
  else
    {
      util_log_write_warnstr (msgcat_message
			      (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_BACKUPDB, BACKUPDB_NOT_USING_SEPARATE_KEYS));
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  sysprm_set_force (prm_get_name (PRM_ID_JAVA_STORED_PROCEDURE), "no");

  AU_DISABLE_PASSWORDS ();	/* disable authorization for this operation */
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("DBA", NULL);

  if (db_restart (arg->command_name, TRUE, database_name) != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }

  if (check)
    {
      int check_flag = 0;

      check_flag |= CHECKDB_FILE_TRACKER_CHECK;
      check_flag |= CHECKDB_HEAP_CHECK_ALLHEAPS;
      check_flag |= CHECKDB_CT_CHECK_CAT_CONSISTENCY;
      check_flag |= CHECKDB_BTREE_CHECK_ALL_BTREES;
      check_flag |= CHECKDB_LC_CHECK_CLASSNAMES;

      if (db_set_isolation (TRAN_READ_COMMITTED) != NO_ERROR
	  || boot_check_db_consistency (check_flag, 0, 0, NULL) != NO_ERROR)
	{
	  const char *tmpname;

	  tmpname = er_get_msglog_filename ();
	  if (tmpname == NULL)
	    {
	      tmpname = "/dev/null";
	    }
	  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CHECKDB,
						 CHECKDB_MSG_INCONSISTENT), tmpname);
	  db_shutdown ();
	  goto error_exit;
	}
    }

  /* some other utilities may need interrupt handler too */
  if (os_set_signal_handler (SIGINT, backupdb_sig_interrupt_handler) == SIG_ERR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      db_shutdown ();
      goto error_exit;
    }

  if (backup_verbose_file && *backup_verbose_file && *backup_verbose_file != '/')
    {
      char dirname[PATH_MAX];

      /* resolve relative path */
      if (getcwd (dirname, PATH_MAX) != NULL)
	{
	  if (snprintf (verbose_file_realpath, PATH_MAX - 1, "%s/%s", dirname, backup_verbose_file) < 0)
	    {
	      assert (false);
	      db_shutdown ();
	      goto error_exit;
	    }
	  backup_verbose_file = verbose_file_realpath;
	}
    }

  if (boot_backup (backup_path, (FILEIO_BACKUP_LEVEL) backup_level, remove_log_archives, backup_verbose_file,
		   backup_num_threads, backup_zip_method, backup_zip_level, skip_activelog, sleep_msecs,
		   separate_keys) != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      db_shutdown ();
      goto error_exit;
    }

  if (db_commit_transaction () != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
    }

  db_shutdown ();

  return EXIT_SUCCESS;

print_backup_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_BACKUPDB, BACKUPDB_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

error_exit:
  return EXIT_FAILURE;
}

/*
 * addvoldb() - addvoldb main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
addvoldb (UTIL_FUNCTION_ARG * arg)
{
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *database_name;
  UINT64 volext_size;
  UINT64 volext_max_writesize;
  const char *volext_string_purpose = NULL;
  const char *volext_npages_string = NULL;
  const char *volext_size_str = NULL;
  const char *volext_max_writesize_in_sec_str = NULL;
  char real_volext_path_buf[PATH_MAX];
  bool sa_mode;
  DBDEF_VOL_EXT_INFO ext_info;

  ext_info.overwrite = false;
  ext_info.max_npages = 0;
  if (utility_get_option_string_table_size (arg_map) < 1)
    {
      goto print_addvol_usage;
    }

  database_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_addvol_usage;
    }

  volext_npages_string = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 1);
  if (volext_npages_string)
    {
      util_print_deprecated ("number-of-pages");
      ext_info.max_npages = atoi (volext_npages_string);
    }

  volext_size = 0;
  volext_size_str = utility_get_option_string_value (arg_map, ADDVOL_VOLUME_SIZE_S, 0);
  if (volext_size_str)
    {
      if (util_size_string_to_byte (&volext_size, volext_size_str) != NO_ERROR)
	{
	  goto print_addvol_usage;
	}
    }

  volext_max_writesize_in_sec_str = utility_get_option_string_value (arg_map, ADDVOL_MAX_WRITESIZE_IN_SEC_S, 0);
  if (volext_max_writesize_in_sec_str)
    {
      if (util_size_string_to_byte (&volext_max_writesize, volext_max_writesize_in_sec_str) != NO_ERROR)
	{
	  goto print_addvol_usage;
	}
      ext_info.max_writesize_in_sec = (int) volext_max_writesize / ONE_K;
    }
  else
    {
      ext_info.max_writesize_in_sec = 0;
    }

  ext_info.name = utility_get_option_string_value (arg_map, ADDVOL_VOLUME_NAME_S, 0);
  ext_info.path = utility_get_option_string_value (arg_map, ADDVOL_FILE_PATH_S, 0);
  if (ext_info.path != NULL)
    {
      memset (real_volext_path_buf, 0, sizeof (real_volext_path_buf));
      if (realpath (ext_info.path, real_volext_path_buf) != NULL)
	{
	  ext_info.path = real_volext_path_buf;
	}
    }
  ext_info.comments = utility_get_option_string_value (arg_map, ADDVOL_COMMENT_S, 0);
  volext_string_purpose = utility_get_option_string_value (arg_map, ADDVOL_PURPOSE_S, 0);
  if (volext_string_purpose == NULL)
    {
      volext_string_purpose = "generic";
    }

  ext_info.purpose = DB_PERMANENT_DATA_PURPOSE;

  if (strcasecmp (volext_string_purpose, "data") == 0)
    {
      ext_info.purpose = DB_PERMANENT_DATA_PURPOSE;
    }
  else if (strcasecmp (volext_string_purpose, "index") == 0)
    {
      ext_info.purpose = DB_PERMANENT_DATA_PURPOSE;
    }
  else if (strcasecmp (volext_string_purpose, "temp") == 0)
    {
      ext_info.purpose = DB_TEMPORARY_DATA_PURPOSE;
    }
  else if (strcasecmp (volext_string_purpose, "generic") == 0)
    {
      ext_info.purpose = DB_PERMANENT_DATA_PURPOSE;
    }
  else
    {
      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_ADDVOLDB, ADDVOLDB_MSG_BAD_PURPOSE),
			     volext_string_purpose);

      goto error_exit;
    }

  sa_mode = utility_get_option_bool_value (arg_map, ADDVOL_SA_MODE_S);
  if (sa_mode && ext_info.max_writesize_in_sec > 0)
    {
      ext_info.max_writesize_in_sec = 0;
      fprintf (stderr,
	       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_ADDVOLDB, ADDVOLDB_INVALID_MAX_WRITESIZE_IN_SEC));
    }

  /* extra validation */
  if (check_database_name (database_name) || check_volume_name (ext_info.name))
    {
      goto error_exit;
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  /* tuning system parameters */
  sysprm_set_force (prm_get_name (PRM_ID_PB_NBUFFERS), "1024");
  sysprm_set_force (prm_get_name (PRM_ID_JAVA_STORED_PROCEDURE), "no");

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("DBA", NULL);
  if (db_restart (arg->command_name, TRUE, database_name) == NO_ERROR)
    {
      if (volext_size == 0)
	{
	  volext_size = prm_get_bigint_value (PRM_ID_DB_VOLUME_SIZE);
	}

      if (ext_info.max_npages == 0)
	{
	  ext_info.max_npages = (int) (volext_size / IO_PAGESIZE);
	}

      if (ext_info.max_npages <= 0)
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_ADDVOLDB,
						 ADDVOLDB_MSG_BAD_NPAGES), ext_info.max_npages);
	  db_shutdown ();
	  goto error_exit;
	}

      if (db_add_volume_ex (&ext_info) == NO_ERROR)
	{
	  db_commit_transaction ();
	}
      else
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
	  db_shutdown ();
	  goto error_exit;
	}
      db_shutdown ();
    }
  else
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }

  return EXIT_SUCCESS;

print_addvol_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_ADDVOLDB, ADDVOLDB_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
error_exit:
  return EXIT_FAILURE;
}

/*
 * util_get_class_oids_and_index_btid() -
 *   return: OID array/NULL
 *   index_name(in)
 *   index_btid(out)
 */
static OID *
util_get_class_oids_and_index_btid (dynamic_array * darray, const char *index_name, BTID * index_btid)
{
  MOP cls_mop;
  OID *oids;
  OID *cls_oid;
  SM_CLASS *cls_sm;
  SM_CLASS_CONSTRAINT *constraint;
  char table[SM_MAX_IDENTIFIER_LENGTH];
  char name[SM_MAX_IDENTIFIER_LENGTH];
  int i;
  int num_tables = da_size (darray);
  MOBJ *obj;

  oids = (OID *) malloc (sizeof (OID) * num_tables);
  if (oids == NULL)
    {
      perror ("malloc");
      util_log_write_errid (MSGCAT_UTIL_GENERIC_NO_MEM);
      return NULL;
    }

  for (i = 0; i < num_tables; i++)
    {
      const char *class_name_p = NULL;
      const char *class_name_only = NULL;
      char owner_name[DB_MAX_USER_LENGTH] = { '\0' };

      if (da_get (darray, i, table) != NO_ERROR)
	{
	  free (oids);
	  return NULL;
	}

      OID_SET_NULL (&oids[i]);
      if (table == NULL || table[0] == '\0')
	{
	  continue;
	}

      sm_qualifier_name (table, owner_name, DB_MAX_USER_LENGTH);
      class_name_only = sm_remove_qualifier_name (table);
      if (strcasecmp (owner_name, "dba") == 0 && sm_check_system_class_by_name (class_name_only))
	{
	  class_name_p = class_name_only;
	}
      else
	{
	  class_name_p = table;
	}

      sm_user_specified_name (class_name_p, name, SM_MAX_IDENTIFIER_LENGTH);
      cls_mop = locator_find_class (name);

      obj = (MOBJ *) & cls_sm;
      ws_find (cls_mop, obj);
      if (cls_sm == NULL || cls_sm->class_type != SM_CLASS_CT)
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CHECKDB,
						 CHECKDB_MSG_NO_SUCH_CLASS), table);
	  continue;
	}

      cls_oid = ws_oid (cls_mop);
      if (cls_oid)
	{
	  oids[i] = *cls_oid;
	}
      else
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CHECKDB,
						 CHECKDB_MSG_NO_SUCH_CLASS), table);
	  continue;
	}

      if (index_name != NULL)
	{
	  constraint = classobj_find_class_index (cls_sm, index_name);
	  if (constraint == NULL)
	    {
	      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CHECKDB,
						     CHECKDB_MSG_NO_SUCH_INDEX), index_name, name);
	      free_and_init (oids);
	      return NULL;
	    }

	  db_constraint_index (constraint, index_btid);

	  assert (i == 0);
	}
    }

  return oids;
}

/*
 * checkdb() - checkdb main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
checkdb (UTIL_FUNCTION_ARG * arg)
{
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *database_name;
  char *fname;
  char *index_name = NULL;
  bool repair = false;
  bool repair_plink = false;
  int flag = 0;
  int i, num_tables;
  OID *oids = NULL;
  BTID index_btid;
  dynamic_array *darray = NULL;

  if (utility_get_option_string_table_size (arg_map) < 1)
    {
      goto print_check_usage;
    }

  database_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_check_usage;
    }

  /* extra validation */
  if (check_database_name (database_name))
    {
      goto error_exit;
    }

  if (utility_get_option_bool_value (arg_map, CHECK_FILE_TRACKER_S))
    {
      flag |= CHECKDB_FILE_TRACKER_CHECK;
    }

  if (utility_get_option_bool_value (arg_map, CHECK_HEAP_ALLHEAPS_S))
    {
      flag |= CHECKDB_HEAP_CHECK_ALLHEAPS;
    }

  if (utility_get_option_bool_value (arg_map, CHECK_CAT_CONSISTENCY_S))
    {
      flag |= CHECKDB_CT_CHECK_CAT_CONSISTENCY;
    }

  if (utility_get_option_bool_value (arg_map, CHECK_BTREE_ALL_BTREES_S))
    {
      flag |= CHECKDB_BTREE_CHECK_ALL_BTREES;
    }

  if (utility_get_option_bool_value (arg_map, CHECK_LC_CLASSNAMES_S))
    {
      flag |= CHECKDB_LC_CHECK_CLASSNAMES;
    }

  if (utility_get_option_bool_value (arg_map, CHECK_LC_ALLENTRIES_OF_ALLBTREES_S))
    {
      flag |= CHECKDB_LC_CHECK_ALLENTRIES_OF_ALLBTREES;
    }

  repair_plink = utility_get_option_bool_value (arg_map, CHECK_REPAIR_PREV_LINK_S);
  if (repair_plink)
    {
      flag |= CHECKDB_REPAIR_PREV_LINK;
    }
  else if (utility_get_option_bool_value (arg_map, CHECK_CHECK_PREV_LINK_S))
    {
      flag |= CHECKDB_CHECK_PREV_LINK;
    }

  if (flag == 0)
    {
      flag = CHECKDB_ALL_CHECK_EXCEPT_PREV_LINK;
    }

  repair = utility_get_option_bool_value (arg_map, CHECK_REPAIR_S);
  if (repair)
    {
      flag |= CHECKDB_REPAIR;
    }

  fname = utility_get_option_string_value (arg_map, CHECK_INPUT_FILE_S, 0);
  index_name = utility_get_option_string_value (arg_map, CHECK_INDEXNAME_S, 0);
  num_tables = utility_get_option_string_table_size (arg_map);
  num_tables -= 1;

  if (index_name != NULL && num_tables != 1)
    {
      PRINT_AND_LOG_ERR_MSG ("Only one table is supported to check specific index.\n");
      goto error_exit;
    }

  darray = da_create (num_tables, SM_MAX_IDENTIFIER_LENGTH);
  if (darray == NULL)
    {
      perror ("calloc");
      util_log_write_errid (MSGCAT_UTIL_GENERIC_NO_MEM);
      goto error_exit;
    }

  if (num_tables > 0)
    {
      char class_name_buf[SM_MAX_IDENTIFIER_LENGTH];
      char *class_name;

      for (i = 0; i < num_tables; i++)
	{
	  class_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, i + 1);
	  if (class_name == NULL)
	    {
	      continue;
	    }

	  if (utility_check_class_name (class_name) != NO_ERROR)
	    {
	      goto error_exit;
	    }

	  strncpy_bufsize (class_name_buf, class_name);

	  if (da_add (darray, class_name_buf) != NO_ERROR)
	    {
	      util_log_write_errid (MSGCAT_UTIL_GENERIC_NO_MEM);
	      perror ("calloc");
	      goto error_exit;
	    }
	}
    }

  if (fname != NULL)
    {
      if (util_get_table_list_from_file (fname, darray) != NO_ERROR)
	{
	  goto error_exit;
	}
    }

  num_tables = da_size (darray);

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  sysprm_set_force (prm_get_name (PRM_ID_JAVA_STORED_PROCEDURE), "no");

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("DBA", NULL);
  if (db_restart (arg->command_name, TRUE, database_name) == NO_ERROR)
    {
      if (num_tables > 0)
	{
	  BTID_SET_NULL (&index_btid);
	  oids = util_get_class_oids_and_index_btid (darray, index_name, &index_btid);
	  if (oids == NULL)
	    {
	      db_shutdown ();
	      goto error_exit;
	    }
	}

      if (db_set_isolation (TRAN_READ_COMMITTED) != NO_ERROR
	  || boot_check_db_consistency (flag, oids, num_tables, &index_btid) != NO_ERROR)
	{
	  const char *tmpname;

	  if ((tmpname = er_get_msglog_filename ()) == NULL)
	    {
	      tmpname = "/dev/null";
	    }
	  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CHECKDB,
						 CHECKDB_MSG_INCONSISTENT), tmpname);

	  if (repair_plink || repair)
	    {
	      db_commit_transaction ();
	    }
	  util_log_write_errstr ("%s\n", db_error_string (3));
	  db_shutdown ();
	  goto error_exit;
	}
      if (repair_plink || repair)
	{
	  db_commit_transaction ();
	}
      db_shutdown ();
    }
  else
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }

  if (darray != NULL)
    {
      da_destroy (darray);
    }
  if (oids != NULL)
    {
      free (oids);
    }

  return EXIT_SUCCESS;

print_check_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CHECKDB, CHECKDB_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

error_exit:
  if (darray != NULL)
    {
      da_destroy (darray);
    }
  if (oids != NULL)
    {
      free (oids);
    }

  return EXIT_FAILURE;
}


/*
 * spacedb() - spacedb main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
spacedb (UTIL_FUNCTION_ARG * arg)
{
#define SPACEDB_TO_SIZE_ARG(no, npage) spacedb_get_size_str (size_str_##no, npage, size_unit_type)

  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *database_name;
  const char *output_file = NULL;
  int i;
  const char *size_unit;
  T_SPACEDB_SIZE_UNIT size_unit_type;

  bool summarize, purpose;
  FILE *outfp = NULL;
  char io_size_str[64], log_size_str[64];

  SPACEDB_ALL all[SPACEDB_ALL_COUNT];
  SPACEDB_ONEVOL *vols = NULL;
  SPACEDB_ONEVOL **volsp = NULL;
  SPACEDB_FILES files[SPACEDB_FILE_COUNT];
  SPACEDB_FILES *filesp = NULL;

  char size_str_1[64];
  char size_str_2[64];
  char size_str_3[64];
  char size_str_4[64];

  const char *file_type_strings[] = {
    "INDEX", "HEAP", "SYSTEM", "TEMP", "-"
  };

  /* todo: there is a lot of work to do here */

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_space_usage;
    }

  database_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_space_usage;
    }

  output_file = utility_get_option_string_value (arg_map, SPACE_OUTPUT_FILE_S, 0);
  size_unit = utility_get_option_string_value (arg_map, SPACE_SIZE_UNIT_S, 0);
  summarize = utility_get_option_bool_value (arg_map, SPACE_SUMMARIZE_S);
  purpose = utility_get_option_bool_value (arg_map, SPACE_PURPOSE_S);

  size_unit_type = SPACEDB_SIZE_UNIT_HUMAN_READABLE;

  if (size_unit != NULL)
    {
      if (strcasecmp (size_unit, "page") == 0)
	{
	  size_unit_type = SPACEDB_SIZE_UNIT_PAGE;
	}
      else if (strcasecmp (size_unit, "m") == 0)
	{
	  size_unit_type = SPACEDB_SIZE_UNIT_MBYTES;
	}
      else if (strcasecmp (size_unit, "g") == 0)
	{
	  size_unit_type = SPACEDB_SIZE_UNIT_GBYTES;
	}
      else if (strcasecmp (size_unit, "t") == 0)
	{
	  size_unit_type = SPACEDB_SIZE_UNIT_TBYTES;
	}
      else if (strcasecmp (size_unit, "h") != 0)
	{
	  /* invalid option string */
	  goto print_space_usage;
	}
    }

  if (output_file == NULL)
    {
      outfp = stdout;
    }
  else
    {
      outfp = fopen (output_file, "w");
      if (outfp == NULL)
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SPACEDB, SPACEDB_MSG_BAD_OUTPUT),
				 output_file);
	  goto error_exit;
	}
    }

  if (check_database_name (database_name))
    {
      goto error_exit;
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  /* tuning system parameters */
  sysprm_set_force (prm_get_name (PRM_ID_PB_NBUFFERS), "1024");
  sysprm_set_force (prm_get_name (PRM_ID_JAVA_STORED_PROCEDURE), "no");

  /* should have little copyright herald message ? */
  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("DBA", NULL);

  if (db_restart (arg->command_name, TRUE, database_name) != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (ER_WARNING_SEVERITY));
      goto error_exit;
    }

  util_byte_to_size_string (io_size_str, 64, IO_PAGESIZE);
  util_byte_to_size_string (log_size_str, 64, LOG_PAGESIZE);

  if (!summarize)
    {
      /* we need space info per each volume. set volsp to non-NULL value */
      volsp = &vols;
    }
  if (purpose)
    {
      /* we need detailed space info for file usage. set filesp to non-NULL value */
      filesp = files;
    }

  if (netcl_spacedb (all, volsp, filesp) != NO_ERROR)
    {
      ASSERT_ERROR ();
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (ER_WARNING_SEVERITY));
      db_shutdown ();
      goto error_exit;
    }

  /* print title */
  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SPACEDB, SPACEDB_OUTPUT_TITLE), database_name,
	   io_size_str, log_size_str);
  fprintf (outfp, "\n");

  /* print aggregated info */
  /* print header */
  if (size_unit_type == SPACEDB_SIZE_UNIT_PAGE)
    {
      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SPACEDB, SPACEDB_MSG_ALL_HEADER_PAGES));
    }
  else
    {
      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SPACEDB, SPACEDB_MSG_ALL_HEADER_SIZE));
    }
  /* print values. the format is:
   * type, purpose, used pages/size, free pages/size, total pages/size */
  /* print perm perm values */
  for (i = 0; i < SPACEDB_ALL_COUNT; i++)
    {
      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SPACEDB, SPACEDB_MSG_PERM_PERM_FORMAT + i),
	       all[i].nvols, SPACEDB_TO_SIZE_ARG (1, all[i].npage_used),
	       SPACEDB_TO_SIZE_ARG (2, all[i].npage_free),
	       SPACEDB_TO_SIZE_ARG (3, all[i].npage_used + all[i].npage_free));
    }
  fprintf (outfp, "\n");

  if (volsp != NULL)
    {
      /* print information on all volumes */
      MSGCAT_SPACEDB_MSG msg_vols_format;

      /* print title */
      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SPACEDB, SPACEDB_MSG_VOLS_TITLE));
      /* print header */
      if (size_unit_type == SPACEDB_SIZE_UNIT_PAGE)
	{
	  fprintf (outfp,
		   msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SPACEDB, SPACEDB_MSG_VOLS_HEADER_PAGES));
	}
      else
	{
	  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SPACEDB, SPACEDB_MSG_VOLS_HEADER_SIZE));
	}

      /* print each volume */
      for (i = 0; i < all[SPACEDB_TOTAL_ALL].nvols; i++)
	{
	  if (vols[i].type == DB_PERMANENT_VOLTYPE)
	    {
	      if (vols[i].purpose == DB_PERMANENT_DATA_PURPOSE)
		{
		  msg_vols_format = SPACEDB_MSG_VOLS_PERM_PERM_FORMAT;
		}
	      else
		{
		  msg_vols_format = SPACEDB_MSG_VOLS_PERM_TEMP_FORMAT;
		}
	    }
	  else
	    {
	      msg_vols_format = SPACEDB_MSG_VOLS_TEMP_TEMP_FORMAT;
	    }
	  /* the format is:
	   * volid, type, purpose, used pages/size, free pages/size, total pages/size, volume name */
	  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SPACEDB, msg_vols_format),
		   vols[i].volid, SPACEDB_TO_SIZE_ARG (1, vols[i].npage_used),
		   SPACEDB_TO_SIZE_ARG (2, vols[i].npage_free),
		   SPACEDB_TO_SIZE_ARG (3, vols[i].npage_used + vols[i].npage_free), vols[i].name);
	}
      fprintf (outfp, "\n");
    }

  if (filesp != NULL)
    {
      /* print detailed files information */

      /* print title */
      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SPACEDB, SPACEDB_MSG_FILES_TITLE));
      /* print header */
      if (size_unit_type == SPACEDB_SIZE_UNIT_PAGE)
	{
	  fprintf (outfp,
		   msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SPACEDB, SPACEDB_MSG_FILES_HEADER_PAGES));
	}
      else
	{
	  fprintf (outfp,
		   msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SPACEDB, SPACEDB_MSG_FILES_HEADER_SIZE));
	}

      /* the format is:
       * data_type, file_count, used pages/size, ftab pages/size, reserved pages/size, total pages/size */
      for (i = 0; i < SPACEDB_FILE_COUNT; i++)
	{
	  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SPACEDB, SPACEDB_MSG_FILES_FORMAT),
		   file_type_strings[i], files[i].nfile,
		   SPACEDB_TO_SIZE_ARG (1, files[i].npage_user), SPACEDB_TO_SIZE_ARG (2, files[i].npage_ftab),
		   SPACEDB_TO_SIZE_ARG (3, files[i].npage_reserved),
		   SPACEDB_TO_SIZE_ARG (4, files[i].npage_user + files[i].npage_ftab + files[i].npage_reserved));
	}
      fprintf (outfp, "\n");
    }

  if (!summarize)
    {
      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SPACEDB, SPACEDB_OUTPUT_TITLE_LOB),
	       boot_get_lob_path ());
    }

  db_shutdown ();
  if (outfp != stdout)
    {
      fclose (outfp);
    }

  if (vols != NULL)
    {
      free_and_init (vols);
    }

  return EXIT_SUCCESS;

print_space_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_SPACEDB, SPACEDB_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

error_exit:
  if (outfp != stdout && outfp != NULL)
    {
      fclose (outfp);
    }
  if (vols != NULL)
    {
      free_and_init (vols);
    }
  return EXIT_FAILURE;

#undef SPACEDB_TO_SIZE_ARG
}

/*
 * acldb() -
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
acldb (UTIL_FUNCTION_ARG * arg)
{
#if defined (CS_MODE)
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *database_name;
  bool reload;
  int ret_code = EXIT_SUCCESS;

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_acl_usage;
    }

  reload = utility_get_option_bool_value (arg_map, ACLDB_RELOAD_S);

  database_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_acl_usage;
    }

  if (check_database_name (database_name))
    {
      goto error_exit;
    }

  /* error message log file */
  sprintf (er_msg_file, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  /* should have little copyright herald message ? */
  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("DBA", NULL);

  if (db_restart (arg->command_name, TRUE, database_name) != NO_ERROR)
    {
      fprintf (stderr, "%s\n", db_error_string (3));
      goto error_exit;
    }

  if (reload)
    {
      ret_code = acl_reload ();
      if (ret_code != NO_ERROR)
	{
	  fprintf (stderr, "%s\n", db_error_string (3));
	}
    }
  else
    {
      acl_dump (stdout);
    }
  db_shutdown ();

  return ret_code;

print_acl_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_ACLDB, ACLDB_MSG_USAGE),
	   basename (arg->argv0));
error_exit:
  return EXIT_FAILURE;
#else /* CS_MODE */
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_ACLDB, ACLDB_MSG_NOT_IN_STANDALONE),
	   basename (arg->argv0));
  return EXIT_FAILURE;
#endif /* !CS_MODE */
}

/*
 * lockdb() - lockdb main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
lockdb (UTIL_FUNCTION_ARG * arg)
{
#if defined (CS_MODE)
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *database_name;
  const char *output_file = NULL;
  FILE *outfp = NULL;
  int is_contention;

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_lock_usage;
    }

  database_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_lock_usage;
    }

  is_contention = utility_get_option_bool_value (arg_map, LOCK_DISPLAY_CONTENTION_S);
  output_file = utility_get_option_string_value (arg_map, LOCK_OUTPUT_FILE_S, 0);
  if (output_file == NULL)
    {
      outfp = stdout;
    }
  else
    {
      outfp = fopen (output_file, "w");
      if (outfp == NULL)
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOCKDB, LOCKDB_MSG_BAD_OUTPUT),
				 output_file);
	  goto error_exit;
	}
    }

  if (check_database_name (database_name))
    {
      goto error_exit;
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  /* should have little copyright herald message ? */
  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("DBA", NULL);

  if (db_restart (arg->command_name, TRUE, database_name) != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }

  (void) db_set_isolation (TRAN_READ_COMMITTED);

  lock_dump (outfp, is_contention);
  db_shutdown ();

  if (outfp != stdout)
    {
      fclose (outfp);
    }

  return EXIT_SUCCESS;

print_lock_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOCKDB, LOCKDB_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

error_exit:
  if (outfp != stdout && outfp != NULL)
    {
      fclose (outfp);
    }
  return EXIT_FAILURE;
#else /* CS_MODE */
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOCKDB, LOCKDB_MSG_NOT_IN_STANDALONE),
	   basename (arg->argv0));
  return EXIT_FAILURE;
#endif /* !CS_MODE */
}

/*
 * isvalid_transaction() - test if transaction is valid
 *   return: non-zero if valid transaction
 *   tran(in)
 */
static int
isvalid_transaction (const ONE_TRAN_INFO * tran)
{
  int valid;

  valid = (tran != NULL && tran->tran_index != -1 && tran->tran_index != tm_Tran_index);

  return valid;
}

/*
 * doesmatch_transaction() - test if matching transaction
 *   return: non-zero if the information matches this transaction
 *   tran(in)
 *   tran_index_list(in)
 *   index_list_size(in)
 *   user_name(in)
 *   hostname(in)
 *   progname(in)
 */
static bool
doesmatch_transaction (const ONE_TRAN_INFO * tran, int *tran_index_list, int index_list_size, const char *username,
		       const char *hostname, const char *progname, const char *sql_id)
{
  int i;

  if (!isvalid_transaction (tran))
    {
      return false;
    }

  if ((username != NULL && strcmp (tran->login_name, username) == 0)
      || (hostname != NULL && strcmp (tran->host_name, hostname) == 0)
      || (progname != NULL && strcmp (tran->program_name, progname) == 0)
      || (sql_id != NULL && tran->query_exec_info.sql_id != NULL && strcmp (tran->query_exec_info.sql_id, sql_id) == 0))
    {
      return true;
    }

  for (i = 0; i < index_list_size; i++)
    {
      if (tran->tran_index == tran_index_list[i])
	{
	  return true;
	}
    }

  return false;
}

/*
 * dump_trantb() - Displays information about all the currently
 *                 active transactions
 *   return: none
 *   info(in) :
 *   dump_level(in) :
 */
static void
dump_trantb (TRANS_INFO * info, TRANDUMP_LEVEL dump_level, bool full_sqltext)
{
  int i;
  int num_valid = 0;
  MSGCAT_TRANLIST_MSG header = TRANLIST_MSG_SUMMARY_HEADER;
  MSGCAT_TRANLIST_MSG underscore = TRANLIST_MSG_SUMMARY_UNDERSCORE;

  if (dump_level == TRANDUMP_FULL_INFO)
    {
      header = TRANLIST_MSG_FULL_INFO_HEADER;
      underscore = TRANLIST_MSG_FULL_INFO_UNDERSCORE;
    }
  else if (dump_level == TRANDUMP_QUERY_INFO)
    {
      header = TRANLIST_MSG_QUERY_INFO_HEADER;
      underscore = TRANLIST_MSG_QUERY_INFO_UNDERSCORE;
    }

  if (info != NULL && info->num_trans > 0)
    {
      /*
       * remember that we have to print the messages one at a time, mts_
       * reuses the message buffer on each call.
       */
      for (i = 0; i < info->num_trans; i++)
	{
	  /*
	   * Display transactions in transaction table that seems to be valid
	   */
	  if (isvalid_transaction (&info->tran[i]))
	    {
	      if (num_valid == 0)
		{
		  /* Dump table header */
		  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_TRANLIST, header));
		  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_TRANLIST, underscore));
		}

	      num_valid++;
	      print_tran_entry (&info->tran[i], dump_level, full_sqltext);
	    }
	}
    }

  if (num_valid > 0)
    {
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_TRANLIST, underscore));
    }
  else
    {
      fprintf (stdout,
	       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_TRANLIST, TRANLIST_MSG_NONE_TABLE_ENTRIES));
    }

  if (info != NULL && (dump_level == TRANDUMP_QUERY_INFO || dump_level == TRANDUMP_FULL_INFO))
    {
      int j;

      fprintf (stdout, "\n");
      /* print query string info */
      for (i = 0; i < info->num_trans; i++)
	{
	  if (isvalid_transaction (&info->tran[i]) && !XASL_ID_IS_NULL (&info->tran[i].query_exec_info.xasl_id))
	    {
	      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_TRANLIST, TRANLIST_MSG_SQL_ID),
		       info->tran[i].query_exec_info.sql_id);
	      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_TRANLIST, TRANLIST_MSG_TRAN_INDEX),
		       info->tran[i].tran_index);

	      for (j = i + 1; j < info->num_trans; j++)
		{
		  if (isvalid_transaction (&info->tran[j])
		      && XASL_ID_EQ (&info->tran[i].query_exec_info.xasl_id, &info->tran[j].query_exec_info.xasl_id))
		    {
		      /* same query */
		      fprintf (stdout, ", %d", info->tran[j].tran_index);
		      /* reset xasl to skip in next search */
		      XASL_ID_SET_NULL (&info->tran[j].query_exec_info.xasl_id);
		    }
		}
	      fprintf (stdout, "\n");

	      /* print query statement */
	      fprintf (stdout, "%s\n\n", info->tran[i].query_exec_info.query_stmt);
	    }
	}
    }
}

/*
 * kill_transactions() - kill transaction(s)
 *   return: number of killed transactions
 *   info(in/out)
 *   tran_index(in)
 *   username(in)
 *   hostname(in)
 *   progname(in)
 *   verify(in)
 *
 * Note: Kill one or several transactions identified only one of the
 *       above parameters. If the verification flag is set, the user is
 *       prompted for verification wheheter or not to kill the
 *       transaction(s).
 *
 *       if tran_index_list != NULL && != ""
 *         kill transactions in the tran_index comma list.
 *       else if username != NULL && != ""
 *         kill all transactions associated with given user.
 *       else if hostname != NULL && != ""
 *         kill all transactions associated with given host.
 *       else if progname != NULL && != ""
 *         kill all transactions associated with given program.
 *       else
 *         error.
 *
 *    If verify is set, the transactions are only killed after prompting
 *    for verification.
 */
static int
kill_transactions (TRANS_INFO * info, int *tran_index_list, int list_size, const char *username, const char *hostname,
		   const char *progname, const char *sql_id, bool verify)
{
  int i, ok;
  int nkills = 0, nfailures = 0;
  int ch;
  MSGCAT_TRANLIST_MSG header = TRANLIST_MSG_SUMMARY_HEADER;
  MSGCAT_TRANLIST_MSG underscore = TRANLIST_MSG_SUMMARY_UNDERSCORE;
  TRANDUMP_LEVEL dump_level = TRANDUMP_SUMMARY;

  if (sql_id != NULL)
    {
      /* print --query-exec-info table format */
      header = TRANLIST_MSG_QUERY_INFO_HEADER;
      underscore = TRANLIST_MSG_QUERY_INFO_UNDERSCORE;
      dump_level = TRANDUMP_QUERY_INFO;
    }

  /* see if we have anything do do */
  for (i = 0; i < info->num_trans; i++)
    {
      if (doesmatch_transaction (&info->tran[i], tran_index_list, list_size, username, hostname, progname, sql_id))
	{
	  break;
	}
    }

  if (i >= info->num_trans)
    {
      /*
       * There is not matches
       */
      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_KILLTRAN, KILLTRAN_MSG_NO_MATCHES));
    }
  else
    {
      if (!verify)
	{
	  ok = 1;
	}
      else
	{
	  ok = 0;
	  /*
	   * display the transactin identifiers that we are about to kill
	   */
	  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_KILLTRAN, KILLTRAN_MSG_READY_TO_KILL));

	  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_TRANLIST, header));
	  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_TRANLIST, underscore));

	  for (i = 0; i < info->num_trans; i++)
	    {
	      if (doesmatch_transaction (&info->tran[i], tran_index_list, list_size, username, hostname, progname,
					 sql_id))
		{
		  print_tran_entry (&info->tran[i], dump_level, false);
		}
	    }
	  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_TRANLIST, underscore));

	  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_KILLTRAN, KILLTRAN_MSG_VERIFY));
	  fflush (stdout);

	  ch = getc (stdin);
	  if (ch == 'Y' || ch == 'y')
	    {
	      ok = 1;
	    }
	}

      if (ok)
	{
	  for (i = 0; i < info->num_trans; i++)
	    {
	      if (doesmatch_transaction (&info->tran[i], tran_index_list, list_size, username, hostname, progname,
					 sql_id))
		{
		  fprintf (stdout,
			   msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_KILLTRAN, KILLTRAN_MSG_KILLING),
			   info->tran[i].tran_index);
		  if (thread_kill_tran_index (info->tran[i].tran_index, info->tran[i].db_user, info->tran[i].host_name,
					      info->tran[i].process_id) == NO_ERROR)
		    {
		      info->tran[i].tran_index = -1;	/* Gone */
		      nkills++;
		    }
		  else
		    {
		      /*
		       * Fail to kill the transaction
		       */
		      if (nfailures == 0)
			{
			  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_KILLTRAN,
							   KILLTRAN_MSG_KILL_FAILED));
			  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_TRANLIST, header));
			  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_TRANLIST, underscore));
			}

		      print_tran_entry (&info->tran[i], dump_level, false);

		      if (er_errid () != NO_ERROR)
			{
			  PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
			}
		      else	/* probably it is the case of timeout */
			{
			  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_KILLTRAN,
								 KILLTRAN_MSG_KILL_TIMEOUT));
			}
		      nfailures++;
		    }
		}
	    }

	  if (nfailures > 0)
	    {
	      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_TRANLIST, underscore));
	    }
	}
    }

  return nkills;
}

/*
 * print_tran_entry()
 *   return: NO_ERROR
 *   tran_info(in) :
 *   include_query_info(in) :
 */
static int
print_tran_entry (const ONE_TRAN_INFO * tran_info, TRANDUMP_LEVEL dump_level, bool full_sqltext)
{
  char *buf = NULL;
  char *query_buf;
  char tmp_query_buf[32];

  if (tran_info == NULL)
    {
      assert_release (0);
      return ER_FAILED;
    }

  assert_release (dump_level <= TRANDUMP_FULL_INFO);

  if (dump_level == TRANDUMP_FULL_INFO || dump_level == TRANDUMP_QUERY_INFO)
    {
      buf = tran_info->query_exec_info.wait_for_tran_index_string;

      if (tran_info->query_exec_info.query_stmt != NULL)
	{
	  /* print 31 string */
	  if (full_sqltext == true)
	    {
	      query_buf = tran_info->query_exec_info.query_stmt;
	    }
	  else
	    {
	      strncpy (tmp_query_buf, tran_info->query_exec_info.query_stmt, 32);
	      tmp_query_buf[31] = '\0';
	      query_buf = tmp_query_buf;
	    }
	}
    }

  if (dump_level == TRANDUMP_FULL_INFO)
    {
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_TRANLIST, TRANLIST_MSG_FULL_INFO_ENTRY),
	       tran_info->tran_index, tran_get_tranlist_state_name (tran_info->state), tran_info->db_user,
	       tran_info->host_name, tran_info->process_id, tran_info->program_name,
	       tran_info->query_exec_info.query_time, tran_info->query_exec_info.tran_time, (buf == NULL ? "-1" : buf),
	       ((tran_info->query_exec_info.sql_id) ? tran_info->query_exec_info.sql_id : "*** empty ***"),
	       ((tran_info->query_exec_info.query_stmt) ? query_buf : " "));
    }
  else if (dump_level == TRANDUMP_QUERY_INFO)
    {
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_TRANLIST, TRANLIST_MSG_QUERY_INFO_ENTRY),
	       tran_info->tran_index, tran_get_tranlist_state_name (tran_info->state),
	       tran_info->process_id, tran_info->program_name, tran_info->query_exec_info.query_time,
	       tran_info->query_exec_info.tran_time, (buf == NULL ? "-1" : buf),
	       ((tran_info->query_exec_info.sql_id) ? tran_info->query_exec_info.sql_id : "*** empty ***"),
	       ((tran_info->query_exec_info.query_stmt) ? query_buf : " "));
    }
  else
    {
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_TRANLIST, TRANLIST_MSG_SUMMARY_ENTRY),
	       tran_info->tran_index, tran_get_tranlist_state_name (tran_info->state), tran_info->db_user,
	       tran_info->host_name, tran_info->process_id, tran_info->program_name);
    }

  return NO_ERROR;
}


/*
 * tranlist() -
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
tranlist (UTIL_FUNCTION_ARG * arg)
{
#if defined (CS_MODE)
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *database_name;
#if defined(NEED_PRIVILEGE_PASSWORD)
  const char *username;
  const char *password;
#endif
  char *passbuf = NULL;
  TRANS_INFO *info = NULL;
  int error;
  bool is_summary, include_query_info, full_sqltext = false;
  TRANDUMP_LEVEL dump_level = TRANDUMP_FULL_INFO;

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_tranlist_usage;
    }

  database_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_tranlist_usage;
    }

#if defined(NEED_PRIVILEGE_PASSWORD)
  username = utility_get_option_string_value (arg_map, TRANLIST_USER_S, 0);
  password = utility_get_option_string_value (arg_map, TRANLIST_PASSWORD_S, 0);
#endif
  is_summary = utility_get_option_bool_value (arg_map, TRANLIST_SUMMARY_S);
  tranlist_Sort_column = utility_get_option_int_value (arg_map, TRANLIST_SORT_KEY_S);
  tranlist_Sort_desc = utility_get_option_bool_value (arg_map, TRANLIST_REVERSE_S);
  full_sqltext = utility_get_option_bool_value (arg_map, TRANLIST_FULL_SQL_S);

#if defined(NEED_PRIVILEGE_PASSWORD)
  if (username == NULL)
    {
      /* default : DBA user */
      username = "DBA";
    }
#endif

  if (check_database_name (database_name) != NO_ERROR)
    {
      goto error_exit;
    }

  if (tranlist_Sort_column > 10 || tranlist_Sort_column < 0 || (is_summary && tranlist_Sort_column > 5))
    {
      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_TRANLIST,
					     TRANLIST_MSG_INVALID_SORT_KEY), tranlist_Sort_column);
      goto error_exit;
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

#if defined(NEED_PRIVILEGE_PASSWORD)
  error = db_restart_ex (arg->command_name, database_name, username, password, NULL, DB_CLIENT_TYPE_ADMIN_UTILITY);
  if (error != NO_ERROR)
    {
      char msg_buf[64];

      if (error == ER_AU_INVALID_PASSWORD && password == NULL)
	{
	  /*
	   * prompt for a valid password and try again, need a reusable
	   * password prompter so we can use getpass() on platforms that
	   * support it.
	   */
	  snprintf (msg_buf, 64,
		    msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_TRANLIST, TRANLIST_MSG_USER_PASSWORD),
		    username);

	  passbuf = getpass (msg_buf);

	  if (passbuf[0] == '\0')
	    {
	      passbuf = (char *) NULL;
	    }
	  password = passbuf;

	  error =
	    db_restart_ex (arg->command_name, database_name, username, password, NULL, DB_CLIENT_TYPE_ADMIN_UTILITY);
	}

      if (error != NO_ERROR)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
	  goto error_exit;
	}
    }

  if (!au_is_dba_group_member (Au_user))
    {
      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_TRANLIST, TRANLIST_MSG_NOT_DBA_USER),
			     username);
      db_shutdown ();
      goto error_exit;
    }
#else
  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  if (db_login ("DBA", NULL) || db_restart (arg->command_name, TRUE, database_name))
    {
      PRINT_AND_LOG_ERR_MSG ("%s: %s. \n\n", arg->command_name, db_error_string (3));
      goto error_exit;
    }
#endif

  /*
   * Get the current state of transaction table information. All the
   * transaction kills are going to be based on this information. The
   * transaction information may be changed back in the server if there
   * are new transactions starting and finishing. We need to do this way
   * since verification is required at this level, and we cannot freeze the
   * state of the server ()transaction table).
   */
  include_query_info = !is_summary;

  info = logtb_get_trans_info (include_query_info);
  if (info == NULL)
    {
      util_log_write_errstr ("%s\n", db_error_string (3));
      db_shutdown ();
      goto error_exit;
    }

  if (is_summary)
    {
      dump_level = TRANDUMP_SUMMARY;
    }

  if (tranlist_Sort_column > 0 || tranlist_Sort_desc == true)
    {
      qsort ((void *) info->tran, info->num_trans, sizeof (ONE_TRAN_INFO), tranlist_cmp_f);
    }

  (void) dump_trantb (info, dump_level, full_sqltext);

  if (info)
    {
      logtb_free_trans_info (info);
    }

  (void) db_shutdown ();
  return EXIT_SUCCESS;

print_tranlist_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_TRANLIST, TRANLIST_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

error_exit:
  return EXIT_FAILURE;
#else /* CS_MODE */
  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_TRANLIST,
					 TRANLIST_MSG_NOT_IN_STANDALONE), basename (arg->argv0));
  return EXIT_FAILURE;
#endif /* !CS_MODE */
}

/*
 * killtran() - killtran main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
killtran (UTIL_FUNCTION_ARG * arg)
{
#if defined (CS_MODE)
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *database_name;
  const char *kill_tran_index;
  const char *kill_progname;
  const char *kill_user;
  const char *kill_host;
  const char *kill_sql_id;
  const char *dba_password;
  bool dump_trantab_flag;
  bool force = true;
  int isbatch;
  char *passbuf = NULL;
  TRANS_INFO *info = NULL;
  int error;
  bool include_query_exec_info;
  int tran_index_list[MAX_KILLTRAN_INDEX_LIST_NUM];
  int list_size = 0;
  int value;
  char delimiter = ',';
  const char *ptr;
  char *tmp;

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_killtran_usage;
    }

  database_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_killtran_usage;
    }

  kill_tran_index = utility_get_option_string_value (arg_map, KILLTRAN_KILL_TRANSACTION_INDEX_S, 0);
  kill_user = utility_get_option_string_value (arg_map, KILLTRAN_KILL_USER_NAME_S, 0);
  kill_host = utility_get_option_string_value (arg_map, KILLTRAN_KILL_HOST_NAME_S, 0);
  kill_progname = utility_get_option_string_value (arg_map, KILLTRAN_KILL_PROGRAM_NAME_S, 0);
  kill_sql_id = utility_get_option_string_value (arg_map, KILLTRAN_KILL_SQL_ID_S, 0);

  force = utility_get_option_bool_value (arg_map, KILLTRAN_FORCE_S);
  dba_password = utility_get_option_string_value (arg_map, KILLTRAN_DBA_PASSWORD_S, 0);
  dump_trantab_flag = utility_get_option_bool_value (arg_map, KILLTRAN_DISPLAY_INFORMATION_S);

  include_query_exec_info = utility_get_option_bool_value (arg_map, KILLTRAN_DISPLAY_QUERY_INFO_S);

  if (check_database_name (database_name))
    {
      goto error_exit;
    }

  isbatch = 0;
  if (kill_tran_index != NULL && strlen (kill_tran_index) != 0)
    {
      isbatch++;
    }
  if (kill_user != NULL && strlen (kill_user) != 0)
    {
      isbatch++;
    }
  if (kill_host != NULL && strlen (kill_host) != 0)
    {
      isbatch++;
    }
  if (kill_progname != NULL && strlen (kill_progname) != 0)
    {
      isbatch++;
    }
  if (kill_sql_id != NULL && strlen (kill_sql_id) != 0)
    {
      isbatch++;
    }

  if (isbatch > 1)
    {
      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_KILLTRAN, KILLTRAN_MSG_MANY_ARGS));
      goto error_exit;
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  /* disable password, if don't use kill option */
  if (isbatch == 0)
    {
      if (dba_password != NULL)
	{
	  goto print_killtran_usage;
	}
      AU_DISABLE_PASSWORDS ();
    }

  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);

  if (db_login ("DBA", dba_password) != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }

  /* first try to restart with the password given (possibly none) */
  error = db_restart (arg->command_name, TRUE, database_name);
  if (error)
    {
      if (error == ER_AU_INVALID_PASSWORD && (dba_password == NULL || strlen (dba_password) == 0))
	{
	  /*
	   * prompt for a valid password and try again, need a reusable
	   * password prompter so we can use getpass() on platforms that
	   * support it.
	   */

	  /* get password interactively if interactive mode */
	  passbuf = getpass (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_KILLTRAN,
					     KILLTRAN_MSG_DBA_PASSWORD));
	  if (passbuf[0] == '\0')	/* to fit into db_login protocol */
	    {
	      passbuf = (char *) NULL;
	    }
	  dba_password = passbuf;
	  if (db_login ("DBA", dba_password) != NO_ERROR)
	    {
	      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
	      goto error_exit;
	    }
	  else
	    {
	      error = db_restart (arg->command_name, TRUE, database_name);
	    }
	}

      if (error)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
	  goto error_exit;
	}
    }

  /*
   * Get the current state of transaction table information. All the
   * transaction kills are going to be based on this information. The
   * transaction information may be changed back in the server if there
   * are new transactions starting and finishing. We need to do this way
   * since verification is required at this level, and we cannot freeze the
   * state of the server ()transaction table).
   */

  info = logtb_get_trans_info (include_query_exec_info || kill_sql_id);
  if (info == NULL)
    {
      util_log_write_errstr ("%s\n", db_error_string (3));
      db_shutdown ();
      goto error_exit;
    }

  if (dump_trantab_flag || include_query_exec_info
      || ((kill_tran_index == NULL || strlen (kill_tran_index) == 0) && (kill_user == NULL || strlen (kill_user) == 0)
	  && (kill_host == NULL || strlen (kill_host) == 0) && (kill_progname == NULL || strlen (kill_progname) == 0)
	  && (kill_sql_id == NULL || strlen (kill_sql_id) == 0)))
    {
      TRANDUMP_LEVEL dump_level;

      dump_level = (include_query_exec_info) ? TRANDUMP_QUERY_INFO : TRANDUMP_SUMMARY;
      dump_trantb (info, dump_level, false);
    }
  else
    {
      if (kill_tran_index != NULL && strlen (kill_tran_index) > 0)
	{
	  int result;

	  ptr = kill_tran_index;

	  tmp = (char *) strchr (ptr, delimiter);
	  while (*ptr != '\0' && tmp)
	    {
	      if (list_size >= MAX_KILLTRAN_INDEX_LIST_NUM)
		{
		  break;
		}

	      *tmp = '\0';
	      result = parse_int (&value, ptr, 10);
	      if (result != 0 || value <= 0)
		{
		  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_KILLTRAN,
							 KILLTRAN_MSG_INVALID_TRANINDEX), ptr);
		  if (info)
		    {
		      logtb_free_trans_info (info);
		    }
		  db_shutdown ();
		  goto error_exit;
		}

	      tran_index_list[list_size++] = value;
	      ptr = tmp + 1;
	      tmp = (char *) strchr (ptr, delimiter);
	    }

	  result = parse_int (&value, ptr, 10);
	  if (result != 0 || value <= 0)
	    {
	      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_KILLTRAN,
						     KILLTRAN_MSG_INVALID_TRANINDEX), ptr);
	      if (info)
		{
		  logtb_free_trans_info (info);
		}
	      db_shutdown ();
	      goto error_exit;
	    }

	  if (list_size < MAX_KILLTRAN_INDEX_LIST_NUM)
	    {
	      tran_index_list[list_size++] = value;
	    }
	}

      /* some piece of transaction identifier was entered, try to use it */
      if (kill_transactions (info, tran_index_list, list_size, kill_user, kill_host, kill_progname, kill_sql_id, !force)
	  <= 0)
	{
	  if (info)
	    {
	      logtb_free_trans_info (info);
	    }
	  db_shutdown ();
	  goto error_exit;
	}
    }

  if (info)
    {
      logtb_free_trans_info (info);
    }

  (void) db_shutdown ();
  return EXIT_SUCCESS;

print_killtran_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_KILLTRAN, KILLTRAN_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

error_exit:
  return EXIT_FAILURE;
#else /* CS_MODE */
  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_KILLTRAN,
					 KILLTRAN_MSG_NOT_IN_STANDALONE), basename (arg->argv0));
  return EXIT_FAILURE;
#endif /* !CS_MODE */
}

/*
 * plandump() - plandump main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
plandump (UTIL_FUNCTION_ARG * arg)
{
#if defined (CS_MODE)
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *database_name;
  const char *output_file = NULL;
  bool drop_flag = false;
  FILE *outfp = NULL;

  database_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_plandump_usage;
    }

  drop_flag = utility_get_option_bool_value (arg_map, PLANDUMP_DROP_S);
  output_file = utility_get_option_string_value (arg_map, PLANDUMP_OUTPUT_FILE_S, 0);

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_plandump_usage;
    }

  if (output_file == NULL)
    {
      outfp = stdout;
    }
  else
    {
      outfp = fopen (output_file, "w");
      if (outfp == NULL)
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_PLANDUMP,
						 PLANDUMP_MSG_BAD_OUTPUT), output_file);
	  goto error_exit;
	}
    }

  if (check_database_name (database_name))
    {
      goto error_exit;
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  /* should have little copyright herald message ? */
  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("DBA", NULL);

  if (db_restart (arg->command_name, TRUE, database_name) != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }

  qmgr_dump_query_plans (outfp);
  if (drop_flag)
    {
      if (qmgr_drop_all_query_plans () != NO_ERROR)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
	  db_shutdown ();
	  goto error_exit;
	}
    }
  db_shutdown ();

  if (outfp != stdout)
    {
      fclose (outfp);
    }

  return EXIT_SUCCESS;

print_plandump_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_PLANDUMP, PLANDUMP_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

error_exit:
  if (outfp != stdout && outfp != NULL)
    {
      fclose (outfp);
    }
  return EXIT_FAILURE;
#else /* CS_MODE */
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_PLANDUMP, PLANDUMP_MSG_NOT_IN_STANDALONE),
	   basename (arg->argv0));
  return EXIT_FAILURE;
#endif /* !CS_MODE */
}

/*
 * paramdump() - paramdump main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
paramdump (UTIL_FUNCTION_ARG * arg)
{
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *database_name;
  const char *output_file = NULL;
  bool both_flag = false;
  FILE *outfp = NULL;

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_dumpparam_usage;
    }

  database_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_dumpparam_usage;
    }

  output_file = utility_get_option_string_value (arg_map, PARAMDUMP_OUTPUT_FILE_S, 0);
  both_flag = utility_get_option_bool_value (arg_map, PARAMDUMP_BOTH_S);

  if (output_file == NULL)
    {
      outfp = stdout;
    }
  else
    {
      outfp = fopen (output_file, "w");
      if (outfp == NULL)
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_PARAMDUMP,
						 PARAMDUMP_MSG_BAD_OUTPUT), output_file);
	  goto error_exit;
	}
    }

  if (check_database_name (database_name))
    {
      goto error_exit;
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  sysprm_set_force (prm_get_name (PRM_ID_JAVA_STORED_PROCEDURE), "no");

#if defined (CS_MODE)
  /* should have little copyright herald message ? */
  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("DBA", NULL);

  if (db_restart (arg->command_name, TRUE, database_name) != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }

  if (both_flag)
    {
      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_PARAMDUMP, PARAMDUMP_MSG_CLIENT_PARAMETER));
      sysprm_dump_parameters (outfp);
      fprintf (outfp, "\n");
    }
  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_PARAMDUMP, PARAMDUMP_MSG_SERVER_PARAMETER),
	   database_name);
  sysprm_dump_server_parameters (outfp);
  db_shutdown ();
#else /* CS_MODE */
  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_PARAMDUMP, PARAMDUMP_MSG_STANDALONE_PARAMETER));
  if (sysprm_load_and_init (database_name, NULL, SYSPRM_LOAD_ALL) == NO_ERROR)
    {
      sysprm_dump_parameters (outfp);
    }
#endif /* !CS_MODE */

  if (outfp != stdout)
    {
      fclose (outfp);
    }

  return EXIT_SUCCESS;

print_dumpparam_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_PARAMDUMP, PARAMDUMP_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

error_exit:
  if (outfp != stdout && outfp != NULL)
    {
      fclose (outfp);
    }
  return EXIT_FAILURE;
}

static void
print_timestamp (FILE * outfp)
{
  time_t tloc;
  struct tm tmloc;
  char str[80];

  tloc = time (NULL);
  utility_localtime (&tloc, &tmloc);
  strftime (str, 80, "%a %B %d %H:%M:%S %Z %Y", &tmloc);
  fprintf (outfp, "\n\t%s\n", str);
}

/*
 * statdump() - statdump main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
statdump (UTIL_FUNCTION_ARG * arg)
{
#if defined (CS_MODE)
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *database_name;
  const char *output_file = NULL;
  int interval;
  bool cumulative;
  const char *substr;
  FILE *outfp = NULL;

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_statdump_usage;
    }

  database_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_statdump_usage;
    }

  output_file = utility_get_option_string_value (arg_map, STATDUMP_OUTPUT_FILE_S, 0);
  interval = utility_get_option_int_value (arg_map, STATDUMP_INTERVAL_S);
  if (interval < 0)
    {
      goto print_statdump_usage;
    }
  cumulative = utility_get_option_bool_value (arg_map, STATDUMP_CUMULATIVE_S);
  substr = utility_get_option_string_value (arg_map, STATDUMP_SUBSTR_S, 0);

  if (interval == 0)
    {
      cumulative = true;
    }

  if (output_file == NULL)
    {
      outfp = stdout;
    }
  else
    {
      outfp = fopen (output_file, "w");
      if (outfp == NULL)
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_STATDUMP,
						 STATDUMP_MSG_BAD_OUTPUT), output_file);
	  goto error_exit;
	}
    }

  if (check_database_name (database_name))
    {
      goto error_exit;
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  /* should have little copyright herald message ? */
  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("DBA", NULL);

  if (db_restart (arg->command_name, TRUE, database_name) != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }

  if (histo_start (true) != NO_ERROR)
    {
      goto error_exit;
    }

  if (interval > 0)
    {
      is_Sigint_caught = false;
#if defined(WINDOWS)
      SetConsoleCtrlHandler ((PHANDLER_ROUTINE) intr_handler, TRUE);
#else
      os_set_signal_handler (SIGINT, intr_handler);
#endif
    }

  do
    {
      print_timestamp (outfp);
      if (histo_print_global_stats (outfp, cumulative, substr) != NO_ERROR)
	{
	  histo_stop ();
	  goto error_exit;
	}
      fflush (outfp);
      sleep (interval);
    }
  while (interval > 0 && !is_Sigint_caught);

  histo_stop ();

  db_shutdown ();

  if (outfp != stdout)
    {
      fclose (outfp);
    }

  return EXIT_SUCCESS;

print_statdump_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_STATDUMP, STATDUMP_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

error_exit:
  if (outfp != stdout && outfp != NULL)
    {
      fclose (outfp);
    }
  return EXIT_FAILURE;
#else /* CS_MODE */
  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_STATDUMP,
					 STATDUMP_MSG_NOT_IN_STANDALONE), basename (arg->argv0));

  return EXIT_FAILURE;
#endif /* !CS_MODE */
}

#if defined (ENABLE_UNUSED_FUNCTION)
/* check ha_mode is turned on in the server */
static int
check_server_ha_mode (void)
{
  char prm_buf[LINE_MAX], *prm_val;

  strcpy (prm_buf, prm_get_name (PRM_ID_HA_MODE));
  if (db_get_system_parameters (prm_buf, LINE_MAX - 1) != NO_ERROR)
    {
      return ER_FAILED;
    }
  prm_val = strchr (prm_buf, '=');
  if (prm_val == NULL)
    {
      return ER_FAILED;
    }
  if (strcmp (prm_val + 1, "y") != 0)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * changemode() - changemode main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 *
 * TODO: this is really confusing. changemode utility actually changes HA state and not HA mode. They are two different
 *       things.
 */
int
changemode (UTIL_FUNCTION_ARG * arg)
{
#if defined (WINDOWS)
  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CHANGEMODE,
					 CHANGEMODE_MSG_HA_NOT_SUPPORT), basename (arg->argv0));

  return EXIT_FAILURE;
#else /* WINDOWS */
#if defined (CS_MODE)
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *database_name;
  char *mode_name;
  int error;
  HA_SERVER_STATE ha_state = HA_SERVER_STATE_NA;
  bool force;
  int timeout;

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_changemode_usage;
    }

  database_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_changemode_usage;
    }

  mode_name = utility_get_option_string_value (arg_map, CHANGEMODE_MODE_S, 0);
  force = utility_get_option_bool_value (arg_map, CHANGEMODE_FORCE_S);
  timeout = utility_get_option_int_value (arg_map, CHANGEMODE_TIMEOUT_S);

  if (timeout == -1)
    {
      timeout = HA_CHANGE_MODE_DEFAULT_TIMEOUT_IN_SECS;
    }

  if (check_database_name (database_name))
    {
      goto error_exit;
    }

  /* check mode_name option argument */
  if (mode_name != NULL)
    {
      int keyval = -1;
      if (changemode_keyword (&keyval, &mode_name) != NO_ERROR)
	{
	  if (sscanf (mode_name, "%d", &keyval) != 1)
	    {
	      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CHANGEMODE,
						     CHANGEMODE_MSG_BAD_MODE), mode_name);
	      goto error_exit;
	    }
	}
      ha_state = (HA_SERVER_STATE) keyval;
      if (!(ha_state == HA_SERVER_STATE_ACTIVE || ha_state == HA_SERVER_STATE_STANDBY
	    || ha_state == HA_SERVER_STATE_MAINTENANCE))
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CHANGEMODE,
						 CHANGEMODE_MSG_BAD_MODE), mode_name);
	  goto error_exit;
	}
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  if (db_login ("DBA", NULL) != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }
  error = db_restart (arg->command_name, TRUE, database_name);
  if (error != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }

  if (HA_DISABLED ())
    {
      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CHANGEMODE,
					     CHANGEMODE_MSG_NOT_HA_MODE));
      goto error_exit;
    }

  if (mode_name == NULL)
    {
      /* display the value of current ha_state */
      ha_state = boot_change_ha_mode (HA_SERVER_STATE_NA, false, timeout);
    }
  else
    {
      /* change server's HA state */
      ha_state = boot_change_ha_mode (ha_state, force, timeout);
    }
  if (ha_state != HA_SERVER_STATE_NA)
    {
      int keyval = (int) ha_state;
      mode_name = NULL;
      if (changemode_keyword (&keyval, &mode_name) != NO_ERROR)
	{
	  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CHANGEMODE, CHANGEMODE_MSG_BAD_MODE),
		   (mode_name ? mode_name : "unknown"));
	}
      else
	{
	  fprintf (stdout,
		   msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CHANGEMODE, CHANGEMODE_MSG_SERVER_MODE),
		   database_name, mode_name);
	}
      ha_state = (HA_SERVER_STATE) keyval;
    }
  else
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }

  (void) db_shutdown ();
  return EXIT_SUCCESS;

print_changemode_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CHANGEMODE, CHANGEMODE_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

error_exit:
  return EXIT_FAILURE;
#else /* CS_MODE */
  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CHANGEMODE,
					 CHANGEMODE_MSG_NOT_IN_STANDALONE), basename (arg->argv0));
  return EXIT_FAILURE;
#endif /* !CS_MODE */
#endif /* !WINDOWS */
}

/*
 * copylogdb() - copylogdb main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
copylogdb (UTIL_FUNCTION_ARG * arg)
{
#if defined (WINDOWS)
  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_COPYLOGDB, COPYLOGDB_MSG_HA_NOT_SUPPORT),
			 basename (arg->argv0));
  return EXIT_FAILURE;
#else /* WINDOWS */
#if defined (CS_MODE)
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *database_name;
  const char *log_path;
  char *mode_name;
  int mode = -1;
  int error = NO_ERROR;
  int retried = 0, sleep_nsecs = 1;
  bool need_er_reinit = false;
  unsigned long num_db_restarted = 0;
#if !defined(WINDOWS)
  char *binary_name;
  char executable_path[PATH_MAX];
#endif
  INT64 start_pageid = 0;

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_copylog_usage;
    }

  database_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_copylog_usage;
    }

  log_path = utility_get_option_string_value (arg_map, COPYLOG_LOG_PATH_S, 0);
  mode_name = utility_get_option_string_value (arg_map, COPYLOG_MODE_S, 0);

  if (check_database_name (database_name))
    {
      goto error_exit;
    }
  if (log_path == NULL)
    {
      goto print_copylog_usage;
    }
  /* check mode_name option argument */
  if (mode_name != NULL)
    {
      if (copylogdb_keyword (&mode, &mode_name) != NO_ERROR)
	{
	  if (sscanf (mode_name, "%d", &mode) != 1)
	    {
	      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_COPYLOGDB,
						     COPYLOGDB_MSG_BAD_MODE), mode_name);
	      goto error_exit;
	    }
	}
      if (!(mode >= LOGWR_MODE_ASYNC && mode <= LOGWR_MODE_SYNC))
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_COPYLOGDB,
						 COPYLOGDB_MSG_BAD_MODE), mode_name);
	  goto error_exit;
	}
    }
  else
    {
      mode = LOGWR_MODE_SYNC;
    }

  /* 'SEMISYNC' is treated as 'SYNC'. */
  if (mode == LOGWR_MODE_SEMISYNC)
    {
      mode = LOGWR_MODE_SYNC;
    }

  start_pageid = utility_get_option_bigint_value (arg_map, COPYLOG_START_PAGEID_S);

#if defined(NDEBUG)
  util_redirect_stdout_to_null ();
#endif

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_LOG_COPIER);
  if (db_login ("DBA", NULL) != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }
#if !defined(WINDOWS)
  /* save executable path */
  binary_name = basename (arg->argv0);
  (void) envvar_bindir_file (executable_path, PATH_MAX, binary_name);

  hb_set_exec_path (executable_path);
  hb_set_argv (arg->argv);

  /* initialize system parameters */
  if (sysprm_load_and_init (database_name, NULL, SYSPRM_LOAD_ALL) != NO_ERROR)
    {
      error = ER_FAILED;
      goto error_exit;
    }

  /*
   * Force error log file system parameter as copylogdb;
   * during a retry loop, `db_restart` will reset the error file name as :
   * er_init (prm_get_string_value (PRM_ID_ER_LOG_FILE), ... ) 
   */
  sysprm_set_force (prm_get_name (PRM_ID_ER_LOG_FILE), er_msg_file);

  if (start_pageid < NULL_PAGEID && !HA_DISABLED ())
    {
      error = hb_process_init (database_name, log_path, HB_PTYPE_COPYLOGDB);
      if (error != NO_ERROR)
	{
	  er_log_debug (ARG_FILE_LINE, "cannot connect to cub_master for heartbeat. \n");
	  if (er_errid () != NO_ERROR)
	    {
	      util_log_write_errstr ("%s\n", db_error_string (3));
	    }

	  return EXIT_FAILURE;
	}
      er_set_ignore_uninit (true);
    }
#endif

retry:
  error = db_restart (arg->command_name, TRUE, database_name);
  if (error != NO_ERROR)
    {
      if (retried % 10 == 0)
	{
	  fprintf (stderr, "%s\n", db_error_string (3));
	}
      goto error_exit;
    }
  num_db_restarted += 1;

  if (need_er_reinit)
    {
      er_init (er_msg_file, ER_NEVER_EXIT);
      need_er_reinit = false;
    }

  /* initialize system parameters */
  if (sysprm_load_and_init (database_name, NULL, SYSPRM_LOAD_ALL) != NO_ERROR)
    {
      (void) db_shutdown ();

      error = ER_FAILED;
      goto error_exit;
    }

  /* PRM_LOG_BACKGROUND_ARCHIVING is always true in CUBRID HA */
  sysprm_set_to_default (prm_get_name (PRM_ID_LOG_BACKGROUND_ARCHIVING), true);

  error = logwr_copy_log_file (database_name, log_path, mode, start_pageid);
  if (error != NO_ERROR)
    {
      fprintf (stderr, "%s\n", db_error_string (3));
      (void) db_shutdown ();
      goto error_exit;
    }

  (void) db_shutdown ();
  return EXIT_SUCCESS;

print_copylog_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_COPYLOGDB, COPYLOGDB_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

  return EXIT_FAILURE;

error_exit:
#if !defined(WINDOWS)
  if (hb_Proc_shutdown)
    {
      return EXIT_SUCCESS;
    }
#endif

  if (logwr_force_shutdown () == false
      && (error == ER_NET_SERVER_CRASHED || error == ER_NET_CANT_CONNECT_SERVER || error == ER_BO_CONNECT_FAILED
	  || error == ERR_CSS_TCP_CANNOT_CONNECT_TO_MASTER || error == ERR_CSS_TCP_CONNECT_TIMEDOUT))
    {
      (void) sleep (sleep_nsecs);
      /* sleep 1, 2, 4, 8, etc; don't wait for more than 1/2 min */
      if (num_db_restarted > 0)
	{
	  sleep_nsecs *= 2;
	  if (sleep_nsecs > 30)
	    {
	      sleep_nsecs = 1;
	    }
	}

      need_er_reinit = true;
      ++retried;

      er_init (er_msg_file, ER_NEVER_EXIT);
      goto retry;
    }

  return EXIT_FAILURE;
#else /* CS_MODE */
  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_COPYLOGDB,
					 COPYLOGDB_MSG_NOT_IN_STANDALONE), basename (arg->argv0));
  return EXIT_FAILURE;
#endif /* !CS_MODE */
#endif /* !WINDOWS */
}

/*
 * applylogdb() - applylogdb main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
applylogdb (UTIL_FUNCTION_ARG * arg)
{
#if defined (WINDOWS)
  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_APPLYLOGDB,
					 APPLYLOGDB_MSG_HA_NOT_SUPPORT), basename (arg->argv0));

  return EXIT_FAILURE;
#else /* WINDOWS */
#if defined (CS_MODE)
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *database_name = NULL;
  const char *log_path = NULL;
  char log_path_buf[PATH_MAX];
  char *log_path_base;
  int max_mem_size = 0;
  int error = NO_ERROR;
  int retried = 0, sleep_nsecs = 1;
  bool need_er_reinit = false;
  char *replica_time_bound_str;
#if !defined(WINDOWS)
  char *binary_name;
  char executable_path[PATH_MAX];
#endif

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_applylog_usage;
    }

  database_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_applylog_usage;
    }

  log_path = utility_get_option_string_value (arg_map, APPLYLOG_LOG_PATH_S, 0);
  max_mem_size = utility_get_option_int_value (arg_map, APPLYLOG_MAX_MEM_SIZE_S);

  if (check_database_name (database_name))
    {
      goto error_exit;
    }
  if (log_path != NULL)
    {
      log_path = realpath (log_path, log_path_buf);
    }
  if (log_path == NULL)
    {
      goto print_applylog_usage;
    }

#if defined(NDEBUG)
  util_redirect_stdout_to_null ();
#endif

  /* error message log file */
  log_path_base = strdup (log_path);
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s_%s.err", database_name, arg->command_name,
	    basename (log_path_base));
  free (log_path_base);
  er_init (er_msg_file, ER_NEVER_EXIT);

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_LOG_APPLIER);
  if (db_login ("DBA", NULL) != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }

#if !defined(WINDOWS)
  /* save executable path */
  binary_name = basename (arg->argv0);
  (void) envvar_bindir_file (executable_path, PATH_MAX, binary_name);

  hb_set_exec_path (executable_path);
  hb_set_argv (arg->argv);

  /* initialize system parameters */
  if (sysprm_load_and_init (database_name, NULL, SYSPRM_LOAD_ALL) != NO_ERROR)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_SERVICE_PROPERTY_FAIL);
      error = ER_FAILED;
      goto error_exit;
    }

  if (!HA_DISABLED ())
    {
      /* initialize heartbeat */
      error = hb_process_init (database_name, log_path, HB_PTYPE_APPLYLOGDB);
      if (error != NO_ERROR)
	{
	  er_log_debug (ARG_FILE_LINE, "Cannot connect to cub_master for heartbeat. \n");
	  if (er_errid () != NO_ERROR)
	    {
	      util_log_write_errstr ("%s\n", db_error_string (3));
	    }
	  return EXIT_FAILURE;
	}
      er_set_ignore_uninit (true);
    }

  if (HA_GET_MODE () == HA_MODE_REPLICA)
    {
      replica_time_bound_str = prm_get_string_value (PRM_ID_HA_REPLICA_TIME_BOUND);
      if (replica_time_bound_str != NULL)
	{
	  if (util_str_to_time_since_epoch (replica_time_bound_str) == 0)
	    {
	      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_GENERIC,
						     MSGCAT_UTIL_GENERIC_INVALID_PARAMETER),
				     prm_get_name (PRM_ID_HA_REPLICA_TIME_BOUND),
				     "(the correct format: YYYY-MM-DD hh:mm:ss)");
	      return EXIT_FAILURE;
	    }
	}
    }
#endif

retry:
  error = db_restart (arg->command_name, TRUE, database_name);
  if (error != NO_ERROR)
    {
      fprintf (stderr, "%s\n", db_error_string (3));
      goto error_exit;
    }

  /* applylogdb should not fire trigger action */
  db_disable_trigger ();

  if (need_er_reinit)
    {
      er_init (er_msg_file, ER_NEVER_EXIT);
      need_er_reinit = false;
    }

  /* set lock timeout to infinite */
  db_set_lock_timeout (-1);

  /* initialize system parameters */
  if (sysprm_load_and_init (database_name, NULL, SYSPRM_LOAD_ALL) != NO_ERROR)
    {
      (void) db_shutdown ();

      error = ER_FAILED;
      goto error_exit;
    }

  if (HA_DISABLED ())
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_APPLYLOGDB, APPLYLOGDB_MSG_NOT_HA_MODE));
      (void) db_shutdown ();
      goto error_exit;
    }

  error = la_apply_log_file (database_name, log_path, max_mem_size);
  if (error != NO_ERROR)
    {
      fprintf (stderr, "%s\n", db_error_string (3));
      (void) db_shutdown ();
      goto error_exit;
    }

  (void) db_shutdown ();
  return EXIT_SUCCESS;

print_applylog_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_APPLYLOGDB, APPLYLOGDB_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

error_exit:
#if !defined(WINDOWS)
  if (hb_Proc_shutdown)
    {
      return EXIT_SUCCESS;
    }
#endif

  if (la_force_shutdown () == false
      && (error == ER_NET_SERVER_CRASHED || error == ER_NET_CANT_CONNECT_SERVER
	  || error == ERR_CSS_TCP_CANNOT_CONNECT_TO_MASTER || error == ER_BO_CONNECT_FAILED
	  || error == ER_NET_SERVER_COMM_ERROR || error == ER_LC_PARTIALLY_FAILED_TO_FLUSH))
    {
      (void) sleep (sleep_nsecs);
      /* sleep 1, 2, 4, 8, etc; don't wait for more than 10 sec */
      if ((sleep_nsecs *= 2) > 10)
	{
	  sleep_nsecs = 1;
	}
      need_er_reinit = true;
      ++retried;
      goto retry;
    }

  return EXIT_FAILURE;
#else /* CS_MODE */
  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_APPLYLOGDB,
					 APPLYLOGDB_MSG_NOT_IN_STANDALONE), basename (arg->argv0));

error_exit:
  return EXIT_FAILURE;
#endif /* !CS_MODE */
#endif /* !WINDOWS */
}

/*
 * sig_interrupt() -
 *   return: none
 *   sig_no(in)
 */
static void
backupdb_sig_interrupt_handler (int sig_no)
{
  db_set_interrupt (1);
}

STATIC_INLINE char *
spacedb_get_size_str (char *buf, UINT64 num_pages, T_SPACEDB_SIZE_UNIT size_unit)
{
#define UNIT_STR(un) (((un) == SPACEDB_SIZE_UNIT_MBYTES) ? 'M' : ((un) == SPACEDB_SIZE_UNIT_GBYTES) ? 'G' : 'T')
  int pgsize, i;
  double size;

  assert (buf);

  if (size_unit == SPACEDB_SIZE_UNIT_PAGE)
    {
      sprintf (buf, "%13llu", (long long unsigned int) num_pages);
    }
  else
    {
      pgsize = IO_PAGESIZE / 1024;
      size = pgsize * ((double) num_pages);

      if (size_unit == SPACEDB_SIZE_UNIT_HUMAN_READABLE)
	{
	  for (i = SPACEDB_SIZE_UNIT_MBYTES; i <= SPACEDB_SIZE_UNIT_TBYTES; i++)
	    {
	      size /= 1024;

	      if (size < 1024)
		{
		  break;
		}
	    }
	  sprintf (buf, "%9.1f %c", size, UNIT_STR (i));
	}
      else
	{
	  for (i = size_unit; i > (int) SPACEDB_SIZE_UNIT_PAGE; i--)
	    {
	      size /= 1024;
	    }
	  sprintf (buf, "%9.1f %c", size, UNIT_STR (size_unit));
	}
    }

  return buf;
#undef UNIT_STR
}

/*
 * applyinfo() - ApplyInfo main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
applyinfo (UTIL_FUNCTION_ARG * arg)
{
#if defined (WINDOWS)
  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_APPLYINFO, APPLYINFO_MSG_HA_NOT_SUPPORT),
			 basename (arg->argv0));

  return EXIT_FAILURE;
#else /* WINDOWS */
#if defined (CS_MODE)
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *database_name;
  const char *master_node_name;
  char local_database_name[CUB_MAXHOSTNAMELEN];
  char master_database_name[CUB_MAXHOSTNAMELEN];
  bool check_applied_info, check_applied_info_temp, check_copied_info, check_copied_info_temp;
  bool check_master_info, check_master_info_temp, check_replica_info;
  bool verbose;
  const char *log_path;
  char log_path_buf[PATH_MAX];
  int error = NO_ERROR;
  INT64 pageid = 0;
  int interval;
  float process_rate = 0.0f;
  char *replica_time_bound_str;
  /* log lsa to calculate the estimated delay */
  LOG_LSA master_eof_lsa, applied_final_lsa;
  LOG_LSA copied_append_lsa, copied_eof_lsa;
  LOG_LSA initial_copied_append_lsa, initial_applied_final_lsa;
  time_t start_time, cur_time;

  start_time = time (NULL);

  LSA_SET_NULL (&master_eof_lsa);
  LSA_SET_NULL (&applied_final_lsa);
  LSA_SET_NULL (&copied_append_lsa);
  LSA_SET_NULL (&copied_eof_lsa);
  LSA_SET_NULL (&initial_copied_append_lsa);
  LSA_SET_NULL (&initial_applied_final_lsa);

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_applyinfo_usage;
    }

  check_applied_info_temp = check_applied_info = check_copied_info_temp = check_copied_info = false;
  check_replica_info = check_master_info_temp = check_master_info = false;

  database_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_applyinfo_usage;
    }

  /* initialize system parameters */
  if (sysprm_load_and_init (database_name, NULL, SYSPRM_LOAD_ALL) != NO_ERROR)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_SERVICE_PROPERTY_FAIL);
      return EXIT_FAILURE;
    }

  master_node_name = utility_get_option_string_value (arg_map, APPLYINFO_REMOTE_NAME_S, 0);
  if (master_node_name != NULL)
    {
      check_master_info_temp = check_master_info = true;
    }

  check_applied_info_temp = check_applied_info = utility_get_option_bool_value (arg_map, APPLYINFO_APPLIED_INFO_S);
  log_path = utility_get_option_string_value (arg_map, APPLYINFO_COPIED_LOG_PATH_S, 0);
  if (log_path != NULL)
    {
      check_copied_info_temp = check_copied_info = true;
    }

  if (!check_copied_info && !check_master_info)
    {
      goto print_applyinfo_usage;
    }

  if (!check_copied_info && check_applied_info)
    {
      goto print_applyinfo_usage;
    }

  check_replica_info = (HA_GET_MODE () == HA_MODE_REPLICA);
  pageid = utility_get_option_bigint_value (arg_map, APPLYINFO_PAGE_S);
  verbose = utility_get_option_bool_value (arg_map, APPLYINFO_VERBOSE_S);

  interval = utility_get_option_int_value (arg_map, APPLYINFO_INTERVAL_S);

  if (interval < 0)
    {
      goto print_applyinfo_usage;
    }

  if (check_replica_info)
    {
      replica_time_bound_str = prm_get_string_value (PRM_ID_HA_REPLICA_TIME_BOUND);
      if (replica_time_bound_str != NULL)
	{
	  if (util_str_to_time_since_epoch (replica_time_bound_str) == 0)
	    {
	      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_GENERIC,
						     MSGCAT_UTIL_GENERIC_INVALID_PARAMETER),
				     prm_get_name (PRM_ID_HA_REPLICA_TIME_BOUND),
				     "(the correct format: YYYY-MM-DD hh:mm:ss)");
	      return EXIT_FAILURE;
	    }
	}
    }

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);

  /* error message log file */
  sprintf (er_msg_file, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  is_Sigint_caught = false;
#if defined(WINDOWS)
  SetConsoleCtrlHandler ((PHANDLER_ROUTINE) intr_handler, TRUE);
#else
  os_set_signal_handler (SIGINT, intr_handler);
#endif

  do
    {
      memset (local_database_name, 0x00, CUB_MAXHOSTNAMELEN);
      strcpy (local_database_name, database_name);
      strcat (local_database_name, "@localhost");

      if (check_applied_info_temp)
	{
	  db_clear_host_connected ();

	  printf ("\n *** Applied Info. *** \n");
	  if (check_database_name (local_database_name))
	    {
	      goto check_applied_info_end;
	    }

	  error = db_login ("DBA", NULL);
	  if (error != NO_ERROR)
	    {
	      goto check_applied_info_end;
	    }

	  error = db_restart (arg->command_name, TRUE, local_database_name);
	  if (error != NO_ERROR)
	    {
	      goto check_applied_info_end;
	    }

	  if (HA_DISABLED ())
	    {
	      check_applied_info_temp = false;
	      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_APPLYINFO,
						     APPLYINFO_MSG_NOT_HA_MODE));
	      goto check_applied_info_end;
	    }

	  error = la_get_applied_log_info (database_name, log_path, check_replica_info, verbose, &applied_final_lsa);
	  if (error != NO_ERROR)
	    {
	      check_applied_info_temp = false;
	      error = NO_ERROR;
	    }
	}

    check_applied_info_end:

      if (error != NO_ERROR)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
	  check_applied_info_temp = false;
	  error = NO_ERROR;
	}

      if (check_copied_info_temp)
	{
	  error =
	    la_get_copied_log_info (database_name, log_path, pageid, verbose, &copied_eof_lsa, &copied_append_lsa);
	  if (error != NO_ERROR)
	    {
	      check_copied_info_temp = false;
	      error = NO_ERROR;
	    }
	}

      if (BOOT_IS_CLIENT_RESTARTED ())
	{
	  (void) db_shutdown ();
	}

      if (check_master_info_temp)
	{
	  printf ("\n ***  Active Info. *** \n");
	  memset (master_database_name, 0x00, CUB_MAXHOSTNAMELEN);
	  strcpy (master_database_name, database_name);
	  strcat (master_database_name, "@");
	  strcat (master_database_name, master_node_name);

	  db_clear_host_connected ();

	  if (check_database_name (master_database_name))
	    {
	      goto check_master_info_end;
	    }

	  if (db_login ("DBA", NULL) != NO_ERROR)
	    {
	      goto check_master_info_end;
	    }

	  error = db_restart (arg->command_name, TRUE, master_database_name);
	  if (error != NO_ERROR)
	    {
	      goto check_master_info_end;
	    }

	  error = logwr_copy_log_header_check (master_database_name, verbose, &master_eof_lsa);
	  if (error != NO_ERROR)
	    {
	      goto check_master_info_end;
	    }

	}

    check_master_info_end:
      if (error != NO_ERROR)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
	  check_master_info_temp = false;
	  error = NO_ERROR;
	}

      if (BOOT_IS_CLIENT_RESTARTED ())
	{
	  (void) db_shutdown ();
	}

      /* print delay info */
      cur_time = time (NULL);
      if (check_copied_info_temp && check_master_info_temp)
	{
	  if (!LSA_ISNULL (&initial_copied_append_lsa))
	    {
	      process_rate =
		(float) (copied_append_lsa.pageid - initial_copied_append_lsa.pageid) / (cur_time - start_time);
	    }
	  else
	    {
	      initial_copied_append_lsa = copied_append_lsa;
	      process_rate = 0.0f;
	    }

	  printf ("\n *** Delay in Copying Active Log *** \n");
	  la_print_delay_info (copied_append_lsa, master_eof_lsa, process_rate);
	}

      if (check_applied_info_temp)
	{
	  if (!LSA_ISNULL (&initial_applied_final_lsa))
	    {
	      process_rate =
		(float) (applied_final_lsa.pageid - initial_applied_final_lsa.pageid) / (cur_time - start_time);
	    }
	  else
	    {
	      initial_applied_final_lsa = applied_final_lsa;
	      process_rate = 0.0f;
	    }

	  printf ("\n *** Delay in Applying Copied Log *** \n");
	  la_print_delay_info (applied_final_lsa, copied_eof_lsa, process_rate);
	}

      check_copied_info_temp = check_copied_info;
      check_applied_info_temp = check_applied_info;
      check_master_info_temp = check_master_info;

      sleep (interval);
    }
  while (interval > 0 && !is_Sigint_caught);

  return EXIT_SUCCESS;

print_applyinfo_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_APPLYINFO, APPLYINFO_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

  return EXIT_FAILURE;
#else /* CS_MODE */
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_APPLYINFO, APPLYINFO_MSG_NOT_IN_STANDALONE),
	   basename (arg->argv0));
  return EXIT_FAILURE;
#endif /* !CS_MODE */
#endif /* !WINDOWS */
}

/*
 * intr_handler() - Interrupt handler for utility
 *   return: none
 *   sig_no(in)
 */
#if defined(WINDOWS)
static BOOL WINAPI
#else
static void
#endif
intr_handler (int sig_no)
{
  is_Sigint_caught = true;

#if defined(WINDOWS)
  if (sig_no == CTRL_C_EVENT)
    {
      return TRUE;
    }

  return FALSE;
#endif /* WINDOWS */
}

/*
 * tranlist_cmp_f() - qsort compare function used in tranlist().
 *   return:
 */
static int
tranlist_cmp_f (const void *p1, const void *p2)
{
  int ret;
  SORT_COLUMN_TYPE column_type;
  const ONE_TRAN_INFO *info1, *info2;
  const char *str_key1 = NULL, *str_key2 = NULL;
  double number_key1 = 0, number_key2 = 0;

  info1 = (ONE_TRAN_INFO *) p1;
  info2 = (ONE_TRAN_INFO *) p2;

  switch (tranlist_Sort_column)
    {
    case 0:
    case 1:
      number_key1 = info1->tran_index;
      number_key2 = info2->tran_index;
      column_type = SORT_COLUMN_TYPE_INT;
      break;
    case 2:
      str_key1 = info1->db_user;
      str_key2 = info2->db_user;
      column_type = SORT_COLUMN_TYPE_STR;
      break;
    case 3:
      str_key1 = info1->host_name;
      str_key2 = info2->host_name;
      column_type = SORT_COLUMN_TYPE_STR;
      break;
    case 4:
      number_key1 = info1->process_id;
      number_key2 = info2->process_id;
      column_type = SORT_COLUMN_TYPE_INT;
      break;
    case 5:
      str_key1 = info1->program_name;
      str_key2 = info2->program_name;
      column_type = SORT_COLUMN_TYPE_STR;
      break;
    case 6:
      number_key1 = info1->query_exec_info.query_time;
      number_key2 = info2->query_exec_info.query_time;
      column_type = SORT_COLUMN_TYPE_FLOAT;
      break;
    case 7:
      number_key1 = info1->query_exec_info.tran_time;
      number_key2 = info2->query_exec_info.tran_time;
      column_type = SORT_COLUMN_TYPE_FLOAT;
      break;
    case 8:
      str_key1 = info1->query_exec_info.wait_for_tran_index_string;
      str_key2 = info2->query_exec_info.wait_for_tran_index_string;
      column_type = SORT_COLUMN_TYPE_STR;
      break;
    case 9:
      str_key1 = info1->query_exec_info.sql_id;
      str_key2 = info2->query_exec_info.sql_id;
      column_type = SORT_COLUMN_TYPE_STR;
      break;
    case 10:
      str_key1 = info1->query_exec_info.query_stmt;
      str_key2 = info2->query_exec_info.query_stmt;
      column_type = SORT_COLUMN_TYPE_STR;
      break;
    default:
      assert (0);
      return 0;
    }

  switch (column_type)
    {
    case SORT_COLUMN_TYPE_INT:
    case SORT_COLUMN_TYPE_FLOAT:
      {
	if (number_key1 == number_key2)
	  {
	    ret = 0;
	  }
	else if (number_key1 > number_key2)
	  {
	    ret = 1;
	  }
	else
	  {
	    ret = -1;
	  }
      }
      break;
    case SORT_COLUMN_TYPE_STR:
      {
	if (str_key1 == NULL && str_key2 == NULL)
	  {
	    ret = 0;
	  }
	else if (str_key1 == NULL && str_key2 != NULL)
	  {
	    ret = -1;
	  }
	else if (str_key1 != NULL && str_key2 == NULL)
	  {
	    ret = 1;
	  }
	else
	  {
	    ret = strcmp (str_key1, str_key2);
	  }
      }
      break;
    default:
      assert (0);
      ret = 0;
    }

  if (tranlist_Sort_desc == true)
    {
      ret *= (-1);
    }

  return ret;
}

/*
 * vacuumdb() - vacuumdb main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
vacuumdb (UTIL_FUNCTION_ARG * arg)
{
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *database_name, *output_file = NULL;
  bool dump_flag;
  FILE *outfp = NULL;

  if (utility_get_option_string_table_size (arg_map) < 1)
    {
      goto print_check_usage;
    }

  database_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_check_usage;
    }

  if (check_database_name (database_name))
    {
      goto error_exit;
    }

  dump_flag = utility_get_option_bool_value (arg_map, VACUUM_DUMP_S);

  if (dump_flag)
    {
      output_file = utility_get_option_string_value (arg_map, VACUUM_OUTPUT_FILE_S, 0);
      if (output_file == NULL)
	{
	  outfp = stdout;
	}
      else
	{
	  outfp = fopen (output_file, "w");
	  if (outfp == NULL)
	    {
	      PRINT_AND_LOG_ERR_MSG (msgcat_message
				     (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_VACUUMDB, VACUUMDB_MSG_BAD_OUTPUT),
				     output_file);
	      goto error_exit;
	    }
	}
    }

#if defined(SA_MODE)
  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  sysprm_set_force (prm_get_name (PRM_ID_JAVA_STORED_PROCEDURE), "no");

  AU_DISABLE_PASSWORDS ();
  if (dump_flag)
    {
      db_set_client_type (DB_CLIENT_TYPE_SKIP_VACUUM_ADMIN_CSQL);
    }
  else
    {
      sysprm_set_force (prm_get_name (PRM_ID_DISABLE_VACUUM), "no");
      db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
    }
  db_login ("DBA", NULL);
  if (db_restart (arg->command_name, TRUE, database_name) == NO_ERROR)
    {
      (void) db_set_isolation (TRAN_READ_COMMITTED);

      if (dump_flag)
	{
	  vacuum_dump (outfp);

	  if (outfp != stdout)
	    {
	      fclose (outfp);
	    }
	}
      else
	{
	  if (cvacuum () != NO_ERROR)
	    {
	      const char *tmpname;

	      if ((tmpname = er_get_msglog_filename ()) == NULL)
		{
		  tmpname = "/dev/null";
		}
	      PRINT_AND_LOG_ERR_MSG (msgcat_message
				     (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_VACUUMDB, VACUUMDB_MSG_FAILED), tmpname);

	      util_log_write_errstr ("%s\n", db_error_string (3));
	      db_shutdown ();
	      goto error_exit;
	    }
	}
      db_shutdown ();
    }
  else
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }

  return EXIT_SUCCESS;
#else
  if (dump_flag)
    {
      /* error message log file */
      snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", database_name, arg->command_name);
      er_init (er_msg_file, ER_NEVER_EXIT);

      AU_DISABLE_PASSWORDS ();
      db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
      db_login ("DBA", NULL);

      if (db_restart (arg->command_name, TRUE, database_name) != NO_ERROR)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
	  goto error_exit;
	}

      (void) db_set_isolation (TRAN_READ_COMMITTED);

      vacuum_dump (outfp);
      db_shutdown ();

      if (outfp != stdout)
	{
	  fclose (outfp);
	}
    }
  else
    {
      fprintf (stderr,
	       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_VACUUMDB,
			       VACUUMDB_MSG_CLIENT_SERVER_NOT_AVAILABLE), basename (arg->argv0));
      util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
    }

  return EXIT_SUCCESS;
#endif
print_check_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_VACUUMDB, VACUUMDB_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

error_exit:

  return EXIT_FAILURE;
}

/*
 * tde() - tde main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
tde (UTIL_FUNCTION_ARG * arg)
{
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  char mk_path[PATH_MAX] = { 0, };
  const char *database_name;
  const char *dba_password;
  char *passbuf;
  int error = NO_ERROR;
  int vdes = NULL_VOLDES;
  bool gen_op;
  bool show_op;
  bool print_val;
  int change_op_idx;
  int delete_op_idx;
  int op_cnt = 0;

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_tde_usage;
    }

  database_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_tde_usage;
    }

  gen_op = utility_get_option_bool_value (arg_map, TDE_GENERATE_KEY_S);
  show_op = utility_get_option_bool_value (arg_map, TDE_SHOW_KEYS_S);
  change_op_idx = utility_get_option_int_value (arg_map, TDE_CHANGE_KEY_S);
  delete_op_idx = utility_get_option_int_value (arg_map, TDE_DELETE_KEY_S);

  print_val = utility_get_option_bool_value (arg_map, TDE_PRINT_KEY_VALUE_S);
  dba_password = utility_get_option_string_value (arg_map, TDE_DBA_PASSWORD_S, 0);

  if (gen_op)
    {
      op_cnt++;
    }
  if (show_op)
    {
      op_cnt++;
    }
  if (change_op_idx != -1)
    {
      op_cnt++;
    }
  if (delete_op_idx != -1)
    {
      op_cnt++;
    }

  if (op_cnt != 1)
    {
      /* Only one and at least one operation has to be given */
      /* -c -1 -d -1 -n is now allowed, but it's trivial */
      goto print_tde_usage;
    }

  /* Checking input range, -1 means not the operation case */
  if (change_op_idx < -1)
    {
      goto print_tde_usage;
    }
  if (delete_op_idx < -1)
    {
      goto print_tde_usage;
    }

  /* extra validation */
  if (check_database_name (database_name))
    {
      goto error_exit;
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  if (db_login ("DBA", dba_password) != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }

  /* first try to restart with the password given (possibly none) */
  error = db_restart (arg->command_name, TRUE, database_name);
  if (error)
    {
      if (error == ER_AU_INVALID_PASSWORD && (dba_password == NULL || strlen (dba_password) == 0))
	{
	  /*
	   * prompt for a valid password and try again, need a reusable
	   * password prompter so we can use getpass() on platforms that
	   * support it.
	   */

	  /* get password interactively if interactive mode */
	  passbuf = getpass (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_TDE, TDE_MSG_DBA_PASSWORD));
	  if (passbuf[0] == '\0')	/* to fit into db_login protocol */
	    {
	      passbuf = (char *) NULL;
	    }
	  dba_password = passbuf;
	  if (db_login ("DBA", dba_password) != NO_ERROR)
	    {
	      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
	      goto error_exit;
	    }
	  else
	    {
	      error = db_restart (arg->command_name, TRUE, database_name);
	    }
	}

      if (error)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
	  goto error_exit;
	}
    }

  if (tde_get_mk_file_path (mk_path) != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      db_shutdown ();
      goto error_exit;
    }

  printf ("Key File: %s\n", mk_path);

  /* 
   * The file lock here is necessary to provide exclusiveness with backupdb in the current design.
   */
  vdes = fileio_mount (NULL, database_name, mk_path, LOG_DBTDE_KEYS_VOLID, 1, false);
  if (vdes == NULL_VOLDES)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      db_shutdown ();
      goto error_exit;
    }
  /* 
   * There is no need to call fileio_dismount() for 'vdes' later in this function
   * because it is dismounted in db_shutdown() 
   * */

  printf ("\n");
  if (gen_op)
    {
      unsigned char master_key[TDE_MASTER_KEY_LENGTH];
      int mk_index = -1;
      time_t created_time;
      char ctime_buf[CTIME_MAX];

      if (tde_create_mk (master_key, &created_time) != NO_ERROR)
	{
	  PRINT_AND_LOG_ERR_MSG ("FAILURE: %s\n", db_error_string (3));
	  db_shutdown ();
	  goto error_exit;
	}
      if (tde_add_mk (vdes, master_key, created_time, &mk_index) != NO_ERROR)
	{
	  PRINT_AND_LOG_ERR_MSG ("FAILURE: %s\n", db_error_string (3));
	  db_shutdown ();
	  goto error_exit;
	}
      ctime_r (&created_time, ctime_buf);
      printf ("SUCCESS: ");
      printf (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_TDE, TDE_MSG_MK_GENERATED), mk_index, ctime_buf);
      if (print_val)
	{
	  printf ("Key: ");
	  tde_print_mk (master_key);
	  printf ("\n");
	}
    }
  else if (show_op)
    {
      int mk_index;
      time_t created_time, set_time;
      char ctime_buf1[CTIME_MAX];
      char ctime_buf2[CTIME_MAX];

      printf ("The current key set on %s:\n", database_name);
      if (tde_get_mk_info (&mk_index, &created_time, &set_time) == NO_ERROR)
	{
	  ctime_r (&created_time, ctime_buf1);
	  ctime_r (&set_time, ctime_buf2);

	  printf ("Key Index: %d\n", mk_index);
	  printf ("Created on %s", ctime_buf1);
	  printf ("Set     on %s", ctime_buf2);
	}
      else
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
	  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_TDE, TDE_MSG_NO_SET_MK_INFO));
	}
      printf ("\n");
      if (tde_dump_mks (vdes, print_val) != NO_ERROR)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
	  db_shutdown ();
	  goto error_exit;
	}
    }
  else if (change_op_idx != -1)
    {
      int prev_mk_idx;
      time_t created_time, set_time;

      if (tde_get_mk_info (&prev_mk_idx, &created_time, &set_time) != NO_ERROR)
	{
	  PRINT_AND_LOG_ERR_MSG ("FAILURE: %s\n", db_error_string (3));
	  db_shutdown ();
	  goto error_exit;
	}

      printf (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_TDE, TDE_MSG_MK_CHANGING), prev_mk_idx,
	      change_op_idx);
      /* no need to check if the previous key exists or not. It is going to be checked on changing on server */
      if (tde_change_mk_on_server (change_op_idx) != NO_ERROR)
	{
	  PRINT_AND_LOG_ERR_MSG ("FAILURE: %s\n", db_error_string (3));
	  db_shutdown ();
	  goto error_exit;
	}

      if (db_commit_transaction () != NO_ERROR)
	{
	  PRINT_AND_LOG_ERR_MSG ("FAILURE: %s\n", db_error_string (3));
	  db_shutdown ();
	  goto error_exit;
	}

      printf ("SUCCESS: ");
      printf (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_TDE, TDE_MSG_MK_CHANGED), prev_mk_idx,
	      change_op_idx);
    }
  else if (delete_op_idx != -1)
    {
      int mk_index;
      time_t created_time, set_time;

      if (tde_get_mk_info (&mk_index, &created_time, &set_time) != NO_ERROR)
	{
	  PRINT_AND_LOG_ERR_MSG ("FAILURE: %s\n", db_error_string (3));
	  db_shutdown ();
	  goto error_exit;
	}
      if (mk_index == delete_op_idx)
	{
	  PRINT_AND_LOG_ERR_MSG ("FAILURE: %s",
				 msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_TDE,
						 TDE_MSG_MK_SET_ON_DATABASE_DELETE));
	  db_shutdown ();
	  goto error_exit;
	}
      if (tde_delete_mk (vdes, delete_op_idx) != NO_ERROR)
	{
	  PRINT_AND_LOG_ERR_MSG ("FAILURE: %s\n", db_error_string (3));
	  db_shutdown ();
	  goto error_exit;
	}
      printf ("SUCCESS: ");
      printf (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_TDE, TDE_MSG_MK_DELETED), delete_op_idx);
    }

  db_shutdown ();

  return EXIT_SUCCESS;

print_tde_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_TDE, TDE_MSG_USAGE), basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
error_exit:
  return EXIT_FAILURE;
}

static void
clean_stdin ()
{
  int c;

  /* consumes all the values in the input buffer */
  do
    {
      c = getchar ();
    }
  while (c != '\n' && c != EOF);
}

static time_t
parse_date_string_to_time (char *date_string)
{
  assert (date_string != NULL);

  time_t result = 0;
  struct tm time_data = { };

  if (sscanf
      (date_string, "%d-%d-%d:%d:%d:%d", &time_data.tm_mday, &time_data.tm_mon, &time_data.tm_year, &time_data.tm_hour,
       &time_data.tm_min, &time_data.tm_sec) != 6)
    {
      return 0;
    }

  if (time_data.tm_mday < 1 || time_data.tm_mday > 31
      || time_data.tm_mon < 1 || time_data.tm_mon > 12
      || time_data.tm_year < 1900
      || time_data.tm_hour < 0 || time_data.tm_hour > 23
      || time_data.tm_min < 0 || time_data.tm_min > 59 || time_data.tm_sec < 0 || time_data.tm_sec > 59)
    {
      return 0;
    }

  time_data.tm_mon -= 1;
  time_data.tm_year -= 1900;
  time_data.tm_isdst = -1;

  result = mktime (&time_data);

  return result < 0 ? 0 : result;
}

int
flashback (UTIL_FUNCTION_ARG * arg)
{
#if defined (CS_MODE)
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];

  const char *database_name = NULL;
  int num_tables = 0;
  dynamic_array *darray = NULL;
  int i = 0;
  char table_name_buf[SM_MAX_IDENTIFIER_LENGTH];
  char *table_name = NULL;

  char *invalid_class = NULL;
  int invalid_class_idx = 0;

  time_t invalid_time = 0;

  OID *oid_list = NULL;

  const char *output_file = NULL;
  FILE *outfp = NULL;
  char *user = NULL;
  const char *dba_password = NULL;

  char *start_date = NULL;
  char *end_date = NULL;
  time_t start_time = 0;
  time_t end_time = 0;

  bool is_detail = false;
  bool is_oldest = false;

  char *passbuf = NULL;
  int error = NO_ERROR;

  int trid = 0;

  int num_item = 5;
  char *loginfo_list = NULL;

  FLASHBACK_SUMMARY_INFO_MAP summary_info;
  FLASHBACK_SUMMARY_INFO *summary_entry = NULL;

  bool need_shutdown = false;

  LOG_LSA start_lsa = LSA_INITIALIZER;
  LOG_LSA end_lsa = LSA_INITIALIZER;

  int timeout = 0;

  time_t current_time = time (NULL);

  num_tables = utility_get_option_string_table_size (arg_map) - 1;
  if (num_tables < 1)
    {
      goto print_flashback_usage;
    }

  database_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_flashback_usage;
    }

  if (check_database_name (database_name))
    {
      goto error_exit;
    }

  output_file = utility_get_option_string_value (arg_map, FLASHBACK_OUTPUT_S, 0);
  user = utility_get_option_string_value (arg_map, FLASHBACK_USER_S, 0);
  dba_password = utility_get_option_string_value (arg_map, FLASHBACK_DBA_PASSWORD_S, 0);
  start_date = utility_get_option_string_value (arg_map, FLASHBACK_START_DATE_S, 0);
  end_date = utility_get_option_string_value (arg_map, FLASHBACK_END_DATE_S, 0);
  is_detail = utility_get_option_bool_value (arg_map, FLASHBACK_DETAIL_S);
  is_oldest = utility_get_option_bool_value (arg_map, FLASHBACK_OLDEST_S);

  /* create table list */
  /* class existence and classoid will be found at server side. if is checked at utility side, it needs addtional access to the server through locator */
  darray = da_create (num_tables, SM_MAX_IDENTIFIER_LENGTH);
  if (darray == NULL)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_NO_MEM);
      goto error_exit;
    }

  /* start date check */
  if (start_date != NULL && strlen (start_date) > 0)
    {
      start_time = parse_date_string_to_time (start_date);
      if (start_time == 0)
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message
				 (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_FLASHBACK, FLASHBACK_MSG_INVALID_DATE_FORMAT));
	  goto error_exit;
	}
    }

  /* end date check */
  if (end_date != NULL && strlen (end_date) > 0)
    {
      end_time = parse_date_string_to_time (end_date);
      if (end_time == 0)
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message
				 (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_FLASHBACK, FLASHBACK_MSG_INVALID_DATE_FORMAT));
	  goto error_exit;
	}
    }

  /* start time and end time setting.
   * 1. 10 minutes before end time, if only end time is specified
   * 2. 10 minutes after start time, if only start time is specified
   * 3. if no time is specified, then get current execution time for end time */

  if (start_time == 0 && end_time != 0)
    {
      start_time = end_time - 600;
    }
  else if (start_time != 0 && end_time == 0)
    {
      end_time = start_time + 600;
    }
  else if (start_time == 0 && end_time == 0)
    {
      end_time = time (NULL);
      start_time = end_time - 600;
    }

  /* 1. start time < end time
   * 2. start time is required to be set after db_creation time (server side check)
   * 3. start time, and end time are required to be set within the log volume range (server side check) */
  if (start_time >= end_time)
    {
      char sdate_buf[20];
      char edate_buf[20];

      if (start_date == NULL)
	{
	  strftime (sdate_buf, 20, "%d-%m-%Y:%H:%M:%S", localtime (&start_time));
	  start_date = sdate_buf;
	}

      if (end_date == NULL)
	{
	  strftime (edate_buf, 20, "%d-%m-%Y:%H:%M:%S", localtime (&end_time));
	  end_date = edate_buf;
	}

      PRINT_AND_LOG_ERR_MSG (msgcat_message
			     (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_FLASHBACK, FLASHBACK_MSG_INVALID_DATE_RANGE),
			     start_date, end_date);
      goto error_exit;
    }

  /* output file check */
  if (output_file == NULL)
    {
      outfp = stdout;
    }
  else
    {
      outfp = fopen (output_file, "w");
      if (outfp == NULL)
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message
				 (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_FLASHBACK, FLASHBACK_MSG_BAD_OUTPUT),
				 output_file);

	  goto error_exit;
	}
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  if (db_login ("DBA", dba_password) != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }

  /* first try to restart with the password given (possibly none) */
  error = db_restart (arg->command_name, TRUE, database_name);
  if (error)
    {
      if (error == ER_AU_INVALID_PASSWORD && (dba_password == NULL || strlen (dba_password) == 0))
	{
	  /*
	   * prompt for a valid password and try again, need a reusable
	   * password prompter so we can use getpass() on platforms that
	   * support it.
	   */

	  /* get password interactively if interactive mode */
	  passbuf =
	    getpass (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_FLASHBACK, FLASHBACK_MSG_DBA_PASSWORD));
	  if (passbuf[0] == '\0')	/* to fit into db_login protocol */
	    {
	      passbuf = (char *) NULL;
	    }
	  dba_password = passbuf;
	  if (db_login ("DBA", dba_password) != NO_ERROR)
	    {
	      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
	      goto error_exit;
	    }
	  else
	    {
	      error = db_restart (arg->command_name, TRUE, database_name);
	    }
	}

      if (error)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
	  goto error_exit;
	}
    }

  need_shutdown = true;

  if (!prm_get_integer_value (PRM_ID_SUPPLEMENTAL_LOG))
    {
      PRINT_AND_LOG_ERR_MSG (msgcat_message
			     (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_FLASHBACK, FLASHBACK_MSG_NO_SUPPLEMENTAL_LOG));
      goto error_exit;
    }

  for (i = 0; i < num_tables; i++)
    {
      table_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, i + 1);

      if (sm_check_system_class_by_name (table_name))
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message
				 (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_FLASHBACK,
				  FLASHBACK_MSG_SYSTEM_CLASS_NOT_SUPPORTED), table_name);

	  goto error_exit;
	}

      if (utility_check_class_name (table_name) != NO_ERROR)
	{
	  goto error_exit;
	}

      strncpy_bufsize (table_name_buf, table_name);

      if (da_add (darray, table_name_buf) != NO_ERROR)
	{
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_NO_MEM);
	  goto error_exit;
	}
    }

  oid_list = (OID *) malloc (sizeof (OID) * num_tables);
  if (oid_list == NULL)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_NO_MEM);
      goto error_exit;
    }

  error =
    flashback_get_and_show_summary (darray, user, start_time, end_time, &summary_info, &oid_list, &invalid_class,
				    &invalid_time);
  if (error != NO_ERROR)
    {
      /* print error message */
      switch (error)
	{
	case ER_FLASHBACK_INVALID_TIME:
	  {
	    char db_creation_time[20];
	    char current_time_buf[20];

	    strftime (db_creation_time, 20, "%d-%m-%Y:%H:%M:%S", localtime (&invalid_time));
	    strftime (current_time_buf, 20, "%d-%m-%Y:%H:%M:%S", localtime (&current_time));

	    PRINT_AND_LOG_ERR_MSG (msgcat_message
				   (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_FLASHBACK, FLASHBACK_MSG_INVALID_TIME),
				   current_time_buf, db_creation_time);
	    break;
	  }
	case ER_FLASHBACK_INVALID_CLASS:
	  PRINT_AND_LOG_ERR_MSG (msgcat_message
				 (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_FLASHBACK, FLASHBACK_MSG_TABLE_NOT_EXIST),
				 invalid_class);
	  free_and_init (invalid_class);

	  break;
	case ER_FLASHBACK_EXCEED_MAX_NUM_TRAN_TO_SUMMARY:
	  PRINT_AND_LOG_ERR_MSG (msgcat_message
				 (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_FLASHBACK, FLASHBACK_MSG_TOO_MANY_TRANSACTION));
	  break;
	case ER_FLASHBACK_DUPLICATED_REQUEST:
	  PRINT_AND_LOG_ERR_MSG (msgcat_message
				 (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_FLASHBACK, FLASHBACK_MSG_DUPLICATED_REQUEST));

	default:
	  break;
	}

      goto error_exit;
    }

  if (summary_info.empty ())
    {
      goto error_exit;
    }

  timeout = prm_get_integer_value (PRM_ID_FLASHBACK_TIMEOUT);

  while (summary_entry == NULL)
    {
      POLL_FD input_fd = { STDIN_FILENO, POLLIN | POLLPRI, 0 };

      printf ("Enter transaction id (press -1 to quit): ");
      fflush (stdout);

      if (poll (&input_fd, 1, timeout * 1000))
	{
	  if (scanf ("%d", &trid) != 1)
	    {
	      /* When non integer value is input, the input buffer must be flushed. */
	      clean_stdin ();

	      continue;
	    }
	}
      else
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message
				 (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_FLASHBACK, FLASHBACK_MSG_TIMEOUT), timeout);

	  goto error_exit;
	}

      if (trid == -1)
	{
	  goto error_exit;
	}

      FLASHBACK_FIND_SUMMARY_ENTRY (trid, summary_info, summary_entry);
      if (summary_entry == NULL)
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message
				 (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_FLASHBACK, FLASHBACK_MSG_INVALID_TRANSACTION),
				 trid);
	}

      printf ("\n");
    }

  start_lsa = summary_entry->start_lsa;
  end_lsa = summary_entry->end_lsa;
  user = summary_entry->user;

  do
    {
      error =
	flashback_get_loginfo (trid, user, oid_list, num_tables, &start_lsa, &end_lsa, &num_item, is_oldest,
			       &loginfo_list, &invalid_class_idx);
      if (error != NO_ERROR)
	{
	  switch (error)
	    {
	    case ER_FLASHBACK_SCHEMA_CHANGED:
	      {
		char classname[SM_MAX_IDENTIFIER_LENGTH];
		da_get (darray, invalid_class_idx, classname);

		PRINT_AND_LOG_ERR_MSG (msgcat_message
				       (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_FLASHBACK,
					FLASHBACK_MSG_TABLE_SCHEMA_CHANGED), classname);
		break;
	      }
	    case ER_FLASHBACK_LOG_NOT_EXIST:
	      PRINT_AND_LOG_ERR_MSG (msgcat_message
				     (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_FLASHBACK,
				      FLASHBACK_MSG_LOG_VOLUME_NOT_EXIST));
	      break;
	    default:
	      break;
	    }

	  goto error_exit;
	}

      error = flashback_print_loginfo (loginfo_list, num_item, darray, oid_list, is_detail, outfp);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}

    }
  while (!LSA_ISNULL (&start_lsa) && !LSA_ISNULL (&end_lsa) && LSA_LT (&start_lsa, &end_lsa));

  db_shutdown ();

  da_destroy (darray);

  free_and_init (oid_list);

  if (loginfo_list != NULL)
    {
      free_and_init (loginfo_list);
    }

  return EXIT_SUCCESS;

print_flashback_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_FLASHBACK, FLASHBACK_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
error_exit:

  if (need_shutdown)
    {
      db_shutdown ();
    }

  if (darray != NULL)
    {
      da_destroy (darray);
    }

  if (oid_list != NULL)
    {
      free_and_init (oid_list);
    }

  if (loginfo_list != NULL)
    {
      free_and_init (loginfo_list);
    }

  return EXIT_FAILURE;

#else /* CS_MODE */
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_FLASHBACK, FLASHBACK_MSG_NOT_IN_STANDALONE),
	   basename (arg->argv0));
  return EXIT_FAILURE;
#endif /* !CS_MODE */

}

int
memmon (UTIL_FUNCTION_ARG * arg)
{
#if defined(CS_MODE)
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *database_name;
  bool need_shutdown = false;
  const char *outfile_name;
  FILE *outfile_fp = NULL;
  int error_code = NO_ERROR;
  MMON_SERVER_INFO server_info;

  outfile_name = utility_get_option_string_value (arg_map, MEMMON_OUTPUT_S, 0);

  database_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_memmon_usage;
    }

  if (check_database_name (database_name))
    {
      goto error_exit;
    }

  if (outfile_name)
    {
      outfile_fp = fopen (outfile_name, "w+");
      if (outfile_fp == NULL)
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message
				 (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MEMMON, MEMMON_MSG_CANNOT_OPEN_OUTPUT_FILE),
				 outfile_name);
	  goto error_exit;
	}
    }
  else
    {
      outfile_fp = stdout;
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  if (db_restart (arg->command_name, TRUE, database_name))
    {
      PRINT_AND_LOG_ERR_MSG ("%s: %s. \n\n", arg->command_name, db_error_string (3));
      goto error_exit;
    }
  need_shutdown = true;

  if (!prm_get_bool_value (PRM_ID_ENABLE_MEMORY_MONITORING))
    {
      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MEMMON, MEMMON_MSG_NOT_SUPPORTED));
      goto error_exit;
    }

  /* execute phase */
  error_code = mmon_get_server_info (server_info);
  if (error_code != NO_ERROR)
    {
      goto error_exit;
    }

  mmon_print_server_info (server_info, outfile_fp);

  fclose (outfile_fp);

  db_shutdown ();

  return EXIT_SUCCESS;

print_memmon_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MEMMON, MEMMON_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

error_exit:
  if (need_shutdown)
    {
      db_shutdown ();
    }

  if (outfile_fp)
    {
      fclose (outfile_fp);
    }

  return EXIT_FAILURE;
#else /* CS_MODE */
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MEMMON, MEMMON_MSG_NOT_IN_STANDALONE),
	   basename (arg->argv0));
  return EXIT_FAILURE;
#endif /* !CS_MODE */
}
