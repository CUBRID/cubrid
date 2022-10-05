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
 * mvcc.c - mvcc snapshot
 */

#ident "$Id$"

#include "mvcc.h"
#include "dbtype.h"
#include "heap_file.h"
#include "page_buffer.h"
#include "overflow_file.h"
#include "perf_monitor.h"
#include "porting_inline.hpp"
#include "vacuum.h"

#define MVCC_IS_REC_INSERTER_ACTIVE(thread_p, rec_header_p) \
  (mvcc_is_active_id (thread_p, (rec_header_p)->mvcc_ins_id))

#define MVCC_IS_REC_DELETER_ACTIVE(thread_p, rec_header_p) \
  (mvcc_is_active_id (thread_p, (rec_header_p)->mvcc_del_id))

#define MVCC_IS_REC_INSERTER_IN_SNAPSHOT(thread_p, rec_header_p, snapshot) \
  (mvcc_is_id_in_snapshot (thread_p, (rec_header_p)->mvcc_ins_id, (snapshot)))

#define MVCC_IS_REC_DELETER_IN_SNAPSHOT(thread_p, rec_header_p, snapshot) \
  (mvcc_is_id_in_snapshot (thread_p, (rec_header_p)->mvcc_del_id, (snapshot)))

#define MVCC_IS_REC_INSERTED_SINCE_MVCCID(rec_header_p, mvcc_id) \
  (!MVCC_ID_PRECEDES ((rec_header_p)->mvcc_ins_id, (mvcc_id)))

#define MVCC_IS_REC_DELETED_SINCE_MVCCID(rec_header_p, mvcc_id) \
  (!MVCC_ID_PRECEDES ((rec_header_p)->mvcc_del_id, (mvcc_id)))


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

static INLINE bool mvcc_is_id_in_snapshot (THREAD_ENTRY * thread_p, MVCCID mvcc_id, MVCC_SNAPSHOT * snapshot)
  __attribute__ ((ALWAYS_INLINE));

static INLINE bool mvcc_is_active_id (THREAD_ENTRY * thread_p, MVCCID mvccid) __attribute__ ((ALWAYS_INLINE));

/*
 * mvcc_is_id_in_snapshot () - check whether mvcc id is in snapshot -
 *                             is mvcc id active from snapshot point of view?
 *   return: true/false
 *   thread_p(in): thread entry
 *   mvcc_id(in): mvcc id
 *   snapshot(in): mvcc snapshot
 */
STATIC_INLINE bool
mvcc_is_id_in_snapshot (THREAD_ENTRY * thread_p, MVCCID mvcc_id, MVCC_SNAPSHOT * snapshot)
{
  assert (snapshot != NULL);

  if (MVCC_ID_PRECEDES (mvcc_id, snapshot->lowest_active_mvccid))
    {
      /* MVCC id is not active */
      return false;
    }

  if (MVCC_ID_FOLLOW_OR_EQUAL (mvcc_id, snapshot->highest_completed_mvccid))
    {
      /* MVCC id is active */
      return true;
    }

  return snapshot->m_active_mvccs.is_active (mvcc_id);
}

/*
 * mvcc_is_active_id - check whether given mvccid is active
 *
 * return: bool
 *
 *   thread_p(in): thread entry
 *   id(in): MVCC id
 *
 * Note: Check whether an active transaction is active by searching transactions
 *  status into current history position. The data from current history position
 *  is atomically checked (See logtb_get_mvcc_snapshot comments).
 */
STATIC_INLINE bool
mvcc_is_active_id (THREAD_ENTRY * thread_p, MVCCID mvccid)
{
  LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
  MVCC_INFO *curr_mvcc_info = NULL;

  assert (tdes != NULL && mvccid != MVCCID_NULL);

  curr_mvcc_info = &tdes->mvccinfo;
  if (MVCC_ID_PRECEDES (mvccid, curr_mvcc_info->recent_snapshot_lowest_active_mvccid))
    {
      return false;
    }

  if (logtb_is_current_mvccid (thread_p, mvccid))
    {
      return true;
    }

  return log_Gl.mvcc_table.is_active (mvccid);
}

/*
 * mvcc_satisfies_snapshot () - Check whether a record is valid for
 *				    a snapshot
 *   return: - SNAPSHOT_SATISFIED: record is valid for snapshot
 *	     - TOO_NEW_FOR_SNAPSHOT: record was either inserted or updated recently; commited after snapshot
 *	     - TOO_OLD_FOR_SNAPSHOT: record not visible; deleted and commited
 *   thread_p(in): thread entry
 *   rec_header(out): the record header
 *   snapshot(in): the snapshot used for record validation
 *   page_ptr(in): the page where the record reside
 */
