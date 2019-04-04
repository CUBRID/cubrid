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
#include <cstdint>

// forward declarations
struct log_tdes;
struct mvcc_snapshot;
struct mvcc_info;

/*
 * MVCC_TRANS_STATUS keep MVCCIDs status in bit area. Thus bit 0 means active
 * MVCCID bit 1 means committed transaction. This structure keep also lowest
 * active MVCCIDs used by VACUUM for MVCCID threshold computation. Also, MVCCIDs
 * of long time transactions MVCCIDs are kept in this structure.
 */
typedef struct mvcc_trans_status MVCC_TRANS_STATUS;
struct mvcc_trans_status
{
  using version_type = unsigned int;

  mvcc_active_tran m_active_mvccs;

  std::atomic<version_type> version;

  mvcc_trans_status ();
  ~mvcc_trans_status ();

  void initialize ();
  void finalize ();
};

typedef struct mvcctable MVCCTABLE;
struct mvcctable
{
  public:
    using lowest_active_mvccid_type = std::atomic<MVCCID>;

    static const size_t HISTORY_MAX_SIZE = 2048;  // must be a power of 2

    /* current transaction status */
    mvcc_trans_status current_trans_status;

    /* lowest active MVCCIDs - array of size NUM_TOTAL_TRAN_INDICES */
    lowest_active_mvccid_type *transaction_lowest_active_mvccids;
    size_t transaction_lowest_active_mvccids_size;

    /* transaction status history - array of size TRANS_STATUS_HISTORY_MAX_SIZE */
    mvcc_trans_status *trans_status_history;
    /* the position in transaction status history array */
    std::atomic<size_t> trans_status_history_position;   // protected by lock

    /* lowest active MVCCID */
    std::atomic<MVCCID> m_lowest_active_mvccid;

    /* protect against getting new MVCCIDs concurrently */
    std::mutex new_mvccid_lock;
    /* protect against current transaction status modifications */
    std::mutex active_trans_mutex;

    mvcctable ();
    ~mvcctable ();

    void initialize ();
    void finalize ();

    void alloc_transaction_lowest_active ();
    void set_transaction_lowest_active (int tran_index, MVCCID mvccid);

    // mvcc_snapshot/mvcc_info functions
    void build_mvcc_info (log_tdes &tdes);
    bool is_active (MVCCID mvccid) const;
    void complete_mvcc (int tran_index, MVCCID mvccid, bool commited);
    void complete_sub_mvcc (MVCCID mvccid);
    MVCCID get_new_mvccid ();
    void get_two_new_mvccid (MVCCID &first, MVCCID &second);
    MVCCID get_oldest_active_mvccid () const;

    void reset_start_mvccid ();

  private:
    static const size_t HISTORY_INDEX_MASK = HISTORY_MAX_SIZE - 1;

    mvcc_trans_status &next_trans_status_start ();
    void next_tran_status_finish (mvcc_trans_status &next);
    void advance_oldest_active (MVCCID next_oldest_active);
};

#endif // !_MVCC_TABLE_H_
