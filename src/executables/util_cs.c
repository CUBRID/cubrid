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
#include "databases_file.h"
#include "boot_sr.h"
#include "db.h"
#include "authenticate.h"
#include "server_interface.h"
#include "object_representation.h"
#include "transaction_cl.h"
#include "porting.h"
#include "network_interface_cl.h"

#define PASSBUF_SIZE 12

static void sig_interrupt (int sig_no);

/*
 * backupdb() - backupdb main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
backupdb (UTIL_FUNCTION_ARG * arg)
{
  UTIL_ARG_MAP *arg_map = arg->arg_map;
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

  AU_DISABLE_PASSWORDS ();	/* disable authorization for this operation */
  db_login ("dba", NULL);

  if (db_restart (arg->argv0, TRUE, database_name) == NO_ERROR)
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
	      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
					       MSGCAT_UTIL_SET_CHECKDB,
					       CHECKDB_MSG_INCONSISTENT),
		       tmpname);
	      db_shutdown ();
	      goto error_exit;
	    }
	}

      /* some other utilities may need interrupt handler too */
      if (os_set_signal_handler (SIGINT, sig_interrupt) == SIG_ERR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  fprintf (stdout, "%s\n", db_error_string (3));
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
	  /* resolve relative path */
	  getcwd (verbose_file_realpath, PATH_MAX);
	  strcat (verbose_file_realpath, "/");
	  strcat (verbose_file_realpath, backup_verbose_file);
	  backup_verbose_file = verbose_file_realpath;
	}

      if (boot_backup (backup_path, (FILEIO_BACKUP_LEVEL) backup_level,
		       remove_log_archives, backup_verbose_file,
		       backup_num_threads,
		       backup_zip_method, backup_zip_level,
		       skip_activelog, safe_pageid) == NO_ERROR)
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
  const char *database_name;
  int volext_npages = 0;
  const char *volext_name = NULL;
  const char *volext_pathname = NULL;
  const char *volext_comments = NULL;
  const char *volext_string_purpose = "generic";
  DISK_VOLPURPOSE Volext_purpose;

  if (utility_get_option_string_table_size (arg_map) != 2)
    {
      goto print_addvol_usage;
    }

  database_name =
    utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  volext_npages =
    atoi (utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 1));
  volext_name =
    utility_get_option_string_value (arg_map, ADDVOL_VOLUME_NAME_S, 0);
  volext_pathname =
    utility_get_option_string_value (arg_map, ADDVOL_FILE_PATH_S, 0);
  volext_comments =
    utility_get_option_string_value (arg_map, ADDVOL_COMMENT_S, 0);
  volext_string_purpose =
    utility_get_option_string_value (arg_map, ADDVOL_PURPOSE_S, 0);

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

  AU_DISABLE_PASSWORDS ();
  db_login ("dba", NULL);
  if (db_restart (arg->argv0, TRUE, database_name) == NO_ERROR)
    {
      if (db_add_volume
	  (volext_pathname, volext_name, volext_comments,
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
  const char *database_name;
  bool repair = false;
  int flag;

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_check_usage;
    }

  database_name =
    utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  repair = utility_get_option_bool_value (arg_map, CHECK_REPAIR_S);

  flag = CHECKDB_ALL_CHECK;

  /* extra validation */
  if (check_database_name (database_name))
    {
      goto error_exit;
    }

  AU_DISABLE_PASSWORDS ();
  db_login ("dba", NULL);
  if (db_restart (arg->argv0, TRUE, database_name) == NO_ERROR)
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
  const char *database_name;
  const char *output_file = NULL;
  int i;
  DB_VOLPURPOSE vol_purpose;
  int vol_ntotal_pages;
  int vol_nfree_pages;
  char vol_label[PATH_MAX];
  int db_ntotal_pages, db_nfree_pages;
  FILE *outfp = NULL;
  int nvols;
  VOLID temp_volid;

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_space_usage;
    }

  database_name =
    utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  output_file =
    utility_get_option_string_value (arg_map, SPACE_OUTPUT_FILE_S, 0);

  sysprm_set_to_default ("data_buffer_pages");

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

  /* should have little copyright herald message ? */
  AU_DISABLE_PASSWORDS ();
  db_login ("dba", NULL);

  if (db_restart (arg->argv0, TRUE, database_name) != NO_ERROR)
    {
      fprintf (outfp, "%s\n", db_error_string (3));
      goto error_exit;
    }

  nvols = db_num_volumes ();
  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
				  MSGCAT_UTIL_SET_SPACEDB,
				  SPACEDB_OUTPUT_TITLE), database_name,
	   IO_PAGESIZE);
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
	  fprintf (outfp,
		   msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_SPACEDB,
				   SPACEDB_OUTPUT_FORMAT),
		   i, VOL_PURPOSE_STRING (vol_purpose),
		   vol_ntotal_pages, vol_nfree_pages, vol_label);
	}
      else
	{
	  fprintf (outfp, "%s\n", db_error_string (3));
	  db_shutdown ();
	  goto error_exit;
	}
    }
  if (nvols > 1)
    {
      fprintf (outfp, "-------------------------------------------------");
      fprintf (outfp, "------------------------------\n");
      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
				      MSGCAT_UTIL_SET_SPACEDB,
				      SPACEDB_OUTPUT_FORMAT), nvols, " ",
	       db_ntotal_pages, db_nfree_pages, " ");
    }

  /* Find info on temp volumes */
  nvols = boot_find_number_temp_volumes ();
  temp_volid = boot_find_last_temp ();
  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
				  MSGCAT_UTIL_SET_SPACEDB,
				  SPACEDB_OUTPUT_TITLE1), database_name,
	   IO_PAGESIZE);
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
	  fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
					  MSGCAT_UTIL_SET_SPACEDB,
					  SPACEDB_OUTPUT_FORMAT),
		   (temp_volid + i), VOL_PURPOSE_STRING (vol_purpose),
		   vol_ntotal_pages, vol_nfree_pages, vol_label);
	}
      else
	{
	  fprintf (outfp, "%s\n", db_error_string (3));
	  db_shutdown ();
	  goto error_exit;
	}
    }
  if (nvols > 1)
    {
      fprintf (outfp, "-------------------------------------------------");
      fprintf (outfp, "------------------------------\n");
      fprintf (outfp, msgcat_message (MSGCAT_CATALOG_UTILS,
				      MSGCAT_UTIL_SET_SPACEDB,
				      SPACEDB_OUTPUT_FORMAT), nvols, " ",
	       db_ntotal_pages, db_nfree_pages, " ");
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
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  const char *database_name;
  const char *output_file = NULL;
  FILE *outfp = NULL;

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_lock_usage;
    }

  database_name =
    utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
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

  /* should have little copyright herald message ? */
  AU_DISABLE_PASSWORDS ();
  db_login ("dba", NULL);

  if (db_restart (arg->argv0, TRUE, database_name) != NO_ERROR)
    {
      fprintf (outfp, "%s\n", db_error_string (3));
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
}

