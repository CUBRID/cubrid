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
 * mvcc.c - mvcc snapshot
 */

#ident "$Id$"

#include "mvcc.h"
#include "dbtype.h"
#include "heap_file.h"
#include "page_buffer.h"
#include "overflow_file.h"
#include "perf_monitor.h"
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
  unsigned int i;
  MVCCID position;
  UINT64 *p_area;

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

  if (snapshot->bit_area_length > 0 && mvcc_id >= snapshot->bit_area_start_mvccid)
    {
      position = mvcc_id - snapshot->bit_area_start_mvccid;
      p_area = MVCC_GET_BITAREA_ELEMENT_PTR (snapshot->bit_area, position);
      if (((*p_area) & MVCC_BITAREA_MASK (position)) == 0)
	{
	  /* active transaction found */
	  return true;
	}
    }

  for (i = 0; i < snapshot->long_tran_mvccids_length; i++)
    {
      /* long transactions - rare case */
      if (MVCCID_IS_EQUAL (mvcc_id, snapshot->long_tran_mvccids[i]))
	{
	  return true;
	}
    }

  return false;
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
 *  is atomically checked (See logtb_get_mvcc_snapshot_data comments).
 */
STATIC_INLINE bool
mvcc_is_active_id (THREAD_ENTRY * thread_p, MVCCID mvccid)
{
  LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
  MVCC_INFO *curr_mvcc_info = NULL;
  MVCCTABLE *mvcc_table = &log_Gl.mvcc_table;
  UINT64 *p_area;
  int local_bit_area_length;
  MVCCID position, local_bit_area_start_mvccid;
  bool is_active;
  unsigned int i;
  MVCC_TRANS_STATUS *trans_status;
  int index;
  unsigned int trans_status_version;

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

#if defined(HAVE_ATOMIC_BUILTINS)
start_check_active:
  index = ATOMIC_INC_32 (&mvcc_table->trans_status_history_position, 0);
  trans_status = mvcc_table->trans_status_history + index;
  trans_status_version = ATOMIC_INC_32 (&trans_status->version, 0);

  local_bit_area_start_mvccid = ATOMIC_INC_64 (&trans_status->bit_area_start_mvccid, 0LL);
  local_bit_area_length = ATOMIC_INC_32 (&trans_status->bit_area_length, 0);

#else
  (void) pthread_mutex_lock (&mvcc_table->active_trans_mutex);
  local_bit_area_length = trans_status->bit_area_length;
  if (local_bit_area_length == 0)
    {
      return false;
    }
  local_bit_area_start_mvccid = mvcc_table->current_trans_status->bit_area_start_mvccid;
#endif

  /* no one can change active transactions while I'm in CS */
  if (MVCC_ID_PRECEDES (mvccid, local_bit_area_start_mvccid))
    {
      is_active = false;
      /* check long time transactions */
      if (trans_status->long_tran_mvccids_length > 0 && trans_status->long_tran_mvccids != NULL)
	{
	  /* called rarely - has long transactions */
	  for (i = 0; i < trans_status->long_tran_mvccids_length; i++)
	    {
	      if (trans_status->long_tran_mvccids[i] == mvccid)
		{
		  break;
		}
	    }
	  if (i < trans_status->long_tran_mvccids_length)
	    {
	      /* MVCCID of long transaction found */
	      is_active = true;
	    }
	}
    }
  else if (local_bit_area_length == 0)
    {
      /* mvccid > highest completed MVCCID */
      is_active = true;
    }
  else
    {
      is_active = true;
      position = mvccid - local_bit_area_start_mvccid;
      if ((int) position < local_bit_area_length)
	{
	  p_area = MVCC_GET_BITAREA_ELEMENT_PTR (trans_status->bit_area, position);
	  if (((*p_area) & MVCC_BITAREA_MASK (position)) != 0)
	    {
	      /* committed transaction found */
	      is_active = false;
	    }
	}
    }

#if defined(HAVE_ATOMIC_BUILTINS)
  if (trans_status_version != ATOMIC_INC_32 (&trans_status->version, 0))
    {
      /* The transaction status version overwritten, need to read again */
      goto start_check_active;
    }
#else
  (void) pthread_mutex_unlock (&mvcc_table->active_trans_mutex);
#endif

  return is_active;
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
mvcc_satisfies_vacuum (THREAD_ENTRY * thread_p, MVCC_REC_HEADER * rec_header, MVCCID oldest_mvccid)
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
