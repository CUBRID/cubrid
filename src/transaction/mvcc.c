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
 * mvcc_snapshot.c - mvcc snapshot
 */

#ident "$Id$"

#include "mvcc.h"
#include "dbtype.h"
#include "heap_file.h"
#include "page_buffer.h"
#include "overflow_file.h"
#include "vacuum.h"

#define MVCC_IS_REC_INSERTER_ACTIVE(thread_p, rec_header_p) \
  (logtb_is_active_mvccid (thread_p, (rec_header_p)->mvcc_ins_id))

#define MVCC_IS_REC_DELETER_ACTIVE(thread_p, rec_header_p) \
  (logtb_is_active_mvccid (thread_p, (rec_header_p)->delid_chn.mvcc_del_id))

#define MVCC_IS_REC_INSERTER_IN_SNAPSHOT(thread_p, rec_header_p, snapshot) \
  (mvcc_is_id_in_snapshot (thread_p, (rec_header_p)->mvcc_ins_id, (snapshot)))

#define MVCC_IS_REC_DELETER_IN_SNAPSHOT(thread_p, rec_header_p, snapshot) \
  (mvcc_is_id_in_snapshot (thread_p, (rec_header_p)->delid_chn.mvcc_del_id, (snapshot)))

#define MVCC_IS_REC_INSERTED_SINCE_MVCCID(rec_header_p, mvcc_id) \
  (!mvcc_id_precedes ((rec_header_p)->mvcc_ins_id, (mvcc_id)))

#define MVCC_IS_REC_DELETED_SINCE_MVCCID(rec_header_p, mvcc_id) \
  (!mvcc_id_precedes ((rec_header_p)->delid_chn.mvcc_del_id, (mvcc_id)))


/* Used by mvcc_chain_satisfies_vacuum to avoid handling the same OID twice */
enum
{
  /* Any positive value should be an index in relocated slots array */
  NOT_VISITED = -1,
  VISITED_DEAD = -2,
  VISITED_ALIVE = -3
};

/* the lowest active mvcc id computed for last */
/* MVCCID recent_snapshot_lowest_active_mvccid = MVCCID_NULL; */

static bool mvcc_is_id_in_snapshot (THREAD_ENTRY * thread_p,
				    MVCCID mvcc_id, MVCC_SNAPSHOT * snapshot);

/*
 * mvcc_is_id_in_snapshot () - check whether mvcc id is in snapshot -
 *                             is mvcc id active from snapshot point of view?
 *   return: true/false
 *   thread_p(in): thread entry
 *   mvcc_id(in): mvcc id
 *   snapshot(in): mvcc snapshot
 */
static bool
mvcc_is_id_in_snapshot (THREAD_ENTRY * thread_p, MVCCID mvcc_id,
			MVCC_SNAPSHOT * snapshot)
{
  unsigned int i;

  assert (snapshot != NULL);

  if (mvcc_id_precedes (mvcc_id, snapshot->lowest_active_mvccid))
    {
      /* mvcc id is not active */
      return false;
    }

  if (mvcc_id_follow_or_equal (mvcc_id, snapshot->highest_completed_mvccid))
    {
      /* mvcc id is active */
      return true;
    }

  /* TO DO - handle subtransactions */
  for (i = 0; i < snapshot->cnt_active_ids; i++)
    {
      if (MVCCID_IS_EQUAL (mvcc_id, snapshot->active_ids[i]))
	{
	  return true;
	}
    }

  return false;
}

/*
 * mvcc_satisfies_snapshot () - Check whether a record is valid for 
 *				    a snapshot
 *   return: true, if the record is valid for snapshot
 *   thread_p(in): thread entry
 *   rec_header(out): the record header
 *   snapshot(in): the snapshot used for record validation
 *   page_ptr(in): the page where the record reside
 */
