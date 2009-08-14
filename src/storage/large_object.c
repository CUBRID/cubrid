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
 * large_object.c - Large Object manager
 */

#ident "$Id$"

#include "config.h"

#include <string.h>
#include <assert.h>

#include "xserver_interface.h"
#include "error_manager.h"
#include "recovery.h"
#include "memory_alloc.h"
#include "storage_common.h"
#include "oid.h"
#include "object_representation.h"
#include "page_buffer.h"
#include "slotted_page.h"
#include "file_manager.h"
#include "large_object.h"
#include "large_object_directory.h"

/* Leave that much of page empty for slot management for more than one slots. */
#define LARGEOBJMGR_DATA_PG_LEAVE_EMPTY_SIZE 80

/* Maximum data slot size */
#define LARGEOBJMGR_MAX_DATA_SLOT_SIZE \
  ((int)(DB_PAGESIZE - LARGEOBJMGR_DATA_PG_LEAVE_EMPTY_SIZE))

/* Minimum uncompressed record size */
#define LARGEOBJMGR_MIN_DONT_COMPRESS_SLOT_SIZE ((int)(LARGEOBJMGR_MAX_DATA_SLOT_SIZE - 50))

/* Minimum new slot insertion size */
#define LARGEOBJMGR_MIN_NEW_SLOT_PORTION_SIZE \
  ((int)(LARGEOBJMGR_DATA_PG_LEAVE_EMPTY_SIZE + 20))

#define LARGEOBJMGR_CACHEVPID_ARRAY_SIZE 20

/* Recovery structures */

/* Operation codes for record regions */
typedef struct largeobjmgr_rv_slot LARGEOBJMGR_RV_SLOT;
struct largeobjmgr_rv_slot
{
  INT16 slotid;
  int offset;
  int length;
};

typedef struct largeobjmgr_alloc_npages LARGEOBJMGR_ALLOC_NPAGES;
struct largeobjmgr_alloc_npages
{
  LOID *loid;
  VPID cached_nthvpids[LARGEOBJMGR_CACHEVPID_ARRAY_SIZE];
  int nxcache;
  int num_free;
  int tot_allocated;
  int nth_first_allocated;
  int nth_next_free;
};

static INT64 largeobjmgr_system_op (THREAD_ENTRY * thread_p, LOID * loid,
				    int op_mode, INT64 offset,
				    INT64 length, char *buffer);

static void largeobjmgr_rv_slot_dump (FILE * fp, LARGEOBJMGR_RV_SLOT * lomrv);
static bool largeobjmgr_slot_split_possible (THREAD_ENTRY * thread_p,
					     PAGE_PTR page_ptr);
static int largeobjmgr_max_append_putin (THREAD_ENTRY * thread_p,
					 PAGE_PTR page_ptr, int length);
static int largeobjmgr_sp_split (THREAD_ENTRY * thread_p, LOID * loid,
				 PAGE_PTR page_ptr, INT16 slotid,
				 int slsplit_offset, INT16 * new_slotid);
static int largeobjmgr_sp_overwrite (THREAD_ENTRY * thread_p, LOID * loid,
				     PAGE_PTR page_ptr, INT16 slotid,
				     int spover_offset, int over_length,
				     char *buffer);
static int largeobjmgr_sp_takeout (THREAD_ENTRY * thread_p, LOID * loid,
				   PAGE_PTR page_ptr, INT16 slotid,
				   int sptakeout_offset, int takeout_length);
static int largeobjmgr_sp_append (THREAD_ENTRY * thread_p, LOID * loid,
				  PAGE_PTR page_ptr, INT16 slotid, int length,
				  int append_length, char *buffer);
static int largeobjmgr_sp_putin (THREAD_ENTRY * thread_p, LOID * loid,
				 PAGE_PTR page_ptr, INT16 slotid,
				 int spputin_offset, int putin_length,
				 char *buffer);
static PAGE_PTR largeobjmgr_getnewpage (THREAD_ENTRY * thread_p, LOID * loid,
					VPID * vpid, VPID * near_vpid);
static int largeobjmgr_allocset_pages (THREAD_ENTRY * thread_p, LOID * loid,
				       int num_pages,
				       LARGEOBJMGR_ALLOC_NPAGES * alloc_p);
static PAGE_PTR largeobjmgr_fetch_nxallocset_page (THREAD_ENTRY * thread_p,
						   LARGEOBJMGR_ALLOC_NPAGES *
						   alloc_p, VPID * vpid);
static int largeobjmgr_deallocset_restof_free_pages (THREAD_ENTRY * thread_p,
						     LARGEOBJMGR_ALLOC_NPAGES
						     * alloc_p);
static int largeobjmgr_deallocset_all_pages (THREAD_ENTRY * thread_p,
					     LARGEOBJMGR_ALLOC_NPAGES *
					     alloc_p);
static int largeobjmgr_write_entry (THREAD_ENTRY * thread_p, LOID * loid,
				    LARGEOBJMGR_DIRSTATE * ds,
				    LARGEOBJMGR_DIRENTRY * X, int bg_woffset,
				    int end_woffset, char *buffer);
static int largeobjmgr_delete_entry (THREAD_ENTRY * thread_p, LOID * loid,
				     LARGEOBJMGR_DIRSTATE * ds,
				     LARGEOBJMGR_DIRENTRY * dir_entry_p,
				     int bg_doffset, int end_doffset);
static int largeobjmgr_insert_internal (THREAD_ENTRY * thread_p,
					LOID * loid,
					LARGEOBJMGR_DIRSTATE * ds,
					INT64 lo_offset, int length,
					char *buffer);
static int largeobjmgr_putin_newentries (THREAD_ENTRY * thread_p,
					 LOID * loid,
					 LARGEOBJMGR_DIRSTATE * ds,
					 int length, char *buffer);
static int largeobjmgr_append_internal (THREAD_ENTRY * thread_p,
					LOID * loid,
					LARGEOBJMGR_DIRSTATE * ds,
					INT64 offset, int length,
					char *buffer);
static INT64 largeobjmgr_process (THREAD_ENTRY * thread_p, LOID * loid,
				  int opr_mode, INT64 offset,
				  INT64 length, char *buffer);
static int largeobjmgr_compress_data (THREAD_ENTRY * thread_p, LOID * loid);

static bool largeobjmgr_initlo_newpage (THREAD_ENTRY * thread_p,
					const VFID * vfid, const VPID * vpid,
					INT32 ignore_napges,
					void *ignore_args);
static bool largeobjmgr_initlo_set_noncontiguous_pages (THREAD_ENTRY *
							thread_p,
							const VFID * vfid,
							const VPID *
							ignore_vpid,
							const INT32 * nthpage,
							INT32 npages,
							void *ignore_args);

/*
 * largeobjmgr_rv_slot_dump () -
 *   return:
 *   lomrv(in):
 */
static void
largeobjmgr_rv_slot_dump (FILE * fp, LARGEOBJMGR_RV_SLOT * lomrv)
{
  fprintf (fp, "Slotid = %d offset = %d length = %d\n",
	   lomrv->slotid, lomrv->offset, lomrv->length);
}

/*
 * largeobjmgr_sp_split () -
 *   return: NO_ERROR
 *   loid(in):
 *   page_ptr(in):
 *   slotid(in):
 *   slsplit_offset(in):
 *   new_slotid(in):
 */
static int
largeobjmgr_sp_split (THREAD_ENTRY * thread_p, LOID * loid, PAGE_PTR page_ptr,
		      INT16 slotid, int slsplit_offset, INT16 * new_slotid)
{
  LARGEOBJMGR_RV_SLOT lomrv;
  LOG_DATA_ADDR addr;
  int ret = NO_ERROR;

  if (spage_split (thread_p, page_ptr, slotid, slsplit_offset, new_slotid) !=
      SP_SUCCESS)
    {
      goto exit_on_error;
    }

  /* Log that the slot has been split in two. */
  lomrv.slotid = *new_slotid;
  lomrv.offset = slsplit_offset;
  lomrv.length = -1;

  addr.vfid = &loid->vfid;
  addr.pgptr = page_ptr;
  addr.offset = slotid;
  log_append_undoredo_data (thread_p, RVLOM_SPLIT, &addr, sizeof (lomrv),
			    sizeof (lomrv), &lomrv, &lomrv);

  pgbuf_set_dirty (thread_p, page_ptr, DONT_FREE);

end:

  return ret;

exit_on_error:

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }
  goto end;
}

/*
 * largeobjmgr_slot_split_possible () -
 *   return:
 *   page_ptr(in):
 */
static bool
largeobjmgr_slot_split_possible (THREAD_ENTRY * thread_p, PAGE_PTR page_ptr)
{
  return (spage_max_space_for_new_record (thread_p, page_ptr) > 0);
}

/*
 * largeobjmgr_sp_overwrite () -
 *   return: NO_ERROR
 *   loid(in):
 *   page_ptr(in):
 *   slotid(in):
 *   spover_offset(in):
 *   over_length(in):
 *   buffer(in):
 */
static int
largeobjmgr_sp_overwrite (THREAD_ENTRY * thread_p, LOID * loid,
			  PAGE_PTR page_ptr, INT16 slotid, int spover_offset,
			  int over_length, char *buffer)
{
  RECDES recdes;
  LOG_DATA_ADDR addr;
  RECDES rec_over;
  LOG_CRUMB undo_crumb[2];
  LOG_CRUMB redo_crumb[2];
  int ret = NO_ERROR;

  /* Peek the slot for undo logging purposes */
  if (spage_get_record (page_ptr, slotid, &recdes, PEEK) != S_SUCCESS)
    {
      goto exit_on_error;
    }

  addr.vfid = &loid->vfid;
  addr.pgptr = page_ptr;
  addr.offset = slotid;

  undo_crumb[0].length = sizeof (spover_offset);
  undo_crumb[0].data = &spover_offset;
  undo_crumb[1].length = over_length;
  undo_crumb[1].data = (char *) recdes.data + spover_offset;

  redo_crumb[0].length = sizeof (spover_offset);
  redo_crumb[0].data = &spover_offset;
  redo_crumb[1].length = over_length;
  redo_crumb[1].data = buffer;

  log_append_undoredo_crumbs (thread_p, RVLOM_OVERWRITE, &addr, 2, 2,
			      undo_crumb, redo_crumb);

  rec_over.area_size = rec_over.length = over_length;
  rec_over.data = buffer;
  rec_over.type = REC_HOME;

  if (spage_overwrite (thread_p, page_ptr, slotid, spover_offset, &rec_over)
      != SP_SUCCESS)
    {
      goto exit_on_error;
    }

  pgbuf_set_dirty (thread_p, page_ptr, DONT_FREE);

end:

  return ret;

exit_on_error:

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }
  goto end;
}

/*
 * largeobjmgr_sp_takeout () -
 *   return: NO_ERROR
 *   loid(in):
 *   page_ptr(in):
 *   slotid(in):
 *   sptakeout_offset(in):
 *   takeout_length(in):
 */
static int
largeobjmgr_sp_takeout (THREAD_ENTRY * thread_p, LOID * loid,
			PAGE_PTR page_ptr, INT16 slotid, int sptakeout_offset,
			int takeout_length)
{
  RECDES recdes;
  LOG_DATA_ADDR addr;
  LARGEOBJMGR_RV_SLOT lomrv;
  LOG_CRUMB crumbs[2];
  int ret = NO_ERROR;

  /* Peek the slot for undo logging purposes */
  if (spage_get_record (page_ptr, slotid, &recdes, PEEK) != S_SUCCESS)
    {
      goto exit_on_error;
    }

  lomrv.slotid = slotid;
  lomrv.offset = sptakeout_offset;
  lomrv.length = takeout_length;

  crumbs[0].length = sizeof (lomrv);
  crumbs[0].data = &lomrv;
  crumbs[1].length = takeout_length;
  crumbs[1].data = (char *) recdes.data + sptakeout_offset;

  addr.vfid = &loid->vfid;
  addr.pgptr = page_ptr;
  addr.offset = slotid;

  log_append_undoredo_crumbs (thread_p, RVLOM_TAKEOUT, &addr, 2, 1, crumbs,
			      crumbs);

  if (spage_take_out (thread_p, page_ptr, slotid, sptakeout_offset,
		      takeout_length) != SP_SUCCESS)
    {
      goto exit_on_error;
    }

  pgbuf_set_dirty (thread_p, page_ptr, DONT_FREE);

end:

  return ret;

exit_on_error:

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }
  goto end;
}

/*
 * largeobjmgr_max_append_putin () -
 *   return:
 *   page_ptr(in):
 *   slotid(in):
 *   length(in):
 */
