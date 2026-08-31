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

//
// Transient row lock bookkeeping for the delete/update force phase
//
// A statement that scans its targets without a row lock takes one at the force phase and gives it up
// before commit.  Two questions about that release live here: which rows this statement still holds
// locks on, and whether a class is under an online index build and must keep them to commit.
//

#ifndef _QUERY_TRANSIENT_ROW_LOCK_HPP_
#define _QUERY_TRANSIENT_ROW_LOCK_HPP_

#include "oid.h"
#include "thread_compat.hpp"

/*
 * transient_row_locks - rows a statement published under the transient row lock
 *
 * Note: the lock spans the statement, not the row.  Releasing it as each row is published leaves that
 *	 row unlocked while the same statement forces the next one, and a second writer arriving in that
 *	 window works from a heap version whose index key this statement has already removed.  The lock
 *	 still ends before commit, so the uncommitted write-lock footprint stays O(transactions).
 */
typedef struct transient_row_locks TRANSIENT_ROW_LOCKS;
struct transient_row_locks
{
  OID *oids;			/* rows to unlock */
  OID *class_oids;		/* the class each row was locked under */
  int count;
  int capacity;
};

#define TRANSIENT_ROW_LOCKS_INITIALIZER { NULL, NULL, 0, 0 }

extern int transient_row_locks_add (THREAD_ENTRY *thread_p, TRANSIENT_ROW_LOCKS *locks, const OID *oid,
				    const OID *class_oid);
extern void transient_row_locks_release (THREAD_ENTRY *thread_p, TRANSIENT_ROW_LOCKS *locks);
extern void transient_row_locks_clear (THREAD_ENTRY *thread_p, TRANSIENT_ROW_LOCKS *locks);
extern bool transient_row_locks_class_has_online_index (THREAD_ENTRY *thread_p, const OID *class_oid);

#endif /* _QUERY_TRANSIENT_ROW_LOCK_HPP_ */
