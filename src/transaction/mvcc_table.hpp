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

#ifndef _MVCC_TABLE_H_
#define _MVCC_TABLE_H_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Wrong Module
#endif

#include "mvcc_active_tran.hpp"
#include "storage_common.h"

#include <atomic>
#include <cstdint>
#include <mutex>

// forward declarations
struct log_tdes;
struct mvcc_info;

// PG-style per-transaction active-MVCCID slot (indexed by tran_index).
// Holds the transaction's top/parent active MVCCID plus a small cache of active
// sub-transaction MVCCIDs (SELECT..UPDATE instant locks). Written lock-free on publish
// (release-store) by the owning transaction; cleared lock-free on completion.
// Read by snapshot scanners without any lock (CBRD-26971).
struct mvcc_active_slot
{
  static const int MAX_CACHED_SUBIDS = 8;
  std::atomic<MVCCID> mvccid;                     // parent/top active MVCCID, MVCCID_NULL if inactive
  std::atomic<int> n_subids;                      // # of valid entries in subids[]
  // Set when more active subs exist than MAX_CACHED_SUBIDS.  There is no runtime fallback;
  // omitted sub-ids are safe because each is > its parent (> last_completed), so >= xmax,
  // and is classified active by the xmax boundary without needing to appear in xip.
  std::atomic<bool> subid_overflow;
  std::atomic<MVCCID> subids[MAX_CACHED_SUBIDS];  // active sub-transaction MVCCIDs
};

class mvcctable
{
  public:
    using lowest_active_mvccid_type = std::atomic<MVCCID>;

    mvcctable ();
    ~mvcctable ();

    void initialize ();
    void finalize ();

    void alloc_transaction_lowest_active ();
    void reset_transaction_lowest_active (int tran_index);

    // mvcc_snapshot/mvcc_info functions
    void build_mvcc_info (log_tdes &tdes);
    void complete_mvcc (int tran_index, MVCCID mvccid, bool committed);
    void complete_sub_mvcc (MVCCID mvccid);
    MVCCID get_new_mvccid ();
    void get_two_new_mvccid (MVCCID &first, MVCCID &second);

    // ProcArray Phase 1: slot publish (lockless, publish-before-stamp) / sub-retire.
    void publish_active_mvccid (int tran_index, MVCCID mvccid);
    void publish_sub_mvccid (int tran_index, MVCCID sub_mvccid);
    void retire_sub_mvccid (int tran_index, MVCCID sub_mvccid);

    bool is_active (MVCCID mvccid) const;

    void reset_start_mvccid ();     // not thread safe

    MVCCID get_global_oldest_visible () const;
    MVCCID update_global_oldest_visible ();
    void lock_global_oldest_visible ();
    void unlock_global_oldest_visible ();
    bool is_global_oldest_visible_locked () const;

  private:

    /* lowest active MVCCIDs - array of size NUM_TOTAL_TRAN_INDICES */
    lowest_active_mvccid_type *m_transaction_lowest_visible_mvccids;
    size_t m_transaction_lowest_visible_mvccids_size;
    /* lowest active MVCCID */
    lowest_active_mvccid_type m_current_status_lowest_active_mvccid;

    /* protect against getting new MVCCIDs concurrently */
    std::mutex m_new_mvccid_lock;     // theoretically, it may be replaced with atomic operations

    std::atomic<MVCCID> m_oldest_visible;
    std::atomic<size_t> m_ov_lock_count;

    // --- ProcArray active-tran tracking (CBRD-26971). Replaces the former global bitmap +
    // history ring + m_active_trans_mutex. ---
    // Per-tran-index slots (parent + sub cache).
    mvcc_active_slot *m_active_mvccids;
    size_t m_active_mvccids_size;
    // Global "most recent completed MVCCID"; snapshot xmax = this + 1 (PG latestCompletedXid).
    std::atomic<MVCCID> m_last_completed_mvccid;
    // Bumped with release ordering, LAST, on every completion (commit/rollback/sub completion).
    // No lock is held when it is bumped; it is the seqlock version counter for the slot scan
    // (build_mvcc_info / is_active) and the Phase-2 snapshot-reuse cache key.
    std::atomic<std::uint64_t> m_completion_count;

    void advance_oldest_active (MVCCID next_oldest_active);
    MVCCID compute_oldest_visible_mvccid () const;
    // CBRD-26971 Phase 1: lowest active MVCCID from a lock-free ProcArray slot scan
    // (a valid lower bound; advance_oldest_active is monotonic so concurrent scans are safe).
    MVCCID compute_lowest_active_from_slots () const;
    // CBRD-26971: apply one commit-clear lock-free (single-writer slot; orders the slot clear +
    // last_completed advance before the m_completion_count seqlock-version bump).
    void apply_clear (int tran_index, MVCCID mvccid);
};

#endif // !_MVCC_TABLE_H_