struct one_traninfo
{
  int tran_index;
  int state;
  int process_id;
  char *progname;
  char *username;
  char *hostname;
};

typedef struct
{
  int num_trans;		/* Number of transactions */
  struct one_traninfo tran[1];	/* Really num */
} TRANS_INFO;

/*
 * free_trantb() - Free transaction table information
 *   return: none
 *   info(in/out)
 */
static void
free_trantb (TRANS_INFO * info)
{
  int i;

  if (info == NULL)
    return;

  for (i = 0; i < info->num_trans; i++)
    {
      if (info->tran[i].progname != NULL)
	db_private_free_and_init (NULL, info->tran[i].progname);
      if (info->tran[i].username != NULL)
	db_private_free_and_init (NULL, info->tran[i].username);
      if (info->tran[i].hostname != NULL)
	db_private_free_and_init (NULL, info->tran[i].hostname);
    }
  free_and_init (info);
}

/*
 * get_trantb() - Get transaction table information which identifies
 *                active transactions
 *   return: TRANS_INFO array or NULL
 */
static TRANS_INFO *
get_trantb (void)
{
  TRANS_INFO *info = NULL;
  char *buffer, *ptr;
  int num_trans, bufsize, i;
  int error;

  error = logtb_get_pack_tran_table (&buffer, &bufsize);
  if (error != NO_ERROR || buffer == NULL)
    return NULL;

  ptr = buffer;
  ptr = or_unpack_int (ptr, &num_trans);

  if (num_trans == 0)
    {
      /* can't happen, there must be at least one transaction */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      goto error;
    }

  i = sizeof (TRANS_INFO) + ((num_trans - 1) * sizeof (struct one_traninfo));

  if ((info = (TRANS_INFO *) malloc (i)) == NULL)
    goto error;

  info->num_trans = num_trans;
  for (i = 0; i < num_trans; i++)
    {
      if ((ptr = or_unpack_int (ptr, &info->tran[i].tran_index)) == NULL ||
	  (ptr = or_unpack_int (ptr, &info->tran[i].state)) == NULL ||
	  (ptr = or_unpack_int (ptr, &info->tran[i].process_id)) == NULL ||
	  (ptr = or_unpack_string (ptr, &info->tran[i].progname)) == NULL ||
	  (ptr = or_unpack_string (ptr, &info->tran[i].username)) == NULL ||
	  (ptr = or_unpack_string (ptr, &info->tran[i].hostname)) == NULL)
	goto error;
    }

  if (((int) (ptr - buffer)) != bufsize)
    {
      /* unpacking didn't match size, garbage */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      goto error;
    }

  free_and_init (buffer);

  return info;

error:
  if (buffer != NULL)
    free_and_init (buffer);

  if (info != NULL)
    free_trantb (info);

  return NULL;
}

