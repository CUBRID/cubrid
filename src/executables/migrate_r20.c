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
 * migrate_r20.c :
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
#include "glo_class.h"
#include "elo_holder.h"
#include "elo_class.h"
#include "locator.h"
#include "locator_cl.h"
#include "schema_manager.h"


#define UNDO_LIST_SIZE 1024;

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

typedef struct old_disk_var_header OLD_DISK_VAR_HEADER;
struct old_disk_var_header
{
  char magic[CUBRID_MAGIC_MAX_LENGTH];
  INT16 iopagesize;
  INT16 volid;
  DB_VOLPURPOSE purpose;
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
  float warn_ratio;
  INT32 warnat;

  INT32 db_creation;

  LOG_LSA chkpt_lsa;
  HFID boot_hfid;
  INT16 offset_to_vol_fullname;
  INT16 offset_to_next_vol_fullname;
  INT16 offset_to_vol_remarks;
  char var_fields[1];
};

typedef struct old_file_header OLD_FILE_HEADER;
struct old_file_header
{
  VFID vfid;
  FILE_TYPE type;
  INT32 creation;
  int ismark_as_deleted;
  int num_table_vpids;
  VPID next_table_vpid;
  VPID last_table_vpid;
  int num_user_pages;
  int num_user_pages_mrkdelete;
  int num_allocsets;
  FILE_ALLOCSET allocset;
  VPID last_allocset_vpid;
  INT16 last_allocset_offset;

  struct
  {
    int total_length;
    int first_length;
    VPID next_part_vpid;
    char piece[1];
  } des;
};

typedef struct
{
  char *filename;
  char *page;
} VOLUME_UNDO_INFO;

typedef struct
{
  VFID vfid;
  char *page;
} FILE_UNDO_INFO;

typedef struct rebuilt_index_list REBUILT_INDEX_LIST;
struct rebuilt_index_list
{
  char *class_name;
  char *con_name;
  REBUILT_INDEX_LIST *next;
};

/* glo */

typedef struct old_dirheader OLD_DIRHEADER;
struct old_dirheader
{
  LOID loid;			/* LOM identifier */
  INT32 index_level;		/* Directory level:
				 * if 0,   page is a dir page (no indices),
				 * if > 0, page is an index page to directory
				 * pages.
				 */
  INT32 tot_length;		/* Total large object length          */
  INT32 tot_slot_cnt;		/* Total number of slots that form LO */
  VPID goodvpid_fordata;	/* A hint for a data page with space.
				 * Usually the last allocated data page.
				 */
  INT32 pg_tot_length;		/* Total length of data represented by
				 * this page
				 */
  INT32 pg_act_idxcnt;		/* Active entry count represented by this
				 * page
				 */
  INT32 pg_lastact_idx;		/* Last active entry index */
  VPID next_vpid;		/* Next directory page identifier */
};

typedef struct old_dirmap_entry OLD_DIRMAP_ENTRY;
struct old_dirmap_entry
{
  VPID vpid;			/* Directory page identifier */
  INT32 length;			/* Length represented by this index item */
};

typedef struct old_direntry OLD_DIRENTRY;
struct old_direntry
{
  union
  {
    VPID vpid;			/* Data page identifier */
    int length;			/* HOLE/EMPTY length */
  } u;
  INT16 slotid;			/* Data slot identifier */
  INT16 length;			/* Length of the data in the slot */
};

typedef struct glo_undo_info GLO_UNDO_INFO;
struct glo_undo_info
{
  VPID vpid;
  PAGE_PTR page;
  PAGE_PTR old_page;
  GLO_UNDO_INFO *next;
};

typedef struct glo_undo_list GLO_UNDO_LIST;
struct glo_undo_list
{
  GLO_UNDO_INFO *head;
  GLO_UNDO_INFO *tail;
  GLO_UNDO_INFO *dest_page;
};

#define OLD_MAX_DIRMAP_ENTRY_CNT                              \
        ((ssize_t) ((DB_PAGESIZE - sizeof (OLD_DIRHEADER))    \
                    / sizeof(OLD_DIRMAP_ENTRY)))

#define OLD_MAX_DIRENTRY_CNT \
        ((ssize_t) ((DB_PAGESIZE - sizeof (OLD_DIRHEADER))   \
                    / sizeof (OLD_DIRENTRY)))

#define NEW_MAX_DIRMAP_ENTRY_CNT                              \
        ((ssize_t) ((DB_PAGESIZE - sizeof (LARGEOBJMGR_DIRHEADER))    \
                    / sizeof(LARGEOBJMGR_DIRMAP_ENTRY)))

#define NEW_MAX_DIRENTRY_CNT \
        ((ssize_t) ((DB_PAGESIZE - sizeof (LARGEOBJMGR_DIRHEADER))   \
                    / sizeof (LARGEOBJMGR_DIRENTRY)))

#define ISEMPTY_DIRENTRY(ent)\
  ((ent)->slotid == NULL_SLOTID && (ent)->u.length == 0)

#define ISHOLE_DIRENTRY(ent)  \
  ((ent)->slotid == NULL_SLOTID && (ent)->u.length > 0)

#define ISREG_DIRENTRY(ent)\
  ((ent)->slotid != NULL_SLOTID)

#define DIRENTRY_LENGTH(ent) \
  (ISREG_DIRENTRY((ent)) ? (ent)->length : (ent)->u.length)


#define OLD_ISEMPTY_DIRENTRY(ent) \
  ((ent)->slotid == NULL_SLOTID && (ent)->u.length == 0)

#define OLD_ISHOLE_DIRENTRY(ent) \
  ((ent)->slotid == NULL_SLOTID && (ent)->u.length > 0)

#define DIR_PG_CNT_INDEX_CREATE_LIMIT 4

static int vol_undo_list_length;
static int vol_undo_count;
static VOLUME_UNDO_INFO *vol_undo_info;

static int file_undo_list_length;
static int file_undo_count;
static FILE_UNDO_INFO *file_undo_info;

static GLO_UNDO_LIST *glo_undo_info;
static int glo_undo_info_count;

/* fix volume header */
static int fix_active_log_header (const char *log_path, char *undo_page);
static int fix_volume_header (const char *vol_path, char *undo_page);
static int fix_volume_and_log_header (const char *db_path);
static int undo_fix_volume_and_log_header (void);
static int undo_fix_volume_header (const char *vol_path, char *undo_page);
static char *make_volume_header_undo_page (char *vol_path);
static void free_volume_header_undo_list (void);

/* fix file header */
static int fix_file_header (VFID * vfid, char *undo_page);
static int fix_all_file_header (void);
static int undo_fix_all_file_header (void);
static int undo_fix_file_header (VFID * vfid, char *undo_page);
static char *make_file_header_undo_page (VFID * vfid);
static void free_file_header_undo_list (void);

static int get_disk_compatibility_level (const char *db_path,
					 float *compat_level,
					 char *release_string);

/* add bigint, datetime info */
static int add_new_data_type (void);

/* add db_ha_info class */
static int add_new_classes (void);

/* rebuild index */
static int read_rebuilt_index_list (FILE * log_fp,
				    REBUILT_INDEX_LIST ** done_list);
static void free_rebuilt_index_list (REBUILT_INDEX_LIST * done_list);
static bool is_index_need_to_rebuild (DB_CONSTRAINT * cons);
static bool is_already_done (REBUILT_INDEX_LIST * done_list,
			     const char *class_name, const char *con_name);
static int rebuild_index (char *log_file, bool is_log_file_exist);

static int get_db_full_path (const char *db_name, char *db_full_path);

/* fix glo header */
static int fix_glo_method ();
static int fix_all_glo_data ();
static int undo_fix_all_glo_data ();
static int glo_fix_dir_pages (int index, LOID * loid);
static int glo_load_dir_pages (int index, LOID * loid);
static int glo_init_dirstate (LARGEOBJMGR_DIRSTATE * ds, PAGE_PTR first_page);
static bool
glo_initdir_newpage (THREAD_ENTRY * thread_p, const VFID * vfid,
		     const VPID * vpid, INT32 ignore_napges, void *xds);
static OLD_DIRMAP_ENTRY *glo_old_dirmap_entry (PAGE_PTR pgptr, int n);
static OLD_DIRENTRY *glo_old_direntry (PAGE_PTR pgptr, int n);

static LARGEOBJMGR_DIRMAP_ENTRY *glo_new_dirmap_entry (PAGE_PTR pgptr, int n);
static LARGEOBJMGR_DIRENTRY *glo_new_direntry (PAGE_PTR pgptr, int n);

