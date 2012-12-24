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
 * large_object_directory.c - Large Object Directory Manager
 */

#ident "$Id$"

#include "config.h"

#include <string.h>
#include <assert.h>

#include "porting.h"
#include "error_manager.h"
#include "recovery.h"
#include "memory_alloc.h"
#include "storage_common.h"
#include "object_representation.h"
#include "page_buffer.h"
#include "slotted_page.h"
#include "file_manager.h"
#include "large_object_directory.h"
#if defined(SERVER_MODE)
#include "thread.h"
#endif /* SERVER_MODE */

#define LARGEOBJMGR_MAX_DIRMAP_ENTRY_CNT                              \
        ((ssize_t) ((DB_PAGESIZE - sizeof (LARGEOBJMGR_DIRHEADER))    \
                    / sizeof(LARGEOBJMGR_DIRMAP_ENTRY)))

#define LARGEOBJMGR_MAX_DIRENTRY_CNT \
        ((ssize_t) ((DB_PAGESIZE - sizeof (LARGEOBJMGR_DIRHEADER))   \
                    / sizeof (LARGEOBJMGR_DIRENTRY)))

/* Set dir. index entry to be empty */
#define LARGEOBJMGR_SET_EMPTY_DIRMAP_ENTRY(ent) \
do {\
  VPID_SET_NULL(&((ent)->vpid));\
  (ent)->length = 0;\
} while(0)

/* Empty/Unused dir. index entry */
#define LARGEOBJMGR_ISEMPTY_DIRMAP_ENTRY(ent)  \
  (VPID_ISNULL(&((ent)->vpid)) && (ent)->length == 0)

/* Copy directory index entry */
#define LARGEOBJMGR_COPY_DIRMAP_ENTRY(ent1,ent2)\
do {\
  (ent1)->vpid.volid = (ent2)->vpid.volid;\
  (ent1)->vpid.pageid = (ent2)->vpid.pageid;\
  (ent1)->length = (ent2)->length;\
} while(0)

/* Initialize directory state information */
#define LARGEOBJMGR_INIT_DIRSTATE(ds)   \
do {\
     (ds)->opr_mode = -1; \
     (ds)->index_level = -1; \
     (ds)->pos = S_BEFORE; \
     (ds)->tot_length = -1; \
     (ds)->lo_offset = -1;\
     (ds)->goodvpid_fordata.volid = NULL_VOLID;\
     (ds)->goodvpid_fordata.pageid = NULL_PAGEID;\
     (ds)->firstdir.pgptr = NULL;\
     (ds)->firstdir.idx = -1;\
     (ds)->firstdir.idxptr = NULL;\
     (ds)->firstdir.hdr   = NULL;\
     (ds)->curdir.pgptr = NULL;\
     (ds)->curdir.idx = -1; \
     (ds)->curdir.idxptr = NULL;\
     (ds)->curdir.hdr   = NULL;\
     } while(0)


/* Scan modes */
#define LARGEOBJMGR_SCAN_MODE(mode) \
  ((mode) == LARGEOBJMGR_READ_MODE     || (mode) == LARGEOBJMGR_DELETE_MODE || \
   (mode) == LARGEOBJMGR_TRUNCATE_MODE || (mode) == LARGEOBJMGR_COMPRESS_MODE)

/* Recovery structures */

typedef enum
{
  L_DIR_PAGE,			/* Directory Page */
  L_ROOT_PAGE,			/* Directory Root Page */
  L_IND_PAGE			/* Directory Index Page */
} PAGE_TYPE;			/* Recovery Page Types */

typedef struct largeobjmgr_rcv_state LARGEOBJMGR_RCV_STATE;
struct largeobjmgr_rcv_state
{
  PAGE_TYPE page_type;		/* Recovery page type */
  int ent_ind;			/* Entry index */
  union
  {
    LARGEOBJMGR_DIRENTRY d;	/* Directory entry content */
    LARGEOBJMGR_DIRMAP_ENTRY i;	/* Index page entry content */
  } ent;
  LARGEOBJMGR_DIRHEADER dir_head;	/* Directory header content */
};				/* Directory recovery state structure */

static LARGEOBJMGR_DIRENTRY *largeobjmgr_direntry (PAGE_PTR pgptr, int n);
static LARGEOBJMGR_DIRENTRY *largeobjmgr_first_direntry (PAGE_PTR pgptr);
static LARGEOBJMGR_DIRMAP_ENTRY *largeobjmgr_dirmap_entry (PAGE_PTR pgptr,
							   int n);
static LARGEOBJMGR_DIRMAP_ENTRY *largeobjmgr_first_dirmap_entry (PAGE_PTR
								 pgptr);
static bool largeobjmgr_initdir_newpage (THREAD_ENTRY * thread_p,
					 const VFID * vfid,
					 const FILE_TYPE file_type,
					 const VPID * vpid,
					 INT32 ignore_npages, void *xds);
static PAGE_PTR largeobjmgr_dir_allocpage (THREAD_ENTRY * thread_p,
					   LARGEOBJMGR_DIRSTATE * ds,
					   VPID * vpid);
static void largeobjmgr_dirheader_dump (FILE * fp,
					LARGEOBJMGR_DIRHEADER * dir_hdr);
static void largeobjmgr_direntry_dump (FILE * fp,
				       LARGEOBJMGR_DIRENTRY * ent_ptr,
				       int idx);
static void largeobjmgr_dirmap_dump (FILE * fp,
				     LARGEOBJMGR_DIRMAP_ENTRY * map_ptr,
				     int idx);
#if defined (CUBRID_DEBUG)
static void largeobjmgr_dir_pgdump (FILE * fp, PAGE_PTR curdir_pgptr);
#endif
static int largeobjmgr_dir_pgcnt (THREAD_ENTRY * thread_p,
				  LARGEOBJMGR_DIRSTATE * ds);
static PAGE_PTR largeobjmgr_dir_get_prvpg (THREAD_ENTRY * thread_p,
					   LARGEOBJMGR_DIRSTATE * ds,
					   VPID * first_vpid, VPID * vpid,
					   VPID * prev_vpid);
static int largeobjmgr_dir_pgcompress (THREAD_ENTRY * thread_p,
				       LARGEOBJMGR_DIRSTATE * ds);
static int largeobjmgr_dir_pgsplit (THREAD_ENTRY * thread_p,
				    LARGEOBJMGR_DIRSTATE * ds, int n);
static int largeobjmgr_dir_pgremove (THREAD_ENTRY * thread_p,
				     LARGEOBJMGR_DIRSTATE * ds,
				     VPID * p_vpid);
static int largeobjmgr_dir_pgadd (THREAD_ENTRY * thread_p,
				  LARGEOBJMGR_DIRSTATE * ds);
static PAGE_PTR largeobjmgr_dir_search (THREAD_ENTRY * thread_p,
					PAGE_PTR first_dir_pg, INT64 offset,
					int *curdir_idx, INT64 * lo_offset);
static void largeobjmgr_firstdir_update (THREAD_ENTRY * thread_p,
					 LARGEOBJMGR_DIRSTATE * ds,
					 int delta_len, int delta_slot_cnt);
static int largeobjmgr_firstdir_map_shrink (THREAD_ENTRY * thread_p,
					    LOID * loid, PAGE_PTR page_ptr);
static int largeobjmgr_firstdir_map_create (THREAD_ENTRY * thread_p,
					    LARGEOBJMGR_DIRSTATE * ds);
static void largeobjmgr_get_rcv_state (LARGEOBJMGR_DIRSTATE * ds,
				       PAGE_TYPE page_type,
				       LARGEOBJMGR_RCV_STATE * rcv);

/* Do not create a directory index until number of directory pages
   reaches this limit. */
static int LARGEOBJMGR_DIR_PG_CNT_INDEX_CREATE_LIMIT = 4;

/*
 * largeobjmgr_direntry () - access to N-th directory entry
 *   return: entry pointer
 *   pgptr(in):
 *   n(in): N-th idx
 */
static LARGEOBJMGR_DIRENTRY *
largeobjmgr_direntry (PAGE_PTR pgptr, int n)
{
  return (LARGEOBJMGR_DIRENTRY *) ((char *) pgptr +
				   sizeof (LARGEOBJMGR_DIRHEADER)) + n;
}

/*
 * largeobjmgr_direntry () - access to first directory entry
 *   return: entry pointer
 *   pgptr(in):
 */
static LARGEOBJMGR_DIRENTRY *
largeobjmgr_first_direntry (PAGE_PTR pgptr)
{
  return largeobjmgr_direntry (pgptr, 0);
}

/*
 * largeobjmgr_dirmap_entry () - access to N-th directory index entry
 *   return: entry pointer
 *   pgptr(in):
 *   n(in): N-th idx
 */
static LARGEOBJMGR_DIRMAP_ENTRY *
largeobjmgr_dirmap_entry (PAGE_PTR pgptr, int n)
{
  return (LARGEOBJMGR_DIRMAP_ENTRY *) ((char *) pgptr +
				       sizeof (LARGEOBJMGR_DIRHEADER)) + n;
}

/*
 * largeobjmgr_first_dirmap_entry () - access to first directory index entry
 *   return: entry pointer
 *   pgptr(in):
 */
static LARGEOBJMGR_DIRMAP_ENTRY *
largeobjmgr_first_dirmap_entry (PAGE_PTR pgptr)
{
  return largeobjmgr_dirmap_entry (pgptr, 0);
}

/*
 * largeobjmgr_reset_last_alloc_page () - Reset the last allocated LO file page
 *                              information
 *   return: void
 *   ds(in): Directory state structure
 *   vpid_ptr(in): New last allocated directory page identifier (or NULL)
 */
void
largeobjmgr_reset_last_alloc_page (THREAD_ENTRY * thread_p,
				   LARGEOBJMGR_DIRSTATE * ds, VPID * vpid_ptr)
{
  LOG_DATA_ADDR addr;
  VPID vpid;

  /* put an UNDO log for the old last_pg field */

  addr.vfid = &ds->firstdir.hdr->loid.vfid;
  addr.pgptr = ds->firstdir.pgptr;
  addr.offset = offsetof (LARGEOBJMGR_DIRHEADER, goodvpid_fordata);

  log_append_undo_data (thread_p, RVLOM_DIR_PG_REGION, &addr, sizeof (VPID),
			&ds->firstdir.hdr->goodvpid_fordata);

  /* update directory root page header for the last allocated directory
     page field. */
  if (vpid_ptr)
    {
      vpid = *vpid_ptr;
    }
  else
    {
      VPID_SET_NULL (&vpid);
    }

  ds->firstdir.hdr->goodvpid_fordata = vpid;
  ds->goodvpid_fordata = vpid;

  /* put an REDO log for the new last_alloc_vpid field */
  log_append_redo_data (thread_p, RVLOM_DIR_PG_REGION, &addr, sizeof (VPID),
			&vpid);

  pgbuf_set_dirty (thread_p, ds->firstdir.pgptr, DONT_FREE);
}

/*
 * largeobjmgr_initdir_newpage () - Initialize a newly allocated directory page
 *   return:
 *   vfid(in): File where the new page belongs
 *   file_type(in):
 *   vpid(in): The new page
 *   ignore_npages(in): Number of contiguous allocated pages
 *   xds(in): Directory State Structure
 */
static bool
largeobjmgr_initdir_newpage (THREAD_ENTRY * thread_p, const VFID * vfid,
			     const FILE_TYPE file_type, const VPID * vpid,
			     INT32 ignore_npages, void *xds)
{
  LARGEOBJMGR_DIRHEADER *new_head;
  LARGEOBJMGR_DIRENTRY *ent_ptr;
  LOG_DATA_ADDR addr;
  int k;

  addr.vfid = vfid;
  addr.offset = 0;

  /* fetch newly allocated directory page and initialize it */
  addr.pgptr = pgbuf_fix (thread_p, vpid, NEW_PAGE, PGBUF_LATCH_WRITE,
			  PGBUF_UNCONDITIONAL_LATCH);
  if (addr.pgptr == NULL)
    {
      return false;
    }

  /* Copy the header and modify fields related to this page */
  new_head = (LARGEOBJMGR_DIRHEADER *) addr.pgptr;
  *new_head = *(((LARGEOBJMGR_DIRSTATE *) xds)->firstdir.hdr);

  /* initialize directory page header entries */
  new_head->index_level = 0;
  new_head->pg_tot_length = 0;
  new_head->pg_act_idxcnt = 0;
  new_head->pg_lastact_idx = -1;
  VPID_SET_NULL (&new_head->next_vpid);

  /* initialize directory entries */
  for (k = 0, ent_ptr = largeobjmgr_first_direntry (addr.pgptr);
       k < LARGEOBJMGR_MAX_DIRENTRY_CNT; k++, ent_ptr++)
    {
      LARGEOBJMGR_SET_EMPTY_DIRENTRY (ent_ptr);
    }

  /* Log only a redo record for page allocation and initialization,
     the caller is responsible for the undo log. */
  log_append_redo_data (thread_p, RVLOM_DIR_NEW_PG, &addr,
			sizeof (LARGEOBJMGR_DIRHEADER), new_head);

  /* set page dirty */
  pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
  addr.pgptr = NULL;

  return true;
}

/*
 * largeobjmgr_dir_allocpage () - Allocate a new directory page and initialize the page
 *                        to be an empty directory page
 *   return: New directory page fetched or NULL
 *   ds(in): Directory State Structure
 *   vpid(in): Set to the newly allocated page identifier
 */
static PAGE_PTR
largeobjmgr_dir_allocpage (THREAD_ENTRY * thread_p, LARGEOBJMGR_DIRSTATE * ds,
			   VPID * vpid)
{
  PAGE_PTR page_ptr = NULL;
  LOG_DATA_ADDR addr;
  VPID *vpid_ptr;

  /* allocate new directory page */
  vpid_ptr = pgbuf_get_vpid_ptr (ds->curdir.pgptr);
  if (file_alloc_pages (thread_p, &ds->curdir.hdr->loid.vfid, vpid, 1,
			vpid_ptr, largeobjmgr_initdir_newpage, ds) == NULL)
    {
      return NULL;
    }

  /*
   * NOTE: we fetch the page as old since it was initialized during the
   * allocation by largeobjmgr_initdir_newpage, we want the current
   * contents of the page.
   */
  page_ptr = pgbuf_fix (thread_p, vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			PGBUF_UNCONDITIONAL_LATCH);
  if (page_ptr == NULL)
    {
      (void) file_dealloc_page (thread_p, &ds->curdir.hdr->loid.vfid, vpid);
      return NULL;
    }

  /* Need undo log, for cases of unexpected rollback, but only
     if the file is not new */
  if (file_is_new_file (thread_p,
			&(ds->curdir.hdr->loid.vfid)) == FILE_OLD_FILE)
    {
      addr.vfid = &ds->curdir.hdr->loid.vfid;
      addr.pgptr = page_ptr;
      addr.offset = -1;
      log_append_undo_data (thread_p, RVLOM_GET_NEWPAGE, &addr, sizeof (VFID),
			    &ds->curdir.hdr->loid.vfid);
    }

  return page_ptr;
}

/*
 * largeobjmgr_dir_pgcnt () - Return current number of directory pages including the
 *                    index page if any
 *   return: current number of directory pages, or -1 on error
 *   ds(in): Directory State Structure
 */
static int
largeobjmgr_dir_pgcnt (THREAD_ENTRY * thread_p, LARGEOBJMGR_DIRSTATE * ds)
{
  LARGEOBJMGR_DIRHEADER *dir_hdr;
  PAGE_PTR page_ptr = NULL;
  VPID curdir_vpid;
  VPID next_vpid;
  int pgcnt = 0;

  pgbuf_get_vpid (ds->curdir.pgptr, &curdir_vpid);
  page_ptr = ds->firstdir.pgptr;
  dir_hdr = ds->curdir.hdr;

  next_vpid = dir_hdr->next_vpid;
  pgcnt = 1;

  /* Follow link list of directory pages */
  while (!VPID_ISNULL (&next_vpid))
    {
      if (page_ptr != ds->firstdir.pgptr && page_ptr != ds->curdir.pgptr)
	{
	  pgbuf_unfix_and_init (thread_p, page_ptr);
	}

      if (VPID_EQ (&next_vpid, &curdir_vpid))
	{
	  page_ptr = ds->curdir.pgptr;
	}
      else
	{
	  page_ptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE,
				PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
	  if (page_ptr == NULL)
	    {
	      return -1;
	    }
	}

      pgcnt++;
      dir_hdr = (LARGEOBJMGR_DIRHEADER *) page_ptr;
      next_vpid = dir_hdr->next_vpid;
    }

  if (page_ptr != ds->firstdir.pgptr && page_ptr != ds->curdir.pgptr)
    {
      pgbuf_unfix_and_init (thread_p, page_ptr);
    }

  return pgcnt;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * largeobjmgr_dir_get_vpids () - Return the list of directory page identifiers
 *   return:
 *   loid(in): Large object identifier
 *   dir_vpid_list(out): the array of directory page identifier
 *   dir_vpid_cnt(out): the directory page identifier count
 *   index_exists(out): true if first page is the index page
 *
 * Note: The area allocated for dir_vpid_list should be FREED by the caller.
 *       This routine is used only by DUMP routines.
 */
int
largeobjmgr_dir_get_vpids (THREAD_ENTRY * thread_p, LOID * loid,
			   VPID ** dir_vpid_list, int *dir_vpid_cnt,
			   bool * index_exists)
{
  LARGEOBJMGR_DIRHEADER *dir_hdr;
  VPID *vpid_list = NULL;
  int vpid_cnt;
  int vpid_ind;
  VPID next_vpid;
  PAGE_PTR page_ptr = NULL;

  *dir_vpid_list = NULL;
  *dir_vpid_cnt = 0;
  *index_exists = false;

  /* fetch the LO root page */
  page_ptr = pgbuf_fix (thread_p, &loid->vpid, OLD_PAGE, PGBUF_LATCH_READ,
			PGBUF_UNCONDITIONAL_LATCH);
  if (page_ptr == NULL)
    {
      return ER_FAILED;
    }

  dir_hdr = (LARGEOBJMGR_DIRHEADER *) page_ptr;

  next_vpid = dir_hdr->next_vpid;

  if (dir_hdr->index_level > 0)
    {
      *index_exists = true;
      vpid_cnt = dir_hdr->pg_act_idxcnt + 1;
    }
  else
    {
      *index_exists = false;
      vpid_cnt = LARGEOBJMGR_DIR_PG_CNT_INDEX_CREATE_LIMIT;
    }

  pgbuf_unfix_and_init (thread_p, page_ptr);

  vpid_list = (VPID *) malloc (vpid_cnt * sizeof (VPID));
  if (vpid_list == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      vpid_cnt * sizeof (VPID));
      return ER_FAILED;
    }

  vpid_list[0] = loid->vpid;
  vpid_ind = 1;

  while (!VPID_ISNULL (&next_vpid))
    {
      if (vpid_ind == vpid_cnt)
	{
	  vpid_cnt += 10;
	  vpid_list = (VPID *) realloc (vpid_list, vpid_cnt * sizeof (VPID));
	  if (vpid_list == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1, vpid_cnt * sizeof (VPID));
	      return ER_FAILED;
	    }
	}

      vpid_list[vpid_ind] = next_vpid;
      vpid_ind++;

      page_ptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			    PGBUF_UNCONDITIONAL_LATCH);
      if (page_ptr == NULL)
	{
	  free_and_init (vpid_list);
	  return ER_FAILED;
	}

      dir_hdr = (LARGEOBJMGR_DIRHEADER *) page_ptr;
      next_vpid = dir_hdr->next_vpid;
      pgbuf_unfix_and_init (thread_p, page_ptr);
    }

  *dir_vpid_list = vpid_list;
  *dir_vpid_cnt = vpid_ind;

  return NO_ERROR;
}
#endif