static int
largeobjmgr_max_append_putin (THREAD_ENTRY * thread_p, PAGE_PTR page_ptr,
			      int length)
{
  int free_space;

  free_space = spage_get_free_space (thread_p, page_ptr);
  if (free_space > LARGEOBJMGR_MAX_DATA_SLOT_SIZE - length)
    {
      free_space = LARGEOBJMGR_MAX_DATA_SLOT_SIZE - length;
    }

  return free_space;
}

/*
 * largeobjmgr_sp_append () -
 *   return: NO_ERROR
 *   loid(in):
 *   page_ptr(in):
 *   slotid(in):
 *   length(in):
 *   append_length(in):
 *   buffer(in):
 */
static int
largeobjmgr_sp_append (THREAD_ENTRY * thread_p, LOID * loid,
		       PAGE_PTR page_ptr, INT16 slotid, int length,
		       int append_length, char *buffer)
{
  RECDES recdes;
  LOG_DATA_ADDR addr;
  LARGEOBJMGR_RV_SLOT lomrv;
  int ret = NO_ERROR;

  addr.vfid = &loid->vfid;
  addr.pgptr = page_ptr;
  addr.offset = slotid;

  recdes.area_size = recdes.length = append_length;
  recdes.data = buffer;
  recdes.type = REC_HOME;

  lomrv.slotid = slotid;
  lomrv.offset = length;	/* Where to append    */
  lomrv.length = append_length;	/* How much to append */
  log_append_undoredo_data (thread_p, RVLOM_APPEND, &addr,
			    sizeof (LARGEOBJMGR_RV_SLOT), append_length,
			    &lomrv, buffer);

  if (spage_append (thread_p, page_ptr, slotid, &recdes) != SP_SUCCESS)
    {
      goto exit_on_error;
    }

  pgbuf_set_dirty (thread_p, page_ptr, DONT_FREE);

end:

  return ret;

exit_on_error:

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }
  goto end;
}

/*
 * largeobjmgr_sp_putin () -
 *   return: NO_ERROR
 *   loid(in):
 *   page_ptr(in):
 *   slotid(in):
 *   spputin_offset(in):
 *   putin_length(in):
 *   buffer(in):
 */
static int
largeobjmgr_sp_putin (THREAD_ENTRY * thread_p, LOID * loid, PAGE_PTR page_ptr,
		      INT16 slotid, int spputin_offset, int putin_length,
		      char *buffer)
{
  RECDES recdes;
  LOG_DATA_ADDR addr;
  LARGEOBJMGR_RV_SLOT lomrv;
  LOG_CRUMB crumbs[2];
  int ret = NO_ERROR;

  lomrv.slotid = slotid;
  lomrv.offset = spputin_offset;
  lomrv.length = putin_length;

  crumbs[0].length = sizeof (lomrv);
  crumbs[0].data = &lomrv;
  crumbs[1].length = putin_length;
  crumbs[1].data = buffer;

  addr.vfid = &loid->vfid;
  addr.pgptr = page_ptr;
  addr.offset = slotid;

  log_append_undoredo_crumbs (thread_p, RVLOM_PUTIN, &addr, 1, 2, crumbs,
			      crumbs);

  recdes.area_size = recdes.length = putin_length;
  recdes.data = buffer;
  recdes.type = REC_HOME;

  if (spage_put (thread_p, page_ptr, slotid, spputin_offset, &recdes) !=
      SP_SUCCESS)
    {
      goto exit_on_error;
    }

  pgbuf_set_dirty (thread_p, page_ptr, DONT_FREE);

end:

  return ret;

exit_on_error:

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }
  goto end;
}

/*
 * largeobjmgr_initlo_newpage () - Initialize a newly large object page
 *   return:
 *   vfid(in): File where the new page belongs
 *   vpid(in): The new page
 *   ignore_napges(in): Number of contiguous allocated pages
 *   ignore_args(in): Ignore arguments
 */
static bool
largeobjmgr_initlo_newpage (THREAD_ENTRY * thread_p, const VFID * vfid,
			    const VPID * vpid, INT32 ignore_napges,
			    void *ignore_args)
{
  LOG_DATA_ADDR addr;

  addr.vfid = vfid;
  addr.offset = -1;

  addr.pgptr = pgbuf_fix (thread_p, vpid, NEW_PAGE, PGBUF_LATCH_WRITE,
			  PGBUF_UNCONDITIONAL_LATCH);
  if (addr.pgptr == NULL)
    {
      return false;
    }

  /* data pages are SLOTTED pages, so initialize them */
  spage_initialize (thread_p, addr.pgptr, ANCHORED, CHAR_ALIGNMENT, false);

  /* put only a redo log for page allocation and initialization,
   * the caller is responsible for the undo log */
  log_append_redo_data (thread_p, RVLOM_GET_NEWPAGE, &addr, sizeof (VFID),
			vfid);

  pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
  addr.pgptr = NULL;

  return true;
}

/*
 * largeobjmgr_initlo_set_noncontiguous_pages () - Initialize a newly large object page
 *   return:
 *   vfid(in): File where the new page belongs
 *   ignore_vpid(in): The new page
 *   nthpage(in):
 *   npages(in): Number of contiguous allocated pages
 *   ignore_args(in): Ignore arguments
 */
static bool
largeobjmgr_initlo_set_noncontiguous_pages (THREAD_ENTRY * thread_p,
					    const VFID * vfid,
					    const VPID * ignore_vpid,
					    const INT32 * nthpage,
					    INT32 npages, void *ignore_args)
{
  int i, max;
  VPID nth_vpid;

  max = *nthpage + npages;
  for (i = *(int *) nthpage; i < max; i++)
    {
      if (file_find_nthpages (thread_p, vfid, &nth_vpid, i, 1) == -1)
	{
	  return false;
	}

      if (largeobjmgr_initlo_newpage
	  (thread_p, vfid, &nth_vpid, 1, ignore_args) == false)
	{
	  return false;
	}
    }

  return true;
}

/*
 * largeobjmgr_getnewpage () - Allocate a new data page for the large object and
 *                     initialize
 *   return:
 *   loid(in): Large object identifier
 *   vpid(out): the allocated page identifier
 *   near_vpid(in): Near page identifier or NULL
 */
static PAGE_PTR
largeobjmgr_getnewpage (THREAD_ENTRY * thread_p, LOID * loid, VPID * vpid,
			VPID * near_vpid)
{
  PAGE_PTR page_ptr = NULL;
  LOG_DATA_ADDR addr;

  /* Allocate a new page for the file and initialize */
  if (file_alloc_pages (thread_p, &loid->vfid, vpid, 1, near_vpid,
			largeobjmgr_initlo_newpage, NULL) == NULL)
    {
      return NULL;
    }

  /*
   * NOTE: we fetch the page as old since it was initialized during the
   * allocation by largeobjmgr_initlo_newpage, therfore, we care about the current
   * content of the page.
   */
  page_ptr = pgbuf_fix (thread_p, vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			PGBUF_UNCONDITIONAL_LATCH);
  if (page_ptr == NULL)
    {
      (void) file_dealloc_page (thread_p, &loid->vfid, vpid);
      return NULL;
    }

  /* Need undo log, for cases of unexpected rollback, but only
     if the file is not new */
  if (file_new_isvalid (thread_p, &loid->vfid) == DISK_INVALID)
    {
      addr.vfid = &loid->vfid;
      addr.pgptr = page_ptr;
      addr.offset = -1;
      log_append_undo_data (thread_p, RVLOM_GET_NEWPAGE, &addr, sizeof (VFID),
			    &loid->vfid);
    }

  return page_ptr;
}

/*
 * largeobjmgr_allocset_pages () -
 *   return: NO_ERROR
 *   loid(in):
 *   num_pages(in):
 *   new(in):
 */
static int
largeobjmgr_allocset_pages (THREAD_ENTRY * thread_p, LOID * loid,
			    int num_pages, LARGEOBJMGR_ALLOC_NPAGES * alloc_p)
{
  LOG_DATA_ADDR addr;
  VPID nth_vpid;
  int i;
  int ret = NO_ERROR;

  alloc_p->loid = loid;
  alloc_p->nxcache = LARGEOBJMGR_CACHEVPID_ARRAY_SIZE - 1;
  alloc_p->num_free = num_pages;
  alloc_p->tot_allocated = num_pages;

  /* Allocate the set of pages for the file */
  if (file_alloc_pages_as_noncontiguous (thread_p, &loid->vfid,
					 &alloc_p->cached_nthvpids[alloc_p->
								   nxcache],
					 &alloc_p->nth_first_allocated,
					 num_pages, NULL,
					 largeobjmgr_initlo_set_noncontiguous_pages,
					 NULL) == NULL)
    {
      goto exit_on_error;
    }
  alloc_p->nth_next_free = alloc_p->nth_first_allocated + 1;


  /* Need undo log, for cases of unexpected rollback, but only
     if the file is not new */
  if (file_new_isvalid (thread_p, &loid->vfid) == DISK_INVALID)
    {
      /* Need undo records for each page just allocated */
      for (i = alloc_p->nth_first_allocated; num_pages > 0; i++, num_pages--)
	{
	  if (file_find_nthpages (thread_p, &alloc_p->loid->vfid,
				  &nth_vpid, i, 1) == -1)
	    {
	      (void) largeobjmgr_deallocset_all_pages (thread_p, alloc_p);
	      goto exit_on_error;
	    }
	  addr.pgptr = pgbuf_fix (thread_p, &nth_vpid, OLD_PAGE,
				  PGBUF_LATCH_WRITE,
				  PGBUF_UNCONDITIONAL_LATCH);
	  if (addr.pgptr == NULL)
	    {
	      (void) largeobjmgr_deallocset_all_pages (thread_p, alloc_p);
	      goto exit_on_error;
	    }

	  /* Need undo log, for cases of unexpected rollback */
	  addr.vfid = &alloc_p->loid->vfid;
	  addr.offset = -1;
	  log_append_undo_data (thread_p, RVLOM_GET_NEWPAGE, &addr,
				sizeof (VFID), &alloc_p->loid->vfid);
	  pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
	  addr.pgptr = NULL;
	}
    }

end:

  return ret;

exit_on_error:

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }
  goto end;
}

/*
 * largeobjmgr_fetch_nxallocset_page () -
 *   return:
 *   new(in):
 *   vpid(in):
 */
static PAGE_PTR
largeobjmgr_fetch_nxallocset_page (THREAD_ENTRY * thread_p,
				   LARGEOBJMGR_ALLOC_NPAGES * alloc_p,
				   VPID * vpid)
{
  PAGE_PTR page_ptr = NULL;
  int num;

  if (alloc_p->nxcache >= LARGEOBJMGR_CACHEVPID_ARRAY_SIZE
      && alloc_p->num_free > 0)
    {
      /* Need to find out the next set of allocated page identifiers */
      num = alloc_p->num_free;
      if (num > LARGEOBJMGR_CACHEVPID_ARRAY_SIZE)
	{
	  num = LARGEOBJMGR_CACHEVPID_ARRAY_SIZE;
	}

      if (file_find_nthpages
	  (thread_p, &alloc_p->loid->vfid, alloc_p->cached_nthvpids,
	   alloc_p->nth_next_free, num) == -1)
	{
	  return NULL;
	}

      alloc_p->nxcache = 0;
      alloc_p->nth_next_free += num;
    }

  if (alloc_p->num_free <= 0)
    {
      /* Allocate a new page for the file and initialize */
      page_ptr = largeobjmgr_getnewpage (thread_p, alloc_p->loid, vpid, NULL);
      if (page_ptr == NULL)
	{
	  return NULL;
	}
      alloc_p->tot_allocated++;
    }
  else
    {
      *vpid = alloc_p->cached_nthvpids[alloc_p->nxcache];
      alloc_p->nxcache += 1;
      alloc_p->num_free -= 1;

      /* fetch the new page as an old page since it has been initialized by
         largeobjmgr_initlo_set_noncontiguous_pages */
      page_ptr = pgbuf_fix (thread_p, vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			    PGBUF_UNCONDITIONAL_LATCH);
      if (page_ptr == NULL)
	{
	  return NULL;
	}
    }

  return page_ptr;
}

/*
 * largeobjmgr_deallocset_restof_free_pages () -
 *   return: NO_ERROR
 *   new(in):
 */
static int
largeobjmgr_deallocset_restof_free_pages (THREAD_ENTRY * thread_p,
					  LARGEOBJMGR_ALLOC_NPAGES * alloc_p)
{
  int num;
  int ret = NO_ERROR;

  while (alloc_p->num_free > 0)
    {
      if (alloc_p->nxcache >= LARGEOBJMGR_CACHEVPID_ARRAY_SIZE)
	{
	  /* Need to find out the next set of allocated page identifiers */
	  num = alloc_p->num_free;
	  if (num > LARGEOBJMGR_CACHEVPID_ARRAY_SIZE)
	    {
	      num = LARGEOBJMGR_CACHEVPID_ARRAY_SIZE;
	    }

	  if (file_find_nthpages
	      (thread_p, &alloc_p->loid->vfid, alloc_p->cached_nthvpids,
	       alloc_p->nth_next_free, num) == -1)
	    {
	      goto exit_on_error;
	    }

	  alloc_p->nxcache = 0;
	  alloc_p->nth_next_free += num;
	}

      ret = file_dealloc_page (thread_p, &alloc_p->loid->vfid,
			       &alloc_p->cached_nthvpids[alloc_p->nxcache]);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}

      alloc_p->nxcache += 1;
      alloc_p->num_free -= 1;
    }

