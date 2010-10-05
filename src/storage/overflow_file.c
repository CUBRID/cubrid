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
 * overflow_file.c - Overflow file manager (at server)
 */

#ident "$Id$"

#include "config.h"

#include <string.h>

#include "storage_common.h"
#include "memory_alloc.h"
#include "error_manager.h"
#include "page_buffer.h"
#include "file_manager.h"
#include "slotted_page.h"
#include "log_manager.h"
#include "overflow_file.h"

#define OVERFLOW_ALLOCVPID_ARRAY_SIZE 64

typedef struct overflow_first_part OVERFLOW_FIRST_PART;
struct overflow_first_part
{
  VPID next_vpid;
  int length;
  char data[1];			/* Really more than one */
};

typedef struct overflow_rest_part OVERFLOW_REST_PART;
struct overflow_rest_part
{
  VPID next_vpid;
  char data[1];			/* Really more than one */
};

typedef struct overflow_recv_links OVERFLOW_RECV_LINKS;
struct overflow_recv_links
{
  VFID ovf_vfid;
  VPID new_vpid;
};

typedef enum
{
  OVERFLOW_DO_DELETE,
  OVERFLOW_DO_FLUSH
} OVERFLOW_DO_FUNC;

static void overflow_next_vpid (const VPID * ovf_vpid, VPID * vpid,
				PAGE_PTR pgptr);
static const VPID *overflow_traverse (THREAD_ENTRY * thread_p,
				      const VFID * ovf_vfid,
				      const VPID * ovf_vpid,
				      OVERFLOW_DO_FUNC func);
static int overflow_delete_internal (THREAD_ENTRY * thread_p,
				     const VFID * ovf_vfid, VPID * vpid,
				     PAGE_PTR pgptr);
static int overflow_flush_internal (THREAD_ENTRY * thread_p, PAGE_PTR pgptr);

/*
 * overflow_insert () - Insert a multipage data in overflow
 *   return: ovf_vpid on success or NULL on failure
 *   ovf_vfid(in): File where the overflow data is going to be stored
 *   ovf_vpid(out): Overflow address
 *   recdes(in): Record descriptor
 *
 * Note: Data in overflow is composed of several pages. Pages in the overflow
 *       area are not shared among other pieces of overflow data.
 *
 *       --------------------------------         ------------------------
 *       |Next_vpid |Length|... data ...| ... --> |Next_vpid|... data ...|
 *       --------------------------------         ------------------------
 *
 *       Single link list of pages.
 *       The length of the multipage data is stored on its first overflow page
 *
 *       Overflow pages are not locked in any mode since they are not shared
 *       by other pieces of data and its address is only know by accessing the
 *       relocation overflow record data which has been appropriately locked.
 */
