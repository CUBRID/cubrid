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
// MVCC table - transaction information required for multi-version concurrency control system
//

#include "mvcc_table.hpp"

#include "extensible_array.hpp"
#include "log_impl.h"
#include "mvcc.h"
#include "perf_monitor.h"
#include "thread_manager.hpp"

#include <cassert>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

// help debugging oldest active by following all changes
struct oldest_active_event
{
  enum op_type
  {
    SET,
    GET,
    GET_LOWEST_ACTIVE
  };

  enum source
  {
    BUILD_MVCC_INFO,
    COMPLETE_MVCC,
    RESET,
    ADVANCE_LOWEST,
    GET_OLDEST_ACTIVE
  };

  MVCCID m_value;
  int m_tran_index_or_global;   // 0 for global, non-zero for active transactions
  op_type m_set_or_get;    // self-explanatory
  source m_source;
  // todo - add thread index?

  oldest_active_event &operator= (const oldest_active_event &other)
  {
    m_value = other.m_value;
    m_tran_index_or_global = other.m_tran_index_or_global;
    m_set_or_get = other.m_set_or_get;
    m_source = other.m_source;

    return *this;
  }
};
#if !defined (NDEBUG)
const size_t OLDEST_ACTIVE_HISTORY_SIZE = 1024 * 8;   // 8k
struct oldest_active_history_tracker
{
  std::atomic<size_t> m_event_count;
  oldest_active_event m_history[OLDEST_ACTIVE_HISTORY_SIZE];
};
oldest_active_history_tracker Oldest_active_tracker;

static inline void
oldest_active_add_event (MVCCID mvccid, int tran_index, oldest_active_event::op_type set_or_get,
			 oldest_active_event::source src)
{
  size_t index = Oldest_active_tracker.m_event_count++ % OLDEST_ACTIVE_HISTORY_SIZE;
  Oldest_active_tracker.m_history[index] = { mvccid, tran_index, set_or_get, src };
}

// NOTE - while investigating history, please consider that not all Oldest_active_event_count events may be mature.
//        investigate concurrent threads that may still be working on populating their event
#endif // debug

static inline void
oldest_active_set (mvcctable::lowest_active_mvccid_type &lowest, int tran_index, MVCCID mvccid,
		   oldest_active_event::source src)
{
#if !defined (NDEBUG)
  oldest_active_add_event (mvccid, tran_index, oldest_active_event::SET, src);
#endif
  lowest.store (mvccid);
}

static inline MVCCID
oldest_active_get (const mvcctable::lowest_active_mvccid_type &lowest, int tran_index,
		   oldest_active_event::source src)
{
  MVCCID mvccid = lowest.load ();
#if !defined (NDEBUG)
  if (mvccid != MVCCID_NULL)
    {
      // don't spam will null reads
      oldest_active_add_event (mvccid, tran_index, oldest_active_event::GET, src);
    }
#endif
  return mvccid;
}

mvcc_trans_status::mvcc_trans_status ()
  : m_active_mvccs ()
  , m_last_completed_mvccid (MVCCID_NULL)
  , m_event_type (COMMIT)
  , m_version (0)
{
}

mvcc_trans_status::~mvcc_trans_status ()
{
}

void
mvcc_trans_status::initialize ()
{
  m_active_mvccs.initialize ();
  m_version = 0;
}

void
mvcc_trans_status::finalize ()
{
  m_active_mvccs.finalize ();
}

void
mvcctable::advance_oldest_active (MVCCID next_oldest_active)
{
  MVCCID crt_oldest_active;
  do
    {
      crt_oldest_active = m_current_status_lowest_active_mvccid.load ();
      if (crt_oldest_active >= next_oldest_active)
	{
	  // already advanced to equal or better
	  return;
	}
    }
  while (!m_current_status_lowest_active_mvccid.compare_exchange_strong (crt_oldest_active, next_oldest_active));
#if !defined (NDEBUG)
  oldest_active_add_event (next_oldest_active, 0, oldest_active_event::SET, oldest_active_event::ADVANCE_LOWEST);
#endif // debug
}