end:

  return ret;

exit_on_error:

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }
  goto end;
}

/*
 * largeobjmgr_deallocset_all_pages () -
 *   return: NO_ERROR
 *   new(in):
 */
static int
largeobjmgr_deallocset_all_pages (THREAD_ENTRY * thread_p,
				  LARGEOBJMGR_ALLOC_NPAGES * alloc_p)
{
  alloc_p->num_free = alloc_p->tot_allocated;
  alloc_p->nth_next_free = alloc_p->nth_first_allocated;
  alloc_p->nxcache = LARGEOBJMGR_CACHEVPID_ARRAY_SIZE + 1;

  return largeobjmgr_deallocset_restof_free_pages (thread_p, alloc_p);
}

/*
 * largeobjmgr_dump () - Dump large object according to given flag n
 *   return: void
 *   loid(in): Large object identifier
 *   n(in): Flag to indicate the dump action
 *            if 0, dump only LO directory,
 *            if 1, dump only LO content,
 *            if 2, dump both LO directory and LO content,
 *            if 3, dump LO directory and LO slotted pages w/o records,
 *            if 4, dump LO directory and LO slotted pages with records.
 *
 * Note: This routine is provided only for debugging purposes.
 */
void
largeobjmgr_dump (THREAD_ENTRY * thread_p, FILE * fp, LOID * loid, int n)
{
  LARGEOBJMGR_DIRSTATE ds_info, *ds;
  LARGEOBJMGR_DIRENTRY *dir_entry_p;
  VPID vpid;
  PAGE_PTR page_ptr = NULL;
  RECDES recdes;
  char *slot_char_p;
  VPID *dir_vpid_list = NULL;
  int dir_vpid_cnt;
  int page_cnt;
  bool index_exists;
  int pg_pos;
  int k;

  ds = &ds_info;

  fprintf (fp, "\n========= LARGE OBJECT DUMP ===========\n\n");
  fprintf (fp, "LOID = {{%d, %d} , {%d, %d}}\n",
	   loid->vfid.volid, loid->vfid.fileid,
	   loid->vpid.volid, loid->vpid.pageid);
  fprintf (fp, "\n");

  if (n != 1)			/* dump LO directory */
    {
      largeobjmgr_dir_dump (thread_p, fp, loid);
    }

  if (n == 1 || n == 2)		/* LO content dump */
    {
      if (largeobjmgr_dir_open (thread_p, loid, 0, LARGEOBJMGR_READ_MODE, ds)
	  == S_ERROR)
	{
	  fprintf (fp, " Error opening directory...\n");
	  return;
	}
      if (ds->tot_length > 0)
	{
	  do
	    {
	      dir_entry_p = ds->curdir.idxptr;
	      if (LARGEOBJMGR_ISHOLE_DIRENTRY (dir_entry_p))	/* entry contains a hole */
		{
		  fprintf (fp, "\n[  Slotid: %d\n", dir_entry_p->slotid);
		  fprintf (fp, "  Length: %d\n\n", dir_entry_p->u.length);
		  for (k = 0; k < dir_entry_p->u.length; k++)
		    {
		      fprintf (fp, "%c", '0');
		    }
		}
	      else
		{
		  fprintf (fp, "\n[ VPID: {%d, %d}\n",
			   dir_entry_p->u.vpid.volid,
			   dir_entry_p->u.vpid.pageid);
		  fprintf (fp, "  Slotid: %d\n", dir_entry_p->slotid);
		  fprintf (fp, "  Length: %d\n\n", dir_entry_p->length);

		  page_ptr = pgbuf_fix (thread_p, &dir_entry_p->u.vpid,
					OLD_PAGE, PGBUF_LATCH_READ,
					PGBUF_UNCONDITIONAL_LATCH);
		  if (page_ptr != NULL)
		    {
		      if (spage_get_record
			  (page_ptr, dir_entry_p->slotid, &recdes,
			   PEEK) != S_SUCCESS)
			{
			  fprintf (fp, "Error Reading slot record\n");
			}
		      else
			{
			  for (k = 0, slot_char_p = (char *) recdes.data;
			       k < recdes.length; k++, slot_char_p++)
			    {
			      fprintf (fp, "%c", *slot_char_p);
			    }
			}
		      pgbuf_unfix (thread_p, page_ptr);
		      page_ptr = NULL;
		    }
		  else
		    {
		      fprintf (fp, "ERROR: fetching page..");
		    }
		}
	      fprintf (fp, " ]\n");
	    }
	  while (largeobjmgr_dir_scan_next (thread_p, ds) == S_SUCCESS);
	}
      largeobjmgr_dir_close (thread_p, ds);
    }
  else if (n > 2)		/* LO slotted page dump */
    {
      if (largeobjmgr_dir_get_vpids
	  (thread_p, loid, &dir_vpid_list, &dir_vpid_cnt,
	   &index_exists) == NO_ERROR)
	{
	  page_cnt = file_get_numpages (thread_p, &loid->vfid);
	  for (pg_pos = 0; pg_pos < page_cnt; pg_pos++)
	    {
	      if (file_find_nthpages (thread_p, &loid->vfid, &vpid, pg_pos, 1)
		  == -1)
		{
		  break;
		}

	      for (k = 0;
		   k < dir_vpid_cnt && !VPID_EQ (&vpid, &dir_vpid_list[k]);
		   k++)
		{
		  ;
		}

	      if (k == dir_vpid_cnt)
		{
		  fprintf (fp, "\n[ Page Position: %d\n", pg_pos);
		  fprintf (fp, "    VPID: {%d, %d}\n", vpid.volid,
			   vpid.pageid);

		  page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE,
					PGBUF_LATCH_READ,
					PGBUF_UNCONDITIONAL_LATCH);
		  if (page_ptr == NULL)
		    {
		      break;
		    }

		  spage_dump (thread_p, fp, page_ptr,
			      (n == 3) ? false : true);
		  fprintf (fp, " ]\n");
		  pgbuf_unfix (thread_p, page_ptr);
		  page_ptr = NULL;
		}
	    }
	}
      free_and_init (dir_vpid_list);
      dir_vpid_list = NULL;
    }
}

/*
 * largeobjmgr_check () - Check to see if given LO is in a correct state and report
 *                any problems found
 *   return: true if given LO is in a correct state, false otherwise
 *   loid(in): Large Object Identifier
 */
bool
largeobjmgr_check (THREAD_ENTRY * thread_p, LOID * loid)
{
  return largeobjmgr_dir_check (thread_p, loid);
}

/*
 * largeobjmgr_write_entry () - Write indicated portion of entry data from buffer
 *   return: Length of the data written on success, ER_code on failure
 *   loid(in): Large Object Identifier
 *   ds(in): Directory state structure
 *   cur_dir_entry_p(in): Current directory entry structure
 *   bg_woffset(in): Write begin offset
 *   end_woffset(in): Write end offset
 *   buffer(in): Buffer to read the data
 */
