/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * overflow_file.c - Overflow file manager (at server)
 */

#ident "$Id$"

#include "overflow_file.h"

#include "config.h"
#include "error_manager.h"
#include "file_manager.h"
#include "heap_file.h"
#include "log_append.hpp"
#include "log_manager.h"
#include "memory_alloc.h"
#include "mvcc.h"
#include "page_buffer.h"
#include "slotted_page.h"
#include "storage_common.h"

#include <string.h>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define OVERFLOW_ALLOCVPID_ARRAY_SIZE 64

typedef enum
{
  OVERFLOW_DO_DELETE,
  OVERFLOW_DO_FLUSH
} OVERFLOW_DO_FUNC;

static void overflow_next_vpid (const VPID * ovf_vpid, VPID * vpid, PAGE_PTR pgptr);
static const VPID *overflow_traverse (THREAD_ENTRY * thread_p, const VFID * ovf_vfid, const VPID * ovf_vpid,
				      OVERFLOW_DO_FUNC func);
static int overflow_delete_internal (THREAD_ENTRY * thread_p, const VFID * ovf_vfid, VPID * vpid, PAGE_PTR pgptr);
static int overflow_flush_internal (THREAD_ENTRY * thread_p, PAGE_PTR pgptr);

/*
 * overflow_insert () - Insert an overflow record (multiple-pages size record).
 *
 * return         : Error code
 * thread_p (in)  : Thread entry
 * ovf_vfid (in)  : Overflow file identifier
 * ovf_vpid (out) : Output VPID of first page in multi-page data
 * recdes (in)    : Multi-page data
 * file_type (in) : Overflow file type
 *
 * Note: Data in overflow is composed of several pages. Pages in the overflow
 *       area are not shared among other pieces of overflow data.
 *
 *       --------------------------------         ------------------------
 *       |Next_vpid |Length|... data ...| ... --> |Next_vpid|... data ...|
 *       --------------------------------         ------------------------
 *
 *       Single link list of pages.
 *       The length of the multi-page data is stored on its first overflow page
 *
 *       Overflow pages are not locked in any mode since they are not shared
 *       by other pieces of data and its address is only know by accessing the
 *       relocation overflow record data which has been appropriately locked.
 */
int
overflow_insert (THREAD_ENTRY * thread_p, const VFID * ovf_vfid, VPID * ovf_vpid, RECDES * recdes, FILE_TYPE file_type)
{
  OVERFLOW_FIRST_PART *first_part;
  OVERFLOW_REST_PART *rest_parts;
  char *copyto;
  int length, copy_length;
  INT32 npages = 0;
  char *data;
  LOG_DATA_ADDR addr;
  int i;
  VPID *vpids = NULL;
  VPID vpids_buffer[OVERFLOW_ALLOCVPID_ARRAY_SIZE + 1];
  bool is_sysop_started = false;
  PAGE_TYPE ptype = PAGE_OVERFLOW;

  int error_code = NO_ERROR;

  assert (ovf_vfid != NULL && !VFID_ISNULL (ovf_vfid));
  assert (ovf_vpid != NULL);
  assert (recdes != NULL);
  assert (file_type == FILE_TEMP	/* sort files */
	  || file_type == FILE_BTREE_OVERFLOW_KEY	/* b-tree overflow key */
	  || file_type == FILE_MULTIPAGE_OBJECT_HEAP /* heap overflow file */ );

  addr.vfid = ovf_vfid;
  addr.offset = 0;

  /*
   * Guess the number of pages. The total number of pages is found by dividing length by page size - the smallest
   * header. Then, we make sure that this estimate is correct. */
  length = recdes->length - (DB_PAGESIZE - (int) offsetof (OVERFLOW_FIRST_PART, data));
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (npages + 1) * sizeof (VPID));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
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

  log_sysop_start (thread_p);
  is_sysop_started = true;

  error_code = file_alloc_multiple (thread_p, ovf_vfid,
				    file_type != FILE_TEMP ? file_init_page_type : file_init_temp_page_type, &ptype,
				    npages, vpids);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit_on_error;
    }
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
      addr.pgptr = pgbuf_fix (thread_p, &vpids[i], OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (addr.pgptr == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  goto exit_on_error;
	}
#if !defined (NDEBUG)
      (void) pgbuf_check_page_ptype (thread_p, addr.pgptr, PAGE_OVERFLOW);
#endif /* !NDEBUG */

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
	  if (file_type != FILE_TEMP)
	    {
	      log_append_empty_record (thread_p, LOG_DUMMY_OVF_RECORD, &addr);
	    }
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

      if (file_type != FILE_TEMP && thread_p->no_logging != true)
	{
	  log_append_redo_data (thread_p, RVOVF_NEWPAGE_INSERT, &addr,
				copy_length + CAST_BUFLEN (copyto - (char *) addr.pgptr), (char *) addr.pgptr);
	}

      data += copy_length;
      length -= copy_length;

      pgbuf_set_dirty_and_free (thread_p, addr.pgptr);
    }

  assert (length == 0);
