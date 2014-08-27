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
#include "schema_manager.h"
#include "locator_cl.h"
#include "dynamic_array.h"
#include "util_func.h"
#if !defined(WINDOWS)
#include "heartbeat.h"
#endif

#define PASSBUF_SIZE 12
#define SPACEDB_NUM_VOL_PURPOSE 5
#define MAX_KILLTRAN_INDEX_LIST_NUM  64

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
static int spacedb_get_size_str (char *buf, UINT64 num_pages,
				 T_SPACEDB_SIZE_UNIT size_unit);
static void print_timestamp (FILE * outfp);
static int print_tran_entry (const ONE_TRAN_INFO * tran_info,
			     TRANDUMP_LEVEL dump_level);
static int tranlist_cmp_f (const void *p1, const void *p2);
static OID *util_get_class_oids_and_index_btid (dynamic_array * darray,
						const char *index_name,
						BTID * index_btid);

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
  bool compress_flag;
  bool sa_mode;
  FILEIO_ZIP_METHOD backup_zip_method = FILEIO_ZIP_NONE_METHOD;
  FILEIO_ZIP_LEVEL backup_zip_level = FILEIO_ZIP_NONE_LEVEL;
  bool skip_activelog = false;
  int sleep_msecs;
  struct stat st_buf;
  char real_pathbuf[PATH_MAX];
  char verbose_file_realpath[PATH_MAX];

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_backup_usage;
    }

  database_name = utility_get_option_string_value (arg_map,
						   OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_backup_usage;
    }

  backup_path = utility_get_option_string_value (arg_map,
						 BACKUP_DESTINATION_PATH_S,
						 0);
  remove_log_archives = utility_get_option_bool_value (arg_map,
						       BACKUP_REMOVE_ARCHIVE_S);
  backup_level = utility_get_option_int_value (arg_map, BACKUP_LEVEL_S);
  backup_verbose_file = utility_get_option_string_value (arg_map,
							 BACKUP_OUTPUT_FILE_S,
							 0);
  no_check = utility_get_option_bool_value (arg_map, BACKUP_NO_CHECK_S);
  check = !no_check;
  backup_num_threads = utility_get_option_int_value (arg_map,
						     BACKUP_THREAD_COUNT_S);
  compress_flag = utility_get_option_bool_value (arg_map, BACKUP_COMPRESS_S);
  skip_activelog = utility_get_option_bool_value (arg_map,
						  BACKUP_EXCEPT_ACTIVE_LOG_S);
  sleep_msecs = utility_get_option_int_value (arg_map, BACKUP_SLEEP_MSECS_S);
  sa_mode = utility_get_option_bool_value (arg_map, BACKUP_SA_MODE_S);

  /* Range checking of input */
  if (backup_level < 0 || backup_level >= FILEIO_BACKUP_UNDEFINED_LEVEL)
    {
      goto print_backup_usage;
    }

  if (sa_mode && backup_num_threads > 1)
    {
      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
					     MSGCAT_UTIL_SET_BACKUPDB,
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

  if (compress_flag)
    {
      backup_zip_method = FILEIO_ZIP_LZO1X_METHOD;
      backup_zip_level = FILEIO_ZIP_LZO1X_DEFAULT_LEVEL;
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

      if (stat (backup_path, &st_buf) != 0 || !S_ISDIR (st_buf.st_mode))
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
						 MSGCAT_UTIL_SET_BACKUPDB,
						 BACKUPDB_INVALID_PATH));
	  goto error_exit;
	}
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1,
	    "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  sysprm_set_force (prm_get_name (PRM_ID_JAVA_STORED_PROCEDURE), "no");

  AU_DISABLE_PASSWORDS ();	/* disable authorization for this operation */
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("DBA", NULL);

  if (db_restart (arg->command_name, TRUE, database_name) == NO_ERROR)
    {
      if (check)
	{
	  int check_flag = 0;

	  check_flag |= CHECKDB_FILE_TRACKER_CHECK;
	  check_flag |= CHECKDB_HEAP_CHECK_ALLHEAPS;
	  check_flag |= CHECKDB_CT_CHECK_CAT_CONSISTENCY;
	  check_flag |= CHECKDB_BTREE_CHECK_ALL_BTREES;
	  check_flag |= CHECKDB_LC_CHECK_CLASSNAMES;

	  if (db_set_isolation (TRAN_READ_COMMITTED) != NO_ERROR
	      || boot_check_db_consistency (check_flag, 0, 0,
					    NULL) != NO_ERROR)
	    {
	      const char *tmpname;
	      if ((tmpname = er_get_msglog_filename ()) == NULL)
		{
		  tmpname = "/dev/null";
		}
	      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
						     MSGCAT_UTIL_SET_CHECKDB,
						     CHECKDB_MSG_INCONSISTENT),
				     tmpname);
	      db_shutdown ();
	      goto error_exit;
	    }
	}

      /* some other utilities may need interrupt handler too */
      if (os_set_signal_handler (SIGINT,
				 backupdb_sig_interrupt_handler) == SIG_ERR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
	  db_shutdown ();
	  goto error_exit;
	}

      if (backup_verbose_file
	  && *backup_verbose_file && *backup_verbose_file != '/')
	{
	  char dirname[PATH_MAX];

	  /* resolve relative path */
	  if (getcwd (dirname, PATH_MAX) != NULL)
	    {
	      snprintf (verbose_file_realpath, PATH_MAX - 1, "%s/%s", dirname,
			backup_verbose_file);
	      backup_verbose_file = verbose_file_realpath;
	    }
	}

      if (boot_backup (backup_path, (FILEIO_BACKUP_LEVEL) backup_level,
		       remove_log_archives, backup_verbose_file,
		       backup_num_threads,
		       backup_zip_method, backup_zip_level,
		       skip_activelog, sleep_msecs) == NO_ERROR)
	{
	  if (db_commit_transaction () != NO_ERROR)
	    {
	      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
	    }
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

print_backup_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_BACKUPDB,
				   BACKUPDB_MSG_USAGE),
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

  database_name = utility_get_option_string_value (arg_map,
						   OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_addvol_usage;
    }

  volext_npages_string = utility_get_option_string_value (arg_map,
							  OPTION_STRING_TABLE,
							  1);
  if (volext_npages_string)
    {
      util_print_deprecated ("number-of-pages");
      ext_info.max_npages = atoi (volext_npages_string);
    }

  volext_size = 0;
  volext_size_str = utility_get_option_string_value (arg_map,
						     ADDVOL_VOLUME_SIZE_S, 0);
  if (volext_size_str)
    {
      if (util_size_string_to_byte (&volext_size,
				    volext_size_str) != NO_ERROR)
	{
	  goto print_addvol_usage;
	}
    }

  volext_max_writesize_in_sec_str =
    utility_get_option_string_value (arg_map, ADDVOL_MAX_WRITESIZE_IN_SEC_S,
				     0);
  if (volext_max_writesize_in_sec_str)
    {
      if (util_size_string_to_byte (&volext_max_writesize,
				    volext_max_writesize_in_sec_str) !=
	  NO_ERROR)
	{
	  goto print_addvol_usage;
	}
      ext_info.max_writesize_in_sec = volext_max_writesize / ONE_K;
    }
  else
    {
      ext_info.max_writesize_in_sec = 0;
    }

  ext_info.name = utility_get_option_string_value (arg_map,
						   ADDVOL_VOLUME_NAME_S, 0);
  ext_info.path = utility_get_option_string_value (arg_map,
						   ADDVOL_FILE_PATH_S, 0);
  if (ext_info.path != NULL)
    {
      memset (real_volext_path_buf, 0, sizeof (real_volext_path_buf));
      if (realpath (ext_info.path, real_volext_path_buf) != NULL)
	{
	  ext_info.path = real_volext_path_buf;
	}
    }
  ext_info.comments = utility_get_option_string_value (arg_map,
						       ADDVOL_COMMENT_S, 0);
  volext_string_purpose = utility_get_option_string_value (arg_map,
							   ADDVOL_PURPOSE_S,
							   0);
  if (volext_string_purpose == NULL)
    {
      volext_string_purpose = "generic";
    }

  ext_info.purpose = DISK_PERMVOL_GENERIC_PURPOSE;

  if (strcasecmp (volext_string_purpose, "data") == 0)
    {
      ext_info.purpose = DISK_PERMVOL_DATA_PURPOSE;
    }
  else if (strcasecmp (volext_string_purpose, "index") == 0)
    {
      ext_info.purpose = DISK_PERMVOL_INDEX_PURPOSE;
    }
  else if (strcasecmp (volext_string_purpose, "temp") == 0)
    {
      ext_info.purpose = DISK_PERMVOL_TEMP_PURPOSE;
    }
  else if (strcasecmp (volext_string_purpose, "generic") == 0)
    {
      ext_info.purpose = DISK_PERMVOL_GENERIC_PURPOSE;
    }
  else
    {
      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
					     MSGCAT_UTIL_SET_ADDVOLDB,
					     ADDVOLDB_MSG_BAD_PURPOSE),
			     volext_string_purpose);

      goto error_exit;
    }

  sa_mode = utility_get_option_bool_value (arg_map, ADDVOL_SA_MODE_S);
  if (sa_mode && ext_info.max_writesize_in_sec > 0)
    {
      ext_info.max_writesize_in_sec = 0;
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_ADDVOLDB,
				       ADDVOLDB_INVALID_MAX_WRITESIZE_IN_SEC));
    }

  /* extra validation */
  if (check_database_name (database_name)
      || check_volume_name (ext_info.name))
    {
      goto error_exit;
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1,
	    "%s_%s.err", database_name, arg->command_name);
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
	  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
						 MSGCAT_UTIL_SET_ADDVOLDB,
						 ADDVOLDB_MSG_BAD_NPAGES),
				 ext_info.max_npages);
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
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_ADDVOLDB,
				   ADDVOLDB_MSG_USAGE),
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
util_get_class_oids_and_index_btid (dynamic_array * darray,
				    const char *index_name, BTID * index_btid)
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

      sm_downcase_name (table, name, SM_MAX_IDENTIFIER_LENGTH);
      cls_mop = locator_find_class (name);

      obj = (void *) &cls_sm;
      ws_find (cls_mop, obj);
      if (cls_sm == NULL || cls_sm->class_type != SM_CLASS_CT)
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
						 MSGCAT_UTIL_SET_CHECKDB,
						 CHECKDB_MSG_NO_SUCH_CLASS),
				 table);
	  continue;
	}

      cls_oid = ws_oid (cls_mop);
      if (cls_oid)
	{
	  oids[i] = *cls_oid;
	}
      else
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
						 MSGCAT_UTIL_SET_CHECKDB,
						 CHECKDB_MSG_NO_SUCH_CLASS),
				 table);
	  continue;
	}

      if (index_name != NULL)
	{
	  constraint = classobj_find_class_index (cls_sm, index_name);
	  if (constraint == NULL)
	    {
	      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
						     MSGCAT_UTIL_SET_CHECKDB,
						     CHECKDB_MSG_NO_SUCH_INDEX),
				     index_name, name);
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
 * util_get_table_list_from_file() -
 *   return: NO_ERROR/ER_GENERIC_ERROR
 */