MVCC_SATISFIES_SNAPSHOT_RESULT
mvcc_satisfies_snapshot (THREAD_ENTRY * thread_p, MVCC_REC_HEADER * rec_header, MVCC_SNAPSHOT * snapshot)
{
  assert (rec_header != NULL && snapshot != NULL);

  if (!MVCC_IS_HEADER_DELID_VALID (rec_header))
    {
      /* The record is not deleted */
      if (!MVCC_IS_FLAG_SET (rec_header, OR_MVCC_FLAG_VALID_INSID))
	{
	  /* Record was inserted and is visible for all transactions */
	  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_MVCC_SNAPSHOT))
	    {
	      perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_SNAPSHOT, PERF_SNAPSHOT_RECORD_INSERTED_VACUUMED,
				     PERF_SNAPSHOT_VISIBLE);
	    }

	  return SNAPSHOT_SATISFIED;
	}
      else if (MVCC_IS_REC_INSERTED_BY_ME (thread_p, rec_header))
	{
	  /* Record was inserted by current transaction and is visible */
	  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_MVCC_SNAPSHOT))
	    {
	      perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_SNAPSHOT,
				     PERF_SNAPSHOT_RECORD_INSERTED_CURR_TRAN, PERF_SNAPSHOT_VISIBLE);
	    }
	  return SNAPSHOT_SATISFIED;
	}
      else if (MVCC_IS_REC_INSERTER_IN_SNAPSHOT (thread_p, rec_header, snapshot))
	{
	  /* Record was inserted by an active transaction or by a transaction that has committed after snapshot was
	   * obtained. */
	  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_MVCC_SNAPSHOT))
	    {
	      perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_SNAPSHOT,
				     PERF_SNAPSHOT_RECORD_INSERTED_OTHER_TRAN, PERF_SNAPSHOT_INVISIBLE);
	    }
	  return TOO_NEW_FOR_SNAPSHOT;
	}
      else
	{
	  /* The inserter transaction has committed and the record is visible to current transaction. */
	  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_MVCC_SNAPSHOT))
	    {
	      if (rec_header->mvcc_ins_id != MVCCID_ALL_VISIBLE && vacuum_is_mvccid_vacuumed (rec_header->mvcc_ins_id))
		{
		  perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_SNAPSHOT,
					 PERF_SNAPSHOT_RECORD_INSERTED_COMMITED_LOST, PERF_SNAPSHOT_VISIBLE);
		}
	      else
		{
		  perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_SNAPSHOT,
					 PERF_SNAPSHOT_RECORD_INSERTED_COMMITED, PERF_SNAPSHOT_VISIBLE);
		}
	    }
	  return SNAPSHOT_SATISFIED;
	}
    }
  else
    {
      /* The record is deleted */
      if (MVCC_IS_REC_DELETED_BY_ME (thread_p, rec_header))
	{
	  /* The record was deleted by current transaction and it is not visible anymore. */
	  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_MVCC_SNAPSHOT))
	    {
	      perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_SNAPSHOT, PERF_SNAPSHOT_RECORD_DELETED_CURR_TRAN,
				     PERF_SNAPSHOT_INVISIBLE);
	    }
	  return TOO_OLD_FOR_SNAPSHOT;
	}
      else if (MVCC_IS_REC_INSERTER_IN_SNAPSHOT (thread_p, rec_header, snapshot))
	{
	  /* !!TODO: Is this check necessary? It seems that if inserter is active, then so will be the deleter (actually
	   *       they will be the same). It only adds an extra-check in a function frequently called.
	   */
	  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_MVCC_SNAPSHOT))
	    {
	      perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_SNAPSHOT, PERF_SNAPSHOT_RECORD_INSERTED_DELETED,
				     PERF_SNAPSHOT_INVISIBLE);
	    }
	  return TOO_NEW_FOR_SNAPSHOT;
	}
      else if (MVCC_IS_REC_DELETER_IN_SNAPSHOT (thread_p, rec_header, snapshot))
	{
	  /* The record was deleted by an active transaction or by a transaction that has committed after snapshot was
	   * obtained. */
	  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_MVCC_SNAPSHOT))
	    {
	      perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_SNAPSHOT,
				     PERF_SNAPSHOT_RECORD_DELETED_OTHER_TRAN, PERF_SNAPSHOT_VISIBLE);
	    }
	  return SNAPSHOT_SATISFIED;
	}
      else
	{
	  /* The deleter transaction has committed and the record is not visible to current transaction. */
	  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_MVCC_SNAPSHOT))
	    {
	      if (vacuum_is_mvccid_vacuumed (rec_header->mvcc_del_id))
		{
		  perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_SNAPSHOT,
					 PERF_SNAPSHOT_RECORD_DELETED_COMMITTED_LOST, PERF_SNAPSHOT_INVISIBLE);
		}
	      else
		{
		  perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_SNAPSHOT,
					 PERF_SNAPSHOT_RECORD_DELETED_COMMITTED, PERF_SNAPSHOT_INVISIBLE);
		}
	    }
	  return TOO_OLD_FOR_SNAPSHOT;
	}
    }
}