/*
 * largeobjmgr_dir_get_prvpg () - Starting from the first page, search to find previous
 *                        directory page
 *   return:
 *   ds(in): Directory State Structure
 *   first_vpid(in): First directory page to start search
 *   vpid(in): The directory page for which previous is searched
 *   prev_vpid(in): Previous directory page identifier
 */
static PAGE_PTR
largeobjmgr_dir_get_prvpg (THREAD_ENTRY * thread_p, LARGEOBJMGR_DIRSTATE * ds,
			   VPID * first_vpid, VPID * vpid, VPID * prev_vpid)
{
  LARGEOBJMGR_DIRHEADER *dir_hdr;
  PAGE_PTR page_ptr = NULL;
  VPID next_vpid;
  VPID *vpid_ptr;

  *prev_vpid = *first_vpid;

  vpid_ptr = pgbuf_get_vpid_ptr (ds->firstdir.pgptr);
  if (VPID_EQ (prev_vpid, vpid_ptr))
    {
      page_ptr = ds->firstdir.pgptr;
    }
  else
    {
      page_ptr = pgbuf_fix (thread_p, prev_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			    PGBUF_UNCONDITIONAL_LATCH);
      if (page_ptr == NULL)
	{
	  VPID_SET_NULL (prev_vpid);
	  return NULL;
	}
    }

  dir_hdr = (LARGEOBJMGR_DIRHEADER *) page_ptr;
  next_vpid = dir_hdr->next_vpid;

  while (!VPID_EQ (&next_vpid, vpid))
    {
      if (page_ptr != ds->firstdir.pgptr)
	{
	  pgbuf_unfix_and_init (thread_p, page_ptr);
	}

      *prev_vpid = next_vpid;
      page_ptr = pgbuf_fix (thread_p, prev_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			    PGBUF_UNCONDITIONAL_LATCH);
      if (page_ptr == NULL)
	{
	  VPID_SET_NULL (prev_vpid);
	  return NULL;
	}

      dir_hdr = (LARGEOBJMGR_DIRHEADER *) page_ptr;
      next_vpid = dir_hdr->next_vpid;
    }

  return page_ptr;
}

/*
 * largeobjmgr_dir_pgcompress () - Compress the current directory page entries
 *   return: NO_ERROR
 *   ds(in): Directory state structure
 */
static int
largeobjmgr_dir_pgcompress (THREAD_ENTRY * thread_p,
			    LARGEOBJMGR_DIRSTATE * ds)
{
  LARGEOBJMGR_DIRSTATE_POS ds_pos;
  SCAN_CODE lo_scan;
  int empty_index;
  LARGEOBJMGR_DIRENTRY *empty_ptr;
  int full_index;
  LARGEOBJMGR_DIRENTRY *full_ptr;
  int last_act_ind;
  LARGEOBJMGR_DIRENTRY *deleted_entries = NULL;
  int deleted_cnt;
  int n;
  INT64 begin_lo_offset;
  LOG_DATA_ADDR addr;
  int ret = NO_ERROR;

  addr.vfid = &ds->firstdir.hdr->loid.vfid;

  /* save current lo_offset which points to the beginning of page */
  begin_lo_offset = ds->lo_offset;

  /* Make an in-page compression of the current dir. page */
  last_act_ind = ds->curdir.hdr->pg_lastact_idx;
  if (last_act_ind > ds->curdir.hdr->pg_act_idxcnt - 1)
    {
      /* log the affected region of the dir. page for UNDO purposes. */
      addr.pgptr = ds->curdir.pgptr;
      addr.offset = 0;

      log_append_undo_data (thread_p, RVLOM_DIR_PG_REGION, &addr,
			    sizeof (LARGEOBJMGR_DIRHEADER) +
			    ((ds->curdir.hdr->pg_lastact_idx +
			      1) * sizeof (LARGEOBJMGR_DIRENTRY)),
			    ds->curdir.pgptr);

      empty_index = full_index = 0;
      empty_ptr = full_ptr = largeobjmgr_first_direntry (ds->curdir.pgptr);

      while (1)
	{
	  /* find next empty entry */
	  while (empty_index < last_act_ind
		 && !LARGEOBJMGR_ISEMPTY_DIRENTRY (empty_ptr))
	    {
	      empty_index++;
	      empty_ptr++;
	    }

	  /* Did we find an empty entry ? */
	  if (empty_index == last_act_ind)
	    {
	      break;
	    }

	  /* find next full entry */
	  if (full_index <= empty_index)
	    {
	      full_index = empty_index + 1;
	      full_ptr = empty_ptr + 1;
	    }
	  while (full_index <= last_act_ind
		 && LARGEOBJMGR_ISEMPTY_DIRENTRY (full_ptr))
	    {
	      full_index++;
	      full_ptr++;
	    }

	  /* Did we find any full entry ? */
	  if (full_index > last_act_ind)
	    {
	      break;
	    }

	  /* Are we merging holes ? */
	  if (LARGEOBJMGR_ISHOLE_DIRENTRY (full_ptr) && empty_index > 0
	      && LARGEOBJMGR_ISHOLE_DIRENTRY (empty_ptr - 1))
	    {
	      /* merge two holes */
	      (empty_ptr - 1)->u.length += full_ptr->u.length;
	      LARGEOBJMGR_SET_EMPTY_DIRENTRY (full_ptr);

	      /* update page header */
	      ds->curdir.hdr->tot_slot_cnt--;
	      ds->curdir.hdr->pg_act_idxcnt--;

	      /* update root page header if different from directory page */
	      if (ds->firstdir.pgptr != ds->curdir.pgptr)
		{
		  /* put an UNDO log for the old tot_slot_cnt field */
		  addr.pgptr = ds->firstdir.pgptr;
		  addr.offset =
		    (PGLENGTH) ((char *) &ds->firstdir.hdr->tot_slot_cnt -
				(char *) ds->firstdir.pgptr);

		  log_append_undo_data (thread_p, RVLOM_DIR_PG_REGION, &addr,
					sizeof (int),
					(char *) &ds->firstdir.hdr->
					tot_slot_cnt);

		  ds->firstdir.hdr->tot_slot_cnt--;

		  /* put an REDO log for the new tot_slot_cnt field */
		  log_append_redo_data (thread_p, RVLOM_DIR_PG_REGION, &addr,
					sizeof (int),
					(char *) &ds->firstdir.hdr->
					tot_slot_cnt);

		  /* set root page dirty */
		  pgbuf_set_dirty (thread_p, ds->firstdir.pgptr, DONT_FREE);
		}

	      /* Is this the last entry ? */
	      if (full_index == last_act_ind)
		{
		  last_act_ind = empty_index - 1;
		  break;
		}
	    }
	  else
	    {
	      /* move full entry to empty entry */
	      *empty_ptr = *full_ptr;
	      LARGEOBJMGR_SET_EMPTY_DIRENTRY (full_ptr);

	      /* Is this the last entry ? */
	      if (full_index == last_act_ind)
		{		/* last entry processed */
		  last_act_ind = empty_index;
		  break;
		}
	      empty_index++;
	      empty_ptr++;
	    }
	  full_index++;
	  full_ptr++;
	}

      /* update directory header */
      ds->curdir.hdr->pg_lastact_idx = last_act_ind;

      /* log the affected region of the dir. page for REDO purposes. */
      addr.pgptr = ds->curdir.pgptr;
      addr.offset = 0;
      log_append_redo_data (thread_p, RVLOM_DIR_PG_REGION, &addr,
			    sizeof (LARGEOBJMGR_DIRHEADER) +
			    (ds->curdir.hdr->pg_lastact_idx + 1) *
			    sizeof (LARGEOBJMGR_DIRENTRY), ds->curdir.pgptr);

      /* setdirty directory page */
      pgbuf_set_dirty (thread_p, ds->curdir.pgptr, DONT_FREE);
    }

  /* allocate area for new entries to be added to directory page */
  deleted_cnt = LARGEOBJMGR_MAX_DIRENTRY_CNT - ds->curdir.hdr->pg_act_idxcnt;
  if (deleted_cnt > 0)
    {
      deleted_entries =
	(LARGEOBJMGR_DIRENTRY *) malloc (deleted_cnt *
					 sizeof (LARGEOBJMGR_DIRENTRY));
      if (deleted_entries == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, deleted_cnt * sizeof (LARGEOBJMGR_DIRENTRY));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      /* set directory state to point to after last active entry */
      ds->curdir.idx = last_act_ind + 1;
      ds->curdir.idxptr =
	largeobjmgr_direntry (ds->curdir.pgptr, ds->curdir.idx);
      ds->lo_offset += ds->curdir.hdr->pg_tot_length;

      /* get the next n entries from the directory */
      largeobjmgr_dir_get_pos (ds, &ds_pos);	/* save directory state */
      ds->opr_mode = LARGEOBJMGR_DELETE_MODE;	/* set ds to delete mode */
      for (n = 0; n < deleted_cnt; n++)
	{
	  lo_scan = largeobjmgr_dir_scan_next (thread_p, ds);
	  if (lo_scan != S_SUCCESS)
	    {
	      if (lo_scan == S_ERROR)
		{
		  free_and_init (deleted_entries);
		  return ER_FAILED;
		}
	    }

	  /* get next directory entry */
	  deleted_entries[n] = *(ds->curdir.idxptr);

	  /* delete next directory entry */
	  ret = largeobjmgr_delete_dir (thread_p, ds);
	  if (ret != NO_ERROR)
	    {
	      free_and_init (deleted_entries);
	      return ret;
	    }
	}

      ret = largeobjmgr_dir_put_pos (thread_p, ds, &ds_pos);
      if (ret != NO_ERROR)
	{
	  free_and_init (deleted_entries);
	  return ret;
	}

      /* insert new entries in the current directory position */
      if (n > 0)
	{
	  ret = largeobjmgr_dir_insert (thread_p, ds, deleted_entries, n);
	  if (ret != NO_ERROR)
	    {
	      free_and_init (deleted_entries);
	      return ret;
	    }
	}

      free_and_init (deleted_entries);

      /* put directory state back to beginning state */
      ret = largeobjmgr_dir_put_pos (thread_p, ds, &ds_pos);
      if (ret != NO_ERROR)
	{
	  return ret;
	}

      ds->curdir.idx = 0;	/* point to the page beginning entry */
      ds->curdir.idxptr = largeobjmgr_first_direntry (ds->curdir.pgptr);
      ds->lo_offset = begin_lo_offset;
    }

  return ret;
}

/*
 * largeobjmgr_dir_pgsplit () - Split the current full directory page into two pages
 *                      and keep the current state of the directory for
 *                      following insertions
 *   return: NO_ERROR
 *   ds(in): Directory state structure
 *   n(in): Estimated number of entries to be inserted after split (or -1)
 *
 * Note: This routine assumes that current directory page is FULL.
 */
static int
largeobjmgr_dir_pgsplit (THREAD_ENTRY * thread_p, LARGEOBJMGR_DIRSTATE * ds,
			 int n)
{
  LARGEOBJMGR_DIRHEADER *dir_hdr;
  PAGE_PTR page_ptr = NULL;
  VPID vpid;
  VPID next_vpid;
  int mid_ind;
  int split_ind;
  int split_cnt;
  int split_size;
  char *spilt_ptr;
  int delta;
  LARGEOBJMGR_DIRENTRY *temp_entry_p;
  int mv_ent_cnt;
  int k;
  LOG_DATA_ADDR addr;

  addr.vfid = &ds->firstdir.hdr->loid.vfid;

  /* get a new page for split operation */
  page_ptr = largeobjmgr_dir_allocpage (thread_p, ds, &vpid);
  if (page_ptr == NULL)
    {
      return ER_FAILED;
    }

  dir_hdr = (LARGEOBJMGR_DIRHEADER *) page_ptr;

  /* find the optimum split index for the originial page
     Are we appending at the end ? */
  if (VPID_ISNULL (&ds->curdir.hdr->next_vpid)
      && ds->curdir.idx >= (LARGEOBJMGR_MAX_DIRENTRY_CNT - 1))
    {
      /* We are likely appending stuff at the end.
         Don't split but allocate a new page */
      split_ind = ds->curdir.idx;
    }
  else
    {
      mid_ind = (int) (LARGEOBJMGR_MAX_DIRENTRY_CNT / 2);
      if (n <= mid_ind)
	{
	  split_ind = mid_ind;
	}
      else
	{
	  if (ds->curdir.idx < mid_ind)
	    {
	      split_ind = ds->curdir.idx + 1;
	    }
	  else
	    {
	      split_ind = ds->curdir.idx;
	    }

	}
    }

  split_cnt = (LARGEOBJMGR_MAX_DIRENTRY_CNT - split_ind);
  split_size = split_cnt * sizeof (LARGEOBJMGR_DIRENTRY);

  delta = 0;
  if (split_cnt > 0)
    {
      spilt_ptr =
	(char *) (largeobjmgr_direntry (ds->curdir.pgptr, split_ind));

      /* move the entries after split point to the new directory page */
      memcpy ((char *) (largeobjmgr_first_direntry (page_ptr)),
	      (char *) spilt_ptr, split_size);

      /* log the affected region of the NEW page for REDO purposes. */
      addr.pgptr = page_ptr;
      addr.offset = sizeof (LARGEOBJMGR_DIRHEADER);
      log_append_redo_data (thread_p, RVLOM_DIR_PG_REGION, &addr, split_size,
			    (char *) (largeobjmgr_first_direntry (page_ptr)));

      /* log the affected region of the ORIGINAL directory page
         for UNDO purposes */
      addr.pgptr = ds->curdir.pgptr;
      addr.offset =
	(PGLENGTH) ((char *) spilt_ptr - (char *) ds->curdir.pgptr);

      log_append_undo_data (thread_p, RVLOM_DIR_PG_REGION, &addr, split_size,
			    spilt_ptr);

      /* empty original entries, get delta length change */
      for (k = split_ind, temp_entry_p = (LARGEOBJMGR_DIRENTRY *) spilt_ptr;
	   k < LARGEOBJMGR_MAX_DIRENTRY_CNT; k++, temp_entry_p++)
	{
	  delta += LARGEOBJMGR_DIRENTRY_LENGTH (temp_entry_p);
	  LARGEOBJMGR_SET_EMPTY_DIRENTRY (temp_entry_p);
	}

      /* log the affected region of the ORIGINAL dir. page for REDO purposes. */
      log_append_redo_data (thread_p, RVLOM_DIR_PG_REGION, &addr, split_size,
			    spilt_ptr);
    }

  /* Update the new directory page header */
  dir_hdr->pg_tot_length = delta;
  dir_hdr->pg_act_idxcnt = split_cnt;
  dir_hdr->pg_lastact_idx = dir_hdr->pg_act_idxcnt - 1;
  dir_hdr->next_vpid = ds->curdir.hdr->next_vpid;
  next_vpid = ds->curdir.hdr->next_vpid;

  /* put a REDO log for the NEW directory header */
  addr.pgptr = page_ptr;
  addr.offset = 0;
  log_append_redo_data (thread_p, RVLOM_DIR_PG_REGION, &addr,
			sizeof (LARGEOBJMGR_DIRHEADER), page_ptr);

  /* put an UNDO log for the ORIGINAL directory header */
  addr.pgptr = ds->curdir.pgptr;
  addr.offset = 0;

  log_append_undo_data (thread_p, RVLOM_DIR_PG_REGION, &addr,
			sizeof (LARGEOBJMGR_DIRHEADER), ds->curdir.pgptr);

  /* update ORIGINAL directory page header */
  ds->curdir.hdr->pg_tot_length -= delta;
  ds->curdir.hdr->pg_act_idxcnt -= split_cnt;
  ds->curdir.hdr->pg_lastact_idx = ds->curdir.hdr->pg_act_idxcnt - 1;
  ds->curdir.hdr->next_vpid = vpid;

  /* put an REDO log for the old directory header */
  log_append_redo_data (thread_p, RVLOM_DIR_PG_REGION, &addr,
			sizeof (LARGEOBJMGR_DIRHEADER), ds->curdir.pgptr);

  /* setdirty pages */
  pgbuf_set_dirty (thread_p, page_ptr, DONT_FREE);
  pgbuf_set_dirty (thread_p, ds->curdir.pgptr, DONT_FREE);

  /* Now locate current directory index for future insertions */
  if (ds->curdir.idx < split_ind)
    {
      /* directory state is the same */
      pgbuf_unfix_and_init (thread_p, page_ptr);
    }
  else
    {
      /* point to the new directory page */
      if (ds->curdir.pgptr != ds->firstdir.pgptr)
	{
	  pgbuf_unfix_and_init (thread_p, ds->curdir.pgptr);
	}

      ds->curdir.pgptr = page_ptr;
      ds->curdir.hdr = (LARGEOBJMGR_DIRHEADER *) ds->curdir.pgptr;
      ds->curdir.idx -= split_ind;
      ds->curdir.idxptr = largeobjmgr_direntry (ds->curdir.pgptr,
						ds->curdir.idx);
    }

  /* If we have an index map, insert a map index entry, if needed */
  if (ds->index_level > 0)
    {
      /*
       * We have a map index.
       * check if map page has space and current map entry points to
       * this page.
       */
      if (ds->firstdir.hdr->pg_act_idxcnt < LARGEOBJMGR_MAX_DIRMAP_ENTRY_CNT
	  && ((ds->firstdir.idx < (ds->firstdir.hdr->pg_act_idxcnt - 1)
	       && VPID_EQ (&((ds->firstdir.idxptr + 1)->vpid), &next_vpid))
	      || (ds->firstdir.idx == (ds->firstdir.hdr->pg_act_idxcnt - 1)
		  && VPID_ISNULL (&next_vpid))))
	{

	  /* find entry count to be moved */
	  mv_ent_cnt = ((ds->firstdir.hdr->pg_act_idxcnt - 1)
			- ds->firstdir.idx);

	  /* log the affected region of the index page for UNDO purposes. */
	  addr.pgptr = ds->firstdir.pgptr;
	  addr.offset = (PGLENGTH) ((char *) ds->firstdir.idxptr -
				    (char *) ds->firstdir.pgptr);

	  log_append_undo_data (thread_p, RVLOM_DIR_PG_REGION, &addr,
				(mv_ent_cnt +
				 2) * sizeof (LARGEOBJMGR_DIRMAP_ENTRY),
				(char *) ds->firstdir.idxptr);

	  /* update current index entry */
	  ds->firstdir.idxptr->length -= delta;

	  /* open space for the new entry */
	  memmove (ds->firstdir.idxptr + 2, ds->firstdir.idxptr + 1,
		   mv_ent_cnt * sizeof (LARGEOBJMGR_DIRMAP_ENTRY));

	  /* insert the new entry */
	  (ds->firstdir.idxptr + 1)->vpid = vpid;
	  (ds->firstdir.idxptr + 1)->length = delta;

	  /* log the affected region of the index page for REDO purposes. */
	  log_append_redo_data (thread_p, RVLOM_DIR_PG_REGION, &addr,
				((mv_ent_cnt + 2)
				 * sizeof (LARGEOBJMGR_DIRMAP_ENTRY)),
				(char *) ds->firstdir.idxptr);

	  /* put an UNDO log for the old index directory header */
	  addr.pgptr = ds->firstdir.pgptr;
	  addr.offset = 0;

	  log_append_undo_data (thread_p, RVLOM_DIR_PG_REGION, &addr,
				sizeof (LARGEOBJMGR_DIRHEADER),
				ds->firstdir.pgptr);

	  /* update index page header */
	  ds->firstdir.hdr->pg_act_idxcnt++;
	  ds->firstdir.hdr->pg_lastact_idx =
	    ds->firstdir.hdr->pg_act_idxcnt - 1;

	  /* put an REDO log for the index directory header */
	  log_append_redo_data (thread_p, RVLOM_DIR_PG_REGION, &addr,
				sizeof (LARGEOBJMGR_DIRHEADER),
				ds->firstdir.pgptr);

	  /* set index page dirty */
	  pgbuf_set_dirty (thread_p, ds->firstdir.pgptr, DONT_FREE);

	  /* update directory index state */
	  if (ds->curdir.pgptr == page_ptr)
	    {
	      ds->firstdir.idx++;
	      ds->firstdir.idxptr++;
	    }
	}
    }

  return NO_ERROR;
}