static DB_ELO *glo_get_glo_from_holder (DB_OBJECT * glo_instance_p);
static int glo_copy_dirheader (LARGEOBJMGR_DIRHEADER * new_dir_header_p,
			       OLD_DIRHEADER * old_dir_header_p);
static int glo_copy_dirmap (LARGEOBJMGR_DIRMAP_ENTRY * new_dirmap_p,
			    OLD_DIRMAP_ENTRY * old_dirmap_p);
static PAGE_PTR glo_dir_allocpage (LARGEOBJMGR_DIRSTATE * ds, VPID * vpid);
static int glo_copy_dir (LARGEOBJMGR_DIRENTRY * new_dir_p,
			 OLD_DIRENTRY * old_dir_p);
static int glo_firstdir_map_create (int index, LARGEOBJMGR_DIRSTATE * ds);
static int glo_firstdir_map_shrink (LOID * lod, PAGE_PTR page_ptr);
static void glo_firstdir_update (LARGEOBJMGR_DIRSTATE * ds, int delta_len);
static int glo_dir_pgadd (int index, LARGEOBJMGR_DIRSTATE * ds);
static PAGE_PTR glo_get_page (int index, VPID * vpid, int page_kind);
static int glo_free_undo_info ();
static GLO_UNDO_INFO *glo_append_undo_info (int index, VPID * vpid,
					    PAGE_PTR current_page);
static int glo_append_dir_entry (int index, LARGEOBJMGR_DIRSTATE * ds,
				 LARGEOBJMGR_DIRENTRY * entry);
static int glo_all_pages_set_dirty (void);



static OLD_DIRMAP_ENTRY *
glo_old_dirmap_entry (PAGE_PTR pgptr, int n)
{
  return ((OLD_DIRMAP_ENTRY *) ((char *) pgptr + sizeof (OLD_DIRHEADER))) + n;
}

static OLD_DIRENTRY *
glo_old_direntry (PAGE_PTR pgptr, int n)
{
  return ((OLD_DIRENTRY *) ((char *) pgptr + sizeof (OLD_DIRHEADER))) + n;
}

static LARGEOBJMGR_DIRMAP_ENTRY *
glo_new_dirmap_entry (PAGE_PTR pgptr, int n)
{
  return ((LARGEOBJMGR_DIRMAP_ENTRY *) ((char *) pgptr +
					sizeof (LARGEOBJMGR_DIRHEADER))) + n;
}

static LARGEOBJMGR_DIRENTRY *
glo_new_direntry (PAGE_PTR pgptr, int n)
{
  return ((LARGEOBJMGR_DIRENTRY *) ((char *) pgptr +
				    sizeof (LARGEOBJMGR_DIRHEADER))) + n;
}

static int
fix_all_glo_data ()
{
  DB_OBJECT *glo_class_p;
  DB_OBJLIST *glo_objects, *tmp_obj, *all_classes, *tmp_class;
  DB_ELO *elo;
  int i, count, error = NO_ERROR;

  printf ("start to fix glo header\n");
  glo_class_p = db_find_class (GLO_CLASS_NAME);
  if (glo_class_p == NULL)
    {
      return db_error_code ();
    }

  all_classes = db_get_all_classes ();
  if (all_classes == NULL)
    {
      return db_error_code ();
    }

  count = 0;
  for (tmp_class = all_classes; tmp_class; tmp_class = tmp_class->next)
    {
      if ((tmp_class->op == glo_class_p)
	  || db_is_subclass (tmp_class->op, glo_class_p))
	{
	  glo_objects = db_get_all_objects (tmp_class->op);
	  if (glo_objects == NULL)
	    {
	      continue;
	    }

	  for (tmp_obj = glo_objects; tmp_obj; tmp_obj = tmp_obj->next)
	    {
	      count++;
	    }
	  db_objlist_free (glo_objects);
	  glo_objects = NULL;
	}
    }

  glo_undo_info = (GLO_UNDO_LIST *) malloc (count * sizeof (GLO_UNDO_LIST));
  if (glo_undo_info == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      count * sizeof (GLO_UNDO_LIST));
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto end;
    }
  memset (glo_undo_info, 0, count * sizeof (GLO_UNDO_LIST));
  glo_undo_info_count = count;

  for (tmp_class = all_classes; tmp_class; tmp_class = tmp_class->next)
    {
      if ((tmp_class->op == glo_class_p)
	  || db_is_subclass (tmp_class->op, glo_class_p))
	{
	  glo_objects = db_get_all_objects (tmp_class->op);
	  if (glo_objects == NULL)
	    {
	      continue;
	    }

	  for (i = 0, tmp_obj = glo_objects; tmp_obj;
	       i++, tmp_obj = tmp_obj->next)
	    {
	      elo = glo_get_glo_from_holder (tmp_obj->op);
	      if (elo == NULL)
		{
		  continue;
		}
	      error = glo_fix_dir_pages (i, &elo->loid);
	      if (error != NO_ERROR)
		{
		  db_objlist_free (glo_objects);
		  goto end;
		}
	    }
	  db_objlist_free (glo_objects);
	}
    }

end:
  db_objlist_free (all_classes);

  return error;
}

static GLO_UNDO_INFO *
glo_append_undo_info (int index, VPID * vpid, PAGE_PTR current_page)
{
  GLO_UNDO_INFO *undo_info;
  PAGE_PTR old_page_ptr;
  static int count = 0;

  old_page_ptr = (PAGE_PTR) malloc (DB_PAGESIZE);
  if (old_page_ptr == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      DB_PAGESIZE);
      return NULL;
    }
  memcpy (old_page_ptr, current_page, DB_PAGESIZE);

  undo_info = (GLO_UNDO_INFO *) malloc (sizeof (GLO_UNDO_INFO));
  if (undo_info == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (GLO_UNDO_INFO));
      free_and_init (old_page_ptr);
      return NULL;
    }

  undo_info->vpid = *vpid;
  undo_info->page = current_page;
  undo_info->old_page = old_page_ptr;
  undo_info->next = NULL;

  if (glo_undo_info[index].head == NULL && glo_undo_info[index].tail == NULL)
    {
      glo_undo_info[index].head = glo_undo_info[index].tail = undo_info;
    }
  else
    {
      glo_undo_info[index].tail->next = undo_info;
      glo_undo_info[index].tail = undo_info;
    }

  return undo_info;
}

static int
glo_free_undo_info ()
{
  GLO_UNDO_INFO *undo_info;
  int i;

  for (i = 0; i < glo_undo_info_count; i++)
    {
      while (glo_undo_info[i].head)
	{
	  undo_info = glo_undo_info[i].head;
	  glo_undo_info[i].head = glo_undo_info[i].head->next;

	  free_and_init (undo_info->old_page);
	  free_and_init (undo_info);
	}
      glo_undo_info[i].head = NULL;
      glo_undo_info[i].tail = NULL;
      glo_undo_info[i].dest_page = NULL;
    }

  free_and_init (glo_undo_info);

  return NO_ERROR;
}

static int
glo_set_dirty_all (void)
{
  GLO_UNDO_INFO *undo_info;
  int i;

  for (i = 0; i < glo_undo_info_count; i++)
    {
      undo_info = glo_undo_info[i].head;
      while (undo_info)
	{
	  pgbuf_set_dirty (NULL, undo_info->page, FREE);
	  undo_info = undo_info->next;
	}
    }

  return NO_ERROR;
}

static int
undo_fix_all_glo_data ()
{
  GLO_UNDO_INFO *undo_info;
  int i, error;

  for (i = 0; i < glo_undo_info_count; i++)
    {
      undo_info = glo_undo_info[i].head;
      while (undo_info)
	{
	  memcpy (undo_info->page, undo_info->old_page, DB_PAGESIZE);
	  undo_info = undo_info->next;
	}
    }

  error = glo_set_dirty_all ();
  if (error != NO_ERROR)
    {
      return error;
    }

  error = glo_free_undo_info ();
  return error;
}