/*
 * mvcc_is_not_deleted_for_snapshot () - Check whether a record is deleted or not regarding the snapshot
 *   return	     : TOO_OLD_FOR_SNAPSHOT if record is deleted and deleter is either me or is not active.
 *		       SNAPSHOT_SATISFIED if record is not deleted or deleter is neither me nor is still active.
 *   thread_p (in)   : thread entry
 *   rec_header (in) : the record header
 *   snapshot (in)   : the snapshot used for record validation
 */
MVCC_SATISFIES_SNAPSHOT_RESULT
mvcc_is_not_deleted_for_snapshot (THREAD_ENTRY * thread_p, MVCC_REC_HEADER * rec_header, MVCC_SNAPSHOT * snapshot)
{
  assert (rec_header != NULL && snapshot != NULL);

  if (!MVCC_IS_HEADER_DELID_VALID (rec_header))
    {
      /* The record is not deleted */
      return SNAPSHOT_SATISFIED;
    }
  else
    {
      /* The record is deleted */
      if (MVCC_IS_REC_DELETED_BY_ME (thread_p, rec_header))
	{
	  /* The record was deleted by current transaction and it is not visible anymore. */
	  return TOO_OLD_FOR_SNAPSHOT;
	}
      else if (MVCC_IS_REC_DELETER_IN_SNAPSHOT (thread_p, rec_header, snapshot))
	{
	  /* The record was deleted by an active transaction or by a transaction that has committed after snapshot was
	   * obtained. */
	  return SNAPSHOT_SATISFIED;
	}
      else
	{
	  /* The deleter transaction has committed and the record is not visible to current transaction. */
	  return TOO_OLD_FOR_SNAPSHOT;
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
mvcc_satisfies_vacuum (THREAD_ENTRY * thread_p, const MVCC_REC_HEADER * rec_header, MVCCID oldest_mvccid)
{
  if (!MVCC_IS_HEADER_DELID_VALID (rec_header) || MVCC_IS_REC_DELETED_SINCE_MVCCID (rec_header, oldest_mvccid))
    {
      /* The record was not deleted or was recently deleted and cannot be vacuumed completely. */
      if (!MVCC_IS_HEADER_INSID_NOT_ALL_VISIBLE (rec_header)
	  || MVCC_IS_REC_INSERTED_SINCE_MVCCID (rec_header, oldest_mvccid))
	{
	  /* 1: Record is all visible, and insert MVCCID was already removed/replaced. 2: Record was recently inserted
	   * and is not yet visible to all active transactions. Cannot vacuum insert MVCCID. */
	  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_MVCC_SNAPSHOT))
	    {
	      if (MVCC_IS_HEADER_DELID_VALID (rec_header)
		  && MVCC_IS_REC_DELETED_SINCE_MVCCID (rec_header, oldest_mvccid))
		{
		  perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_VACUUM,
					 PERF_SNAPSHOT_RECORD_DELETED_OTHER_TRAN, PERF_SNAPSHOT_INVISIBLE);
		}
	      else if (!MVCC_IS_HEADER_INSID_NOT_ALL_VISIBLE (rec_header))
		{
		  perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_VACUUM,
					 PERF_SNAPSHOT_RECORD_INSERTED_VACUUMED, PERF_SNAPSHOT_INVISIBLE);
		}
	      else
		{
		  perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_VACUUM,
					 PERF_SNAPSHOT_RECORD_INSERTED_OTHER_TRAN, PERF_SNAPSHOT_INVISIBLE);
		}
	    }
	  return VACUUM_RECORD_CANNOT_VACUUM;
	}
      else
	{
	  /* The inserter transaction has committed and the record is visible to all running transactions. Insert
	   * MVCCID and previous version lsa can be removed. */
	  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_MVCC_SNAPSHOT))
	    {
	      perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_VACUUM, PERF_SNAPSHOT_RECORD_INSERTED_COMMITED,
				     PERF_SNAPSHOT_VISIBLE);
	    }
	  return VACUUM_RECORD_DELETE_INSID_PREV_VER;
	}
    }
  else
    {
      /* The deleter transaction has committed and the record is not visible to any running transactions. */
      if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_MVCC_SNAPSHOT))
	{
	  perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_VACUUM, PERF_SNAPSHOT_RECORD_DELETED_COMMITTED,
				 PERF_SNAPSHOT_VISIBLE);
	}
      return VACUUM_RECORD_REMOVE;
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

  if (!MVCC_IS_HEADER_DELID_VALID (rec_header))
    {
      /* Record was not deleted */
      if (!MVCC_IS_FLAG_SET (rec_header, OR_MVCC_FLAG_VALID_INSID))
	{
	  /* Record was inserted and is visible for all transactions */
	  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_MVCC_SNAPSHOT))
	    {
	      perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_DELETE, PERF_SNAPSHOT_RECORD_INSERTED_VACUUMED,
				     PERF_SNAPSHOT_VISIBLE);
	    }
	  return DELETE_RECORD_CAN_DELETE;
	}

      if (MVCC_IS_REC_INSERTED_BY_ME (thread_p, rec_header))
	{
	  /* Record is only visible to current transaction and can be safely deleted. */
	  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_MVCC_SNAPSHOT))
	    {
	      perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_DELETE, PERF_SNAPSHOT_RECORD_INSERTED_CURR_TRAN,
				     PERF_SNAPSHOT_VISIBLE);
	    }
	  return DELETE_RECORD_CAN_DELETE;
	}
      else if (MVCC_IS_REC_INSERTER_ACTIVE (thread_p, rec_header))
	{
	  /* Record is inserted by an active transaction and is not visible to current transaction. */
	  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_MVCC_SNAPSHOT))
	    {
	      perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_DELETE, PERF_SNAPSHOT_RECORD_INSERTED_OTHER_TRAN,
				     PERF_SNAPSHOT_INVISIBLE);
	    }
	  return DELETE_RECORD_INSERT_IN_PROGRESS;
	}
      else
	{
	  /* The inserter transaction has committed and the record can be deleted by current transaction. */
	  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_MVCC_SNAPSHOT))
	    {
	      if (rec_header->mvcc_ins_id != MVCCID_ALL_VISIBLE && vacuum_is_mvccid_vacuumed (rec_header->mvcc_ins_id))
		{
		  perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_DELETE,
					 PERF_SNAPSHOT_RECORD_INSERTED_COMMITED_LOST, PERF_SNAPSHOT_VISIBLE);
		}
	      else
		{
		  perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_DELETE,
					 PERF_SNAPSHOT_RECORD_INSERTED_COMMITED, PERF_SNAPSHOT_VISIBLE);
		}
	    }
	  return DELETE_RECORD_CAN_DELETE;
	}
    }
  else
    {
      /* Record was already deleted */
      if (MVCC_IS_REC_DELETED_BY_ME (thread_p, rec_header))
	{
	  /* Record was already deleted by me... */
	  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_MVCC_SNAPSHOT))
	    {
	      perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_DELETE, PERF_SNAPSHOT_RECORD_DELETED_CURR_TRAN,
				     PERF_SNAPSHOT_INVISIBLE);
	    }
	  return DELETE_RECORD_SELF_DELETED;
	}
      else if (MVCC_IS_REC_DELETER_ACTIVE (thread_p, rec_header))
	{
	  /* Record was deleted by an active transaction. Current transaction must wait until the deleter completes. */
	  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_MVCC_SNAPSHOT))
	    {
	      perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_DELETE, PERF_SNAPSHOT_RECORD_DELETED_OTHER_TRAN,
				     PERF_SNAPSHOT_INVISIBLE);
	    }
	  return DELETE_RECORD_DELETE_IN_PROGRESS;
	}
      else
	{
	  /* Record was already deleted and the deleter has committed. Cannot be updated by current transaction. */
	  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_MVCC_SNAPSHOT))
	    {
	      if (vacuum_is_mvccid_vacuumed (rec_header->mvcc_del_id))
		{
		  perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_DELETE,
					 PERF_SNAPSHOT_RECORD_DELETED_COMMITTED_LOST, PERF_SNAPSHOT_INVISIBLE);
		}
	      else
		{
		  perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_DELETE,
					 PERF_SNAPSHOT_RECORD_DELETED_COMMITTED, PERF_SNAPSHOT_INVISIBLE);
		}
	    }
	  return DELETE_RECORD_DELETED;
	}
    }
}