static int
util_get_table_list_from_file (char *fname, dynamic_array * darray)
{
  int c, i, p;
  char name[SM_MAX_IDENTIFIER_LENGTH];
  FILE *fp = fopen (fname, "r");

  if (fp == NULL)
    {
      perror (fname);
      util_log_write_errid (MSGCAT_UTIL_GENERIC_FILEOPEN_ERROR, fname);
      return ER_GENERIC_ERROR;
    }

  i = p = 0;
  while (1)
    {
      c = fgetc (fp);
      if (c == ' ' || c == '\t' || c == ',' || c == '\n' || c == EOF)
	{
	  if (p != 0)
	    {
	      name[p] = '\0';
	      if (da_add (darray, name) != NO_ERROR)
		{
		  perror ("calloc");
		  fclose (fp);
		  util_log_write_errid (MSGCAT_UTIL_GENERIC_NO_MEM);
		  return ER_GENERIC_ERROR;
		}
	      i++;
	      p = 0;
	    }
	  if (c == EOF)
	    {
	      break;
	    }
	  continue;
	}
      name[p++] = c;
      if (p == SM_MAX_IDENTIFIER_LENGTH)
	{
	  /* too long table name */
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
	  fclose (fp);
	  return ER_GENERIC_ERROR;
	}
    }
  fclose (fp);

  return NO_ERROR;
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

  database_name = utility_get_option_string_value (arg_map,
						   OPTION_STRING_TABLE, 0);
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

  if (utility_get_option_bool_value
      (arg_map, CHECK_LC_ALLENTRIES_OF_ALLBTREES_S))
    {
      flag |= CHECKDB_LC_CHECK_ALLENTRIES_OF_ALLBTREES;
    }

  repair_plink = utility_get_option_bool_value (arg_map,
						CHECK_REPAIR_PREV_LINK_S);
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
  index_name =
    utility_get_option_string_value (arg_map, CHECK_INDEXNAME_S, 0);
  num_tables = utility_get_option_string_table_size (arg_map);
  num_tables -= 1;

  if (index_name != NULL && num_tables != 1)
    {
      PRINT_AND_LOG_ERR_MSG
	("Only one table is supported to check specific index.\n");
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
      char n[SM_MAX_IDENTIFIER_LENGTH];
      char *p;

      for (i = 0; i < num_tables; i++)
	{
	  p = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE,
					       i + 1);
	  if (p == NULL)
	    {
	      continue;
	    }
	  strncpy (n, p, SM_MAX_IDENTIFIER_LENGTH);
	  if (da_add (darray, n) != NO_ERROR)
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
  snprintf (er_msg_file, sizeof (er_msg_file) - 1,
	    "%s_%s.err", database_name, arg->command_name);
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
	  oids =
	    util_get_class_oids_and_index_btid (darray, index_name,
						&index_btid);
	  if (oids == NULL)
	    {
	      db_shutdown ();
	      goto error_exit;
	    }
	}

      if (db_set_isolation (TRAN_READ_COMMITTED) != NO_ERROR
	  || boot_check_db_consistency (flag, oids, num_tables,
					&index_btid) != NO_ERROR)
	{
	  const char *tmpname;

	  if ((tmpname = er_get_msglog_filename ()) == NULL)
	    {
	      tmpname = "/dev/null";
	    }
	  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
						 MSGCAT_UTIL_SET_CHECKDB,
						 CHECKDB_MSG_INCONSISTENT),
				 tmpname);

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
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_CHECKDB,
				   CHECKDB_MSG_USAGE), basename (arg->argv0));
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

#define VOL_PURPOSE_STRING(VOL_PURPOSE)		\
	    ((VOL_PURPOSE == DISK_PERMVOL_DATA_PURPOSE) ? "DATA"	\
	    : (VOL_PURPOSE == DISK_PERMVOL_INDEX_PURPOSE) ? "INDEX"	\
	    : (VOL_PURPOSE == DISK_PERMVOL_GENERIC_PURPOSE) ? "GENERIC"	\
	    : (VOL_PURPOSE == DISK_TEMPVOL_TEMP_PURPOSE) ? "TEMP TEMP" \
	    : "TEMP")


