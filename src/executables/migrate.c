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
#include "transform.h"
#include "message_catalog.h"
#include "error_manager.h"
#include "system_parameter.h"

#define V9_1_LEVEL (9.1f)
#define V9_2_LEVEL (9.2f)

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

#define UNDO_LIST_SIZE (32)

typedef struct r91_disk_var_header R91_DISK_VAR_HEADER;
struct r91_disk_var_header
{
  char magic[CUBRID_MAGIC_MAX_LENGTH];
  INT16 iopagesize;
  INT16 volid;
  DISK_VOLPURPOSE purpose;
  INT32 sect_npgs;
  INT32 total_sects;
  INT32 free_sects;
  INT32 hint_allocsect;
  INT32 total_pages;
  INT32 free_pages;
  INT32 sect_alloctb_npages;
  INT32 page_alloctb_npages;
  INT32 sect_alloctb_page1;
  INT32 page_alloctb_page1;
  INT32 sys_lastpage;
  INT64 db_creation;
  INT32 max_npages;
  INT32 dummy;
  LOG_LSA chkpt_lsa;
  HFID boot_hfid;
  INT16 offset_to_vol_fullname;
  INT16 offset_to_next_vol_fullname;
  INT16 offset_to_vol_remarks;
  char var_fields[1];
};

typedef struct
{
  char *filename;
  char *page;
  int page_size;
} VOLUME_UNDO_INFO;

static int vol_undo_list_length;
static int vol_undo_count;
static VOLUME_UNDO_INFO *vol_undo_info;

static int fix_all_volume_header (const char *db_path);
static int fix_volume_header (const char *vol_path);
static int undo_fix_volume_header (const char *vol_path, char *undo_page,
				   int size);
static char *make_volume_header_undo_page (const char *vol_path, int size);
static void free_volume_header_undo_list (void);

static int get_active_log_vol_path (const char *db_path, char *logvol_path);
static int check_and_fix_compat_level (const char *db_name,
				       const char *vol_path);
static int get_db_path (const char *db_name, char *db_full_path);
static int fix_codeset_in_active_log (const char *db_path,
				      INTL_CODESET codeset);

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
fix_codeset_in_active_log (const char *db_path, INTL_CODESET codeset)
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

  hdr->db_charset = codeset;

  rewind (fp);
  FWRITE_AND_CHECK (log_io_page, sizeof (char), LOG_PAGESIZE, fp, vol_path);
  FFLUSH_AND_CHECK (fp, vol_path);
  FCLOSE_AND_CHECK (fp, vol_path);

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

  if (hdr->is_shutdown == false)
    {
      printf ("This database (%s) was not normally terminated.\n"
	      "Please start and shutdown with CUBRID 9.1 ,"
	      "and retry migration.\n", db_name);
      return ER_FAILED;
    }

  if (hdr->db_compatibility == V9_2_LEVEL)
    {
      printf ("This database (%s) is already updated.\n", db_name);
      return ER_FAILED;
    }

  if (hdr->db_compatibility != V9_1_LEVEL)
    {
      printf ("Cannot migrate this database: "
	      "%s is not CUBRID 9.1 database.\n", db_name);
      return ER_FAILED;
    }

  hdr->db_compatibility = rel_disk_compatible ();
  hdr->db_charset = INTL_CODESET_NONE;

  rewind (fp);
  FWRITE_AND_CHECK (log_io_page, sizeof (char), hdr->db_logpagesize, fp,
		    vol_path);
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

  hdr->db_compatibility = V9_1_LEVEL;

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

static int
get_codeset_from_db_root (void)
{
#define QUERY_SIZE 1024

  DB_QUERY_RESULT *query_result;
  DB_QUERY_ERROR query_error;
  int db_status;
  char query[QUERY_SIZE];
  DB_VALUE value;

  snprintf (query, QUERY_SIZE, "SELECT [charset] FROM [db_root]");
  db_status = db_execute (query, &query_result, &query_error);
  if (db_status < 0)
    {
      return db_status;
    }

  db_status = db_query_next_tuple (query_result);
  if (db_status == DB_CURSOR_SUCCESS)
    {
      db_status = db_query_get_tuple_value (query_result, 0, &value);
      if (db_status != NO_ERROR)
	{
	  db_query_end (query_result);
	  return db_status;
	}
    }
  else if (db_status == DB_CURSOR_END)
    {
      return ER_FAILED;
    }
  else
    {
      return db_status;
    }

  db_query_end (query_result);
  return db_get_int (&value);
}


