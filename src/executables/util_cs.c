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
#if !defined(WINDOWS)
#include "heartbeat.h"
#endif

#define PASSBUF_SIZE 12

typedef enum
{
  SPACEDB_SIZE_UNIT_PAGE = 0,
  SPACEDB_SIZE_UNIT_MBYTES,
  SPACEDB_SIZE_UNIT_GBYTES,
  SPACEDB_SIZE_UNIT_TBYTES,
  SPACEDB_SIZE_UNIT_HUMAN_READABLE
} T_SPACEDB_SIZE_UNIT;

static int changemode_keyword (int *keyval_p, char **keystr_p);
static int copylogdb_keyword (int *keyval_p, char **keystr_p);
static void backupdb_sig_interrupt_handler (int sig_no);
static int spacedb_get_size_str (char *buf, int num_pages,
				 T_SPACEDB_SIZE_UNIT size_unit);
static void print_timestamp (FILE * outfp);

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
  int safe_pageid = -1;
#if defined(SOLARIS) || defined(LINUX)
  char real_pathbuf[PATH_MAX];
#endif
  char verbose_file_realpath[PATH_MAX];

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_backup_usage;
    }

  database_name =
    utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_backup_usage;
    }

  backup_path =
    utility_get_option_string_value (arg_map, BACKUP_DESTINATION_PATH_S, 0);
  remove_log_archives =
    utility_get_option_bool_value (arg_map, BACKUP_REMOVE_ARCHIVE_S);
  backup_level = utility_get_option_int_value (arg_map, BACKUP_LEVEL_S);
  backup_verbose_file =
    utility_get_option_string_value (arg_map, BACKUP_OUTPUT_FILE_S, 0);
  no_check = utility_get_option_bool_value (arg_map, BACKUP_NO_CHECK_S);
  check = !no_check;
  backup_num_threads =
    utility_get_option_int_value (arg_map, BACKUP_THREAD_COUNT_S);
  compress_flag = utility_get_option_bool_value (arg_map, BACKUP_COMPRESS_S);
  skip_activelog =
    utility_get_option_bool_value (arg_map, BACKUP_EXCEPT_ACTIVE_LOG_S);
  sa_mode = utility_get_option_bool_value (arg_map, BACKUP_SA_MODE_S);
  safe_pageid = utility_get_option_int_value (arg_map, BACKUP_SAFE_PAGE_ID_S);

  /* Range checking of input */
  if (backup_level < 0 || backup_level >= FILEIO_BACKUP_UNDEFINED_LEVEL)
    {
      goto print_backup_usage;
    }

  if (sa_mode && backup_num_threads > 1)
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_BACKUPDB,
				       BACKUPDB_INVALID_THREAD_NUM_OPT));
    }

  if (backup_num_threads < FILEIO_NUM_THREADS_AUTO)
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

  /* error message log file */
  sprintf (er_msg_file, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  sysprm_set_force (PRM_NAME_JAVA_STORED_PROCEDURE, "no");

  AU_DISABLE_PASSWORDS ();	/* disable authorization for this operation */
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("dba", NULL);

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

	  if (db_set_isolation (TRAN_READ_COMMITTED) != NO_ERROR ||
	      boot_check_db_consistency (check_flag) != NO_ERROR)
	    {
	      const char *tmpname;
	      if ((tmpname = er_msglog_filename ()) == NULL)
		{
		  tmpname = "/dev/null";
		}
	      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
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
	  fprintf (stderr, "%s\n", db_error_string (3));
	  db_shutdown ();
	  goto error_exit;
	}

#if defined(SOLARIS) || defined(LINUX)
      if (backup_path != NULL)
	{
	  memset (real_pathbuf, 0, sizeof (real_pathbuf));
	  if (realpath (backup_path, real_pathbuf) != NULL)
	    {
	      backup_path = real_pathbuf;
	    }
	}
#endif

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
		       skip_activelog, safe_pageid) == NO_ERROR)
	{
	  if (db_commit_transaction () != NO_ERROR)
	    {
	      fprintf (stderr, "%s\n", db_error_string (3));
	    }
	}
      else
	{
	  fprintf (stderr, "%s\n", db_error_string (3));
	  db_shutdown ();
	  goto error_exit;
	}

      db_shutdown ();
    }
  else
    {
      fprintf (stderr, "%s\n", db_error_string (3));
      goto error_exit;
    }
  return EXIT_SUCCESS;

