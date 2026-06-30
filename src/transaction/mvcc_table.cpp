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

#include <algorithm>
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
  , m_new_mvccid_lock ()
  , m_oldest_visible (MVCCID_NULL)
  , m_ov_lock_count (0)
  , m_active_mvccids (NULL)
  , m_active_mvccids_size (0)
  , m_last_completed_mvccid (MVCCID_NULL)
  , m_completion_count (0)
{
}

mvcctable::~mvcctable ()
{
  delete [] m_transaction_lowest_visible_mvccids;
  delete [] m_active_mvccids;
}

void
mvcctable::initialize ()
{
  m_current_status_lowest_active_mvccid = MVCCID_FIRST;
  m_last_completed_mvccid = MVCCID_NULL;
  m_completion_count = 0;

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

      // ProcArray slot array shares lifecycle/size with the per-tran lowest-visible array.
      delete [] m_active_mvccids;
      m_active_mvccids_size = m_transaction_lowest_visible_mvccids_size;
      m_active_mvccids = new mvcc_active_slot[m_active_mvccids_size] ();
      // value-initialized: mvccid=0(MVCCID_NULL), n_subids=0, subid_overflow=false, subids=0
    }
}

void
mvcctable::finalize ()
{
  delete [] m_transaction_lowest_visible_mvccids;
  m_transaction_lowest_visible_mvccids = NULL;
  m_transaction_lowest_visible_mvccids_size = 0;

  delete [] m_active_mvccids;
  m_active_mvccids = NULL;
  m_active_mvccids_size = 0;
}

void
mvcctable::build_mvcc_info (log_tdes &tdes)
{
  MVCCID tx_lowest_active;
  MVCCID crt_status_lowest_active;

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

  // vacuum coordinate: publish this snapshot's lowest-visible MVCCID. The MVCCID_ALL_VISIBLE
  // sentinel protects the read-global-then-set-per-tran race (see scenario below). Preserved
  // unchanged from the history-ring design; only the active-set construction changes (slot scan).
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

  if (logtb_load_global_statistics_to_tran (thread_get_thread_entry_info ()) != NO_ERROR)
    {
      /* just error setting without returning for further processing */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_MVCC_CANT_GET_SNAPSHOT, 0);
    }

  // CBRD-26971 Phase 2 (PG14 GetSnapshotDataReuse): if no completion happened since this
  // transaction last built a snapshot, the active set (xmin/xmax/xip) is unchanged -> reuse it
  // and skip the slot scan entirely. m_completion_count is bumped (release) on every
  // commit/rollback/sub completion (see apply_clear / retire_sub_mvccid) and is the cache
  // invalidation key; no lock is held when it is bumped or read.
  if (tdes.mvccinfo.snapshot.valid
      && tdes.mvccinfo.snapshot.cached_completion_count == m_completion_count.load (std::memory_order_acquire))
    {
      // cache hit: keep snapshot.m_xip; reuse the cached xmax.
      highest_completed_mvccid = tdes.mvccinfo.snapshot.highest_completed_mvccid;
    }
  else
    {
      // CBRD-26971 lock-free seqlock scan: read the completion counter (v1), build the active set,
      // re-read the counter (v2); retry while they differ. Completion bumps the counter (release)
      // AFTER clearing its slot and advancing last_completed, so a stable v1==v2 window yields a
      // consistent cut without any lock.
      UINT64 v1, v2;
      std::vector<MVCCID> &xip = tdes.mvccinfo.snapshot.m_xip;
      do
	{
	  v1 = m_completion_count.load (std::memory_order_acquire);

	  highest_completed_mvccid = m_last_completed_mvccid.load (std::memory_order_acquire);
	  MVCCID_FORWARD (highest_completed_mvccid);

	  xip.clear ();
	  for (size_t i = 0; i < m_active_mvccids_size; i++)
	    {
	      const mvcc_active_slot &slot = m_active_mvccids[i];
	      MVCCID id = slot.mvccid.load (std::memory_order_acquire);
	      if (MVCCID_IS_VALID (id))
		{
		  xip.push_back (id);
		}
	      // instant-lock sub depth is ~1; the fixed sub-cache is never expected to overflow.
	      assert (!slot.subid_overflow.load (std::memory_order_acquire));
	      // n_subids (acquire) is read BEFORE subids[] so the publish_sub release on n_subids
	      // makes the subid stores visible (no torn read).
	      int ns = slot.n_subids.load (std::memory_order_acquire);
	      for (int k = 0; k < ns && k < mvcc_active_slot::MAX_CACHED_SUBIDS; k++)
		{
		  MVCCID sid = slot.subids[k].load (std::memory_order_acquire);
		  if (MVCCID_IS_VALID (sid))
		    {
		      xip.push_back (sid);
		    }
		}
	    }

	  v2 = m_completion_count.load (std::memory_order_acquire);
	  if (v1 != v2)
	    {
	      snapshot_retry_count++;
	    }
	}
      while (v1 != v2);

      std::sort (xip.begin (), xip.end ());
      xip.erase (std::unique (xip.begin (), xip.end ()), xip.end ());
      // Store v1 as the Phase-2 reuse key.  Note: logtb_complete_sub_mvcc may later adjust the
      // live snapshot (highest_completed_mvccid, m_xip) in place without touching
      // cached_completion_count.  That is safe: retire_sub_mvccid bumped m_completion_count, so
      // the stored key is now stale, which forces a cache MISS (never a wrong reuse) on the
      // next build_mvcc_info call.
      tdes.mvccinfo.snapshot.cached_completion_count = v1;
    }

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
  // CBRD-26971 lock-free: active iff mvccid is published in some slot (parent or active sub).
  // seqlock retry: re-scan if a completion bumped the version mid-scan, so the answer reflects
  // a consistent cut. No lock.
  UINT64 v1, v2;
  bool found;
  do
    {
      v1 = m_completion_count.load (std::memory_order_acquire);
      found = false;
      for (size_t i = 0; i < m_active_mvccids_size && !found; i++)
	{
	  const mvcc_active_slot &slot = m_active_mvccids[i];
	  if (slot.mvccid.load (std::memory_order_acquire) == mvccid)
	    {
	      found = true;
	      break;
	    }
	  int ns = slot.n_subids.load (std::memory_order_acquire);
	  for (int k = 0; k < ns && k < mvcc_active_slot::MAX_CACHED_SUBIDS; k++)
	    {
	      if (slot.subids[k].load (std::memory_order_acquire) == mvccid)
		{
		  found = true;
		  break;
		}
	    }
	}
      v2 = m_completion_count.load (std::memory_order_acquire);
    }
  while (v1 != v2);
  return found;
}

