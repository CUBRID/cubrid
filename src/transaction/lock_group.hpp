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
 * lock_group.hpp - the entrance of a transaction's threads to the lock manager, for lock_manager.c
 */

#ifndef _LOCK_GROUP_HPP_
#define _LOCK_GROUP_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif

#if defined (SERVER_MODE)

#include "lock_manager.h"
#include "thread_compat.hpp"

#include <pthread.h>

/* the releases called with a page fixed, which a thread hands to the entrance owner asleep (lock_group_guard) */
typedef enum
{
  LK_PENDING_UNLOCK,		/* lock_unlock_object () */
  LK_PENDING_UNLOCK_DONOT_MOVE,	/* lock_unlock_object_donot_move_to_non2pl () */
  LK_PENDING_UNLOCK_TRANSIENT	/* lock_unlock_object_transient () */
} LK_PENDING_RELEASE_TYPE;

typedef struct lk_pending_release LK_PENDING_RELEASE;
struct lk_pending_release
{
  LK_PENDING_RELEASE *next;
  LK_PENDING_RELEASE_TYPE type;
  OID oid;
  OID class_oid;		/* NULL OID for none */
  LOCK lock;
  bool force;
};

/* the clock of LK_GROUP.cond's timed waits: a wall clock set back would stretch them */
#if defined (WINDOWS)
#define LK_GROUP_COND_CLOCK CLOCK_REALTIME
#else
#define LK_GROUP_COND_CLOCK CLOCK_MONOTONIC
#endif

/*
 * The threads of one transaction in the lock manager. While a parallel section of the transaction is open, they
 * enter it one at a time (lock_group_guard), so the transaction's lock state keeps a single writer.
 */
typedef struct lk_group LK_GROUP;
struct lk_group
{
  int tran_index;		/* the owning unit: locks its threads take belong to this transaction */
  int sections;			/* open parallel sections (__atomic) */
  /* written under the mutex, but owner and the depths; owner_sleeping, owner_wait_key and owner_held_row are also read
   * only under it, guest outside it only to see the reader's own role and in debug checks. No lock is taken in it. */
  pthread_mutex_t mutex;
  pthread_cond_t cond;		/* the entrance freed, the owner asleep, or an abort for the waiters to see */
  THREAD_ENTRY *owner;		/* the thread inside: taken by a CAS from NULL, given back with no mutex (__atomic) */
  int depth;			/* the owner's re-entries; only the owner touches it */
  THREAD_ENTRY *guest;		/* a second thread let in while the owner sleeps (lock_group_guard; __atomic) */
  int guest_depth;		/* the guest's re-entries; only the guest touches it */
  int waiters;			/* threads in a timed wait (__atomic: an owner leaving reads it with no mutex) */
  bool owner_sleeping;		/* the owner waits for another transaction's lock: a guest may come in */
  bool owner_blocked;		/* owner_sleeping, or since waiting for its guest to leave: nothing else may wait
				 * (__atomic: the fast path reads it with no mutex) */
  LK_RES_KEY owner_wait_key;	/* the resource it waits for, while owner_sleeping */
  UINT64 owner_held_row;	/* the row it holds while it waits on a self-lock (deferred_wake.held_row), or 0 */
  bool interrupted;		/* an interrupt came while sections were open; cleared when they close (__atomic) */
  bool abort_deferred;		/* an abort waits for the section owner or a PL callback's caller; cleared when the
				 * transaction ends (__atomic) */
  LK_PENDING_RELEASE *pending_release;	/* run by the owner before it leaves (__atomic: read leaving with no mutex) */
#if !defined (NDEBUG)
  THREAD_ENTRY *solo_owner;	/* the thread inside with no section open, to catch a second one */
  int solo_depth;
#endif
};

typedef enum
{
  LK_GROUP_ACQUIRE,		/* may sleep for another transaction */
  LK_GROUP_ACQUIRE_COND,	/* sleeps for no one */
  LK_GROUP_RELEASE		/* gives up or changes locks, however the transaction is to end */
} LK_GROUP_REQUEST;