/*
 * mvcc_satisfies_dirty () - Check whether a record is visible considering following effects:
 *			      - committed transactions
 *			      - in progress transactions
 *			      - previous commands of current transaction
 *
 *   return	     : TOO_OLD_FOR_SNAPSHOT, if the record is deleted and deleter is either me or is not active.
 *		       SNAPSHOT_SATISFIED, if the record is not deleted and deleter is neither me nor is not active.
 *   thread_p (in)   : thread entry
 *   rec_header (in) : the record header
 *   snapshot (in)   : the snapshot used for record validation
 *
 * NOTE: Besides returning snapshot result, when the result is SNAPSHOT_SATISFIED, the function have also have side
 *	 effects, changing snapshot->lowest_active_mvccid and snapshot->highest_completed_mvccid.
 *	 If record is recently inserted and inserter is considered active, its MVCCID is saved in
 *	 snapshot->lowest_active_mvccid.
 *	 If record is recently deleted and deleter is considered active, its MVCCID is saved in
 *	 snapshot->highest_completed_mvccid.
 *	 Otherwise, the two values are set to MVCCID_NULL.
 *
 * NOTE: The snapshot argument can never be the transaction snapshot!
 */
MVCC_SATISFIES_SNAPSHOT_RESULT
mvcc_satisfies_dirty (THREAD_ENTRY * thread_p, MVCC_REC_HEADER * rec_header, MVCC_SNAPSHOT * snapshot)
{
  assert (rec_header != NULL && snapshot != NULL);

  snapshot->lowest_active_mvccid = MVCCID_NULL;
  snapshot->highest_completed_mvccid = MVCCID_NULL;

  if (!MVCC_IS_HEADER_DELID_VALID (rec_header))
    {
      /* Record was not deleted */
      if (!MVCC_IS_FLAG_SET (rec_header, OR_MVCC_FLAG_VALID_INSID))
	{
	  /* Record was inserted and is visible for all transactions */
	  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_MVCC_SNAPSHOT))
	    {
	      perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_DIRTY, PERF_SNAPSHOT_RECORD_INSERTED_VACUUMED,
				     PERF_SNAPSHOT_VISIBLE);
	    }
	  return SNAPSHOT_SATISFIED;
	}
      else if (MVCC_IS_REC_INSERTED_BY_ME (thread_p, rec_header))
	{
	  /* Record was inserted by current transaction and is visible */
	  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_MVCC_SNAPSHOT))
	    {
	      perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_DIRTY, PERF_SNAPSHOT_RECORD_INSERTED_CURR_TRAN,
				     PERF_SNAPSHOT_VISIBLE);
	    }
	  return SNAPSHOT_SATISFIED;
	}
      else if (MVCC_IS_REC_INSERTER_ACTIVE (thread_p, rec_header))
	{
	  /* Record is inserted by an active transaction and is visible */
	  snapshot->lowest_active_mvccid = MVCC_GET_INSID (rec_header);
	  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_MVCC_SNAPSHOT))
	    {
	      perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_DIRTY, PERF_SNAPSHOT_RECORD_INSERTED_OTHER_TRAN,
				     PERF_SNAPSHOT_VISIBLE);
	    }
	  return SNAPSHOT_SATISFIED;
	}
      else
	{
	  /* Record is inserted by committed transaction. */
	  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_MVCC_SNAPSHOT))
	    {
	      if (rec_header->mvcc_ins_id != MVCCID_ALL_VISIBLE && vacuum_is_mvccid_vacuumed (rec_header->mvcc_ins_id))
		{
		  perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_DIRTY,
					 PERF_SNAPSHOT_RECORD_INSERTED_COMMITED_LOST, PERF_SNAPSHOT_VISIBLE);
		}
	      else
		{
		  perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_DIRTY,
					 PERF_SNAPSHOT_RECORD_INSERTED_COMMITED, PERF_SNAPSHOT_VISIBLE);
		}
	    }
	  return SNAPSHOT_SATISFIED;
	}
    }
  else
    {
      /* Record was already deleted */
      if (MVCC_IS_REC_DELETED_BY_ME (thread_p, rec_header))
	{
	  /* Record was deleted by current transaction and is not visible */
	  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_MVCC_SNAPSHOT))
	    {
	      perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_DIRTY, PERF_SNAPSHOT_RECORD_DELETED_CURR_TRAN,
				     PERF_SNAPSHOT_INVISIBLE);
	    }
	  return TOO_OLD_FOR_SNAPSHOT;
	}
      else if (MVCC_IS_REC_DELETER_ACTIVE (thread_p, rec_header))
	{
	  /* Record was deleted by other active transaction and is still visible */
	  snapshot->highest_completed_mvccid = rec_header->mvcc_del_id;
	  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_MVCC_SNAPSHOT))
	    {
	      perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_DIRTY, PERF_SNAPSHOT_RECORD_DELETED_OTHER_TRAN,
				     PERF_SNAPSHOT_VISIBLE);
	    }

	  return SNAPSHOT_SATISFIED;
	}
      else
	{
	  /* Record was already deleted and the deleter has committed. */
	  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_MVCC_SNAPSHOT))
	    {
	      if (vacuum_is_mvccid_vacuumed (rec_header->mvcc_del_id))
		{
		  perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_DIRTY,
					 PERF_SNAPSHOT_RECORD_DELETED_COMMITTED_LOST, PERF_SNAPSHOT_INVISIBLE);
		}
	      else
		{
		  perfmon_mvcc_snapshot (thread_p, PERF_SNAPSHOT_SATISFIES_DIRTY,
					 PERF_SNAPSHOT_RECORD_DELETED_COMMITTED, PERF_SNAPSHOT_INVISIBLE);
		}
	    }
	  return TOO_OLD_FOR_SNAPSHOT;
	}
    }
}

