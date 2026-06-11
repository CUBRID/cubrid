/*
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
 *   UPDATE/DELETE_NEWHOME pre-images found in the undo log. See ADR-0001 and ADR-0002.
 */

#ifndef _VACUUM_OOS_HPP_
#define _VACUUM_OOS_HPP_

#include "log_lsa.hpp"
#include "storage_common.h"
#include "thread_compat.hpp"

/* Forward-walk OOS reclamation helpers. */
#define VACUUM_OOS_VFID_CACHE_SIZE 16
typedef struct vacuum_oos_vfid_cache_entry
{
  VFID heap_vfid;		/* key */
  VFID oos_vfid;		/* value; VFID_NULL sentinel means "no OOS file" */
} VACUUM_OOS_VFID_CACHE_ENTRY;

/* Per-block cache mapping heap-VFID -> OOS-VFID (VFID_NULL sentinel = "no OOS file").
 * Avoids repeated file_descriptor_get + heap_oos_find_vfid for the same heap file within one block.
 * size tracks population (capped at VACUUM_OOS_VFID_CACHE_SIZE); evict_idx is the per-block
 * round-robin cursor used once the cache is full. Declared on the stack in vacuum_process_log_block
 * => per-worker-per-block, so the lookup needs no synchronization. Do NOT replace with a static —
 * vacuum runs across multiple worker threads and a shared static would race. */
typedef struct vacuum_oos_vfid_cache VACUUM_OOS_VFID_CACHE;
struct vacuum_oos_vfid_cache
{
  VACUUM_OOS_VFID_CACHE_ENTRY entries[VACUUM_OOS_VFID_CACHE_SIZE];
  int size = 0;			/* populated entries */
  int evict_idx = 0;		/* round-robin eviction cursor used once the cache is full */
};

extern void vacuum_forward_walk_reclaim_oos (THREAD_ENTRY *thread_p, const RECDES *undo_recdes,
    const VFID *heap_vfid, VACUUM_OOS_VFID_CACHE *oos_vfid_cache);
extern int vacuum_oos_find_vfid_for_heap_record (THREAD_ENTRY *thread_p, const HFID *hfid, const RECDES *record,
    PGSLOTID slotid, INT16 record_type, VFID *oos_vfid);
extern int vacuum_heap_oos_delete (THREAD_ENTRY *thread_p, const VFID *oos_vfid, const RECDES *record);

/* Test bridge below is unconditionally built (not NDEBUG-gated) so unit_tests/oos can link in any
 * build mode — gating on NDEBUG breaks `./build.sh -m release -c -DUNIT_TESTS=ON`. */
extern void bridge_log_append_undo_for_prev_version_test (THREAD_ENTRY *thread_p, const VFID *vfid,
    const RECDES *old_recdes, LOG_LSA *out_lsa);

#endif /* _VACUUM_OOS_HPP_ */
