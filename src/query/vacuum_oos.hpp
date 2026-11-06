/*
 *
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
 * vacuum_oos.hpp - Vacuum-side OOS (Out-of-row Overflow Storage) reclamation
 *
 *   Vacuum reclaims OOS chains from two sources (ADR-0008): the REMOVE path records the chains of a
 *   vacuumed record as a reclaim intent in its sysop, and the RVOOS_NOTIFY_VACUUM consumer reclaims the
 *   chains a committed UPDATE dropped once no snapshot can read the old version.
 */

#ifndef _VACUUM_OOS_HPP_
#define _VACUUM_OOS_HPP_

#include "storage_common.h"
#include "thread_compat.hpp"

#include <vector>

/* Pages that one delete batch left with zero OOS records, each once. A plain typedef so
 * vacuum.c (GNU-indent formatted) can declare one without exposing template syntax. */
typedef std::vector<VPID> VACUUM_OOS_EMPTIED_PAGES;

extern int vacuum_oos_find_vfid_for_heap_record (THREAD_ENTRY *thread_p, const HFID *hfid, const RECDES *record,
    PGSLOTID slotid, INT16 record_type, VFID *oos_vfid);
extern int vacuum_heap_oos_log_reclaim_intent (THREAD_ENTRY *thread_p, const VFID *oos_vfid, const RECDES *record,
    const OID *owner_oid);
extern void vacuum_oos_sysop_commit_collecting_emptied_pages (THREAD_ENTRY *thread_p,
    VACUUM_OOS_EMPTIED_PAGES *emptied_pages);
extern int vacuum_oos_reclaim_notified_chains (THREAD_ENTRY *thread_p, const char *undo_data, int undo_data_size);
extern int vacuum_oos_reclaim_empty_pages (THREAD_ENTRY *thread_p, const VFID *oos_vfid,
    VACUUM_OOS_EMPTIED_PAGES *emptied_pages);

#endif /* _VACUUM_OOS_HPP_ */