print_backup_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_BACKUPDB,
				   BACKUPDB_MSG_USAGE),
	   basename (arg->argv0));
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
  int volext_npages = 0;
  const char *volext_name = NULL;
  const char *volext_pathname = NULL;
  const char *volext_comments = NULL;
  const char *volext_string_purpose = NULL;
  const char *volext_npages_string = NULL;
  DISK_VOLPURPOSE Volext_purpose;

  if (utility_get_option_string_table_size (arg_map) != 2)
    {
      goto print_addvol_usage;
    }

  database_name =
    utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  volext_npages_string =
    utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 1);
  if (database_name == NULL || volext_npages_string == NULL)
    {
      goto print_addvol_usage;
    }
  volext_npages = atoi (volext_npages_string);

  volext_name =
    utility_get_option_string_value (arg_map, ADDVOL_VOLUME_NAME_S, 0);
  volext_pathname =
    utility_get_option_string_value (arg_map, ADDVOL_FILE_PATH_S, 0);
  volext_comments =
    utility_get_option_string_value (arg_map, ADDVOL_COMMENT_S, 0);
  volext_string_purpose =
    utility_get_option_string_value (arg_map, ADDVOL_PURPOSE_S, 0);
  if (volext_string_purpose == NULL)
    {
      volext_string_purpose = "generic";
    }

  Volext_purpose = DISK_PERMVOL_GENERIC_PURPOSE;

  if (volext_npages <= 0)
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_ADDVOLDB,
				       ADDVOLDB_MSG_BAD_NPAGES),
	       volext_npages);
      goto error_exit;
    }
  if (strcasecmp (volext_string_purpose, "data") == 0)
    {
      Volext_purpose = DISK_PERMVOL_DATA_PURPOSE;
    }
  else if (strcasecmp (volext_string_purpose, "index") == 0)
    {
      Volext_purpose = DISK_PERMVOL_INDEX_PURPOSE;
    }
  else if (strcasecmp (volext_string_purpose, "temp") == 0)
    {
      Volext_purpose = DISK_PERMVOL_TEMP_PURPOSE;
    }
  else if (strcasecmp (volext_string_purpose, "generic") == 0)
    {
      Volext_purpose = DISK_PERMVOL_GENERIC_PURPOSE;
    }
  else
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_ADDVOLDB,
				       ADDVOLDB_MSG_BAD_PURPOSE),
	       volext_string_purpose);
      goto error_exit;
    }

  /* extra validation */
  if (check_database_name (database_name) || check_volume_name (volext_name))
    {
      goto error_exit;
    }

  /* error message log file */
  sprintf (er_msg_file, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  /* tuning system parameters */
  sysprm_set_to_default (PRM_NAME_PB_NBUFFERS, true);
  sysprm_set_force (PRM_NAME_JAVA_STORED_PROCEDURE, "no");

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("dba", NULL);
  if (db_restart (arg->command_name, TRUE, database_name) == NO_ERROR)
    {
      if (db_add_volume (volext_pathname, volext_name, volext_comments,
			 volext_npages, Volext_purpose) == NO_ERROR)
	{
	  db_commit_transaction ();
	}
      else
	{
	  fprintf (stderr, "%s\n", db_error_string (3));
	  db_shutdown ();
	  goto error_exit;
	}
      db_shutdown ();
    }
  else
    {
      fprintf (stderr, "%s\n", db_error_string (3));
      goto error_exit;
    }
  return EXIT_SUCCESS;

print_addvol_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_ADDVOLDB,
				   ADDVOLDB_MSG_USAGE),
	   basename (arg->argv0));
error_exit:
  return EXIT_FAILURE;
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
  bool repair = false;
  int flag;

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_check_usage;
    }

  database_name =
    utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_check_usage;
    }

  repair = utility_get_option_bool_value (arg_map, CHECK_REPAIR_S);

  flag = CHECKDB_ALL_CHECK;

  /* extra validation */
  if (check_database_name (database_name))
    {
      goto error_exit;
    }

  /* error message log file */
  sprintf (er_msg_file, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  sysprm_set_force (PRM_NAME_JAVA_STORED_PROCEDURE, "no");

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("dba", NULL);
  if (db_restart (arg->command_name, TRUE, database_name) == NO_ERROR)
    {
      if (repair)
	{
	  flag |= CHECKDB_REPAIR;
	}
      if (db_set_isolation (TRAN_READ_COMMITTED) != NO_ERROR ||
	  boot_check_db_consistency (flag) != NO_ERROR)
	{
	  const char *tmpname;
	  if ((tmpname = er_msglog_filename ()) == NULL)
	    tmpname = "/dev/null";
	  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
					   MSGCAT_UTIL_SET_CHECKDB,
					   CHECKDB_MSG_INCONSISTENT),
		   tmpname);
	  db_shutdown ();
	  goto error_exit;
	}
      db_shutdown ();
    }
  else
    {
      fprintf (stderr, "%s\n", db_error_string (3));
      goto error_exit;
    }
  return EXIT_SUCCESS;

print_check_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_CHECKDB,
				   CHECKDB_MSG_USAGE), basename (arg->argv0));
error_exit:
  return EXIT_FAILURE;
}