static inline bool
lock_group_in_section (const LK_GROUP * group)
{
  return __atomic_load_n (&group->sections, __ATOMIC_ACQUIRE) > 0;
}

static inline LK_GROUP_REQUEST
lock_group_acquire_request (int cond_flag)
{
  return (cond_flag == LK_COND_LOCK) ? LK_GROUP_ACQUIRE_COND : LK_GROUP_ACQUIRE;
}

class lock_group_guard
{
  public:
    /* guest_key: the resource, for a request that may enter as the guest; release: a release that may be handed */
    lock_group_guard (THREAD_ENTRY *thread_p, int tran_index, LK_GROUP_REQUEST request,
		      const LK_RES_KEY *guest_key = NULL, const LK_PENDING_RELEASE *release = NULL);
    ~lock_group_guard ();

    /* false also for a release handed to the owner, which runs it before it leaves */
    bool admitted () const
    {
      return m_lock_result == LK_GRANTED;
    }
    /* why an acquisition was not admitted: LK_NOTGRANTED_DUE_ABORTED or _ERROR with the error set, or
     * LK_NOTGRANTED_DUE_TIMEOUT with none for a conditional one that met the owner asleep */
    int lock_result () const
    {
      return m_lock_result;
    }

  private:
    enum state
    {
      NOT_NEEDED,
      SOLO,
      ENTERED
    };

    LK_GROUP *m_group;
    THREAD_ENTRY *m_thread_p;
    state m_state;
    int m_lock_result;
};

extern void lock_group_init_cond (LK_GROUP * group);
extern int lock_group_abort_reason (const LK_GROUP * group);
extern void lock_group_set_abort_error (int tran_index, int error);
extern void lock_pending_release_init (LK_PENDING_RELEASE * release, LK_PENDING_RELEASE_TYPE type, const OID * oid,
				       const OID * class_oid, LOCK lock, bool force);
extern const LK_RES_KEY *lock_group_dir_oid_key (const OID * oid, const OID * class_oid, LK_RES_KEY * key);
extern const LK_RES_KEY *lock_group_row_release_key (const OID * oid, const OID * class_oid, LK_RES_KEY * key);
extern void lock_group_keep_interrupt (LK_GROUP * group);
extern void lock_group_wait_for_guest (LK_GROUP * group);
extern int lock_group_step_out (LK_GROUP * group, THREAD_ENTRY * thread_p);
extern void lock_group_step_back (LK_GROUP * group, THREAD_ENTRY * thread_p, int depth);
extern void lock_group_start_wait (LK_GROUP * group, bool in_group, INT64 stime);
extern void lock_group_end_wait (LK_GROUP * group, bool in_group, const THREAD_ENTRY * thread_p);
extern bool lock_group_holds_interrupt (int tran_index);
extern void lock_group_owner_sleeps (LK_GROUP * group, const LK_RES_KEY * wait_key, UINT64 held_row);
extern void lock_group_wakes (LK_GROUP * group, bool is_owner, bool interrupted);
#if !defined (NDEBUG)
extern int lock_group_solo_step_out (LK_GROUP * group, THREAD_ENTRY * thread_p);
extern void lock_group_solo_step_back (LK_GROUP * group, THREAD_ENTRY * thread_p, int depth);
extern bool lock_group_is_alone (THREAD_ENTRY * thread_p, int tran_index);
#endif

/* from lock_manager.c */
extern LK_GROUP *lock_get_tran_group (int tran_index);
extern bool lock_has_tran_lock (int tran_index);
extern bool lock_is_tran_deadlock_victim (int tran_index);
extern INT64 *lock_get_wait_stime (int tran_index);
extern void lock_interrupt_lockwait (THREAD_ENTRY * guest, int tran_index);
extern void lock_set_error_for_aborted (int tran_index);
extern LK_RES_KEY lock_create_search_key (const OID * oid, const OID * class_oid);
extern UINT64 lock_pack_row_oid (const OID * oid);
extern int lock_res_key_compare (void *k1, void *k2);

#endif /* SERVER_MODE */

#endif /* _LOCK_GROUP_HPP_ */
