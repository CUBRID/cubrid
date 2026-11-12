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

/* Writeset ordering uses commit LSAs, not integer sequence numbers.
 * commit_lsa is a transaction's commit record location. dependency_lsa is the commit LSA that must
 * complete before an apply task can run. write_lsa and ref_lsa retain the last writer's and
 * referrer's commit LSAs for a key. A label carries dependency_lsa from master to slave.
 * The coordinator's dispatch sequence is a separate integer used to track apply progress. */

/* FNV-1a hash of one key = class OID (8 bytes) + the index VFID (6 bytes) + its packed key value.
 * The index VFID distinguishes two indexes of the same class that pack the same key bytes (for
 * example a primary key and a unique key holding equal values); a child foreign-key reference folds
 * the parent primary key's VFID so the reference and the parent's own write meet in one slot. A
 * modified row yields a separate hash for its primary key and for each unique key, so every distinct
 * key a transaction touched contributes one hash. */
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
  WSET_FALLBACK_NAN_KEY = 2
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

#endif /* _WRITESET_TYPES_HPP_ */