VPID *
overflow_insert (THREAD_ENTRY * thread_p, const VFID * ovf_vfid,
		 VPID * ovf_vpid, RECDES * recdes)
{
  PAGE_PTR vfid_fhdr_pgptr = NULL;
  OVERFLOW_FIRST_PART *first_part;
  OVERFLOW_REST_PART *rest_parts;
  OVERFLOW_RECV_LINKS undo_recv;
  char *copyto;
  int length, copy_length;
  INT32 npages = 0;
  char *data;
  int alloc_nth;
  LOG_DATA_ADDR addr;
  LOG_DATA_ADDR logical_undoaddr;
  int i;
  VPID *vpids, fhdr_vpid;
  VPID vpids_buffer[OVERFLOW_ALLOCVPID_ARRAY_SIZE + 1];
  FILE_ALLOC_VPIDS alloc_vpids;

  /*
   * We don't need to lock the overflow pages since these pages are not
   * shared among several pieces of overflow data. The overflow pages are
   * know by accessing the relocation-overflow record with the appropiate lock
   */

  addr.vfid = ovf_vfid;
  addr.offset = 0;

  logical_undoaddr.vfid = ovf_vfid;
  logical_undoaddr.offset = 0;
  logical_undoaddr.pgptr = NULL;

  undo_recv.ovf_vfid = *ovf_vfid;

  /*
   * Temporary:
   *   Lock the file header, so I am the only one changing the file table
   *   of allocated pages. This is needed since this function is using
   *   file_find_nthpages, which could give me not the expected page, if someone
   *   else remove pages, after the initial allocation.
   */

  fhdr_vpid.volid = ovf_vfid->volid;
  fhdr_vpid.pageid = ovf_vfid->fileid;

  vfid_fhdr_pgptr = pgbuf_fix (thread_p, &fhdr_vpid, OLD_PAGE,
			       PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (vfid_fhdr_pgptr == NULL)
    {
      return NULL;
    }

  /*
   * Guess the number of pages. The total number of pages is found by
   * dividing length by pagesize - the smallest header. Then, we make sure
   * that this estimate is correct.
   */
  length = recdes->length - (DB_PAGESIZE -
			     (int) offsetof (OVERFLOW_FIRST_PART, data));
  if (length > 0)
    {
      i = DB_PAGESIZE - offsetof (OVERFLOW_REST_PART, data);
      npages = 1 + CEIL_PTVDIV (length, i);
    }
  else
    {
      npages = 1;
    }

  if (npages > OVERFLOW_ALLOCVPID_ARRAY_SIZE)
    {
      vpids = (VPID *) malloc ((npages + 1) * sizeof (VPID));
      if (vpids == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, (npages + 1) * sizeof (VPID));
	  pgbuf_unfix (thread_p, vfid_fhdr_pgptr);
	  return NULL;
	}
    }
  else
    {
      vpids = vpids_buffer;
    }

#if !defined(NDEBUG)
  for (i = 0; i < npages; i++)
    {
      VPID_SET_NULL (&vpids[i]);
    }
#endif

  VPID_SET_NULL (&vpids[npages]);

  alloc_vpids.vpids = vpids;
  alloc_vpids.index = 0;

  /*
   * We do not initialize the pages during allocation since they are not
   * pointed by anyone until we return from this function, at that point
   * they are initialized.
   */
  if (file_alloc_pages_as_noncontiguous (thread_p, ovf_vfid, vpids,
					 &alloc_nth, npages, NULL, NULL,
					 NULL, &alloc_vpids) == NULL)
    {
      if (vpids != vpids_buffer)
	{
	  free_and_init (vpids);
	}

      pgbuf_unfix (thread_p, vfid_fhdr_pgptr);
      return NULL;
    }

  assert (alloc_vpids.index == npages);

#if !defined(NDEBUG)
  for (i = 0; i < npages; i++)
    {
      assert (!VPID_ISNULL (&vpids[i]));
    }
#endif

  *ovf_vpid = vpids[0];

  /* Copy the content of the data */

  data = recdes->data;
  length = recdes->length;

  for (i = 0; i < npages; i++)
    {
      addr.pgptr = pgbuf_fix (thread_p, &vpids[i], NEW_PAGE,
			      PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (addr.pgptr == NULL)
	{
	  goto exit_on_error;
	}

      /* Is this the first page ? */
      if (i == 0)
	{
	  /* This is the first part */
	  first_part = (OVERFLOW_FIRST_PART *) addr.pgptr;

	  first_part->next_vpid = vpids[i + 1];
	  first_part->length = length;
	  copyto = (char *) first_part->data;

	  copy_length = DB_PAGESIZE - offsetof (OVERFLOW_FIRST_PART, data);
	  if (length < copy_length)
	    {
	      copy_length = length;
	    }

	  /* notify the first part of overflow recdes */
	  log_append_empty_record (thread_p, LOG_DUMMY_OVF_RECORD);
	}
      else
	{
	  rest_parts = (OVERFLOW_REST_PART *) addr.pgptr;

	  rest_parts->next_vpid = vpids[i + 1];
	  copyto = (char *) rest_parts->data;

	  copy_length = DB_PAGESIZE - offsetof (OVERFLOW_REST_PART, data);
	  if (length < copy_length)
	    {
	      copy_length = length;
	    }
	}

      memcpy (copyto, data, copy_length);
      data += copy_length;
      length -= copy_length;

      pgbuf_get_vpid (addr.pgptr, &undo_recv.new_vpid);
      if (file_is_new_file (thread_p, ovf_vfid) == FILE_OLD_FILE)
	{
	  /* we don't do undo logging for new files */
	  log_append_undo_data (thread_p, RVOVF_NEWPAGE_LOGICAL_UNDO,
				&logical_undoaddr, sizeof (undo_recv),
				&undo_recv);
	}

      log_append_redo_data (thread_p, RVOVF_NEWPAGE_INSERT, &addr,
			    copy_length +
			    CAST_BUFLEN (copyto - (char *) addr.pgptr),
			    (char *) addr.pgptr);

      pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
      addr.pgptr = NULL;
    }

  assert (length == 0);
#if defined (CUBRID_DEBUG)
  if (length > 0)
    {
      er_log_debug (ARG_FILE_LINE, "ovf_insert: ** SYSTEM ERROR calculation"
		    " of number of pages needed to store overflow data seems"
		    " incorrect. Need no more than %d pages", npages);
      goto exit_on_error;
    }
#endif

  /*
   * Temporary:
   *   Unlock the file header, so I am the only one changing the file table
   *   of allocated pages. This is needed since curently, I am using
   *   file_find_nthpages, which could give me not the expected page, if someone
   *   else remove pages.
   */

  if (vpids != vpids_buffer)
    {
      free_and_init (vpids);
    }

  pgbuf_unfix (thread_p, vfid_fhdr_pgptr);
  return ovf_vpid;

exit_on_error:

  for (i = 0; i < npages; i++)
    {
      (void) file_dealloc_page (thread_p, ovf_vfid, &vpids[i]);
    }

  if (vpids != vpids_buffer)
    {
      free_and_init (vpids);
    }

  pgbuf_unfix (thread_p, vfid_fhdr_pgptr);
  return NULL;
}

/*
 * overflow_next_vpid () -
 *   return: ovf_vpid on success or NULL on failure
 *   ovf_vpid(in): Overflow address
 *   vpid(in/out): current/next vpid
 *   pgptr(in): current page
 */
static void
overflow_next_vpid (const VPID * ovf_vpid, VPID * vpid, PAGE_PTR pgptr)
{
  if (VPID_EQ (ovf_vpid, vpid))
    {
      *vpid = ((OVERFLOW_FIRST_PART *) pgptr)->next_vpid;
    }
  else
    {
      *vpid = ((OVERFLOW_REST_PART *) pgptr)->next_vpid;
    }
}

/*
 * overflow_traverse () -
 *   return: ovf_vpid on success or NULL on failure
 *   ovf_vfid(in): File where the overflow data is stored
 *                 WARNING: MUST BE THE SAME AS IT WAS GIVEN DURING INSERT
 *   ovf_vpid(in): Overflow address
 *   func(in): Overflow address
 */
static const VPID *
overflow_traverse (THREAD_ENTRY * thread_p, const VFID * ovf_vfid,
		   const VPID * ovf_vpid, OVERFLOW_DO_FUNC func)
{
  VPID next_vpid;
  VPID vpid;
  PAGE_PTR pgptr = NULL;

  /*
   * We don't need to lock the overflow pages since these pages are not
   * shared among several pieces of overflow data. The overflow pages are
   * know by accessing the relocation-overflow record with the appropiate lock
   */

  next_vpid = *ovf_vpid;

  while (!(VPID_ISNULL (&next_vpid)))
    {
      pgptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			 PGBUF_UNCONDITIONAL_LATCH);
      if (pgptr == NULL)
	{
	  goto exit_on_error;
	}

      vpid = next_vpid;
      overflow_next_vpid (ovf_vpid, &next_vpid, pgptr);

      switch (func)
	{
	case OVERFLOW_DO_DELETE:
	  if (ovf_vfid == NULL)	/* assert */
	    {
	      goto exit_on_error;
	    }
	  if (overflow_delete_internal (thread_p, ovf_vfid, &vpid, pgptr) !=
	      NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	  break;
	case OVERFLOW_DO_FLUSH:
	  if (overflow_flush_internal (thread_p, pgptr) != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	  break;
	default:
	  break;
	}
    }

  return ovf_vpid;

exit_on_error:

  /* TODO: suspect pgbuf_unfix */
  return NULL;
}

/*
 * overflow_update () - Update the content of a multipage data
 *   return: ovf_vpid on success or NULL on failure
 *   ovf_vfid(in): File where the overflow data is stored
 *                 WARNING: MUST BE THE SAME AS IT WAS GIVEN DURING INSERT
 *   ovf_vpid(in): Overflow address
 *   recdes(in): Record descriptor
 *
 * Note: The function may allocate or deallocate several overflow pages if
 *       the multipage data increase/decrease in length.
 *
 *       Overflow pages are not locked in any mode since they are not shared
 *       by other data and its address is know by accessing the relocation
 *       overflow record which has been appropriately locked.
 */
const VPID *
overflow_update (THREAD_ENTRY * thread_p, const VFID * ovf_vfid,
		 const VPID * ovf_vpid, RECDES * recdes)
{
  OVERFLOW_FIRST_PART *first_part = NULL;
  OVERFLOW_REST_PART *rest_parts = NULL;
  char *copyto;
  OVERFLOW_RECV_LINKS recv;
  VPID tmp_vpid;
  int hdr_length;
  int copy_length;
  int old_length = 0;
  int length;
  char *data;
  VPID next_vpid;
  VPID *addr_vpid_ptr;
  LOG_DATA_ADDR addr;
  LOG_DATA_ADDR logical_undoaddr;
  bool isnewpage = false;

  /*
   * We don't need to lock the overflow pages since these pages are not
   * shared among several pieces of overflow data. The overflow pages are
   * know by accessing the relocation-overflow record with the appropiate lock
   */

  addr.vfid = ovf_vfid;
  addr.offset = 0;
  logical_undoaddr.vfid = ovf_vfid;
  logical_undoaddr.offset = 0;
  logical_undoaddr.pgptr = NULL;

  recv.ovf_vfid = *ovf_vfid;

  next_vpid = *ovf_vpid;

  data = recdes->data;
  length = recdes->length;
  while (length > 0)
    {
      addr.pgptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE,
			      PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (addr.pgptr == NULL)
	{
	  goto exit_on_error;
	}

      addr_vpid_ptr = pgbuf_get_vpid_ptr (addr.pgptr);

      /* Log before and after images */

      /* Is this the first page ? */
      if (VPID_EQ (addr_vpid_ptr, ovf_vpid))
	{
	  /* This is the first part */
	  first_part = (OVERFLOW_FIRST_PART *) addr.pgptr;
	  old_length = first_part->length;

	  copyto = (char *) first_part->data;
	  next_vpid = first_part->next_vpid;

	  hdr_length = offsetof (OVERFLOW_FIRST_PART, data);
	  if ((length + hdr_length) > DB_PAGESIZE)
	    {
	      copy_length = DB_PAGESIZE - hdr_length;
	    }
	  else
	    {
	      copy_length = length;
	    }

	  /* Log before image */
	  if (hdr_length + old_length > DB_PAGESIZE)
	    {
	      log_append_undo_data (thread_p, RVOVF_PAGE_UPDATE, &addr,
				    DB_PAGESIZE, addr.pgptr);
	      old_length -= DB_PAGESIZE - hdr_length;
	    }
	  else
	    {
	      log_append_undo_data (thread_p, RVOVF_PAGE_UPDATE, &addr,
				    hdr_length + old_length, addr.pgptr);
	      old_length = 0;
	    }

	  /* Modify the new length */
	  first_part->length = length;

	  /* notify the first part of overflow recdes */
	  log_append_empty_record (thread_p, LOG_DUMMY_OVF_RECORD);
	}
      else
	{
	  rest_parts = (OVERFLOW_REST_PART *) addr.pgptr;
	  copyto = (char *) rest_parts->data;
	  if (isnewpage == true)
	    {
	      VPID_SET_NULL (&next_vpid);
	      rest_parts->next_vpid = next_vpid;
	    }
	  else
	    {
	      next_vpid = rest_parts->next_vpid;
	    }

	  hdr_length = offsetof (OVERFLOW_REST_PART, data);
	  if ((length + hdr_length) > DB_PAGESIZE)
	    {
	      copy_length = DB_PAGESIZE - hdr_length;
	    }
	  else
	    {
	      copy_length = length;
	    }

	  if (old_length > 0)
	    {
	      if (hdr_length + old_length > DB_PAGESIZE)
		{
		  log_append_undo_data (thread_p, RVOVF_PAGE_UPDATE, &addr,
					DB_PAGESIZE, addr.pgptr);
		  old_length -= DB_PAGESIZE - hdr_length;
		}
	      else
		{
		  log_append_undo_data (thread_p, RVOVF_PAGE_UPDATE, &addr,
					hdr_length + old_length, addr.pgptr);
		  old_length = 0;
		}
	    }
	}

      memcpy (copyto, data, copy_length);
      data += copy_length;
      length -= copy_length;

      log_append_redo_data (thread_p, RVOVF_PAGE_UPDATE, &addr,
			    copy_length + hdr_length, (char *) addr.pgptr);

      if (length > 0)
	{
	  /* Need more pages... Get next page */
	  if (VPID_ISNULL (&next_vpid))
	    {
	      /* We need to allocate a new page */
	      if (file_alloc_pages (thread_p, ovf_vfid, &next_vpid, 1,
				    addr_vpid_ptr, NULL, NULL) == NULL)
		{
		  pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
		  addr.pgptr = NULL;

		  goto exit_on_error;
		}
	      recv.new_vpid = next_vpid;

	      log_append_undoredo_data (thread_p, RVOVF_NEWPAGE_LINK, &addr,
					sizeof (recv), sizeof (next_vpid),
					&recv, &next_vpid);

	      isnewpage = true;	/* So that its link can be set to NULL */

	      /* This logical undo is to remove the page in case of rollback */
	      if (file_is_new_file (thread_p, ovf_vfid) == FILE_OLD_FILE)
		{
		  /* we don't do undo logging for new files */
		  log_append_undo_data (thread_p, RVOVF_NEWPAGE_LOGICAL_UNDO,
					&logical_undoaddr, sizeof (recv),
					&recv);
		}

	      if (rest_parts == NULL)
		{
		  /* This is the first part */
		  first_part->next_vpid = next_vpid;
		}
	      else
		{
		  /* This is part of rest part */
		  rest_parts->next_vpid = next_vpid;
		}
	    }
	  pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
	  addr.pgptr = NULL;
	}
      else
	{
	  /* The content of the data has been copied. We don't need more pages.
	     Deallocate any additional pages */
	  VPID_SET_NULL (&tmp_vpid);

	  log_append_undoredo_data (thread_p, RVOVF_CHANGE_LINK, &addr,
				    sizeof (next_vpid), sizeof (next_vpid),
				    &next_vpid, &tmp_vpid);

	  if (rest_parts == NULL)
	    {
	      /* This is the first part */
	      VPID_SET_NULL (&first_part->next_vpid);
	    }
	  else
	    {
	      /* This is part of rest part */
	      VPID_SET_NULL (&rest_parts->next_vpid);
	    }
	  pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
	  addr.pgptr = NULL;

	  while (!(VPID_ISNULL (&next_vpid)))
	    {
	      addr.pgptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE,
				      PGBUF_LATCH_WRITE,
				      PGBUF_UNCONDITIONAL_LATCH);
	      if (addr.pgptr == NULL)
		{
		  goto exit_on_error;
		}

	      tmp_vpid = next_vpid;
	      rest_parts = (OVERFLOW_REST_PART *) addr.pgptr;
	      next_vpid = rest_parts->next_vpid;

	      if (pgbuf_invalidate (thread_p, addr.pgptr) != NO_ERROR)
		{
		  goto exit_on_error;
		}
	      addr.pgptr = NULL;

	      if (file_dealloc_page (thread_p, ovf_vfid, &tmp_vpid) !=
		  NO_ERROR)
		{
		  goto exit_on_error;
		}
	    }
	  break;
	}
    }

  return ovf_vpid;

exit_on_error:

  return NULL;
}

/*
 * overflow_delete_internal () -
 *   return: NO_ERROR
 *   ovf_vfid(in): File where the overflow data is stored
 *                 WARNING: MUST BE THE SAME AS IT WAS GIVEN DURING INSERT
 *   vpid(in):
 *   pgptr(in):
 */
static int
overflow_delete_internal (THREAD_ENTRY * thread_p, const VFID * ovf_vfid,
			  VPID * vpid, PAGE_PTR pgptr)
{
  int ret;

  ret = pgbuf_invalidate (thread_p, pgptr);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  ret = file_dealloc_page (thread_p, ovf_vfid, vpid);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  return ret;

exit_on_error:

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * overflow_delete () - Delete the content of a multipage data
 *   return: ovf_vpid on success or NULL on failure
 *   ovf_vfid(in): File where the overflow data is stored
 *                 WARNING: MUST BE THE SAME AS IT WAS GIVEN DURING INSERT
 *   ovf_vpid(in): Overflow address
 *
 * Note: The function deallocate the pages composing the overflow record
 */
const VPID *
overflow_delete (THREAD_ENTRY * thread_p, const VFID * ovf_vfid,
		 const VPID * ovf_vpid)
{
  return overflow_traverse (thread_p, ovf_vfid, ovf_vpid, OVERFLOW_DO_DELETE);
}

/*
 * overflow_flush_internal () -
 *   return: NO_ERROR
 *   pgptr(in):
 */
static int
overflow_flush_internal (THREAD_ENTRY * thread_p, PAGE_PTR pgptr)
{
  int ret = NO_ERROR;

  if (pgbuf_flush_with_wal (thread_p, pgptr) == NULL)
    {
      goto exit_on_error;
    }

  return ret;

exit_on_error:

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * overflow_flush () - Flush all overflow dirty pages where the object resides
 *   return: void
 *   ovf_vpid(in): Overflow address
 */
void
overflow_flush (THREAD_ENTRY * thread_p, const VPID * ovf_vpid)
{
  (void) overflow_traverse (thread_p, NULL, ovf_vpid, OVERFLOW_DO_FLUSH);
}

/*
 * overflow_get_length () - FIND LENGTH OF OVERFLOW OBJECT
 *   return: length of overflow object, or -1 on error
 *   ovf_vpid(in):
 *
 * Note: The length of the content of a multipage object associated with the
 *       given overflow address is returned.
 */
int
overflow_get_length (THREAD_ENTRY * thread_p, const VPID * ovf_vpid)
{
  PAGE_PTR pgptr;
  int length;

  /*
   * We don't need to lock the overflow pages since these pages are not
   * shared among several pieces of overflow data. The overflow pages are
   * know by accessing the relocation-overflow record with the appropiate lock
   */

  pgptr = pgbuf_fix (thread_p, ovf_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		     PGBUF_UNCONDITIONAL_LATCH);
  if (pgptr == NULL)
    {
      return -1;
    }

  length = ((OVERFLOW_FIRST_PART *) pgptr)->length;

  pgbuf_unfix_and_init (thread_p, pgptr);

  return length;
}

/*
 * overflow_get_nbytes () - GET A PORTION OF THE CONTENT OF AN OVERFLOW RECORD
 *   return: scan status
 *   ovf_vpid(in): Overflow address
 *   recdes(in): Record descriptor
 *   start_offset(in): Start offset of portion to copy
 *   max_nbytes(in): Maximum number of bytes to retrieve
 *   remaining_length(in): The number of remaining bytes to read
 *
 * Note: A portion of the content of the overflow record associated with the
 *       given overflow address(ovf_pid) is placed into the area pointed to by
 *       the record descriptor. If the content of the object does not fit in
 *       such an area (i.e., recdes->area_size), an error is returned and a
 *       hint of the number of bytes needed is returned as a negative value in
 *       recdes->length. The length of the retrieved number of bytes is *set
 *       in the the record descriptor (i.e., recdes->length).
 */
SCAN_CODE
overflow_get_nbytes (THREAD_ENTRY * thread_p, const VPID * ovf_vpid,
		     RECDES * recdes, int start_offset, int max_nbytes,
		     int *remaining_length)
{
  OVERFLOW_FIRST_PART *first_part;
  OVERFLOW_REST_PART *rest_parts;
  PAGE_PTR pgptr = NULL;
  char *copyfrom;
  VPID next_vpid;
  int copy_length;
  char *data;

  /*
   * We don't need to lock the overflow pages since these pages are not
   * shared among several pieces of overflow data. The overflow pages are
   * know by accessing the relocation-overflow record with the appropiate lock
   */

  next_vpid = *ovf_vpid;

  pgptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		     PGBUF_UNCONDITIONAL_LATCH);
  if (pgptr == NULL)
    {
      return S_ERROR;
    }

  first_part = (OVERFLOW_FIRST_PART *) pgptr;
  *remaining_length = first_part->length;

  if (max_nbytes < 0)
    {
      /* The rest of the overflow record starting at start_offset */
      max_nbytes = *remaining_length - start_offset;
    }
  else
    {
      /* Don't give more than what we have */
      if (max_nbytes > (*remaining_length - start_offset))
	{
	  max_nbytes = *remaining_length - start_offset;
	}
    }

  if (max_nbytes < 0)
    {
      /* Likely the offset was beyond the size of the overflow record */
      max_nbytes = 0;
      *remaining_length = 0;
    }
  else
    {
      *remaining_length -= max_nbytes;
    }

  /* Make sure that there is enough space to copy the desired length object */
  if (max_nbytes > recdes->area_size)
    {
      pgbuf_unfix_and_init (thread_p, pgptr);

      /* Give a hint to the user of the needed length. Hint is given as a
         negative value */
      recdes->length = -max_nbytes;
      return S_DOESNT_FIT;
    }
  else if (max_nbytes == 0)
    {
      pgbuf_unfix_and_init (thread_p, pgptr);

      recdes->length = 0;
      return S_SUCCESS;
    }

  recdes->length = max_nbytes;

  /* Start copying the object */
  data = recdes->data;
  copyfrom = (char *) first_part->data;
  next_vpid = first_part->next_vpid;

  while (max_nbytes > 0)
    {
      /* Continue seeking until the starting offset is reached (passed) */
      if (start_offset > 0)
	{
	  /* Advance .. seek as much as you can */
	  copy_length =
	    (int) ((copyfrom + start_offset) > ((char *) pgptr + DB_PAGESIZE)
		   ? DB_PAGESIZE - (copyfrom -
				    (char *) pgptr) : start_offset);
	  start_offset -= copy_length;
	  copyfrom += copy_length;
	}

      /*
       * Copy as much as you can when you do not need to continue seeking,
       * and there is something to copy in current page (i.e., not at end
       * of the page) and we are not located at the end of the overflow record.
       */
      if (start_offset == 0)
	{
	  if (copyfrom + max_nbytes > (char *) pgptr + DB_PAGESIZE)
	    {
	      copy_length =
		DB_PAGESIZE - CAST_BUFLEN (copyfrom - (char *) pgptr);
	    }
	  else
	    {
	      copy_length = max_nbytes;
	    }

	  /* If we were not at the end of the page, perform the copy */
	  if (copy_length > 0)
	    {
	      memcpy (data, copyfrom, copy_length);
	      data += copy_length;
	      max_nbytes -= copy_length;
	    }
	}

      pgbuf_unfix_and_init (thread_p, pgptr);
      if (max_nbytes > 0)
	{
	  if (VPID_ISNULL (&next_vpid))
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_HEAP_OVFADDRESS_CORRUPTED, 3, ovf_vpid->volid,
		      ovf_vpid->pageid, NULL_SLOTID);
	      return S_ERROR;
	    }

	  pgptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			     PGBUF_UNCONDITIONAL_LATCH);
	  if (pgptr == NULL)
	    {
	      recdes->length = 0;
	      return S_ERROR;
	    }
	  rest_parts = (OVERFLOW_REST_PART *) pgptr;
	  copyfrom = (char *) rest_parts->data;
	  next_vpid = rest_parts->next_vpid;
	}
    }

  return S_SUCCESS;
}

