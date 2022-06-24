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
#include <mutex>

// forward declarations
struct log_tdes;
struct mvcc_info;

struct mvcc_trans_status
{
  using version_type = unsigned int;

  enum event_type
  {
    COMMIT,
    ROLLBACK,
    SUBTRAN
  };

  mvcc_active_tran m_active_mvccs;

  MVCCID m_last_completed_mvccid;   // just for info
  event_type m_event_type;          // just for info
  std::atomic<version_type> m_version;

  mvcc_trans_status ();
  ~mvcc_trans_status ();

  void initialize ();
  void finalize ();
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
    // update next_mvcc_id value with one received from ATS if it's larger than the current one
    void set_mvccid_from_active_transaction_server (MVCCID id);

    bool is_active (MVCCID mvccid) const;

    void reset_start_mvccid ();     // not thread safe

    MVCCID get_global_oldest_visible () const;
    MVCCID update_global_oldest_visible ();
    void lock_global_oldest_visible ();
    void unlock_global_oldest_visible ();
    bool is_global_oldest_visible_locked () const;

  private:
    static constexpr size_t HISTORY_MAX_SIZE = 2048;  // must be a power of 2
    static constexpr size_t HISTORY_INDEX_MASK = HISTORY_MAX_SIZE - 1;

    /* lowest active MVCCIDs - array of size NUM_TOTAL_TRAN_INDICES */
    lowest_active_mvccid_type *m_transaction_lowest_visible_mvccids;
    size_t m_transaction_lowest_visible_mvccids_size;
    /* lowest active MVCCID */
    lowest_active_mvccid_type m_current_status_lowest_active_mvccid;

    /* current transaction status */
    mvcc_trans_status m_current_trans_status;
    /* transaction status history - array of size TRANS_STATUS_HISTORY_MAX_SIZE */
    /* the position in transaction status history array */
    std::atomic<size_t> m_trans_status_history_position;
    mvcc_trans_status *m_trans_status_history;

    /* protect against getting new MVCCIDs concurrently */
    std::mutex m_new_mvccid_lock;     // theoretically, it may be replaced with atomic operations
    /* protect against current transaction status modifications */
    std::mutex m_active_trans_mutex;

    std::atomic<MVCCID> m_oldest_visible;
    std::atomic<size_t> m_ov_lock_count;

    mvcc_trans_status &next_trans_status_start (mvcc_trans_status::version_type &next_version, size_t &next_index);
    void next_tran_status_finish (mvcc_trans_status &next_trans_status, size_t next_index);
    void advance_oldest_active (MVCCID next_oldest_active);
    MVCCID compute_oldest_visible_mvccid () const;
};

#endif // !_MVCC_TABLE_H_
