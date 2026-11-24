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

/*
 * writeset_types.hpp - common writeset key types
 */

#ifndef _WRITESET_TYPES_HPP_
#define _WRITESET_TYPES_HPP_

#ident "$Id$"

#include "storage_common.h"
#include "log_lsa.hpp"

/* Common writeset and parallel-apply terms:
 * Apply unit: replicated changes ending at LOG_COMMIT or an independently applied LOG_SYSOP_END.
 * commit_lsa: the unit's ending log position; ordering uses LSAs, not integer sequence numbers.
 * dependency_lsa: the position through which earlier apply units must complete before this unit executes.
 * write_lsa/ref_lsa: the latest commit_lsa published to a key's WRITE/REF slot.
 * Label: LOG_DUMMY_WSET_LABEL, carrying the dependency_lsa and an optional independent-unit key hash.
 * SYSOP fragment: replication items separated from the outer transaction at an independent SYSOP_END.
 * Task/result: a worker submission and its outcome; a task may include merged SYSOP fragments.
 * dispatch_seq: the integer assigned at dispatch to match a result with its in-flight entry for retire.
 * Frontier: the latest contiguously completed apply-unit position, used to test dependencies.
 * Commit order: ordering a unit after all earlier units; this policy also applies to some SYSOPs.
 * Fallback: abandoning transaction-wide writeset ordering in favor of commit order. wset_fallback is
 * the state; WSET_FALLBACK_REASON identifies its cause, and fallback_* counters report it. Functions
 * named *_commit_order perform the ordering policy, and trace mode=COMMIT_ORDER identifies that policy.
 * A SYSOP's commit-order policy does not by itself put its outer transaction into fallback. */

/* Key identity is the index VFID plus the key normalized to the index's equality rules.
 * Class OID is excluded because inherited PK/UNIQUE constraints share one B-tree. Including it would
 * give the same index key different hashes across classes and miss their dependency.
 * An FK REF uses the parent primary key's VFID and domain to meet the parent's WRITE in the same history slot. */
typedef UINT64 LOG_WSET_HASH;

/* A collected key is either written or referenced by this transaction.
 * WRITE probes the previous WRITE and REF slots, then publishes to write_lsa.
 * REF probes only the previous WRITE slot, then publishes to ref_lsa. Since a later REF does not
 * probe ref_lsa, sibling children that reference the same parent do not wait for one another. */
typedef enum
{
  LOG_WSET_KIND_WRITE = 0,
  LOG_WSET_KIND_REF = 1
} LOG_WSET_KIND;

typedef enum
{
  WSET_FALLBACK_TRANSACTION_LIMIT = 0,
  WSET_FALLBACK_STATEMENT_REPLICATION = 1,
  WSET_FALLBACK_NAN_KEY = 2,
  WSET_FALLBACK_KEY_UNAVAILABLE = 3
} WSET_FALLBACK_REASON;

typedef struct wset_entry LOG_WSET_ENTRY;
struct wset_entry
{
  LOG_WSET_HASH hash;
  LOG_WSET_KIND kind;
  LOG_LSA lsa;			/* the transaction's last log record when the key was collected; a key collected
				 * after a system operation started lies after its parent LSA, which is how
				 * wset_discard_keys_after_lsa finds the keys of an aborted operation */
};

/* Advance a boundary to a non-NULL candidate without moving it backward. The caller supplies any required lock. */
inline void
wset_advance_lsa (LOG_LSA *lsa, const LOG_LSA *candidate)
{
  assert (lsa != NULL);
  assert (candidate != NULL);

  if (!LSA_ISNULL (candidate) && (LSA_ISNULL (lsa) || LSA_LT (lsa, candidate)))
    {
      LSA_COPY (lsa, candidate);
    }
}

#endif /* _WRITESET_TYPES_HPP_ */
