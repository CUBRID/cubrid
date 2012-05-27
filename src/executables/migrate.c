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

#include "porting.h"
#include "utility.h"
#include "db.h"
#include "dbi.h"
#include "error_manager.h"
#include "message_catalog.h"
#include "system_parameter.h"
#include "databases_file.h"
#include "authenticate.h"
#include "execute_schema.h"
#include "page_buffer.h"
#include "disk_manager.h"
#include "file_manager.h"
#include "transform.h"
#include "large_object_directory.h"
#include "locator.h"
#include "locator_cl.h"
#include "schema_manager.h"
#include "log_manager.h"
#include "boot_sr.h"

#define R20_LEVEL (8.2f)
#define R22_LEVEL (8.3f)
#define R30_LEVEL (8.301f)
#define R40_BETA_LEVEL (8.4f)
#define R40_GA_LEVEL (8.41f)

#define UNDO_LIST_SIZE (32)

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

typedef struct old_log_hdr_bkup_level_info OLD_LOG_HDR_BKUP_LEVEL_INFO;
struct old_log_hdr_bkup_level_info
{
  INT32 bkup_attime;
  INT32 io_baseln_time;
  INT32 io_bkuptime;
  int ndirty_pages_post_bkup;
  int io_numpages;
};

struct old_log_header
{
  char magic[CUBRID_MAGIC_MAX_LENGTH];
  INT32 db_creation;
  char db_release[REL_MAX_RELEASE_LENGTH];
  float db_compatibility;
  PGLENGTH db_iopagesize;
  int is_shutdown;
  TRANID next_trid;
  int avg_ntrans;
  int avg_nlocks;
  DKNPAGES npages;
  PAGEID fpageid;
  LOG_LSA append_lsa;
  LOG_LSA chkpt_lsa;
  PAGEID nxarv_pageid;
  PAGEID nxarv_phy_pageid;
  int nxarv_num;
  int last_arv_num_for_syscrashes;
  int last_deleted_arv_num;
  bool has_logging_been_skipped;
  LOG_LSA bkup_level0_lsa;
  LOG_LSA bkup_level1_lsa;
  LOG_LSA bkup_level2_lsa;
  char prefix_name[MAXLOGNAME];
  int lowest_arv_num_for_backup;
  int highest_arv_num_for_backup;
  int perm_status;
  OLD_LOG_HDR_BKUP_LEVEL_INFO bkinfo[FILEIO_BACKUP_UNDEFINED_LEVEL];
};

typedef struct r40_beta_log_lsa R40_BETA_LOG_LSA;
struct r40_beta_log_lsa
{
  int pageid;
  short offset;
};

typedef struct r40_beta_fileio_page R40_BETA_FILEIO_PAGE;
struct r40_beta_fileio_page
{
  R40_BETA_LOG_LSA lsa;
  char page[1];
};

typedef struct r40_beta_disk_var_header R40_BETA_DISK_VAR_HEADER;
struct r40_beta_disk_var_header
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
  INT32 warnat;
  R40_BETA_LOG_LSA chkpt_lsa;
  HFID boot_hfid;
  INT16 offset_to_vol_fullname;
  INT16 offset_to_next_vol_fullname;
  INT16 offset_to_vol_remarks;
  char var_fields[1];
};

typedef struct r40_beta_log_page_header R40_BETA_LOG_PAGE_HEADER;
struct r40_beta_log_page_header
{
  int logical_pageid;
  short offset;
};

typedef struct r40_beta_log_page R40_BETA_LOG_PAGE;
struct r40_beta_log_page
{
  R40_BETA_LOG_PAGE_HEADER hdr;
  char page[1];
};

