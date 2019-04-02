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

#include "storage_common.h"

#include <atomic>
#include <cstdint>

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
  using bit_area_unit_type = std::uint64_t;

  /* bit area to store MVCCIDS status - size MVCC_BITAREA_MAXIMUM_ELEMENTS */
  bit_area_unit_type *bit_area;
  /* first MVCCID whose status is stored in bit area */
  MVCCID bit_area_start_mvccid;
  /* the area length expressed in bits */
  unsigned int bit_area_length;

  /* long time transaction mvccid array */
  MVCCID *long_tran_mvccids;
  /* long time transactions mvccid array length */
  unsigned int long_tran_mvccids_length;

  volatile version_type version;

  /* lowest active MVCCID */
  MVCCID lowest_active_mvccid;

  mvcc_trans_status ();
  ~mvcc_trans_status ();

  void initialize ();
  void finalize ();
};

typedef struct mvcctable MVCCTABLE;
struct mvcctable
{
  /* current transaction status */
  mvcc_trans_status current_trans_status;

  /* lowest active MVCCIDs - array of size NUM_TOTAL_TRAN_INDICES */
  volatile MVCCID *transaction_lowest_active_mvccids;

  /* transaction status history - array of size TRANS_STATUS_HISTORY_MAX_SIZE */
  mvcc_trans_status *trans_status_history;
  /* the position in transaction status history array */
  volatile int trans_status_history_position;

  /* protect against getting new MVCCIDs concurrently */
  std::mutex new_mvccid_lock;
  /* protect against current transaction status modifications */
  std::mutex active_trans_mutex;

  mvcctable ();
  ~mvcctable ();

  void initialize ();
  void finalize ();
};

#endif // !_MVCC_TABLE_H_