/*
 * isvalid_transaction() - test if transaction is valid
 *   return: non-zero if valid transaction
 *   tran(in)
 */
static int
isvalid_transaction (const struct one_traninfo *tran)
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
doesmatch_transaction (const struct one_traninfo *tran, int tran_index,
		       const char *username, const char *hostname,
		       const char *progname)
{
  int match;

  match = (isvalid_transaction (tran)
	   && (tran->tran_index == tran_index
	       || (username != NULL && strcmp (tran->username, username) == 0)
	       || (hostname != NULL && strcmp (tran->hostname, hostname) == 0)
	       || (progname != NULL
		   && strcmp (tran->progname, progname) == 0)));

  return match;

}

#define TRAN_STATE_CHAR(STATE)					\
	((STATE == TRAN_ACTIVE) ? '+' 				\
	: (info->tran[i].state == TRAN_UNACTIVE_ABORTED) ? '-'	\
	: ('A' + info->tran[i].state))
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
		       info->tran[i].username, info->tran[i].hostname,
		       info->tran[i].process_id, info->tran[i].progname);
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
			 info->tran[i].username, info->tran[i].hostname,
			 info->tran[i].process_id, info->tran[i].progname);
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
		  if (thread_kill_tran_index
		      (info->tran[i].tran_index, info->tran[i].username,
		       info->tran[i].hostname,
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
			       info->tran[i].username, info->tran[i].hostname,
			       info->tran[i].process_id,
			       info->tran[i].progname);
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
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  const char *database_name;
  int kill_tran_index;
  const char *kill_progname;
  const char *kill_user;
  const char *kill_host;
  const char *dba_password;
  bool dump_trantab_flag;
  bool verify = true;
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
  kill_tran_index =
    utility_get_option_int_value (arg_map, KILLTRAN_KILL_TRANSACTION_INDEX_S);
  kill_user =
    utility_get_option_string_value (arg_map, KILLTRAN_KILL_USER_NAME_S, 0);
  kill_host =
    utility_get_option_string_value (arg_map, KILLTRAN_KILL_HOST_NAME_S, 0);
  kill_progname =
    utility_get_option_string_value (arg_map, KILLTRAN_KILL_PROGRAM_NAME_S,
				     0);
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
  if (strlen (kill_user) != 0)
    isbatch++;
  if (strlen (kill_host) != 0)
    isbatch++;
  if (strlen (kill_progname) != 0)
    isbatch++;
  if (isbatch > 1)
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_KILLTRAN,
				       KILLTRAN_MSG_MANY_ARGS));
      goto error_exit;
    }

  if (db_login ("dba", dba_password) != NO_ERROR)
    {
      fprintf (stderr, "%s\n", db_error_string (3));
      goto error_exit;
    }

  /* first try to restart with the password given (possibly none) */
  error = db_restart (arg->argv0, TRUE, database_name);
  if (error)
    {
      if (error == ER_AU_INVALID_PASSWORD && strlen (dba_password) == 0)
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
	    error = db_restart (arg->argv0, TRUE, database_name);
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
   * are new transactions statring and finishing. We need to do this way
   * since verification is required at this level, and we cannot freeze the
   * state of the server ()transactin table).
   */

  if ((info = get_trantb ()) == NULL)
    {
      db_shutdown ();
      goto error_exit;
    }

  if (dump_trantab_flag ||
      (kill_tran_index == -1 && strlen (kill_user) == 0 &&
       strlen (kill_host) == 0 && strlen (kill_progname) == 0))
    {
      dump_trantb (info);
    }
  else
    {
      /* some piece of transaction identifer was entered, try to use it */
      if (kill_transactions (info, kill_tran_index, kill_user, kill_host,
			     kill_progname, verify) <= 0)
	{
	  db_shutdown ();
	  goto error_exit;
	}
    }

  if (info)
    free_trantb (info);


  (void) db_shutdown ();
  return EXIT_SUCCESS;

print_killtran_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_KILLTRAN,
				   KILLTRAN_MSG_USAGE),
	   basename (arg->argv0));
error_exit:
  if (info)
    free_trantb (info);

  return EXIT_FAILURE;
}

/*
 * plandump() - plandump main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
plandump (UTIL_FUNCTION_ARG * arg)
{
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  const char *database_name;
  const char *output_file = NULL;
  bool drop_flag = false;
  FILE *outfp = NULL;

  database_name =
    utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
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

  /* should have little copyright herald message ? */
  AU_DISABLE_PASSWORDS ();
  db_login ("dba", NULL);

  if (db_restart (arg->argv0, TRUE, database_name) != NO_ERROR)
    {
      fprintf (stderr, "%s\n", db_error_string (3));
      goto error_exit;
    }

  (void) db_set_isolation (TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE);
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
}

/*
 * sig_interrupt() - 
 *   return: none
 *   sig_no(in)
 */
static void
sig_interrupt (int sig_no)
{
  db_set_interrupt (1);
}