bool
mvcc_satisfies_snapshot (THREAD_ENTRY * thread_p,
			 MVCC_REC_HEADER * rec_header,
			 MVCC_SNAPSHOT * snapshot)
{
  assert (rec_header != NULL && snapshot != NULL);

  if (!MVCC_IS_FLAG_SET (rec_header, OR_MVCC_FLAG_VALID_DELID))
    {
      /* The record is not deleted */
      if (!MVCC_IS_FLAG_SET (rec_header, OR_MVCC_FLAG_VALID_INSID))
	{
	  /* Record was inserted and is visible for all transactions */
	  return true;
	}
      else if (MVCC_IS_REC_INSERTED_BY_ME (thread_p, rec_header))
	{
	  /* Record was inserted by current transaction and is visible */
	  return true;
	}
      else if (MVCC_IS_REC_INSERTER_IN_SNAPSHOT (thread_p, rec_header,
						 snapshot))
	{
	  /* Record was inserted by an active transaction or by a transaction
	   * that has committed after snapshot was obtained.
	   */
	  return false;
	}
      else
	{
	  /* The inserter transaction has committed and the record is visible
	   * to current transaction.
	   */
	  return true;
	}
    }
  else
    {
      /* The record is deleted */
      if (MVCC_IS_REC_INSERTER_IN_SNAPSHOT (thread_p, rec_header, snapshot))
	{
	  return false;
	}
      else if (MVCC_IS_REC_DELETED_BY_ME (thread_p, rec_header))
	{
	  /* The record was deleted by current transaction and it is not
	   * visible anymore.
	   */
	  return false;
	}
      else if (MVCC_IS_REC_DELETER_IN_SNAPSHOT (thread_p, rec_header,
						snapshot))
	{
	  /* The record was deleted by an active transaction or by a
	   * transaction that has committed after snapshot was obtained.
	   */
	  return true;
	}
      else
	{
	  /* The deleter transaction has committed and the record is not
	   * visible to current transaction.
	   */
	  return false;
	}
    }
}

/*
 * mvcc_satisfies_vacuum () - Check whether record satisfies VACUUM
 *
 * return	      : Heap record satisfies vacuum result.
 * thread_p (in)      : Thread entry.
 * rec_header (in)    : MVCC record header.
 * oldest_mvccid (in) : MVCCID for oldest active transaction.
 * page_p (in)	      : Heap page pointer.
 */
MVCC_SATISFIES_VACUUM_RESULT
mvcc_satisfies_vacuum (THREAD_ENTRY * thread_p, MVCC_REC_HEADER * rec_header,
		       MVCCID oldest_mvccid)
{
  if (!MVCC_IS_FLAG_SET (rec_header, OR_MVCC_FLAG_VALID_DELID))
    {
      /* The record was not deleted */
      if (!MVCC_IS_FLAG_SET (rec_header, OR_MVCC_FLAG_VALID_INSID)
	  || MVCC_IS_REC_INSERTED_SINCE_MVCCID (rec_header, oldest_mvccid))
	{
	  /* 1: Record is all visible, but insert MVCCID was already removed
	   * 2: Record was recently inserted and is not yet visible to all
	   *    active transactions.
	   */
	  return VACUUM_RECORD_CANNOT_VACUUM;
	}
      else
	{
	  /* The inserter transaction has committed and the record is visible
	   * to all running transactions. Insert MVCCID can be removed.
	   */
	  return VACUUM_RECORD_DELETE_INSID;
	}
    }
  else
    {
      /* The record was deleted */
      if (MVCC_IS_REC_DELETED_SINCE_MVCCID (rec_header, oldest_mvccid))
	{
	  /* Record was recently deleted and may still be visible to some
	   * active transactions.
	   */
	  return VACUUM_RECORD_CANNOT_VACUUM;
	}
      else
	{
	  /* The deleter transaction has committed and the record is not
	   * visible to any running transactions.
	   */
	  return VACUUM_RECORD_REMOVE;
	}
    }
}

/*
 * mvcc_satisfies_delete () - Check whether a record is valid for 
 *			instant snapshot
 *   return: true, if the record is valid for snapshot
 *   thread_p(in): thread entry
 *   rec_header(out): the record header
 *   snapshot(in): the snapshot used for record validation
 *   page_ptr(in): the page where the record reside
 *
 * Note: The function return a complex result since delete/update commands
 *	    needs to know not only if the row is visible or not
 */
MVCC_SATISFIES_DELETE_RESULT
mvcc_satisfies_delete (THREAD_ENTRY * thread_p, MVCC_REC_HEADER * rec_header)
{
  assert (rec_header != NULL);

  if (!MVCC_IS_FLAG_SET (rec_header, OR_MVCC_FLAG_VALID_DELID))
    {
      /* Record was not deleted */
      if (!MVCC_IS_FLAG_SET (rec_header, OR_MVCC_FLAG_VALID_INSID))
	{
	  /* Record was inserted and is visible for all transactions */
	  return DELETE_RECORD_CAN_DELETE;
	}

      if (MVCC_IS_REC_INSERTED_BY_ME (thread_p, rec_header))
	{
	  /* Record is only visible to current transaction and can be safely
	   * deleted.
	   */
	  return DELETE_RECORD_CAN_DELETE;
	}
      else if (MVCC_IS_REC_INSERTER_ACTIVE (thread_p, rec_header))
	{
	  /* Record is inserted by an active transaction and is not visible to
	   * current transaction.
	   */
	  return DELETE_RECORD_INVISIBLE;
	}
      else
	{
	  /* The inserter transaction has committed and the record can be
	   * deleted by current transaction.
	   */
	  return DELETE_RECORD_CAN_DELETE;
	}
    }
  else
    {
      /* Record was already deleted */
      if (MVCC_IS_REC_DELETED_BY_ME (thread_p, rec_header))
	{
	  /* Record was already deleted by me... */
	  return DELETE_RECORD_SELF_DELETED;
	}
      else if (MVCC_IS_REC_DELETER_ACTIVE (thread_p, rec_header))
	{
	  /* Record was deleted by an active transaction. Current transaction
	   * must wait until the deleter completes.
	   */
	  return DELETE_RECORD_IN_PROGRESS;
	}
      else
	{
	  /* Record was already deleted and the deleter has committed. Cannot
	   * be updated by current transaction.
	   */
	  return DELETE_RECORD_DELETED;
	}
    }
}

