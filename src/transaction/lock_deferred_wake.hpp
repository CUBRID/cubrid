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
// lock_deferred_wake - a thread's part in the deferred wake of self-lock waiters
//
//    An undo can remove the stamps other writers wait on through our MVCCID self-lock; their wake is deferred to
//    the end of our next request that writes a row (lock_deferred_wake_mark ()). A thread keeps here what one
//    self-lock wait and what one request need for that. Only lock_manager.c reads and writes it.
//

#ifndef _LOCK_DEFERRED_WAKE_HPP_
#define _LOCK_DEFERRED_WAKE_HPP_

#include "system.h"		// for UINT64, INT64

struct lk_entry;

struct lk_thread_deferred_wake
{
  /* the self-lock wait a recheck cut short; see lock_wait_transaction_mvccid () */
  struct recheck_record
  {
    UINT64 mvccid;		/* the owner's MVCCID, or 0 */
    int trid;			/* the transaction that waited */
    int wait_msecs;		/* the lock timeout it waited with */
    INT64 waited_msecs;		/* how long it has waited on that owner, in all */
  };

  struct request_state
  {
    recheck_record recheck;
    UINT64 epoch_seen;		/* the deferred wake due at a row write of this request, or 0 */
  };

  /* around one wait: the waker reads held_row under our thread mutex, a granter reads prequeued under the
   * resource mutex */
  UINT64 wake_gen;		/* lock_get_tran_wake_generation () the waiter read before its stamp */
  UINT64 held_row;		/* packed OID of the row held X while waiting on a self-lock, or 0 */
  lk_entry *prequeued;		/* our lock entry queued but not asleep yet (lock_ask_row_hand ()) */

  /* one request: lock_reset_request_state () clears it */
  request_state request;
};

#endif // _LOCK_DEFERRED_WAKE_HPP_