//
// MVCC table
//

mvcctable::mvcctable ()
  : m_transaction_lowest_visible_mvccids (NULL)
  , m_transaction_lowest_visible_mvccids_size (0)
  , m_current_status_lowest_active_mvccid (MVCCID_FIRST)
  , m_current_trans_status ()
  , m_trans_status_history_position (0)
  , m_trans_status_history (NULL)
  , m_new_mvccid_lock ()
  , m_active_trans_mutex ()
  , m_oldest_visible (MVCCID_NULL)
  , m_ov_lock_count (0)
{
}

mvcctable::~mvcctable ()
{
  delete [] m_transaction_lowest_visible_mvccids;
  delete [] m_trans_status_history;
}

void
mvcctable::initialize ()
{
  m_current_trans_status.initialize ();
  m_trans_status_history = new mvcc_trans_status[HISTORY_MAX_SIZE];
  for (size_t idx = 0; idx < HISTORY_MAX_SIZE; idx++)
    {
      m_trans_status_history[idx].initialize ();
    }
  m_trans_status_history_position = 0;
  m_current_status_lowest_active_mvccid = MVCCID_FIRST;

  alloc_transaction_lowest_active ();
}

void
mvcctable::alloc_transaction_lowest_active ()
{
  if (m_transaction_lowest_visible_mvccids_size != (size_t) logtb_get_number_of_total_tran_indices ())
    {
      // either first time or transaction table size has changed
      delete [] m_transaction_lowest_visible_mvccids;
      m_transaction_lowest_visible_mvccids_size = logtb_get_number_of_total_tran_indices ();
      m_transaction_lowest_visible_mvccids = new lowest_active_mvccid_type[m_transaction_lowest_visible_mvccids_size] ();
      // all are 0 = MVCCID_NULL
    }
}

void
mvcctable::finalize ()
{
  m_current_trans_status.finalize ();

  delete [] m_trans_status_history;
  m_trans_status_history = NULL;

  delete [] m_transaction_lowest_visible_mvccids;
  m_transaction_lowest_visible_mvccids = NULL;
  m_transaction_lowest_visible_mvccids_size = 0;
}