static int
largeobjmgr_write_entry (THREAD_ENTRY * thread_p, LOID * loid,
			 LARGEOBJMGR_DIRSTATE * ds,
			 LARGEOBJMGR_DIRENTRY * cur_dir_entry_p,
			 int bg_woffset, int end_woffset, char *buffer)
{
  PAGE_PTR page_ptr = NULL;
  LARGEOBJMGR_DIRENTRY temp_dir_entry_p;
  int cur_slot_length;
  int write_length;
  int ret = ER_FAILED;

  /* write the indicated portion from the buffer */
  cur_slot_length = LARGEOBJMGR_DIRENTRY_LENGTH (cur_dir_entry_p);
  write_length = end_woffset - bg_woffset;

  if (LARGEOBJMGR_ISHOLE_DIRENTRY (cur_dir_entry_p))
    {
      /* ENTRY CONTAINS A HOLE.
         A portion of the hole will be removed with this overwrite. */
      if (bg_woffset > 0)
	{
	  /*
	   * The insertion happens within the hole.
	   * This hole is split into two pieces, the actual data is placed
	   * within the second hole shrinking the second hole. In fact the
	   * second hole may be completely eliminated.
	   */
	  LARGEOBJMGR_SET_HOLE_DIRENTRY (&temp_dir_entry_p, bg_woffset);
	  ret = largeobjmgr_dir_insert (thread_p, ds, &temp_dir_entry_p, 1);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}

      /*
       * Now insert the data by creating new slots */
      write_length =
	largeobjmgr_putin_newentries (thread_p, loid, ds, write_length,
				      buffer);
      if (write_length < 0)
	{
	  ret = write_length;
	  goto exit_on_error;
	}

      if (largeobjmgr_skip_empty_entries (thread_p, ds) != S_SUCCESS)
	{
	  goto exit_on_error;
	}

      /* update ending hole */
      if (end_woffset < cur_slot_length)
	{
	  /* Shrink the hole */
	  LARGEOBJMGR_SET_HOLE_DIRENTRY (&temp_dir_entry_p,
					 cur_slot_length - end_woffset);
	  ret = largeobjmgr_dir_update (thread_p, ds, &temp_dir_entry_p);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
      else
	{
	  /* delete the hole */
	  ret = largeobjmgr_delete_dir (thread_p, ds);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
    }
  else
    {
      /* Regular entry. Overwrite the data in this entry */

      page_ptr = pgbuf_fix (thread_p, &cur_dir_entry_p->u.vpid, OLD_PAGE,
			    PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (page_ptr == NULL)
	{
	  goto exit_on_error;
	}

      /* overwrite the area in the slot */
      ret = largeobjmgr_sp_overwrite
	(thread_p, loid, page_ptr, cur_dir_entry_p->slotid, bg_woffset,
	 write_length, buffer);
      if (ret != NO_ERROR)
	{
	  pgbuf_unfix (thread_p, page_ptr);
	  page_ptr = NULL;
	  goto exit_on_error;
	}

      pgbuf_unfix (thread_p, page_ptr);
      page_ptr = NULL;
    }

end:

  return write_length;

exit_on_error:

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }

  write_length = ret;		/* ERROR */

  goto end;
}

/*
 * largeobjmgr_delete_entry () - Delete indicated portion of entry data
 *   return: Length of the data deleted on success, ER_code on failure
 *   loid(in): Large Object Identifier
 *   ds(in):  Directory state structure
 *   X(in): Current directory entry structure
 *   bg_doffset(in): Delete begin offset
 *   end_doffset(in): Delete end offset
 */
static int
largeobjmgr_delete_entry (THREAD_ENTRY * thread_p, LOID * loid,
			  LARGEOBJMGR_DIRSTATE * ds,
			  LARGEOBJMGR_DIRENTRY * dir_entry_p, int bg_doffset,
			  int end_doffset)
{
  PAGE_PTR page_ptr = NULL;
  int cur_slot_length;
  LARGEOBJMGR_DIRENTRY temp_dir_ent;
  int dlength;
  int num_recs;
  RECDES recdes;
  LOG_DATA_ADDR addr;
  int ret = ER_FAILED;

  /* delete the indicated portion from the buffer */
  cur_slot_length = LARGEOBJMGR_DIRENTRY_LENGTH (dir_entry_p);
  dlength = end_doffset - bg_doffset;

  if (LARGEOBJMGR_ISHOLE_DIRENTRY (dir_entry_p))
    {
      /* ENTRY CONTAINS A HOLE.
         A portion of the hole will be removed with this deletion. */
      if (dlength == cur_slot_length)
	{
	  /* Deletion is the full hole */
	  ret = largeobjmgr_delete_dir (thread_p, ds);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
      else
	{
	  /* Shrink the hole with the difference in length. */
	  LARGEOBJMGR_SET_HOLE_DIRENTRY (&temp_dir_ent,
					 cur_slot_length - dlength);
	  ret = largeobjmgr_dir_update (thread_p, ds, &temp_dir_ent);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
    }
  else
    {
      /* Regular entry. Delete data from this entry. */

      page_ptr = pgbuf_fix (thread_p, &dir_entry_p->u.vpid, OLD_PAGE,
			    PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (page_ptr == NULL)
	{
	  goto exit_on_error;
	}

      if (dlength == cur_slot_length)
	{
	  /*
	   * Delete the slot completely.
	   * Deallocate the page if the deleation of the slot will make it empty.
	   */

	  num_recs = spage_number_of_records (page_ptr);

	  if (num_recs <= 1
	      && !VPID_EQ (&dir_entry_p->u.vpid, &(ds->goodvpid_fordata)))
	    {
	      /* Page has become empty */
	      pgbuf_unfix (thread_p, page_ptr);
	      page_ptr = NULL;
	      ret =
		file_dealloc_page (thread_p, &loid->vfid,
				   &dir_entry_p->u.vpid);
	      if (ret != NO_ERROR)
		{
		  goto exit_on_error;
		}
	    }
	  else
	    {
	      /* Just remove the slot from the page */
	      recdes.area_size = -1;
	      if (spage_get_record
		  (page_ptr, dir_entry_p->slotid, &recdes, PEEK) != S_SUCCESS)
		{
		  pgbuf_unfix (thread_p, page_ptr);
		  page_ptr = NULL;

		  goto exit_on_error;
		}
	      addr.vfid = &loid->vfid;
	      addr.pgptr = page_ptr;
	      addr.offset = dir_entry_p->slotid;

	      log_append_undoredo_recdes (thread_p, RVLOM_DELETE, &addr,
					  &recdes, NULL);

	      if (spage_delete (thread_p, page_ptr, dir_entry_p->slotid) !=
		  dir_entry_p->slotid)
		{
		  pgbuf_unfix (thread_p, page_ptr);
		  page_ptr = NULL;

		  goto exit_on_error;
		}

	      pgbuf_set_dirty (thread_p, page_ptr, FREE);
	      page_ptr = NULL;
	    }

	  /* delete the corresponding directory entry */
	  ret = largeobjmgr_delete_dir (thread_p, ds);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
      else
	{
	  /* Delete a portion of the area within the slot */
	  ret = largeobjmgr_sp_takeout (thread_p, loid, page_ptr,
					dir_entry_p->slotid, bg_doffset,
					dlength);
	  if (ret != NO_ERROR)
	    {
	      pgbuf_unfix (thread_p, page_ptr);
	      page_ptr = NULL;

	      goto exit_on_error;
	    }

	  pgbuf_unfix (thread_p, page_ptr);
	  page_ptr = NULL;

	  /* update the corresponding directory entry for new length */
	  LARGEOBJMGR_COPY_DIRENTRY (&temp_dir_ent, dir_entry_p);
	  temp_dir_ent.length = cur_slot_length - dlength;
	  ret = largeobjmgr_dir_update (thread_p, ds, &temp_dir_ent);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
    }

end:

  return dlength;

exit_on_error:

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }

  dlength = ret;		/* ERROR */

  goto end;
}

/*
 * largeobjmgr_insert_internal () - Insert the given data at the offset within the
 *                          large object
 *   return: Length of the data inserted on success, ER_code on failure
 *   loid(in): Large Object Identifier
 *   ds(in): Directory state structure
 *   lo_offset(in): Offset where data is to be inserted
 *   length(in): Length of data to be inserted
 *   buffer(in): Data to be inserted
 */
static int
largeobjmgr_insert_internal (THREAD_ENTRY * thread_p, LOID * loid,
			     LARGEOBJMGR_DIRSTATE * ds, INT64 lo_offset,
			     int length, char *buffer)
{
  LARGEOBJMGR_DIRENTRY *cur_dir_entry_p;
  LARGEOBJMGR_DIRENTRY temp_dir_entry;
  PAGE_PTR page_ptr = NULL;
  RECDES recdes;
  int slot_insert_offset;
  int right_slot_len;
  INT16 right_slot_id;
  char *temp_area = NULL;
  int plength;
  int xlength;
  int inserted_length;
  int ret = ER_FAILED;

  inserted_length = 0;
  if (lo_offset == ds->lo_offset)
    {
      /* Insert at the beginning of the slot */
      xlength = largeobjmgr_putin_newentries (thread_p, loid, ds,
					      length, buffer);
      if (xlength < 0)
	{
	  ret = (int) xlength;
	  goto exit_on_error;
	}
      inserted_length += xlength;
    }
  else
    {
      /* insert within the slot */
      cur_dir_entry_p = ds->curdir.idxptr;

      slot_insert_offset = (int) (lo_offset - ds->lo_offset);	/* left slot length */
      right_slot_len = (LARGEOBJMGR_DIRENTRY_LENGTH (cur_dir_entry_p) -
			slot_insert_offset);

      if (LARGEOBJMGR_ISHOLE_DIRENTRY (cur_dir_entry_p))
	{
	  /* Inserting in the middle of the hole.
	     The hole will be split into two */
	  LARGEOBJMGR_COPY_DIRENTRY (&temp_dir_entry, cur_dir_entry_p);
	  temp_dir_entry.u.length = slot_insert_offset;

	  ret = largeobjmgr_dir_update (thread_p, ds, &temp_dir_entry);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	  if (largeobjmgr_dir_scan_next (thread_p, ds) == S_ERROR)
	    {
	      goto exit_on_error;
	    }

	  /* Now insert after the new hole. */
	  xlength = largeobjmgr_putin_newentries (thread_p, loid, ds,
						  length, buffer);
	  if (xlength < 0)
	    {
	      ret = (int) xlength;
	      goto exit_on_error;
	    }
	  inserted_length += xlength;

	  /* insert a new directory entry for the right hole slot */
	  temp_dir_entry.u.length = right_slot_len;
	  ret = largeobjmgr_dir_insert (thread_p, ds, &temp_dir_entry, 1);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
      else
	{
	  /* regular entry */

	  page_ptr = pgbuf_fix (thread_p, &cur_dir_entry_p->u.vpid, OLD_PAGE,
				PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (page_ptr == NULL)
	    {
	      goto exit_on_error;
	    }

	  plength = 0;
	  if (cur_dir_entry_p->length <=
	      LARGEOBJMGR_MAX_DATA_SLOT_SIZE - length)
	    {
	      plength = largeobjmgr_max_append_putin (thread_p, page_ptr,
						      length);
	      if (plength >= length)
		{
		  /* We can add the desired record into the record in this slot */
		  ret = largeobjmgr_sp_putin (thread_p, loid, page_ptr,
					      cur_dir_entry_p->slotid,
					      slot_insert_offset, length,
					      buffer);
		  if (ret != NO_ERROR)
		    {
		      goto exit_on_error;
		    }

		  /* update directory entry for the new length */
		  LARGEOBJMGR_COPY_DIRENTRY (&temp_dir_entry,
					     cur_dir_entry_p);
		  temp_dir_entry.length += (INT16) length;
		  ret = largeobjmgr_dir_update (thread_p, ds,
						&temp_dir_entry);
		  if (ret != NO_ERROR)
		    {
		      goto exit_on_error;
		    }
		  inserted_length += length;
		}
	    }

	  if (plength >= length)
	    {
	      pgbuf_unfix (thread_p, page_ptr);
	      page_ptr = NULL;
	    }
	  else
	    {

	      if (largeobjmgr_slot_split_possible (thread_p, page_ptr) ==
		  true)
		{
		  /* Split on the same page */
		  ret = largeobjmgr_sp_split (thread_p, loid, page_ptr,
					      cur_dir_entry_p->slotid,
					      slot_insert_offset,
					      &right_slot_id);
		  if (ret != NO_ERROR)
		    {
		      goto exit_on_error;
		    }

		  pgbuf_unfix (thread_p, page_ptr);
		  page_ptr = NULL;

		  /* update current dir. entry for the new left slot length */
		  LARGEOBJMGR_COPY_DIRENTRY (&temp_dir_entry,
					     cur_dir_entry_p);
		  temp_dir_entry.length = slot_insert_offset;
		  ret = largeobjmgr_dir_update (thread_p, ds,
						&temp_dir_entry);
		  if (ret != NO_ERROR)
		    {
		      goto exit_on_error;
		    }
		  if (largeobjmgr_dir_scan_next (thread_p, ds) == S_ERROR)
		    {
		      goto exit_on_error;
		    }

		  xlength = largeobjmgr_putin_newentries (thread_p, loid,
							  ds, length, buffer);
		  if (xlength < 0)
		    {
		      ret = (int) xlength;
		      goto exit_on_error;
		    }
		  inserted_length += xlength;

		  /* insert a new directory entry for the right slot */
		  temp_dir_entry.slotid = right_slot_id;
		  temp_dir_entry.length = right_slot_len;
		  ret = largeobjmgr_dir_insert (thread_p, ds,
						&temp_dir_entry, 1);
		  if (ret != NO_ERROR)
		    {
		      goto exit_on_error;
		    }
		}
	      else
		{
		  /*
		   * We cannot split on the same page. The split must happen
		   * between two pages.
		   *
		   * Note: In this operation, we are modifying parts of the
		   *       record, without actually changing the length of the
		   *       slot. Therefore, we use a PEEK operation instead of
		   *       COPY, for performance reasons.
		   */

		  /*
		   * Move length bytes of the current slot when possible.
		   * If we cannot move length bytes, the new data will also
		   * be split in two.
		   */

		  recdes.area_size = -1;
		  if (spage_get_record (page_ptr, cur_dir_entry_p->slotid,
					&recdes, PEEK) != S_SUCCESS)
		    {
		      goto exit_on_error;
		    }

		  /* allocate a temporary area of buffer size */
		  temp_area = (char *) malloc (length);
		  if (temp_area == NULL)
		    {
		      goto exit_on_error;
		    }

		  xlength = MIN (length, right_slot_len);


		  if (length > xlength)
		    {
		      memcpy (temp_area, buffer + xlength, length - xlength);
		    }

		  if (xlength > 0)
		    {
		      memcpy ((char *) temp_area + length - xlength,
			      (char *) recdes.data + cur_dir_entry_p->length -
			      xlength, xlength);
		      ret = largeobjmgr_sp_takeout (thread_p, loid, page_ptr,
						    cur_dir_entry_p->slotid,
						    cur_dir_entry_p->length
						    - xlength, xlength);
		      if (ret != NO_ERROR)
			{
			  goto exit_on_error;
			}

		      /* Now add xlength bytes to the current slot */
		      ret = largeobjmgr_sp_putin (thread_p, loid, page_ptr,
						  cur_dir_entry_p->slotid,
						  slot_insert_offset, xlength,
						  buffer);
		      if (ret != NO_ERROR)
			{
			  goto exit_on_error;
			}
		    }

		  pgbuf_set_dirty (thread_p, page_ptr, FREE);
		  page_ptr = NULL;

		  /* Naw, insert the right portion of the split with may be a
		   * little bit of new data */
		  if (largeobjmgr_dir_scan_next (thread_p, ds) == S_ERROR)
		    {
		      goto exit_on_error;
		    }

		  xlength = largeobjmgr_putin_newentries (thread_p, loid, ds,
							  length, temp_area);
		  if (xlength < 0)
		    {
		      ret = xlength;
		      goto exit_on_error;
		    }

		  inserted_length += xlength;
		  free_and_init (temp_area);
		  temp_area = NULL;
		}
	    }
	}
    }

end:

  return inserted_length;

exit_on_error:

  if (page_ptr)
    {
      pgbuf_set_dirty (thread_p, page_ptr, FREE);
      page_ptr = NULL;
    }

  if (temp_area)
    {
      free_and_init (temp_area);
      temp_area = NULL;
    }

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }

  inserted_length = ret;	/* ERROR */

  goto end;
}

/*
 * largeobjmgr_putin_newentries () - Insert the given data AT the current directory
 *                           state entry
 *   return: Length of data inserted or ER_code
 *   loid(in):  Large object identifier
 *   ds(in): Directory state structure
 *   length(in): Length of data to be inserted
 *   buffer(in): Data to be inserted
 *
 * Note: Distribute the given data to a set of data pages and insert
 *       corresponding new entries to the directory at the current position.
 */
static int
largeobjmgr_putin_newentries (THREAD_ENTRY * thread_p, LOID * loid,
			      LARGEOBJMGR_DIRSTATE * ds, int length,
			      char *buffer)
{
  VPID vpid;
  VPID near_vpid;
  PAGE_PTR page_ptr = NULL;
  RECDES recdes;
  INT16 slotid;
  int xlength;
  int ent_cnt;
  LARGEOBJMGR_DIRENTRY *dir_entries = NULL;
  int index;
  int buf_offset;
  char *buf_ptr;
  int inserted_length;
  LOG_DATA_ADDR addr;
  LARGEOBJMGR_ALLOC_NPAGES alloc;
  LARGEOBJMGR_ALLOC_NPAGES *new_alloc = NULL;
  int ret = NO_ERROR;

  if (length == 0)
    {
      return 0;
    }

  inserted_length = 0;

  /* first use last page of the LO for insertion, if possible */
  page_ptr = NULL;
  VPID_SET_NULL (&vpid);

  vpid = ds->goodvpid_fordata;

  if (!VPID_ISNULL (&vpid))
    {
      /* Check the space in last data page allocated for the long object */

      page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			    PGBUF_UNCONDITIONAL_LATCH);
      if (page_ptr == NULL)
	{
	  goto exit_on_error;
	}

      /* How much space do we have on last page ? */
      xlength = spage_max_space_for_new_record (thread_p, page_ptr);
      if (xlength > LARGEOBJMGR_MAX_DATA_SLOT_SIZE)
	{
	  xlength = LARGEOBJMGR_MAX_DATA_SLOT_SIZE;
	}
      if (xlength < LARGEOBJMGR_MIN_NEW_SLOT_PORTION_SIZE && length > xlength)
	{
	  /* Too small to do anything */
	  xlength = 0;
	  pgbuf_unfix (thread_p, page_ptr);
	  page_ptr = NULL;
	  VPID_SET_NULL (&vpid);
	}
    }
  else
    {
      xlength = 0;
    }

  near_vpid = vpid;

  /* compute number of directory entries needed.
     That is, how many new entries and pages are needed. */
  ent_cnt = (xlength > 0) ? 1 : 0;
  if (length > xlength)
    {
      ent_cnt += CEIL_PTVDIV (length - xlength,
			      LARGEOBJMGR_MAX_DATA_SLOT_SIZE);
    }

  dir_entries = (LARGEOBJMGR_DIRENTRY *) malloc (ent_cnt *
						 sizeof
						 (LARGEOBJMGR_DIRENTRY));
  if (dir_entries == NULL)
    {
      goto exit_on_error;
    }

  index = 0;
  buf_offset = 0;
  buf_ptr = buffer;

  /* Now add the rest by allocating any needed pages */

  if (xlength > 0)
    {
      ent_cnt--;
    }

  if (ent_cnt > 0)
    {
      /* more data to be copied */
      ret = largeobjmgr_allocset_pages (thread_p, loid, ent_cnt, &alloc);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
      new_alloc = &alloc;
    }

  recdes.type = REC_HOME;
  while (buf_offset < length)
    {
      /* Get next available page of the set that were previously allocated.
         or allocate a new page if needed. */

      recdes.data = buf_ptr;
      if (xlength > 0)
	{
	  /* use last page for insertion */
	  recdes.length = MIN (xlength, length);
	  xlength = 0;
	}
      else
	{
	  /* Use one of the newly allocated pages */
	  recdes.length = MIN (length - buf_offset,
			       LARGEOBJMGR_MAX_DATA_SLOT_SIZE);
	  if (new_alloc != NULL)
	    {
	      page_ptr = largeobjmgr_fetch_nxallocset_page (thread_p,
							    new_alloc, &vpid);
	      if (page_ptr == NULL)
		{
		  (void) largeobjmgr_deallocset_all_pages (thread_p,
							   new_alloc);
		  goto exit_on_error;
		}
	    }
	}

      if (page_ptr == NULL
	  || spage_insert (thread_p, page_ptr, &recdes,
			   &slotid) != SP_SUCCESS)
	{
	  if (new_alloc != NULL)
	    {
	      (void) largeobjmgr_deallocset_all_pages (thread_p, new_alloc);
	    }
	  goto exit_on_error;
	}

      /* Log the insertion */
      addr.vfid = &loid->vfid;
      addr.pgptr = page_ptr;
      addr.offset = slotid;
      log_append_undoredo_recdes (thread_p, RVLOM_INSERT, &addr, NULL,
				  &recdes);

      pgbuf_set_dirty (thread_p, page_ptr, FREE);
      page_ptr = NULL;

      /* form next entry  */
      dir_entries[index].u.vpid = vpid;
      dir_entries[index].slotid = slotid;
      dir_entries[index].length = recdes.length;
      index++;

      /* move to the next slot to be copied */
      buf_offset += recdes.length;
      buf_ptr += recdes.length;
      inserted_length += recdes.length;
    }

  if (index > 0
      && !VPID_EQ (&dir_entries[index - 1].u.vpid, &(ds->goodvpid_fordata)))
    {
      /* reset file last page status */
      largeobjmgr_reset_last_alloc_page (thread_p, ds,
					 &dir_entries[index - 1].u.vpid);
    }

  /* insert all the created directory entries at current position */
  if (index > 0)
    {
      ret = largeobjmgr_dir_insert (thread_p, ds, dir_entries, index);
      if (ret != NO_ERROR)
	{
	  if (new_alloc != NULL)
	    {
	      (void) largeobjmgr_deallocset_all_pages (thread_p, new_alloc);
	    }
	  goto exit_on_error;
	}
    }

  if (new_alloc != NULL)
    {
      (void) largeobjmgr_deallocset_restof_free_pages (thread_p, new_alloc);
    }
  free_and_init (dir_entries);
  dir_entries = NULL;

end:

  return inserted_length;

exit_on_error:

  if (page_ptr)
    {
      pgbuf_unfix (thread_p, page_ptr);
      page_ptr = NULL;
    }

  if (dir_entries)
    {
      free_and_init (dir_entries);
      dir_entries = NULL;
    }

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }

  inserted_length = ret;	/* ERROR */

  goto end;
}

/*
 * largeobjmgr_append_internal () - Append given data to the end of the large object at
 *                          the given offset
 *   return: Length of data appended on success, ER_code on error
 *   loid(in): Large object identifier
 *   ds(in): Directory state structure
 *   offset(in): Offset where data is to be appended (>= lo_length)
 *   length(in): Length of data to be appended
 *   buffer(in): Data to be appended
 *
 * Note: If offset > lo_length, a hole is inserted to the end.
 */
static int
largeobjmgr_append_internal (THREAD_ENTRY * thread_p, LOID * loid,
			     LARGEOBJMGR_DIRSTATE * ds, INT64 offset,
			     int length, char *buffer)
{
  LARGEOBJMGR_DIRENTRY *cur_dir_entry_p;
  LARGEOBJMGR_DIRENTRY temp_dir_entry;
  PAGE_PTR page_ptr = NULL;
  int hole_length, plength;
  int xlength;
  int appended_length;
  int ret = ER_FAILED;

  appended_length = 0;

  if (ds->pos == S_AFTER)
    {
      /* points to the after LO */
      if (offset > ds->tot_length)
	{
	  /* a hole need to be inserted */
	  hole_length = (int) (offset - ds->tot_length);
	  LARGEOBJMGR_SET_HOLE_DIRENTRY (&temp_dir_entry, hole_length);

	  ret = largeobjmgr_dir_insert (thread_p, ds, &temp_dir_entry, 1);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

	  appended_length += hole_length;
	}

      /* Do we have any data to be appended ? */
      if (length > 0)
	{
	  xlength = largeobjmgr_putin_newentries (thread_p, loid, ds,
						  length, buffer);
	  if (xlength < 0)
	    {
	      ret = xlength;
	      goto exit_on_error;
	    }
	  appended_length += xlength;
	}
    }
  else
    {
      cur_dir_entry_p = ds->curdir.idxptr;

      if (offset > ds->tot_length)
	{
	  /* a hole need to be inserted or expanded */
	  hole_length = (int) (offset - ds->tot_length);
	  if (LARGEOBJMGR_ISHOLE_DIRENTRY (cur_dir_entry_p))
	    {
	      /* Last entry is a hole,
	         expand it instead of creating another one */
	      LARGEOBJMGR_COPY_DIRENTRY (&temp_dir_entry, cur_dir_entry_p);
	      temp_dir_entry.u.length += hole_length;

	      ret = largeobjmgr_dir_update (thread_p, ds, &temp_dir_entry);
	      if (ret != NO_ERROR)
		{
		  goto exit_on_error;
		}

	      appended_length += hole_length;

	      if (length > 0)
		{
		  /* more data to append */
		  if (largeobjmgr_dir_scan_next (thread_p, ds) == S_ERROR)
		    {
		      goto exit_on_error;
		    }

		  xlength = largeobjmgr_putin_newentries (thread_p, loid,
							  ds, length, buffer);
		  if (xlength < 0)
		    {
		      ret = xlength;
		      goto exit_on_error;
		    }

		  appended_length += xlength;
		}
	    }
	  else
	    {
	      /* insert a hole */
	      if (largeobjmgr_dir_scan_next (thread_p, ds) == S_ERROR)
		{
		  goto exit_on_error;
		}

	      LARGEOBJMGR_SET_HOLE_DIRENTRY (&temp_dir_entry, hole_length);
	      ret = largeobjmgr_dir_insert (thread_p, ds, &temp_dir_entry, 1);
	      if (ret != NO_ERROR)
		{
		  goto exit_on_error;
		}

	      appended_length += hole_length;

	      if (length > 0)
		{
		  /* more data to append */
		  xlength = largeobjmgr_putin_newentries (thread_p, loid,
							  ds, length, buffer);
		  if (xlength < 0)
		    {
		      ret = xlength;
		      goto exit_on_error;
		    }
		  appended_length += xlength;
		}
	    }
	}
      else
	{
	  /* Append data to the end of current slot. */
	  if (!LARGEOBJMGR_ISHOLE_DIRENTRY (cur_dir_entry_p) && length > 0
	      && cur_dir_entry_p->length < LARGEOBJMGR_MAX_DATA_SLOT_SIZE)
	    {
	      page_ptr = pgbuf_fix (thread_p, &cur_dir_entry_p->u.vpid,
				    OLD_PAGE, PGBUF_LATCH_WRITE,
				    PGBUF_UNCONDITIONAL_LATCH);
	      if (page_ptr == NULL)
		{
		  goto exit_on_error;
		}

	      /* Append as much as you can to the current slot entry */
	      plength = largeobjmgr_max_append_putin (thread_p, page_ptr,
						      cur_dir_entry_p->
						      length);
	      plength = MIN (plength, length);

	      if (plength > 0)
		{
		  ret = largeobjmgr_sp_append (thread_p, loid, page_ptr,
					       cur_dir_entry_p->slotid,
					       cur_dir_entry_p->length,
					       plength, buffer);
		  if (ret != NO_ERROR)
		    {
		      goto exit_on_error;
		    }

		  /* update directory entry for the new length */
		  LARGEOBJMGR_COPY_DIRENTRY (&temp_dir_entry,
					     cur_dir_entry_p);
		  temp_dir_entry.length += plength;
		  ret = largeobjmgr_dir_update (thread_p, ds,
						&temp_dir_entry);
		  if (ret != NO_ERROR)
		    {
		      goto exit_on_error;
		    }

		  buffer += plength;
		  length -= plength;
		  appended_length += plength;
		}
	      pgbuf_unfix (thread_p, page_ptr);
	      page_ptr = NULL;
	    }

	  if (length > 0)
	    {
	      /* more data to append */
	      if (!LARGEOBJMGR_ISEMPTY_DIRENTRY (cur_dir_entry_p)
		  && largeobjmgr_dir_scan_next (thread_p, ds) == S_ERROR)
		{
		  goto exit_on_error;
		}

	      xlength = largeobjmgr_putin_newentries (thread_p, loid, ds,
						      length, buffer);
	      if (xlength < 0)
		{
		  ret = xlength;
		  goto exit_on_error;
		}
	      appended_length += xlength;
	    }
	}
    }

end:

  return appended_length;

exit_on_error:

  if (page_ptr)
    {
      pgbuf_unfix (thread_p, page_ptr);
      page_ptr = NULL;
    }

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }

  appended_length = ret;	/* ERROR */

  goto end;
}

/*
 * largeobjmgr_process () - Process length bytes of large object from the given offset
 *   return: Length of the data processed on success, ER_code on failure
 *   loid(in): Large Object Identifier
 *   opr_mode(in): Operation mode code
 *   offset(in): Offset where process is to start
 *   length(in): Length of data to be processed
 *   buffer(in): Buffer where data is read/stored (can be NULL)
 *
 * Note: Directory state points to beginning entry where process is to take
 *       place.
 *       Operation mode can only be LARGEOBJMGR_READ_MODE, LARGEOBJMGR_WRITE_MODE, LARGEOBJMGR_DELETE_MODE
 *       and LARGEOBJMGR_TRUNCATE_MODE.
 */
static INT64
largeobjmgr_process (THREAD_ENTRY * thread_p, LOID * loid, int opr_mode,
		     INT64 offset, INT64 length, char *buffer)
{
  LARGEOBJMGR_DIRSTATE ds_info, *ds;
  SCAN_CODE lo_scan;
  LARGEOBJMGR_DIRENTRY *cur_dir_entry_p;
  int xlength;
  PAGE_PTR page_ptr = NULL;
  RECDES recdes;
  char *buffer_ptr;
  INT64 end_offset;
  INT64 cur_slot_begin_offset;
  int cur_slot_length;
  int process_begin_offset;
  int process_end_offset;
  int cur_data_length;
  INT64 processed_length;
  INT64 process_length;
  int ret = ER_FAILED;

  ds = &ds_info;

  if (opr_mode != LARGEOBJMGR_TRUNCATE_MODE && length <= 0)
    {
      return 0;
    }

  /* locate the directory entry that contains the given offset */
  lo_scan = largeobjmgr_dir_open (thread_p, loid, offset, opr_mode, ds);
  if (lo_scan == S_ERROR)
    {
      return ER_FAILED;
    }
  else if (lo_scan == S_END)
    {
      largeobjmgr_dir_close (thread_p, ds);
      return NO_ERROR;
    }

  /* Compute length of operation. */
  switch (opr_mode)
    {
    case LARGEOBJMGR_TRUNCATE_MODE:
      {
	length = ds->tot_length - offset;
	process_length = length;
	break;
      }
    case LARGEOBJMGR_READ_MODE:
    case LARGEOBJMGR_DELETE_MODE:
    case LARGEOBJMGR_WRITE_MODE:
    default:
      {
	process_length = MIN (length, ds->tot_length - offset);
	break;
      }
    }

  /* initialize parameters */
  processed_length = 0;
  buffer_ptr = buffer;
  end_offset = offset + length;
  cur_slot_begin_offset = ds->lo_offset;
  recdes.area_size = -1;

  if (process_length > 0)
    {
      /* We are between the current boundaries of the lom object */
      while (processed_length < process_length && lo_scan != S_END)
	{
	  /*
	   * More data to process
	   *
	   * Point to current directory data slot X and find portion of
	   * slot/record to be processed.
	   */
	  cur_dir_entry_p = ds->curdir.idxptr;
	  cur_slot_length = LARGEOBJMGR_DIRENTRY_LENGTH (cur_dir_entry_p);

	  if (cur_slot_begin_offset >= offset)
	    {
	      process_begin_offset = 0;
	    }
	  else
	    {
	      process_begin_offset = (int) (offset - cur_slot_begin_offset);
	    }

	  if (cur_slot_begin_offset + cur_slot_length <= end_offset)
	    {
	      process_end_offset = cur_slot_length;
	    }
	  else
	    {
	      process_end_offset = (int) (end_offset - cur_slot_begin_offset);
	    }

	  /* process current slot according to the mode */
	  switch (opr_mode)
	    {
	    case LARGEOBJMGR_READ_MODE:
	      {
		/* copy the indicated portion */
		cur_data_length = process_end_offset - process_begin_offset;

		if (LARGEOBJMGR_ISHOLE_DIRENTRY (cur_dir_entry_p))
		  {
		    /* entry contains a hole. Set buffer to null bytes */
		    memset (buffer_ptr, ' ', cur_data_length);
		  }
		else
		  {
		    page_ptr = pgbuf_fix (thread_p, &cur_dir_entry_p->u.vpid,
					  OLD_PAGE, PGBUF_LATCH_READ,
					  PGBUF_UNCONDITIONAL_LATCH);
		    if (page_ptr == NULL)
		      {
			goto exit_on_error;
		      }

		    if (spage_get_record (page_ptr, cur_dir_entry_p->slotid,
					  &recdes, PEEK) != S_SUCCESS)
		      {
			goto exit_on_error;
		      }

		    memcpy (buffer_ptr,
			    (char *) recdes.data + process_begin_offset,
			    cur_data_length);
		    pgbuf_unfix (thread_p, page_ptr);
		    page_ptr = NULL;
		  }
	      }
	      break;

	    case LARGEOBJMGR_WRITE_MODE:
	      {
		cur_data_length = largeobjmgr_write_entry (thread_p, loid, ds,
							   cur_dir_entry_p,
							   process_begin_offset,
							   process_end_offset,
							   buffer_ptr);
		if (cur_data_length < 0)
		  {
		    ret = cur_data_length;
		    goto exit_on_error;
		  }
	      }
	      break;

	    case LARGEOBJMGR_DELETE_MODE:
	    case LARGEOBJMGR_TRUNCATE_MODE:
	      {
		cur_data_length = largeobjmgr_delete_entry (thread_p, loid,
							    ds,
							    cur_dir_entry_p,
							    process_begin_offset,
							    process_end_offset);
		if (cur_data_length < 0)
		  {
		    ret = cur_data_length;
		    goto exit_on_error;
		  }
	      }
	      break;

	    default:
	      /* invalid mode specification */
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
			1);
		ret = ER_GENERIC_ERROR;
		goto exit_on_error;
	      }
	      break;
	    }

	  /* advance parameters */
	  buffer_ptr += cur_data_length;
	  processed_length += cur_data_length;
	  cur_slot_begin_offset += cur_slot_length;

	  /* move to the next non-empty directory entry */
	  lo_scan = largeobjmgr_dir_scan_next (thread_p, ds);
	  if (lo_scan == S_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
    }

  if (processed_length < length)
    {
      /* We do not have that much length to process at given offset, unless
         we are writting/appending. */
      if (opr_mode == LARGEOBJMGR_WRITE_MODE)
	{
	  /* append data to the end */
	  xlength = largeobjmgr_append_internal (thread_p, loid, ds,
						 offset + processed_length,
						 (int) (length -
							processed_length),
						 buffer + processed_length);
	  if (xlength < 0)
	    {
	      ret = xlength;
	      goto exit_on_error;
	    }
	  processed_length += xlength;
	}
    }

  largeobjmgr_dir_close (thread_p, ds);

end:

  return processed_length;

exit_on_error:

  largeobjmgr_dir_close (thread_p, ds);

  if (page_ptr)
    {
      pgbuf_unfix (thread_p, page_ptr);
      page_ptr = NULL;
    }

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }

  processed_length = ret;	/* ERROR */

  goto end;
}

/*
 * largeobjmgr_compress_data () - Compress the large object data pages.
 *   return: NO_ERROR
 *   loid(in): Large  Object Identifier
 */
static int
largeobjmgr_compress_data (THREAD_ENTRY * thread_p, LOID * loid)
{
  LARGEOBJMGR_DIRSTATE ds_info, *ds;
  LARGEOBJMGR_DIRSTATE_POS ds_pos;
  SCAN_CODE lo_scan;
  SCAN_CODE temp_scan;
  LARGEOBJMGR_DIRENTRY cur_dir_entry;
  LARGEOBJMGR_DIRENTRY *cur_temp_dir_entry_p;
  LARGEOBJMGR_DIRENTRY temp_dir_entry, temp_entry;
  PAGE_PTR page_ptr = NULL;
  int nrecs;
  PAGE_PTR temp_page_ptr = NULL;
  RECDES recdes;
  char *buffer_ptr;
  int buffer_len;
  RECDES data_recdes;
  char *rec_data = NULL;
  int append_length;
  int appended_length;
  int curr_append_length;
  LOG_DATA_ADDR addr;
  int ret = NO_ERROR;

  ds = &ds_info;

  addr.vfid = &loid->vfid;

  /* initializations */
  recdes.data = NULL;
  recdes.type = REC_HOME;
  rec_data = NULL;
  page_ptr = NULL;
  temp_page_ptr = NULL;

  /* first compress data pages */
  lo_scan = largeobjmgr_dir_open (thread_p, loid, 0,
				  LARGEOBJMGR_COMPRESS_MODE, ds);
  if (lo_scan == S_ERROR)
    {
      return ER_FAILED;
    }

  if (lo_scan == S_END)
    {
      return NO_ERROR;
    }

  /* allocate area for compression */
  recdes.area_size = LARGEOBJMGR_MAX_DATA_SLOT_SIZE;
  data_recdes.area_size = LARGEOBJMGR_MAX_DATA_SLOT_SIZE;

  recdes.data = (char *) malloc (recdes.area_size);
  if (recdes.data == NULL)
    {
      goto exit_on_error;
    }

  rec_data = (char *) malloc (data_recdes.area_size);
  if (rec_data == NULL)
    {
      goto exit_on_error;
    }

  /* Scan each directory entry */
  do
    {
      /* get current directory data slot X */
      LARGEOBJMGR_COPY_DIRENTRY (&cur_dir_entry, ds->curdir.idxptr);

      if (!LARGEOBJMGR_ISHOLE_DIRENTRY (&cur_dir_entry)
	  && cur_dir_entry.length < LARGEOBJMGR_MIN_DONT_COMPRESS_SLOT_SIZE)
	{
	  /* compress this entry */

	  page_ptr = pgbuf_fix (thread_p, &cur_dir_entry.u.vpid, OLD_PAGE,
				PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (page_ptr == NULL)
	    {
	      goto exit_on_error;
	    }

	  if (spage_get_record (page_ptr, cur_dir_entry.slotid, &recdes, COPY)
	      != S_SUCCESS)
	    {
	      goto exit_on_error;
	    }

	  /* enlarge the record by appending */

	  nrecs = spage_number_of_records (page_ptr);
	  append_length = ((nrecs == 1)
			   ? largeobjmgr_max_append_putin (thread_p, page_ptr,
							   cur_dir_entry.
							   length)
			   : (LARGEOBJMGR_MAX_DATA_SLOT_SIZE -
			      cur_dir_entry.length));
	  appended_length = 0;
	  buffer_ptr = (char *) recdes.data + cur_dir_entry.length;
	  buffer_len = cur_dir_entry.length;
	  largeobjmgr_dir_get_pos (ds, &ds_pos);

	  while (appended_length < append_length)
	    {
	      temp_scan = largeobjmgr_dir_scan_next (thread_p, ds);
	      if (temp_scan == S_ERROR)
		{
		  goto exit_on_error;
		}
	      if (temp_scan == S_END)
		{
		  break;
		}

	      /* read next entry data */
	      cur_temp_dir_entry_p = ds->curdir.idxptr;

	      if (LARGEOBJMGR_ISHOLE_DIRENTRY (cur_temp_dir_entry_p))
		{
		  /* A hole was found. We cannot compress anything to the right
		     of the hole with the left of the hole. */
		  break;
		}

	      data_recdes.data = (char *) rec_data;
	      temp_page_ptr = pgbuf_fix (thread_p,
					 &cur_temp_dir_entry_p->u.vpid,
					 OLD_PAGE, PGBUF_LATCH_WRITE,
					 PGBUF_UNCONDITIONAL_LATCH);
	      if (temp_page_ptr == NULL)
		{
		  goto exit_on_error;
		}

	      if (spage_get_record
		  (temp_page_ptr, cur_temp_dir_entry_p->slotid, &data_recdes,
		   COPY) != S_SUCCESS)
		{
		  goto exit_on_error;
		}

	      curr_append_length =
		MIN (cur_temp_dir_entry_p->length,
		     append_length - appended_length);

	      memcpy (buffer_ptr, (char *) data_recdes.data,
		      curr_append_length);

	      if (curr_append_length == cur_temp_dir_entry_p->length)
		{
		  /* Delete the current slot at this point since its record has
		     been merged with previous slot. */

		  /* first log the record content for UNDO/REDO purposes */
		  addr.pgptr = temp_page_ptr;
		  addr.offset = cur_temp_dir_entry_p->slotid;
		  log_append_undoredo_recdes (thread_p, RVLOM_DELETE, &addr,
					      &data_recdes, NULL);

		  if (spage_delete
		      (thread_p, temp_page_ptr,
		       cur_temp_dir_entry_p->slotid) !=
		      cur_temp_dir_entry_p->slotid)
		    {
		      goto exit_on_error;
		    }

		  if ((spage_number_of_records (temp_page_ptr) == 0)
		      && !VPID_EQ (&cur_temp_dir_entry_p->u.vpid,
				   &(ds->goodvpid_fordata)))
		    {
		      /* Deallocate the page if it becomes empty */
		      pgbuf_set_dirty (thread_p, temp_page_ptr, FREE);
		      temp_page_ptr = NULL;
		      ret = file_dealloc_page (thread_p, &loid->vfid,
					       &cur_temp_dir_entry_p->u.vpid);
		      if (ret != NO_ERROR)
			{
			  goto exit_on_error;
			}
		    }
		  else
		    {
		      pgbuf_set_dirty (thread_p, temp_page_ptr, FREE);
		      temp_page_ptr = NULL;
		    }

		  /* delete the corresponding directory entry */
		  ret = largeobjmgr_delete_dir (thread_p, ds);
		  if (ret != NO_ERROR)
		    {
		      goto exit_on_error;
		    }
		}
	      else
		{
		  /* Update the entry. Only a portion of the record is removed */
		  ret = largeobjmgr_sp_takeout (thread_p, loid, temp_page_ptr,
						cur_temp_dir_entry_p->slotid,
						0, curr_append_length);
		  if (ret != NO_ERROR)
		    {
		      goto exit_on_error;
		    }

		  data_recdes.length -= curr_append_length;
		  data_recdes.data += curr_append_length;

		  pgbuf_set_dirty (thread_p, temp_page_ptr, FREE);
		  temp_page_ptr = NULL;

		  /* update the corresponding directory entry for new length */
		  LARGEOBJMGR_COPY_DIRENTRY (&temp_entry,
					     cur_temp_dir_entry_p);
		  temp_entry.length = data_recdes.length;
		  ret = largeobjmgr_dir_update (thread_p, ds, &temp_entry);
		  if (ret != NO_ERROR)
		    {
		      goto exit_on_error;
		    }
		}

	      /* reset parameters */
	      appended_length += curr_append_length;
	      buffer_ptr += curr_append_length;
	      buffer_len += curr_append_length;
	    }

	  ret = largeobjmgr_dir_put_pos (thread_p, ds, &ds_pos);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

	  if (appended_length == 0)
	    {
	      /* record not changed */
	      pgbuf_unfix (thread_p, page_ptr);
	      page_ptr = NULL;
	    }
	  else
	    {
	      /* We would like to add this compression into a single page.
	         That is, we do not want pieces of it. */

	      if (nrecs == 1)
		{
		  recdes.length = buffer_len;

		  /* The update must fit into same page */
		  ret = largeobjmgr_sp_append (thread_p, loid, page_ptr,
					       cur_dir_entry.slotid,
					       cur_dir_entry.length,
					       (buffer_len -
						cur_dir_entry.length),
					       ((char *) recdes.data +
						cur_dir_entry.length));
		  if (ret != NO_ERROR)
		    {
		      goto exit_on_error;
		    }

		  /* update the corresponding directory entry for new length */
		  LARGEOBJMGR_COPY_DIRENTRY (&temp_dir_entry, &cur_dir_entry);
		  temp_dir_entry.length = recdes.length;
		  ret = largeobjmgr_dir_update (thread_p, ds,
						&temp_dir_entry);
		  if (ret != NO_ERROR)
		    {
		      goto exit_on_error;
		    }

		  pgbuf_set_dirty (thread_p, page_ptr, FREE);
		  page_ptr = NULL;
		}
	      else
		{
		  /*
		   * The compression does not fit into current page.
		   * Delete the slot from current page and store the record at
		   * another new page.
		   */
		  addr.pgptr = page_ptr;
		  addr.offset = cur_dir_entry.slotid;

		  log_append_undoredo_recdes (thread_p, RVLOM_DELETE, &addr,
					      &recdes, NULL);

		  recdes.length = buffer_len;

		  if (spage_delete (thread_p, page_ptr, cur_dir_entry.slotid)
		      != cur_dir_entry.slotid)
		    {
		      goto exit_on_error;
		    }

		  pgbuf_set_dirty (thread_p, page_ptr, FREE);
		  page_ptr = NULL;

		  /* Now add the record into a new page */

		  page_ptr = largeobjmgr_getnewpage (thread_p, loid,
						     &temp_dir_entry.u.vpid,
						     &cur_dir_entry.u.vpid);
		  if (page_ptr == NULL)
		    {
		      goto exit_on_error;
		    }

		  /* insert the record to the page */
		  if (spage_insert (thread_p, page_ptr, &recdes,
				    &temp_dir_entry.slotid) != SP_SUCCESS)
		    {
		      goto exit_on_error;
		    }

		  /* Log the insertion */
		  addr.pgptr = page_ptr;
		  addr.offset = temp_dir_entry.slotid;
		  log_append_undoredo_recdes (thread_p, RVLOM_INSERT, &addr,
					      NULL, &recdes);

		  /* update the corresponding directory entry for new length */
		  temp_dir_entry.length = recdes.length;
		  ret = largeobjmgr_dir_update (thread_p, ds,
						&temp_dir_entry);
		  if (ret != NO_ERROR)
		    {
		      goto exit_on_error;
		    }

		  /* log, setdirty page and free */
		  pgbuf_set_dirty (thread_p, page_ptr, FREE);
		  page_ptr = NULL;
		}
	    }
	}
    }
  while ((lo_scan = largeobjmgr_dir_scan_next (thread_p, ds)) == S_SUCCESS);

  if (lo_scan == S_ERROR)
    {
      goto exit_on_error;
    }

  largeobjmgr_dir_close (thread_p, ds);
  free_and_init (recdes.data);
  recdes.data = NULL;
  free_and_init (rec_data);
  rec_data = NULL;

end:

  return ret;

exit_on_error:

  if (recdes.data)
    {
      free_and_init (recdes.data);
      recdes.data = NULL;
    }

  if (rec_data)
    {
      free_and_init (rec_data);
      rec_data = NULL;
    }

  if (page_ptr)
    {
      pgbuf_unfix (thread_p, page_ptr);
      page_ptr = NULL;
    }

  if (temp_page_ptr)
    {
      pgbuf_unfix (thread_p, temp_page_ptr);
      temp_page_ptr = NULL;
    }

  largeobjmgr_dir_close (thread_p, ds);

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }
  goto end;
}

/*
 * xlargeobjmgr_create () - A long object is created for the given data and Long Object
 *                  Identifier is set
 *   return: Pointer to the created LOID on success, or NULL on failure
 *   loid(in): the created Large Object Identifier
 *   length(in): Length of the data in buffer
 *   buffer(in): Data buffer area
 *   est_lo_len(in): Estimated total large object length (>length or -1)
 *   oid(in): OID of the object associated with this OID
 */
LOID *
xlargeobjmgr_create (THREAD_ENTRY * thread_p, LOID * loid, int length,
		     char *buffer, int est_lo_len, OID * oid)
{
  int est_data_pg_cnt;
  int dir_pg_cnt;
  int dir_ind_pg_cnt;
  int tot_pg_cnt;
  FILE_LO_DES lodes;

  /* create a file descriptor */
  if (oid != NULL)
    {
      COPY_OID (&lodes.oid, oid);
    }
  else
    {
      OID_SET_NULL (&lodes.oid);
    }

  /*
   * First compute the number of data pages and directory pages
   * required for the given data.
   */
  est_data_pg_cnt = ((est_lo_len <= length)
		     ? ((length > 0)
			? CEIL_PTVDIV (length, LARGEOBJMGR_MAX_DATA_SLOT_SIZE)
			: 0) : CEIL_PTVDIV (est_lo_len,
					    LARGEOBJMGR_MAX_DATA_SLOT_SIZE));

  largeobjmgr_init_dir_pagecnt (est_data_pg_cnt, &dir_pg_cnt,
				&dir_ind_pg_cnt);
  tot_pg_cnt = dir_pg_cnt + dir_ind_pg_cnt;

  loid->vfid.volid = 0;

  /* Create a file for LO with total number of estimated pages */
  if (file_create (thread_p, &loid->vfid, tot_pg_cnt + est_data_pg_cnt,
		   FILE_LONGDATA, &lodes, NULL, 0) == NULL)
    {
      return NULL;
    }

  /* Allocate the pages needed at this time */
  if (file_alloc_pages (thread_p, &loid->vfid, &loid->vpid, tot_pg_cnt,
			NULL, NULL, NULL) == NULL)
    {
      (void) file_destroy (thread_p, &loid->vfid);
      return NULL;
    }

  /* Create the directory for the LO */
  if (largeobjmgr_dir_create (thread_p, loid, 0, dir_ind_pg_cnt, dir_pg_cnt,
			      0, LARGEOBJMGR_MAX_DATA_SLOT_SIZE) != NO_ERROR)
    {
      (void) file_destroy (thread_p, &loid->vfid);
      return NULL;
    }

  if (length > 0
      && xlargeobjmgr_append (thread_p, loid, length, buffer) != length)
    {
      (void) file_destroy (thread_p, &loid->vfid);
      return NULL;
    }

  return loid;
}

/*
 * xlargeobjmgr_destroy () - Destroy the given large object by destroying the
 *                   associated file
 *   return: NO_ERROR
 *   loid(in): Large Object Identifier
 */
int
xlargeobjmgr_destroy (THREAD_ENTRY * thread_p, LOID * loid)
{
  return file_destroy (thread_p, &loid->vfid);
}

/*
 * xlargeobjmgr_read () - Read length bytes from the large object starting from offset
 *   return: Length of the data read on success, ER_code on failure
 *   loid(in): Large Object Identifier
 *   offset(in): Offset where read is to start
 *   length(in): Length of data to be read
 *   buffer(in): Buffer where data read is to be copied
 */
int
xlargeobjmgr_read (THREAD_ENTRY * thread_p, LOID * loid, INT64 offset,
		   int length, char *buffer)
{
  return (int) largeobjmgr_process (thread_p, loid, LARGEOBJMGR_READ_MODE,
				    offset, length, buffer);
}

/*
 * largeobjmgr_system_op () -
 *   return: Length of the data written on success, ER_code on failure
 *   loid(in): Large Object Identifier
 *   op_mode(in): Operation mode code
 *   offset(in): Offset where write is to start
 *   length(in): Length of data to be written
 *   buffer(in):Buffer from which data is to be copied
 */
static INT64
largeobjmgr_system_op (THREAD_ENTRY * thread_p, LOID * loid, int op_mode,
		       INT64 offset, INT64 length, char *buffer)
{
  INT64 ret_length = ER_FAILED;


  if (log_start_system_op (thread_p) == NULL)
    {
      return ER_FAILED;
    }

  /* Note that the comparasion need to be less than since we can add a hole. */
  ret_length = largeobjmgr_process (thread_p, loid, op_mode, offset,
				    length, buffer);
  if (ret_length < length)
    {
      goto exit_on_error;
    }

  log_end_system_op (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER);

end:

  return ret_length;

exit_on_error:

  log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);

  if (ret_length == NO_ERROR)
    {
      ret_length = er_errid ();
      if (ret_length == NO_ERROR)
	{
	  ret_length = ER_FAILED;
	}
    }
  goto end;
}

/*
 * xlargeobjmgr_write () - Overwrite length bytes of data given in the buffer to the
 *                 large object area starting from offset
 *   return: Length of the data written on success, ER_code on failure
 *   loid(in): Large Object Identifier
 *   offset(in): Offset where write is to start
 *   length(in): Length of data to be written
 *   buffer(in):Buffer from which data is to be copied
 */
int
xlargeobjmgr_write (THREAD_ENTRY * thread_p, LOID * loid, INT64 offset,
		    int length, char *buffer)
{
  return (int) largeobjmgr_system_op (thread_p, loid, LARGEOBJMGR_WRITE_MODE,
				      offset, length, buffer);
}

/*
 * xlargeobjmgr_delete () - Delete length bytes of large object from the given offset
 *   return: Length of the data deleted on success, ER_code on failure
 *   loid(in): Large Object Identifier
 *   offset(in): Offset where deletion is to start
 *   length(in): Length of data to be deleted
 */
INT64
xlargeobjmgr_delete (THREAD_ENTRY * thread_p, LOID * loid, INT64 offset,
		     INT64 length)
{
  return largeobjmgr_system_op (thread_p, loid, LARGEOBJMGR_DELETE_MODE,
				offset, length, NULL);
}

/*
 * xlargeobjmgr_truncate () - Truncate the large object from the given offset
 *   return: lenght of data truncated or ER_code on error
 *   loid(in): Large Object Identifier
 *   offset(in): Offset to truncate large object from
 */
INT64
xlargeobjmgr_truncate (THREAD_ENTRY * thread_p, LOID * loid, INT64 offset)
{
  return largeobjmgr_system_op (thread_p, loid, LARGEOBJMGR_TRUNCATE_MODE,
				offset, -1, NULL);
}

/*
 * xlargeobjmgr_insert () - Insert given data at offset
 *   return: Length of the data inserted on success, ER_code on failure
 *   loid(in): Large object identifier
 *   offset(in): Offset where data is to be inserted
 *   length(in): Length of data to be inserted
 *   buffer(in): Data to be inserted
 */
int
xlargeobjmgr_insert (THREAD_ENTRY * thread_p, LOID * loid, INT64 offset,
		     int length, char *buffer)
{
  LARGEOBJMGR_DIRSTATE ds_info, *ds;
  int inserted_length;
  int ret = ER_FAILED;

  ds = &ds_info;

  if (length <= 0)
    {
      return 0;
    }

  if (log_start_system_op (thread_p) == NULL)
    {
      return ER_FAILED;
    }

  /* locate the directory entry to point to the entry that contains offset */
  if (largeobjmgr_dir_open (thread_p, loid, offset, LARGEOBJMGR_INSERT_MODE,
			    ds) == S_ERROR)
    {
      goto exit_on_error;
    }

  if (offset >= ds->tot_length)
    {
      inserted_length = largeobjmgr_append_internal (thread_p, loid, ds,
						     offset, length, buffer);
      if (inserted_length < length)
	{
	  largeobjmgr_dir_close (thread_p, ds);
	  goto exit_on_error;
	}
    }
  else
    {
      inserted_length = largeobjmgr_insert_internal (thread_p, loid, ds,
						     offset, length, buffer);
      if (inserted_length < length)
	{
	  largeobjmgr_dir_close (thread_p, ds);
	  goto exit_on_error;
	}
    }

  largeobjmgr_dir_close (thread_p, ds);
  log_end_system_op (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER);

end:

  return inserted_length;

exit_on_error:

  log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }

  inserted_length = ret;	/* ERROR */

  goto end;
}

/*
 * xlargeobjmgr_append () - Append given data to the end of the large object
 *   return: Length of the data appended on success, ER_code on failure
 *   loid(in): Large object identifier
 *   length(in): Length of data to be appended
 *   buffer(in): Data to be appended
 */
int
xlargeobjmgr_append (THREAD_ENTRY * thread_p, LOID * loid, int length,
		     char *buffer)
{
  LARGEOBJMGR_DIRSTATE ds_info, *ds;
  int appended_length;
  int ret = ER_FAILED;

  ds = &ds_info;

  if (length <= 0)
    {
      return 0;
    }

  if (log_start_system_op (thread_p) == NULL)
    {
      return ER_FAILED;
    }

  /* locate the directory entry to point to the last entry */
  if (largeobjmgr_dir_open (thread_p, loid, -1, LARGEOBJMGR_APPEND_MODE, ds)
      == S_ERROR)
    {
      goto exit_on_error;
    }

  appended_length = largeobjmgr_append_internal (thread_p, loid, ds,
						 ds->tot_length, length,
						 buffer);
  if (appended_length != length)
    {
      largeobjmgr_dir_close (thread_p, ds);
      goto exit_on_error;
    }

  largeobjmgr_dir_close (thread_p, ds);
  log_end_system_op (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER);

end:

  return appended_length;

exit_on_error:

  log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }

  appended_length = ret;	/* ERROR */

  goto end;
}