#define VOL_PURPOSE_STRING(VOL_PURPOSE)		\
	    ((VOL_PURPOSE == DISK_PERMVOL_DATA_PURPOSE) ? "DATA"	\
	    : (VOL_PURPOSE == DISK_PERMVOL_INDEX_PURPOSE) ? "INDEX"	\
	    : (VOL_PURPOSE == DISK_PERMVOL_GENERIC_PURPOSE) ? "GENERIC"	\
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
  int vol_ntotal_pages;
  int vol_nfree_pages;
  char vol_label[PATH_MAX];
  int db_ntotal_pages, db_nfree_pages;
  FILE *outfp = NULL;
  int nvols;
  VOLID temp_volid;
  char num_total_str[64], num_free_str[64];

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_space_usage;
    }

  database_name =
    utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_space_usage;
    }

  output_file =
    utility_get_option_string_value (arg_map, SPACE_OUTPUT_FILE_S, 0);
  size_unit = utility_get_option_string_value (arg_map, SPACE_SIZE_UNIT_S, 0);

  size_unit_type = SPACEDB_SIZE_UNIT_PAGE;

  if (size_unit != NULL)
    {
      if (strcasecmp (size_unit, "m") == 0)
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
      else if (strcasecmp (size_unit, "h") == 0)
	{
	  size_unit_type = SPACEDB_SIZE_UNIT_HUMAN_READABLE;
	}
      else if (strcasecmp (size_unit, "page") != 0)
	{
	  /* invalid option string */
	  goto print_space_usage;
	}
    }

  if (output_file == NULL)
    outfp = stdout;
  else
    {
      outfp = fopen (output_file, "w");
      if (outfp == NULL)
	{
	  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
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
  sprintf (er_msg_file, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  /* tuning system parameters */
  sysprm_set_to_default (PRM_NAME_PB_NBUFFERS, true);
  sysprm_set_force (PRM_NAME_JAVA_STORED_PROCEDURE, "no");

  /* should have little copyright herald message ? */
  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("dba", NULL);

  if (db_restart (arg->command_name, TRUE, database_name) != NO_ERROR)
    {
      fprintf (stderr, "%s\n", db_error_string (3));
      goto error_exit;
    }

  nvols = db_num_volumes ();
  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
				  MSGCAT_UTIL_SET_SPACEDB,
				  SPACEDB_OUTPUT_SUMMARY), database_name,
	   IO_PAGESIZE, LOG_PAGESIZE);

  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
				  MSGCAT_UTIL_SET_SPACEDB,
				  (size_unit_type == SPACEDB_SIZE_UNIT_PAGE) ?
				  SPACEDB_OUTPUT_TITLE_PAGE :
				  SPACEDB_OUTPUT_TITLE_SIZE));

  db_ntotal_pages = db_nfree_pages = 0;

  for (i = 0; i < nvols; i++)
    {
      if (db_purpose_totalpgs_freepgs (i, &vol_purpose, &vol_ntotal_pages,
				       &vol_nfree_pages) != NULL_VOLID)
	{
	  db_ntotal_pages += vol_ntotal_pages;
	  db_nfree_pages += vol_nfree_pages;
	  if (db_vol_label (i, vol_label) == NULL)
	    strcpy (vol_label, " ");

	  spacedb_get_size_str (num_total_str, vol_ntotal_pages,
				size_unit_type);
	  spacedb_get_size_str (num_free_str, vol_nfree_pages,
				size_unit_type);

	  fprintf (outfp,
		   msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_SPACEDB,
				   SPACEDB_OUTPUT_FORMAT),
		   i, VOL_PURPOSE_STRING (vol_purpose),
		   num_total_str, num_free_str, vol_label);
	}
      else
	{
	  fprintf (stderr, "%s\n", db_error_string (3));
	  db_shutdown ();
	  goto error_exit;
	}
    }
  if (nvols > 1)
    {
      fprintf (outfp, "-------------------------------------------------");
      fprintf (outfp, "------------------------------\n");

      spacedb_get_size_str (num_total_str, db_ntotal_pages, size_unit_type);
      spacedb_get_size_str (num_free_str, db_nfree_pages, size_unit_type);

      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
				      MSGCAT_UTIL_SET_SPACEDB,
				      SPACEDB_OUTPUT_FORMAT), nvols, " ",
	       num_total_str, num_free_str, " ");
    }

  /* Find info on temp volumes */
  nvols = boot_find_number_temp_volumes ();
  temp_volid = boot_find_last_temp ();
  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
				  MSGCAT_UTIL_SET_SPACEDB,
				  SPACEDB_OUTPUT_SUMMARY_TMP_VOL),
	   database_name, IO_PAGESIZE);

  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
				  MSGCAT_UTIL_SET_SPACEDB,
				  (size_unit_type == SPACEDB_SIZE_UNIT_PAGE) ?
				  SPACEDB_OUTPUT_TITLE_PAGE :
				  SPACEDB_OUTPUT_TITLE_SIZE));

  db_ntotal_pages = db_nfree_pages = 0;

  for (i = 0; i < nvols; i++)
    {
      if (db_purpose_totalpgs_freepgs ((temp_volid + i), &vol_purpose,
				       &vol_ntotal_pages,
				       &vol_nfree_pages) != NULL_VOLID)
	{
	  db_ntotal_pages += vol_ntotal_pages;
	  db_nfree_pages += vol_nfree_pages;
	  if (db_vol_label ((temp_volid + i), vol_label) == NULL)
	    strcpy (vol_label, " ");

	  spacedb_get_size_str (num_total_str, vol_ntotal_pages,
				size_unit_type);
	  spacedb_get_size_str (num_free_str, vol_nfree_pages,
				size_unit_type);

	  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
					  MSGCAT_UTIL_SET_SPACEDB,
					  SPACEDB_OUTPUT_FORMAT),
		   (temp_volid + i), VOL_PURPOSE_STRING (vol_purpose),
		   num_total_str, num_free_str, vol_label);
	}
      else
	{
	  fprintf (stderr, "%s\n", db_error_string (3));
	  db_shutdown ();
	  goto error_exit;
	}
    }
  if (nvols > 1)
    {
      fprintf (outfp, "-------------------------------------------------");
      fprintf (outfp, "------------------------------\n");

      spacedb_get_size_str (num_total_str, vol_ntotal_pages, size_unit_type);
      spacedb_get_size_str (num_free_str, vol_nfree_pages, size_unit_type);

      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
				      MSGCAT_UTIL_SET_SPACEDB,
				      SPACEDB_OUTPUT_FORMAT), nvols, " ",
	       num_total_str, num_free_str, " ");
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
error_exit:
  if (outfp != stdout && outfp != NULL)
    {
      fclose (outfp);
    }
  return EXIT_FAILURE;
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

  database_name =
    utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_lock_usage;
    }

  output_file =
    utility_get_option_string_value (arg_map, LOCK_OUTPUT_FILE_S, 0);

  if (output_file == NULL)
    outfp = stdout;
  else
    {
      outfp = fopen (output_file, "w");
      if (outfp == NULL)
	{
	  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
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
  sprintf (er_msg_file, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  /* should have little copyright herald message ? */
  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("dba", NULL);

  if (db_restart (arg->command_name, TRUE, database_name) != NO_ERROR)
    {
      fprintf (stderr, "%s\n", db_error_string (3));
      goto error_exit;
    }

  (void) db_set_isolation (TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE);
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
 *   tran_index(in)
 *   user_name(in)
 *   hostname(in)
 *   progname(in)
 */
static int
doesmatch_transaction (const ONE_TRAN_INFO * tran, int tran_index,
		       const char *username, const char *hostname,
		       const char *progname)
{
  int match;

  match = (isvalid_transaction (tran)
	   && (tran->tran_index == tran_index
	       || (username != NULL
		   && strcmp (tran->login_name, username) == 0)
	       || (hostname != NULL
		   && strcmp (tran->host_name, hostname) == 0)
	       || (progname != NULL
		   && strcmp (tran->program_name, progname) == 0)));

  return match;

}

/*
 * dump_trantb() - Displays information about all the currently
 *                 active transactions
 *   return: none
 *   info(in)
 */
static void
dump_trantb (const TRANS_INFO * info)
{
  int i;
  int num_valid = 0;

  if (info != NULL && info->num_trans > 0)
    {
      /*
       * remember that we have to print the messages one at a time, mts_
       * resuses the message buffer on each call.
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
						   MSGCAT_UTIL_SET_KILLTRAN,
						   KILLTRAN_MSG_TABLE_HEADER));
		  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
						   MSGCAT_UTIL_SET_KILLTRAN,
						   KILLTRAN_MSG_TABLE_UNDERSCORE));
		}
	      num_valid++;
	      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
					       MSGCAT_UTIL_SET_KILLTRAN,
					       KILLTRAN_MSG_TABLE_ENTRY),
		       info->tran[i].tran_index,
		       TRAN_STATE_CHAR (info->tran[i].state),
		       info->tran[i].db_user, info->tran[i].host_name,
		       info->tran[i].process_id, info->tran[i].program_name);
	    }
	}
    }

  if (num_valid > 0)
    fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
				     MSGCAT_UTIL_SET_KILLTRAN,
				     KILLTRAN_MSG_TABLE_UNDERSCORE));
  else
    fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
				     MSGCAT_UTIL_SET_KILLTRAN,
				     KILLTRAN_MSG_NONE_TABLE_ENTRIES));

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
 *       transaction(s).                                               *
 *
 *       if tran_index != -1
 *         kill only the transaction associated with tran index.
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
kill_transactions (TRANS_INFO * info, int tran_index,
		   const char *username, const char *hostname,
		   const char *progname, bool verify)
{
  int i, ok;
  int nkills = 0, nfailures = 0;
  int ch;

  /* see if we have anything do do */
  for (i = 0; i < info->num_trans; i++)
    if (doesmatch_transaction (&info->tran[i], tran_index, username, hostname,
			       progname))
      {
	break;
      }

  if (i >= info->num_trans)
    {
      /*
       * There is not matches
       */
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_KILLTRAN,
				       KILLTRAN_MSG_NO_MATCHES));
    }
  else
    {
      if (!verify)
	ok = 1;
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
					   MSGCAT_UTIL_SET_KILLTRAN,
					   KILLTRAN_MSG_TABLE_HEADER));
	  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
					   MSGCAT_UTIL_SET_KILLTRAN,
					   KILLTRAN_MSG_TABLE_UNDERSCORE));

	  for (i = 0; i < info->num_trans; i++)
	    if (doesmatch_transaction (&info->tran[i], tran_index, username,
				       hostname, progname))
	      {
		fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
						 MSGCAT_UTIL_SET_KILLTRAN,
						 KILLTRAN_MSG_TABLE_ENTRY),
			 info->tran[i].tran_index,
			 TRAN_STATE_CHAR (info->tran[i].state),
			 info->tran[i].db_user, info->tran[i].host_name,
			 info->tran[i].process_id,
			 info->tran[i].program_name);
	      }
	  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
					   MSGCAT_UTIL_SET_KILLTRAN,
					   KILLTRAN_MSG_TABLE_UNDERSCORE));

	  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
					   MSGCAT_UTIL_SET_KILLTRAN,
					   KILLTRAN_MSG_VERIFY));
	  fflush (stdout);

	  ch = getc (stdin);
	  if (ch == 'Y' || ch == 'y')
	    ok = 1;
	}

      if (ok)
	{
	  for (i = 0; i < info->num_trans; i++)
	    {
	      if (doesmatch_transaction (&info->tran[i], tran_index, username,
					 hostname, progname))
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
						   MSGCAT_UTIL_SET_KILLTRAN,
						   KILLTRAN_MSG_TABLE_HEADER));
			  fprintf (stdout,
				   msgcat_message (MSGCAT_CATALOG_UTILS,
						   MSGCAT_UTIL_SET_KILLTRAN,
						   KILLTRAN_MSG_TABLE_UNDERSCORE));
			}
		      fprintf (stdout,
			       msgcat_message (MSGCAT_CATALOG_UTILS,
					       MSGCAT_UTIL_SET_KILLTRAN,
					       KILLTRAN_MSG_TABLE_ENTRY),
			       info->tran[i].tran_index,
			       TRAN_STATE_CHAR (info->tran[i].state),
			       info->tran[i].db_user,
			       info->tran[i].host_name,
			       info->tran[i].process_id,
			       info->tran[i].program_name);
		      if (er_errid () != NO_ERROR)
			fprintf (stdout, "%s\n", db_error_string (3));
		      else	/* probably it is the case of timeout */
			fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
							 MSGCAT_UTIL_SET_KILLTRAN,
							 KILLTRAN_MSG_KILL_TIMEOUT));
		      nfailures++;
		    }
		}
	    }
	  if (nfailures > 0)
	    {
	      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
					       MSGCAT_UTIL_SET_KILLTRAN,
					       KILLTRAN_MSG_TABLE_UNDERSCORE));
	    }
	}
    }

  return nkills;
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
  int kill_tran_index;
  const char *kill_progname;
  const char *kill_user;
  const char *kill_host;
  const char *dba_password;
  bool dump_trantab_flag;
  bool force = true;
  int isbatch;
  char *passbuf = NULL;
  TRANS_INFO *info = NULL;
  int error;

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_killtran_usage;
    }

  database_name =
    utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_killtran_usage;
    }

  kill_tran_index =
    utility_get_option_int_value (arg_map, KILLTRAN_KILL_TRANSACTION_INDEX_S);
  kill_user =
    utility_get_option_string_value (arg_map, KILLTRAN_KILL_USER_NAME_S, 0);
  kill_host =
    utility_get_option_string_value (arg_map, KILLTRAN_KILL_HOST_NAME_S, 0);
  kill_progname =
    utility_get_option_string_value (arg_map, KILLTRAN_KILL_PROGRAM_NAME_S,
				     0);

  force = utility_get_option_bool_value (arg_map, KILLTRAN_FORCE_S);
  dba_password =
    utility_get_option_string_value (arg_map, KILLTRAN_DBA_PASSWORD_S, 0);
  dump_trantab_flag =
    utility_get_option_bool_value (arg_map, KILLTRAN_DISPLAY_INFORMATION_S);

  if (check_database_name (database_name))
    {
      goto error_exit;
    }

  isbatch = 0;
  if (kill_tran_index != -1)
    isbatch++;
  if (kill_user != NULL && strlen (kill_user) != 0)
    isbatch++;
  if (kill_host != NULL && strlen (kill_host) != 0)
    isbatch++;
  if (kill_progname != NULL && strlen (kill_progname) != 0)
    isbatch++;
  if (isbatch > 1)
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_KILLTRAN,
				       KILLTRAN_MSG_MANY_ARGS));
      goto error_exit;
    }

  /* error message log file */
  sprintf (er_msg_file, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  if (db_login ("dba", dba_password) != NO_ERROR)
    {
      fprintf (stderr, "%s\n", db_error_string (3));
      goto error_exit;
    }

  /* first try to restart with the password given (possibly none) */
  error = db_restart (arg->command_name, TRUE, database_name);
  if (error)
    {
      if (error == ER_AU_INVALID_PASSWORD &&
	  (dba_password == NULL || strlen (dba_password) == 0))
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
	    passbuf = (char *) NULL;
	  dba_password = passbuf;
	  if (db_login ("dba", dba_password) != NO_ERROR)
	    goto error_exit;
	  else
	    error = db_restart (arg->command_name, TRUE, database_name);
	}

      if (error)
	{
	  fprintf (stderr, "%s\n", db_error_string (3));
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

  info = logtb_get_trans_info ();
  if (info == NULL)
    {
      db_shutdown ();
      goto error_exit;
    }

  if (dump_trantab_flag ||
      (kill_tran_index == -1
       && (kill_user == NULL || strlen (kill_user) == 0)
       && (kill_host == NULL || strlen (kill_host) == 0)
       && (kill_progname == NULL || strlen (kill_progname) == 0)))
    {
      dump_trantb (info);
    }
  else
    {
      /* some piece of transaction identifier was entered, try to use it */
      if (kill_transactions (info, kill_tran_index, kill_user, kill_host,
			     kill_progname, !force) <= 0)
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
error_exit:
  return EXIT_FAILURE;
#else /* CS_MODE */
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
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

  database_name =
    utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_plandump_usage;
    }

  drop_flag = utility_get_option_bool_value (arg_map, PLANDUMP_DROP_S);
  output_file =
    utility_get_option_string_value (arg_map, PLANDUMP_OUTPUT_FILE_S, 0);

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_plandump_usage;
    }

  if (output_file == NULL)
    outfp = stdout;
  else
    {
      outfp = fopen (output_file, "w");
      if (outfp == NULL)
	{
	  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
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
  sprintf (er_msg_file, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  /* should have little copyright herald message ? */
  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("dba", NULL);

  if (db_restart (arg->command_name, TRUE, database_name) != NO_ERROR)
    {
      fprintf (stderr, "%s\n", db_error_string (3));
      goto error_exit;
    }

  qmgr_dump_query_plans (outfp);
  if (drop_flag)
    {
      if (qmgr_drop_all_query_plans () != NO_ERROR)
	{
	  fprintf (outfp, "%s\n", db_error_string (3));
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

  database_name =
    utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_dumpparam_usage;
    }

  output_file =
    utility_get_option_string_value (arg_map, PARAMDUMP_OUTPUT_FILE_S, 0);
  both_flag = utility_get_option_bool_value (arg_map, PARAMDUMP_BOTH_S);

  if (output_file == NULL)
    outfp = stdout;
  else
    {
      outfp = fopen (output_file, "w");
      if (outfp == NULL)
	{
	  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
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
  sprintf (er_msg_file, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  sysprm_set_force (PRM_NAME_JAVA_STORED_PROCEDURE, "no");

#if defined (CS_MODE)
  /* should have little copyright herald message ? */
  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("dba", NULL);

  if (db_restart (arg->command_name, TRUE, database_name) != NO_ERROR)
    {
      fprintf (stderr, "%s\n", db_error_string (3));
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

  if (output_file == NULL)
    {
      outfp = stdout;
    }
  else
    {
      outfp = fopen (output_file, "w");
      if (outfp == NULL)
	{
	  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
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
  sprintf (er_msg_file, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  /* should have little copyright herald message ? */
  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("dba", NULL);

  if (db_restart (arg->command_name, TRUE, database_name) != NO_ERROR)
    {
      fprintf (stderr, "%s\n", db_error_string (3));
      goto error_exit;
    }

  histo_start (true);
  do
    {
      print_timestamp (outfp);
      histo_print_global_stats (outfp);
      if (cumulative == false)
	{
	  histo_clear_global_stats ();
	}
      sleep (interval);
    }
  while (interval > 0);
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

error_exit:
  if (outfp != stdout && outfp != NULL)
    {
      fclose (outfp);
    }
  return EXIT_FAILURE;
#else /* CS_MODE */
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
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

  strcpy (prm_buf, PRM_NAME_HA_MODE);
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
 * changemode_keyword() - get keyword value or string of the server mode
 *   return: NO_ERROR or ER_FAILED
 *   keyval_p(in/out): keyword value
 *   keystr_p(in/out): keyword string
 */
static int
changemode_keyword (int *keyval_p, char **keystr_p)
{
  static UTIL_KEYWORD keywords[] = {
    {HA_SERVER_STATE_IDLE, HA_SERVER_STATE_IDLE_STR},
    {HA_SERVER_STATE_ACTIVE, HA_SERVER_STATE_ACTIVE_STR},
    {HA_SERVER_STATE_TO_BE_ACTIVE, HA_SERVER_STATE_TO_BE_ACTIVE_STR},
    {HA_SERVER_STATE_STANDBY, HA_SERVER_STATE_STANDBY_STR},
    {HA_SERVER_STATE_TO_BE_STANDBY, HA_SERVER_STATE_TO_BE_STANDBY_STR},
    {HA_SERVER_STATE_MAINTENANCE, HA_SERVER_STATE_MAINTENANCE_STR},
    {HA_SERVER_STATE_DEAD, HA_SERVER_STATE_DEAD_STR},
    {-1, NULL}
  };

  return utility_keyword_search (keywords, keyval_p, keystr_p);
}

/*
 * changemode() - changemode main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
changemode (UTIL_FUNCTION_ARG * arg)
{
#if defined (WINDOWS)
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
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
  bool wait, force;

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_changemode_usage;
    }

  database_name =
    utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_changemode_usage;
    }

  mode_name = utility_get_option_string_value (arg_map, CHANGEMODE_MODE_S, 0);
  wait = utility_get_option_bool_value (arg_map, CHANGEMODE_WAIT_S);
  force = utility_get_option_bool_value (arg_map, CHANGEMODE_FORCE_S);

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
	      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
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
	  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
					   MSGCAT_UTIL_SET_CHANGEMODE,
					   CHANGEMODE_MSG_BAD_MODE),
		   mode_name);
	  goto error_exit;
	}
    }

  /* error message log file */
  sprintf (er_msg_file, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  if (db_login ("dba", NULL) != NO_ERROR)
    {
      fprintf (stderr, "%s\n", db_error_string (3));
      goto error_exit;
    }
  error = db_restart (arg->command_name, TRUE, database_name);
  if (error != NO_ERROR)
    {
      fprintf (stderr, "%s\n", db_error_string (3));
      goto error_exit;
    }

  if (PRM_HA_MODE == HA_MODE_OFF)
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_CHANGEMODE,
				       CHANGEMODE_MSG_NOT_HA_MODE));
      goto error_exit;
    }

  if (mode_name == NULL)
    {
      /* display the value of current mode */
      mode = boot_change_ha_mode (HA_SERVER_MODE_NA, false, false);
    }
  else
    {
      /* change server's HA mode */
      mode = boot_change_ha_mode (mode, force, wait);
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
      fprintf (stderr, "%s\n", db_error_string (3));
    }

  (void) db_shutdown ();
  return EXIT_SUCCESS;

print_changemode_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_CHANGEMODE,
				   CHANGEMODE_MSG_USAGE),
	   basename (arg->argv0));

error_exit:
  return EXIT_FAILURE;
#else /* CS_MODE */
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_CHANGEMODE,
				   CHANGEMODE_MSG_NOT_IN_STANDALONE),
	   basename (arg->argv0));
  return EXIT_FAILURE;
#endif /* !CS_MODE */
#endif /* !WINDOWS */
}

/*
 * copylogdb_keyword() - get keyword value or string of the copylogdb mode
 *   return: NO_ERROR or ER_FAILED
 *   keyval_p(in/out): keyword value
 *   keystr_p(in/out): keyword string
 */
static int
copylogdb_keyword (int *keyval_p, char **keystr_p)
{
  static UTIL_KEYWORD keywords[] = {
    {LOGWR_MODE_ASYNC, "async"},
    {LOGWR_MODE_SEMISYNC, "semisync"},
    {LOGWR_MODE_SYNC, "sync"},
    {-1, NULL}
  };

  return utility_keyword_search (keywords, keyval_p, keystr_p);
}

/*
 * copylogdb() - copylogdb main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
copylogdb (UTIL_FUNCTION_ARG * arg)
{
#if defined (WINDOWS)
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
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
#if !defined(WINDOWS)
  char *binary_name;
  char executable_path[PATH_MAX];
#endif

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_copylog_usage;
    }

  database_name =
    utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
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
	      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
					       MSGCAT_UTIL_SET_COPYLOGDB,
					       COPYLOGDB_MSG_BAD_MODE),
		       mode_name);
	      goto error_exit;
	    }
	}
      if (!(mode >= LOGWR_MODE_ASYNC && mode <= LOGWR_MODE_SYNC))
	{
	  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
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

  /* error message log file */
  sprintf (er_msg_file, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  db_Enable_replications++;
  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_LOG_COPIER);
  if (db_login ("dba", NULL) != NO_ERROR)
    {
      fprintf (stderr, "%s\n", db_error_string (3));
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

  if (PRM_HA_MODE)
    {
      error = hb_process_init (database_name, log_path, true);
      if (error != NO_ERROR)
	{
	  er_log_debug (ARG_FILE_LINE,
			"cannot connect to cub_master for heartbeat. \n");
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

  if (PRM_HA_MODE == HA_MODE_OFF)
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_COPYLOGDB,
				       COPYLOGDB_MSG_NOT_HA_MODE));
      (void) db_shutdown ();
      goto error_exit;
    }

  error = logwr_copy_log_file (database_name, log_path, mode);
  if (error != NO_ERROR)
    {
      fprintf (stdout, "%s\n", db_error_string (3));
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
  return EXIT_FAILURE;

error_exit:
#if !defined(WINDOWS)
  if (hb_Proc_shutdown)
    {
      return EXIT_SUCCESS;
    }
#endif

  if (error == ER_NET_SERVER_CRASHED
      || error == ER_NET_CANT_CONNECT_SERVER
      || error == ER_BO_CONNECT_FAILED
      || error == ERR_CSS_TCP_CANNOT_CONNECT_TO_MASTER)
    {
      (void) sleep (sleep_nsecs);
      /* sleep 1, 2, 4, 8, etc; don't wait for more than 1/2 min */
      if ((sleep_nsecs *= 2) > 32)
	{
	  sleep_nsecs = 1;
	}
      ++retried;
      goto retry;
    }

  return EXIT_FAILURE;
#else /* CS_MODE */
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
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
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_APPLYLOGDB,
				   APPLYLOGDB_MSG_HA_NOT_SUPPORT),
	   basename (arg->argv0));

  return EXIT_FAILURE;
#else /* WINDOWS */
#if defined (CS_MODE)
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *database_name;
  const char *log_path;
  char log_path_buf[PATH_MAX];
  char *log_path_base;
  int test_log;
  int max_mem_size;
  int error = NO_ERROR;
  int retried = 0, sleep_nsecs = 1;
#if !defined(WINDOWS)
  char *binary_name;
  char executable_path[PATH_MAX];
#endif


  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_applylog_usage;
    }

  database_name =
    utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_applylog_usage;
    }

  log_path =
    utility_get_option_string_value (arg_map, APPLYLOG_LOG_PATH_S, 0);
  test_log = utility_get_option_int_value (arg_map, APPLYLOG_TEST_LOG_S);
  max_mem_size =
    utility_get_option_int_value (arg_map, APPLYLOG_MAX_MEM_SIZE_S);

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
  if (max_mem_size > 1000)
    {
      goto print_applylog_usage;
    }

  /* error message log file */
  log_path_base = strdup (log_path);
  sprintf (er_msg_file, "%s_%s_%s.err", database_name, arg->command_name,
	   basename (log_path_base));
  free (log_path_base);
  er_init (er_msg_file, ER_NEVER_EXIT);

  if (test_log >= 0)
    {
      la_test_log_page (database_name, log_path, test_log);
      return EXIT_SUCCESS;
    }

  db_Enable_replications++;
  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_LOG_APPLIER);
  if (db_login ("dba", NULL) != NO_ERROR)
    {
      fprintf (stderr, "%s\n", db_error_string (3));
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

  if (PRM_HA_MODE)
    {
      /* initialize heartbeat */
      error = hb_process_init (database_name, log_path, false);
      if (error != NO_ERROR)
	{
	  er_log_debug (ARG_FILE_LINE,
			"Cannot connect to cub_master for heartbeat. \n");
	  return EXIT_FAILURE;
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
  /* set lock timeout to infinite */
  db_set_lock_timeout (-1);

  if (PRM_HA_MODE == HA_MODE_OFF)
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

error_exit:
#if !defined(WINDOWS)
  if (hb_Proc_shutdown)
    {
      return EXIT_SUCCESS;
    }
#endif

  if (error == ER_NET_SERVER_CRASHED
      || error == ER_NET_CANT_CONNECT_SERVER
      || error == ERR_CSS_TCP_CANNOT_CONNECT_TO_MASTER
      || error == ER_BO_CONNECT_FAILED || error == ER_NET_SERVER_COMM_ERROR)
    {
      (void) sleep (sleep_nsecs);
      /* sleep 1, 2, 4, 8, etc; don't wait for more than 10 sec */
      if ((sleep_nsecs *= 2) > 10)
	{
	  sleep_nsecs = 1;
	}
      ++retried;
      er_init (er_msg_file, ER_NEVER_EXIT);
      goto retry;
    }

  return EXIT_FAILURE;
#else /* CS_MODE */
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
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
spacedb_get_size_str (char *buf, int num_pages, T_SPACEDB_SIZE_UNIT size_unit)
{
  int pgsize, i;
  double size;

  assert (buf);

  if (size_unit == SPACEDB_SIZE_UNIT_PAGE)
    {
      sprintf (buf, "%11d", num_pages);
    }
  else
    {
      pgsize = IO_PAGESIZE / 1024;
      size = pgsize * num_pages;

      if (size_unit == SPACEDB_SIZE_UNIT_HUMAN_READABLE)
	{
	  for (i = SPACEDB_SIZE_UNIT_MBYTES;
	       i < SPACEDB_SIZE_UNIT_TBYTES; i++)
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