/*
 * spacedb() - spacedb main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
spacedb (UTIL_FUNCTION_ARG * arg)
{
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *database_name;
  const char *output_file = NULL;
  int i;
  const char *size_unit;
  DB_VOLPURPOSE vol_purpose;
  T_SPACEDB_SIZE_UNIT size_unit_type;
  char vol_label[PATH_MAX];

  UINT64 db_ntotal_pages, db_nfree_pages;
  UINT64 db_ndata_pages, db_nindex_pages, db_ntemp_pages;

  UINT64 db_summarize_ntotal_pages[SPACEDB_NUM_VOL_PURPOSE];
  UINT64 db_summarize_nfree_pages[SPACEDB_NUM_VOL_PURPOSE];
  UINT64 db_summarize_ndata_pages[SPACEDB_NUM_VOL_PURPOSE];
  UINT64 db_summarize_nindex_pages[SPACEDB_NUM_VOL_PURPOSE];
  UINT64 db_summarize_ntemp_pages[SPACEDB_NUM_VOL_PURPOSE];
  int db_summarize_nvols[SPACEDB_NUM_VOL_PURPOSE];

  bool summarize, purpose;
  FILE *outfp = NULL;
  int nvols;
  VOLID temp_volid;
  char num_total_str[64], num_free_str[64], num_used_str[64];
  char num_data_used_str[64];
  char num_index_used_str[64];
  char num_temp_used_str[64];
  char io_size_str[64], log_size_str[64];
  VOL_SPACE_INFO space_info;
  MSGCAT_SPACEDB_MSG title_format, output_format, size_title_format,
    underline;

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_space_usage;
    }

  database_name = utility_get_option_string_value (arg_map,
						   OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_space_usage;
    }

  output_file = utility_get_option_string_value (arg_map,
						 SPACE_OUTPUT_FILE_S, 0);
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
	  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
						 MSGCAT_UTIL_SET_SPACEDB,
						 SPACEDB_MSG_BAD_OUTPUT),
				 output_file);
	  goto error_exit;
	}
    }

  if (check_database_name (database_name))
    {
      goto error_exit;
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1,
	    "%s_%s.err", database_name, arg->command_name);
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
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }

  nvols = db_num_volumes ();

  db_ntotal_pages = db_nfree_pages = 0;
  db_ndata_pages = db_nindex_pages = db_ntemp_pages = 0;

  util_byte_to_size_string (io_size_str, 64, IO_PAGESIZE);
  util_byte_to_size_string (log_size_str, 64, LOG_PAGESIZE);

  if (summarize && purpose)
    {
      title_format = SPACEDB_OUTPUT_SUMMARIZED_PURPOSE_TITLE;
      size_title_format =
	(size_unit_type == SPACEDB_SIZE_UNIT_PAGE) ?
	SPACEDB_OUTPUT_SUMMARIZED_PURPOSE_TITLE_PAGE :
	SPACEDB_OUTPUT_SUMMARIZED_PURPOSE_TITLE_SIZE;
      output_format = SPACEDB_OUTPUT_SUMMARIZED_PURPOSE_FORMAT;
      underline = SPACEDB_OUTPUT_SUMMARIZED_PURPOSE_UNDERLINE;
    }
  else if (summarize && !purpose)
    {
      title_format = SPACEDB_OUTPUT_SUMMARIZED_TITLE;
      size_title_format =
	(size_unit_type == SPACEDB_SIZE_UNIT_PAGE) ?
	SPACEDB_OUTPUT_SUMMARIZED_TITLE_PAGE :
	SPACEDB_OUTPUT_SUMMARIZED_TITLE_SIZE;
      output_format = SPACEDB_OUTPUT_SUMMARIZED_FORMAT;
      underline = SPACEDB_OUTPUT_SUMMARIZED_UNDERLINE;
    }
  else if (!summarize && purpose)
    {
      title_format = SPACEDB_OUTPUT_PURPOSE_TITLE;
      size_title_format =
	(size_unit_type == SPACEDB_SIZE_UNIT_PAGE) ?
	SPACEDB_OUTPUT_PURPOSE_TITLE_PAGE : SPACEDB_OUTPUT_PURPOSE_TITLE_SIZE;
      output_format = SPACEDB_OUTPUT_PURPOSE_FORMAT;
      underline = SPACEDB_OUTPUT_PURPOSE_UNDERLINE;
    }
  else				/* !summarize && !purpose */
    {
      title_format = SPACEDB_OUTPUT_TITLE;
      size_title_format =
	(size_unit_type == SPACEDB_SIZE_UNIT_PAGE) ?
	SPACEDB_OUTPUT_TITLE_PAGE : SPACEDB_OUTPUT_TITLE_SIZE;
      output_format = SPACEDB_OUTPUT_FORMAT;
      underline = SPACEDB_OUTPUT_UNDERLINE;
    }

  if (summarize)
    {
      for (i = 0; i < SPACEDB_NUM_VOL_PURPOSE; i++)
	{
	  db_summarize_ntotal_pages[i] = 0;
	  db_summarize_nfree_pages[i] = 0;
	  db_summarize_nvols[i] = 0;

	  if (purpose)
	    {
	      db_summarize_ndata_pages[i] = 0;
	      db_summarize_nindex_pages[i] = 0;
	      db_summarize_ntemp_pages[i] = 0;
	    }
	}
    }

  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
				  MSGCAT_UTIL_SET_SPACEDB,
				  title_format),
	   database_name, io_size_str, log_size_str);

  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
				  MSGCAT_UTIL_SET_SPACEDB,
				  size_title_format));

  for (i = 0; i < nvols; i++)
    {
      if (disk_get_purpose_and_space_info (i, &vol_purpose,
					   &space_info) != NULL_VOLID)
	{
	  if (summarize)
	    {
	      if (vol_purpose < DISK_UNKNOWN_PURPOSE)
		{
		  db_summarize_ntotal_pages[vol_purpose]
		    += space_info.total_pages;
		  db_summarize_nfree_pages[vol_purpose]
		    += space_info.free_pages;
		  db_summarize_nvols[vol_purpose]++;

		  if (purpose)
		    {
		      db_summarize_ndata_pages[vol_purpose] +=
			space_info.used_data_npages;
		      db_summarize_nindex_pages[vol_purpose] +=
			space_info.used_index_npages;
		      db_summarize_ntemp_pages[vol_purpose] +=
			space_info.used_temp_npages;
		    }
		}
	    }
	  else
	    {
	      db_ntotal_pages += space_info.total_pages;
	      db_nfree_pages += space_info.free_pages;

	      if (db_vol_label (i, vol_label) == NULL)
		{
		  strcpy (vol_label, " ");
		}

	      spacedb_get_size_str (num_total_str,
				    (UINT64) space_info.total_pages,
				    size_unit_type);
	      spacedb_get_size_str (num_free_str,
				    (UINT64) space_info.free_pages,
				    size_unit_type);

	      if (purpose)
		{
		  db_ndata_pages += space_info.used_data_npages;
		  db_nindex_pages += space_info.used_index_npages;
		  db_ntemp_pages += space_info.used_temp_npages;

		  spacedb_get_size_str (num_data_used_str,
					(UINT64) space_info.used_data_npages,
					size_unit_type);
		  spacedb_get_size_str (num_index_used_str,
					(UINT64) space_info.used_index_npages,
					size_unit_type);
		  spacedb_get_size_str (num_temp_used_str,
					(UINT64) space_info.used_temp_npages,
					size_unit_type);

		  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
						  MSGCAT_UTIL_SET_SPACEDB,
						  output_format),
			   i, VOL_PURPOSE_STRING (vol_purpose),
			   num_total_str, num_free_str, num_data_used_str,
			   num_index_used_str, num_temp_used_str, vol_label);
		}
	      else
		{
		  fprintf (outfp,
			   msgcat_message (MSGCAT_CATALOG_UTILS,
					   MSGCAT_UTIL_SET_SPACEDB,
					   output_format),
			   i, VOL_PURPOSE_STRING (vol_purpose),
			   num_total_str, num_free_str, vol_label);
		}
	    }
	}
      else
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
	  db_shutdown ();
	  goto error_exit;
	}
    }

  if (nvols > 1 && !summarize)
    {
      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
				      MSGCAT_UTIL_SET_SPACEDB, underline));

      spacedb_get_size_str (num_total_str, db_ntotal_pages, size_unit_type);
      spacedb_get_size_str (num_free_str, db_nfree_pages, size_unit_type);

      if (purpose)
	{
	  spacedb_get_size_str (num_data_used_str, db_ndata_pages,
				size_unit_type);
	  spacedb_get_size_str (num_index_used_str, db_nindex_pages,
				size_unit_type);
	  spacedb_get_size_str (num_temp_used_str, db_ntemp_pages,
				size_unit_type);

	  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
					  MSGCAT_UTIL_SET_SPACEDB,
					  output_format), nvols, " ",
		   num_total_str, num_free_str, num_data_used_str,
		   num_index_used_str, num_temp_used_str, " ");
	}
      else
	{
	  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
					  MSGCAT_UTIL_SET_SPACEDB,
					  output_format), nvols, " ",
		   num_total_str, num_free_str, " ");
	}
    }

  /* Find info on temp volumes */
  nvols = boot_find_number_temp_volumes ();
  temp_volid = boot_find_last_temp ();

  if (!summarize)
    {
      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
				      MSGCAT_UTIL_SET_SPACEDB,
				      SPACEDB_OUTPUT_TITLE_TMP_VOL),
	       database_name, io_size_str);

      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
				      MSGCAT_UTIL_SET_SPACEDB,
				      size_title_format));
      db_ntotal_pages = db_nfree_pages = 0;
      db_ndata_pages = db_nindex_pages = db_ntemp_pages = 0;
    }

  for (i = 0; i < nvols; i++)
    {
      if (disk_get_purpose_and_space_info ((temp_volid + i), &vol_purpose,
					   &space_info) != NULL_VOLID)
	{
	  assert (space_info.used_data_npages == 0
		  && space_info.used_index_npages == 0);

	  if (summarize)
	    {
	      if (vol_purpose < DISK_UNKNOWN_PURPOSE)
		{
		  db_summarize_ntotal_pages[vol_purpose]
		    += space_info.total_pages;
		  db_summarize_nfree_pages[vol_purpose]
		    += space_info.free_pages;
		  db_summarize_nvols[vol_purpose]++;

		  if (purpose)
		    {
		      db_summarize_ntemp_pages[vol_purpose]
			+= space_info.used_temp_npages;
		    }
		}
	    }
	  else
	    {
	      db_ntotal_pages += space_info.total_pages;
	      db_nfree_pages += space_info.free_pages;

	      if (db_vol_label ((temp_volid + i), vol_label) == NULL)
		{
		  strcpy (vol_label, " ");
		}

	      spacedb_get_size_str (num_total_str,
				    (UINT64) space_info.total_pages,
				    size_unit_type);
	      spacedb_get_size_str (num_free_str,
				    (UINT64) space_info.free_pages,
				    size_unit_type);

	      if (purpose)
		{
		  db_ntemp_pages += space_info.used_temp_npages;

		  spacedb_get_size_str (num_data_used_str,
					(UINT64) 0, size_unit_type);
		  spacedb_get_size_str (num_index_used_str,
					(UINT64) 0, size_unit_type);
		  spacedb_get_size_str (num_temp_used_str,
					(UINT64) space_info.used_temp_npages,
					size_unit_type);

		  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
						  MSGCAT_UTIL_SET_SPACEDB,
						  output_format),
			   (temp_volid + i),
			   VOL_PURPOSE_STRING (DISK_TEMPVOL_TEMP_PURPOSE),
			   num_total_str, num_free_str, num_data_used_str,
			   num_index_used_str, num_temp_used_str, vol_label);
		}
	      else
		{
		  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
						  MSGCAT_UTIL_SET_SPACEDB,
						  output_format),
			   (temp_volid + i),
			   VOL_PURPOSE_STRING (DISK_TEMPVOL_TEMP_PURPOSE),
			   num_total_str, num_free_str, vol_label);
		}
	    }
	}
      else
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
	  db_shutdown ();
	  goto error_exit;
	}
    }

  if (!summarize)
    {
      if (nvols > 1)
	{
	  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
					  MSGCAT_UTIL_SET_SPACEDB,
					  underline));

	  spacedb_get_size_str (num_total_str, db_ntotal_pages,
				size_unit_type);
	  spacedb_get_size_str (num_free_str, db_nfree_pages, size_unit_type);

	  if (purpose)
	    {
	      spacedb_get_size_str (num_temp_used_str, db_ntemp_pages,
				    size_unit_type);

	      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
					      MSGCAT_UTIL_SET_SPACEDB,
					      output_format), nvols, " ",
		       num_total_str, num_free_str, num_data_used_str,
		       num_index_used_str, num_temp_used_str, " ");
	    }
	  else
	    {
	      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
					      MSGCAT_UTIL_SET_SPACEDB,
					      output_format), nvols, " ",
		       num_total_str, num_free_str, " ");
	    }
	}

      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
				      MSGCAT_UTIL_SET_SPACEDB,
				      SPACEDB_OUTPUT_TITLE_LOB),
	       boot_get_lob_path ());
    }
  else
    {
      int total_volume_count = 0;

      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
				      MSGCAT_UTIL_SET_SPACEDB, underline));

      for (i = 0; i < SPACEDB_NUM_VOL_PURPOSE; i++)
	{
	  spacedb_get_size_str (num_total_str, db_summarize_ntotal_pages[i],
				size_unit_type);

	  spacedb_get_size_str (num_used_str,
				db_summarize_ntotal_pages[i] -
				db_summarize_nfree_pages[i], size_unit_type);

	  spacedb_get_size_str (num_free_str, db_summarize_nfree_pages[i],
				size_unit_type);

	  db_ntotal_pages += db_summarize_ntotal_pages[i];
	  db_nfree_pages += db_summarize_nfree_pages[i];
	  total_volume_count += db_summarize_nvols[i];

	  if (purpose)
	    {
	      db_ndata_pages += db_summarize_ndata_pages[i];
	      db_nindex_pages += db_summarize_nindex_pages[i];
	      db_ntemp_pages += db_summarize_ntemp_pages[i];

	      spacedb_get_size_str (num_data_used_str,
				    db_summarize_ndata_pages[i],
				    size_unit_type);
	      spacedb_get_size_str (num_index_used_str,
				    db_summarize_nindex_pages[i],
				    size_unit_type);
	      spacedb_get_size_str (num_temp_used_str,
				    db_summarize_ntemp_pages[i],
				    size_unit_type);

	      fprintf (outfp,
		       msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_SPACEDB,
				       output_format),
		       VOL_PURPOSE_STRING (i), num_total_str, num_used_str,
		       num_free_str, num_data_used_str, num_index_used_str,
		       num_temp_used_str, db_summarize_nvols[i]);
	    }
	  else
	    {
	      fprintf (outfp,
		       msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_SPACEDB,
				       output_format),
		       VOL_PURPOSE_STRING (i), num_total_str, num_used_str,
		       num_free_str, db_summarize_nvols[i]);
	    }
	}

      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
				      MSGCAT_UTIL_SET_SPACEDB, underline));

      spacedb_get_size_str (num_total_str, db_ntotal_pages, size_unit_type);
      spacedb_get_size_str (num_used_str, db_ntotal_pages - db_nfree_pages,
			    size_unit_type);
      spacedb_get_size_str (num_free_str, db_nfree_pages, size_unit_type);

      if (purpose)
	{
	  spacedb_get_size_str (num_data_used_str,
				db_ndata_pages, size_unit_type);
	  spacedb_get_size_str (num_index_used_str,
				db_nindex_pages, size_unit_type);
	  spacedb_get_size_str (num_temp_used_str,
				db_ntemp_pages, size_unit_type);

	  fprintf (outfp,
		   msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_SPACEDB,
				   output_format),
		   "TOTAL", num_total_str, num_used_str,
		   num_free_str, num_data_used_str,
		   num_index_used_str, num_temp_used_str, total_volume_count);
	}
      else
	{
	  fprintf (outfp,
		   msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_SPACEDB,
				   output_format),
		   "TOTAL", num_total_str, num_used_str, num_free_str,
		   total_volume_count);
	}
    }

  db_shutdown ();
  if (outfp != stdout)
    {
      fclose (outfp);
    }

  return EXIT_SUCCESS;

