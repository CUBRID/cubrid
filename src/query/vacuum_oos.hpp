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
 *   Reclaims OOS records that became unreachable from the heap: the REMOVE path deletes the OOS
 *   referenced by a vacuumed record, and the forward-walk path deletes the OOS referenced only by
 *   UPDATE/DELETE_NEWHOME pre-images found in the undo log.
 */

#ifndef _VACUUM_OOS_HPP_
#define _VACUUM_OOS_HPP_

#include "storage_common.h"
#include "thread_compat.hpp"

#include <vector>

/* Pages that lost OOS chunks during one delete batch (duplicates allowed). Feeds
 * vacuum_oos_reclaim_empty_pages after the batch's sysop commits. A plain typedef so vacuum.c
 * (GNU-indent formatted) can declare one without exposing template syntax to the formatter. */
typedef std::vector<VPID> VACUUM_OOS_TOUCHED_PAGES;

/* Forward-walk OOS reclamation helpers. */

/* Single-slot memo mapping the most-recently-resolved heap-VFID -> OOS-VFID, used only by the
 * forward-walk path. A bulk UPDATE emits a run of consecutive RVHF_UPDATE_NOTIFY_VACUUM records for
 * the same heap, so a one-entry memo elides the repeat file_descriptor_get + heap_oos_find_vfid page
 * fixes across that run. Declared on the stack in vacuum_process_log_block => per-worker-per-block,
 * so the lookup needs no synchronization. Do NOT replace with a static — vacuum runs across multiple
 * worker threads and a shared static would race. `valid` is false until the first successful lookup;
 * once set, oos_vfid may itself be VFID_NULL, meaning the heap legitimately has no OOS file.
 * Transient lookup failures are never memoized, so a later record retries cleanly. */
typedef struct vacuum_oos_vfid_memo VACUUM_OOS_VFID_MEMO;
struct vacuum_oos_vfid_memo
{
  bool valid = false;		/* false until the first successful lookup */
  VFID heap_vfid;		/* key; meaningful only when valid */
  VFID oos_vfid;		/* value; meaningful only when valid (VFID_NULL = "no OOS file") */
};

extern void vacuum_forward_walk_reclaim_oos (THREAD_ENTRY *thread_p, char *undo_data, int undo_data_size,
    const VFID *heap_vfid, VACUUM_OOS_VFID_MEMO *oos_vfid_memo);
extern int vacuum_oos_find_vfid_for_heap_record (THREAD_ENTRY *thread_p, const HFID *hfid, const RECDES *record,
    PGSLOTID slotid, INT16 record_type, VFID *oos_vfid);
extern int vacuum_heap_oos_delete_within_sysop (THREAD_ENTRY *thread_p, const VFID *oos_vfid, const RECDES *record,
    VACUUM_OOS_TOUCHED_PAGES *touched_pages_out = NULL);
extern void vacuum_oos_reclaim_empty_pages (THREAD_ENTRY *thread_p, const VFID *oos_vfid,
    VACUUM_OOS_TOUCHED_PAGES *touched_pages);

#endif /* _VACUUM_OOS_HPP_ */