/*
 * largeobjmgr_dir_pgremove () - Remove the current directory page
 *   return: NO_ERROR
 *   ds(in): Directory state structure
 *   p_vpid(in): Previous page identifier to current dir.
 *
 * Note: Current directory is assumed to be empty and current directory state
 *       points to deleted (empty) entry in the page. After removal, directory
 *       state points to the first previous entry if any. If there is no
 *       previous entry, it puts the directory state to point to before the LO.
 *
         The root page of directory (index or first directory page) is not
 *       deallocated
 */
static int
largeobjmgr_dir_pgremove (THREAD_ENTRY * thread_p, LARGEOBJMGR_DIRSTATE * ds,
			  VPID * p_vpid)
{
  LARGEOBJMGR_DIRHEADER *prevdir_hdr;
  VPID cur_vpid;
  VPID prev_vpid;
  VPID next_vpid;
  PAGE_PTR prev_pgptr = NULL;
  VPID first_vpid;
  VPID *vpid_ptr;
  int mv_ent_cnt;
  LOG_DATA_ADDR addr;
  LARGEOBJMGR_DIRMAP_ENTRY *ent_ptr;
  int ret = NO_ERROR;

  addr.vfid = &ds->firstdir.hdr->loid.vfid;

  /* If the directory page is the root page, don't remove the page
     leave the state pointing to the current entry. */
  if (ds->curdir.pgptr == ds->firstdir.pgptr)
    {
      return NO_ERROR;
    }

  /* get current directory page identifier */
  pgbuf_get_vpid (ds->curdir.pgptr, &cur_vpid);
  next_vpid = ds->curdir.hdr->next_vpid;

  /* find previous directory page, if any */
  VPID_SET_NULL (&prev_vpid);
  prev_pgptr = NULL;

  if (p_vpid != NULL && !VPID_ISNULL (p_vpid))
    {
      prev_vpid = *p_vpid;
      vpid_ptr = pgbuf_get_vpid_ptr (ds->firstdir.pgptr);
      if (VPID_EQ (&prev_vpid, vpid_ptr))
	{
	  prev_pgptr = ds->firstdir.pgptr;
	}
      else
	{
	  prev_pgptr = pgbuf_fix (thread_p, &prev_vpid, OLD_PAGE,
				  PGBUF_LATCH_WRITE,
				  PGBUF_UNCONDITIONAL_LATCH);
	  if (prev_pgptr == NULL)
	    {
	      return ER_FAILED;
	    }
	}
    }
  else
    {
      VPID_SET_NULL (&first_vpid);
      if (ds->index_level > 0)
	{
	  if (VPID_EQ (&ds->firstdir.idxptr->vpid, &cur_vpid))
	    {
	      if (ds->firstdir.idx > 0)
		{
		  first_vpid = (ds->firstdir.idxptr - 1)->vpid;
		}
	    }
	  else
	    {
	      first_vpid = ds->firstdir.idxptr->vpid;
	    }
	}
      else
	{
	  pgbuf_get_vpid (ds->firstdir.pgptr, &first_vpid);
	}

      if (!VPID_ISNULL (&first_vpid))
	{
	  prev_pgptr =
	    largeobjmgr_dir_get_prvpg (thread_p, ds, &first_vpid, &cur_vpid,
				       &prev_vpid);
	  if (prev_pgptr == NULL)
	    {
	      return ER_FAILED;
	    }
	}
    }

  prevdir_hdr = (LARGEOBJMGR_DIRHEADER *) prev_pgptr;

  /* link previous page to the following one, if any */
  if (!VPID_ISNULL (&prev_vpid))
    {
      /* put an UNDO log for the old next_vpid field */
      addr.pgptr = prev_pgptr;
      addr.offset = (PGLENGTH) ((char *) (&prevdir_hdr->next_vpid) -
				(char *) prev_pgptr);

      log_append_undoredo_data (thread_p, RVLOM_DIR_PG_REGION, &addr,
				sizeof (VPID), sizeof (VPID),
				&prevdir_hdr->next_vpid, &next_vpid);

      prevdir_hdr->next_vpid = next_vpid;
      pgbuf_set_dirty (thread_p, prev_pgptr, DONT_FREE);
    }

  /* deallocate current empty directory page */
  pgbuf_unfix_and_init (thread_p, ds->curdir.pgptr);
  ds->curdir.pgptr = NULL;
  ds->curdir.hdr = NULL;
  ds->curdir.idx = -1;
  ret = file_dealloc_page (thread_p, &ds->firstdir.hdr->loid.vfid, &cur_vpid);
  if (ret != NO_ERROR)
    {
      if (!VPID_ISNULL (&prev_vpid))
	{
	  pgbuf_unfix_and_init (thread_p, prev_pgptr);
	}
      return ER_FAILED;
    }

  /*
   * set directory state to point to the previous entry/page, if any
   */
  if (VPID_ISNULL (&prev_vpid))
    {
      /* in this case, directory must have index and the first directory
       * page has been deallocated. Then, just set the directory
       * state to before LO.
       */
      ds->pos = S_BEFORE;
      ds->lo_offset = 0;
    }
  else
    {
      ds->curdir.pgptr = prev_pgptr;
      ds->curdir.hdr = (LARGEOBJMGR_DIRHEADER *) ds->curdir.pgptr;

      if (ds->opr_mode == LARGEOBJMGR_DIRCOMPRESS_MODE)
	{
	  /* point to first entry */
	  ds->curdir.idx = 0;
	  ds->curdir.idxptr = largeobjmgr_first_direntry (ds->curdir.pgptr);
	  ds->lo_offset -= ds->curdir.hdr->pg_tot_length;
	}
      else
	{
	  /* point to last entry */
	  ds->curdir.idx = ds->curdir.hdr->pg_lastact_idx;
	  ds->curdir.idxptr = largeobjmgr_direntry (ds->curdir.pgptr,
						    ds->curdir.idx);
	  ds->lo_offset -= LARGEOBJMGR_DIRENTRY_LENGTH (ds->curdir.idxptr);
	}
    }

  /* Update the map index to reflect any changes */
  if (ds->index_level > 0 && VPID_EQ (&ds->firstdir.idxptr->vpid, &cur_vpid))
    {
      /* process current index entry */
      if (ds->firstdir.idxptr->length == 0)
	{
	  /* find entry count to be moved */
	  mv_ent_cnt =
	    ds->firstdir.hdr->pg_act_idxcnt - (ds->firstdir.idx + 1);

	  /* log the affected region of the index page for UNDO purposes. */
	  addr.pgptr = ds->firstdir.pgptr;
	  addr.offset = (PGLENGTH) ((char *) ds->firstdir.idxptr -
				    (char *) ds->firstdir.pgptr);

	  log_append_undo_data (thread_p, RVLOM_DIR_PG_REGION, &addr,
				((mv_ent_cnt + 1)
				 * sizeof (LARGEOBJMGR_DIRMAP_ENTRY)),
				(char *) ds->firstdir.idxptr);

	  /* delete current index entry */
	  if (ds->firstdir.idx < ds->firstdir.hdr->pg_lastact_idx)
	    {
	      memmove (ds->firstdir.idxptr, ds->firstdir.idxptr + 1,
		       mv_ent_cnt * sizeof (LARGEOBJMGR_DIRMAP_ENTRY));
	    }
	  ent_ptr = largeobjmgr_dirmap_entry (ds->firstdir.pgptr,
					      ds->firstdir.hdr->
					      pg_lastact_idx);
	  LARGEOBJMGR_SET_EMPTY_DIRMAP_ENTRY (ent_ptr);

	  /* log the affected region of the index page for REDO purposes. */
	  log_append_redo_data (thread_p, RVLOM_DIR_PG_REGION, &addr,
				((mv_ent_cnt + 1)
				 * sizeof (LARGEOBJMGR_DIRMAP_ENTRY)),
				(char *) ds->firstdir.idxptr);

	  /* put an UNDO log for the old index directory header */
	  addr.pgptr = ds->firstdir.pgptr;
	  addr.offset = 0;

	  log_append_undo_data (thread_p, RVLOM_DIR_PG_REGION, &addr,
				sizeof (LARGEOBJMGR_DIRHEADER),
				ds->firstdir.pgptr);

	  /* update index page header */
	  ds->firstdir.hdr->pg_act_idxcnt--;
	  ds->firstdir.hdr->pg_lastact_idx =
	    ds->firstdir.hdr->pg_act_idxcnt - 1;

	  /* put an REDO log for the index directory header */
	  log_append_redo_data (thread_p, RVLOM_DIR_PG_REGION, &addr,
				sizeof (LARGEOBJMGR_DIRHEADER),
				ds->firstdir.pgptr);
	}
      else
	{
	  /* update current index entry
	     log the affected region of the index page for UNDO purposes. */
	  addr.pgptr = ds->firstdir.pgptr;
	  addr.offset = (PGLENGTH) ((char *) ds->firstdir.idxptr -
				    (char *) ds->firstdir.pgptr);

	  log_append_undoredo_data (thread_p, RVLOM_DIR_PG_REGION, &addr,
				    sizeof (LARGEOBJMGR_DIRMAP_ENTRY),
				    sizeof (LARGEOBJMGR_DIRMAP_ENTRY),
				    &ds->firstdir.idxptr->vpid, &next_vpid);

	  /* make entry point to the following one */
	  ds->firstdir.idxptr->vpid = next_vpid;

	}
      /* set index page dirty */
      pgbuf_set_dirty (thread_p, ds->firstdir.pgptr, DONT_FREE);

      /* set index entry to point to previous entry */
      if (ds->firstdir.idx > 0)
	{
	  ds->firstdir.idx--;
	  ds->firstdir.idxptr--;
	}
    }

  return ret;
}

/*
 * largeobjmgr_dir_pgadd () - Add a new page to the directory at the end
 *   return: NO_ERROR
 *   ds(in): Directory state structure
 *
 * Note: The directory state is set to point to the first entry of the new
 *       page. The directory page is added to the end of existing directory
 *       pages, if any.
 */
static int
largeobjmgr_dir_pgadd (THREAD_ENTRY * thread_p, LARGEOBJMGR_DIRSTATE * ds)
{
  VPID next_vpid;
  PAGE_PTR page_ptr = NULL;
  int dir_pgcnt;
  LARGEOBJMGR_RCV_STATE rcv;
  LOG_DATA_ADDR addr;
  int ret = NO_ERROR;

  addr.vfid = &ds->firstdir.hdr->loid.vfid;

  /* allocate a new last directory page */
  page_ptr = largeobjmgr_dir_allocpage (thread_p, ds, &next_vpid);
  if (page_ptr == NULL)
    {
      return ER_FAILED;
    }

  if (ds->curdir.pgptr != NULL)
    {
      /* link current directory page to the last page
         put an UNDO log for the old next_vpid field */
      addr.pgptr = ds->curdir.pgptr;
      addr.offset = (PGLENGTH) ((char *) &ds->curdir.hdr->next_vpid -
				(char *) ds->curdir.pgptr);

      log_append_undoredo_data (thread_p, RVLOM_DIR_PG_REGION, &addr,
				sizeof (VPID), sizeof (VPID),
				&ds->curdir.hdr->next_vpid, &next_vpid);

      ds->curdir.hdr->next_vpid = next_vpid;

      /* setdirty directory page and free if it is not the root page */
      if (ds->curdir.pgptr != ds->firstdir.pgptr)
	{
	  pgbuf_set_dirty (thread_p, ds->curdir.pgptr, FREE);
	  ds->curdir.pgptr = NULL;
	}
      else
	{
	  pgbuf_set_dirty (thread_p, ds->curdir.pgptr, DONT_FREE);
	}
    }

  ds->curdir.pgptr = page_ptr;
  ds->curdir.hdr = (LARGEOBJMGR_DIRHEADER *) ds->curdir.pgptr;
  ds->curdir.idx = 0;
  ds->curdir.idxptr = largeobjmgr_first_direntry (ds->curdir.pgptr);

  /* Do we have a map index ? */
  if (ds->index_level == 0)
    {
      /* create a map index for the directory, if needed */
      dir_pgcnt = largeobjmgr_dir_pgcnt (thread_p, ds);
      if (dir_pgcnt < 0)
	{
	  return ER_FAILED;
	}

      if (dir_pgcnt >= LARGEOBJMGR_DIR_PG_CNT_INDEX_CREATE_LIMIT)
	{
	  ret = largeobjmgr_firstdir_map_create (thread_p, ds);
	  if (ret != NO_ERROR)
	    {
	      return ret;
	    }
	}
    }
  else
    {
      /* Map index already exists.
         Insert a map index for the newly created directory page */
      if (ds->firstdir.hdr->pg_act_idxcnt < LARGEOBJMGR_MAX_DIRMAP_ENTRY_CNT)
	{
	  if (ds->firstdir.hdr->pg_act_idxcnt == 0)
	    {
	      ds->firstdir.idx = 0;
	      ds->firstdir.idxptr =
		largeobjmgr_first_dirmap_entry (ds->firstdir.pgptr);
	    }
	  else
	    {
	      ds->firstdir.idx++;
	      ds->firstdir.idxptr++;
	    }
	}
      else
	{
	  /* index page is full */
	  ret = largeobjmgr_firstdir_map_shrink (thread_p,
						 &ds->firstdir.hdr->loid,
						 ds->firstdir.pgptr);
	  if (ret != NO_ERROR)
	    {
	      return ret;
	    }
	}

      /* put an UNDO log to save current index page state */
      largeobjmgr_get_rcv_state (ds, L_IND_PAGE, &rcv);
      addr.pgptr = ds->firstdir.pgptr;
      addr.offset = -1;

      log_append_undo_data (thread_p, RVLOM_DIR_RCV_STATE, &addr,
			    sizeof (LARGEOBJMGR_RCV_STATE), &rcv);

      /* insert the new entry */
      ds->firstdir.idxptr->vpid = next_vpid;
      ds->firstdir.idxptr->length = 0;

      /* update index page header */
      ds->firstdir.hdr->pg_act_idxcnt++;
      ds->firstdir.hdr->pg_lastact_idx = ds->firstdir.hdr->pg_act_idxcnt - 1;

      /* put a REDO log to save new index page state */
      largeobjmgr_get_rcv_state (ds, L_IND_PAGE, &rcv);
      log_append_redo_data (thread_p, RVLOM_DIR_RCV_STATE, &addr,
			    sizeof (LARGEOBJMGR_RCV_STATE), &rcv);

      /* set index page dirty */
      pgbuf_set_dirty (thread_p, ds->firstdir.pgptr, DONT_FREE);
    }

  /* advance the directory index entry, if needed */
  if (ds->index_level > 0
      && ds->firstdir.idx < ds->firstdir.hdr->pg_lastact_idx
      && VPID_EQ (&next_vpid, &((ds->firstdir.idxptr + 1)->vpid)))
    {
      ds->firstdir.idx++;
      ds->firstdir.idxptr++;
    }

  return ret;
}