/*
 * overflow_get () - Get the content of a multipage object from overflow
 *   return: scan status
 *   ovf_vpid(in): Overflow address
 *   recdes(in): Record descriptor
 *
 * Note: The content of a multipage object associated with the given overflow
 *       address(oid) is placed into the area pointed to by the record
 *       descriptor. If the content of the object does not fit in such an area
 *       (i.e., recdes->area_size), an error is returned and a hint of its
 *       length is returned as a negative value in recdes->length. The length
 *       of the retrieved object is set in the the record descriptor
 *       (i.e., recdes->length).
 *
 */
SCAN_CODE
overflow_get (THREAD_ENTRY * thread_p, const VPID * ovf_vpid, RECDES * recdes)
{
  int remaining_length;

  return overflow_get_nbytes (thread_p, ovf_vpid, recdes, 0, -1,
			      &remaining_length);
}

/*
 * overflow_get_capacity () - Find the current storage facts/capacity of given
 *                   overflow rec
 *   return: NO_ERROR
 *   ovf_vpid(in): Overflow address
 *   ovf_size(out): Length of overflow object
 *   ovf_num_pages(out): Total number of overflow pages
 *   ovf_overhead(out): System overhead for overflow record
 *   ovf_free_space(out): Free space for exapnsion of the overflow rec
 */