static int
glo_copy_dirheader (LARGEOBJMGR_DIRHEADER * new_dir_header_p,
		    OLD_DIRHEADER * old_dir_header_p)
{
  new_dir_header_p->loid = old_dir_header_p->loid;
  new_dir_header_p->index_level = old_dir_header_p->index_level;
  new_dir_header_p->tot_length = (INT64) old_dir_header_p->tot_length;
  new_dir_header_p->tot_slot_cnt = old_dir_header_p->tot_length;
  new_dir_header_p->goodvpid_fordata = old_dir_header_p->goodvpid_fordata;
  new_dir_header_p->pg_tot_length = (INT64) old_dir_header_p->pg_tot_length;
  new_dir_header_p->pg_act_idxcnt = old_dir_header_p->pg_act_idxcnt;
  new_dir_header_p->pg_lastact_idx = old_dir_header_p->pg_lastact_idx;
  new_dir_header_p->next_vpid = old_dir_header_p->next_vpid;

  return NO_ERROR;
}

static int
glo_copy_dirmap (LARGEOBJMGR_DIRMAP_ENTRY * new_dirmap_p,
		 OLD_DIRMAP_ENTRY * old_dirmap_p)
{
  new_dirmap_p->vpid = old_dirmap_p->vpid;
  new_dirmap_p->length = old_dirmap_p->length;

  return NO_ERROR;
}

static int
glo_copy_dir (LARGEOBJMGR_DIRENTRY * new_dir_p, OLD_DIRENTRY * old_dir_p)
{
  new_dir_p->u.vpid = old_dir_p->u.vpid;
  new_dir_p->slotid = old_dir_p->slotid;
  new_dir_p->length = old_dir_p->length;

  return NO_ERROR;
}

static bool
glo_initdir_newpage (THREAD_ENTRY * thread_p, const VFID * vfid,
		     const VPID * vpid, INT32 ignore_napges, void *xds)
{
  LARGEOBJMGR_DIRHEADER *new_head;
  LARGEOBJMGR_DIRENTRY *ent_ptr;
  PAGE_PTR pgptr;
  int k;

  /* fetch newly allocated directory page and initialize it */
  pgptr = pgbuf_fix (thread_p, vpid, NEW_PAGE, PGBUF_LATCH_WRITE,
		     PGBUF_UNCONDITIONAL_LATCH);
  if (pgptr == NULL)
    {
      return false;
    }

  /* Copy the header and modify fields related to this page */
  new_head = (LARGEOBJMGR_DIRHEADER *) pgptr;
  *new_head = *(((LARGEOBJMGR_DIRSTATE *) xds)->firstdir.hdr);

  /* initialize directory page header entries */
  new_head->index_level = 0;
  new_head->pg_tot_length = 0;
  new_head->pg_act_idxcnt = 0;
  new_head->pg_lastact_idx = -1;
  VPID_SET_NULL (&new_head->next_vpid);

  /* initialize directory entries */
  for (k = 0, ent_ptr = glo_new_direntry (pgptr, 0);
       k < NEW_MAX_DIRENTRY_CNT; k++, ent_ptr++)
    {
      ent_ptr->slotid = NULL_SLOTID;
      ent_ptr->u.length = 0;
      ent_ptr->length = -1;
    }

  pgbuf_set_dirty (NULL, pgptr, FREE);

  return true;
}

static PAGE_PTR
glo_dir_allocpage (LARGEOBJMGR_DIRSTATE * ds, VPID * vpid)
{
  PAGE_PTR page_ptr = NULL;
  VPID *vpid_ptr;

  /* allocate new directory page */
  vpid_ptr = pgbuf_get_vpid_ptr (ds->curdir.pgptr);
  if (file_alloc_pages (NULL, &ds->curdir.hdr->loid.vfid, vpid, 1,
			vpid_ptr, glo_initdir_newpage, ds) == NULL)
    {
      return NULL;
    }

  page_ptr = pgbuf_fix (NULL, vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			PGBUF_UNCONDITIONAL_LATCH);
  if (page_ptr == NULL)
    {
      (void) file_dealloc_page (NULL, &ds->curdir.hdr->loid.vfid, vpid);
      return NULL;
    }

  return page_ptr;
}

static PAGE_PTR
glo_get_page (int index, VPID * vpid, int page_kind)
{
  GLO_UNDO_INFO *info;

  info = glo_undo_info[index].head;
  while (info)
    {
      if (VPID_EQ (vpid, &info->vpid))
	{
	  if (page_kind == OLD_PAGE)
	    {
	      return info->old_page;
	    }
	  else
	    {
	      return info->page;
	    }
	}
      info = info->next;
    }

  return NULL;
}