void
mvcctable::complete_mvcc (int tran_index, MVCCID mvccid, bool committed)
{
  assert (MVCCID_IS_VALID (mvccid));

  // CBRD-26971 Phase 1: no global mutex, no whole-bitmap copy. Unique stats update is now lock-free.
  if (committed && logtb_tran_update_all_global_unique_stats (thread_get_thread_entry_info ()) != NO_ERROR)
    {
      assert (false);
    }

  // Per-tran vacuum coordinate (only this transaction writes its own index; atomic, no global lock).
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

  // CBRD-26971 lock-free: the slot is single-writer (this tran_index); apply_clear orders the
  // slot clear + last_completed advance before the m_completion_count bump (seqlock version).
  apply_clear (tran_index, mvccid);

  // Advance the global oldest-active (indicative for vacuum) from a lock-free slot scan. The scanned
  // minimum is a valid lower bound (newer transactions get higher MVCCIDs), and advance_oldest_active
  // is a monotonic CAS, so this is safe under concurrency without any serialization.
  advance_oldest_active (compute_lowest_active_from_slots ());
}

void
mvcctable::apply_clear (int tran_index, MVCCID mvccid)
{
  // CBRD-26971 lock-free: the slot is single-writer (this tran_index), so no lock is needed.
  // Ordering: clear the slot and advance last_completed BEFORE bumping m_completion_count
  // (the trailing release fetch_add, LAST) so a seqlock reader that sees the new count also
  // sees the slot clear and the advanced xmax.
  //   n_subids and subid_overflow are cleared with relaxed ordering, which is safe because
  // the trailing release m_completion_count.fetch_add is sequenced after them in this thread;
  // a reader that observes the new count acquires that release edge and therefore observes
  // these clears as well.
  // Safety model: this is a *single-trailing-bump* seqlock (not classic two-phase); a reader
  // can observe partial mutations and still see v1==v2.  This is safe because every
  // partially-observable state is conservative: any value seen mid-mutation is a real,
  // untearable MVCCID that is either active or completing, and the xmax boundary guarantees
  // that a still-active id is always >= xmax and therefore classified active even if absent
  // from xip.
  mvcc_active_slot &slot = m_active_mvccids[tran_index];
  slot.mvccid.store (MVCCID_NULL, std::memory_order_release);
  slot.n_subids.store (0, std::memory_order_relaxed);
  slot.subid_overflow.store (false, std::memory_order_relaxed);

  MVCCID cur = m_last_completed_mvccid.load (std::memory_order_relaxed);
  while (MVCC_ID_PRECEDES (cur, mvccid)
	 && !m_last_completed_mvccid.compare_exchange_weak (cur, mvccid, std::memory_order_release,
	     std::memory_order_relaxed))
    {
      /* cur reloaded by compare_exchange_weak; retry */
    }

  m_completion_count.fetch_add (1, std::memory_order_release);
}