int
overflow_get_capacity (THREAD_ENTRY * thread_p, const VPID * ovf_vpid,
		       int *ovf_size, int *ovf_num_pages, int *ovf_overhead,
		       int *ovf_free_space)
{
  OVERFLOW_FIRST_PART *first_part;
  OVERFLOW_REST_PART *rest_parts;
  PAGE_PTR pgptr = NULL;
  VPID next_vpid;
  int remain_length;
  int hdr_length;
  int ret = NO_ERROR;

  /*
   * We don't need to lock the overflow pages since these pages are not
   * shared among several pieces of overflow data. The overflow pages are
   * know by accessing the relocation-overflow record with the appropiate lock
   */

  next_vpid = *ovf_vpid;

  pgptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		     PGBUF_UNCONDITIONAL_LATCH);
  if (pgptr == NULL)
    {
      return ER_FAILED;
    }

  first_part = (OVERFLOW_FIRST_PART *) pgptr;
  remain_length = first_part->length;

  *ovf_size = first_part->length;
  *ovf_num_pages = 0;
  *ovf_overhead = 0;
  *ovf_free_space = 0;

  hdr_length = offsetof (OVERFLOW_FIRST_PART, data);

  next_vpid = first_part->next_vpid;

  while (remain_length > 0)
    {
      if (remain_length > DB_PAGESIZE)
	{
	  remain_length -= DB_PAGESIZE - hdr_length;
	}
      else
	{
	  *ovf_free_space = DB_PAGESIZE - remain_length;
	  remain_length = 0;
	}

      *ovf_num_pages += 1;
      *ovf_overhead += hdr_length;

      if (remain_length > 0)
	{
	  pgbuf_unfix_and_init (thread_p, pgptr);
	  if (VPID_ISNULL (&next_vpid))
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_HEAP_OVFADDRESS_CORRUPTED, 3, ovf_vpid->volid,
		      ovf_vpid->pageid, NULL_SLOTID);
	      ret = ER_HEAP_OVFADDRESS_CORRUPTED;
	      goto exit_on_error;
	    }

	  pgptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			     PGBUF_UNCONDITIONAL_LATCH);
	  if (pgptr == NULL)
	    {
	      goto exit_on_error;
	    }

	  rest_parts = (OVERFLOW_REST_PART *) pgptr;
	  hdr_length = offsetof (OVERFLOW_REST_PART, data);
	  next_vpid = rest_parts->next_vpid;
	}
    }

  pgbuf_unfix_and_init (thread_p, pgptr);

  return ret;

