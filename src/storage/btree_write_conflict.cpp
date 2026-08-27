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

//
// Write-write conflict resolution for a b-tree key probe
//

#include "btree_write_conflict.hpp"

#include "error_manager.h"
#include "lock_manager.h"
#include "log_impl.h"
#include "page_buffer.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if defined (SERVER_MODE)
/*
 * btree_key_release_locked_object_and_pages () - Drop what this scan holds on the key: the object it
 *						  locked and the page latches. The caller re-reads from root.
 *
 * thread_p (in)       : Thread entry.
 * find_unique_helper (in/out) : Find-unique state; any object it has locked is released and forgotten.
 * leaf_page (in/out)  : Leaf node page latch; unfixed and left NULL.
 * overflow_page (in/out) : Optional overflow node page latch; unfixed and left NULL, NULL when there is none.
 */
void
btree_key_release_locked_object_and_pages (THREAD_ENTRY *thread_p, BTREE_FIND_UNIQUE_HELPER *find_unique_helper,
    PAGE_PTR *leaf_page, PAGE_PTR *overflow_page)
{
  if (!OID_ISNULL (&find_unique_helper->locked_oid))
    {
      lock_unlock_object_donot_move_to_non2pl (thread_p, &find_unique_helper->locked_oid,
	  &find_unique_helper->locked_class_oid, find_unique_helper->lock_mode);
      OID_SET_NULL (&find_unique_helper->locked_oid);
    }
  if (overflow_page != NULL && *overflow_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, *overflow_page);
    }
  pgbuf_unfix_and_init (thread_p, *leaf_page);
}

/*
 * btree_key_wait_for_tran_end () - Wait for the writer working under the given MVCCID to end,
 *				    then signal a restart from root.
 *
 * return	       : Error code (NO_ERROR on success, with *restart set to true).
 * thread_p (in)       : Thread entry.
 * writer_mvccid (in)  : MVCCID of the in-progress inserter or deleter to wait on.
 * find_unique_helper (in/out) : Find-unique state; any object it has locked is released first.
 * leaf_page (in/out)  : Leaf node page latch; unfixed before suspending and left NULL.
 * overflow_page (in/out) : Optional overflow node page latch (..._of_non_unique () scans overflow pages);
 *			    unfixed before suspending and left NULL. NULL when there is no overflow page.
 * restart (out)       : Set to true so the caller re-reads the key from root.
 *
 * Note: all page latches must be released before blocking on the lock (never wait while holding a latch).
 */
int
btree_key_wait_for_tran_end (THREAD_ENTRY *thread_p, MVCCID writer_mvccid,
			     BTREE_FIND_UNIQUE_HELPER *find_unique_helper, PAGE_PTR *leaf_page,
			     PAGE_PTR *overflow_page, bool *restart)
{
  int error_code = NO_ERROR;

  /* Release the locked object and the page latches before blocking on the lock. */
  btree_key_release_locked_object_and_pages (thread_p, find_unique_helper, leaf_page, overflow_page);

  error_code = logtb_wait_for_tran_end (thread_p, writer_mvccid);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* The page may have changed during the wait, so re-read the key from root. */
  *restart = true;
  return NO_ERROR;
}

/*
 * btree_key_wait_out_conflicting_writer () - Wait out the transaction inserting or deleting the key's
 *					      first object, if one is still live.
 *
 * return	       : Error code.  On NO_ERROR, *restart says what the caller must do next.
 * thread_p (in)       : Thread entry.
 * satisfies_delete (in) : DELETE_RECORD_INSERT_IN_PROGRESS or DELETE_RECORD_DELETE_IN_PROGRESS.
 * mvcc_header (in)    : MVCC header of the first object, carrying the writer's MVCCID.
 * find_unique_helper (in/out) : Find-unique state.
 * leaf_page (in/out)  : Leaf node page latch.
 * overflow_page (in/out) : Optional overflow node page latch; NULL when there is none.
 * restart (out)       : Outputs true when the key must be re-read from root; otherwise untouched.
 *
 * Note: neither writer holds a row lock the caller's object lock could suspend on, so that lock would be
 *	 granted at once and the re-check would spin.  Serialize on the writer's transaction end instead.
 */
int
btree_key_wait_out_conflicting_writer (THREAD_ENTRY *thread_p, MVCC_SATISFIES_DELETE_RESULT satisfies_delete,
				       MVCC_REC_HEADER *mvcc_header, BTREE_FIND_UNIQUE_HELPER *find_unique_helper,
				       PAGE_PTR *leaf_page, PAGE_PTR *overflow_page, bool *restart)
{
  MVCCID writer_mvccid;

  assert (satisfies_delete == DELETE_RECORD_INSERT_IN_PROGRESS || satisfies_delete == DELETE_RECORD_DELETE_IN_PROGRESS);

  writer_mvccid = (satisfies_delete == DELETE_RECORD_INSERT_IN_PROGRESS)
		  ? MVCC_GET_INSID (mvcc_header) : MVCC_GET_DELID (mvcc_header);

  if (logtb_is_active_other_mvccid (thread_p, writer_mvccid))
    {
      return btree_key_wait_for_tran_end (thread_p, writer_mvccid, find_unique_helper, leaf_page, overflow_page,
					  restart);
    }

  if (satisfies_delete == DELETE_RECORD_DELETE_IN_PROGRESS)
    {
      /* Verdict is stale: the deleter ended in the race and may have freed an object this scan locked. */
      btree_key_release_locked_object_and_pages (thread_p, find_unique_helper, leaf_page, overflow_page);
      *restart = true;
    }

  return NO_ERROR;
}
#endif /* SERVER_MODE */
