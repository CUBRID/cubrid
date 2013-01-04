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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 */

/*
 * migrate.c :
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>

#include "dbi.h"
#include "porting.h"
#include "utility.h"
#include "databases_file.h"
#include "boot_sr.h"
#include "log_impl.h"
#include "btree.h"

#define V9_0_LEVEL (9.0f)
#define V9_1_LEVEL (9.1f)

#define FOPEN_AND_CHECK(fp, filename, mode) \
do { \
  (fp) = fopen ((filename), (mode)); \
  if ((fp) == NULL) \
    { \
      printf ("file open error: %s, %d\n", (filename), errno); \
      return ER_FAILED; \
    } \
} while (0)

#define FSEEK_AND_CHECK(fp, size, origin, filename) \
do { \
  if (fseek ((fp), (size), (origin)) < 0) \
    { \
      printf ("file seek error: %s, %d\n", (filename), errno); \
      fclose ((fp)); \
      return ER_FAILED; \
    } \
} while (0)

#define FREAD_AND_CHECK(ptr, size, n, fp, filename) \
do { \
  fread ((ptr), (size), (n), (fp)); \
  if (ferror ((fp)) != 0) \
    { \
      printf ("file fread error: %s, %d\n", (filename), errno); \
      fclose ((fp)); \
      return ER_FAILED; \
    } \
} while (0)

#define FWRITE_AND_CHECK(ptr, size, n, fp, filename) \
do { \
  fwrite ((ptr), (size), (n), (fp)); \
  if (ferror ((fp)) != 0) \
    { \
      printf ("file fwrite error: %s, %d\n", (filename), errno); \
      fclose ((fp)); \
      return ER_FAILED; \
    } \
} while (0)

#define FFLUSH_AND_CHECK(fp, filename) \
do { \
  if (fflush ((fp)) != 0) \
    { \
      printf ("file fflush error: %s, %d\n", (filename), errno); \
      fclose ((fp)); \
      return ER_FAILED; \
    } \
} while (0)

#define FCLOSE_AND_CHECK(fp, filename) \
do { \
  if (fclose ((fp)) != 0) \
    { \
      printf ("file fclose error: %s, %d\n", (filename), errno); \
      return ER_FAILED; \
    } \
} while (0)

extern int btree_fix_overflow_oid_page_all_btrees (void);

static int get_active_log_vol_path (const char *db_path, char *logvol_path);
static int check_and_fix_compat_level (const char *db_name,
				       const char *vol_path);
static int get_db_path (const char *db_name, char *db_full_path);

static int
get_active_log_vol_path (const char *db_path, char *logvol_path)
{
  char vol_info_path[PATH_MAX], vol_path[PATH_MAX];
  FILE *vol_info_fp = NULL;
  int volid = NULL_VOLID;
  char scan_format[32];

  fileio_make_volume_info_name (vol_info_path, db_path);

  FOPEN_AND_CHECK (vol_info_fp, vol_info_path, "r");

  sprintf (scan_format, "%%d %%%ds", (int) (sizeof (vol_path) - 1));
  while (true)
    {
      if (fscanf (vol_info_fp, scan_format, &volid, vol_path) != 2)
	{
	  break;
	}

      if (volid != LOG_DBLOG_ACTIVE_VOLID)
	{
	  continue;
	}

      strcpy (logvol_path, vol_path);
      break;
    }

  FCLOSE_AND_CHECK (vol_info_fp, vol_info_path);
  return NO_ERROR;
}

static int
check_and_fix_compat_level (const char *db_name, const char *db_path)
{
  FILE *fp = NULL;
  char vol_path[PATH_MAX];
  LOG_HEADER *hdr;
  LOG_PAGE *hdr_page;
  char log_io_page[IO_MAX_PAGE_SIZE];

  if (get_active_log_vol_path (db_path, vol_path) != NO_ERROR)
    {
      printf ("Can't found log active volume path.\n");
      return ER_FAILED;
    }

  hdr_page = (LOG_PAGE *) log_io_page;
  hdr = (struct log_header *) hdr_page->area;

  FOPEN_AND_CHECK (fp, vol_path, "rb+");
  FREAD_AND_CHECK (log_io_page, sizeof (char), LOG_PAGESIZE, fp, vol_path);

  if (hdr->db_compatibility == V9_1_LEVEL)
    {
      printf ("This database (%s) is already updated.\n", db_name);
      return ER_FAILED;
    }

  if (hdr->db_compatibility != V9_0_LEVEL)
    {
      printf ("Cannot migrate this database: "
	      "%s is not CUBRID 9.0 BETA database.\n", db_name);
      return ER_FAILED;
    }

  hdr->db_compatibility = rel_disk_compatible ();

  rewind (fp);
  FWRITE_AND_CHECK (log_io_page, sizeof (char), LOG_PAGESIZE, fp, vol_path);
  FFLUSH_AND_CHECK (fp, vol_path);
  FCLOSE_AND_CHECK (fp, vol_path);

  return NO_ERROR;
}

static int
undo_fix_compat_level (const char *db_path)
{
  FILE *fp = NULL;
  char vol_path[PATH_MAX];
  LOG_HEADER *hdr;
  LOG_PAGE *hdr_page;
  char log_io_page[IO_MAX_PAGE_SIZE];

  if (get_active_log_vol_path (db_path, vol_path) != NO_ERROR)
    {
      printf ("Can't found log active volume path.\n");
      return ER_FAILED;
    }

  hdr_page = (LOG_PAGE *) log_io_page;
  hdr = (struct log_header *) hdr_page->area;

  FOPEN_AND_CHECK (fp, vol_path, "rb+");
  FREAD_AND_CHECK (log_io_page, sizeof (char), LOG_PAGESIZE, fp, vol_path);

  hdr->db_compatibility = V9_0_LEVEL;

  rewind (fp);
  FWRITE_AND_CHECK (log_io_page, sizeof (char), LOG_PAGESIZE, fp, vol_path);
  FFLUSH_AND_CHECK (fp, vol_path);
  FCLOSE_AND_CHECK (fp, vol_path);

  return NO_ERROR;
}

static int
get_db_path (const char *db_name, char *db_full_path)
{
  DB_INFO *dir = NULL;
  DB_INFO *db = NULL;

  if (cfg_read_directory (&dir, false) != NO_ERROR)
    {
      printf ("Can't found databases.txt.\n");
      return ER_FAILED;
    }

  db = cfg_find_db_list (dir, db_name);

  if (db == NULL)
    {
      if (dir)
	{
	  cfg_free_directory (dir);
	}

      printf ("Unknown database: %s\n", db_name);
      return ER_FAILED;
    }

  COMPOSE_FULL_NAME (db_full_path, PATH_MAX, db->pathname, db_name);

  return NO_ERROR;
}

#if defined(WINDOWS)
static BOOL WINAPI
#else
static void
#endif
intr_handler (int sig_no)
{
  /* do nothing */