static int
glo_firstdir_map_create (int index, LARGEOBJMGR_DIRSTATE * ds)
{
  VPID next_vpid;
  PAGE_PTR page_ptr;
  GLO_UNDO_INFO *undo_info;
  LARGEOBJMGR_DIRMAP_ENTRY *firstdir_idxptr;
  LARGEOBJMGR_DIRHEADER *dir_hdr;
  int i;

  assert (glo_undo_info[index].dest_page != NULL);

  glo_undo_info[index].dest_page = glo_undo_info[index].dest_page->next;
  if (glo_undo_info[index].dest_page == NULL)
    {
      page_ptr = glo_dir_allocpage (ds, &next_vpid);
      if (page_ptr == NULL)
	{
	  return ER_FAILED;
	}

      undo_info = glo_append_undo_info (index, &next_vpid, page_ptr);
      if (undo_info == NULL)
	{
	  pgbuf_set_dirty (NULL, page_ptr, FREE);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }
  else
    {
      page_ptr = glo_undo_info[index].dest_page->page;
      next_vpid = glo_undo_info[index].dest_page->vpid;
    }

  memcpy (page_ptr, ds->firstdir.pgptr, DB_PAGESIZE);

  for (ds->firstdir.idx = 0,
       ds->firstdir.idxptr = glo_new_dirmap_entry (ds->firstdir.pgptr, 0);
       ds->firstdir.idx < NEW_MAX_DIRMAP_ENTRY_CNT;
       ds->firstdir.idxptr++, ds->firstdir.idx++)
    {
      if (VPID_ISNULL (&next_vpid))
	{
	  break;
	}
      page_ptr = glo_get_page (index, &next_vpid, NEW_PAGE);
      if (page_ptr == NULL)
	{
	  return ER_FAILED;
	}
      dir_hdr = (LARGEOBJMGR_DIRHEADER *) page_ptr;
      ds->firstdir.idxptr->vpid = next_vpid;
      ds->firstdir.idxptr->length = dir_hdr->pg_tot_length;
      next_vpid = dir_hdr->next_vpid;
    }

  for (i = ds->firstdir.idx, firstdir_idxptr = ds->firstdir.idxptr;
       i < NEW_MAX_DIRMAP_ENTRY_CNT; i++, firstdir_idxptr++)
    {
      VPID_SET_NULL (&firstdir_idxptr->vpid);
      firstdir_idxptr->length = 0;
    }

  ds->firstdir.idxptr--;
  ds->firstdir.idx--;
  ds->index_level = 1;

  /* Now modify the header of the first page */
  ds->firstdir.hdr->index_level = 1;
  ds->firstdir.hdr->pg_tot_length = ds->firstdir.hdr->tot_length;
  ds->firstdir.hdr->pg_act_idxcnt = ds->firstdir.idx + 1;
  ds->firstdir.hdr->pg_lastact_idx = ds->firstdir.hdr->pg_act_idxcnt - 1;
  VPID_SET_NULL (&ds->firstdir.hdr->next_vpid);

  return NO_ERROR;
}

static int
glo_firstdir_map_shrink (LOID * lod, PAGE_PTR page_ptr)
{
  LARGEOBJMGR_DIRMAP_ENTRY *del_ent_ptr, *ent_ptr;
  LARGEOBJMGR_DIRHEADER *dir_hdr;
  int del_idx, del_length, mv_ent_cnt;
  char *reg_ptr;

  del_idx = 1 + rand () % (NEW_MAX_DIRMAP_ENTRY_CNT - 3);

  del_ent_ptr = glo_new_dirmap_entry (page_ptr, del_idx);
  del_length = del_ent_ptr->length;
  mv_ent_cnt = NEW_MAX_DIRMAP_ENTRY_CNT - (del_idx + 1);
  reg_ptr = (char *) ((LARGEOBJMGR_DIRMAP_ENTRY *) del_ent_ptr - 1);

  /* update previous entry length */
  (del_ent_ptr - 1)->length += del_length;

  /* left shift entries to deleted entry */
  memmove (del_ent_ptr, del_ent_ptr + 1,
	   mv_ent_cnt * sizeof (LARGEOBJMGR_DIRMAP_ENTRY));

  /* initialize last entry */
  ent_ptr = glo_new_dirmap_entry (page_ptr, NEW_MAX_DIRMAP_ENTRY_CNT - 1);
  VPID_SET_NULL (&ent_ptr->vpid);
  ent_ptr->length = 0;

  /* update directory header */
  dir_hdr = (LARGEOBJMGR_DIRHEADER *) page_ptr;
  dir_hdr->pg_act_idxcnt--;
  dir_hdr->pg_lastact_idx = dir_hdr->pg_act_idxcnt - 1;

  return NO_ERROR;
}

static int
glo_dir_pgadd (int index, LARGEOBJMGR_DIRSTATE * ds)
{
  static int dir_pgcnt = 0;
  PAGE_PTR page_ptr;
  LARGEOBJMGR_DIRENTRY *new_ent_ptr;
  VPID next_vpid;
  GLO_UNDO_INFO *undo_info;
  int error = NO_ERROR;
  int i;

  assert (glo_undo_info[index].dest_page != NULL);

  glo_undo_info[index].dest_page = glo_undo_info[index].dest_page->next;
  if (glo_undo_info[index].dest_page == NULL)
    {
      page_ptr = glo_dir_allocpage (ds, &next_vpid);
      if (page_ptr == NULL)
	{
	  return ER_FAILED;
	}

      undo_info = glo_append_undo_info (index, &next_vpid, page_ptr);
      if (undo_info == NULL)
	{
	  pgbuf_set_dirty (NULL, page_ptr, FREE);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      glo_undo_info[index].dest_page = glo_undo_info[index].tail;
    }
  else
    {
      page_ptr = glo_undo_info[index].dest_page->page;
      next_vpid = glo_undo_info[index].dest_page->vpid;
    }

  if (ds->curdir.pgptr != NULL)
    {
      ds->curdir.hdr->next_vpid = next_vpid;
    }
  ds->curdir.pgptr = page_ptr;
  ds->curdir.hdr = (LARGEOBJMGR_DIRHEADER *) ds->curdir.pgptr;
  ds->curdir.idx = 0;
  ds->curdir.idxptr = glo_new_direntry (ds->curdir.pgptr, 0);

  /* initialize directory entry header */
  ds->curdir.hdr->index_level = 0;
  ds->curdir.hdr->pg_tot_length = 0;
  ds->curdir.hdr->pg_act_idxcnt = 0;
  ds->curdir.hdr->pg_lastact_idx = -1;
  VPID_SET_NULL (&ds->curdir.hdr->next_vpid);

  for (i = 0, new_ent_ptr = ds->curdir.idxptr;
       i < NEW_MAX_DIRENTRY_CNT; i++, new_ent_ptr++)
    {
      new_ent_ptr->slotid = NULL_SLOTID;
      new_ent_ptr->u.length = 0;
      new_ent_ptr->length = -1;
    }

  if (ds->index_level == 0)
    {
      if (dir_pgcnt >= DIR_PG_CNT_INDEX_CREATE_LIMIT)
	{
	  error = glo_firstdir_map_create (index, ds);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
    }
  else
    {
      if (ds->firstdir.hdr->pg_act_idxcnt < NEW_MAX_DIRMAP_ENTRY_CNT)
	{
	  if (ds->firstdir.hdr->pg_act_idxcnt == 0)
	    {
	      ds->firstdir.idx = 0;
	      ds->firstdir.idxptr = glo_new_dirmap_entry (ds->firstdir.pgptr,
							  0);
	    }
	  else
	    {
	      ds->firstdir.idx++;
	      ds->firstdir.idxptr++;
	    }
	}
      else
	{
	  error = glo_firstdir_map_shrink (&ds->firstdir.hdr->loid,
					   ds->firstdir.pgptr);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}

      ds->firstdir.idxptr->vpid = next_vpid;
      ds->firstdir.idxptr->length = 0;

      ds->firstdir.hdr->pg_act_idxcnt++;
      ds->firstdir.hdr->pg_lastact_idx = ds->firstdir.hdr->pg_act_idxcnt - 1;
    }

  if (ds->index_level > 0
      && ds->firstdir.idx < ds->firstdir.hdr->pg_lastact_idx
      && VPID_EQ (&next_vpid, &((ds->firstdir.idxptr + 1)->vpid)))
    {
      ds->firstdir.idx++;
      ds->firstdir.idxptr++;
    }
  dir_pgcnt++;

  return error;
}

static void
glo_firstdir_update (LARGEOBJMGR_DIRSTATE * ds, int delta_len)
{
  /* Do we have a directory index page ? */
  if (ds->index_level > 0)
    {
      /* update index page entry */
      ds->firstdir.idxptr->length += delta_len;

      /* update index page header */
      ds->firstdir.hdr->tot_length += delta_len;
      ds->firstdir.hdr->tot_slot_cnt += 1;
      ds->firstdir.hdr->pg_tot_length += delta_len;
    }
  else if (ds->firstdir.pgptr != ds->curdir.pgptr)
    {
      ds->firstdir.hdr->tot_length += delta_len;
      ds->firstdir.hdr->tot_slot_cnt += 1;
    }
}

static int
glo_append_dir_entry (int index, LARGEOBJMGR_DIRSTATE * ds,
		      LARGEOBJMGR_DIRENTRY * entry)
{
  int error = NO_ERROR;
  int delta;

  if (ds->curdir.hdr->pg_act_idxcnt >= NEW_MAX_DIRENTRY_CNT)
    {
      error = glo_dir_pgadd (index, ds);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  if (!LARGEOBJMGR_ISEMPTY_DIRENTRY (ds->curdir.idxptr))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }
  memcpy (ds->curdir.idxptr, entry, sizeof (LARGEOBJMGR_DIRENTRY));

  ds->curdir.idx += 1;
  ds->curdir.idxptr += 1;

  delta = DIRENTRY_LENGTH (entry);
  ds->curdir.hdr->tot_length += delta;
  ds->curdir.hdr->tot_slot_cnt += 1;
  ds->curdir.hdr->pg_tot_length += delta;
  ds->curdir.hdr->pg_act_idxcnt += 1;
  ds->curdir.hdr->pg_lastact_idx += 1;

  glo_firstdir_update (ds, delta);

  return NO_ERROR;
}

static int
glo_load_dir_pages (int index, LOID * loid)
{
  PAGE_PTR page_ptr;
  OLD_DIRHEADER *old_dir_header_p;
  VPID next_vpid;
  GLO_UNDO_INFO *undo_info;

  next_vpid = loid->vpid;
  do
    {
      page_ptr = pgbuf_fix (NULL, &next_vpid, OLD_PAGE,
			    PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (page_ptr == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return ER_FAILED;
	}

      undo_info = glo_append_undo_info (index, &next_vpid, page_ptr);
      if (undo_info == NULL)
	{
	  pgbuf_set_dirty (NULL, page_ptr, FREE);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      old_dir_header_p = (OLD_DIRHEADER *) page_ptr;

      next_vpid = old_dir_header_p->next_vpid;
    }
  while (!VPID_ISNULL (&next_vpid));

  return NO_ERROR;
}

static int
glo_init_dirstate (LARGEOBJMGR_DIRSTATE * ds, PAGE_PTR first_page)
{
  LARGEOBJMGR_DIRENTRY *new_ent_ptr;
  int i;

  /* initialize directory state */
  ds->firstdir.pgptr = first_page;
  ds->firstdir.hdr = ((LARGEOBJMGR_DIRHEADER *) (ds->firstdir.pgptr));
  ds->firstdir.idxptr = NULL;
  ds->firstdir.idx = 0;

  ds->curdir.pgptr = ds->firstdir.pgptr;
  ds->curdir.hdr = ((LARGEOBJMGR_DIRHEADER *) (ds->curdir.pgptr));
  ds->curdir.idxptr = glo_new_direntry (ds->curdir.pgptr, 0);
  ds->curdir.idx = 0;

  ds->index_level = 0;
  ds->tot_length = 0;
  ds->lo_offset = 0;
  ds->goodvpid_fordata = ds->firstdir.hdr->goodvpid_fordata;

  /* initialize directory page header */
  ds->firstdir.hdr->index_level = 0;
  ds->firstdir.hdr->tot_length = 0;
  ds->firstdir.hdr->pg_tot_length = 0;
  ds->firstdir.hdr->pg_act_idxcnt = 0;
  ds->firstdir.hdr->pg_lastact_idx = -1;
  ds->firstdir.hdr->tot_slot_cnt = 0;
  VPID_SET_NULL (&ds->firstdir.hdr->next_vpid);

  for (i = 0, new_ent_ptr = glo_new_direntry (ds->firstdir.pgptr, 0);
       i < NEW_MAX_DIRENTRY_CNT; i++, new_ent_ptr++)
    {
      new_ent_ptr->slotid = NULL_SLOTID;
      new_ent_ptr->u.length = 0;
      new_ent_ptr->length = -1;
    }

  return NO_ERROR;
}

static int
glo_fix_dir_pages (int index, LOID * loid)
{
  LARGEOBJMGR_DIRHEADER *new_dirheader;
  OLD_DIRHEADER *old_dirheader;
  OLD_DIRMAP_ENTRY *dirmap;
  LARGEOBJMGR_DIRSTATE *ds = NULL;
  PAGE_PTR page_ptr;
  VPID next_vpid;
  int error = NO_ERROR;

  if (loid->vpid.pageid == NULL_PAGEID)
    {
      return error;
    }

  error = glo_load_dir_pages (index, loid);
  if (error != NO_ERROR)
    {
      return error;
    }

  new_dirheader = (LARGEOBJMGR_DIRHEADER *) glo_undo_info[index].head->page;
  old_dirheader = (OLD_DIRHEADER *) glo_undo_info[index].head->old_page;
  glo_copy_dirheader (new_dirheader, old_dirheader);

  ds = (LARGEOBJMGR_DIRSTATE *) malloc (sizeof (LARGEOBJMGR_DIRSTATE));
  if (ds == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (LARGEOBJMGR_DIRSTATE));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  if (glo_init_dirstate (ds, glo_undo_info[index].head->page) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }

  glo_undo_info[index].dest_page = glo_undo_info[index].head;

  next_vpid = glo_undo_info[index].head->vpid;
  while (!VPID_ISNULL (&next_vpid))
    {
      page_ptr = glo_get_page (index, &next_vpid, OLD_PAGE);
      if (page_ptr == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return ER_FAILED;
	}

      old_dirheader = (OLD_DIRHEADER *) page_ptr;

      if (old_dirheader->index_level > 0)
	{
	  /* move next page */
	  dirmap = glo_old_dirmap_entry (page_ptr, 0);
	  page_ptr = glo_get_page (index, &dirmap->vpid, OLD_PAGE);
	  if (page_ptr == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      return ER_FAILED;
	    }
	  old_dirheader = (OLD_DIRHEADER *) page_ptr;
	}

      if (old_dirheader->index_level == 0)
	{
	  /* Regular directory */
	  LARGEOBJMGR_DIRENTRY new_dir;
	  OLD_DIRENTRY *old_dir_p;
	  int new_dir_idx = 0, old_dir_idx = 0;

	  old_dir_p = glo_old_direntry (page_ptr, 0);

	  while (old_dir_idx < OLD_MAX_DIRENTRY_CNT)
	    {
	      if (!OLD_ISEMPTY_DIRENTRY (old_dir_p)
		  && !OLD_ISHOLE_DIRENTRY (old_dir_p))
		{
		  glo_copy_dir (&new_dir, old_dir_p);
		  error = glo_append_dir_entry (index, ds, &new_dir);
		  if (error != NO_ERROR)
		    {
		      return error;
		    }
		}
	      old_dir_idx++;
	      old_dir_p++;
	    }
	}

      next_vpid = old_dirheader->next_vpid;
    }
  while (!VPID_ISNULL (&next_vpid));

  return error;
}

static DB_ELO *
glo_get_glo_from_holder (DB_OBJECT * glo_instance_p)
{
  DB_VALUE value;
  DB_OBJECT *glo_holder_p;
  int rc, save;
  DB_ELO *glo_p = NULL;

  save = au_disable ();

  rc = db_get (glo_instance_p, GLO_CLASS_HOLDER_NAME, &value);
  if (rc != NO_ERROR)
    {
      printf ("%s\n", db_error_string (3));
      goto end;
    }

  glo_holder_p = DB_GET_OBJECT (&value);
  if (glo_holder_p == NULL)
    {
      if (db_error_code () != NO_ERROR)
	{
	  printf ("%s\n", db_error_string (3));
	}
      goto end;
    }

  rc = db_get (glo_holder_p, GLO_HOLDER_GLO_NAME, &value);
  if (rc != NO_ERROR)
    {
      printf ("%s\n", db_error_string (3));
      goto end;
    }

  glo_p = DB_GET_ELO (&value);
  if (glo_p == NULL)
    {
      if (db_error_code () != NO_ERROR)
	{
	  printf ("%s\n", db_error_string (3));
	}
      goto end;
    }

  if (elo_get_pathname (glo_p) == NULL)
    {
      goto end;
    }

end:
  au_enable (save);
  return (glo_p);
}

#if 0
static int
fix_glo_methods ()
{
  DB_OBJECT *glo_class;
  int save;

  save = au_disable ();

  printf ("start to fix glo methods\n");

  glo_class = db_find_class ("glo");
  if (glo_class == NULL)
    {
      printf ("%s\n", db_error_string (3));
      return db_error_code ();
    }

  def_instance_signature (glo_class, GLO_METHOD_SEEK, db_get_type_name (DB_TYPE_BIGINT),	/*return arg */
			  db_get_type_name (DB_TYPE_BIGINT),	/*position */
			  NULL, NULL, NULL, NULL, NULL, NULL, NULL);

  def_instance_signature (glo_class, GLO_METHOD_DELETE, db_get_type_name (DB_TYPE_BIGINT),	/*return arg */
			  db_get_type_name (DB_TYPE_BIGINT),	/*length */
			  NULL, NULL, NULL, NULL, NULL, NULL, NULL);

  def_instance_signature (glo_class, GLO_METHOD_TRUNCATE, db_get_type_name (DB_TYPE_BIGINT),	/*return arg */
			  NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

  def_instance_signature (glo_class, GLO_METHOD_SIZE, db_get_type_name (DB_TYPE_BIGINT),	/*return arg */
			  NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

  def_instance_signature (glo_class, GLO_METHOD_COPY_TO, db_get_type_name (DB_TYPE_BIGINT),	/*return arg */
			  NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

  def_instance_signature (glo_class, GLO_METHOD_COPY_FROM, db_get_type_name (DB_TYPE_BIGINT),	/*return arg */
			  NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

  def_instance_signature (glo_class, GLO_METHOD_POSITION, db_get_type_name (DB_TYPE_BIGINT),	/*return arg */
			  NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

  def_instance_signature (glo_class, GLO_METHOD_LIKE_SEARCH, db_get_type_name (DB_TYPE_BIGINT),	/*return arg */
			  db_get_type_name (DB_TYPE_STRING),	/*search str */
			  NULL, NULL, NULL, NULL, NULL, NULL, NULL);

  def_instance_signature (glo_class, GLO_METHOD_REG_SEARCH, db_get_type_name (DB_TYPE_BIGINT),	/*return arg */
			  db_get_type_name (DB_TYPE_STRING),	/*search str */
			  NULL, NULL, NULL, NULL, NULL, NULL, NULL);

  def_instance_signature (glo_class, GLO_METHOD_BINARY_SEARCH, db_get_type_name (DB_TYPE_BIGINT),	/*return arg */
			  db_get_type_name (DB_TYPE_STRING),	/*search str */
			  db_get_type_name (DB_TYPE_INTEGER),	/*str length */
			  NULL, NULL, NULL, NULL, NULL, NULL);

  au_enable (save);

  db_commit_transaction ();

  return NO_ERROR;
}
#endif

static int
fix_active_log_header (const char *log_path, char *undo_page)
{
  FILE *fp = NULL;
  struct old_log_header *old;
  struct log_header new;
  int i;
  FILEIO_PAGE *iopage;
  char page[IO_MAX_PAGE_SIZE];

  FOPEN_AND_CHECK (fp, log_path, "r+");
  FREAD_AND_CHECK (page, sizeof (char), IO_PAGESIZE, fp, log_path);

  memcpy (undo_page, page, IO_PAGESIZE);

  iopage = (FILEIO_PAGE *) page;
  old = (struct old_log_header *) iopage->page;

  memset (&new, 0, sizeof (struct log_header));
  memcpy (new.magic, old->magic, CUBRID_MAGIC_MAX_LENGTH);
  new.db_creation = old->db_creation;
  strcpy (new.db_release, rel_release_string ());
  new.db_compatibility = rel_disk_compatible ();
  new.db_iopagesize = old->db_iopagesize;
  new.is_shutdown = old->is_shutdown;
  new.next_trid = old->next_trid;
  new.avg_ntrans = old->avg_ntrans;
  new.avg_nlocks = old->avg_nlocks;
  new.npages = old->npages;
  new.fpageid = old->fpageid;
  new.append_lsa = old->append_lsa;
  new.chkpt_lsa = old->chkpt_lsa;
  new.nxarv_pageid = old->nxarv_pageid;
  new.nxarv_phy_pageid = old->nxarv_phy_pageid;
  new.nxarv_num = old->nxarv_num;
  new.last_arv_num_for_syscrashes = old->last_arv_num_for_syscrashes;
  new.last_deleted_arv_num = old->last_deleted_arv_num;
  new.has_logging_been_skipped = old->has_logging_been_skipped;
  new.bkup_level0_lsa = old->bkup_level0_lsa;
  new.bkup_level1_lsa = old->bkup_level1_lsa;
  new.bkup_level2_lsa = old->bkup_level2_lsa;
  memcpy (new.prefix_name, old->prefix_name, MAXLOGNAME);
  new.lowest_arv_num_for_backup = old->lowest_arv_num_for_backup;
  new.highest_arv_num_for_backup = old->highest_arv_num_for_backup;
  new.perm_status = old->perm_status;

  for (i = 0; i < FILEIO_BACKUP_UNDEFINED_LEVEL; i++)
    {
      new.bkinfo[i].bkup_attime = old->bkinfo[i].bkup_attime;
      new.bkinfo[i].io_baseln_time = old->bkinfo[i].io_baseln_time;
      new.bkinfo[i].io_bkuptime = old->bkinfo[i].io_bkuptime;
      new.bkinfo[i].ndirty_pages_post_bkup =
	old->bkinfo[i].ndirty_pages_post_bkup;
      new.bkinfo[i].io_numpages = old->bkinfo[i].io_numpages;
    }

  new.ha_server_state = -1;
  new.ha_file_status = -1;
  LSA_SET_NULL (&(new.eof_lsa));

  FSEEK_AND_CHECK (fp, sizeof (FILEIO_PAGE_RESERVED), SEEK_SET, log_path);
  FWRITE_AND_CHECK (&new, sizeof (struct log_header), 1, fp, log_path);
  FFLUSH_AND_CHECK (fp, log_path);
  FCLOSE_AND_CHECK (fp, log_path);

  return NO_ERROR;
}

static int
fix_volume_header (const char *vol_path, char *undo_page)
{
  FILE *fp = NULL;
  OLD_DISK_VAR_HEADER *old;
  DISK_VAR_HEADER *new_hd;
  FILEIO_PAGE *iopage;
  char page[IO_MAX_PAGE_SIZE];
  INT32 save_warnat;
  INT32 save_db_creation;

  FOPEN_AND_CHECK (fp, vol_path, "rb+");
  FREAD_AND_CHECK (page, sizeof (char), IO_PAGESIZE, fp, vol_path);

  memcpy (undo_page, page, IO_PAGESIZE);

  iopage = (FILEIO_PAGE *) page;
  old = (OLD_DISK_VAR_HEADER *) iopage->page;
  new_hd = (DISK_VAR_HEADER *) iopage->page;

  save_warnat = old->warnat;
  save_db_creation = old->db_creation;

  new_hd->warnat = save_warnat;
  new_hd->db_creation = save_db_creation;

  rewind (fp);
  FWRITE_AND_CHECK (page, sizeof (char), IO_PAGESIZE, fp, vol_path);

  FFLUSH_AND_CHECK (fp, vol_path);

  FCLOSE_AND_CHECK (fp, vol_path);

  return NO_ERROR;
}

static int
undo_fix_volume_header (const char *vol_path, char *undo_page)
{
  FILE *fp = NULL;

  FOPEN_AND_CHECK (fp, vol_path, "rb+");
  FWRITE_AND_CHECK (undo_page, sizeof (char), IO_PAGESIZE, fp, vol_path);
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
  struct log_header *new_header;
  struct old_log_header *old_header;
  FILEIO_PAGE *iopage;
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

  iopage = (FILEIO_PAGE *) page;
  old_header = (struct old_log_header *) iopage->page;
  new_header = (struct log_header *) iopage->page;

  if (new_header->db_compatibility == rel_disk_compatible ())
    {
      *compat_level = new_header->db_compatibility;
      strcpy (release_string, new_header->db_release);
    }
  else
    {
      *compat_level = old_header->db_compatibility;
      strcpy (release_string, old_header->db_release);
    }

  FCLOSE_AND_CHECK (log_active_fp, vol_path);
  return NO_ERROR;
}

static int
fix_volume_and_log_header (const char *db_path)
{
  char vol_info_path[PATH_MAX], vol_path[PATH_MAX], *undo_page;
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

      undo_page = make_volume_header_undo_page (vol_path);
      if (undo_page == NULL)
	{
	  fclose (vol_info_fp);
	  return ER_FAILED;
	}

      if (volid == LOG_DBLOG_ACTIVE_VOLID)
	{
	  if (fix_active_log_header (vol_path, undo_page) != NO_ERROR)
	    {
	      fclose (vol_info_fp);
	      return ER_FAILED;
	    }
	}
      else
	{
	  if (fix_volume_header (vol_path, undo_page) != NO_ERROR)
	    {
	      fclose (vol_info_fp);
	      return ER_FAILED;
	    }
	}
    }

  FCLOSE_AND_CHECK (vol_info_fp, vol_info_path);
  return NO_ERROR;
}

static char *
make_volume_header_undo_page (char *vol_path)
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

  undo_page = (char *) malloc (IO_PAGESIZE * sizeof (char));
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
undo_fix_volume_and_log_header (void)
{
  int i;
  VOLUME_UNDO_INFO *p;

  for (p = vol_undo_info, i = 0; i < vol_undo_count; i++, p++)
    {
      if (undo_fix_volume_header (p->filename, p->page) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  free_volume_header_undo_list ();

  return NO_ERROR;
}

static int
fix_file_header (VFID * vfid, char *undo_page)
{
  VPID vpid;
  PAGE_PTR fhdr_pgptr;
  OLD_FILE_HEADER *old;
  FILE_HEADER *new;
  FILE_TYPE save_type;
  INT32 save_time;
  INT32 save_ismark_as_deleted;

  vpid.volid = vfid->volid;
  vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (NULL, &vpid, OLD_PAGE,
			  PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      return ER_FAILED;
    }

  memcpy (undo_page, fhdr_pgptr, DB_PAGESIZE);

  old = (OLD_FILE_HEADER *) fhdr_pgptr;
  new = (FILE_HEADER *) fhdr_pgptr;

  save_type = old->type;
  save_time = old->creation;
  save_ismark_as_deleted = old->ismark_as_deleted;

  new->creation = save_time;
  new->type = (INT16) save_type;
  new->ismark_as_deleted = (INT16) save_ismark_as_deleted;

  pgbuf_set_dirty (NULL, fhdr_pgptr, FREE);

  return NO_ERROR;
}

static int
undo_fix_file_header (VFID * vfid, char *undo_page)
{
  VPID vpid;
  PAGE_PTR fhdr_pgptr;

  vpid.volid = vfid->volid;
  vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (NULL, &vpid, OLD_PAGE,
			  PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      return ER_FAILED;
    }

  memcpy (fhdr_pgptr, undo_page, DB_PAGESIZE);
  pgbuf_set_dirty (NULL, fhdr_pgptr, FREE);

  return NO_ERROR;
}

static int
fix_all_file_header (void)
{
  int num_files, i;
  VFID vfid, *tracker_vfid;
  char *undo_page;

  file_undo_count = 0;
  file_undo_list_length = UNDO_LIST_SIZE;
  file_undo_info = (FILE_UNDO_INFO *) calloc (file_undo_list_length,
					      sizeof (FILE_UNDO_INFO));
  if (file_undo_info == NULL)
    {
      return ER_FAILED;
    }

  tracker_vfid = file_get_tracker_vfid ();

  undo_page = make_file_header_undo_page (tracker_vfid);
  if (undo_page == NULL)
    {
      return ER_FAILED;
    }

  if (fix_file_header (tracker_vfid, undo_page) != NO_ERROR)
    {
      return ER_FAILED;
    }

  num_files = file_get_numfiles (NULL);
  if (num_files < 0)
    {
      return ER_FAILED;
    }


  for (i = 0; i < num_files; i++)
    {
      if (file_find_nthfile (NULL, &vfid, i) != 1)
	{
	  break;
	}

      if (VFID_EQ (&vfid, tracker_vfid) || vfid.fileid == 0)
	{
	  continue;
	}

      undo_page = make_file_header_undo_page (&vfid);
      if (undo_page == NULL)
	{
	  return ER_FAILED;
	}

      if (fix_file_header (&vfid, undo_page) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  if (db_commit_transaction () != NO_ERROR)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

static char *
make_file_header_undo_page (VFID * vfid)
{
  FILE_UNDO_INFO *new_undo_info;
  char *undo_page;

  if (file_undo_count == file_undo_list_length)
    {
      new_undo_info =
	(FILE_UNDO_INFO *) calloc (2 * file_undo_list_length,
				   sizeof (FILE_UNDO_INFO));
      if (new_undo_info == NULL)
	{
	  return NULL;
	}

      memcpy (new_undo_info, file_undo_info,
	      file_undo_list_length * sizeof (FILE_UNDO_INFO));
      free (file_undo_info);
      file_undo_info = new_undo_info;

      file_undo_list_length *= 2;
    }

  undo_page = (char *) malloc (DB_PAGESIZE * sizeof (char));
  if (undo_page == NULL)
    {
      return NULL;
    }

  file_undo_info[file_undo_count].vfid = *vfid;
  file_undo_info[file_undo_count].page = undo_page;
  file_undo_count++;

  return undo_page;
}

static void
free_file_header_undo_list (void)
{
  int i;
  FILE_UNDO_INFO *p;

  for (p = file_undo_info, i = 0; i < file_undo_count; i++, p++)
    {
      if (p->page)
	{
	  free (p->page);
	}
    }

  if (file_undo_info)
    {
      free (file_undo_info);
    }
}

static int
undo_fix_all_file_header (void)
{
  int i;
  FILE_UNDO_INFO *p;

  for (p = file_undo_info, i = 0; i < file_undo_count; i++, p++)
    {
      if (undo_fix_file_header (&p->vfid, p->page) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  free_file_header_undo_list ();

  if (db_commit_transaction () != NO_ERROR)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

static int
add_new_data_type (void)
{
  MOP class_mop;
  DB_OBJECT *obj;
  DB_VALUE val;
  int error_code = NO_ERROR;
  DB_TYPE new_types[] = { DB_TYPE_BIGINT, DB_TYPE_DATETIME };
  const char *type_names[] = { "BIGINT", "DATETIME" };
  int au_save;
  unsigned int i;

  class_mop = db_find_class (CT_DATATYPE_NAME);
  if (class_mop == NULL)
    {
      return ER_FAILED;
    }

  au_save = au_disable ();

  for (i = 0; i < sizeof (new_types) / sizeof (DB_TYPE); i++)
    {
      obj = db_create_internal (class_mop);
      if (obj == NULL)
	{
	  error_code = er_errid ();
	  goto error;
	}

      DB_MAKE_INTEGER (&val, new_types[i]);
      error_code = db_put_internal (obj, "type_id", &val);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}

      DB_MAKE_VARCHAR (&val, 9, (char *) type_names[i],
		       strlen (type_names[i]));
      error_code = db_put_internal (obj, "type_name", &val);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
    }

error:
  au_enable (au_save);

  if (error_code == NO_ERROR)
    {
      error_code = db_commit_transaction ();
    }
  return error_code;
}

static int
new_db_serial (void)
{
  MOP class_mop, public_user, dba_user;
  SM_TEMPLATE *def;
  char domain_string[32];
  unsigned char num[DB_NUMERIC_BUF_SIZE];	/* Copy of a DB_C_NUMERIC */
  DB_VALUE default_value;
  int error_code = NO_ERROR;
  const char *index_col_names[] = { "name", NULL };

  def = smt_def_class (CT_SERIAL_NAME);
  if (def == NULL)
    {
      return er_errid ();
    }

  error_code = smt_add_attribute (def, "name", "string", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code =
    smt_add_attribute (def, "owner", au_get_user_class_name (), NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sprintf (domain_string, "numeric(%d,0)", DB_MAX_NUMERIC_PRECISION);
  numeric_coerce_int_to_num (1, num);
  DB_MAKE_NUMERIC (&default_value, num, DB_MAX_NUMERIC_PRECISION, 0);

  error_code = smt_add_attribute (def, "current_val", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }
  error_code = smt_set_attribute_default (def, "current_val", 0,
					  &default_value);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "increment_val", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }
  error_code = smt_set_attribute_default (def, "increment_val", 0,
					  &default_value);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "max_val", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "min_val", domain_string, NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  DB_MAKE_INTEGER (&default_value, 0);

  error_code = smt_add_attribute (def, "cyclic", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }
  error_code = smt_set_attribute_default (def, "cyclic", 0, &default_value);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "started", "integer", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }
  error_code = smt_set_attribute_default (def, "started", 0, &default_value);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "class_name", "string", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_attribute (def, "att_name", "string", NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = smt_add_class_method (def, "change_serial_owner",
				     "au_change_serial_owner_method");
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* add index */
  error_code = dbt_add_constraint (def, DB_CONSTRAINT_PRIMARY_KEY, NULL,
				   index_col_names, 0);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = dbt_constrain_non_null (def, "current_val", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = dbt_constrain_non_null (def, "increment_val", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = dbt_constrain_non_null (def, "max_val", 0, 1);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = dbt_constrain_non_null (def, "min_val", 0, 1);
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

  error_code = smt_add_attribute (def, "page_id", "integer", NULL);
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

  error_code = smt_add_attribute (def, "required_page_id", "integer", NULL);
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
add_new_classes (void)
{
  MOP class_mop;
  int au_save;
  int error_code = NO_ERROR;
  char sql_stmt[LINE_MAX];

  au_save = au_disable ();

  /*
   * change old db_serial to new db_serial
   */
  class_mop = db_find_class (CT_SERIAL_NAME);
  if (class_mop == NULL)
    {
      error_code = er_errid ();
      goto error;
    }

  error_code = db_rename_class (class_mop, "old_" CT_SERIAL_NAME);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  error_code = new_db_serial ();
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  snprintf (sql_stmt, LINE_MAX, "INSERT INTO %s SELECT * FROM %s;",
	    CT_SERIAL_NAME, "old_" CT_SERIAL_NAME);
  error_code = db_execute (sql_stmt, NULL, NULL);
  if (error_code < 0)
    {
      goto error;
    }

  error_code = db_drop_class (class_mop);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  /*
   * add db_ha_info class
   */

  error_code = new_db_ha_apply_info ();
  if (error_code != NO_ERROR)
    {
      goto error;
    }

error:
  au_enable (au_save);

  if (error_code == NO_ERROR)
    {
      db_commit_transaction ();
    }
  else
    {
      db_abort_transaction ();
    }
  return error_code;
}

static int
read_rebuilt_index_list (FILE * log_fp, REBUILT_INDEX_LIST ** done_list)
{
  char class_name[1024];
  char con_name[1024];
  REBUILT_INDEX_LIST *node;

  while (!feof (log_fp))
    {
      fscanf (log_fp, "%1023s %1023s\n", class_name, con_name);
      node = (REBUILT_INDEX_LIST *) malloc (sizeof (REBUILT_INDEX_LIST));
      if (node == NULL)
	{
	  return 0;
	}
      node->class_name = strdup (class_name);
      node->con_name = strdup (con_name);
      node->next = *done_list;

      *done_list = node;
    }

  return 1;
}

static bool
is_already_done (REBUILT_INDEX_LIST * done_list,
		 const char *class_name, const char *con_name)
{
  REBUILT_INDEX_LIST *node;

  node = done_list;
  while (node)
    {
      if (strcmp (node->class_name, class_name) == 0 &&
	  strcmp (node->con_name, con_name) == 0)
	{
	  return true;
	}
      node = node->next;
    }

  return false;
}

static void
free_rebuilt_index_list (REBUILT_INDEX_LIST * done_list)
{
  REBUILT_INDEX_LIST *node, *next;

  node = done_list;
  while (node)
    {
      next = node->next;

      free (node->class_name);
      free (node->con_name);
      free (node);

      node = next;
    }
}

static bool
is_index_need_to_rebuild (DB_CONSTRAINT * cons)
{
  DB_ATTRIBUTE **attr;
  DB_TYPE type;
  int cnt = 0;

  for (attr = cons->attributes; *attr; attr++)
    {
      cnt++;
    }

  if (cnt < 2)
    {
      return false;
    }

  for (attr = cons->attributes; *attr; attr++)
    {
      type = db_attribute_type (*attr);
      if (type == DB_TYPE_DOUBLE)
	{
	  return true;
	}
    }

  return false;
}

static int
rebuild_index (char *log_file, bool is_log_file_exist)
{
  FILE *log_fp;
  REBUILT_INDEX_LIST *done_list = NULL;

  DB_OBJLIST *classes, *class;
  const char *classname;
  int is_partitioned;
  DB_CONSTRAINT *cons;
  DB_ATTRIBUTE **attr;

  DB_CONSTRAINT_TYPE save_con_type;
  char save_con_name[1024];
  char **save_att_names = NULL;

  char *constraint_name_to_rebuild[1024];
  int rebuild_count;
  int attr_count, i, j, error;

  if (is_log_file_exist)
    {
      log_fp = fopen (log_file, "r+");
      if (log_fp == NULL)
	{
	  return ER_FAILED;
	}

      if (!feof (log_fp))
	{
	  read_rebuilt_index_list (log_fp, &done_list);
	}
    }
  else
    {
      log_fp = fopen (log_file, "w");
      if (log_fp == NULL)
	{
	  return ER_FAILED;
	}

      done_list = NULL;
    }

  classes = db_fetch_all_classes (DB_FETCH_CLREAD_INSTREAD);
  for (class = classes; class != NULL; class = class->next)
    {
      if (db_is_vclass (class->op))
	{
	  continue;
	}

      classname = db_get_class_name (class->op);

      if (do_is_partitioned_subclass (&is_partitioned, classname, NULL))
	{
	  continue;
	}

      rebuild_count = 0;
      for (cons = db_get_constraints (class->op); cons; cons = cons->next)
	{
	  if (!SM_IS_CONSTRAINT_INDEX_FAMILY (cons->type))
	    {
	      continue;
	    }

	  if (done_list && is_already_done (done_list, classname, cons->name))
	    {
	      continue;
	    }

	  if (is_index_need_to_rebuild (cons))
	    {
	      constraint_name_to_rebuild[rebuild_count] = strdup (cons->name);
	      rebuild_count++;
	    }
	}

      for (i = 0; i < rebuild_count; i++)
	{
	  for (cons = db_get_constraints (class->op); cons; cons = cons->next)
	    {
	      if (strcmp (constraint_name_to_rebuild[i], cons->name))
		{
		  continue;
		}

	      attr_count = 0;
	      for (attr = cons->attributes; *attr; attr++)
		{
		  (attr_count)++;
		}

	      save_att_names =
		(char **) malloc ((attr_count + 1) * sizeof (char *));
	      if (save_att_names == NULL)
		{
		  goto error;
		}

	      for (j = 0, attr = cons->attributes; *attr; j++, attr++)
		{
		  save_att_names[j] = strdup (db_attribute_name (*attr));
		}
	      save_att_names[j] = NULL;

	      strcpy (save_con_name, cons->name);
	      save_con_type = db_constraint_type (cons);
	      error =
		db_drop_constraint (class->op, cons->type, cons->name,
				    NULL, false);

	      if (error != NO_ERROR)
		{
		  printf ("drop constraint(%s, %s): error = %d\n",
			  classname, save_con_name, error);
		  goto error;
		}

	      db_commit_transaction ();

	      error = db_add_constraint (class->op, save_con_type,
					 save_con_name,
					 (const char **) save_att_names,
					 false);
	      if (error != NO_ERROR)
		{
		  printf ("add constraint(%s, %s): error = %d\n",
			  classname, save_con_name, error);
		  goto error;
		}

	      db_commit_transaction ();

	      if (save_att_names != NULL)
		{
		  for (j = 0; j < attr_count; j++)
		    {
		      free_and_init (save_att_names[j]);
		    }
		  free_and_init (save_att_names);
		}

	      printf ("%s: %s\n", classname, save_con_name);
	      fprintf (log_fp, "%s %s\n", classname, save_con_name);
	      fflush (log_fp);

	      break;
	    }

	  free_and_init (constraint_name_to_rebuild[i]);
	}
    }

  fclose (log_fp);

  if (done_list != NULL)
    {
      free_rebuilt_index_list (done_list);
    }

  return NO_ERROR;

error:
  fclose (log_fp);

  for (i = 0; i < rebuild_count; i++)
    {
      if (constraint_name_to_rebuild[i] != NULL)
	{
	  free_and_init (constraint_name_to_rebuild[i]);
	}
    }

  if (save_att_names != NULL)
    {
      for (j = 0; j < attr_count; j++)
	{
	  free_and_init (save_att_names[j]);
	}
      free_and_init (save_att_names);
    }

  if (done_list != NULL)
    {
      free_rebuilt_index_list (done_list);
    }
  return ER_FAILED;
}

static int
get_db_full_path (const char *db_name, char *db_full_path)
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

int
main (int argc, char *argv[])
{
  const char *db_name;
  char db_full_path[PATH_MAX], release_string[REL_MAX_RELEASE_LENGTH];
  float compat_level;
  bool is_already_fixed = false;
  struct stat stat_buf;
  char log_file[256];
  bool is_log_file_exist;

  if (argc != 2)
    {
      printf ("usage: migrate_r20 <database name>\n");
      return EXIT_FAILURE;
    }

  db_name = argv[1];

  if (check_database_name (db_name))
    {
      return EXIT_FAILURE;
    }

  if (get_db_full_path (db_name, db_full_path) != NO_ERROR)
    {
      return EXIT_FAILURE;
    }

  if (get_disk_compatibility_level (db_full_path, &compat_level,
				    release_string) != NO_ERROR)
    {
      return EXIT_FAILURE;
    }

  if (compat_level < 8.0f)
    {
      printf ("can't migrate database (too old database) : %s.\n",
	      release_string);
      return EXIT_FAILURE;
    }

  printf ("CUBRID Migration: %s to %s\n", release_string,
	  rel_release_string ());

  is_already_fixed = (compat_level == rel_disk_compatible ());

  if (!is_already_fixed)
    {
      printf ("start to fix volume header\n");
      if (fix_volume_and_log_header (db_full_path) != NO_ERROR)
	{
	  if (undo_fix_volume_and_log_header () != NO_ERROR)
	    {
	      printf ("recovering volume header fails.\n");
	    }

	  return EXIT_FAILURE;
	}
    }

  /* tuning system parameters */
  sysprm_set_to_default (PRM_NAME_PB_NBUFFERS, true);
  sysprm_set_force (PRM_NAME_JAVA_STORED_PROCEDURE, "no");

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("dba", NULL);

  if (db_restart (argv[0], TRUE, db_name) != NO_ERROR)
    {
      printf ("%s\n", db_error_string (3));

      if (!is_already_fixed && undo_fix_volume_and_log_header () != NO_ERROR)
	{
	  printf ("recovering volume header fails.\n");
	}

      return EXIT_FAILURE;
    }

  if (!is_already_fixed)
    {
      printf ("start to fix file header\n");
      if (fix_all_file_header () != NO_ERROR
	  || add_new_data_type () != NO_ERROR
	  || add_new_classes () != NO_ERROR
	  || fix_all_glo_data () != NO_ERROR)
	{
	  if (undo_fix_all_glo_data () != NO_ERROR)
	    {
	      printf ("recovering file header fails.\n");
	      return EXIT_FAILURE;
	    }
	  if (undo_fix_all_file_header () != NO_ERROR)
	    {
	      printf ("recovering file header fails.\n");
	      return EXIT_FAILURE;
	    }

	  printf ("%s\n", db_error_string (3));
	  db_shutdown ();

	  if (undo_fix_volume_and_log_header () != NO_ERROR)
	    {
	      printf ("recovering volume header fails.\n");
	      return EXIT_FAILURE;
	    }

	  return EXIT_FAILURE;
	}

      if (glo_set_dirty_all () != NO_ERROR)
	{
	  return EXIT_FAILURE;
	}
      glo_free_undo_info ();
      free_file_header_undo_list ();
      free_volume_header_undo_list ();
    }

  sprintf (log_file, "migrate_r20_%s.log", db_name);
  is_log_file_exist = (stat (log_file, &stat_buf) == 0);

  if (!is_already_fixed || is_log_file_exist)
    {
      if (is_log_file_exist)
	{
	  printf ("continue to rebuild index\n");
	}
      else
	{
	  printf ("start to rebuild index\n");
	}

      if (rebuild_index (log_file, is_log_file_exist) != NO_ERROR)
	{
	  printf ("%s\n", db_error_string (3));
	  db_shutdown ();
	  return EXIT_FAILURE;
	}
    }

  db_shutdown ();
  printf ("migration success\n");

  return EXIT_SUCCESS;
}