/*
 * largeobjmgr_dirheader_dump () -
 *   return: void
 *   dir_hdr(in):
 */
static void
largeobjmgr_dirheader_dump (FILE * fp, LARGEOBJMGR_DIRHEADER * dir_hdr)
{
  fprintf (fp, "Page Total Length: %lld\n",
	   (long long) dir_hdr->pg_tot_length);
  fprintf (fp, "Active Entry Count: %d, Last Active Entry Index; %d\n",
	   dir_hdr->pg_act_idxcnt, dir_hdr->pg_lastact_idx);
  fprintf (fp, "Next_VPID = %d|%d\n",
	   dir_hdr->next_vpid.volid, dir_hdr->next_vpid.pageid);
}

/*
 * largeobjmgr_direntry_dump () -
 *   return: void
 *   ent_ptr(in):
 *   idx(in):
 */
static void
largeobjmgr_direntry_dump (FILE * fp, LARGEOBJMGR_DIRENTRY * ent_ptr, int idx)
{
  if (LARGEOBJMGR_ISEMPTY_DIRENTRY (ent_ptr))
    {
      fprintf (fp, "[%3d:  _EMPTY_ENTRY_  ] ", idx);
    }
  else if (LARGEOBJMGR_ISHOLE_DIRENTRY (ent_ptr))
    {
      fprintf (fp, "[%3d: _HOLE_ENTRY_ , %d] ", idx, ent_ptr->u.length);
    }
  else
    {
      fprintf (fp, "[%3d: {%2d|%4d}, %4d, %4d] ",
	       idx, ent_ptr->u.vpid.volid,
	       ent_ptr->u.vpid.pageid, ent_ptr->slotid, ent_ptr->length);
    }
}

/*
 * largeobjmgr_dirmap_dump () -
 *   return: void
 *   map_ptr(in):
 *   idx(in):
 */
static void
largeobjmgr_dirmap_dump (FILE * fp, LARGEOBJMGR_DIRMAP_ENTRY * map_ptr,
			 int idx)
{
  fprintf (fp, "[%3d: {%2d, %4d} , %4lld] ",
	   idx, map_ptr->vpid.volid, map_ptr->vpid.pageid,
	   (long long) map_ptr->length);
}

#if defined (CUBRID_DEBUG)
/*
 * largeobjmgr_dir_pgdump () - Dump given directory page content
 *   return: void
 *   curdir_pgptr(in): Directory page pointer
 *
 * Note: This routine is provided only for debugging purposes.
 */
static void
largeobjmgr_dir_pgdump (FILE * fp, PAGE_PTR curdir_pgptr)
{
  LARGEOBJMGR_DIRHEADER *dir_hdr;
  VPID *vpid;
  LARGEOBJMGR_DIRENTRY *curdir_idxptr;
  LARGEOBJMGR_DIRMAP_ENTRY *firstdir_idxptr;
  int n;
  int k;

  dir_hdr = (LARGEOBJMGR_DIRHEADER *) curdir_pgptr;
  vpid = pgbuf_get_vpid_ptr (curdir_pgptr);

  if (dir_hdr->index_level > 0)
    {
      fprintf (fp, "DIRECTORY MAP INDEX PAGE:\n");
    }
  else
    {
      fprintf (fp, "DIRECTORY PAGE:\n");
    }

  fprintf (fp, "VPID = %d|%d\n", vpid->volid, vpid->pageid);

  largeobjmgr_dirheader_dump (fp, dir_hdr);

  n = 0;
  if (dir_hdr->index_level > 0)
    {
      /* Map index directory */
      for (k = 0,
	   firstdir_idxptr = largeobjmgr_first_dirmap_entry (curdir_pgptr);
	   k < LARGEOBJMGR_MAX_DIRMAP_ENTRY_CNT; k++, firstdir_idxptr++)
	{
	  if ((n++ % 3) == 0)
	    {
	      fprintf (fp, "\n");
	    }

	  largeobjmgr_dirmap_dump (fp, firstdir_idxptr, k);
	}
    }
  else
    {
      /* Regular directory */
      for (k = 0, curdir_idxptr = largeobjmgr_first_direntry (curdir_pgptr);
	   k < LARGEOBJMGR_MAX_DIRENTRY_CNT; k++, curdir_idxptr++)
	{
	  if ((n++ % 3) == 0)
	    {
	      fprintf (fp, "\n");
	    }

	  largeobjmgr_direntry_dump (fp, curdir_idxptr, k);
	}
    }

  fprintf (fp, "\n--------------------------------------------\n\n");
}
#endif

/*
 * largeobjmgr_dir_search () - Start searching directory from given first page to
 *                     locate the entry that contains "offset"
 *   return: Directory page, or NULL on failure
 *   first_dir_pg(in): First directory page pointer to start search
 *   offset(in): Offset of data searched for with respect to first page
 *   curdir_idx(out): Directory page slot index
 *   lo_offset(out): beginning offset of pointed directory slot
 */
static PAGE_PTR
largeobjmgr_dir_search (THREAD_ENTRY * thread_p, PAGE_PTR first_dir_pg,
			INT64 offset, int *curdir_idx, INT64 * lo_offset)
{
  LARGEOBJMGR_DIRHEADER *dir_hdr;
  PAGE_PTR page_ptr = NULL;
  VPID next_vpid;
  LARGEOBJMGR_DIRENTRY *ent_ptr;

  /* initializations */
  *curdir_idx = 0;
  *lo_offset = 0;
  page_ptr = first_dir_pg;
  dir_hdr = (LARGEOBJMGR_DIRHEADER *) page_ptr;

  /* Scan the link list of directory pages until we find the directory page
     that contains and index entry to the desired offset. */
  while ((*lo_offset + dir_hdr->pg_tot_length) <= offset)
    {
      /* move to the next page */
      *lo_offset += dir_hdr->pg_tot_length;
      next_vpid = dir_hdr->next_vpid;
      if (page_ptr != first_dir_pg)
	{
	  pgbuf_unfix_and_init (thread_p, page_ptr);
	}

      page_ptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			    PGBUF_UNCONDITIONAL_LATCH);
      if (page_ptr == NULL)
	{
	  *curdir_idx = -1;
	  *lo_offset = 0;
	  return NULL;
	}

      dir_hdr = (LARGEOBJMGR_DIRHEADER *) page_ptr;
    }

  /* Now find the entry in this directory page */
  for (ent_ptr = largeobjmgr_first_direntry (page_ptr);
       (LARGEOBJMGR_ISEMPTY_DIRENTRY (ent_ptr)
	|| (*lo_offset + LARGEOBJMGR_DIRENTRY_LENGTH (ent_ptr)) <= offset);
       ent_ptr++)
    {
      *lo_offset += LARGEOBJMGR_DIRENTRY_LENGTH (ent_ptr);
      *curdir_idx += 1;
    }

  return page_ptr;
}

/*
 * largeobjmgr_dir_scan_next () - Scan directory
 *   return: scan status
 *   ds(in): Directory state structure
 *
 * Note:  If directory state is in READ, WRITE, DELETE, TRUNCATE modes:
 *          - move to the next non_empty directory entry, if any.
 *          - return S_END when no more non_empty entries.
 *        If directory state is in INSERT, APPEND modes:
 *          - move to the next directory entry, if any.
 *          - create a new directory page and point to the first entry
 *            when no more entries.
 */
SCAN_CODE
largeobjmgr_dir_scan_next (THREAD_ENTRY * thread_p, LARGEOBJMGR_DIRSTATE * ds)
{
  VPID next_vpid;
  int last_ind;

  switch (ds->opr_mode)
    {
    case LARGEOBJMGR_READ_MODE:
    case LARGEOBJMGR_WRITE_MODE:
    case LARGEOBJMGR_DELETE_MODE:
    case LARGEOBJMGR_TRUNCATE_MODE:
    case LARGEOBJMGR_COMPRESS_MODE:
      {
	/* Find next non-empty entry */
	switch (ds->pos)
	  {
	  case S_BEFORE:
	    {
	      /*
	       * Likely, starting the scan of the LO
	       *
	       * Currently, we are pointing to before the first LO position.
	       * Move to first LO position (offset = 0)
	       */
	      ds->lo_offset = 0;
	      if (ds->tot_length == 0)
		{
		  /* LO is empty */
		  if (ds->opr_mode == LARGEOBJMGR_WRITE_MODE)
		    {
		      ds->pos = S_AFTER;
		      ds->opr_mode = LARGEOBJMGR_APPEND_MODE;
		    }
		  return S_END;
		}

	      /* Point to first entry of LO (offset = 0) */
	      if (ds->index_level > 0)
		{
		  /* has index
		     first entry is in the first page pointed by index */
		  ds->firstdir.idx = 0;
		  ds->firstdir.idxptr =
		    largeobjmgr_first_dirmap_entry (ds->firstdir.pgptr);

		  ds->curdir.pgptr = pgbuf_fix (thread_p,
						&ds->firstdir.idxptr->vpid,
						OLD_PAGE, PGBUF_LATCH_WRITE,
						PGBUF_UNCONDITIONAL_LATCH);
		  if (ds->curdir.pgptr == NULL)
		    {
		      return S_ERROR;
		    }
		}
	      else
		{
		  ds->curdir.pgptr = ds->firstdir.pgptr;
		}

	      ds->curdir.hdr = (LARGEOBJMGR_DIRHEADER *) ds->curdir.pgptr;
	      ds->curdir.idx = 0;
	      ds->curdir.idxptr =
		largeobjmgr_first_direntry (ds->curdir.pgptr);
	      if (largeobjmgr_skip_empty_entries (thread_p, ds) == S_ERROR)
		{
		  return S_ERROR;
		}

	      ds->pos = S_ON;
	      break;
	    }

	  case S_ON:
	    {
	      /* We are currently within the LO */
	      if ((ds->lo_offset +
		   LARGEOBJMGR_DIRENTRY_LENGTH (ds->curdir.idxptr)) >=
		  ds->tot_length)
		{
		  /* End of Scan */
		  if (ds->opr_mode == LARGEOBJMGR_WRITE_MODE)
		    {
		      /* Continue beyond the LO, change to Append Mode */
		      ds->opr_mode = LARGEOBJMGR_APPEND_MODE;
		      if (LARGEOBJMGR_DIRENTRY_LENGTH (ds->curdir.idxptr)
			  == 0)
			{
			  ds->pos = S_AFTER;
			}
		      else
			{
			  ds->pos = S_ON;
			}
		    }
		  else
		    {
		      ds->pos = S_AFTER;
		    }

		  return S_END;
		}

	      /* advance to the next position */
	      ds->lo_offset +=
		LARGEOBJMGR_DIRENTRY_LENGTH (ds->curdir.idxptr);
	      ds->curdir.idx++;
	      ds->curdir.idxptr++;

	      /* skip empty entries */
	      if (ds->curdir.idx >= ds->curdir.hdr->pg_act_idxcnt
		  || LARGEOBJMGR_ISEMPTY_DIRENTRY (ds->curdir.idxptr))
		{
		  if (largeobjmgr_skip_empty_entries (thread_p, ds)
		      != S_SUCCESS)
		    {
		      return S_ERROR;
		    }
		}
	      break;
	    }

	  case S_AFTER:
	    /* We are already located at the end of the LO */
	    return S_END;

	  default:
	    return S_ERROR;
	  }
	break;
      }

    case LARGEOBJMGR_INSERT_MODE:
    case LARGEOBJMGR_APPEND_MODE:
      {
	ds->lo_offset += LARGEOBJMGR_DIRENTRY_LENGTH (ds->curdir.idxptr);
	ds->curdir.idx++;
	ds->curdir.idxptr++;
	last_ind = LARGEOBJMGR_MAX_DIRENTRY_CNT;

	if (ds->curdir.idx >= last_ind)
	  {
	    /* Move to the following directory page */
	    next_vpid = ds->curdir.hdr->next_vpid;
	    if (!VPID_ISNULL (&next_vpid))
	      {
		if (ds->curdir.pgptr != ds->firstdir.pgptr)
		  {
		    pgbuf_unfix_and_init (thread_p, ds->curdir.pgptr);
		  }

		ds->curdir.pgptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE,
					      PGBUF_LATCH_WRITE,
					      PGBUF_UNCONDITIONAL_LATCH);
		if (ds->curdir.pgptr == NULL)
		  {
		    return S_ERROR;
		  }

		ds->curdir.hdr = (LARGEOBJMGR_DIRHEADER *) ds->curdir.pgptr;
		ds->curdir.idx = 0;
		ds->curdir.idxptr =
		  largeobjmgr_first_direntry (ds->curdir.pgptr);

		/* advance the directory map entry, if needed */
		if (ds->index_level > 0
		    && ds->firstdir.idx < ds->firstdir.hdr->pg_lastact_idx
		    && VPID_EQ (&next_vpid,
				&((ds->firstdir.idxptr + 1)->vpid)))
		  {
		    ds->firstdir.idx++;
		    ds->firstdir.idxptr++;
		  }
	      }
	    else
	      {
		/* There is not more directory pages. Add one */
		if (largeobjmgr_dir_pgadd (thread_p, ds) != NO_ERROR)
		  {
		    return S_ERROR;
		  }
	      }
	  }
	break;
      }

    default:
      /* invalid operation mode */
      return S_ERROR;
    }

  return S_SUCCESS;
}

/*
 * largeobjmgr_dir_create () - Create the directory for the given large object
 *   return: NO_ERROR
 *   loid(in): Large Object Identifier
 *   length(in): Length of the large object data
 *   mapidx_pgcnt(in): Directory index page count
 *   dir_pgcnt(in): Directory page count
 *   data_pgcnt(in): Data page count
 *   max_data_slot_size(in): Maximum data slot size
 *
 * Note:  This routine is called during long object creation, after all the
 *        directory pages have been allocated. It basically initializes the
 *        directory pages to point to the data slots. If there is also
 *        a directory index page, the index page too is initialized
 *        to point to directory pages.
 */
int
largeobjmgr_dir_create (THREAD_ENTRY * thread_p, LOID * loid, int length,
			int mapidx_pgcnt, int dir_pgcnt, int data_pgcnt,
			int max_data_slot_size)
{
  LARGEOBJMGR_DIRHEADER head;
  LARGEOBJMGR_DIRENTRY *ent_ptr;
  LARGEOBJMGR_DIRMAP_ENTRY *mapidx_ptr;
  int max_dir_pg_data_size;
  int data_offset;
  int data_slot_ind;
  int data_nthpage;
  int dir_nthpage;
  PAGE_PTR page_ptr = NULL;
  VPID vpid;
  int k;
  LOG_DATA_ADDR addr;
  int ret = NO_ERROR;

  addr.vfid = &loid->vfid;

  /* Set up directory header for the general LOID data */
  head.loid = *loid;
  head.index_level = (mapidx_pgcnt > 0) ? 1 : 0;
  head.tot_length = length;
  head.tot_slot_cnt = data_pgcnt;

  if (data_pgcnt > 0)
    {
      data_nthpage = mapidx_pgcnt + dir_pgcnt + data_pgcnt - 1;
      if (file_find_nthpages
	  (thread_p, &loid->vfid, &head.goodvpid_fordata, data_nthpage,
	   1) == -1)
	{
	  VPID_SET_NULL (&head.goodvpid_fordata);
	}
    }
  else
    {
      VPID_SET_NULL (&head.goodvpid_fordata);
    }

  /* initialize directory index page, if any */
  max_dir_pg_data_size = LARGEOBJMGR_MAX_DIRENTRY_CNT * max_data_slot_size;

  if (mapidx_pgcnt > 0)
    {
      /* We are createting a MAP index too */
      if (file_find_nthpages (thread_p, &loid->vfid, &vpid, 0, 1) == -1)
	{
	  return ER_FAILED;
	}

      page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			    PGBUF_UNCONDITIONAL_LATCH);
      if (page_ptr == NULL)
	{
	  return ER_FAILED;
	}

      /* set up the header information for the index page */
      head.pg_tot_length = length;
      head.pg_act_idxcnt = MIN (dir_pgcnt, LARGEOBJMGR_MAX_DIRMAP_ENTRY_CNT);
      head.pg_lastact_idx = head.pg_act_idxcnt - 1;
      VPID_SET_NULL (&head.next_vpid);
      *(LARGEOBJMGR_DIRHEADER *) page_ptr = head;

      /* set up index entries */
      data_offset = 0;
      dir_nthpage = mapidx_pgcnt;

      for (k = 0, mapidx_ptr = largeobjmgr_first_dirmap_entry (page_ptr);
	   k < head.pg_act_idxcnt; k++, mapidx_ptr++)
	{
	  if (file_find_nthpages (thread_p, &loid->vfid, &mapidx_ptr->vpid,
				  dir_nthpage, 1) == -1)
	    {
	      pgbuf_unfix_and_init (thread_p, page_ptr);
	      return ER_FAILED;
	    }
	  mapidx_ptr->length = MIN (length - data_offset,
				    max_dir_pg_data_size);
	  data_offset += (int) mapidx_ptr->length;
	  dir_nthpage++;
	}

      if (head.pg_act_idxcnt <= LARGEOBJMGR_MAX_DIRMAP_ENTRY_CNT)
	{
	  for (; k < LARGEOBJMGR_MAX_DIRMAP_ENTRY_CNT; k++, mapidx_ptr++)
	    {
	      LARGEOBJMGR_SET_EMPTY_DIRMAP_ENTRY (mapidx_ptr);
	    }
	}
      else
	{
	  /* compress index page and redistribute entries */
	  mapidx_ptr--;		/* point to the last entry */
	  for (; k < dir_pgcnt; k++)
	    {
	      ret = largeobjmgr_firstdir_map_shrink (thread_p, loid,
						     page_ptr);
	      if (ret != NO_ERROR)
		{
		  pgbuf_unfix_and_init (thread_p, page_ptr);
		  return ret;
		}
	      if (file_find_nthpages (thread_p, &loid->vfid,
				      &mapidx_ptr->vpid, dir_nthpage,
				      1) == -1)
		{
		  pgbuf_unfix_and_init (thread_p, page_ptr);
		  return ER_FAILED;
		}

	      mapidx_ptr->length =
		MIN (length - data_offset, max_dir_pg_data_size);
	      data_offset += (int) mapidx_ptr->length;
	      dir_nthpage++;
	    }
	}

      /* log the whole index page for REDO purposes. This is a new file, don't
         need undos */
      addr.pgptr = page_ptr;
      addr.offset = 0;
      log_append_redo_data (thread_p, RVLOM_DIR_PG_REGION, &addr, DB_PAGESIZE,
			    page_ptr);

      /* log, setdirty page and free */
      pgbuf_set_dirty (thread_p, page_ptr, FREE);
      page_ptr = NULL;
    }

  /* initialize all directory pages */
  data_offset = 0;
  data_slot_ind = 0;
  data_nthpage = mapidx_pgcnt + dir_pgcnt;

  for (dir_nthpage = mapidx_pgcnt;
       dir_nthpage < (mapidx_pgcnt + dir_pgcnt); dir_nthpage++)
    {
      /* fetch directory page */
      if (file_find_nthpages (thread_p, &loid->vfid, &vpid,
			      dir_nthpage, 1) == -1)
	{
	  return ER_FAILED;
	}

      page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			    PGBUF_UNCONDITIONAL_LATCH);
      if (page_ptr == NULL)
	{
	  return ER_FAILED;
	}

      /* set up the header information for this directory page */
      head.pg_tot_length = MIN (length - data_offset, max_dir_pg_data_size);
      head.pg_act_idxcnt = data_pgcnt - data_slot_ind;
      head.pg_lastact_idx = (head.pg_act_idxcnt - 1);

      if (dir_nthpage == (mapidx_pgcnt + dir_pgcnt - 1))
	{
	  VPID_SET_NULL (&head.next_vpid);
	}
      else
	{
	  if (file_find_nthpages (thread_p, &loid->vfid, &vpid,
				  dir_nthpage + 1, 1) == -1)
	    {
	      pgbuf_unfix_and_init (thread_p, page_ptr);
	      return ER_FAILED;
	    }
	  head.next_vpid = vpid;
	}
      *(LARGEOBJMGR_DIRHEADER *) page_ptr = head;

      /* fill in the entries for this page */
      for (k = 0, ent_ptr = largeobjmgr_first_direntry (page_ptr);
	   k < head.pg_act_idxcnt; k++, ent_ptr++)
	{
	  if (file_find_nthpages (thread_p, &loid->vfid, &ent_ptr->u.vpid,
				  data_nthpage, 1) == -1)
	    {
	      pgbuf_unfix_and_init (thread_p, page_ptr);
	      return ER_FAILED;
	    }
	  ent_ptr->slotid = 0;
	  ent_ptr->length =
	    (INT16) MIN (length - data_offset, max_data_slot_size);
	  data_offset += ent_ptr->length;
	  data_slot_ind++;
	  data_nthpage++;
	}

      /* initialize the rest of the entries */
      for (; k < LARGEOBJMGR_MAX_DIRENTRY_CNT; k++, ent_ptr++)
	{
	  LARGEOBJMGR_SET_EMPTY_DIRENTRY (ent_ptr);
	}

      /* log the whole index page for REDO purposes. This is a new file, don't
         need undos */
      addr.pgptr = page_ptr;
      addr.offset = 0;
      log_append_redo_data (thread_p, RVLOM_DIR_PG_REGION, &addr, DB_PAGESIZE,
			    page_ptr);

      /* setdirty page and free */
      pgbuf_set_dirty (thread_p, page_ptr, FREE);
      page_ptr = NULL;
    }

  return ret;
}