int
main (int argc, char *argv[])
{
  const char *db_name;
  char db_full_path[PATH_MAX];
  int coll_need_manual_migr = 0;
  INTL_CODESET codeset;
  int i;
  VOLUME_UNDO_INFO *p;

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

  printf ("CUBRID Migration: 9.1 to 9.2\n\n");

  if (rel_disk_compatible () != V9_2_LEVEL)
    {
      /* invalid cubrid library */
      printf ("CUBRID library version is invalid.\n"
	      "Please upgrade to CUBRID 9.2 and retry migrate.\n");
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

  if (utility_initialize () != NO_ERROR)
    {
      goto error_undo_compat;
    }

  if (er_init ("./migrate_91_to_92.err", ER_NEVER_EXIT) != NO_ERROR)
    {
      printf ("Failed to initialize error manager.\n");
      goto error_undo_compat;
    }

  if (fix_all_volume_header (db_full_path) != NO_ERROR)
    {
      goto error_undo_vol_header;
    }

  sysprm_set_force (prm_get_name (PRM_ID_PB_NBUFFERS), "1024");
  sysprm_set_force (prm_get_name (PRM_ID_JAVA_STORED_PROCEDURE), "no");

  AU_DISABLE_PASSWORDS ();

  /* boot_set_skip_check_ct_classes (true); */
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("DBA", NULL);

  if (db_restart (argv[0], TRUE, db_name) != NO_ERROR)
    {
      printf ("\n%s\n", db_error_string (3));
      goto error_undo_vol_header;
    }

  au_disable ();

  if (file_update_used_pages_of_vol_header (NULL) == DISK_ERROR)
    {
      printf ("Could not update used pages of volume header : %s.\n",
	      db_error_string (3));
      db_shutdown ();
      goto error_undo_vol_header;
    }

  /* read codeset from db_root */
  codeset = get_codeset_from_db_root ();
  db_shutdown ();

  if (codeset >= INTL_CODESET_ISO88591 && codeset <= INTL_CODESET_LAST)
    {
      /* write codeset to the header of an active log */
      if (fix_codeset_in_active_log (db_full_path, codeset) != NO_ERROR)
	{
	  goto error_undo_vol_header;
	}
    }
  else
    {
      printf ("\nCould not get the codeset from db_root: %s",
	      db_error_string (3));
      goto error_undo_vol_header;
    }

  printf ("\nMigration to CUBRID 9.2 has been completed successfully.\n");

  free_volume_header_undo_list ();

  return NO_ERROR;

error_undo_vol_header:
  for (p = vol_undo_info, i = 0; i < vol_undo_count; i++, p++)
    {
      printf ("rollback volume header: %s\n", p->filename);
      fflush (stdout);

      if (undo_fix_volume_header (p->filename, p->page,
				  p->page_size) != NO_ERROR)
	{
	  printf ("recovering volume header fails.\n");
	  break;
	}
    }

  free_volume_header_undo_list ();

error_undo_compat:
  if (undo_fix_compat_level (db_full_path) != NO_ERROR)
    {
      printf ("\nRecovering db_compatibility level fails.\n");
    }

  printf ("\nMigration failed.\n");
  return EXIT_FAILURE;
}


static int
fix_all_volume_header (const char *db_path)
{
  char vol_info_path[PATH_MAX], vol_path[PATH_MAX];
  FILE *vol_info_fp = NULL;
  int volid = NULL_VOLID;
  char scan_format[32];

  fileio_make_volume_info_name (vol_info_path, db_path);

  FOPEN_AND_CHECK (vol_info_fp, vol_info_path, "r");

  vol_undo_count = 0;
  vol_undo_list_length = UNDO_LIST_SIZE;
  vol_undo_info = (VOLUME_UNDO_INFO *) calloc (vol_undo_list_length,
					       sizeof (VOLUME_UNDO_INFO));

  if (vol_undo_info == NULL)
    {
      fclose (vol_info_fp);
      return ER_FAILED;
    }

  sprintf (scan_format, "%%d %%%ds", (int) (sizeof (vol_path) - 1));
  while (true)
    {
      if (fscanf (vol_info_fp, scan_format, &volid, vol_path) != 2)
	{
	  break;
	}

      if (volid == NULL_VOLID || volid < LOG_DBLOG_ACTIVE_VOLID)
	{
	  continue;
	}

      if (volid != LOG_DBLOG_ACTIVE_VOLID)
	{
	  if (fix_volume_header (vol_path) != NO_ERROR)
	    {
	      fclose (vol_info_fp);
	      return ER_FAILED;
	    }
	}
    }


  return NO_ERROR;
}

static int
fix_volume_header (const char *vol_path)
{
  FILE *fp = NULL;
  DISK_VAR_HEADER *r92_header;
  R91_DISK_VAR_HEADER *r91_header;
  char *undo_page;

  FILEIO_PAGE *r91_iopage, *r92_iopage;
  char r91_page[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  char r92_page[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  char *r91_aligned_buf, *r92_aligned_buf;
  int var_field_length;

  r91_aligned_buf = PTR_ALIGN (r91_page, MAX_ALIGNMENT);
  r92_aligned_buf = PTR_ALIGN (r92_page, MAX_ALIGNMENT);

  FOPEN_AND_CHECK (fp, vol_path, "rb+");
  FREAD_AND_CHECK (r91_aligned_buf, sizeof (char), IO_PAGESIZE, fp, vol_path);

  undo_page = make_volume_header_undo_page (vol_path, IO_PAGESIZE);

  if (undo_page == NULL)
    {
      FCLOSE_AND_CHECK (fp, vol_path);
      return ER_FAILED;
    }

  memcpy (undo_page, r91_aligned_buf, IO_PAGESIZE);

  r91_iopage = (FILEIO_PAGE *) r91_aligned_buf;
  r91_header = (R91_DISK_VAR_HEADER *) r91_iopage->page;

  r92_iopage = (FILEIO_PAGE *) r92_aligned_buf;
  r92_header = (DISK_VAR_HEADER *) r92_iopage->page;

  LSA_COPY (&r92_iopage->prv.lsa, &r91_iopage->prv.lsa);

  memcpy (r92_header->magic, r91_header->magic, CUBRID_MAGIC_MAX_LENGTH);
  r92_header->iopagesize = r91_header->iopagesize;
  r92_header->volid = r91_header->volid;
  r92_header->purpose = r91_header->purpose;
  r92_header->sect_npgs = r91_header->sect_npgs;
  r92_header->total_sects = r91_header->total_sects;
  r92_header->free_sects = r91_header->free_sects;
  r92_header->hint_allocsect = r91_header->hint_allocsect;
  r92_header->total_pages = r91_header->total_pages;
  r92_header->free_pages = r91_header->free_pages;
  r92_header->sect_alloctb_npages = r91_header->sect_alloctb_npages;
  r92_header->page_alloctb_npages = r91_header->page_alloctb_npages;
  r92_header->sect_alloctb_page1 = r91_header->sect_alloctb_page1;
  r92_header->page_alloctb_page1 = r91_header->page_alloctb_page1;
  r92_header->sys_lastpage = r91_header->sys_lastpage;
  r92_header->db_creation = r91_header->db_creation;
  r92_header->max_npages = r91_header->total_pages;
  LSA_COPY (&r92_header->chkpt_lsa, &r91_header->chkpt_lsa);
  HFID_COPY (&r92_header->boot_hfid, &r91_header->boot_hfid);
  r92_header->offset_to_vol_fullname = r91_header->offset_to_vol_fullname;
  r92_header->offset_to_next_vol_fullname =
    r91_header->offset_to_next_vol_fullname;
  r92_header->offset_to_vol_remarks = r91_header->offset_to_vol_remarks;

  var_field_length = r91_header->offset_to_vol_remarks +
    (int) strlen ((char *) (r91_header->var_fields +
			    r91_header->offset_to_vol_remarks));

  memcpy (r92_header->var_fields, r91_header->var_fields, var_field_length);

  /* This will be fixed later */
  r92_header->used_data_npages = r92_header->used_index_npages = 0;

  rewind (fp);
  FWRITE_AND_CHECK (r92_aligned_buf, sizeof (char), r92_header->iopagesize,
		    fp, vol_path);
  FFLUSH_AND_CHECK (fp, vol_path);
  FCLOSE_AND_CHECK (fp, vol_path);

  printf ("%s... done\n", vol_path);
  fflush (stdout);

  return NO_ERROR;
}

static char *
make_volume_header_undo_page (const char *vol_path, int size)
{
  VOLUME_UNDO_INFO *new_undo_info;
  char *undo_page, *undo_file_name;

  if (vol_undo_count == vol_undo_list_length)
    {
      new_undo_info =
	(VOLUME_UNDO_INFO *) calloc (2 * vol_undo_list_length,
				     sizeof (VOLUME_UNDO_INFO));
      if (new_undo_info == NULL)
	{
	  return NULL;
	}

      memcpy (new_undo_info, vol_undo_info,
	      vol_undo_list_length * sizeof (VOLUME_UNDO_INFO));
      free (vol_undo_info);
      vol_undo_info = new_undo_info;

      vol_undo_list_length *= 2;
    }

  undo_page = (char *) malloc (size * sizeof (char));
  if (undo_page == NULL)
    {
      return NULL;
    }

  undo_file_name = (char *) malloc (PATH_MAX);
  if (undo_file_name == NULL)
    {
      free (undo_page);
      return NULL;
    }


  strcpy (undo_file_name, vol_path);
  vol_undo_info[vol_undo_count].filename = undo_file_name;
  vol_undo_info[vol_undo_count].page = undo_page;
  vol_undo_info[vol_undo_count].page_size = size;
  vol_undo_count++;

  return undo_page;
}

static int
undo_fix_volume_header (const char *vol_path, char *undo_page, int size)
{
  FILE *fp = NULL;

  FOPEN_AND_CHECK (fp, vol_path, "rb+");
  FWRITE_AND_CHECK (undo_page, sizeof (char), size, fp, vol_path);
  FFLUSH_AND_CHECK (fp, vol_path);
  FCLOSE_AND_CHECK (fp, vol_path);

  return NO_ERROR;
}

static void
free_volume_header_undo_list (void)
{
  int i;
  VOLUME_UNDO_INFO *p;

  for (p = vol_undo_info, i = 0; i < vol_undo_count; i++, p++)
    {
      if (p->filename)
	{
	  free (p->filename);
	}

      if (p->page)
	{
	  free (p->page);
	}
    }

  if (vol_undo_info)
    {
      free (vol_undo_info);
    }
}