/*
 * mvcc_satisfies_dirty () - Check whether a record is visible considering
 *			    following effects:
 *			      - committed transactions
 *			      - in progress transactions
 *			      - previous commands of current transaction
 *				    
 *   return: true, if the record is valid for snapshot
 *   thread_p(in): thread entry
 *   rec_header(out): the record header
 *   snapshot(in): the snapshot used for record validation
 *   page_ptr(in): the page where the record reside
 * Note: snapshot->lowest_active_mvccid and snapshot->highest_completed_mvccid 
 *    are set as a side effect. Thus, snapshot->lowest_active_mvccid is set 
 *    to tuple insert id when it is the id of another active transaction, otherwise 
 *    is set to MVCCID_NULL
 */
bool
mvcc_satisfies_dirty (THREAD_ENTRY * thread_p,
		      MVCC_REC_HEADER * rec_header, MVCC_SNAPSHOT * snapshot)
{
  assert (rec_header != NULL && snapshot != NULL);

  snapshot->lowest_active_mvccid = MVCCID_NULL;
  snapshot->highest_completed_mvccid = MVCCID_NULL;

  if (!MVCC_IS_FLAG_SET (rec_header, OR_MVCC_FLAG_VALID_DELID))
    {
      /* Record was not deleted */
      if (!MVCC_IS_FLAG_SET (rec_header, OR_MVCC_FLAG_VALID_INSID))
	{
	  /* Record was inserted and is visible for all transactions */
	  return true;
	}
      else if (MVCC_IS_REC_INSERTED_BY_ME (thread_p, rec_header))
	{
	  /* Record was inserted by current transaction and is visible */
	  return true;
	}
      else if (MVCC_IS_REC_INSERTER_ACTIVE (thread_p, rec_header))
	{
	  /* Record is inserted by an active transaction and is visible */
	  snapshot->lowest_active_mvccid = MVCC_GET_INSID (rec_header);
	  return true;
	}
      else
	{
	  /* Record is inserted by committed transaction. */
	  return true;
	}
    }
  else
    {
      /* Record was already deleted */
      if (MVCC_IS_REC_DELETED_BY_ME (thread_p, rec_header))
	{
	  /* Record was deleted by current transaction and is not visible */
	  return false;
	}
      else if (MVCC_IS_REC_DELETER_ACTIVE (thread_p, rec_header))
	{
	  /* Record was deleted by other active transaction and is still visible
	   */
	  snapshot->highest_completed_mvccid =
	    rec_header->delid_chn.mvcc_del_id;
	  return true;
	}
      else
	{
	  /* Record was already deleted and the deleter has committed. */
	  return false;
	}
    }
}

/*
 * mvcc_id_precedes - compare MVCC ids
 *
 * return: true, if id1 precede id2, false otherwise
 *
 *   id1(in): first MVCC id to compare
 *   id2(in): the second MVCC id to compare
 *
 */
bool
mvcc_id_precedes (MVCCID id1, MVCCID id2)
{
  int difference;

  if (!MVCCID_IS_NORMAL (id1) || !MVCCID_IS_NORMAL (id2))
    {
      return (id1 < id2);
    }

  difference = (int) (id1 - id2);
  return (difference < 0);
}

/*
 * mvcc_id_follow_or_equal - compare MVCC ids
 *
 * return: true, if id1 follow or equal id2, false otherwise
 *
 *   id1(in): first MVCC id to compare
 *   id2(in): the second MVCC id to compare
 *
 */
bool
mvcc_id_follow_or_equal (MVCCID id1, MVCCID id2)
{
  int difference;

  if (!MVCCID_IS_NORMAL (id1) || !MVCCID_IS_NORMAL (id2))
    {
      return (id1 >= id2);
    }

  difference = (int) (id1 - id2);
  return (difference >= 0);
}