/*
 * largeobjmgr_dir_insert () - Insert new directory entries at current directory
 *                     position in the order given
 *   return: NO_ERROR
 *   ds(in): Directory state structure
 *   dir_entries(in): Array of new directory entries
 *   ins_ent_cnt(in): Number of new entries to be inserted (n > 0)
 *
 * Note: Directory points to the position after the all given entries. Thus,
 *       if entry content of the directory is A B C and directory points to B,
 *       after inserting X = {x, y, z} the directory content will  A x y z B C,
 *       and the directory will point to B. If the new inserted entries form
 *       the new end of large object (append), the directory will point to an
 *       empty slot after the large object, thus faciliating further append
 *       operations.
 */
int
largeobjmgr_dir_insert (THREAD_ENTRY * thread_p, LARGEOBJMGR_DIRSTATE * ds,
			LARGEOBJMGR_DIRENTRY * dir_entries, int ins_ent_cnt)
{
  LARGEOBJMGR_DIRENTRY *empty_shift_ptr;
  int empty_shift_ind;
  int empty_shift_cnt;
  LARGEOBJMGR_DIRENTRY *empty_ptr;
  int empty_ind;
  int empty_cnt;
  bool found;
  int delta;
  int last_ind;
  int pg_reg_len;
  LOG_DATA_ADDR addr;
  int ret = NO_ERROR;

  addr.vfid = &ds->firstdir.hdr->loid.vfid;

  while (ins_ent_cnt > 0)
    {
      /* more entries to be inserted
       * if the directory page for insertion is full, first open
       * space for insertion.
       */
      if (ds->curdir.hdr->pg_act_idxcnt == LARGEOBJMGR_MAX_DIRENTRY_CNT)
	{
	  /* Are we inserting entries at the end ? */
	  ret = largeobjmgr_dir_pgsplit (thread_p, ds, ins_ent_cnt);
	  if (ret != NO_ERROR)
	    {
	      return ret;
	    }
	}

      /* start searching from the current position to find the closest
         empty place that can take n or max entries. */
      empty_shift_ptr = NULL;
      empty_shift_ind = -1;
      empty_shift_cnt = -1;
      empty_ptr = ds->curdir.idxptr;
      empty_ind = ds->curdir.idx;
      found = false;
      last_ind = LARGEOBJMGR_MAX_DIRENTRY_CNT;

      /* Search to the right of current entry.
         Likely empty entries are located at the end of the directory page */
      while (!found && empty_ind < last_ind)
	{
	  empty_cnt = 0;

	  if (LARGEOBJMGR_ISEMPTY_DIRENTRY (empty_ptr))
	    {
	      /* an empty entry has been found */
	      do
		{
		  empty_cnt++;
		  empty_ptr++;
		  empty_ind++;
		}
	      while (empty_cnt < ins_ent_cnt && empty_ind < last_ind
		     && LARGEOBJMGR_ISEMPTY_DIRENTRY (empty_ptr));

	      if (empty_cnt == ins_ent_cnt || empty_cnt > empty_shift_cnt)
		{
		  empty_shift_ptr = empty_ptr - empty_cnt;
		  empty_shift_ind = empty_ind - empty_cnt;
		  empty_shift_cnt = empty_cnt;

		  if (empty_cnt == ins_ent_cnt)
		    {
		      found = true;
		    }
		}
	    }
	  else
	    {
	      empty_ptr++;
	      empty_ind++;
	    }
	}

      if (!found
	  && !(ds->curdir.hdr->pg_lastact_idx ==
	       ds->curdir.hdr->pg_act_idxcnt - 1))
	{
	  /* Search to the left */
	  empty_ptr = ds->curdir.idxptr;
	  empty_ind = ds->curdir.idx;

	  while (!found && empty_ind >= 0)
	    {
	      empty_cnt = 0;
	      if (LARGEOBJMGR_ISEMPTY_DIRENTRY (empty_ptr))
		{
		  /* an empty entry has been found */
		  do
		    {
		      empty_cnt++;
		      empty_ptr--;
		      empty_ind--;
		    }
		  while (empty_cnt < ins_ent_cnt && empty_ind >= 0
			 && LARGEOBJMGR_ISEMPTY_DIRENTRY (empty_ptr));

		  if (empty_cnt == ins_ent_cnt || empty_cnt > empty_shift_cnt)
		    {
		      empty_shift_ptr = empty_ptr + 1;
		      empty_shift_ind = empty_ind + 1;
		      empty_shift_cnt = empty_cnt;

		      if (empty_cnt == ins_ent_cnt)
			{
			  found = true;
			}
		    }
		}
	      else
		{
		  empty_ptr--;
		  empty_ind--;
		}
	    }
	}

      assert (empty_shift_ind >= 0);
      assert (empty_shift_cnt >= 0);

      /* shift the region between current entry and empty shift entry */
      if (empty_shift_ind < ds->curdir.idx && empty_shift_ptr != NULL)
	{
	  /* left shift
	     put an UNDO log for the old content of affected region */
	  pg_reg_len = ((ds->curdir.idx - empty_shift_ind) *
			sizeof (LARGEOBJMGR_DIRENTRY));

	  addr.pgptr = ds->curdir.pgptr;
	  addr.offset = (PGLENGTH) ((char *) empty_shift_ptr -
				    (char *) ds->curdir.pgptr);

	  log_append_undoredo_data (thread_p, RVLOM_DIR_PG_REGION, &addr,
				    pg_reg_len,
				    (pg_reg_len - (empty_shift_cnt *
						   sizeof
						   (LARGEOBJMGR_DIRENTRY))),
				    empty_shift_ptr,
				    empty_shift_ptr + empty_shift_cnt);

	  memmove (empty_shift_ptr, empty_shift_ptr + empty_shift_cnt,
		   (ds->curdir.idx - (empty_shift_ind + empty_shift_cnt))
		   * sizeof (LARGEOBJMGR_DIRENTRY));

	  ds->curdir.idx -= empty_shift_cnt;
	  ds->curdir.idxptr -= empty_shift_cnt;
	}
      else if (empty_shift_ind > ds->curdir.idx)
	{
	  /* right shift
	     put an UNDO log for the old content of affected region */
	  pg_reg_len = ((empty_shift_cnt + (empty_shift_ind - ds->curdir.idx))
			* sizeof (LARGEOBJMGR_DIRENTRY));
	  addr.pgptr = ds->curdir.pgptr;
	  addr.offset =
	    (PGLENGTH) ((char *) (ds->curdir.idxptr + empty_shift_cnt) -
			(char *) ds->curdir.pgptr);

	  log_append_undoredo_data (thread_p, RVLOM_DIR_PG_REGION, &addr,
				    pg_reg_len,
				    pg_reg_len - (empty_shift_cnt *
						  sizeof
						  (LARGEOBJMGR_DIRENTRY)),
				    ds->curdir.idxptr + empty_shift_cnt,
				    ds->curdir.idxptr);

	  memmove (ds->curdir.idxptr + empty_shift_cnt, ds->curdir.idxptr,
		   (empty_shift_ind - ds->curdir.idx)
		   * sizeof (LARGEOBJMGR_DIRENTRY));
	}

      /* put an UNDO log for the old content of affected region */
      pg_reg_len = empty_shift_cnt * sizeof (LARGEOBJMGR_DIRENTRY);
      addr.pgptr = ds->curdir.pgptr;
      addr.offset = (PGLENGTH) ((char *) ds->curdir.idxptr -
				(char *) ds->curdir.pgptr);

      log_append_undoredo_data (thread_p, RVLOM_DIR_PG_REGION, &addr,
				pg_reg_len, pg_reg_len,
				ds->curdir.idxptr, dir_entries);

      /* insert the emp_shift_cnt new entries in the current position */
      memcpy (ds->curdir.idxptr, dir_entries, pg_reg_len);

      delta = 0;
      for (empty_ptr = dir_entries, empty_cnt = 0;
	   empty_cnt < empty_shift_cnt; empty_ptr++, empty_cnt++)
	{
	  delta += LARGEOBJMGR_DIRENTRY_LENGTH (empty_ptr);
	}

      ds->curdir.idxptr += empty_shift_cnt;
      ds->curdir.idx += empty_shift_cnt;

      /* put an UNDO log for the old content of affected region */
      addr.pgptr = ds->curdir.pgptr;
      addr.offset = 0;

      log_append_undo_data (thread_p, RVLOM_DIR_PG_REGION, &addr,
			    sizeof (LARGEOBJMGR_DIRHEADER), ds->curdir.pgptr);

      /* update the page header information */
      ds->curdir.hdr->tot_length += delta;
      ds->curdir.hdr->tot_slot_cnt += empty_shift_cnt;
      ds->curdir.hdr->pg_tot_length += delta;
      ds->curdir.hdr->pg_act_idxcnt += empty_shift_cnt;

      if (empty_shift_ind > ds->curdir.hdr->pg_lastact_idx)
	{
	  ds->curdir.hdr->pg_lastact_idx =
	    empty_shift_ind + empty_shift_cnt - 1;
	}

      /* put an REDO log for the new content of affected region */
      log_append_redo_data (thread_p, RVLOM_DIR_PG_REGION, &addr,
			    sizeof (LARGEOBJMGR_DIRHEADER), ds->curdir.pgptr);

      /* set directory page dirty */
      pgbuf_set_dirty (thread_p, ds->curdir.pgptr, DONT_FREE);

      /* update index/root page entry */
      largeobjmgr_firstdir_update (thread_p, ds, delta, empty_shift_cnt);

      /* set directory state to point to after all inserted entries */
      ds->curdir.idxptr--;	/* first backup the last inserted entry position */
      ds->curdir.idx--;
      ds->lo_offset += (delta -
			LARGEOBJMGR_DIRENTRY_LENGTH (ds->curdir.idxptr));
      ds->tot_length += delta;

      /* Note: When this routine is called in a dircompression mode, it
       *       is guaranteed that the given entries be fit into the
       *       current directory page.
       */
      if (ds->opr_mode != LARGEOBJMGR_DIRCOMPRESS_MODE
	  && largeobjmgr_dir_scan_next (thread_p, ds) == S_ERROR)
	{
	  return ER_FAILED;
	}

      /* advance parameters */
      ins_ent_cnt -= empty_shift_cnt;
      dir_entries += empty_shift_cnt;
    }

  return ret;
}

/*
 * largeobjmgr_dir_update () - Update current directory entry to contain new directory
 *                     entry
 *   return: NO_ERROR
 *   ds(in): Directory state structure
 *   X(in): New directory entry (can be NULL)
 *
 * Note: If X is given is NULL, the routine behaves like a "delete entry"
 *       routine, replacing current entry with an EMPTY entry.
 */
