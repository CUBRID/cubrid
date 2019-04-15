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

struct mvcctable
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

    bool is_active (MVCCID mvccid) const;
    MVCCID compute_oldest_active_mvccid () const;

    void reset_start_mvccid ();     // not thread safe

  private:

    static const size_t HISTORY_MAX_SIZE = 2048;  // must be a power of 2
    static const size_t HISTORY_INDEX_MASK = HISTORY_MAX_SIZE - 1;

    /* lowest active MVCCIDs - array of size NUM_TOTAL_TRAN_INDICES */
    lowest_active_mvccid_type *m_transaction_lowest_active_mvccids;
    size_t m_transaction_lowest_active_mvccids_size;
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

    mvcc_trans_status &next_trans_status_start (mvcc_trans_status::version_type &next_version, size_t &next_index);
    void next_tran_status_finish (mvcc_trans_status &next_trans_status, size_t next_index);
    void advance_oldest_active (MVCCID next_oldest_active);
};

#endif // !_MVCC_TABLE_H_