exit_on_error:

  *ovf_size = 0;
  *ovf_num_pages = 0;
  *ovf_overhead = 0;
  *ovf_free_space = 0;

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

#if defined (CUBRID_DEBUG)
/*
 * overflow_dump () - Dump an overflow object in ascii
 *   return: NO_ERROR
 *   ovf_vpid(in): Overflow address
 */
int
overflow_dump (THREAD_ENTRY * thread_p, FILE * fp, VPID * ovf_vpid)
{
  OVERFLOW_FIRST_PART *first_part;
  OVERFLOW_REST_PART *rest_parts;
  PAGE_PTR pgptr = NULL;
  VPID next_vpid;
  int remain_length, dump_length;
  char *dumpfrom;
  int i;
  int ret = NO_ERROR;

  /*
   * We don't need to lock the overflow pages since these pages are not
   * shared among several pieces of overflow data. The overflow pages are
   * know by accessing the relocation-overflow record with the appropiate lock
   */

  next_vpid = *ovf_vpid;
  pgptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		     PGBUF_UNCONDITIONAL_LATCH);
  if (pgptr == NULL)
    {
      return ((ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
    }

  first_part = (OVERFLOW_FIRST_PART *) pgptr;
  remain_length = first_part->length;
  dumpfrom = (char *) first_part->data;
  next_vpid = first_part->next_vpid;

  while (remain_length > 0)
    {
      dump_length =
	(int) ((dumpfrom + remain_length > (char *) pgptr + DB_PAGESIZE)
	       ? DB_PAGESIZE - (dumpfrom - (char *) pgptr) : remain_length);
      for (i = 0; i < dump_length; i++)
	{
	  (void) fputc (*dumpfrom++, fp);
	}

      remain_length -= dump_length;

      if (remain_length > 0)
	{
	  pgbuf_unfix_and_init (thread_p, pgptr);
	  if (VPID_ISNULL (&next_vpid))
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_HEAP_OVFADDRESS_CORRUPTED, 3, ovf_vpid->volid,
		      ovf_vpid->pageid, NULL_SLOTID);
	      return ER_HEAP_OVFADDRESS_CORRUPTED;
	    }

	  pgptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			     PGBUF_UNCONDITIONAL_LATCH);
	  if (pgptr == NULL)
	    {
	      return ((ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
	    }

	  rest_parts = (OVERFLOW_REST_PART *) pgptr;
	  dumpfrom = (char *) rest_parts->data;
	  next_vpid = rest_parts->next_vpid;
	}
    }

  pgbuf_unfix_and_init (thread_p, pgptr);

  return ret;
}
#endif