int
largeobjmgr_dir_update (THREAD_ENTRY * thread_p, LARGEOBJMGR_DIRSTATE * ds,
			LARGEOBJMGR_DIRENTRY * X)
{
  int delta;
  int pg_lastact_idx;
  LARGEOBJMGR_DIRENTRY *last_act_ent_ptr;
  LARGEOBJMGR_RCV_STATE rcv;
  LOG_DATA_ADDR addr;

  addr.vfid = &ds->firstdir.hdr->loid.vfid;

  pg_lastact_idx = -1;
  if (X != NULL)
    {
      delta = (LARGEOBJMGR_DIRENTRY_LENGTH (X) -
	       LARGEOBJMGR_DIRENTRY_LENGTH (ds->curdir.idxptr));
    }
  else
    {
      /* Delete the current entry
         get the current entry length and find new last active entry index */
      delta = -LARGEOBJMGR_DIRENTRY_LENGTH (ds->curdir.idxptr);
      if (ds->curdir.hdr->pg_lastact_idx > 1
	  && ((pg_lastact_idx = ds->curdir.hdr->pg_lastact_idx) ==
	      ds->curdir.idx))
	{
	  last_act_ent_ptr = largeobjmgr_direntry (ds->curdir.pgptr,
						   pg_lastact_idx);
	  do
	    {
	      pg_lastact_idx--;
	      last_act_ent_ptr--;
	    }
	  while (pg_lastact_idx >= 0
		 && LARGEOBJMGR_ISEMPTY_DIRENTRY (last_act_ent_ptr));
	}
    }

  /* put an UNDO log to save current directory page state */
  largeobjmgr_get_rcv_state (ds, L_DIR_PAGE, &rcv);

  addr.pgptr = ds->curdir.pgptr;
  addr.offset = -1;
  log_append_undo_data (thread_p, RVLOM_DIR_RCV_STATE, &addr,
			sizeof (LARGEOBJMGR_RCV_STATE), &rcv);

  /* update directory page entry */
  if (X != NULL)
    {
      LARGEOBJMGR_COPY_DIRENTRY (ds->curdir.idxptr, X);	/* update */
    }
  else
    {
      LARGEOBJMGR_SET_EMPTY_DIRENTRY (ds->curdir.idxptr);	/* delete */
    }

  /* update directory page header */
  ds->curdir.hdr->pg_tot_length += delta;
  ds->curdir.hdr->tot_length += delta;

  if (X == NULL)
    {
      /* entry deletion */
      ds->curdir.hdr->tot_slot_cnt--;
      ds->curdir.hdr->pg_act_idxcnt--;
      if (ds->curdir.hdr->pg_act_idxcnt == 0)
	{
	  ds->curdir.hdr->pg_lastact_idx = -1;
	}
      else
	{
	  ds->curdir.hdr->pg_lastact_idx = pg_lastact_idx;
	}
    }

  /* put a REDO log to save new directory page state */
  largeobjmgr_get_rcv_state (ds, L_DIR_PAGE, &rcv);
  log_append_redo_data (thread_p, RVLOM_DIR_RCV_STATE, &addr,
			sizeof (LARGEOBJMGR_RCV_STATE), &rcv);

  /* set directory page dirty */
  pgbuf_set_dirty (thread_p, ds->curdir.pgptr, DONT_FREE);

  /* update first directory page entry */
  largeobjmgr_firstdir_update (thread_p, ds, delta, (X == NULL) ? -1 : 0);

  /* Update ds length information */
  ds->tot_length += delta;

  /* deallocate directory page if page becomes empty */
  if (X == NULL && ds->curdir.hdr->pg_act_idxcnt == 0)
    {
      if (largeobjmgr_dir_pgremove (thread_p, ds, NULL) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

#if defined (CUBRID_DEBUG)
/*
 * largeobjmgr_dir_dump () - Dump all the entries in the large object directory
 *   return: void
 *   loid(in): Large object identifier
 *
 * Note: This routine is provided only for debugging purposes.
 */
void
largeobjmgr_dir_dump (THREAD_ENTRY * thread_p, FILE * fp, LOID * loid)
{
  LARGEOBJMGR_DIRHEADER *dir_hdr;
  PAGE_PTR page_ptr = NULL;
  PAGE_PTR curdir_pgptr = NULL;
  VPID next_vpid;
  LARGEOBJMGR_DIRMAP_ENTRY *firstdir_idxptr;

  /* fetch the root page of LO and print root header */
  page_ptr = pgbuf_fix (thread_p, &loid->vpid, OLD_PAGE, PGBUF_LATCH_READ,
			PGBUF_UNCONDITIONAL_LATCH);
  if (page_ptr == NULL)
    {
      fprintf (fp, "....Error reading page.. Bye\n");
      return;
    }
  dir_hdr = (LARGEOBJMGR_DIRHEADER *) page_ptr;

  fprintf (fp, "\n========= LARGE OBJECT DIRECTORY ===========\n\n");
  fprintf (fp, "LOID = {{%d, %d} , {%d, %d}}\n",
	   dir_hdr->loid.vfid.volid, dir_hdr->loid.vfid.fileid,
	   dir_hdr->loid.vpid.volid, dir_hdr->loid.vpid.pageid);
  fprintf (fp, "Index Level: %d\n", dir_hdr->index_level);
  fprintf (fp, "Total Length: %lld, Total Slot Count: %d\n",
	   (long long) dir_hdr->tot_length, dir_hdr->tot_slot_cnt);
  fprintf (fp, "Good VPID Data (Last Allocated VPID data): {%d, %d}\n\n",
	   dir_hdr->goodvpid_fordata.volid, dir_hdr->goodvpid_fordata.pageid);

  if (dir_hdr->index_level > 0)
    {
      /* dump index page entry content */
      largeobjmgr_dir_pgdump (fp, page_ptr);

      /* get first directory page */
      firstdir_idxptr = largeobjmgr_first_dirmap_entry (page_ptr);
      if (LARGEOBJMGR_ISEMPTY_DIRMAP_ENTRY (firstdir_idxptr))
	{
	  pgbuf_unfix_and_init (thread_p, page_ptr);
	  return;
	}

      curdir_pgptr = pgbuf_fix (thread_p, &firstdir_idxptr->vpid, OLD_PAGE,
				PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (curdir_pgptr == NULL)
	{
	  fprintf (fp, "....Error reading page.. Bye\n");
	  return;
	}
      pgbuf_unfix_and_init (thread_p, page_ptr);
    }
  else
    {
      curdir_pgptr = page_ptr;
    }

  /* dump each directory page */
  do
    {
      dir_hdr = (LARGEOBJMGR_DIRHEADER *) curdir_pgptr;
      largeobjmgr_dir_pgdump (fp, curdir_pgptr);
      next_vpid = dir_hdr->next_vpid;
      pgbuf_unfix_and_init (thread_p, curdir_pgptr);

      if (!VPID_ISNULL (&next_vpid))
	{
	  curdir_pgptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE,
				    PGBUF_LATCH_READ,
				    PGBUF_UNCONDITIONAL_LATCH);
	  if (curdir_pgptr == NULL)
	    {
	      fprintf (fp, "....Error reading page.. Bye\n");
	      return;
	    }
	}
    }
  while (!VPID_ISNULL (&next_vpid));

  fprintf (fp, "\n\n");
}

/*
 * largeobjmgr_dir_check () - Check the correctness of the existing LO directory and
 *                    report any problems found
 *   return: true if directory is in a correct/consistent state,
 *           false otherwise
 *   loid(in):  Large Object Identifier
 */
bool
largeobjmgr_dir_check (THREAD_ENTRY * thread_p, LOID * loid)
{
  LARGEOBJMGR_DIRHEADER *firstdir_hdr;
  LARGEOBJMGR_DIRHEADER *curdir_hdr;
  PAGE_PTR page_ptr = NULL;
  LARGEOBJMGR_DIRHEADER root_head;
  LARGEOBJMGR_DIRHEADER temp_dir_head;
  PAGE_PTR curdir_pgptr = NULL;
  VPID last_vpid;
  VPID next_vpid;
  VPID stop_vpid;
  LARGEOBJMGR_DIRMAP_ENTRY *firstdir_idxptr;
  LARGEOBJMGR_DIRENTRY *curdir_idxptr;
  bool correct;
  int k;

  /* fetch the root page of LO and copy root header */
  page_ptr = pgbuf_fix (thread_p, &loid->vpid, OLD_PAGE, PGBUF_LATCH_READ,
			PGBUF_UNCONDITIONAL_LATCH);
  if (page_ptr == NULL)
    {
      fprintf (stderr, "....Error reading page.. Bye\n");
      return false;
    }

  correct = true;
  if (file_find_last_page (thread_p, &loid->vfid, &last_vpid) == NULL)
    {
      correct = false;
    }

  firstdir_hdr = (LARGEOBJMGR_DIRHEADER *) page_ptr;
  root_head = *firstdir_hdr;

  /* index page exists ? */
  if (firstdir_hdr->index_level > 0)
    {
      /* Check the map index page */
      temp_dir_head.tot_length = 0;
      temp_dir_head.pg_act_idxcnt = 0;
      temp_dir_head.pg_lastact_idx = -1;

      for (k = 0, firstdir_idxptr = largeobjmgr_first_dirmap_entry (page_ptr);
	   k < LARGEOBJMGR_MAX_DIRMAP_ENTRY_CNT; k++, firstdir_idxptr++)
	{
	  /* Length must be positive */
	  if (firstdir_idxptr->length < 0)
	    {
	      fprintf (stderr, "Error: Negative length on Map index entry\n");
	      fprintf (stderr, "[%d: {%d|%d}, %lld] ",
		       k, firstdir_idxptr->vpid.volid,
		       firstdir_idxptr->vpid.pageid,
		       (long long) firstdir_idxptr->length);
	      correct = false;
	    }

	  if (LARGEOBJMGR_ISEMPTY_DIRMAP_ENTRY (firstdir_idxptr))
	    {
	      continue;		/* skip and go ahead */
	    }

	  if ((k + 1) < LARGEOBJMGR_MAX_DIRMAP_ENTRY_CNT)
	    {
	      stop_vpid = (firstdir_idxptr + 1)->vpid;
	    }
	  else
	    {
	      VPID_SET_NULL (&stop_vpid);
	    }

	  next_vpid = firstdir_idxptr->vpid;
	  temp_dir_head.pg_tot_length = 0;

	  while (!VPID_ISNULL (&next_vpid)
		 && !VPID_EQ (&next_vpid, &stop_vpid))
	    {
	      curdir_pgptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE,
					PGBUF_LATCH_READ,
					PGBUF_UNCONDITIONAL_LATCH);
	      if (curdir_pgptr == NULL)
		{
		  fprintf (stderr, "....Error reading page.. Bye\n");
		  correct = false;
		  break;
		}

	      curdir_hdr = (LARGEOBJMGR_DIRHEADER *) page_ptr;
	      temp_dir_head.pg_tot_length += curdir_hdr->pg_tot_length;
	      next_vpid = curdir_hdr->next_vpid;
	      pgbuf_unfix_and_init (thread_p, curdir_pgptr);
	    }

	  if (temp_dir_head.pg_tot_length != firstdir_idxptr->length)
	    {
	      fprintf (stderr,
		       "Error: Different length for map index entry\n");
	      fprintf (stderr, "[%d: {%d|%d}, %lld] ", k,
		       firstdir_idxptr->vpid.volid,
		       firstdir_idxptr->vpid.pageid,
		       (long long) firstdir_idxptr->length);
	      fprintf (stderr, "Actual Page Length for Entry: %lld\n",
		       (long long) temp_dir_head.pg_tot_length);
	      correct = false;
	    }

	  temp_dir_head.tot_length += temp_dir_head.pg_tot_length;
	  temp_dir_head.pg_act_idxcnt++;
	  temp_dir_head.pg_lastact_idx = k;
	}

      if (temp_dir_head.tot_length != firstdir_hdr->pg_tot_length ||
	  temp_dir_head.pg_act_idxcnt != firstdir_hdr->pg_act_idxcnt ||
	  temp_dir_head.pg_lastact_idx != firstdir_hdr->pg_lastact_idx)
	{
	  fprintf (stderr, "Invalid Map index Information...\n");
	  fprintf (stderr, "Page Total Length: Map %lld, Found %lld\n",
		   (long long) firstdir_hdr->pg_tot_length,
		   (long long) temp_dir_head.tot_length);
	  fprintf (stderr, "Active Entry Count: Map %d, Found %d\n",
		   firstdir_hdr->pg_act_idxcnt, temp_dir_head.pg_act_idxcnt);
	  fprintf (stderr, "Last Active Entry Index: Map %d, Found %d\n",
		   firstdir_hdr->pg_lastact_idx,
		   temp_dir_head.pg_lastact_idx);
	  correct = false;
	}

      /* get first directory page */
      firstdir_idxptr = largeobjmgr_first_dirmap_entry (page_ptr);
      if (LARGEOBJMGR_ISEMPTY_DIRMAP_ENTRY (firstdir_idxptr))
	{
	  pgbuf_unfix_and_init (thread_p, page_ptr);

	  return correct;
	}

      next_vpid = firstdir_idxptr->vpid;
      pgbuf_unfix_and_init (thread_p, page_ptr);

      curdir_pgptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE,
				PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (curdir_pgptr == NULL)
	{
	  fprintf (stderr, "....Error reading page.. Bye\n");
	  return false;
	}
    }
  else
    {
      next_vpid = loid->vpid;
      curdir_pgptr = page_ptr;
    }

  /* Now check each directory page */

  temp_dir_head.tot_length = 0;
  temp_dir_head.tot_slot_cnt = 0;

  do
    {
      curdir_hdr = (LARGEOBJMGR_DIRHEADER *) curdir_pgptr;
      temp_dir_head.pg_tot_length = 0;
      temp_dir_head.pg_act_idxcnt = 0;
      temp_dir_head.pg_lastact_idx = -1;

      for (k = 0, curdir_idxptr = largeobjmgr_first_direntry (curdir_pgptr);
	   k < LARGEOBJMGR_MAX_DIRENTRY_CNT; k++, curdir_idxptr++)
	{
	  if (curdir_idxptr->slotid != NULL_SLOTID
	      && curdir_idxptr->length <= 0)
	    {
	      fprintf (stderr, "Error: Length should be greater than zero\n");
	      fprintf (stderr, "[%d: {%d|%d}, %d, %d] ",
		       k, curdir_idxptr->u.vpid.volid,
		       curdir_idxptr->u.vpid.pageid, curdir_idxptr->slotid,
		       curdir_idxptr->length);
	      correct = false;
	    }
	  if (curdir_idxptr->slotid == NULL_SLOTID
	      && curdir_idxptr->u.length != 0)
	    {
	      fprintf (stderr, "Error:Empty slot must have length of zero\n");
	      fprintf (stderr, "[%d: %d, %d] ",
		       k, curdir_idxptr->slotid, curdir_idxptr->u.length);
	      correct = false;
	    }

	  if (!LARGEOBJMGR_ISEMPTY_DIRENTRY (curdir_idxptr))
	    {
	      temp_dir_head.pg_tot_length +=
		LARGEOBJMGR_DIRENTRY_LENGTH (curdir_idxptr);
	      temp_dir_head.pg_act_idxcnt++;
	      temp_dir_head.pg_lastact_idx = k;
	    }
	}

      if (temp_dir_head.pg_tot_length != curdir_hdr->pg_tot_length
	  || temp_dir_head.pg_act_idxcnt != curdir_hdr->pg_act_idxcnt
	  || temp_dir_head.pg_lastact_idx != curdir_hdr->pg_lastact_idx)
	{
	  fprintf (stderr, "Invalid Directory  Information...\n");
	  fprintf (stderr, "Page Total Length: Hdr %lld, Found %lld\n",
		   (long long) curdir_hdr->pg_tot_length,
		   (long long) temp_dir_head.pg_tot_length);
	  fprintf (stderr, "Active Entry Count: Hdr %d, Found %d\n",
		   curdir_hdr->pg_act_idxcnt, temp_dir_head.pg_act_idxcnt);
	  fprintf (stderr, "Last Active Entry Index: Hdr %d, Found %d\n",
		   curdir_hdr->pg_lastact_idx, temp_dir_head.pg_lastact_idx);
	  correct = false;
	}

      temp_dir_head.tot_length += temp_dir_head.pg_tot_length;
      temp_dir_head.tot_slot_cnt += temp_dir_head.pg_act_idxcnt;

      next_vpid = curdir_hdr->next_vpid;
      pgbuf_unfix_and_init (thread_p, curdir_pgptr);
      if (!VPID_ISNULL (&next_vpid))
	{
	  curdir_pgptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE,
				    PGBUF_LATCH_READ,
				    PGBUF_UNCONDITIONAL_LATCH);
	  if (curdir_pgptr == NULL)
	    {
	      fprintf (stderr, "....Error reading page.. Bye\n");
	      correct = false;
	      break;
	    }
	}
    }
  while (!VPID_ISNULL (&next_vpid));

  if (temp_dir_head.tot_length != root_head.tot_length ||
      temp_dir_head.tot_slot_cnt != root_head.tot_slot_cnt)
    {
      fprintf (stderr, "Invalid LO Root Page Header Information...\n");
      fprintf (stderr, "LO Total Length: Hdr %lld, Found %lld\n",
	       (long long) root_head.tot_length,
	       (long long) temp_dir_head.tot_length);
      fprintf (stderr, "LO Total Slot Count: Hdr %d, Found %d\n",
	       root_head.tot_slot_cnt, temp_dir_head.tot_slot_cnt);
      correct = false;
    }

  return correct;
}
#endif

/*
 * largeobjmgr_dir_open () - Open the long object directory
 *   return: scan status
 *   loid(in): Large Object Identifier
 *   offset(in): Offset to locate
 *   opr_mode(in): LOM operation mode
 *   ds(out): The directory state information after searched entry is located
 *
 * Note: Open the long object directory and put it into a state such that
 *       directory structures initially points to the data located at offset,
 *       with S_ON directory position. If offset >= total LO length, then it
 *       returns S_END in LARGEOBJMGR_READ_MODE, LARGEOBJMGR_DELETE_MODE and LARGEOBJMGR_TRUNCATE_MODE
 *       and directory state is undefined, it returns S_SUCCESS in
 *       LARGEOBJMGR_WRITE_MODE, LARGEOBJMGR_INSERT_MODE, LARGEOBJMGR_APPEND_MODE and directory points to
 *       last entry, if any, with S_ON pos. and points to first empty entry,
 *       if no enties, with S_BEFORE position.
 */
SCAN_CODE
largeobjmgr_dir_open (THREAD_ENTRY * thread_p, LOID * loid, INT64 offset,
		      int opr_mode, LARGEOBJMGR_DIRSTATE * ds)
{
  PAGE_PTR page_ptr = NULL;
  INT64 dlo_offset;
  int act_ind_ent_cnt;

  /* initializations */
  LARGEOBJMGR_INIT_DIRSTATE (ds);
  ds->opr_mode = opr_mode;

  /* fetch the root page of LO */
  ds->firstdir.pgptr = pgbuf_fix (thread_p, &loid->vpid, OLD_PAGE,
				  PGBUF_LATCH_WRITE,
				  PGBUF_UNCONDITIONAL_LATCH);
  if (ds->firstdir.pgptr == NULL)
    {
      return S_ERROR;
    }
  ds->firstdir.hdr = (LARGEOBJMGR_DIRHEADER *) ds->firstdir.pgptr;

  ds->index_level = ds->firstdir.hdr->index_level;
  ds->tot_length = ds->firstdir.hdr->tot_length;
  ds->lo_offset = 0;
  ds->goodvpid_fordata = ds->firstdir.hdr->goodvpid_fordata;

  if (ds->opr_mode == LARGEOBJMGR_DIRCOMPRESS_MODE)
    {
      /* in directory compression mode, point to the very first directory
         page entry, if any. */
      ds->lo_offset = 0;
      if (ds->index_level > 0)
	{
	  /* There is a MAP index page */
	  ds->firstdir.idx = 0;
	  ds->firstdir.idxptr =
	    largeobjmgr_first_dirmap_entry (ds->firstdir.pgptr);
	  if (ds->firstdir.hdr->pg_act_idxcnt <= 0)
	    {
	      /* Nothing to compress */
	      ds->pos = S_AFTER;
	      return S_END;
	    }

	  /* fetch the first directory page */
	  ds->curdir.pgptr = pgbuf_fix (thread_p, &ds->firstdir.idxptr->vpid,
					OLD_PAGE, PGBUF_LATCH_WRITE,
					PGBUF_UNCONDITIONAL_LATCH);
	  if (ds->curdir.pgptr == NULL)
	    {
	      goto exit_on_error;
	    }
	}
      else
	{
	  /* There is not a map index page */
	  ds->curdir.pgptr = ds->firstdir.pgptr;
	}

      ds->curdir.hdr = (LARGEOBJMGR_DIRHEADER *) ds->curdir.pgptr;
      ds->curdir.idx = 0;
      ds->curdir.idxptr = largeobjmgr_first_direntry (ds->curdir.pgptr);
      ds->pos = S_ON;

      return S_SUCCESS;		/* OK */
    }

  /* If we are in append mode, reset the offset to the end of the LO */
  if (ds->opr_mode == LARGEOBJMGR_APPEND_MODE)
    {
      offset = ds->tot_length;
    }

  if (offset >= ds->tot_length)
    {
      /* Points at the end of the LO */
      if (LARGEOBJMGR_SCAN_MODE (ds->opr_mode))
	{
	  /* READ, DELETE, TRUNCATE, COMPRESS */
	  pgbuf_unfix_and_init (thread_p, ds->firstdir.pgptr);
	  ds->firstdir.hdr = NULL;
	  ds->pos = S_AFTER;

	  return S_END;
	}

      /* WRITE, INSERT, APPEND modes */
      ds->opr_mode = LARGEOBJMGR_APPEND_MODE;

      if (ds->tot_length > 0)
	{
	  /* point to the last entry */
	  offset = ds->tot_length - 1;
	}
      else
	{
	  /* empty LO, no entries */
	  if (ds->index_level > 0)
	    {
	      /*
	       * There is a map index page */
	      ds->firstdir.idx = 0;
	      ds->firstdir.idxptr =
		largeobjmgr_first_dirmap_entry (ds->firstdir.pgptr);

	      /* check if first entry points a page with zero length */
	      if (!VPID_ISNULL (&ds->firstdir.idxptr->vpid))
		{
		  page_ptr = pgbuf_fix (thread_p, &ds->firstdir.idxptr->vpid,
					OLD_PAGE, PGBUF_LATCH_WRITE,
					PGBUF_UNCONDITIONAL_LATCH);
		  if (page_ptr == NULL)
		    {
		      goto exit_on_error;
		    }

		  ds->curdir.pgptr = page_ptr;
		  ds->curdir.hdr = (LARGEOBJMGR_DIRHEADER *) ds->curdir.pgptr;
		  ds->curdir.idx = 0;
		  ds->curdir.idxptr =
		    largeobjmgr_first_direntry (ds->curdir.pgptr);
		}
	      else
		{
		  /* allocate a new directory page */
		  if (largeobjmgr_dir_pgadd (thread_p, ds) != NO_ERROR)
		    {
		      goto exit_on_error;
		    }
		}
	    }
	  else
	    {
	      /* There is not a map index page */
	      ds->curdir.pgptr = ds->firstdir.pgptr;
	      ds->curdir.hdr = (LARGEOBJMGR_DIRHEADER *) ds->curdir.pgptr;
	      ds->curdir.idx = 0;
	      ds->curdir.idxptr =
		largeobjmgr_first_direntry (ds->curdir.pgptr);
	    }

	  ds->pos = S_AFTER;
	  return S_SUCCESS;
	}
    }

  if (ds->index_level > 0)
    {
      /* There is a MAP index page.
         search the map to find a directory page to start the search. */
      act_ind_ent_cnt = ds->firstdir.hdr->pg_act_idxcnt;
      for ((ds->firstdir.idx = 0,
	    ds->firstdir.idxptr =
	    largeobjmgr_first_dirmap_entry (ds->firstdir.pgptr));
	   (ds->firstdir.idx < act_ind_ent_cnt
	    && (ds->lo_offset + ds->firstdir.idxptr->length) <= offset);
	   ds->firstdir.idx++, ds->firstdir.idxptr++)
	{
	  ds->lo_offset += ds->firstdir.idxptr->length;
	}

      /* fetch the directory page at the indexed location */
      ds->curdir.pgptr = pgbuf_fix (thread_p, &ds->firstdir.idxptr->vpid,
				    OLD_PAGE, PGBUF_LATCH_WRITE,
				    PGBUF_UNCONDITIONAL_LATCH);
      if (ds->curdir.pgptr == NULL)
	{
	  goto exit_on_error;
	}
      ds->curdir.hdr = (LARGEOBJMGR_DIRHEADER *) ds->curdir.pgptr;

      page_ptr = largeobjmgr_dir_search (thread_p, ds->curdir.pgptr,
					 offset - ds->lo_offset,
					 &ds->curdir.idx, &dlo_offset);
      if (page_ptr == NULL)
	{
	  pgbuf_unfix_and_init (thread_p, ds->curdir.pgptr);
	  goto exit_on_error;
	}
      ds->lo_offset += dlo_offset;

      if (ds->curdir.pgptr != page_ptr)
	{
	  pgbuf_unfix_and_init (thread_p, ds->curdir.pgptr);
	  ds->curdir.pgptr = page_ptr;
	  ds->curdir.hdr = (LARGEOBJMGR_DIRHEADER *) ds->curdir.pgptr;
	}
      ds->curdir.idxptr = largeobjmgr_direntry (ds->curdir.pgptr,
						ds->curdir.idx);
    }
  else
    {
      /* There is not a MAP index page */
      if (ds->opr_mode == LARGEOBJMGR_INSERT_MODE && offset == 0)
	{
	  /* Note: Point to the beginning. This special handling is done to
	   * force to use first directory page entries for insertion in case
	   * all its entries have been deleted.
	   */
	  ds->lo_offset = 0;
	  ds->curdir.pgptr = ds->firstdir.pgptr;
	  ds->curdir.hdr = (LARGEOBJMGR_DIRHEADER *) ds->curdir.pgptr;
	  ds->curdir.idx = 0;
	  ds->curdir.idxptr = largeobjmgr_first_direntry (ds->curdir.pgptr);
	}
      else
	{
	  page_ptr = largeobjmgr_dir_search (thread_p, ds->firstdir.pgptr,
					     offset, &ds->curdir.idx,
					     &ds->lo_offset);
	  if (page_ptr == NULL)
	    {
	      goto exit_on_error;
	    }
	  ds->curdir.pgptr = page_ptr;
	  ds->curdir.hdr = (LARGEOBJMGR_DIRHEADER *) ds->curdir.pgptr;
	  ds->curdir.idxptr = largeobjmgr_direntry (ds->curdir.pgptr,
						    ds->curdir.idx);
	}
    }

  /* free directory index page, if any, in READ operation mode */
  if (ds->opr_mode == LARGEOBJMGR_READ_MODE
      && ds->firstdir.pgptr != ds->curdir.pgptr)
    {
      pgbuf_unfix_and_init (thread_p, ds->firstdir.pgptr);
      ds->firstdir.hdr = NULL;
      ds->firstdir.idx = -1;
    }

  ds->pos = S_ON;

  return S_SUCCESS;

exit_on_error:

  if (ds->firstdir.pgptr)
    {
      pgbuf_unfix_and_init (thread_p, ds->firstdir.pgptr);
    }

  return S_ERROR;
}

/*
 * largeobjmgr_dir_close () - Close the directory by freeing fetched directory pages
 *   return: void
 *   ds(in): Directory state structure
 */
void
largeobjmgr_dir_close (THREAD_ENTRY * thread_p, LARGEOBJMGR_DIRSTATE * ds)
{
  /* free directory pages */
  if (ds->curdir.pgptr != NULL)
    {
      if (ds->curdir.pgptr != ds->firstdir.pgptr)
	{
	  pgbuf_unfix_and_init (thread_p, ds->curdir.pgptr);
	}
    }

  if (ds->firstdir.pgptr != NULL)
    {
      pgbuf_unfix_and_init (thread_p, ds->firstdir.pgptr);
    }

  LARGEOBJMGR_INIT_DIRSTATE (ds);
}

/*
 * largeobjmgr_dir_get_lolength () - Return the length of the large object
 *   return: the length of the large object, or -1 on error
 *   loid(in): Large Object Identifier
 */
INT64
largeobjmgr_dir_get_lolength (THREAD_ENTRY * thread_p, LOID * loid)
{
  PAGE_PTR pgptr = NULL;
  INT64 length;

  /* fetch the root page of LO */
  pgptr = pgbuf_fix (thread_p, &loid->vpid, OLD_PAGE, PGBUF_LATCH_READ,
		     PGBUF_UNCONDITIONAL_LATCH);
  if (pgptr == NULL)
    {
      return -1;
    }

  length = ((LARGEOBJMGR_DIRHEADER *) pgptr)->tot_length;

  pgbuf_unfix_and_init (thread_p, pgptr);

  return length;
}

/*
 * largeobjmgr_skip_empty_entries () - Skip empty entries pointed by current directory
 *                             state
 *   return: scan status
 *   ds(in): Directory state structure
 */
SCAN_CODE
largeobjmgr_skip_empty_entries (THREAD_ENTRY * thread_p,
				LARGEOBJMGR_DIRSTATE * ds)
{
  VPID next_vpid;

  /* skip empty entries on current directory page */
  while (ds->curdir.idx <= ds->curdir.hdr->pg_lastact_idx
	 && LARGEOBJMGR_ISEMPTY_DIRENTRY (ds->curdir.idxptr))
    {
      ds->curdir.idx++;
      ds->curdir.idxptr++;
    }

  /* If we passed the last active index on this page, continue on next page
     until we find a non empty index */
  while (ds->curdir.idx > ds->curdir.hdr->pg_lastact_idx)
    {
      next_vpid = ds->curdir.hdr->next_vpid;
      if (ds->curdir.pgptr != ds->firstdir.pgptr)
	{
	  pgbuf_unfix_and_init (thread_p, ds->curdir.pgptr);
	  ds->curdir.hdr = NULL;
	}

      if (VPID_ISNULL (&next_vpid))
	{
	  return S_END;
	}

      ds->curdir.pgptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE,
				    PGBUF_LATCH_WRITE,
				    PGBUF_UNCONDITIONAL_LATCH);
      if (ds->curdir.pgptr == NULL)
	{
	  return S_ERROR;
	}
      ds->curdir.hdr = (LARGEOBJMGR_DIRHEADER *) ds->curdir.pgptr;

      /* Do we need to advance map directory entry ? */
      if (ds->index_level > 0
	  && ds->firstdir.pgptr != NULL
	  && ds->firstdir.idx < ds->firstdir.hdr->pg_lastact_idx
	  && VPID_EQ (&next_vpid, &((ds->firstdir.idxptr + 1)->vpid)))
	{
	  ds->firstdir.idx++;
	  ds->firstdir.idxptr++;
	}
      ds->curdir.idx = 0;
      ds->curdir.idxptr = largeobjmgr_first_direntry (ds->curdir.pgptr);

      /* skip empty entries on current directory page */
      while (ds->curdir.idx <= ds->curdir.hdr->pg_lastact_idx
	     && LARGEOBJMGR_ISEMPTY_DIRENTRY (ds->curdir.idxptr))
	{
	  ds->curdir.idxptr++;
	  ds->curdir.idx++;
	}
    }

  return S_SUCCESS;
}