typedef struct r40_beta_log_header R40_BETA_LOG_HEADER;
struct r40_beta_log_header
{
  char magic[CUBRID_MAGIC_MAX_LENGTH];
  INT32 dummy;
  INT64 db_creation;
  char db_release[REL_MAX_RELEASE_LENGTH];
  float db_compatibility;
  PGLENGTH db_iopagesize;
  PGLENGTH db_logpagesize;
  int is_shutdown;
  TRANID next_trid;
  int avg_ntrans;
  int avg_nlocks;
  DKNPAGES npages;
  PAGEID fpageid;
  R40_BETA_LOG_LSA append_lsa;
  R40_BETA_LOG_LSA chkpt_lsa;
  PAGEID nxarv_pageid;
  PAGEID nxarv_phy_pageid;
  int nxarv_num;
  int last_arv_num_for_syscrashes;
  int last_deleted_arv_num;
  bool has_logging_been_skipped;
  R40_BETA_LOG_LSA bkup_level0_lsa;
  R40_BETA_LOG_LSA bkup_level1_lsa;
  R40_BETA_LOG_LSA bkup_level2_lsa;
  char prefix_name[MAXLOGNAME];
  int lowest_arv_num_for_backup;
  int highest_arv_num_for_backup;
  int perm_status;
  LOG_HDR_BKUP_LEVEL_INFO bkinfo[FILEIO_BACKUP_UNDEFINED_LEVEL];
  int ha_server_state;
  int ha_file_status;
  R40_BETA_LOG_LSA eof_lsa;
  R40_BETA_LOG_LSA smallest_lsa_at_last_chkpt;
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
static DKNPAGES log_Npage;

/* functions to fix volume header page */
static int fix_volume_and_log_header (const char *db_path);
static int fix_volume_header (const char *vol_path);
static int fix_log_header (const char *vol_path);

/* undo functions */
static char *make_volume_header_undo_page (const char *vol_path, int size);
static int undo_fix_volume_and_log_header (void);
static int undo_fix_volume_header (const char *vol_path, char *undo_page,
				   int size);
static void free_volume_header_undo_list (void);

/* functions to recreate log and reset data page header */
static int recreate_log_and_reset_lsa (const char *db_name);

/* to fix system catalog  */
static int fix_system_class (void);
static int fix_db_ha_apply_info (void);
static int new_db_ha_apply_info (void);

static int get_db_path (const char *db_name, char *db_full_path);

static int
fix_volume_header (const char *vol_path)
{
  FILE *fp = NULL;
  R40_BETA_DISK_VAR_HEADER *r40_beta_header;
  DISK_VAR_HEADER *r40_ga_header;
  R40_BETA_FILEIO_PAGE *beta_iopage;
  FILEIO_PAGE *ga_iopage;
  char beta_page[IO_MAX_PAGE_SIZE];
  char ga_page[IO_MAX_PAGE_SIZE];
  char *undo_page;
  int var_field_length;

  FOPEN_AND_CHECK (fp, vol_path, "rb+");
  FREAD_AND_CHECK (beta_page, sizeof (char), IO_PAGESIZE, fp, vol_path);

  undo_page = make_volume_header_undo_page (vol_path, IO_PAGESIZE);
  if (undo_page == NULL)
    {
      FCLOSE_AND_CHECK (fp, vol_path);
      return ER_FAILED;
    }

  memcpy (undo_page, beta_page, IO_PAGESIZE);

  beta_iopage = (R40_BETA_FILEIO_PAGE *) beta_page;
  r40_beta_header = (R40_BETA_DISK_VAR_HEADER *) beta_iopage->page;

  ga_iopage = (FILEIO_PAGE *) ga_page;
  r40_ga_header = (DISK_VAR_HEADER *) ga_iopage->page;

  ga_iopage->prv.lsa.pageid = beta_iopage->lsa.pageid;
  ga_iopage->prv.lsa.offset = beta_iopage->lsa.offset;

  memcpy (r40_ga_header->magic, r40_beta_header->magic,
	  CUBRID_MAGIC_MAX_LENGTH);
  r40_ga_header->iopagesize = r40_beta_header->iopagesize;
  r40_ga_header->volid = r40_beta_header->volid;
  r40_ga_header->purpose = r40_beta_header->purpose;
  r40_ga_header->sect_npgs = r40_beta_header->sect_npgs;
  r40_ga_header->total_sects = r40_beta_header->total_sects;
  r40_ga_header->free_sects = r40_beta_header->free_sects;
  r40_ga_header->hint_allocsect = r40_beta_header->hint_allocsect;
  r40_ga_header->total_pages = r40_beta_header->total_pages;
  r40_ga_header->free_pages = r40_beta_header->free_pages;

  r40_ga_header->sect_alloctb_npages = r40_beta_header->sect_alloctb_npages;
  r40_ga_header->page_alloctb_npages = r40_beta_header->page_alloctb_npages;
  r40_ga_header->sect_alloctb_page1 = r40_beta_header->sect_alloctb_page1;
  r40_ga_header->page_alloctb_page1 = r40_beta_header->page_alloctb_page1;
  r40_ga_header->sys_lastpage = r40_beta_header->sys_lastpage;
  r40_ga_header->db_creation = r40_beta_header->db_creation;
  r40_ga_header->warnat = r40_beta_header->warnat;
  LSA_SET_NULL (&r40_ga_header->chkpt_lsa);
  r40_ga_header->boot_hfid = r40_beta_header->boot_hfid;
  r40_ga_header->offset_to_vol_fullname =
    r40_beta_header->offset_to_vol_fullname;
  r40_ga_header->offset_to_next_vol_fullname =
    r40_beta_header->offset_to_next_vol_fullname;
  r40_ga_header->offset_to_vol_remarks =
    r40_beta_header->offset_to_vol_remarks;

  var_field_length = r40_beta_header->offset_to_vol_remarks +
    (int) strlen ((char *) (r40_beta_header->var_fields +
			    r40_beta_header->offset_to_vol_remarks));

  memcpy (r40_ga_header->var_fields, r40_beta_header->var_fields,
	  var_field_length);

  rewind (fp);
  FWRITE_AND_CHECK (ga_page, sizeof (char), IO_PAGESIZE, fp, vol_path);
  FFLUSH_AND_CHECK (fp, vol_path);
  FCLOSE_AND_CHECK (fp, vol_path);

  printf ("%s... done\n", vol_path);
  fflush (stdout);

  return NO_ERROR;
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

static int
get_disk_compatibility_level (const char *db_path,
			      float *compat_level, char *release_string)
{
  char vol_info_path[PATH_MAX], vol_path[PATH_MAX];
  FILE *vol_info_fp, *log_active_fp;
  int volid = NULL_VOLID;

  R40_BETA_LOG_HEADER *r40_beta_header;
  R40_BETA_LOG_PAGE *beta_iopage;
  struct old_log_header *old_header;

  LOG_PAGE *ga_iopage;
  struct log_header *r40_ga_header;

  char page[IO_MAX_PAGE_SIZE];
  char scan_format[32];

  fileio_make_volume_info_name (vol_info_path, db_path);

  FOPEN_AND_CHECK (vol_info_fp, vol_info_path, "r");

  sprintf (scan_format, "%%d %%%ds", (int) (sizeof (vol_path) - 1));
  while (true)
    {
      if (fscanf (vol_info_fp, scan_format, &volid, vol_path) != 2)
	{
	  printf ("active log volume infomation is not found.\n");
	  fclose (vol_info_fp);
	  return ER_FAILED;
	}

      if (volid == LOG_DBLOG_ACTIVE_VOLID)
	{
	  break;
	}
    }

  FCLOSE_AND_CHECK (vol_info_fp, vol_info_path);

  FOPEN_AND_CHECK (log_active_fp, vol_path, "r");
  FREAD_AND_CHECK (page, sizeof (char), IO_PAGESIZE, log_active_fp, vol_path);

  beta_iopage = (R40_BETA_LOG_PAGE *) page;
  r40_beta_header = (R40_BETA_LOG_HEADER *) beta_iopage->page;
  old_header = (struct old_log_header *) beta_iopage->page;

  ga_iopage = (LOG_PAGE *) page;
  r40_ga_header = (struct log_header *) ga_iopage->area;

  if (r40_ga_header->db_compatibility == rel_disk_compatible ())
    {
      *compat_level = r40_ga_header->db_compatibility;
      strcpy (release_string, r40_ga_header->db_release);
    }
  else if (r40_beta_header->db_compatibility > 8.0f
	   && r40_beta_header->db_compatibility <= rel_disk_compatible ())
    {
      *compat_level = r40_beta_header->db_compatibility;
      strcpy (release_string, r40_beta_header->db_release);

      if (r40_beta_header->db_iopagesize < IO_MIN_PAGE_SIZE)
	{
	  printf ("Data page size(%d) is too small.\n"
		  "The minimum data page size of 2008 R4.0 is %d.\n"
		  "You must use cubrid unloaddb/loaddb to make"
		  " 2008 R4.0 database.\n\n",
		  r40_beta_header->db_iopagesize, IO_MIN_PAGE_SIZE);
	  printf ("Migration failed.\n");
	  FCLOSE_AND_CHECK (log_active_fp, vol_path);
	  return EXIT_FAILURE;
	}

      if (r40_beta_header->db_logpagesize < IO_MIN_PAGE_SIZE)
	{
	  printf ("Log page size(%d) is too small.\n"
		  "The minimum log page size of 2008 R4.0 is %d.\n"
		  "You must use cubrid unloaddb/loaddb to make"
		  " 2008 R4.0 database.\n\n",
		  r40_beta_header->db_logpagesize, IO_MIN_PAGE_SIZE);
	  printf ("Migration failed.\n");
	  FCLOSE_AND_CHECK (log_active_fp, vol_path);
	  return EXIT_FAILURE;
	}

      db_set_page_size (r40_beta_header->db_iopagesize,
			r40_beta_header->db_logpagesize);

      log_Npage = r40_beta_header->npages;
    }
  else
    {
      *compat_level = old_header->db_compatibility;
      strcpy (release_string, old_header->db_release);
    }

  FCLOSE_AND_CHECK (log_active_fp, vol_path);
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

static int
fix_log_header (const char *vol_path)
{
  int i;
  FILE *fp = NULL;
  char *undo_page;

  R40_BETA_LOG_HEADER *beta_hdr;
  R40_BETA_LOG_PAGE *beta_iopage;
  char beta_page[IO_MAX_PAGE_SIZE];

  struct log_header *ga_hdr;
  LOG_PAGE *ga_iopage;
  char ga_page[IO_MAX_PAGE_SIZE];

  beta_iopage = (R40_BETA_LOG_PAGE *) beta_page;
  beta_hdr = (R40_BETA_LOG_HEADER *) beta_iopage->page;

  ga_iopage = (LOG_PAGE *) ga_page;
  ga_hdr = (struct log_header *) ga_iopage->area;

  FOPEN_AND_CHECK (fp, vol_path, "rb+");
  FREAD_AND_CHECK (beta_page, sizeof (char), LOG_PAGESIZE, fp, vol_path);

  undo_page = make_volume_header_undo_page (vol_path, LOG_PAGESIZE);
  if (undo_page == NULL)
    {
      FCLOSE_AND_CHECK (fp, vol_path);
      return ER_FAILED;
    }

  memcpy (undo_page, beta_page, LOG_PAGESIZE);

  ga_iopage->hdr.logical_pageid = beta_iopage->hdr.logical_pageid;
  ga_iopage->hdr.offset = beta_iopage->hdr.offset;

  memcpy (ga_hdr->magic, beta_hdr->magic, CUBRID_MAGIC_MAX_LENGTH);
  ga_hdr->db_creation = beta_hdr->db_creation;
  memcpy (ga_hdr->db_release, beta_hdr->db_release, REL_MAX_RELEASE_LENGTH);
  ga_hdr->db_compatibility = rel_disk_compatible ();
  ga_hdr->db_iopagesize = beta_hdr->db_iopagesize;
  ga_hdr->db_logpagesize = beta_hdr->db_logpagesize;
  ga_hdr->is_shutdown = beta_hdr->is_shutdown;
  ga_hdr->next_trid = 1;
  ga_hdr->avg_ntrans = beta_hdr->avg_ntrans;
  ga_hdr->avg_nlocks = beta_hdr->avg_nlocks;
  ga_hdr->npages = beta_hdr->npages;
  ga_hdr->fpageid = 0;
  ga_hdr->append_lsa.pageid = ga_hdr->fpageid;
  ga_hdr->append_lsa.offset = 0;
  LSA_COPY (&ga_hdr->chkpt_lsa, &ga_hdr->append_lsa);
  ga_hdr->nxarv_pageid = ga_hdr->fpageid;
  ga_hdr->nxarv_phy_pageid = 1;
  ga_hdr->nxarv_num = 0;
  ga_hdr->last_arv_num_for_syscrashes = -1;
  ga_hdr->last_deleted_arv_num = -1;
  LSA_SET_NULL (&ga_hdr->bkup_level0_lsa);
  LSA_SET_NULL (&ga_hdr->bkup_level1_lsa);
  LSA_SET_NULL (&ga_hdr->bkup_level2_lsa);
  memcpy (ga_hdr->prefix_name, beta_hdr->prefix_name, MAXLOGNAME);
  ga_hdr->reserved_int_1 = -1;
  ga_hdr->reserved_int_2 = -1;
  ga_hdr->perm_status = LOG_PSTAT_CLEAR;

  for (i = 0; i < FILEIO_BACKUP_UNDEFINED_LEVEL; i++)
    {
      ga_hdr->bkinfo[i].bkup_attime = 0;
      ga_hdr->bkinfo[i].ndirty_pages_post_bkup = 0;
      ga_hdr->bkinfo[i].io_baseln_time = 0;
      ga_hdr->bkinfo[i].io_numpages = 0;
      ga_hdr->bkinfo[i].io_bkuptime = 0;
    }

  ga_hdr->ha_server_state = 0;
  ga_hdr->ha_file_status = -1;
  LSA_SET_NULL (&ga_hdr->eof_lsa);
  LSA_SET_NULL (&ga_hdr->smallest_lsa_at_last_chkpt);

  rewind (fp);
  FWRITE_AND_CHECK (ga_page, sizeof (char), LOG_PAGESIZE, fp, vol_path);
  FFLUSH_AND_CHECK (fp, vol_path);
  FCLOSE_AND_CHECK (fp, vol_path);

  printf ("%s... done\n", vol_path);
  fflush (stdout);

  return NO_ERROR;
}

static int
undo_fix_volume_and_log_header (void)
{
  int i;
  VOLUME_UNDO_INFO *p;

  for (p = vol_undo_info, i = 0; i < vol_undo_count; i++, p++)
    {
      printf ("rollback volume header: %s\n", p->filename);
      fflush (stdout);

      if (undo_fix_volume_header (p->filename, p->page, p->page_size) !=
	  NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  free_volume_header_undo_list ();

  return NO_ERROR;
}

static int
new_db_ha_apply_info (void)
{
  MOP class_mop, public_user, dba_user;
  SM_TEMPLATE *def;
  int error_code = NO_ERROR;
  const char *index_col_names[] = { "db_name", "copied_log_path", NULL };

  class_mop = db_find_class (CT_HA_APPLY_INFO_NAME);
  if (class_mop != NULL)
    {
      return NO_ERROR;
    }

  def = smt_def_class (CT_HA_APPLY_INFO_NAME);
  if (def == NULL)
    {
      return er_errid ();
    }

  error_code = smt_add_attribute (def, "db_name", "varchar(255)", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "db_creation_time", "datetime", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "copied_log_path", "varchar(4096)",
				  NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "page_id", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "offset", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "log_record_time", "datetime", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "last_access_time", "datetime", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "status", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "insert_counter", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "update_counter", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "delete_counter", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "schema_counter", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "commit_counter", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "fail_counter", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "required_page_id", "bigint", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "start_time", "datetime", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add constraints */
  error_code = dbt_add_constraint (def, DB_CONSTRAINT_UNIQUE, NULL,
				   index_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = dbt_constrain_non_null (def, "db_name", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = dbt_constrain_non_null (def, "copied_log_path", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = dbt_constrain_non_null (def, "page_id", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = dbt_constrain_non_null (def, "offset", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_finish_class (def, &class_mop);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (locator_has_heap (class_mop) == NULL)
    {
      return er_errid ();
    }

  dba_user = au_get_dba_user ();
  error_code = au_change_owner (class_mop, dba_user);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  public_user = au_get_public_user ();
  error_code = au_grant (public_user, class_mop, AU_SELECT, false);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sm_mark_system_class (class_mop, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return NO_ERROR;
}

static int
fix_system_class ()
{
  int au_save;
  int error_code = NO_ERROR;

  au_save = au_disable ();

  error_code = fix_db_ha_apply_info ();

  au_enable (au_save);

  if (error_code == NO_ERROR)
    {
      db_commit_transaction ();
    }
  else
    {
      printf ("abort transaction: errno %d\n", error_code);
      db_abort_transaction ();
    }

  return error_code;
}

static int
get_db_path (const char *db_name, char *db_full_path)
{
  DB_INFO *dir = NULL;
  DB_INFO *db = NULL;

  if (cfg_read_directory (&dir, false) != NO_ERROR)
    {
      printf ("can't found databases.txt\n");
      return ER_FAILED;
    }

  db = cfg_find_db_list (dir, db_name);

  if (db == NULL)
    {
      if (dir)
	{
	  cfg_free_directory (dir);
	}

      printf ("unknown database: %s\n", db_name);
      return ER_FAILED;
    }

  COMPOSE_FULL_NAME (db_full_path, PATH_MAX, db->pathname, db_name);

  return NO_ERROR;
}

static int
fix_volume_and_log_header (const char *db_path)
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

      if (volid == LOG_DBLOG_ACTIVE_VOLID)
	{
	  if (fix_log_header (vol_path) != NO_ERROR)
	    {
	      fclose (vol_info_fp);
	      return ER_FAILED;
	    }
	}
      else
	{
	  if (fix_volume_header (vol_path) != NO_ERROR)
	    {
	      fclose (vol_info_fp);
	      return ER_FAILED;
	    }
	}
    }

  FCLOSE_AND_CHECK (vol_info_fp, vol_info_path);
  return NO_ERROR;
}

static int
fix_db_ha_apply_info ()
{
  MOP class_mop;
  int error_code = NO_ERROR;

  class_mop = db_find_class (CT_HA_APPLY_INFO_NAME);
  if (class_mop == NULL)
    {
      error_code = er_errid ();
      goto error;
    }

  error_code = db_drop_class (class_mop);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  error_code = new_db_ha_apply_info ();
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  printf ("%s... done\n", CT_HA_APPLY_INFO_NAME);

  return NO_ERROR;

error:
  return error_code;
}

static int
recreate_log_and_reset_lsa (const char *db_name)
{
  return boot_emergency_patch (db_name, true, log_Npage, stdout);
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
  char db_full_path[PATH_MAX], release_string[REL_MAX_RELEASE_LENGTH];
  float compat_level;

  if (argc != 2)
    {
      printf ("usage: %s <database name>\n", argv[0]);
      return EXIT_FAILURE;
    }

#if defined(WINDOWS)
  SetConsoleCtrlHandler ((PHANDLER_ROUTINE) intr_handler, TRUE);
#else
  os_set_signal_handler (SIGINT, intr_handler);
#endif

  db_name = argv[1];

  printf ("CUBRID Migration: 2008 R4.0 BETA to 2008 R4.0\n\n");

  if (rel_disk_compatible () != R40_GA_LEVEL)
    {
      /* invalid cubrid library */
      printf ("CUBRID library version is invalid.\n"
	      "Please upgrade to CUBRID 2008 R4.0 and retry migrate.\n");
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

  if (get_disk_compatibility_level (db_full_path, &compat_level,
				    release_string) != NO_ERROR)
    {
      return EXIT_FAILURE;
    }

  if (compat_level == R40_GA_LEVEL)
    {
      printf ("This database (%s) is already updated.\n", db_name);
      return EXIT_FAILURE;
    }

  if (compat_level != R40_BETA_LEVEL)
    {
      printf ("Cannot migrate this version (%s) of database.\n"
	      "%s migrates CUBRID 2008 R4.0 BETA database to 2008 R4.0.\n",
	      release_string, argv[0]);
      return EXIT_FAILURE;
    }

  printf ("\nStep 1. Start to fix volume header\n");

  if (fix_volume_and_log_header (db_full_path) != NO_ERROR)
    {
      if (undo_fix_volume_and_log_header () != NO_ERROR)
	{
	  printf ("recovering volume header fails.\n");
	}

      return EXIT_FAILURE;
    }

  AU_DISABLE_PASSWORDS ();

  (void) boot_set_skip_check_ct_classes (true);
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("dba", NULL);

  if (db_restart (argv[0], TRUE, db_name) != NO_ERROR)
    {
      printf ("%s\n", db_error_string (3));

      if (undo_fix_volume_and_log_header () != NO_ERROR)
	{
	  printf ("recovering volume header fails.\n");
	}

      printf ("Migration failed.\n");

      return EXIT_FAILURE;
    }

  printf ("\nStep 2. Start to fix system classes\n");

  if (fix_system_class () != NO_ERROR)
    {
      db_shutdown ();

      if (undo_fix_volume_and_log_header () != NO_ERROR)
	{
	  printf ("recovering volume header fails.\n");
	}

      printf ("Migration failed.\n");
      return EXIT_FAILURE;
    }

  printf ("\nStep 3. Start to recreate log and reset data volume header\n");
  printf
    ("This may take a long time depending on the number of pages and volumes.\n");
  printf ("Please don't stop while this step is being processed.\n");

  if (recreate_log_and_reset_lsa (db_name) != NO_ERROR)
    {
      printf ("Migration failed.\n");
      return EXIT_FAILURE;
    }

  free_volume_header_undo_list ();

  db_shutdown ();

  printf ("\nMigration to 2008 R4.0 has been completed successfully.\n");

  return NO_ERROR;
}