void
mvcctable::build_mvcc_info (log_tdes &tdes)
{
  MVCCID tx_lowest_active;
  MVCCID crt_status_lowest_active;
  size_t index;
  mvcc_trans_status::version_type trans_status_version;

  MVCCID highest_completed_mvccid;

  bool is_perf_tracking = perfmon_is_perf_tracking ();
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
  UINT64 snapshot_wait_time;
  UINT64 snapshot_retry_count = 0;

  assert (tdes.tran_index >= 0 && tdes.tran_index < logtb_get_number_of_total_tran_indices ());

  if (is_perf_tracking)
    {
      tsc_getticks (&start_tick);
    }

  // make sure snapshot has allocated data
  tdes.mvccinfo.snapshot.m_active_mvccs.initialize ();

  tx_lowest_active = oldest_active_get (m_transaction_lowest_visible_mvccids[tdes.tran_index], tdes.tran_index,
					oldest_active_event::BUILD_MVCC_INFO);

  // repeat steps until a trans_status can be read successfully without a version change
  while (true)
    {
      snapshot_retry_count++;

      if (!MVCCID_IS_VALID (tx_lowest_active))
	{
	  /*
	   * First, by setting MVCCID_ALL_VISIBLE we will tell to VACUUM that transaction lowest MVCCID will be set
	   * soon.
	   * This is needed since setting p_transaction_lowest_active_mvccid is not an atomic operation (global
	   * lowest_active_mvccid must be obtained first). We want to avoid a possible scenario (even if the chances
	   * are minimal) like the following one:
	   *    - the snapshot thread reads the initial value of global lowest active MVCCID but the thread is
	   * suspended (due to thread switching) just before setting p_transaction_lowest_active_mvccid
	   *    - the transaction having global lowest active MVCCID commits, so the global value is updated (advanced)
	   *    - the VACCUM thread computes the MVCCID threshold as the updated global lowest active MVCCID
	   *    - the snapshot thread resumes and p_transaction_lowest_active_mvccid is set to initial value of global
	   * lowest active MVCCID
	   *    - the VACUUM thread computes the threshold again and found a value (initial global lowest active MVCCID)
	   * less than the previously threshold
	   */
	  oldest_active_set (m_transaction_lowest_visible_mvccids[tdes.tran_index], tdes.tran_index,
			     MVCCID_ALL_VISIBLE, oldest_active_event::BUILD_MVCC_INFO);

	  /*
	   * Is important that between next two code lines to not have delays (to not execute any other code).
	   * Otherwise, VACUUM may delay, waiting more in logtb_get_oldest_active_mvccid.
	   */
	  crt_status_lowest_active = oldest_active_get (m_current_status_lowest_active_mvccid, 0,
				     oldest_active_event::BUILD_MVCC_INFO);
	  oldest_active_set (m_transaction_lowest_visible_mvccids[tdes.tran_index], tdes.tran_index,
			     crt_status_lowest_active, oldest_active_event::BUILD_MVCC_INFO);
	}
      else
	{
	  crt_status_lowest_active = oldest_active_get (m_current_status_lowest_active_mvccid, 0,
				     oldest_active_event::BUILD_MVCC_INFO);
	}

      index = m_trans_status_history_position.load ();
      assert (index < HISTORY_MAX_SIZE);

      const mvcc_trans_status &trans_status = m_trans_status_history[index];

      trans_status_version = trans_status.m_version.load ();
      trans_status.m_active_mvccs.copy_to (tdes.mvccinfo.snapshot.m_active_mvccs,
					   mvcc_active_tran::copy_safety::THREAD_UNSAFE);

      if (logtb_load_global_statistics_to_tran (thread_get_thread_entry_info())!= NO_ERROR)
	{
	  /* just error setting without returning for further processing */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_MVCC_CANT_GET_SNAPSHOT, 0);
	}

      if (trans_status_version == trans_status.m_version.load ())
	{
	  // no version change; copying status was successful
	  break;
	}
      else
	{
	  // a failed copy may break data validity; to make sure next copy is not affected, it is better to reset
	  // bit area.
	  tdes.mvccinfo.snapshot.m_active_mvccs.reset_active_transactions ();
	}
    }

  // tdes.mvccinfo.snapshot.m_active_mvccs was not checked because it was not safe; now it is
  tdes.mvccinfo.snapshot.m_active_mvccs.check_valid ();

  highest_completed_mvccid = tdes.mvccinfo.snapshot.m_active_mvccs.compute_highest_completed_mvccid ();
  MVCCID_FORWARD (highest_completed_mvccid);

  /* update lowest active mvccid computed for the most recent snapshot */
  tdes.mvccinfo.recent_snapshot_lowest_active_mvccid = crt_status_lowest_active;

  /* update remaining snapshot data */
  tdes.mvccinfo.snapshot.snapshot_fnc = mvcc_satisfies_snapshot;
  tdes.mvccinfo.snapshot.lowest_active_mvccid = crt_status_lowest_active;
  tdes.mvccinfo.snapshot.highest_completed_mvccid = highest_completed_mvccid;
  tdes.mvccinfo.snapshot.valid = true;

  if (is_perf_tracking)
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
      snapshot_wait_time = tv_diff.tv_sec * 1000000LL + tv_diff.tv_usec;
      if (snapshot_wait_time > 0)
	{
	  perfmon_add_stat (thread_get_thread_entry_info (), PSTAT_LOG_SNAPSHOT_TIME_COUNTERS, snapshot_wait_time);
	}
      if (snapshot_retry_count > 1)
	{
	  perfmon_add_stat (thread_get_thread_entry_info (), PSTAT_LOG_SNAPSHOT_RETRY_COUNTERS,
			    snapshot_retry_count - 1);
	}
    }
}