/* Index page routines */

/*
 * largeobjmgr_firstdir_update () - Update current directory index/root page entry
 *   return: void
 *   ds(in): Directory state structure
 *   delta_len(in): Change in length for the entry
 *   delta_slot_cnt(in): Change in total slot count
 */
static void
largeobjmgr_firstdir_update (THREAD_ENTRY * thread_p,
			     LARGEOBJMGR_DIRSTATE * ds, int delta_len,
			     int delta_slot_cnt)
{
  LARGEOBJMGR_RCV_STATE rcv;
  LOG_DATA_ADDR addr;

  /* Do we have a directory index page ? */
  if (ds->index_level > 0)
    {
      /* put an UNDO log to save current index page state */
      largeobjmgr_get_rcv_state (ds, L_IND_PAGE, &rcv);

      addr.vfid = &ds->firstdir.hdr->loid.vfid;
      addr.pgptr = ds->firstdir.pgptr;
      addr.offset = -1;

      log_append_undo_data (thread_p, RVLOM_DIR_RCV_STATE, &addr,
			    sizeof (LARGEOBJMGR_RCV_STATE), &rcv);

      /* update index page entry */
      ds->firstdir.idxptr->length += delta_len;

      /* update index page header */
      ds->firstdir.hdr->tot_length += delta_len;
      ds->firstdir.hdr->tot_slot_cnt += delta_slot_cnt;
      ds->firstdir.hdr->pg_tot_length += delta_len;

      /* put a REDO log to save new index page state */
      largeobjmgr_get_rcv_state (ds, L_IND_PAGE, &rcv);
      log_append_redo_data (thread_p, RVLOM_DIR_RCV_STATE, &addr,
			    sizeof (LARGEOBJMGR_RCV_STATE), &rcv);

      /* set index page dirty */
      pgbuf_set_dirty (thread_p, ds->firstdir.pgptr, DONT_FREE);

    }
  else if (ds->firstdir.pgptr != ds->curdir.pgptr)
    {
      /* update root page header if different from directory page
         put an UNDO log to save current root page state */
      largeobjmgr_get_rcv_state (ds, L_ROOT_PAGE, &rcv);

      addr.vfid = &ds->firstdir.hdr->loid.vfid;
      addr.pgptr = ds->firstdir.pgptr;
      addr.offset = -1;

      log_append_undo_data (thread_p, RVLOM_DIR_RCV_STATE, &addr,
			    sizeof (LARGEOBJMGR_RCV_STATE), &rcv);

      ds->firstdir.hdr->tot_length += delta_len;
      ds->firstdir.hdr->tot_slot_cnt += delta_slot_cnt;

      /* put a REDO log to save new index page state */
      largeobjmgr_get_rcv_state (ds, L_ROOT_PAGE, &rcv);
      log_append_redo_data (thread_p, RVLOM_DIR_RCV_STATE, &addr,
			    sizeof (LARGEOBJMGR_RCV_STATE), &rcv);

      /* set root page dirty */
      pgbuf_set_dirty (thread_p, ds->firstdir.pgptr, DONT_FREE);
    }
}

/*
 * largeobjmgr_firstdir_map_shrink () - Shrink the given directory index page by
 *                              deleting one entry, shifting entries and
 *                              opening an empty entry at the end
 *   return:
 *   loid(in): Large Object Identifier
 *   page_ptr(in): Directory index page ptr
 */
static int
largeobjmgr_firstdir_map_shrink (THREAD_ENTRY * thread_p, LOID * loid,
				 PAGE_PTR page_ptr)
{
  LARGEOBJMGR_DIRHEADER *dir_hdr;
  LARGEOBJMGR_DIRMAP_ENTRY *del_ent_ptr;
  LARGEOBJMGR_DIRMAP_ENTRY *ent_ptr;
  int del_ind;
  int del_length;
  int mv_ent_cnt;
  char *reg_ptr;
  LOG_DATA_ADDR addr;

  /*
   * find randomly an entry index to delete. The entry is chosen
   * randomly in order to distribute multiple page entries fairly
   * inside the index. The first and last entries are not chosen
   * for deletion.
   */
#if defined(SERVER_MODE)
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }
  del_ind =
    1 +
    (rand_r (&thread_p->rand_seed) % (LARGEOBJMGR_MAX_DIRMAP_ENTRY_CNT - 3));
#else /* SERVER_MODE */
  del_ind = 1 + (rand () % (LARGEOBJMGR_MAX_DIRMAP_ENTRY_CNT - 3));