print_space_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_SPACEDB,
				   SPACEDB_MSG_USAGE), basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
error_exit:
  if (outfp != stdout && outfp != NULL)
    {
      fclose (outfp);
    }
  return EXIT_FAILURE;
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
  const char *output_file = NULL;
  bool reload;
  int ret_code = EXIT_SUCCESS;

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_acl_usage;
    }

  reload = utility_get_option_bool_value (arg_map, ACLDB_RELOAD_S);

  database_name = utility_get_option_string_value (arg_map,
						   OPTION_STRING_TABLE, 0);
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
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_ACLDB, ACLDB_MSG_USAGE),
	   basename (arg->argv0));
error_exit:
  return EXIT_FAILURE;
#else /* CS_MODE */
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_ACLDB,
				   ACLDB_MSG_NOT_IN_STANDALONE),
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

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_lock_usage;
    }

  database_name = utility_get_option_string_value (arg_map,
						   OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_lock_usage;
    }

  output_file = utility_get_option_string_value (arg_map, LOCK_OUTPUT_FILE_S,
						 0);
  if (output_file == NULL)
    {
      outfp = stdout;
    }
  else
    {
      outfp = fopen (output_file, "w");
      if (outfp == NULL)
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
						 MSGCAT_UTIL_SET_LOCKDB,
						 LOCKDB_MSG_BAD_OUTPUT),
				 output_file);
	  goto error_exit;
	}
    }

  if (check_database_name (database_name))
    {
      goto error_exit;
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1,
	    "%s_%s.err", database_name, arg->command_name);
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

  lock_dump (outfp);
  db_shutdown ();

  if (outfp != stdout)
    {
      fclose (outfp);
    }

  return EXIT_SUCCESS;

print_lock_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_LOCKDB, LOCKDB_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

error_exit:
  if (outfp != stdout && outfp != NULL)
    {
      fclose (outfp);
    }
  return EXIT_FAILURE;