MVCCID
mvcctable::compute_oldest_visible_mvccid () const
{
  perf_utime_tracker perf;
  cubthread::entry &threadr = cubthread::get_entry ();
  PERF_UTIME_TRACKER_START (&threadr, &perf);

  const size_t MVCC_OLDEST_ACTIVE_BUFFER_LENGTH = 32;
  cubmem::appendable_array<size_t, MVCC_OLDEST_ACTIVE_BUFFER_LENGTH> waiting_mvccids_pos;
  MVCCID loaded_tran_mvccid;
  MVCCID lowest_active_mvccid = oldest_active_get (m_current_status_lowest_active_mvccid, 0,
				oldest_active_event::GET_OLDEST_ACTIVE);

  for (size_t idx = 0; idx < m_transaction_lowest_visible_mvccids_size; idx++)
    {
      loaded_tran_mvccid = oldest_active_get (m_transaction_lowest_visible_mvccids[idx], idx,
					      oldest_active_event::GET_OLDEST_ACTIVE);
      if (loaded_tran_mvccid == MVCCID_ALL_VISIBLE)
	{
	  waiting_mvccids_pos.append (idx);
	}
      else if (loaded_tran_mvccid != MVCCID_NULL && MVCC_ID_PRECEDES (loaded_tran_mvccid, lowest_active_mvccid))
	{
	  lowest_active_mvccid = loaded_tran_mvccid;
	}
    }

  size_t retry_count = 0;
  while (waiting_mvccids_pos.get_size () > 0)
    {
      ++retry_count;
      if (retry_count % 20 == 0)
	{
	  thread_sleep (10);
	}

      for (size_t i = waiting_mvccids_pos.get_size () - 1; i < waiting_mvccids_pos.get_size (); --i)
	{
	  size_t pos = waiting_mvccids_pos.get_array ()[i];
	  loaded_tran_mvccid = oldest_active_get (m_transaction_lowest_visible_mvccids[pos], pos,
						  oldest_active_event::GET_OLDEST_ACTIVE);
	  if (loaded_tran_mvccid == MVCCID_ALL_VISIBLE)
	    {
	      /* Not set yet, need to wait more. */
	      continue;
	    }
	  if (loaded_tran_mvccid != MVCCID_NULL && MVCC_ID_PRECEDES (loaded_tran_mvccid, lowest_active_mvccid))
	    {
	      lowest_active_mvccid = loaded_tran_mvccid;
	    }
	  // remove from waiting array
	  waiting_mvccids_pos.erase (i);
	}
    }

  if (perf.is_perf_tracking)
    {
      PERF_UTIME_TRACKER_TIME (&threadr, &perf, PSTAT_LOG_OLDEST_MVCC_TIME_COUNTERS);
      if (retry_count > 0)
	{
	  perfmon_add_stat (&cubthread::get_entry (), PSTAT_LOG_OLDEST_MVCC_RETRY_COUNTERS, retry_count);
	}
    }

  assert (MVCCID_IS_NORMAL (lowest_active_mvccid));
  return lowest_active_mvccid;
}

bool
mvcctable::is_active (MVCCID mvccid) const
{
  size_t index = 0;
  mvcc_trans_status::version_type version;
  bool ret_active = false;
  // trans status must be same before and after computing is_active. if it is not, we need to repeat the computation.
  do
    {
      index = m_trans_status_history_position.load ();
      version = m_trans_status_history[index].m_version.load ();
      ret_active = m_trans_status_history[index].m_active_mvccs.is_active (mvccid);
    }
  while (version != m_trans_status_history[index].m_version.load ());

  return ret_active;
}