#endif /* SERVER_MODE */

  del_ent_ptr = largeobjmgr_dirmap_entry (page_ptr, del_ind);
  del_length = (int) del_ent_ptr->length;
  mv_ent_cnt = LARGEOBJMGR_MAX_DIRMAP_ENTRY_CNT - (del_ind + 1);
  reg_ptr = (char *) ((LARGEOBJMGR_DIRMAP_ENTRY *) del_ent_ptr - 1);

  /* log the affected region of the index page for UNDO purposes. */
  addr.vfid = &loid->vfid;
  addr.pgptr = page_ptr;
  addr.offset = (PGLENGTH) ((char *) reg_ptr - (char *) page_ptr);

  log_append_undo_data (thread_p, RVLOM_DIR_PG_REGION, &addr,
			(mv_ent_cnt + 2) * sizeof (LARGEOBJMGR_DIRMAP_ENTRY),
			(char *) reg_ptr);

  /* update previous entry length */
  (del_ent_ptr - 1)->length += del_length;

  /* left shift entries to deleted entry */
  memmove (del_ent_ptr, del_ent_ptr + 1,
	   mv_ent_cnt * sizeof (LARGEOBJMGR_DIRMAP_ENTRY));

  /* initialize last entry */
  ent_ptr =
    largeobjmgr_dirmap_entry (page_ptr, LARGEOBJMGR_MAX_DIRMAP_ENTRY_CNT - 1);
  LARGEOBJMGR_SET_EMPTY_DIRMAP_ENTRY (ent_ptr);

  /* log the affected region of the index page for REDO purposes. */
  log_append_redo_data (thread_p, RVLOM_DIR_PG_REGION, &addr,
			(mv_ent_cnt + 2) * sizeof (LARGEOBJMGR_DIRMAP_ENTRY),
			(char *) reg_ptr);

  /* put an UNDO log for the old index directory header */
  addr.pgptr = page_ptr;
  addr.offset = 0;

  log_append_undo_data (thread_p, RVLOM_DIR_PG_REGION, &addr,
			sizeof (LARGEOBJMGR_DIRHEADER), page_ptr);

  /* update directory header */

  dir_hdr = (LARGEOBJMGR_DIRHEADER *) page_ptr;
  dir_hdr->pg_act_idxcnt--;
  dir_hdr->pg_lastact_idx = dir_hdr->pg_act_idxcnt - 1;

  /* put an REDO log for the index directory header */
  log_append_redo_data (thread_p, RVLOM_DIR_PG_REGION, &addr,
			sizeof (LARGEOBJMGR_DIRHEADER), page_ptr);

  /* set index page dirty */
  pgbuf_set_dirty (thread_p, page_ptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * largeobjmgr_firstdir_map_create () - Create an index for the already existing
 *                              directory
 *   return: NO_ERROR
 *   ds(in): Directory state structure
 */
static int
largeobjmgr_firstdir_map_create (THREAD_ENTRY * thread_p,
				 LARGEOBJMGR_DIRSTATE * ds)
{
  LARGEOBJMGR_DIRHEADER *dir_hdr;
  PAGE_PTR page_ptr = NULL;
  VPID next_vpid;
  LARGEOBJMGR_DIRMAP_ENTRY *firstdir_idxptr;
  int k;
  LOG_DATA_ADDR addr;

  /* allocate new page for the directory */
  page_ptr = largeobjmgr_dir_allocpage (thread_p, ds, &next_vpid);
  if (page_ptr == NULL)
    {
      return ER_FAILED;
    }

  /* copy first directory page content to this new page */
  memcpy (page_ptr, ds->firstdir.pgptr, DB_PAGESIZE);

  /* log the whole page content for REDO purposes */
  addr.vfid = &ds->firstdir.hdr->loid.vfid;
  addr.pgptr = page_ptr;
  addr.offset = 0;
  log_append_redo_data (thread_p, RVLOM_DIR_PG_REGION, &addr, DB_PAGESIZE,
			page_ptr);

  /* set dirty page */
  pgbuf_set_dirty (thread_p, page_ptr, FREE);
  page_ptr = NULL;

  /* log the whole index page content for UNDO purposes */
  addr.pgptr = ds->firstdir.pgptr;
  addr.offset = 0;

  log_append_undo_data (thread_p, RVLOM_DIR_PG_REGION, &addr, DB_PAGESIZE,
			ds->firstdir.pgptr);

  for ((ds->firstdir.idx = 0,
	ds->firstdir.idxptr =
	largeobjmgr_first_dirmap_entry (ds->firstdir.pgptr));
       ds->firstdir.idx < LARGEOBJMGR_MAX_DIRMAP_ENTRY_CNT;
       ds->firstdir.idxptr++, ds->firstdir.idx++)
    {
      if (VPID_ISNULL (&next_vpid))
	{
	  break;
	}
      page_ptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			    PGBUF_UNCONDITIONAL_LATCH);
      if (page_ptr == NULL)
	{
	  break;
	}

      dir_hdr = (LARGEOBJMGR_DIRHEADER *) page_ptr;
      ds->firstdir.idxptr->vpid = next_vpid;
      ds->firstdir.idxptr->length = (int) dir_hdr->pg_tot_length;
      next_vpid = dir_hdr->next_vpid;
      pgbuf_unfix_and_init (thread_p, page_ptr);
    }

  for (k = ds->firstdir.idx, firstdir_idxptr = ds->firstdir.idxptr;
       k < LARGEOBJMGR_MAX_DIRMAP_ENTRY_CNT; k++, firstdir_idxptr++)
    {
      LARGEOBJMGR_SET_EMPTY_DIRMAP_ENTRY (firstdir_idxptr);
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

  /* log the whole index page content for REDO purposes */
  addr.pgptr = ds->firstdir.pgptr;
  addr.offset = 0;
  log_append_redo_data (thread_p, RVLOM_DIR_PG_REGION, &addr, DB_PAGESIZE,
			ds->firstdir.pgptr);

  /* setdirty index page */
  pgbuf_set_dirty (thread_p, ds->firstdir.pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * largeobjmgr_dir_get_pos () - Get current directory state position and save
 *   return: void
 *   ds(in): Directory state structure
 *   ds_pos(out): current dierctory state position
 *
 * Note: This routine is called only if ds works in LARGEOBJMGR_COMPRESS_MODE or in
 *       LO_DIRCOMPRESS_MODE.
 */
void
largeobjmgr_dir_get_pos (LARGEOBJMGR_DIRSTATE * ds,
			 LARGEOBJMGR_DIRSTATE_POS * ds_pos)
{
  ds_pos->opr_mode = ds->opr_mode;
  ds_pos->pos = ds->pos;
  ds_pos->lo_offset = ds->lo_offset;

  if (ds->firstdir.pgptr != NULL)
    {
      pgbuf_get_vpid (ds->firstdir.pgptr, &ds_pos->firstdir_vpid);
    }
  else
    {
      VPID_SET_NULL (&ds_pos->firstdir_vpid);
    }
  ds_pos->firstdir_idx = ds->firstdir.idx;

  if (ds->curdir.pgptr != NULL)
    {
      pgbuf_get_vpid (ds->curdir.pgptr, &ds_pos->curdir_vpid);
    }
  else
    {
      VPID_SET_NULL (&ds_pos->curdir_vpid);
    }
  ds_pos->curdir_idx = ds->curdir.idx;
}

/*
 * largeobjmgr_dir_put_pos () - Put directory state back to the given position
 *   return: NO_ERROR
 *   ds(in): Directory state structure
 *   ds_pos(in): Directory state position
 *
 * Note: This routine is called only if ds works in LARGEOBJMGR_COMPRESS_MODE or in
 *       LO_DIRCOMPRESS_MODE.
 */
int
largeobjmgr_dir_put_pos (THREAD_ENTRY * thread_p, LARGEOBJMGR_DIRSTATE * ds,
			 LARGEOBJMGR_DIRSTATE_POS * ds_pos)
{
  VPID firstdir_vpid;
  VPID curdir_vpid;

  /* get first and current directory page identifiers */
  if (ds->firstdir.pgptr != NULL)
    {
      pgbuf_get_vpid (ds->firstdir.pgptr, &firstdir_vpid);
    }
  else
    {
      VPID_SET_NULL (&firstdir_vpid);
    }

  if (ds->curdir.pgptr != NULL)
    {
      pgbuf_get_vpid (ds->curdir.pgptr, &curdir_vpid);
    }
  else
    {
      VPID_SET_NULL (&curdir_vpid);
    }

  /* free first directory page, if needed */
  if (ds->firstdir.pgptr != NULL
      && !VPID_EQ (&firstdir_vpid, &ds_pos->firstdir_vpid))
    {
      if (ds->firstdir.pgptr != ds->curdir.pgptr)
	{
	  pgbuf_unfix_and_init (thread_p, ds->firstdir.pgptr);
	}
      ds->firstdir.pgptr = NULL;
      ds->firstdir.hdr = NULL;
    }

  /* fetch new index page, if needed */
  if (ds->firstdir.pgptr == NULL && !VPID_ISNULL (&ds_pos->firstdir_vpid))
    {
      if (VPID_EQ (&ds_pos->firstdir_vpid, &curdir_vpid))
	{
	  ds->firstdir.pgptr = ds->curdir.pgptr;
	}
      else
	{
	  ds->firstdir.pgptr = pgbuf_fix (thread_p, &ds_pos->firstdir_vpid,
					  OLD_PAGE, PGBUF_LATCH_WRITE,
					  PGBUF_UNCONDITIONAL_LATCH);
	}

      if (ds->firstdir.pgptr == NULL)
	{
	  return ER_FAILED;
	}

      ds->firstdir.hdr = (LARGEOBJMGR_DIRHEADER *) ds->firstdir.pgptr;
      pgbuf_get_vpid (ds->firstdir.pgptr, &firstdir_vpid);
    }

  ds->firstdir.idx = ds_pos->firstdir_idx;
  ds->firstdir.idxptr = largeobjmgr_dirmap_entry (ds->firstdir.pgptr,
						  ds->firstdir.idx);

  /* free current directory page, if needed */
  if (ds->curdir.pgptr != NULL
      && !VPID_EQ (&curdir_vpid, &ds_pos->curdir_vpid))
    {
      if (ds->curdir.pgptr != ds->firstdir.pgptr)
	{
	  pgbuf_unfix_and_init (thread_p, ds->curdir.pgptr);
	}
      ds->curdir.pgptr = NULL;
      ds->curdir.hdr = NULL;
    }

  /* fetch new directory page, if needed */
  if (ds->curdir.pgptr == NULL && !VPID_ISNULL (&ds_pos->curdir_vpid))
    {
      if (VPID_EQ (&ds_pos->curdir_vpid, &firstdir_vpid))
	{
	  ds->curdir.pgptr = ds->firstdir.pgptr;
	}
      else
	{
	  ds->curdir.pgptr = pgbuf_fix (thread_p, &ds_pos->curdir_vpid,
					OLD_PAGE, PGBUF_LATCH_WRITE,
					PGBUF_UNCONDITIONAL_LATCH);
	}

      if (ds->curdir.pgptr == NULL)
	{
	  return ER_FAILED;
	}

      ds->curdir.hdr = (LARGEOBJMGR_DIRHEADER *) ds->curdir.pgptr;
    }
  ds->curdir.idx = ds_pos->curdir_idx;
  ds->curdir.idxptr = largeobjmgr_direntry (ds->curdir.pgptr, ds->curdir.idx);

  /* put other state information back */
  ds->opr_mode = ds_pos->opr_mode;
  ds->pos = ds_pos->pos;
  ds->lo_offset = ds_pos->lo_offset;

  return NO_ERROR;
}

/*
 * largeobjmgr_dir_compress () - Compress the directory for the given large object
 *   return:
 *   loid(in): Large object identifier
 */
int
largeobjmgr_dir_compress (THREAD_ENTRY * thread_p, LOID * loid)
{
  LARGEOBJMGR_DIRSTATE ds_info, *ds;
  int lo_scan;
  VPID prev_vpid;
  VPID next_vpid;
  int ret = NO_ERROR;

  ds = &ds_info;

  /* open the directory with a directory compression mode */
  lo_scan = largeobjmgr_dir_open (thread_p, loid, -1,
				  LARGEOBJMGR_DIRCOMPRESS_MODE, ds);
  if (lo_scan == S_ERROR)
    {
      return ER_FAILED;
    }

  if (lo_scan == S_END)
    {
      /* no directory to be compressed */
      largeobjmgr_dir_close (thread_p, ds);
      return NO_ERROR;
    }

  VPID_SET_NULL (&prev_vpid);
  /* Go over each directory page */
  do
    {
      /* the directory state points to the beginning of the current
       * directory page with corresponding lo_offset. After current
       * page is processed lo_offset should point to the end of this
       * page to be prepared for next page access.
       */
      if (ds->curdir.hdr->pg_act_idxcnt == 0
	  && ds->curdir.pgptr != ds->firstdir.pgptr)
	{
	  /* empty page.. Remove the page
	     save next page identifier, prev. page identifier remains same */
	  next_vpid = ds->curdir.hdr->next_vpid;
	  ret = largeobjmgr_dir_pgremove (thread_p, ds, &prev_vpid);
	  if (ret != NO_ERROR)
	    {
	      largeobjmgr_dir_close (thread_p, ds);
	      return ret;
	    }
	}
      else
	{
	  /* page has entries */
	  if (ds->curdir.hdr->pg_act_idxcnt < LARGEOBJMGR_MAX_DIRENTRY_CNT
	      || (ds->curdir.hdr->pg_lastact_idx >
		  ds->curdir.hdr->pg_act_idxcnt - 1))
	    {
	      /* compress current directory page */
	      ret = largeobjmgr_dir_pgcompress (thread_p, ds);
	      if (ret != NO_ERROR)
		{
		  largeobjmgr_dir_close (thread_p, ds);
		  return ret;
		}
	    }
	  /* set previous and next page identifiers */
	  pgbuf_get_vpid (ds->curdir.pgptr, &prev_vpid);
	  next_vpid = ds->curdir.hdr->next_vpid;
	}

      /* get next directory page, if any */
      if (!VPID_ISNULL (&next_vpid))
	{
	  /* update lo_offset to point to next page */
	  if (ds->curdir.pgptr != NULL)
	    {
	      ds->lo_offset += ds->curdir.hdr->tot_length;
	      if (ds->curdir.pgptr != ds->firstdir.pgptr)
		{
		  pgbuf_unfix_and_init (thread_p, ds->curdir.pgptr);
		}
	      ds->curdir.pgptr = NULL;
	      ds->curdir.hdr = NULL;
	    }

	  /* fetch next directory page */
	  ds->curdir.pgptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE,
					PGBUF_LATCH_WRITE,
					PGBUF_UNCONDITIONAL_LATCH);
	  if (ds->curdir.pgptr == NULL)
	    {
	      largeobjmgr_dir_close (thread_p, ds);
	      return ER_FAILED;
	    }

	  ds->curdir.hdr = (LARGEOBJMGR_DIRHEADER *) ds->curdir.pgptr;
	  ds->curdir.idx = 0;
	  ds->curdir.idxptr = largeobjmgr_first_direntry (ds->curdir.pgptr);

	  /* move directory index entry, if necessary */
	  if (ds->index_level > 0
	      && ds->firstdir.idx < ds->firstdir.hdr->pg_lastact_idx
	      && VPID_EQ (&next_vpid, &((ds->firstdir.idxptr + 1)->vpid)))
	    {
	      ds->firstdir.idx++;
	      ds->firstdir.idxptr++;
	    }
	}
    }
  while (!VPID_ISNULL (&next_vpid));

  largeobjmgr_dir_close (thread_p, ds);

  return ret;
}

/*
 * largeobjmgr_init_dir_pagecnt () - Find and set initial directory and directory index
 *                         page count for the given initial data page count
 *   return: void
 *   data_pgcnt(in): Initial data page count
 *   dir_pgcnt(out): the initial directory page count
 *   dir_ind_pgcnt(out): the initial directory index page count
 */
void
largeobjmgr_init_dir_pagecnt (int data_pgcnt, int *dir_pgcnt,
			      int *dir_ind_pgcnt)
{
  if (data_pgcnt > 0)
    {
      *dir_pgcnt = CEIL_PTVDIV (data_pgcnt, LARGEOBJMGR_MAX_DIRENTRY_CNT);
    }
  else
    {
      *dir_pgcnt = 1;
    }

  if (*dir_pgcnt < LARGEOBJMGR_DIR_PG_CNT_INDEX_CREATE_LIMIT)
    {
      *dir_ind_pgcnt = 0;
    }
  else
    {
      *dir_ind_pgcnt = 1;
    }
}

/* Recovery routines */

/*
 * largeobjmgr_get_rcv_state () - Get current recovery state information of indicated
 *                        page
 *   return: void
 *   ds(in): Directory state structure
 *   page_type(in):  Directory page type
 *   rcv(out): recovery state information
 */
static void
largeobjmgr_get_rcv_state (LARGEOBJMGR_DIRSTATE * ds, PAGE_TYPE page_type,
			   LARGEOBJMGR_RCV_STATE * rcv)
{
  rcv->page_type = page_type;
  switch (page_type)
    {
    case L_DIR_PAGE:
      rcv->ent_ind = ds->curdir.idx;
      LARGEOBJMGR_COPY_DIRENTRY (&rcv->ent.d, ds->curdir.idxptr);
      rcv->dir_head = *(LARGEOBJMGR_DIRHEADER *) ds->curdir.pgptr;
      break;

    case L_ROOT_PAGE:
      rcv->ent_ind = -1;
      rcv->dir_head = *(LARGEOBJMGR_DIRHEADER *) ds->firstdir.pgptr;
      break;

    case L_IND_PAGE:
      rcv->ent_ind = ds->firstdir.idx;
      LARGEOBJMGR_COPY_DIRMAP_ENTRY (&rcv->ent.i, ds->firstdir.idxptr);
      rcv->dir_head = *(LARGEOBJMGR_DIRHEADER *) ds->firstdir.pgptr;
      break;
    }
}

/*
 * largeobjmgr_rv_dir_rcv_state_undoredo () - Put directory page back to
 *   the state indicated by the recovery structure for UNDO or REDO purposes
 *   return:
 *   recv(in): Recovery structure
 */
int
largeobjmgr_rv_dir_rcv_state_undoredo (THREAD_ENTRY * thread_p,
				       LOG_RCV * recv)
{
  LARGEOBJMGR_RCV_STATE *rcv = (LARGEOBJMGR_RCV_STATE *) recv->data;

  /* put header information */
  *(LARGEOBJMGR_DIRHEADER *) (recv->pgptr) = rcv->dir_head;

  /* put entry information */
  switch (rcv->page_type)
    {
    case L_DIR_PAGE:
      {
	LARGEOBJMGR_DIRENTRY *ent_ptr;

	ent_ptr = largeobjmgr_direntry (recv->pgptr, rcv->ent_ind);
	LARGEOBJMGR_COPY_DIRENTRY (ent_ptr, &rcv->ent.d);
      }
      break;

    case L_IND_PAGE:
      {
	LARGEOBJMGR_DIRMAP_ENTRY *ent_ptr;

	ent_ptr = largeobjmgr_dirmap_entry (recv->pgptr, rcv->ent_ind);
	LARGEOBJMGR_COPY_DIRMAP_ENTRY (ent_ptr, &rcv->ent.i);
      }
      break;

    default:
      break;
    }

  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * largeobjmgr_rv_dir_rcv_state_dump () - Dump recovery state information for the page
 *   return: void
 *   length(in): Length of recovery data
 *   data(in): Data that has been logged
 */
void
largeobjmgr_rv_dir_rcv_state_dump (FILE * fp, int length, void *data)
{
  LARGEOBJMGR_RCV_STATE *rcv = (LARGEOBJMGR_RCV_STATE *) data;

  /* dump header information */
  fprintf (fp, "\n--------------------------------------------\n");
  fprintf (fp, "LARGEOBJMGR_RCVSTATE_INFORMATION:\n");
  fprintf (fp, "\nDirectory Header:\n");
  fprintf (fp, "LOID = {{%d|%d}, {%d|%d}}\n",
	   rcv->dir_head.loid.vfid.volid, rcv->dir_head.loid.vfid.fileid,
	   rcv->dir_head.loid.vpid.volid, rcv->dir_head.loid.vpid.pageid);

  largeobjmgr_dirheader_dump (fp, &rcv->dir_head);

  /* dump entry information */
  fprintf (fp, "\nEntry Index: %d\n", rcv->ent_ind);
  if (rcv->page_type == L_IND_PAGE)
    {
      fprintf (fp, "\nDirectory Index Entry: ");
      largeobjmgr_dirmap_dump (fp, &rcv->ent.i, rcv->ent_ind);
    }
  else if (rcv->page_type == L_DIR_PAGE)
    {
      fprintf (fp, "\nDirectory Entry: ");
      largeobjmgr_direntry_dump (fp, &rcv->ent.d, rcv->ent_ind);
    }
  fprintf (fp, "\n");

  fprintf (fp, "\n--------------------------------------------\n");
}

/*
 * largeobjmgr_rv_dir_page_region_undoredo () - Cover a region of page with data indicated by
 *                           recovery structure for UNDO or REDO purposes
 *   return: 0 if no error, or error code
 *   recv(in): Recovery structure
 */
int
largeobjmgr_rv_dir_page_region_undoredo (THREAD_ENTRY * thread_p,
					 LOG_RCV * recv)
{
  memcpy ((char *) recv->pgptr + recv->offset, recv->data, recv->length);
  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);

  return NO_ERROR;
}


/*
 * largeobjmgr_rv_dir_new_page_undo () - Deallocate a new directory page for UNDO
 *                             purposes
 *   return: 0 if no error, or error code
 *   recv(in): Recovery structure
 */
int
largeobjmgr_rv_dir_new_page_undo (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  LARGEOBJMGR_DIRHEADER *dir_hdr;

  dir_hdr = (LARGEOBJMGR_DIRHEADER *) recv->pgptr;
  VPID_SET_NULL (&dir_hdr->loid.vpid);
  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);

  (void) file_dealloc_page (thread_p, (VFID *) recv->data,
			    pgbuf_get_vpid_ptr (recv->pgptr));

  return NO_ERROR;
}

/*
 * largeobjmgr_rv_dir_new_page_redo () - Initialize a new directory page for REDO
 *                             purposes
 *   return: 0 if no error, or error code
 *   recv(in): Recovery structure
 */
int
largeobjmgr_rv_dir_new_page_redo (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  LARGEOBJMGR_DIRENTRY *ent_ptr;
  int k;

  /* put page header */
  *(LARGEOBJMGR_DIRHEADER *) recv->pgptr =
    *(LARGEOBJMGR_DIRHEADER *) recv->data;

  /* initialize entries */
  ent_ptr = largeobjmgr_first_direntry (recv->pgptr);
  for (k = 0; k < LARGEOBJMGR_MAX_DIRENTRY_CNT; k++)
    {
      LARGEOBJMGR_SET_EMPTY_DIRENTRY (ent_ptr++);
    }

  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);

  return NO_ERROR;
}
