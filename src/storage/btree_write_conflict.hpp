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
// Write-write conflict resolution for a b-tree key probe
//

#ifndef _BTREE_WRITE_CONFLICT_HPP_
#define _BTREE_WRITE_CONFLICT_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "lock_manager.h"
#include "mvcc.h"
#include "oid.h"
#include "perf_monitor.h"
#include "storage_common.h"
#include "thread_compat.hpp"

/* BTREE_FIND_UNIQUE_HELPER -
 * Structure used by find unique functions.
 *
 * Functions:
 * btree_key_find_unique_version_oid.
 * btree_key_find_and_lock_unique.
 * btree_key_find_and_lock_unique_of_unique.
 * btree_key_find_and_lock_unique_of_non_unique.
 */
typedef struct btree_find_unique_helper BTREE_FIND_UNIQUE_HELPER;
struct btree_find_unique_helper
{
  OID oid;			/* OID of found object (if found). */
  OID match_class_oid;		/* Object is only considered if its class OID matches this class OID. */
  LOCK lock_mode;		/* Lock mode for found unique object. */
  MVCC_SNAPSHOT *snapshot;	/* Snapshot used to filter objects not visible. If NULL, objects are not filtered. */
  bool found_object;		/* Set to true if object was found. */

  PERF_UTIME_TRACKER time_track;

#if defined (SERVER_MODE)
  OID locked_oid;		/* Locked object. */
  OID locked_class_oid;		/* Locked object class OID. */
#endif				/* SERVER_MODE */
};
/* BTREE_FIND_UNIQUE_HELPER static initializer. */
#if defined (SERVER_MODE)
#define BTREE_FIND_UNIQUE_HELPER_INITIALIZER \
  { OID_INITIALIZER, /* oid */ \
    OID_INITIALIZER, /* match_class_oid */ \
    NULL_LOCK, /* lock_mode */ \
    NULL, /* snapshot */ \
    false, /* found_object */ \
    PERF_UTIME_TRACKER_INITIALIZER, /* time_track */ \
    OID_INITIALIZER, /* locked_oid */ \
    OID_INITIALIZER /* locked_class_oid */ \
  }
#else	/* !SERVER_MODE */		   /* SA_MODE */
#define BTREE_FIND_UNIQUE_HELPER_INITIALIZER \
  { OID_INITIALIZER, /* oid */ \
    OID_INITIALIZER, /* match_class_oid */ \
    NULL_LOCK, /* lock_mode */ \
    NULL, /* snapshot */ \
    false, /* found_object */ \
    PERF_UTIME_TRACKER_INITIALIZER /* time_track */ \
  }
#endif /* !SA_MODE */

#if defined (SERVER_MODE)
extern int btree_key_wait_out_conflicting_writer (THREAD_ENTRY *thread_p,
    MVCC_SATISFIES_DELETE_RESULT satisfies_delete,
    MVCC_REC_HEADER *mvcc_header,
    BTREE_FIND_UNIQUE_HELPER *find_unique_helper,
    PAGE_PTR *leaf_page, PAGE_PTR *overflow_page, bool *restart);
#endif /* SERVER_MODE */

#endif // _BTREE_WRITE_CONFLICT_HPP_