/*
 * xlargeobjmgr_compress () - Compress the large object
 *   return:
 *   loid(in): Large Object Identifier
 */
int
xlargeobjmgr_compress (THREAD_ENTRY * thread_p, LOID * loid)
{
  int ret = NO_ERROR;

  if (log_start_system_op (thread_p) == NULL)
    {
      return ER_FAILED;
    }

  ret = largeobjmgr_compress_data (thread_p, loid);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  if (largeobjmgr_dir_compress (thread_p, loid) != NO_ERROR)
    {
      goto exit_on_error;
    }

  log_end_system_op (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER);

end:

  return ret;

exit_on_error:

  log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }
  goto end;
}

/*
 * xlargeobjmgr_length () - Return the length of large object
 *   return: length of large object or -1
 *   loid(in): Large Object Identifier
 */
INT64
xlargeobjmgr_length (THREAD_ENTRY * thread_p, LOID * loid)
{
  return largeobjmgr_dir_get_lolength (thread_p, loid);
}

/* RECOVERY ROUTINES */

/*
 * largeobjmgr_rv_insert () - Insert the record at the slot indicated by recv
 *   return: 0 if no error, or error code
 *   recv(in): recdesovery structure
 */
int
largeobjmgr_rv_insert (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  INT16 slotid;
  RECDES recdes;
  int sp_success;

  slotid = recv->offset;
  recdes.type = *(INT16 *) (recv->data);
  recdes.data = (char *) (recv->data) + sizeof (recdes.type);
  recdes.area_size = recdes.length = recv->length - sizeof (recdes.type);

  sp_success = spage_insert_for_recovery (thread_p, recv->pgptr, slotid,
					  &recdes);
  if (sp_success != SP_SUCCESS)
    {
      if (sp_success != SP_ERROR)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
		  0);
	}

      return er_errid ();
    }

  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * largeobjmgr_rv_delete () - Delete the record at the slot indicated by recv
 *   return: 0 if no error, or error code
 *   recv(in): recdesovery structure
 */