#else /* CS_MODE */
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_LOCKDB,
				   LOCKDB_MSG_NOT_IN_STANDALONE),
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

  valid = (tran != NULL && tran->tran_index != -1
	   && tran->tran_index != tm_Tran_index);

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
doesmatch_transaction (const ONE_TRAN_INFO * tran, int *tran_index_list,
		       int index_list_size,
		       const char *username, const char *hostname,
		       const char *progname, const char *sql_id)
{
  int i;

  if (isvalid_transaction (tran))
    {
      if ((username != NULL && strcmp (tran->login_name, username) == 0)
	  || (hostname != NULL && strcmp (tran->host_name, hostname) == 0)
	  || (progname != NULL && strcmp (tran->program_name, progname) == 0)
	  || (sql_id != NULL && tran->query_exec_info.sql_id != NULL
	      && strcmp (tran->query_exec_info.sql_id, sql_id) == 0))
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
dump_trantb (TRANS_INFO * info, TRANDUMP_LEVEL dump_level)
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
		  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
						   MSGCAT_UTIL_SET_TRANLIST,
						   header));
		  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
						   MSGCAT_UTIL_SET_TRANLIST,
						   underscore));
		}

	      num_valid++;
	      print_tran_entry (&info->tran[i], dump_level);
	    }
	}
    }

  if (num_valid > 0)
    {
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_TRANLIST, underscore));
    }
  else
    {
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_TRANLIST,
				       TRANLIST_MSG_NONE_TABLE_ENTRIES));
    }

  if (info != NULL
      && (dump_level == TRANDUMP_QUERY_INFO
	  || dump_level == TRANDUMP_FULL_INFO))
    {
      int j;

      fprintf (stdout, "\n");
      /* print query string info */
      for (i = 0; i < info->num_trans; i++)
	{
	  if (isvalid_transaction (&info->tran[i])
	      && !XASL_ID_IS_NULL (&info->tran[i].query_exec_info.xasl_id))
	    {
	      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
					       MSGCAT_UTIL_SET_TRANLIST,
					       TRANLIST_MSG_SQL_ID),
		       info->tran[i].query_exec_info.sql_id);
	      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
					       MSGCAT_UTIL_SET_TRANLIST,
					       TRANLIST_MSG_TRAN_INDEX),
		       info->tran[i].tran_index);

	      for (j = i + 1; j < info->num_trans; j++)
		{
		  if (isvalid_transaction (&info->tran[j])
		      && XASL_ID_EQ (&info->tran[i].query_exec_info.xasl_id,
				     &info->tran[j].query_exec_info.xasl_id))
		    {
		      /* same query */
		      fprintf (stdout, ", %d", info->tran[j].tran_index);
		      /* reset xasl to skip in next search */
		      XASL_ID_SET_NULL (&info->tran[j].query_exec_info.
					xasl_id);
		    }
		}
	      fprintf (stdout, "\n");

	      /* print query statement */
	      fprintf (stdout, "%s\n\n",
		       info->tran[i].query_exec_info.query_stmt);
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
kill_transactions (TRANS_INFO * info, int *tran_index_list, int list_size,
		   const char *username, const char *hostname,
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
      if (doesmatch_transaction (&info->tran[i], tran_index_list, list_size,
				 username, hostname, progname, sql_id))
	{
	  break;
	}
    }

  if (i >= info->num_trans)
    {
      /*
       * There is not matches
       */
      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
					     MSGCAT_UTIL_SET_KILLTRAN,
					     KILLTRAN_MSG_NO_MATCHES));
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
	  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
					   MSGCAT_UTIL_SET_KILLTRAN,
					   KILLTRAN_MSG_READY_TO_KILL));

	  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
					   MSGCAT_UTIL_SET_TRANLIST, header));
	  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
					   MSGCAT_UTIL_SET_TRANLIST,
					   underscore));

	  for (i = 0; i < info->num_trans; i++)
	    {
	      if (doesmatch_transaction (&info->tran[i], tran_index_list,
					 list_size, username, hostname,
					 progname, sql_id))
		{
		  print_tran_entry (&info->tran[i], dump_level);
		}
	    }
	  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
					   MSGCAT_UTIL_SET_TRANLIST,
					   underscore));

	  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
					   MSGCAT_UTIL_SET_KILLTRAN,
					   KILLTRAN_MSG_VERIFY));
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
	      if (doesmatch_transaction (&info->tran[i], tran_index_list,
					 list_size, username, hostname,
					 progname, sql_id))
		{
		  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
						   MSGCAT_UTIL_SET_KILLTRAN,
						   KILLTRAN_MSG_KILLING),
			   info->tran[i].tran_index);
		  if (thread_kill_tran_index (info->tran[i].tran_index,
					      info->tran[i].db_user,
					      info->tran[i].host_name,
					      info->tran[i].process_id) ==
		      NO_ERROR)
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
			  fprintf (stdout,
				   msgcat_message (MSGCAT_CATALOG_UTILS,
						   MSGCAT_UTIL_SET_KILLTRAN,
						   KILLTRAN_MSG_KILL_FAILED));
			  fprintf (stdout,
				   msgcat_message (MSGCAT_CATALOG_UTILS,
						   MSGCAT_UTIL_SET_TRANLIST,
						   header));
			  fprintf (stdout,
				   msgcat_message (MSGCAT_CATALOG_UTILS,
						   MSGCAT_UTIL_SET_TRANLIST,
						   underscore));
			}

		      print_tran_entry (&info->tran[i], dump_level);

		      if (er_errid () != NO_ERROR)
			{
			  PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
			}
		      else	/* probably it is the case of timeout */
			{
			  PRINT_AND_LOG_ERR_MSG (msgcat_message
						 (MSGCAT_CATALOG_UTILS,
						  MSGCAT_UTIL_SET_KILLTRAN,
						  KILLTRAN_MSG_KILL_TIMEOUT));
			}
		      nfailures++;
		    }
		}
	    }

	  if (nfailures > 0)
	    {
	      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
					       MSGCAT_UTIL_SET_TRANLIST,
					       underscore));
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
print_tran_entry (const ONE_TRAN_INFO * tran_info, TRANDUMP_LEVEL dump_level)
{
  char *buf = NULL;
  char query_buf[32];

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
	  strncpy (query_buf, tran_info->query_exec_info.query_stmt, 32);
	  query_buf[31] = '\0';
	}
    }

  if (dump_level == TRANDUMP_FULL_INFO)
    {
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_TRANLIST,
				       TRANLIST_MSG_FULL_INFO_ENTRY),
	       tran_info->tran_index,
	       tran_get_tranlist_state_name (tran_info->state),
	       tran_info->db_user, tran_info->host_name,
	       tran_info->process_id, tran_info->program_name,
	       tran_info->query_exec_info.query_time,
	       tran_info->query_exec_info.tran_time,
	       (buf == NULL ? "-1" : buf),
	       ((tran_info->query_exec_info.sql_id) ? tran_info->
		query_exec_info.sql_id : "*** empty ***"),
	       ((tran_info->query_exec_info.query_stmt) ? query_buf : " "));
    }
  else if (dump_level == TRANDUMP_QUERY_INFO)
    {
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_TRANLIST,
				       TRANLIST_MSG_QUERY_INFO_ENTRY),
	       tran_info->tran_index,
	       tran_get_tranlist_state_name (tran_info->state),
	       tran_info->process_id, tran_info->program_name,
	       tran_info->query_exec_info.query_time,
	       tran_info->query_exec_info.tran_time,
	       (buf == NULL ? "-1" : buf),
	       ((tran_info->query_exec_info.sql_id) ? tran_info->
		query_exec_info.sql_id : "*** empty ***"),
	       ((tran_info->query_exec_info.query_stmt) ? query_buf : " "));
    }
  else
    {
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_TRANLIST,
				       TRANLIST_MSG_SUMMARY_ENTRY),
	       tran_info->tran_index,
	       tran_get_tranlist_state_name (tran_info->state),
	       tran_info->db_user, tran_info->host_name,
	       tran_info->process_id, tran_info->program_name);
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
  const char *username;
  const char *password;
  char *passbuf = NULL;
  TRANS_INFO *info = NULL;
  int error;
  bool is_summary, include_query_info;
  TRANDUMP_LEVEL dump_level = TRANDUMP_FULL_INFO;

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_tranlist_usage;
    }

  database_name = utility_get_option_string_value (arg_map,
						   OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_tranlist_usage;
    }

  username = utility_get_option_string_value (arg_map, TRANLIST_USER_S, 0);
  password =
    utility_get_option_string_value (arg_map, TRANLIST_PASSWORD_S, 0);
  is_summary = utility_get_option_bool_value (arg_map, TRANLIST_SUMMARY_S);
  tranlist_Sort_column =
    utility_get_option_int_value (arg_map, TRANLIST_SORT_KEY_S);
  tranlist_Sort_desc =
    utility_get_option_bool_value (arg_map, TRANLIST_REVERSE_S);

  if (username == NULL)
    {
      /* default : DBA user */
      username = "DBA";
    }

  if (check_database_name (database_name) != NO_ERROR)
    {
      goto error_exit;
    }

  if (tranlist_Sort_column > 10 || tranlist_Sort_column < 0
      || (is_summary && tranlist_Sort_column > 5))
    {
      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
					     MSGCAT_UTIL_SET_TRANLIST,
					     TRANLIST_MSG_INVALID_SORT_KEY),
			     tranlist_Sort_column);
      goto error_exit;
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1,
	    "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  error =
    db_restart_ex (arg->command_name, database_name, username, password, NULL,
		   DB_CLIENT_TYPE_ADMIN_UTILITY);
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
	  snprintf (msg_buf, 64, msgcat_message (MSGCAT_CATALOG_UTILS,
						 MSGCAT_UTIL_SET_TRANLIST,
						 TRANLIST_MSG_USER_PASSWORD),
		    username);

	  passbuf = getpass (msg_buf);

	  if (passbuf[0] == '\0')
	    {
	      passbuf = (char *) NULL;
	    }
	  password = passbuf;

	  error =
	    db_restart_ex (arg->command_name, database_name, username,
			   password, NULL, DB_CLIENT_TYPE_ADMIN_UTILITY);
	}

      if (error != NO_ERROR)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
	  goto error_exit;
	}
    }

  if (!au_is_dba_group_member (Au_user))
    {
      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
					     MSGCAT_UTIL_SET_TRANLIST,
					     TRANLIST_MSG_NOT_DBA_USER),
			     username);
      db_shutdown ();
      goto error_exit;
    }

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
      qsort ((void *) info->tran, info->num_trans,
	     sizeof (ONE_TRAN_INFO), tranlist_cmp_f);
    }

  (void) dump_trantb (info, dump_level);

  if (info)
    {
      logtb_free_trans_info (info);
    }

  (void) db_shutdown ();
  return EXIT_SUCCESS;

print_tranlist_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_TRANLIST,
				   TRANLIST_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

error_exit:
  return EXIT_FAILURE;
#else /* CS_MODE */
  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
					 MSGCAT_UTIL_SET_TRANLIST,
					 TRANLIST_MSG_NOT_IN_STANDALONE),
			 basename (arg->argv0));
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

  database_name = utility_get_option_string_value (arg_map,
						   OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_killtran_usage;
    }

  kill_tran_index =
    utility_get_option_string_value (arg_map,
				     KILLTRAN_KILL_TRANSACTION_INDEX_S, 0);
  kill_user =
    utility_get_option_string_value (arg_map, KILLTRAN_KILL_USER_NAME_S, 0);
  kill_host =
    utility_get_option_string_value (arg_map, KILLTRAN_KILL_HOST_NAME_S, 0);
  kill_progname =
    utility_get_option_string_value (arg_map, KILLTRAN_KILL_PROGRAM_NAME_S,
				     0);
  kill_sql_id =
    utility_get_option_string_value (arg_map, KILLTRAN_KILL_SQL_ID_S, 0);

  force = utility_get_option_bool_value (arg_map, KILLTRAN_FORCE_S);
  dba_password = utility_get_option_string_value (arg_map,
						  KILLTRAN_DBA_PASSWORD_S, 0);
  dump_trantab_flag =
    utility_get_option_bool_value (arg_map, KILLTRAN_DISPLAY_INFORMATION_S);

  include_query_exec_info =
    utility_get_option_bool_value (arg_map, KILLTRAN_DISPLAY_QUERY_INFO_S);

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
      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
					     MSGCAT_UTIL_SET_KILLTRAN,
					     KILLTRAN_MSG_MANY_ARGS));
      goto error_exit;
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1,
	    "%s_%s.err", database_name, arg->command_name);
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
      if (error == ER_AU_INVALID_PASSWORD
	  && (dba_password == NULL || strlen (dba_password) == 0))
	{
	  /*
	   * prompt for a valid password and try again, need a reusable
	   * password prompter so we can use getpass() on platforms that
	   * support it.
	   */

	  /* get password interactively if interactive mode */
	  passbuf = getpass (msgcat_message (MSGCAT_CATALOG_UTILS,
					     MSGCAT_UTIL_SET_KILLTRAN,
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
      || ((kill_tran_index == NULL || strlen (kill_tran_index) == 0)
	  && (kill_user == NULL || strlen (kill_user) == 0)
	  && (kill_host == NULL || strlen (kill_host) == 0)
	  && (kill_progname == NULL || strlen (kill_progname) == 0)
	  && (kill_sql_id == NULL || strlen (kill_sql_id) == 0)))
    {
      TRANDUMP_LEVEL dump_level;

      dump_level =
	(include_query_exec_info) ? TRANDUMP_QUERY_INFO : TRANDUMP_SUMMARY;
      dump_trantb (info, dump_level);
    }
  else
    {
      if (kill_tran_index != NULL && strlen (kill_tran_index) > 0)
	{
	  int result;

	  ptr = kill_tran_index;

	  tmp = strchr (ptr, delimiter);
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
		  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
							 MSGCAT_UTIL_SET_KILLTRAN,
							 KILLTRAN_MSG_INVALID_TRANINDEX),
					 ptr);

		  if (info)
		    {
		      logtb_free_trans_info (info);
		    }
		  db_shutdown ();
		  goto error_exit;
		}

	      tran_index_list[list_size++] = value;
	      ptr = tmp + 1;
	      tmp = strchr (ptr, delimiter);
	    }

	  result = parse_int (&value, ptr, 10);
	  if (result != 0 || value <= 0)
	    {
	      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
						     MSGCAT_UTIL_SET_KILLTRAN,
						     KILLTRAN_MSG_INVALID_TRANINDEX),
				     ptr);

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
      if (kill_transactions (info, tran_index_list,
			     list_size, kill_user, kill_host,
			     kill_progname, kill_sql_id, !force) <= 0)
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
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_KILLTRAN,
				   KILLTRAN_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