/*
 * overflow_estimate_npages_needed () - Guess the number of pages needed to insert
 *                                 a set of overflow datas
 *   return: npages
 *   total_novf_sets(in): Number of set of overflow data
 *   avg_ovfdata_size(in): Avergae size of overflow data
 */
int
overflow_estimate_npages_needed (THREAD_ENTRY * thread_p, int total_novf_sets,
				 int avg_ovfdata_size)
{
  int npages;

  /* Overflow insertion
     First page.. Substract length for insertion in first page */
  avg_ovfdata_size -=
    (DB_PAGESIZE - (int) offsetof (OVERFLOW_FIRST_PART, data));
  if (avg_ovfdata_size > 0)
    {
      /* The rest of the pages */
      npages = DB_PAGESIZE - offsetof (OVERFLOW_REST_PART, data);
      npages = CEIL_PTVDIV (avg_ovfdata_size, npages);
    }
  else
    {
      npages = 1;
    }

  npages *= total_novf_sets;
  npages += file_guess_numpages_overhead (thread_p, NULL, npages);

  return npages;
}

/*
 * overflow_rv_newpage_logical_undo () - Undo new overflow page creation
 *   return: 0 if no error, or error code
 *   rcv(in): Recovery structure
 */
int
overflow_rv_newpage_logical_undo (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  OVERFLOW_RECV_LINKS *newpg;

  newpg = (OVERFLOW_RECV_LINKS *) rcv->data;
  (void) file_dealloc_page (thread_p, &newpg->ovf_vfid, &newpg->new_vpid);
  return NO_ERROR;
}