mvcc_trans_status &
mvcctable::next_trans_status_start (mvcc_trans_status::version_type &next_version, size_t &next_index)
{
  // new version, new status entry
  next_index = (m_trans_status_history_position.load () + 1) & HISTORY_INDEX_MASK;
  next_version = ++m_current_trans_status.m_version;

  // invalidate next status entry
  mvcc_trans_status &next_trans_status = m_trans_status_history[next_index];
  next_trans_status.m_version.store (next_version);

  return next_trans_status;
}

void
mvcctable::next_tran_status_finish (mvcc_trans_status &next_trans_status, size_t next_index)
{
  m_current_trans_status.m_active_mvccs.copy_to (next_trans_status.m_active_mvccs,
      mvcc_active_tran::copy_safety::THREAD_SAFE);
  next_trans_status.m_last_completed_mvccid = m_current_trans_status.m_last_completed_mvccid;
  next_trans_status.m_event_type = m_current_trans_status.m_event_type;
  m_trans_status_history_position.store (next_index);
}

void
mvcctable::complete_mvcc (int tran_index, MVCCID mvccid, bool committed)
{
  assert (MVCCID_IS_VALID (mvccid));

  // only one can change status at a time
  std::unique_lock<std::mutex> ulock (m_active_trans_mutex);

  mvcc_trans_status::version_type next_version;
  size_t next_index;
  mvcc_trans_status &next_status = next_trans_status_start (next_version, next_index);

  // todo - until we activate count optimization (if ever), should we move this outside mutex?
  if (committed && logtb_tran_update_all_global_unique_stats (thread_get_thread_entry_info ()) != NO_ERROR)
    {
      assert (false);
    }

  // update current trans status
  m_current_trans_status.m_active_mvccs.set_inactive_mvccid (mvccid);
  m_current_trans_status.m_last_completed_mvccid = mvccid;
  m_current_trans_status.m_event_type = committed ? mvcc_trans_status::COMMIT : mvcc_trans_status::ROLLBACK;

  // finish next trans status
  next_tran_status_finish (next_status, next_index);

  if (committed)
    {
      /* be sure that transaction modifications can't be vacuumed up to LOG_COMMIT. Otherwise, the following
       * scenario will corrupt the database:
       * - transaction set its lowest_active_mvccid to MVCCID_NULL
       * - VACUUM clean up transaction modifications
       * - the system crash before LOG_COMMIT of current transaction
       *
       * It will be set to NULL after LOG_COMMIT
       */
      MVCCID tran_lowest_active = oldest_active_get (m_transaction_lowest_visible_mvccids[tran_index], tran_index,
				  oldest_active_event::COMPLETE_MVCC);
      if (tran_lowest_active == MVCCID_NULL || MVCC_ID_PRECEDES (tran_lowest_active, mvccid))
	{
	  oldest_active_set (m_transaction_lowest_visible_mvccids[tran_index], tran_index, mvccid,
			     oldest_active_event::COMPLETE_MVCC);
	}
    }
  else
    {
      oldest_active_set (m_transaction_lowest_visible_mvccids[tran_index], tran_index, MVCCID_NULL,
			 oldest_active_event::COMPLETE_MVCC);
    }

  ulock.unlock ();

  // update lowest active in current transactions status. can be done outside lock
  // this doesn't have to be 100% accurate; it is used as indicative by vacuum to clean up the database. however, it
  // shouldn't be left too much behind, or vacuum can't advance
  // so we try to limit recalculation when mvccid matches current global_lowest_active; since we are not locked, it is
  // not guaranteed to be always updated; therefore we add the second condition to go below trans status
  // bit area starting MVCCID; the recalculation will happen on each iteration if there are long transactions.
  MVCCID global_lowest_active = m_current_status_lowest_active_mvccid;
  if (global_lowest_active == mvccid
      || MVCC_ID_PRECEDES (mvccid, next_status.m_active_mvccs.get_bit_area_start_mvccid ()))
    {
      MVCCID new_lowest_active = next_status.m_active_mvccs.compute_lowest_active_mvccid ();
#if !defined (NDEBUG)
      oldest_active_add_event (new_lowest_active, (int) next_index, oldest_active_event::GET_LOWEST_ACTIVE,
			       oldest_active_event::COMPLETE_MVCC);
#endif // !NDEBUG
      // we need to recheck version to validate result
      if (next_status.m_version.load () == next_version)
	{
	  // advance
	  advance_oldest_active (new_lowest_active);
	}
    }
}