error_exit:
  return EXIT_FAILURE;
#else /* CS_MODE */
  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
					 MSGCAT_UTIL_SET_KILLTRAN,
					 KILLTRAN_MSG_NOT_IN_STANDALONE),
			 basename (arg->argv0));
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

  database_name = utility_get_option_string_value (arg_map,
						   OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_plandump_usage;
    }

  drop_flag = utility_get_option_bool_value (arg_map, PLANDUMP_DROP_S);
  output_file = utility_get_option_string_value (arg_map,
						 PLANDUMP_OUTPUT_FILE_S, 0);

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
	  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
						 MSGCAT_UTIL_SET_PLANDUMP,
						 PLANDUMP_MSG_BAD_OUTPUT),
				 output_file);
	  goto error_exit;
	}
    }

  if (check_database_name (database_name))
    {
      goto error_exit;
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1,
	    "%s_%s.err", database_name, arg->command_name);
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
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_PLANDUMP,
				   PLANDUMP_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

error_exit:
  if (outfp != stdout && outfp != NULL)
    {
      fclose (outfp);
    }
  return EXIT_FAILURE;
#else /* CS_MODE */
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_PLANDUMP,
				   PLANDUMP_MSG_NOT_IN_STANDALONE),
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

  database_name = utility_get_option_string_value (arg_map,
						   OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_dumpparam_usage;
    }

  output_file = utility_get_option_string_value (arg_map,
						 PARAMDUMP_OUTPUT_FILE_S, 0);
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
	  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
						 MSGCAT_UTIL_SET_PARAMDUMP,
						 PARAMDUMP_MSG_BAD_OUTPUT),
				 output_file);
	  goto error_exit;
	}
    }

  if (check_database_name (database_name))
    {
      goto error_exit;
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1,
	    "%s_%s.err", database_name, arg->command_name);
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
      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
				      MSGCAT_UTIL_SET_PARAMDUMP,
				      PARAMDUMP_MSG_CLIENT_PARAMETER));
      sysprm_dump_parameters (outfp);
      fprintf (outfp, "\n");
    }
  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
				  MSGCAT_UTIL_SET_PARAMDUMP,
				  PARAMDUMP_MSG_SERVER_PARAMETER),
	   database_name);
  sysprm_dump_server_parameters (outfp);
  db_shutdown ();
#else /* CS_MODE */
  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
				  MSGCAT_UTIL_SET_PARAMDUMP,
				  PARAMDUMP_MSG_STANDALONE_PARAMETER));
  if (sysprm_load_and_init (database_name, NULL) == NO_ERROR)
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
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_PARAMDUMP,
				   PARAMDUMP_MSG_USAGE),
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

  database_name = utility_get_option_string_value (arg_map,
						   OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_statdump_usage;
    }

  output_file = utility_get_option_string_value (arg_map,
						 STATDUMP_OUTPUT_FILE_S, 0);
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
	  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
						 MSGCAT_UTIL_SET_STATDUMP,
						 STATDUMP_MSG_BAD_OUTPUT),
				 output_file);
	  goto error_exit;
	}
    }

  if (check_database_name (database_name))
    {
      goto error_exit;
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1,
	    "%s_%s.err", database_name, arg->command_name);
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

  histo_start (true);

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
      histo_print_global_stats (outfp, cumulative, substr);
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
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_STATDUMP,
				   STATDUMP_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

error_exit:
  if (outfp != stdout && outfp != NULL)
    {
      fclose (outfp);
    }
  return EXIT_FAILURE;
#else /* CS_MODE */
  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
					 MSGCAT_UTIL_SET_STATDUMP,
					 STATDUMP_MSG_NOT_IN_STANDALONE),
			 basename (arg->argv0));

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
 */
int
changemode (UTIL_FUNCTION_ARG * arg)
{
#if defined (WINDOWS)
  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
					 MSGCAT_UTIL_SET_CHANGEMODE,
					 CHANGEMODE_MSG_HA_NOT_SUPPORT),
			 basename (arg->argv0));

  return EXIT_FAILURE;
#else /* WINDOWS */
#if defined (CS_MODE)
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *database_name;
  char *mode_name;
  int mode = -1, error;
  bool force;
  int timeout;

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_changemode_usage;
    }

  database_name = utility_get_option_string_value (arg_map,
						   OPTION_STRING_TABLE, 0);
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
      if (changemode_keyword (&mode, &mode_name) != NO_ERROR)
	{
	  if (sscanf (mode_name, "%d", &mode) != 1)
	    {
	      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
						     MSGCAT_UTIL_SET_CHANGEMODE,
						     CHANGEMODE_MSG_BAD_MODE),
				     mode_name);
	      goto error_exit;
	    }
	}
      if (!(mode == HA_SERVER_STATE_ACTIVE
	    || mode == HA_SERVER_STATE_STANDBY
	    || mode == HA_SERVER_STATE_MAINTENANCE))
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
						 MSGCAT_UTIL_SET_CHANGEMODE,
						 CHANGEMODE_MSG_BAD_MODE),
				 mode_name);
	  goto error_exit;
	}
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1,
	    "%s_%s.err", database_name, arg->command_name);
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

  if (prm_get_integer_value (PRM_ID_HA_MODE) == HA_MODE_OFF)
    {
      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
					     MSGCAT_UTIL_SET_CHANGEMODE,
					     CHANGEMODE_MSG_NOT_HA_MODE));
      goto error_exit;
    }

  if (mode_name == NULL)
    {
      /* display the value of current mode */
      mode = boot_change_ha_mode (HA_SERVER_MODE_NA, false, timeout);
    }
  else
    {
      /* change server's HA mode */
      mode = boot_change_ha_mode (mode, force, timeout);
    }
  if (mode != HA_SERVER_MODE_NA)
    {
      mode_name = NULL;
      if (changemode_keyword (&mode, &mode_name) != NO_ERROR)
	{
	  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
					   MSGCAT_UTIL_SET_CHANGEMODE,
					   CHANGEMODE_MSG_BAD_MODE),
		   (mode_name ? mode_name : "unknown"));
	}
      else
	{
	  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
					   MSGCAT_UTIL_SET_CHANGEMODE,
					   CHANGEMODE_MSG_SERVER_MODE),
		   database_name, mode_name);
	}
    }
  else
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }

  (void) db_shutdown ();
  return EXIT_SUCCESS;

print_changemode_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_CHANGEMODE,
				   CHANGEMODE_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

error_exit:
  return EXIT_FAILURE;
#else /* CS_MODE */
  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
					 MSGCAT_UTIL_SET_CHANGEMODE,
					 CHANGEMODE_MSG_NOT_IN_STANDALONE),
			 basename (arg->argv0));
  return EXIT_FAILURE;
#endif /* !CS_MODE */
#endif /* !WINDOWS */
}

int
prefetchlogdb (UTIL_FUNCTION_ARG * arg)
{
#if defined (WINDOWS)
  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
					 MSGCAT_UTIL_SET_PREFETCHLOGDB,
					 PREFETCHLOGDB_MSG_HA_NOT_SUPPORT),
			 basename (arg->argv0));
  return EXIT_FAILURE;
#else /* WINDOWS */
#if defined (CS_MODE)
  int error = NO_ERROR;
  int retried = 0, sleep_nsecs = 1;
  bool need_er_reinit = false;
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *database_name = NULL;
  const char *log_path = NULL;
  char *log_path_base = NULL;
  char log_path_buf[PATH_MAX];
  char *binary_name = NULL;
  char executable_path[PATH_MAX];

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_prefetchlog_usage;
    }

  database_name = utility_get_option_string_value (arg_map,
						   OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_prefetchlog_usage;
    }

  log_path =
    utility_get_option_string_value (arg_map, PREFETCH_LOG_PATH_S, 0);
  if (log_path == NULL)
    {
      goto print_prefetchlog_usage;
    }
  else
    {
      log_path = realpath (log_path, log_path_buf);
    }

  if (check_database_name (database_name))
    {
      goto error_exit;
    }