/*
 * overflow_rv_newpage_logical_dump_undo () - Dump undo information of new overflow
 *                                       page creation
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
overflow_rv_newpage_logical_dump_undo (FILE * fp, int length_ignore,
				       void *data)
{
  OVERFLOW_RECV_LINKS *newpg;

  newpg = (OVERFLOW_RECV_LINKS *) data;
  (void) fprintf (fp, "Deallocating page %d|%d"
		  " from Volid = %d, Fileid = %d\n",
		  newpg->new_vpid.volid, newpg->new_vpid.pageid,
		  newpg->ovf_vfid.volid, newpg->ovf_vfid.fileid);
}

/*
 * overflow_rv_newpage_link_undo () - Undo allocation of new overflow page and the
 *                               reference to it
 *   return: 0 if no error, or error code
 *   rcv(in): Recovery structure
 */
int
overflow_rv_newpage_link_undo (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  OVERFLOW_REST_PART *rest_parts;

  rest_parts = (OVERFLOW_REST_PART *) rcv->pgptr;
  VPID_SET_NULL (&rest_parts->next_vpid);
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * overflow_rv_link () - Recover overflow
 *   return: 0 if no error, or error code
 *   rcv(in): Recovery structure
 *
 * Note: It can be used for undo a new allocation of overflow page or for redo
 *       deallocation of overflow page
 */
int
overflow_rv_link (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  VPID *vpid;
  OVERFLOW_REST_PART *rest_parts;

  vpid = (VPID *) rcv->data;
  rest_parts = (OVERFLOW_REST_PART *) rcv->pgptr;
  rest_parts->next_vpid = *vpid;
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * overflow_rv_link_dump () - Dump recovery information related to overflow link
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
overflow_rv_link_dump (FILE * fp, int length_ignore, void *data)
{
  VPID *vpid;

  vpid = (VPID *) data;
  fprintf (fp, "Overflow Reference to Volid = %d|Pageid = %d\n",
	   vpid->volid, vpid->pageid);
}

/*
 * overflow_rv_page_dump () - Dump overflow page
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
overflow_rv_page_dump (FILE * fp, int length, void *data)
{
  OVERFLOW_REST_PART *rest_parts;
  char *dumpfrom;
  int hdr_length;

  rest_parts = (OVERFLOW_REST_PART *) data;
  fprintf (fp, "Overflow Link to Volid = %d|Pageid = %d\n",
	   rest_parts->next_vpid.volid, rest_parts->next_vpid.pageid);
  dumpfrom = (char *) data;

  hdr_length = offsetof (OVERFLOW_REST_PART, data);
  length -= hdr_length;
  dumpfrom += hdr_length;

  log_rv_dump_char (fp, length, (void *) dumpfrom);
}
