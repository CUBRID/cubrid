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

#include <atomic>
#include <pthread.h>

/* the releases called with a page fixed, which a thread hands to the entrance owner asleep (lock_group::enter ()) */
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

typedef enum
{
  LK_GROUP_ACQUIRE,		/* may sleep for another transaction */
  LK_GROUP_ACQUIRE_COND,	/* sleeps for no one */
  LK_GROUP_RELEASE		/* gives up or changes locks, however the transaction is to end */
} LK_GROUP_REQUEST;

static inline LK_GROUP_REQUEST
lock_group_acquire_request (int cond_flag)
{
  return (cond_flag == LK_COND_LOCK) ? LK_GROUP_ACQUIRE_COND : LK_GROUP_ACQUIRE;
}

/*
 * The threads of one transaction in the lock manager. While a parallel section of the transaction is open, they
 * enter it one at a time (enter ()), so the transaction's lock state keeps a single writer.
 */
class lock_group
{
  public:
    explicit lock_group (int tran_index);
    ~lock_group ();

    lock_group (const lock_group &) = delete;
    lock_group &operator= (const lock_group &) = delete;

    int get_tran_index () const
    {
      return m_tran_index;
    }
    bool in_section () const
    {
      return m_sections.load (std::memory_order_acquire) > 0;
    }
    bool is_owner (const THREAD_ENTRY *thread_p) const
    {
      return m_owner.load (std::memory_order_acquire) == thread_p;
    }
    bool is_guest (const THREAD_ENTRY *thread_p) const
    {
      return m_guest.load (std::memory_order_acquire) == thread_p;
    }

    /* the parallel sections and the transaction's end (lock_group_section_begin () and the rest of lock_manager.h) */
    void section_begin ();
    void section_end ();
    void clear ();
    bool defer_abort (THREAD_ENTRY *thread_p, bool to_pl_caller);
    bool is_abort_deferred () const
    {
      return m_abort_deferred.load (std::memory_order_acquire);
    }
    void clear_abort_deferred ()
    {
      m_abort_deferred.store (false, std::memory_order_release);
    }
    int abort_reason () const;
    bool holds_interrupt () const;

    /* a lock wait of one of its threads (lock_suspend ()) */
    void start_wait (bool in_group, INT64 stime);
    void end_wait (bool in_group, const THREAD_ENTRY *thread_p);
    void owner_sleeps (const LK_RES_KEY *wait_key, UINT64 held_row);
    void wakes (bool is_owner, bool interrupted);
    void wait_for_guest ();
    void keep_interrupt ();
    int step_out (THREAD_ENTRY *thread_p);
    void step_back (THREAD_ENTRY *thread_p, int depth);

#if !defined (NDEBUG)
    int solo_step_out (THREAD_ENTRY *thread_p);
    void solo_step_back (THREAD_ENTRY *thread_p, int depth);
    bool is_alone (THREAD_ENTRY *thread_p) const;
#endif

  private:
    friend class lock_group_guard;

    /* the threads take turns only in a section. Group 0, the system transaction's, never opens one, and a change
     * here must keep it ungated */
    bool is_gated () const
    {
      return in_section ();
    }
    /* a lock call of the transaction while it is gated (lock_group_guard) */
    int enter (THREAD_ENTRY *thread_p, LK_GROUP_REQUEST request, const LK_RES_KEY *guest_key,
	       const LK_PENDING_RELEASE *release);
    int enter_object (THREAD_ENTRY *thread_p, LK_GROUP_REQUEST request, const OID *oid, const OID *class_oid);
    int enter_release (THREAD_ENTRY *thread_p, LK_PENDING_RELEASE_TYPE type, const OID *oid, const OID *class_oid,
		       LOCK lock, bool force);
    void exit (THREAD_ENTRY *thread_p);
    bool try_own (THREAD_ENTRY *thread_p);
    bool spin_to_own (THREAD_ENTRY *thread_p);
    void timed_wait ();
    bool wait_for_owner ();
    void give_back ();
    void leave (THREAD_ENTRY *thread_p);
    void guest_leave ();
    void wait_guest_locked ();
#if !defined (NDEBUG)
    bool solo_enter (THREAD_ENTRY *thread_p);
    void solo_leave ();
#endif