#if defined(NDEBUG)
  util_redirect_stdout_to_null ();
#endif

  /* error message log file */
  log_path_base = strdup (log_path);
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s_%s.err",
	    database_name, arg->command_name, basename (log_path_base));
  free (log_path_base);
  er_init (er_msg_file, ER_NEVER_EXIT);

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_LOG_PREFETCHER);
  if (db_login ("DBA", NULL) != NO_ERROR)
    {
      fprintf (stderr, "%s\n", db_error_string (3));
      goto error_exit;
    }

  /* save executable path */
  binary_name = basename (arg->argv0);
  envvar_bindir_file (executable_path, PATH_MAX, binary_name);

  hb_set_exec_path (executable_path);
  hb_set_argv (arg->argv);

  /* initialize system parameters */
  if (sysprm_load_and_init (database_name, NULL) != NO_ERROR)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_SERVICE_PROPERTY_FAIL);
      error = ER_FAILED;
      goto error_exit;
    }

  if (prm_get_integer_value (PRM_ID_HA_MODE) != HA_MODE_OFF)
    {
      /* initialize heartbeat */
      error =
	hb_process_init (database_name, log_path, HB_PTYPE_PREFETCHLOGDB);
      if (error != NO_ERROR)
	{
	  er_log_debug (ARG_FILE_LINE,
			"Cannot connect to cub_master for heartbeat. \n");
	  if (er_errid () != NO_ERROR)
	    {
	      util_log_write_errstr ("%s\n", db_error_string (3));
	    }
	  return EXIT_FAILURE;
	}
    }

retry:
  error = db_restart (arg->command_name, TRUE, database_name);
  if (error != NO_ERROR)
    {
      fprintf (stderr, "%s\n", db_error_string (3));
      goto error_exit;
    }

  if (need_er_reinit)
    {
      er_init (er_msg_file, ER_NEVER_EXIT);
      need_er_reinit = false;
    }

  db_set_lock_timeout (5);

  /* initialize system parameters */
  if (sysprm_load_and_init (database_name, NULL) != NO_ERROR)
    {
      db_shutdown ();
      error = ER_FAILED;
      goto error_exit;
    }

  if (prm_get_integer_value (PRM_ID_HA_MODE) == HA_MODE_OFF)
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_PREFETCHLOGDB,
				       PREFETCHLOGDB_MSG_NOT_HA_MODE));
      db_shutdown ();
      goto error_exit;
    }

  if (prm_get_bool_value (PRM_ID_HA_PREFETCHLOGDB_ENABLE) == false)
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_PREFETCHLOGDB,
				       PREFETCHLOGDB_MSG_FEATURE_DISABLE));
      db_shutdown ();
      goto error_exit;
    }

  error = lp_prefetch_log_file (database_name, log_path);
  if (error != NO_ERROR)
    {
      fprintf (stderr, "%s\n", db_error_string (3));
      (void) db_shutdown ();
      goto error_exit;
    }

  db_shutdown ();
  return EXIT_SUCCESS;

print_prefetchlog_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_PREFETCHLOGDB,
				   PREFETCHLOGDB_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

  return EXIT_FAILURE;

error_exit:
  if (hb_Proc_shutdown)
    {
      return EXIT_SUCCESS;
    }

  if (la_force_shutdown () == false
      && (error == ER_NET_SERVER_CRASHED
	  || error == ER_NET_CANT_CONNECT_SERVER
	  || error == ERR_CSS_TCP_CANNOT_CONNECT_TO_MASTER
	  || error == ER_BO_CONNECT_FAILED
	  || error == ER_NET_SERVER_COMM_ERROR
	  || error == ER_LC_PARTIALLY_FAILED_TO_FLUSH))
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
  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
					 MSGCAT_UTIL_SET_PREFETCHLOGDB,
					 PREFETCHLOGDB_MSG_NOT_IN_STANDALONE),
			 basename (arg->argv0));
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
  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
					 MSGCAT_UTIL_SET_COPYLOGDB,
					 COPYLOGDB_MSG_HA_NOT_SUPPORT),
			 basename (arg->argv0));
  return EXIT_FAILURE;
#else /* WINDOWS */
#if defined (CS_MODE)
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *database_name;
  const char *log_path;
  char log_path_buf[PATH_MAX];
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
  INT64 start_pageid;

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_copylog_usage;
    }

  database_name = utility_get_option_string_value (arg_map,
						   OPTION_STRING_TABLE, 0);
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
  if (log_path != NULL)
    {
      log_path = realpath (log_path, log_path_buf);
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
	      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
						     MSGCAT_UTIL_SET_COPYLOGDB,
						     COPYLOGDB_MSG_BAD_MODE),
				     mode_name);
	      goto error_exit;
	    }
	}
      if (!(mode >= LOGWR_MODE_ASYNC && mode <= LOGWR_MODE_SYNC))
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
						 MSGCAT_UTIL_SET_COPYLOGDB,
						 COPYLOGDB_MSG_BAD_MODE),
				 mode_name);
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

  start_pageid =
    utility_get_option_bigint_value (arg_map, COPYLOG_START_PAGEID_S);

#if defined(NDEBUG)
  util_redirect_stdout_to_null ();
#endif

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1,
	    "%s_%s.err", database_name, arg->command_name);
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
  if (sysprm_load_and_init (database_name, NULL) != NO_ERROR)
    {
      error = ER_FAILED;
      goto error_exit;
    }

  if (start_pageid < NULL_PAGEID
      && prm_get_integer_value (PRM_ID_HA_MODE) != HA_MODE_OFF)
    {
      error = hb_process_init (database_name, log_path, HB_PTYPE_COPYLOGDB);
      if (error != NO_ERROR)
	{
	  er_log_debug (ARG_FILE_LINE,
			"cannot connect to cub_master for heartbeat. \n");
	  if (er_errid () != NO_ERROR)
	    {
	      util_log_write_errstr ("%s\n", db_error_string (3));
	    }

	  return EXIT_FAILURE;
	}
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
  if (sysprm_load_and_init (database_name, NULL) != NO_ERROR)
    {
      (void) db_shutdown ();

      error = ER_FAILED;
      goto error_exit;
    }

  /* PRM_LOG_BACKGROUND_ARCHIVING is always true in CUBRID HA */
  sysprm_set_to_default (prm_get_name (PRM_ID_LOG_BACKGROUND_ARCHIVING),
			 true);

  if (prm_get_integer_value (PRM_ID_HA_MODE) == HA_MODE_OFF)
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_COPYLOGDB,
				       COPYLOGDB_MSG_NOT_HA_MODE));
      (void) db_shutdown ();
      goto error_exit;
    }

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
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_COPYLOGDB,
				   COPYLOGDB_MSG_USAGE),
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
      && (error == ER_NET_SERVER_CRASHED
	  || error == ER_NET_CANT_CONNECT_SERVER
	  || error == ER_BO_CONNECT_FAILED
	  || error == ERR_CSS_TCP_CANNOT_CONNECT_TO_MASTER))
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
  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
					 MSGCAT_UTIL_SET_COPYLOGDB,
					 COPYLOGDB_MSG_NOT_IN_STANDALONE),
			 basename (arg->argv0));
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
  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
					 MSGCAT_UTIL_SET_APPLYLOGDB,
					 APPLYLOGDB_MSG_HA_NOT_SUPPORT),
			 basename (arg->argv0));

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

  database_name = utility_get_option_string_value (arg_map,
						   OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_applylog_usage;
    }

  log_path = utility_get_option_string_value (arg_map, APPLYLOG_LOG_PATH_S,
					      0);
  max_mem_size = utility_get_option_int_value (arg_map,
					       APPLYLOG_MAX_MEM_SIZE_S);

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
  if (max_mem_size > 500)
    {
      goto print_applylog_usage;
    }

#if defined(NDEBUG)
  util_redirect_stdout_to_null ();