int
largeobjmgr_rv_delete (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  if (spage_delete_for_recovery (thread_p, recv->pgptr, (INT16) recv->offset)
      == NULL_SLOTID)
    {
      return er_errid ();
    }

  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * largeobjmgr_rv_get_newpage_undo () - Deallocate a newly allocated LO data page for
 *                              UNDO purposes
 *   return: 0 if no error, or error code
 *   recv(in): recdesovery structure
 */
int
largeobjmgr_rv_get_newpage_undo (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  VFID *vfid;

  /* NOTE: change the page and set it dirty to avoid complaints
   *       by the recovery manager. This is safe since page is to
   *       be deallocated.
   */
  *(int *) recv->pgptr = -1;
  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);

  vfid = (VFID *) recv->data;
  (void) file_dealloc_page (thread_p, vfid, pgbuf_get_vpid_ptr (recv->pgptr));

  return NO_ERROR;
}

/*
 * largeobjmgr_rv_get_newpage_redo () - Initialize a newly allocated LO data page for
 *                              REDO purposes
 *   return: 0 if no error, or error code
 *   recv(in): recdesovery structure
 */
int
largeobjmgr_rv_get_newpage_redo (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  spage_initialize (thread_p, recv->pgptr, ANCHORED, CHAR_ALIGNMENT, false);
  return NO_ERROR;
}