MVCCID
mvcctable::compute_lowest_active_from_slots () const
{
  // default = "no active transaction": oldest visible can advance to just past the last completed id.
  MVCCID lowest = m_last_completed_mvccid.load (std::memory_order_acquire);
  MVCCID_FORWARD (lowest);
  for (size_t i = 0; i < m_active_mvccids_size; i++)
    {
      // parent ids are the candidates for the minimum; a sub id is always greater than its parent,
      // and the parent stays published while any of its subs are active.
      MVCCID id = m_active_mvccids[i].mvccid.load (std::memory_order_acquire);
      if (MVCCID_IS_VALID (id) && MVCC_ID_PRECEDES (id, lowest))
	{
	  lowest = id;
	}
    }
  return lowest;
}

void
mvcctable::complete_sub_mvcc (MVCCID mvccid)
{
  assert (MVCCID_IS_VALID (mvccid));
  // CBRD-26971: this entry point is intentionally a no-op stub.  All authoritative
  // sub-completion work — dropping the sub from the slot cache, advancing
  // last_completed, and bumping m_completion_count — is performed by retire_sub_mvccid(),
  // which the caller (log_tran_table.c) invokes immediately after this function.  The
  // stub is kept for caller-API symmetry (complete_mvcc / complete_sub_mvcc pairing).
  // A sub id is never the global oldest active, so no vacuum-coordinate update is needed.
  (void) mvccid;
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

//
// ProcArray slot publish / retire (CBRD-26971).
// These slots are the SOLE source of truth for the active transaction set.  They are
// published and cleared lock-free and scanned by snapshot readers under the seqlock
// protocol (m_completion_count v1/v2 wrap in build_mvcc_info / is_active).
//
// Correctness of publish (no counter bump): a freshly published id is always
// > m_last_completed_mvccid (MVCCIDs are allocated monotonically and last_completed only
// advances when a transaction actually completes), so every concurrent snapshot sees
// id >= xmax and classifies it active WITHOUT needing it in xip.  The dangerous direction
// — a still-active id judged visible — cannot occur because that requires id < xmax, which
// requires last_completed to have advanced past id, which only happens when id completes.
//

void
mvcctable::publish_active_mvccid (int tran_index, MVCCID mvccid)
{
  assert (tran_index >= 0 && (size_t) tran_index < m_active_mvccids_size);
  m_active_mvccids[tran_index].mvccid.store (mvccid, std::memory_order_release);
}

void
mvcctable::publish_sub_mvccid (int tran_index, MVCCID sub_mvccid)
{
  assert (tran_index >= 0 && (size_t) tran_index < m_active_mvccids_size);
  mvcc_active_slot &slot = m_active_mvccids[tran_index];
  int n = slot.n_subids.load (std::memory_order_relaxed);
  if (n < mvcc_active_slot::MAX_CACHED_SUBIDS)
    {
      slot.subids[n].store (sub_mvccid, std::memory_order_relaxed);
      slot.n_subids.store (n + 1, std::memory_order_release);
    }
  else
    {
      // Cache full.  There is no runtime fallback: the snapshot scanner asserts (!overflow) in
      // debug builds and silently omits the excess sub-ids in release.  This is
      // correctness-safe: every overflowed sub-id is strictly greater than its already-published
      // parent id, which is itself > m_last_completed_mvccid; therefore every omitted sub-id
      // satisfies id >= xmax and is classified active by that boundary alone, without needing to
      // appear in xip.  The assert is a debug guard for the expected shallow (~1) instant-lock
      // sub depth; overflow is not expected in production.
      slot.subid_overflow.store (true, std::memory_order_release);
    }
}

void
mvcctable::retire_sub_mvccid (int tran_index, MVCCID sub_mvccid)
{
  assert (tran_index >= 0 && (size_t) tran_index < m_active_mvccids_size);
  // A completed sub becomes visible to others (its rows are no longer hidden by the sub id),
  // so drop it from the active cache. sub_ids is a strict stack, so the completing sub is the
  // most-recently published (top). The slot is single-writer (this tran_index); no lock needed.
  mvcc_active_slot &slot = m_active_mvccids[tran_index];
  int n = slot.n_subids.load (std::memory_order_relaxed);
  if (n > 0 && slot.subids[n - 1].load (std::memory_order_relaxed) == sub_mvccid)
    {
      slot.n_subids.store (n - 1, std::memory_order_release);
      if (n - 1 == 0)
	{
	  slot.subid_overflow.store (false, std::memory_order_relaxed);
	}
    }
  else
    {
      // defensive: not at top (or overflowed) -> linear compact.
      //
      // Seqlock safety of this branch: a concurrent snapshot reader can observe the subids[]
      // shifts mid-flight with v1==v2 (single-trailing-bump seqlock; counter not yet bumped).
      // This is safe for three reasons:
      //   1. Each array element is a full-width UINT64 atomic; every load is untearable.
      //   2. Every value visible to a mid-shift reader is a real sub-MVCCID of THIS same
      //      transaction — either the one being retired (still briefly in the array) or a
      //      neighbour that has been shifted.  Reading a sub-id as "active" is conservative.
      //   3. All such sub-ids are > m_last_completed_mvccid (a sub-id is always greater than
      //      its already-published parent and greater than last_completed), so they are >= xmax
      //      and classified active by that boundary even without appearing in xip.
      //   std::sort + std::unique in build_mvcc_info deduplicates any value seen twice during
      //   the shift, so no phantom duplicate can violate snapshot isolation.
      bool found = false;
      for (int i = 0; i < n; i++)
	{
	  if (!found && slot.subids[i].load (std::memory_order_relaxed) == sub_mvccid)
	    {
	      found = true;
	    }
	  if (found && i + 1 < n)
	    {
	      slot.subids[i].store (slot.subids[i + 1].load (std::memory_order_relaxed), std::memory_order_relaxed);
	    }
	}
      if (found)
	{
	  slot.n_subids.store (n - 1, std::memory_order_release);
	}
      if (slot.n_subids.load (std::memory_order_relaxed) == 0)
	{
	  slot.subid_overflow.store (false, std::memory_order_relaxed);
	}
    }
  // CBRD-26971: lock-free seqlock readers observe m_completion_count with acquire, so the version
  // bump must be release (LAST) and last_completed must advance with release, otherwise a reader
  // seeing the new count may not yet see this sub's n_subids decrement.
  MVCCID cur = m_last_completed_mvccid.load (std::memory_order_relaxed);
  while (MVCC_ID_PRECEDES (cur, sub_mvccid)
	 && !m_last_completed_mvccid.compare_exchange_weak (cur, sub_mvccid, std::memory_order_release,
	     std::memory_order_relaxed))
    {
      /* cur reloaded; retry */
    }
  m_completion_count.fetch_add (1, std::memory_order_release);
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
  // Called at boot/recovery (single-threaded) after log_Gl.hdr.mvcc_next_id is set. In the slot
  // model there is no active transaction yet, so just align the global trackers: oldest-active and
  // xmax boundary point at the next id to be allocated (everything below is already completed).
  m_current_status_lowest_active_mvccid.store (log_Gl.hdr.mvcc_next_id);
  m_last_completed_mvccid.store (log_Gl.hdr.mvcc_next_id - 1);
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