#endif

  /* error message log file */
  log_path_base = strdup (log_path);
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s_%s.err",
	    database_name, arg->command_name, basename (log_path_base));
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
  if (sysprm_load_and_init (database_name, NULL) != NO_ERROR)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_SERVICE_PROPERTY_FAIL);
      error = ER_FAILED;
      goto error_exit;
    }

  if (prm_get_integer_value (PRM_ID_HA_MODE) != HA_MODE_OFF)
    {
      /* initialize heartbeat */
      error = hb_process_init (database_name, log_path, HB_PTYPE_APPLYLOGDB);
      if (error != NO_ERROR)
	{
	  er_log_debug (ARG_FILE_LINE,
			"Cannot connect to cub_master for heartbeat. \n");
	  if (er_errid () != NO_ERROR)
	    {
	      util_log_write_errstr ("%s\n", db_error_string (3));
	    }
	  return EXIT_FAILURE;
	}
    }

  if (prm_get_integer_value (PRM_ID_HA_MODE) == HA_MODE_REPLICA)
    {
      replica_time_bound_str =
	prm_get_string_value (PRM_ID_HA_REPLICA_TIME_BOUND);
      if (replica_time_bound_str != NULL)
	{
	  if (util_str_to_time_since_epoch (replica_time_bound_str) == 0)
	    {
	      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
						     MSGCAT_UTIL_SET_GENERIC,
						     MSGCAT_UTIL_GENERIC_INVALID_PARAMETER),
				     prm_get_name
				     (PRM_ID_HA_REPLICA_TIME_BOUND),
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
  if (sysprm_load_and_init (database_name, NULL) != NO_ERROR)
    {
      (void) db_shutdown ();

      error = ER_FAILED;
      goto error_exit;
    }

  if (prm_get_integer_value (PRM_ID_HA_MODE) == HA_MODE_OFF)
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_APPLYLOGDB,
				       APPLYLOGDB_MSG_NOT_HA_MODE));
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
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_APPLYLOGDB,
				   APPLYLOGDB_MSG_USAGE),
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
      && (error == ER_NET_SERVER_CRASHED
	  || error == ER_NET_CANT_CONNECT_SERVER
	  || error == ERR_CSS_TCP_CANNOT_CONNECT_TO_MASTER
	  || error == ER_BO_CONNECT_FAILED
	  || error == ER_NET_SERVER_COMM_ERROR
	  || error == ER_LC_PARTIALLY_FAILED_TO_FLUSH))
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
  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
					 MSGCAT_UTIL_SET_APPLYLOGDB,
					 APPLYLOGDB_MSG_NOT_IN_STANDALONE),
			 basename (arg->argv0));

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

static int
spacedb_get_size_str (char *buf, UINT64 num_pages,
		      T_SPACEDB_SIZE_UNIT size_unit)
{
  int pgsize, i;
  double size;

  assert (buf);

  if (size_unit == SPACEDB_SIZE_UNIT_PAGE)
    {
      sprintf (buf, "%11llu", (long long unsigned int) num_pages);
    }
  else
    {
      pgsize = IO_PAGESIZE / 1024;
      size = pgsize * ((double) num_pages);

      if (size_unit == SPACEDB_SIZE_UNIT_HUMAN_READABLE)
	{
	  for (i = SPACEDB_SIZE_UNIT_MBYTES;
	       i <= SPACEDB_SIZE_UNIT_TBYTES; i++)
	    {
	      size /= 1024;

	      if (size < 1024)
		{
		  break;
		}
	    }
	}
      else
	{
	  i = size_unit;
	  for (; size_unit > SPACEDB_SIZE_UNIT_PAGE; size_unit--)
	    {
	      size /= 1024;
	    }
	}

      sprintf (buf, "%9.1f %c", size,
	       (i == SPACEDB_SIZE_UNIT_MBYTES) ? 'M' :
	       (i == SPACEDB_SIZE_UNIT_GBYTES) ? 'G' : 'T');
    }

  return NO_ERROR;
}

/*
 * applyinfo() - ApplyInfo main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
applyinfo (UTIL_FUNCTION_ARG * arg)
{
#if defined (WINDOWS)
  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
					 MSGCAT_UTIL_SET_APPLYINFO,
					 APPLYINFO_MSG_HA_NOT_SUPPORT),
			 basename (arg->argv0));

  return EXIT_FAILURE;
#else /* WINDOWS */
#if defined (CS_MODE)
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *database_name;
  const char *master_node_name;
  char local_database_name[MAXHOSTNAMELEN];
  char master_database_name[MAXHOSTNAMELEN];
  bool check_applied_info, check_copied_info;
  bool check_master_info, check_replica_info;
  bool verbose;
  const char *log_path;
  char log_path_buf[PATH_MAX];
  char *log_path_base;
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

  check_applied_info = check_copied_info = false;
  check_replica_info = check_master_info = false;

  database_name = utility_get_option_string_value (arg_map,
						   OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_applyinfo_usage;
    }

  /* initialize system parameters */
  if (sysprm_load_and_init (database_name, NULL) != NO_ERROR)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_SERVICE_PROPERTY_FAIL);
      return EXIT_FAILURE;
    }

  master_node_name = utility_get_option_string_value (arg_map,
						      APPLYINFO_REMOTE_NAME_S,
						      0);
  if (master_node_name != NULL)
    {
      check_master_info = true;
    }

  check_applied_info = utility_get_option_bool_value (arg_map,
						      APPLYINFO_APPLIED_INFO_S);
  log_path = utility_get_option_string_value (arg_map,
					      APPLYINFO_COPIED_LOG_PATH_S, 0);
  if (log_path != NULL)
    {
      log_path = realpath (log_path, log_path_buf);
    }
  if (log_path != NULL)
    {
      check_copied_info = true;
    }

  if (check_applied_info && (log_path == NULL))
    {
      goto print_applyinfo_usage;
    }

  check_replica_info =
    (prm_get_integer_value (PRM_ID_HA_MODE) == HA_MODE_REPLICA);
  pageid = utility_get_option_bigint_value (arg_map, APPLYINFO_PAGE_S);
  verbose = utility_get_option_bool_value (arg_map, APPLYINFO_VERBOSE_S);

  interval = utility_get_option_int_value (arg_map, APPLYINFO_INTERVAL_S);
  if (interval < 0)
    {
      goto print_applyinfo_usage;
    }

  if (check_replica_info)
    {
      replica_time_bound_str
	= prm_get_string_value (PRM_ID_HA_REPLICA_TIME_BOUND);
      if (replica_time_bound_str != NULL)
	{
	  if (util_str_to_time_since_epoch (replica_time_bound_str) == 0)
	    {
	      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
						     MSGCAT_UTIL_SET_GENERIC,
						     MSGCAT_UTIL_GENERIC_INVALID_PARAMETER),
				     prm_get_name
				     (PRM_ID_HA_REPLICA_TIME_BOUND),
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

      if (check_applied_info)
	{
	  memset (local_database_name, 0x00, MAXHOSTNAMELEN);
	  strcpy (local_database_name, database_name);
	  strcat (local_database_name, "@localhost");

	  db_clear_host_connected ();

	  if (check_database_name (local_database_name))
	    {
	      goto check_applied_info_end;
	    }
	  if (db_login ("DBA", NULL) != NO_ERROR)
	    {
	      goto check_applied_info_end;
	    }
	  error = db_restart (arg->command_name, TRUE, local_database_name);
	  if (error != NO_ERROR)
	    {
	      goto check_applied_info_end;
	    }

	  if (prm_get_integer_value (PRM_ID_HA_MODE) == HA_MODE_OFF)
	    {
	      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS,
						     MSGCAT_UTIL_SET_APPLYINFO,
						     APPLYINFO_MSG_NOT_HA_MODE));
	      goto check_applied_info_end;
	    }

	  error =
	    la_log_page_check (local_database_name, log_path, pageid,
			       check_applied_info, check_copied_info,
			       check_replica_info, verbose, &copied_eof_lsa,
			       &copied_append_lsa, &applied_final_lsa);
	  (void) db_shutdown ();
	}
      else if (check_copied_info)
	{
	  memset (local_database_name, 0x00, MAXHOSTNAMELEN);
	  strcpy (local_database_name, database_name);
	  strcat (local_database_name, "@localhost");

	  error =
	    la_log_page_check (local_database_name, log_path, pageid,
			       check_applied_info, check_copied_info,
			       check_replica_info, verbose, &copied_eof_lsa,
			       &copied_append_lsa, &applied_final_lsa);
	}

    check_applied_info_end:
      if (error != NO_ERROR)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
	}
      error = NO_ERROR;

      if (check_master_info)
	{
	  memset (master_database_name, 0x00, MAXHOSTNAMELEN);
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

	  error =
	    logwr_copy_log_header_check (master_database_name, verbose,
					 &master_eof_lsa);
	  if (error != NO_ERROR)
	    {
	      goto check_master_info_end;
	    }

	  (void) db_shutdown ();
	}

    check_master_info_end:
      if (error != NO_ERROR)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
	}
      error = NO_ERROR;

      /* print delay info */
      cur_time = time (NULL);
      if (check_copied_info && check_master_info)
	{
	  if (!LSA_ISNULL (&initial_copied_append_lsa))
	    {
	      process_rate =
		(float) (copied_append_lsa.pageid -
			 initial_copied_append_lsa.pageid)
		/ (cur_time - start_time);
	    }
	  else
	    {
	      initial_copied_append_lsa = copied_append_lsa;
	      process_rate = 0.0f;
	    }

	  printf ("\n *** Delay in Copying Active Log *** \n");
	  la_print_delay_info (copied_append_lsa, master_eof_lsa,
			       process_rate);
	}

      if (check_applied_info)
	{
	  if (!LSA_ISNULL (&initial_applied_final_lsa))
	    {
	      process_rate =
		(float) (applied_final_lsa.pageid -
			 initial_applied_final_lsa.pageid)
		/ (cur_time - start_time);
	    }
	  else
	    {
	      initial_applied_final_lsa = applied_final_lsa;
	      process_rate = 0.0f;
	    }

	  printf ("\n *** Delay in Applying Copied Log *** \n");
	  la_print_delay_info (applied_final_lsa, copied_eof_lsa,
			       process_rate);
	}

      sleep (interval);
    }
  while (interval > 0 && !is_Sigint_caught);

  return EXIT_SUCCESS;

print_applyinfo_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_APPLYINFO,
				   APPLYINFO_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

  return EXIT_FAILURE;
#else /* CS_MODE */
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_APPLYINFO,
				   APPLYINFO_MSG_NOT_IN_STANDALONE),
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