    const int m_tran_index;	/* the owning unit: locks its threads take belong to this transaction */
    std::atomic<int> m_sections;	/* open parallel sections */
    /* written under the mutex, but the owner, the depths and the transaction end's m_abort_deferred; m_owner_wait_key
     * and m_owner_held_row are also read only under it, m_guest and m_owner_sleeping outside it only to see the
     * reader's own role and in debug checks. No lock is taken in it. A pthread cond, as its timed waits must read
     * CLOCK_MONOTONIC on any glibc (timed_wait ()). */
    pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;	/* the entrance freed, the owner asleep, or an abort for the waiters to see */
    std::atomic<THREAD_ENTRY *> m_owner;	/* the thread inside: taken by a CAS from NULL, given back with no mutex */
    int m_depth;		/* the owner's re-entries; only the owner touches it */
    std::atomic<THREAD_ENTRY *> m_guest;	/* a second thread let in while the owner sleeps */
    int m_guest_depth;		/* the guest's re-entries; only the guest touches it */
    std::atomic<int> m_waiters;	/* threads in a timed wait: an owner leaving reads it with no mutex */
    std::atomic<bool> m_owner_sleeping;	/* the owner waits for another transaction's lock: a guest may come in */
    /* m_owner_sleeping, or since waiting for its guest to leave: nothing else may wait. The fast path reads it with
     * no mutex. */
    std::atomic<bool> m_owner_blocked;
    LK_RES_KEY m_owner_wait_key;	/* the resource it waits for, while m_owner_sleeping */
    UINT64 m_owner_held_row;	/* the row it holds while it waits on a self-lock (deferred_wake.held_row), or 0 */
    std::atomic<bool> m_interrupted;	/* an interrupt came while sections were open; cleared when they close */
    /* an abort waits for the section owner or a PL callback's caller; cleared when the transaction ends */
    std::atomic<bool> m_abort_deferred;
    std::atomic<LK_PENDING_RELEASE *> m_pending_release;	/* run by the owner before it leaves */
#if !defined (NDEBUG)
    std::atomic<THREAD_ENTRY *> m_solo_owner;	/* the thread inside with no section open, to catch a second one */
    int m_solo_depth;
#endif
};

/* a lock call's pass through the entrance: inline, so that with no section open it costs a load and a branch */
class lock_group_guard
{
  public:
    /* guest_key: the resource, for a request that may enter as the guest */
    lock_group_guard (lock_group *group, THREAD_ENTRY *thread_p, LK_GROUP_REQUEST request,
		      const LK_RES_KEY *guest_key = NULL)
      : lock_group_guard (group, thread_p)
    {
      if (m_group->is_gated ())
	{
	  admit (m_group->enter (m_thread_p, request, guest_key, NULL));
	}
      else
	{
	  solo ();
	}
    }
    /* an acquisition of an object */
    lock_group_guard (lock_group *group, THREAD_ENTRY *thread_p, LK_GROUP_REQUEST request, const OID *oid,
		      const OID *class_oid)
      : lock_group_guard (group, thread_p)
    {
      if (m_group->is_gated ())
	{
	  admit (m_group->enter_object (m_thread_p, request, oid, class_oid));
	}
      else
	{
	  solo ();
	}
    }
    /* a release of an object, which may be handed to the owner */
    lock_group_guard (lock_group *group, THREAD_ENTRY *thread_p, LK_PENDING_RELEASE_TYPE type, const OID *oid,
		      const OID *class_oid, LOCK lock, bool force)
      : lock_group_guard (group, thread_p)
    {
      if (m_group->is_gated ())
	{
	  admit (m_group->enter_release (m_thread_p, type, oid, class_oid, lock, force));
	}
      else
	{
	  solo ();
	}
    }
    ~lock_group_guard ()
    {
      if (m_state == ENTERED)
	{
	  m_group->exit (m_thread_p);
	}
#if !defined (NDEBUG)
      else if (m_state == SOLO)
	{
	  m_group->solo_leave ();
	}
#endif
    }

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

    lock_group_guard (lock_group *group, THREAD_ENTRY *thread_p)
      : m_group (group)
      , m_thread_p (thread_p)
      , m_state (NOT_NEEDED)
      , m_lock_result (LK_GRANTED)
    {
    }
    void admit (int lock_result)
    {
      m_lock_result = lock_result;
      if (lock_result == LK_GRANTED)
	{
	  m_state = ENTERED;
	}
    }
    void solo ()
    {
#if !defined (NDEBUG)
      if (m_group->solo_enter (m_thread_p))
	{
	  m_state = SOLO;
	}
#endif
    }

    lock_group *m_group;
    THREAD_ENTRY *m_thread_p;
    state m_state;
    int m_lock_result;
};

extern void lock_group_set_abort_error (int tran_index, int error);

/* from lock_manager.c */
extern lock_group *lock_get_tran_group (int tran_index);
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