#if defined (CUBRID_DEBUG)
  if (length > 0)
    {
      assert (false);
      er_log_debug (ARG_FILE_LINE,
		    "ovf_insert: ** SYSTEM ERROR calculation of number of pages needed to store overflow data seems"
		    " incorrect. Need no more than %d pages", npages);
      error_code = ER_FAILED;
      goto exit_on_error;
    }
#endif

  log_sysop_attach_to_outer (thread_p);

  if (vpids != vpids_buffer)
    {
      free_and_init (vpids);
    }
  return NO_ERROR;

exit_on_error:

  if (is_sysop_started)
    {
      log_sysop_abort (thread_p);
    }

  if (vpids != vpids_buffer)
    {
      free_and_init (vpids);
    }
  return error_code;
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
overflow_traverse (THREAD_ENTRY * thread_p, const VFID * ovf_vfid, const VPID * ovf_vpid, OVERFLOW_DO_FUNC func)
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
      pgptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (pgptr == NULL)
	{
	  goto exit_on_error;
	}

#if !defined (NDEBUG)
      (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_OVERFLOW);
#endif /* !NDEBUG */

      vpid = next_vpid;
      overflow_next_vpid (ovf_vpid, &next_vpid, pgptr);

      switch (func)
	{
	case OVERFLOW_DO_DELETE:
	  if (ovf_vfid == NULL)	/* assert */
	    {
	      goto exit_on_error;
	    }
	  if (overflow_delete_internal (thread_p, ovf_vfid, &vpid, pgptr) != NO_ERROR)
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
 * overflow_update () - Update the content of multi-page data.
 *
 * return         : Error code
 * thread_p (in)  : Thread entry
 * ovf_vfid (in)  : Overflow file identifier
 * ovf_vpid (in)  : VPID of first page in multi-page data
 * recdes (in)    : New multi-page data
 * file_type (in) : Overflow file type
 *
 * Note: The function may allocate or deallocate several overflow pages if the multipage data increase/decrease in
 *       length.
 *
 *       Overflow pages are not locked in any mode since they are not shared by other data and its address is know by
 *       accessing the relocation overflow record which has been appropriately locked.
 */
int
overflow_update (THREAD_ENTRY * thread_p, const VFID * ovf_vfid, const VPID * ovf_vpid, RECDES * recdes,
		 FILE_TYPE file_type)
{
  OVERFLOW_FIRST_PART *first_part = NULL;
  OVERFLOW_REST_PART *rest_parts = NULL;
  char *copyto;
  VPID tmp_vpid;
  int hdr_length;
  int copy_length;
  int old_length = 0;
  int length;
  char *data;
  VPID next_vpid;
  VPID *addr_vpid_ptr;
  LOG_DATA_ADDR addr;
  bool isnewpage = false;
  PAGE_TYPE ptype = PAGE_OVERFLOW;
  int error_code = NO_ERROR;

  assert (ovf_vfid != NULL && !VFID_ISNULL (ovf_vfid));

  /* used only for heap for now... I left this here just in case other file types start using this.
   * If you hit this assert, check the code is alright for your usage (e.g. this doesn't consider temporary files).
   */
  assert (file_type == FILE_MULTIPAGE_OBJECT_HEAP);

  addr.vfid = ovf_vfid;
  addr.offset = 0;
  next_vpid = *ovf_vpid;

  data = recdes->data;
  length = recdes->length;

  log_sysop_start (thread_p);

  while (length > 0)
    {
      addr.pgptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (addr.pgptr == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  goto exit_on_error;
	}
#if !defined (NDEBUG)
      (void) pgbuf_check_page_ptype (thread_p, addr.pgptr, PAGE_OVERFLOW);
#endif /* !NDEBUG */

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
	      log_append_undo_data (thread_p, RVOVF_PAGE_UPDATE, &addr, DB_PAGESIZE, addr.pgptr);
	      old_length -= DB_PAGESIZE - hdr_length;
	    }
	  else
	    {
	      log_append_undo_data (thread_p, RVOVF_PAGE_UPDATE, &addr, hdr_length + old_length, addr.pgptr);
	      old_length = 0;
	    }

	  /* Modify the new length */
	  first_part->length = length;

	  /* notify the first part of overflow recdes */
	  log_append_empty_record (thread_p, LOG_DUMMY_OVF_RECORD, &addr);
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
		  log_append_undo_data (thread_p, RVOVF_PAGE_UPDATE, &addr, DB_PAGESIZE, addr.pgptr);
		  old_length -= DB_PAGESIZE - hdr_length;
		}
	      else
		{
		  log_append_undo_data (thread_p, RVOVF_PAGE_UPDATE, &addr, hdr_length + old_length, addr.pgptr);
		  old_length = 0;
		}
	    }
	}

      memcpy (copyto, data, copy_length);
      data += copy_length;
      length -= copy_length;

      if (thread_p->no_logging != true)
	{
	  log_append_redo_data (thread_p, RVOVF_PAGE_UPDATE, &addr, copy_length + hdr_length, (char *) addr.pgptr);
	}

      if (length > 0)
	{
	  /* Need more pages... Get next page */
	  if (VPID_ISNULL (&next_vpid))
	    {
	      /* We need to allocate a new page */
	      error_code = file_alloc (thread_p, ovf_vfid, file_init_page_type, &ptype, &next_vpid, NULL);
	      if (error_code != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  pgbuf_set_dirty_and_free (thread_p, addr.pgptr);

		  goto exit_on_error;
		}

	      log_append_undoredo_data (thread_p, RVOVF_NEWPAGE_LINK, &addr, 0, sizeof (next_vpid), NULL, &next_vpid);

	      isnewpage = true;	/* So that its link can be set to NULL */

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
	  pgbuf_set_dirty_and_free (thread_p, addr.pgptr);
	}
      else
	{
	  /* The content of the data has been copied. We don't need more pages. Deallocate any additional pages */
	  VPID_SET_NULL (&tmp_vpid);

	  log_append_undoredo_data (thread_p, RVOVF_CHANGE_LINK, &addr, sizeof (next_vpid), sizeof (next_vpid),
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
	  pgbuf_set_dirty_and_free (thread_p, addr.pgptr);

	  while (!(VPID_ISNULL (&next_vpid)))
	    {
	      addr.pgptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	      if (addr.pgptr == NULL)
		{
		  ASSERT_ERROR_AND_SET (error_code);
		  goto exit_on_error;
		}

#if !defined (NDEBUG)
	      (void) pgbuf_check_page_ptype (thread_p, addr.pgptr, PAGE_OVERFLOW);
#endif /* !NDEBUG */

	      tmp_vpid = next_vpid;
	      rest_parts = (OVERFLOW_REST_PART *) addr.pgptr;
	      next_vpid = rest_parts->next_vpid;

	      pgbuf_unfix_and_init (thread_p, addr.pgptr);

	      error_code = file_dealloc (thread_p, ovf_vfid, &tmp_vpid, file_type);
	      if (error_code != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  goto exit_on_error;
		}
	    }
	  break;
	}
    }

  /* done */
  log_sysop_attach_to_outer (thread_p);

  return NO_ERROR;

exit_on_error:

  log_sysop_abort (thread_p);

  return error_code;
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
overflow_delete_internal (THREAD_ENTRY * thread_p, const VFID * ovf_vfid, VPID * vpid, PAGE_PTR pgptr)
{
  int ret;

  /* Unfix page. */
  pgbuf_unfix_and_init (thread_p, pgptr);

  /* TODO: clarify file_type */
  ret = file_dealloc (thread_p, ovf_vfid, vpid, FILE_UNKNOWN_TYPE);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  return ret;

exit_on_error:

  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
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
overflow_delete (THREAD_ENTRY * thread_p, const VFID * ovf_vfid, const VPID * ovf_vpid)
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

  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
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

  pgptr = pgbuf_fix (thread_p, ovf_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (pgptr == NULL)
    {
      return -1;
    }

#if !defined (NDEBUG)
  (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_OVERFLOW);
#endif /* !NDEBUG */

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
 *   mvcc_snapshot(in): mvcc snapshot
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
overflow_get_nbytes (THREAD_ENTRY * thread_p, const VPID * ovf_vpid, RECDES * recdes, int start_offset, int max_nbytes,
		     int *remaining_length, MVCC_SNAPSHOT * mvcc_snapshot)
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

  pgptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (pgptr == NULL)
    {
      return S_ERROR;
    }

#if !defined (NDEBUG)
  (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_OVERFLOW);
#endif /* !NDEBUG */

  first_part = (OVERFLOW_FIRST_PART *) pgptr;
  if (mvcc_snapshot != NULL)
    {
      MVCC_REC_HEADER mvcc_header;
      heap_get_mvcc_rec_header_from_overflow (pgptr, &mvcc_header, NULL);
      if (mvcc_snapshot->snapshot_fnc (thread_p, &mvcc_header, mvcc_snapshot) == TOO_OLD_FOR_SNAPSHOT)
	{
	  /* consider snapshot is not satisified only in case of TOO_OLD_FOR_SNAPSHOT;
	   * TOO_NEW_FOR_SNAPSHOT records should be accepted, e.g. a recently updated record, locked at select */
	  pgbuf_unfix_and_init (thread_p, pgptr);
	  return S_SNAPSHOT_NOT_SATISFIED;
	}
    }

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

      /* Give a hint to the user of the needed length. Hint is given as a negative value */
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
	  copy_length = (int) ((copyfrom + start_offset) > ((char *) pgptr + DB_PAGESIZE)
			       ? DB_PAGESIZE - (copyfrom - (char *) pgptr) : start_offset);
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
	      copy_length = DB_PAGESIZE - CAST_BUFLEN (copyfrom - (char *) pgptr);
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
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_OVFADDRESS_CORRUPTED, 3, ovf_vpid->volid,
		      ovf_vpid->pageid, NULL_SLOTID);
	      return S_ERROR;
	    }

	  pgptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
	  if (pgptr == NULL)
	    {
	      recdes->length = 0;
	      return S_ERROR;
	    }

#if !defined (NDEBUG)
	  (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_OVERFLOW);
#endif /* !NDEBUG */

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
 *   mvcc_snapshot(in): mvcc snapshot
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
overflow_get (THREAD_ENTRY * thread_p, const VPID * ovf_vpid, RECDES * recdes, MVCC_SNAPSHOT * mvcc_snapshot)
{
  int remaining_length;

  return overflow_get_nbytes (thread_p, ovf_vpid, recdes, 0, -1, &remaining_length, mvcc_snapshot);
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
overflow_get_capacity (THREAD_ENTRY * thread_p, const VPID * ovf_vpid, int *ovf_size, int *ovf_num_pages,
		       int *ovf_overhead, int *ovf_free_space)
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

  pgptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (pgptr == NULL)
    {
      return ER_FAILED;
    }

#if !defined (NDEBUG)
  (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_OVERFLOW);
#endif /* !NDEBUG */

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
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_OVFADDRESS_CORRUPTED, 3, ovf_vpid->volid,
		      ovf_vpid->pageid, NULL_SLOTID);
	      ret = ER_HEAP_OVFADDRESS_CORRUPTED;
	      goto exit_on_error;
	    }

	  pgptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
	  if (pgptr == NULL)
	    {
	      goto exit_on_error;
	    }