/*
 * largeobjmgr_rv_split_undo () - Undo the split of a record
 *   return:
 *   rcv(in):
 */
int
largeobjmgr_rv_split_undo (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  int sp_success;
  INT16 slotid;
  LARGEOBJMGR_RV_SLOT *lomrv;

  slotid = rcv->offset;
  lomrv = (LARGEOBJMGR_RV_SLOT *) (rcv->data);

  sp_success = spage_merge (thread_p, rcv->pgptr, slotid, lomrv->slotid);
  if (sp_success != SP_SUCCESS)
    {
      if (sp_success != SP_ERROR)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
		  0);
	}

      return er_errid ();
    }

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * largeobjmgr_rv_split_redo () - Redo the split of a record
 *   return: 0 if no error, or error code
 *   recv(in): recdesovery structure
 */
int
largeobjmgr_rv_split_redo (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  int sp_success;
  INT16 slotid;
  INT16 new_slotid;
  LARGEOBJMGR_RV_SLOT *lomrv;

  slotid = rcv->offset;
  lomrv = (LARGEOBJMGR_RV_SLOT *) (rcv->data);

  sp_success = spage_split (thread_p, rcv->pgptr, slotid, lomrv->offset,
			    &new_slotid);
  if (sp_success != SP_SUCCESS)
    {
      if (sp_success != SP_ERROR)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
		  0);
	}

      return er_errid ();
    }

  if (new_slotid != lomrv->slotid)
    {
      er_log_debug (ARG_FILE_LINE,
		    "largeobjmgr_rv_split_redo: WARNING record was split"
		    " different. New_slotid = %d.. expected new_slotid = %d",
		    new_slotid, lomrv->slotid);
    }

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * largeobjmgr_rv_split_dump () - Dump information about splitting a record
 *   return: void
 *   length_ignore(in): Length of recdesovery Data
 *   data(in): The data being logged
 */