/*
* mvcc_is_mvcc_disabled_class () - MVCC is disabled for root class and
*					db_serial, db_partition.
*
* return	  : True if MVCC is disabled for class.
* thread_p (in)  : Thread entry.
* class_oid (in) : Class OID.
*/
bool
mvcc_is_mvcc_disabled_class (const OID * class_oid)
{
  if (OID_ISNULL (class_oid) || OID_IS_ROOTOID (class_oid))
    {
      /* MVCC is disabled for root class */
      return true;
    }

  if (oid_is_serial (class_oid))
    {
      return true;
    }

  if (oid_check_cached_class_oid (OID_CACHE_COLLATION_CLASS_ID, class_oid))
    {
      return true;
    }

  if (oid_check_cached_class_oid (OID_CACHE_HA_APPLY_INFO_CLASS_ID, class_oid))
    {
      return true;
    }

  return false;
}

// *INDENT-OFF*
mvcc_snapshot::mvcc_snapshot ()
  : lowest_active_mvccid (MVCCID_NULL)
  , highest_completed_mvccid (MVCCID_NULL)
  , m_active_mvccs ()
  , snapshot_fnc (NULL)
  , valid (false)
{
}

void
mvcc_snapshot::reset ()
{
  snapshot_fnc = NULL;
  lowest_active_mvccid = MVCCID_NULL;
  highest_completed_mvccid = MVCCID_NULL;

  m_active_mvccs.reset ();

  valid = false;
}

void
mvcc_snapshot::copy_to (mvcc_snapshot & dest) const
{
  dest.m_active_mvccs.initialize ();
  m_active_mvccs.copy_to (dest.m_active_mvccs, mvcc_active_tran::copy_safety::THREAD_SAFE);

  dest.lowest_active_mvccid = lowest_active_mvccid;
  dest.highest_completed_mvccid = highest_completed_mvccid;
  dest.snapshot_fnc = snapshot_fnc;
  dest.valid = valid;
}

mvcc_info::mvcc_info ()
  : snapshot ()
  , id (MVCCID_NULL)
  , recent_snapshot_lowest_active_mvccid (MVCCID_NULL)
  , sub_ids ()
  , last_mvcc_lsa (NULL_LSA)
{
}

void
mvcc_info::init ()
{
  new (this) mvcc_info ();
}

void
mvcc_info::reset ()
{
  snapshot.reset ();
  id = MVCCID_NULL;
  recent_snapshot_lowest_active_mvccid = MVCCID_NULL;
  sub_ids.clear ();
  last_mvcc_lsa.set_null ();
}
// *INDENT-ON*