#if !defined (NDEBUG)
	  (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_OVERFLOW);
#endif /* !NDEBUG */

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

  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
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
  pgptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
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
      dump_length = (int) ((dumpfrom + remain_length > (char *) pgptr + DB_PAGESIZE)
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
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_OVFADDRESS_CORRUPTED, 3, ovf_vpid->volid,
		      ovf_vpid->pageid, NULL_SLOTID);
	      return ER_HEAP_OVFADDRESS_CORRUPTED;
	    }

	  pgptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
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
 * overflow_rv_newpage_insert_redo () -
 *   return: 0 if no error, or error code
 *   rcv(in): Recovery structure
 */
int
overflow_rv_newpage_insert_redo (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  return log_rv_copy_char (thread_p, rcv);
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
  fprintf (fp, "Overflow Reference to Volid = %d|Pageid = %d\n", vpid->volid, vpid->pageid);
}

/*
 * overflow_rv_page_update_redo () -
 *   return: 0 if no error, or error code
 *   rcv(in): Recovery structure
 */
int
overflow_rv_page_update_redo (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  (void) pgbuf_set_page_ptype (thread_p, rcv->pgptr, PAGE_OVERFLOW);

  return log_rv_copy_char (thread_p, rcv);
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
  fprintf (fp, "Overflow Link to Volid = %d|Pageid = %d\n", rest_parts->next_vpid.volid, rest_parts->next_vpid.pageid);
  dumpfrom = (char *) data;

  hdr_length = offsetof (OVERFLOW_REST_PART, data);
  length -= hdr_length;
  dumpfrom += hdr_length;

  log_rv_dump_char (fp, length, (void *) dumpfrom);
}

/*
 * overflow_get_first_page_data () - get data of overflow first page
 *
 *   return: overflow first page data data
 *   page_ptr(in): overflow page
 *
 */
char *
overflow_get_first_page_data (char *page_ptr)
{
  assert (page_ptr != NULL);
  return ((OVERFLOW_FIRST_PART *) page_ptr)->data;
}