#if defined(WINDOWS)
  if (sig_no == CTRL_C_EVENT)
    {
      /* ignore */
      return TRUE;
    }

  return FALSE;
#endif /* WINDOWS */
}

int
main (int argc, char *argv[])
{
  const char *db_name;
  char db_full_path[PATH_MAX];

  if (argc != 2)
    {
      printf ("Usage: %s <database name>\n", argv[0]);
      return EXIT_FAILURE;
    }

#if defined(WINDOWS)
  SetConsoleCtrlHandler ((PHANDLER_ROUTINE) intr_handler, TRUE);
#else
  os_set_signal_handler (SIGINT, intr_handler);
#endif

  db_name = argv[1];

  printf ("CUBRID Migration: 9.0 BETA to 9.1\n\n");

  if (rel_disk_compatible () != V9_1_LEVEL)
    {
      /* invalid cubrid library */
      printf ("CUBRID library version is invalid.\n"
	      "Please upgrade to CUBRID 9.1 and retry migrate.\n");
      return EXIT_FAILURE;
    }

  if (check_database_name (db_name))
    {
      return EXIT_FAILURE;
    }

  if (get_db_path (db_name, db_full_path) != NO_ERROR)
    {
      return EXIT_FAILURE;
    }

  if (check_and_fix_compat_level (db_name, db_full_path) != NO_ERROR)
    {
      return EXIT_FAILURE;
    }

  AU_DISABLE_PASSWORDS ();

  (void) boot_set_skip_check_ct_classes (true);
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("DBA", NULL);

  if (db_restart (argv[0], TRUE, db_name) != NO_ERROR)
    {
      printf ("%s\n", db_error_string (3));

      if (undo_fix_compat_level (db_full_path) != NO_ERROR)
	{
	  printf ("\nRecovering db_compatibility level fails.\n");
	}

      printf ("\nMigration failed.\n");
      return EXIT_FAILURE;
    }

  printf ("Start to fix BTREE Overflow OID pages\n\n");

  if (btree_fix_overflow_oid_page_all_btrees () != NO_ERROR)
    {
      db_abort_transaction ();
      db_shutdown ();

      if (undo_fix_compat_level (db_full_path) != NO_ERROR)
	{
	  printf ("\nRecovering db_compatibility level fails.\n");
	}

      printf ("\nMigration failed.\n");
      return EXIT_FAILURE;
    }

  db_commit_transaction ();
  db_shutdown ();

  printf ("\nMigration to CUBRID 9.1 has been completed successfully.\n");

  return NO_ERROR;
}