void
mvcctable::complete_sub_mvcc (MVCCID mvccid)
{
  assert (MVCCID_IS_VALID (mvccid));

  // only one can change status at a time
  std::unique_lock<std::mutex> ulock (m_active_trans_mutex);

  mvcc_trans_status::version_type next_version;
  size_t next_index;
  mvcc_trans_status &next_status = next_trans_status_start (next_version, next_index);

  // update current trans status
  m_current_trans_status.m_active_mvccs.set_inactive_mvccid (mvccid);
  m_current_trans_status.m_last_completed_mvccid = mvccid;
  m_current_trans_status.m_last_completed_mvccid = mvcc_trans_status::SUBTRAN;

  // finish next trans status
  next_tran_status_finish (next_status, next_index);

  ulock.unlock ();

  // mvccid can't be lowest, so no need to update it here
}

MVCCID
mvcctable::get_new_mvccid ()
{
  MVCCID id;

  m_new_mvccid_lock.lock ();
  id = log_Gl.hdr.mvcc_next_id;
  MVCCID_FORWARD (log_Gl.hdr.mvcc_next_id);
  m_new_mvccid_lock.unlock ();

  return id;
}

void
mvcctable::get_two_new_mvccid (MVCCID &first, MVCCID &second)
{
  m_new_mvccid_lock.lock ();

  first = log_Gl.hdr.mvcc_next_id;
  MVCCID_FORWARD (log_Gl.hdr.mvcc_next_id);

  second = log_Gl.hdr.mvcc_next_id;
  MVCCID_FORWARD (log_Gl.hdr.mvcc_next_id);

  m_new_mvccid_lock.unlock ();
}

void
mvcctable::reset_transaction_lowest_active (int tran_index)
{
  oldest_active_set (m_transaction_lowest_visible_mvccids[tran_index], tran_index, MVCCID_NULL,
		     oldest_active_event::RESET);
}

void
mvcctable::reset_start_mvccid ()
{
  m_current_trans_status.m_active_mvccs.reset_start_mvccid (log_Gl.hdr.mvcc_next_id);

  assert (m_trans_status_history_position < HISTORY_MAX_SIZE);
  m_trans_status_history[m_trans_status_history_position].m_active_mvccs.reset_start_mvccid (log_Gl.hdr.mvcc_next_id);

  m_current_status_lowest_active_mvccid.store (log_Gl.hdr.mvcc_next_id);
}

MVCCID
mvcctable::get_global_oldest_visible () const
{
  return m_oldest_visible.load ();
}

MVCCID
mvcctable::update_global_oldest_visible ()
{
  if (m_ov_lock_count == 0)
    {
      MVCCID oldest_visible = compute_oldest_visible_mvccid ();
      if (m_ov_lock_count == 0)
	{
	  assert (m_oldest_visible.load () <= oldest_visible);
	  m_oldest_visible.store (oldest_visible);
	}
    }
  return m_oldest_visible.load ();
}

void
mvcctable::lock_global_oldest_visible ()
{
  ++m_ov_lock_count;
}

void
mvcctable::unlock_global_oldest_visible ()
{
  assert (m_ov_lock_count > 0);
  --m_ov_lock_count;
}

bool
mvcctable::is_global_oldest_visible_locked () const
{
  return m_ov_lock_count != 0;
}