void
largeobjmgr_rv_split_dump (FILE * fp, int length_ignore, void *data)
{
  largeobjmgr_rv_slot_dump (fp, (LARGEOBJMGR_RV_SLOT *) data);
}

/*
 * largeobjmgr_rv_overwrite () - Undo/redo the overwritten portion of the record
 *   return: 0 if no error, or error code
 *   recv(in): recdesovery structure
 */
int
largeobjmgr_rv_overwrite (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  int sp_success;
  INT16 slotid;
  int offset;
  RECDES recdes;
  char *data;

  data = (char *) rcv->data;
  slotid = rcv->offset;

  offset = *(int *) data;
  data += sizeof (offset);

  recdes.type = REC_HOME;
  recdes.data = data;
  recdes.area_size = recdes.length = rcv->length - sizeof (offset);

  sp_success = spage_overwrite (thread_p, rcv->pgptr, slotid, offset,
				&recdes);
  if (sp_success != SP_SUCCESS)
    {
      if (sp_success != SP_ERROR)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
		  0);
	}

      return er_errid ();
    }

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * largeobjmgr_rv_overwrite_dump () - Dump information about record overwrite
 *   return:
 *   length(in):  Length of recdesovery Data
 *   dump_data(in): The data being logged
 */
void
largeobjmgr_rv_overwrite_dump (FILE * fp, int length, void *dump_data)
{
  int offset;
  char *data;

  data = (char *) dump_data;

  offset = *(int *) data;
  data += sizeof (offset);
  length -= sizeof (offset);

  fprintf (fp, "Offset = %d, Overwrite_data =", offset);
  log_rv_dump_char (fp, length, data);
}

/*
 * largeobjmgr_rv_putin () - Undo the partial removal of data from record or redo the
 *                   additon of data
 *   return: 0 if no error, or error code
 *   recv(in): recdesovery structure
 */
int
largeobjmgr_rv_putin (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  char *data;
  int sp_success;
  RECDES recdes;
  LARGEOBJMGR_RV_SLOT *lomrv;

  data = (char *) rcv->data;
  lomrv = (LARGEOBJMGR_RV_SLOT *) data;
  data += sizeof (LARGEOBJMGR_RV_SLOT);

  recdes.type = REC_HOME;
  recdes.data = data;
  recdes.area_size = recdes.length = lomrv->length;

  sp_success = spage_put (thread_p, rcv->pgptr, lomrv->slotid, lomrv->offset,
			  &recdes);
  if (sp_success != SP_SUCCESS)
    {
      if (sp_success != SP_ERROR)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
		  0);
	}

      return er_errid ();
    }

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * largeobjmgr_rv_takeout () - Redo the partial removal of data from record or undo
 *                     the addtionof data
 *   return: 0 if no error, or error code
 *   recv(in): recdesovery structure
 */
int
largeobjmgr_rv_takeout (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  int sp_success;
  LARGEOBJMGR_RV_SLOT *lomrv;

  lomrv = (LARGEOBJMGR_RV_SLOT *) rcv->data;

  sp_success = spage_take_out (thread_p, rcv->pgptr, lomrv->slotid,
			       lomrv->offset, lomrv->length);
  if (sp_success != SP_SUCCESS)
    {
      if (sp_success != SP_ERROR)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
		  0);
	}

      return er_errid ();
    }

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * largeobjmgr_rv_putin_dump () -
 *   return: void
 *   length_ignore(in): Length of recdesovery Data
 *   data(in): The data being logged
 */
void
largeobjmgr_rv_putin_dump (FILE * fp, int length_ignore, void *data)
{
  largeobjmgr_rv_slot_dump (fp, (LARGEOBJMGR_RV_SLOT *) data);
}

/*
 * largeobjmgr_rv_takeout_dump () -
 *   return: void
 *   length_ignore(in): Length of recdesovery Data
 *   dump_data(in): The data being logged
 */
void
largeobjmgr_rv_takeout_dump (FILE * fp, int length_ignore, void *dump_data)
{
  char *data;
  LARGEOBJMGR_RV_SLOT *lomrv;

  data = (char *) dump_data;
  lomrv = (LARGEOBJMGR_RV_SLOT *) data;
  data += sizeof (LARGEOBJMGR_RV_SLOT);

  largeobjmgr_rv_slot_dump (fp, lomrv);
  log_rv_dump_char (fp, lomrv->length, data);
}

/*
 * largeobjmgr_rv_append_redo () - Redo the append of data to the slot
 *   return: 0 if no error, or error code
 *   recv(in): recdesovery structure
 */
int
largeobjmgr_rv_append_redo (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  INT16 slotid;
  RECDES recdes;
  int sp_success;

  slotid = rcv->offset;

  recdes.area_size = recdes.length = rcv->length;
  recdes.data = (char *) rcv->data;

  sp_success = spage_append (thread_p, rcv->pgptr, slotid, &recdes);
  if (sp_success != SP_SUCCESS)
    {
      if (sp_success != SP_ERROR)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
		  0);
	}

      return er_errid ();
    }

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * largeobjmgr_rv_append_undo () -
 *   return: 0 if no error, or error code
 *   recv(in): recdesovery structure
 */
int
largeobjmgr_rv_append_undo (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  INT16 slotid;
  int sp_success;
  LARGEOBJMGR_RV_SLOT *lomrv;

  slotid = rcv->offset;
  lomrv = (LARGEOBJMGR_RV_SLOT *) rcv->data;

  sp_success = spage_take_out (thread_p, rcv->pgptr, lomrv->slotid,
			       lomrv->offset, lomrv->length);
  if (sp_success != SP_SUCCESS)
    {
      if (sp_success != SP_ERROR)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
		  0);
	}

      return er_errid ();
    }

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * largeobjmgr_rv_append_dump_undo () -
 *   return:
 *   length_ignore(in):
 *   dump_data(in):
 */
void
largeobjmgr_rv_append_dump_undo (FILE * fp, int length_ignore,
				 void *dump_data)
{
  char *data;
  LARGEOBJMGR_RV_SLOT *lomrv;

  data = (char *) dump_data;
  lomrv = (LARGEOBJMGR_RV_SLOT *) data;
  largeobjmgr_rv_slot_dump (fp, lomrv);
}
