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

/* FNV-1a hash of one key = class OID (8 bytes) + the index VFID (6 bytes) + its packed key value.
 * The index VFID distinguishes two indexes of the same class that pack the same key bytes (for
 * example a primary key and a unique key holding equal values); a child foreign-key reference folds
 * the parent primary key's VFID so the reference and the parent's own write meet in one slot. A
 * modified row yields a separate hash for its primary key and for each unique key, so every distinct
 * key a transaction touched contributes one hash. */
typedef UINT64 LOG_WSET_HASH;

/* A collected key is either written or referenced by this transaction.
 * WRITE probes the previous WRITE and REF slots, then publishes to write_seq.
 * REF probes only the previous WRITE slot, then publishes to ref_seq. Since a later REF does not
 * probe ref_seq, sibling children that reference the same parent do not wait for one another. */
typedef enum
{
  LOG_WSET_KIND_WRITE = 0,
  LOG_WSET_KIND_REF = 1
} LOG_WSET_KIND;

typedef struct wset_entry LOG_WSET_ENTRY;
struct wset_entry
{
  LOG_WSET_HASH hash;
  LOG_WSET_KIND kind;
};

#endif /* _WRITESET_TYPES_HPP_ */
