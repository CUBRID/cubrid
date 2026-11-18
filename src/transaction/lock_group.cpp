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
 * lock_group.cpp - the entrance of a transaction's threads to the lock manager
 */

#include "lock_group.hpp"

#include "error_manager.h"
#include "log_impl.h"
#include "oid.h"
#include "page_buffer.h"
#include "thread_entry.hpp"
#include "thread_manager.hpp"

#include <stdlib.h>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if defined(SERVER_MODE)
/*
 * lock_group - the entrance of a transaction's threads to the lock manager
 *
 * A public function that writes the transaction's lock state constructs a lock_group_guard first. While a parallel
 * section of the transaction is open, the guard lets one of its threads in at a time, and the thread inside (the
 * owner) keeps the entrance while it sleeps for a lock: the lock state, its waits included, stay one thread's as
 * lock_manager.c assumes. While the owner sleeps, one more thread may come in as the guest to lock or release a
 * catalog directory OID or an MVCCID, or to release a row, other than the resource the owner waits for and the row it
 * may hand over: requests with no escalation, no class entry to free, no hold-list walk and no hand-over, the locks
 * of the first two the only ones a thread may wait for with a page fixed. The owner wakes to wait for the guest to
 * leave. A thread already inside only re-enters.
 */

/* LK_GROUP.cond, whose timed waits read LK_GROUP_COND_CLOCK */
void
lock_group_init_cond (LK_GROUP * group)
{
#if defined (WINDOWS)
  pthread_cond_init (&group->cond, NULL);
#else
  pthread_condattr_t attr;

  pthread_condattr_init (&attr);
  pthread_condattr_setclock (&attr, LK_GROUP_COND_CLOCK);
  pthread_cond_init (&group->cond, &attr);
  pthread_condattr_destroy (&attr);
#endif
}

/* the threads take turns only in a section; true here would make them take turns always */
static inline bool
lock_group_is_gated (const LK_GROUP * group)
{
  return lock_group_in_section (group);
}

/*
 * lock_group_abort_reason () - why the transaction in a section may not wait for, or take, another lock
 *   return: ER_LK_UNILATERALLY_ABORTED, ER_INTERRUPTED or NO_ERROR
 *
 * Note: reads, takes no lock. The transaction's interrupt is read as it is, not through logtb_is_interrupted_tran (),
 *	 which looks up the PL session: lock_suspend () asks with the thread entry locked.
 */
int
lock_group_abort_reason (const LK_GROUP * group)
{
  LOG_TDES *tdes = LOG_FIND_TDES (group->tran_index);

  if (lock_is_tran_deadlock_victim (group->tran_index) || __atomic_load_n (&group->abort_deferred, __ATOMIC_ACQUIRE)
      || (tdes != NULL && tdes->tran_abort_reason != TRAN_NORMAL))
    {
      return ER_LK_UNILATERALLY_ABORTED;
    }
  if (__atomic_load_n (&group->interrupted, __ATOMIC_ACQUIRE) || (tdes != NULL && tdes->interrupt))
    {
      return ER_INTERRUPTED;
    }
  return NO_ERROR;
}

void
lock_group_set_abort_error (int tran_index, int error)
{
  if (error == ER_INTERRUPTED)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
    }
  else
    {
      lock_set_error_for_aborted (tran_index);
    }
}

/*
 * the entrance taken as the owner: only a CAS from NULL, so a thread that finds the owner gone takes it with no mutex
 * (lock_group_guard), and every other taker, holding the mutex or not, competes the same way
 */
static inline bool
lock_group_try_own (LK_GROUP * group, THREAD_ENTRY * thread_p)
{
  THREAD_ENTRY *expected = NULL;

  return __atomic_compare_exchange_n (&group->owner, &expected, thread_p, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

#if defined (WINDOWS)
#define LK_GROUP_CPU_PAUSE() YieldProcessor ()
#elif defined (__GNUC__) && (defined (__x86_64__) || defined (__i386__))
#define LK_GROUP_CPU_PAUSE() __builtin_ia32_pause ()
#elif defined (__GNUC__) && defined (__aarch64__)
#define LK_GROUP_CPU_PAUSE() __asm__ __volatile__ ("yield")
#else
#define LK_GROUP_CPU_PAUSE()
#endif
#define LK_GROUP_OWN_SPINS 64

/*
 * An owner awake leaves within a lock call, much sooner than the mutex and a timed wait take: spin a little for it.
 * Not for an owner blocked, which waits for another transaction.
 */
static bool
lock_group_spin_to_own (LK_GROUP * group, THREAD_ENTRY * thread_p)
{
  int i;

  for (i = 0; i < LK_GROUP_OWN_SPINS; i++)
    {
      if (__atomic_load_n (&group->owner_blocked, __ATOMIC_ACQUIRE))
	{
	  return false;
	}
      LK_GROUP_CPU_PAUSE ();
      if (__atomic_load_n (&group->owner, __ATOMIC_RELAXED) == NULL && lock_group_try_own (group, thread_p))
	{
	  return true;
	}
    }
  return false;
}

/* at most 10 ms: no one signals an abort reason found by the deadlock detector */
static void
lock_group_timed_wait (LK_GROUP * group)
{
  struct timespec until;

  clock_gettime (LK_GROUP_COND_CLOCK, &until);
  until.tv_nsec += 10 * 1000 * 1000;
  if (until.tv_nsec >= 1000 * 1000 * 1000)
    {
      until.tv_sec++;
      until.tv_nsec -= 1000 * 1000 * 1000;
    }
  (void) pthread_cond_timedwait (&group->cond, &group->mutex, &until);
}

/*
 * A timed wait for the owner to leave, with group->mutex held: the owner leaves with no mutex, storing NULL and then
 * reading waiters, while a waiter counts itself and then reads owner. Both seq_cst, so one of the two sees the other:
 * the waiter does not wait, or the owner broadcasts, which it can do only once the waiter is in the wait, as the
 * waiter holds the mutex until pthread_cond_timedwait () releases it.
 *   return: false if the owner was found gone instead
 */
static bool
lock_group_wait_for_owner (LK_GROUP * group)
{
  __atomic_add_fetch (&group->waiters, 1, __ATOMIC_SEQ_CST);
  if (__atomic_load_n (&group->owner, __ATOMIC_SEQ_CST) == NULL)
    {
      __atomic_sub_fetch (&group->waiters, 1, __ATOMIC_SEQ_CST);
      return false;
    }
  lock_group_timed_wait (group);
  __atomic_sub_fetch (&group->waiters, 1, __ATOMIC_SEQ_CST);
  return true;
}

/* the owner, with no guest and no release handed to it, gives the entrance back with no mutex */
static void
lock_group_give_back (LK_GROUP * group)
{
  assert (__atomic_load_n (&group->guest, __ATOMIC_ACQUIRE) == NULL);
  assert (__atomic_load_n (&group->pending_release, __ATOMIC_ACQUIRE) == NULL);
  __atomic_store_n (&group->owner, (THREAD_ENTRY *) NULL, __ATOMIC_SEQ_CST);
  /* see lock_group_wait_for_owner () */
  if (__atomic_load_n (&group->waiters, __ATOMIC_SEQ_CST) > 0)
    {
      pthread_mutex_lock (&group->mutex);
      pthread_cond_broadcast (&group->cond);
      pthread_mutex_unlock (&group->mutex);
    }
}

/* a release handed over under the mutex is taken before the owner is cleared under it, so none is left behind */
static void
lock_group_leave (LK_GROUP * group, THREAD_ENTRY * thread_p)
{
  LK_PENDING_RELEASE *release, *next;

  while (true)
    {
      pthread_mutex_lock (&group->mutex);
      release = __atomic_exchange_n (&group->pending_release, (LK_PENDING_RELEASE *) NULL, __ATOMIC_ACQ_REL);
      if (release == NULL)
	{
	  assert (group->guest == NULL);
	  __atomic_store_n (&group->owner, (THREAD_ENTRY *) NULL, __ATOMIC_SEQ_CST);
	  /* all: a waiter woken alone could return on an abort without passing the entrance on */
	  pthread_cond_broadcast (&group->cond);
	  pthread_mutex_unlock (&group->mutex);
	  return;
	}
      pthread_mutex_unlock (&group->mutex);

      /* each re-enters as the owner */
      group->depth = 1;
      for (; release != NULL; release = next)
	{
	  next = release->next;
	  switch (release->type)
	    {
	    case LK_PENDING_UNLOCK:
	      lock_unlock_object (thread_p, &release->oid, &release->class_oid, release->lock, release->force);
	      break;
	    case LK_PENDING_UNLOCK_DONOT_MOVE:
	      lock_unlock_object_donot_move_to_non2pl (thread_p, &release->oid, &release->class_oid, release->lock);
	      break;
	    case LK_PENDING_UNLOCK_TRANSIENT:
	      lock_unlock_object_transient (thread_p, &release->oid,
					    OID_ISNULL (&release->class_oid) ? NULL : &release->class_oid,
					    release->lock);
	      break;
	    }
	  free (release);
	}
      group->depth = 0;
    }
}

void
lock_pending_release_init (LK_PENDING_RELEASE * release, LK_PENDING_RELEASE_TYPE type, const OID * oid,
			   const OID * class_oid, LOCK lock, bool force)
{
  release->next = NULL;
  release->type = type;
  release->oid = *oid;
  if (class_oid != NULL)
    {
      release->class_oid = *class_oid;
    }
  else
    {
      OID_SET_NULL (&release->class_oid);
    }
  release->lock = lock;
  release->force = force;
}

/* the key of a catalog directory OID lock, which may enter as the guest (lock_group_guard), or NULL */
const LK_RES_KEY *
lock_group_dir_oid_key (const OID * oid, const OID * class_oid, LK_RES_KEY * key)
{
  if (oid == NULL || class_oid == NULL || !OID_IS_VIRTUAL_CLASS_OF_DIR_OID (class_oid))
    {
      return NULL;
    }
  *key = lock_create_search_key (oid, class_oid);
  return key;
}

/*
 * the key of a row whose release may enter as the guest, or NULL for a class: a release of one instance entry frees
 * no class entry, only lowers its granule count, and escalates nothing
 */
const LK_RES_KEY *
lock_group_row_release_key (const OID * oid, const OID * class_oid, LK_RES_KEY * key)
{
  if (oid == NULL || class_oid == NULL || OID_IS_ROOTOID (oid) || OID_IS_ROOTOID (class_oid))
    {
      return NULL;
    }
  *key = lock_create_search_key (oid, class_oid);
  return key;
}

/* the owner, or a page-fixed sibling, may wait for the guest */
static void
lock_group_guest_leave (LK_GROUP * group)
{
  pthread_mutex_lock (&group->mutex);
  __atomic_store_n (&group->guest, (THREAD_ENTRY *) NULL, __ATOMIC_RELEASE);
  pthread_cond_broadcast (&group->cond);
  pthread_mutex_unlock (&group->mutex);
}

/*
 * wait, with group->mutex held, until the guest has left. Once the transaction is to abort the guest's lock wait is
 * ended here: the deadlock detector reads the victim's waiters before it marks the victim, so a guest that goes to
 * sleep in between is woken by no one else.
 */
static void
lock_group_wait_guest_locked (LK_GROUP * group)
{
  THREAD_ENTRY *guest;

  while ((guest = group->guest) != NULL)
    {
      if (lock_group_abort_reason (group) != NO_ERROR)
	{
	  /* the guest's thread mutex is not taken under the group mutex */
	  pthread_mutex_unlock (&group->mutex);
	  lock_interrupt_lockwait (guest, group->tran_index);
	  pthread_mutex_lock (&group->mutex);
	  if (group->guest != guest)
	    {
	      continue;
	    }
	}
      __atomic_add_fetch (&group->waiters, 1, __ATOMIC_SEQ_CST);
      lock_group_timed_wait (group);
      __atomic_sub_fetch (&group->waiters, 1, __ATOMIC_SEQ_CST);
    }
}

/* an interrupt for the siblings that the transaction's own may no longer show; kept until the sections close */
void
lock_group_keep_interrupt (LK_GROUP * group)
{
  pthread_mutex_lock (&group->mutex);
  if (lock_group_in_section (group))
    {
      __atomic_store_n (&group->interrupted, true, __ATOMIC_RELEASE);
      pthread_cond_broadcast (&group->cond);
    }
  pthread_mutex_unlock (&group->mutex);
}

/*
 * the owner wakes to the transaction's state only once the guest has left. This wait closes no cycle: the guest never
 * waits for the owner, and the only page it may fix in here, the root class heap page it reads for a lock timeout
 * message (lock_set_error_for_timeout ()), it reads with a read latch released before it returns; the owner holds no
 * page here but one safe to hold in a lock wait (lock_suspend ()).
 */
void
lock_group_wait_for_guest (LK_GROUP * group)
{
  pthread_mutex_lock (&group->mutex);
  lock_group_wait_guest_locked (group);
  __atomic_store_n (&group->owner_blocked, false, __ATOMIC_RELEASE);
  pthread_mutex_unlock (&group->mutex);
}

/*
 * lock_group_step_out () - a thread leaves the entrance it holds as owner or guest, keeping its depth to come back with
 *   return: the depth, or 0 if it held none
 *
 * Note: an owner lets its guest leave first, so no one finds the owner gone while a guest stays.
 */
int
lock_group_step_out (LK_GROUP * group, THREAD_ENTRY * thread_p)
{
  int depth = 0;

  pthread_mutex_lock (&group->mutex);
  if (__atomic_load_n (&group->owner, __ATOMIC_ACQUIRE) == thread_p)
    {
      lock_group_wait_guest_locked (group);
      __atomic_store_n (&group->owner_blocked, false, __ATOMIC_RELEASE);
      depth = group->depth;
      pthread_mutex_unlock (&group->mutex);
      /* the releases handed to it go with it, not to whoever enters next */
      lock_group_leave (group, thread_p);
      return depth;
    }
  else if (group->guest == thread_p)
    {
      depth = group->guest_depth;
      group->guest_depth = 0;
      __atomic_store_n (&group->guest, (THREAD_ENTRY *) NULL, __ATOMIC_RELEASE);
      pthread_cond_broadcast (&group->cond);
    }
  pthread_mutex_unlock (&group->mutex);
  return depth;
}

/* back in after lock_group_step_out (), as owner or guest, whichever is free first; no abort check, as it cleans up */
void
lock_group_step_back (LK_GROUP * group, THREAD_ENTRY * thread_p, int depth)
{
  if (depth == 0)
    {
      return;
    }
  pthread_mutex_lock (&group->mutex);
  while (true)
    {
      if (lock_group_try_own (group, thread_p))
	{
	  group->depth = depth;
	  break;
	}
      if (group->owner_sleeping && group->guest == NULL)
	{
	  __atomic_store_n (&group->guest, thread_p, __ATOMIC_RELEASE);
	  group->guest_depth = depth;
	  break;
	}
      (void) lock_group_wait_for_owner (group);
    }
  pthread_mutex_unlock (&group->mutex);
}

/*
 * The transaction's one wait start in the WFG, while its owner and its guest may both sleep: the earlier start, as the
 * deadlock detector skips an edge older than the node's wait, and 0 only once neither waits. A thread of the group
 * updates it under the group mutex, as the two can sleep or wake at once; a waker clears a thread's lockwait before
 * that thread's lock_group_end_wait (), and leaves the slot to it (lock_wake_mvccid_waiters ()).
 */
void
lock_group_start_wait (LK_GROUP * group, bool in_group, INT64 stime)
{
  INT64 *node_stime = lock_get_wait_stime (group->tran_index);

  if (in_group)
    {
      pthread_mutex_lock (&group->mutex);
    }
  if (*node_stime == 0 || *node_stime > stime)
    {
      *node_stime = stime;
    }
  if (in_group)
    {
      pthread_mutex_unlock (&group->mutex);
    }
}

void
lock_group_end_wait (LK_GROUP * group, bool in_group, const THREAD_ENTRY * thread_p)
{
  const THREAD_ENTRY *owner, *guest, *sibling;
  INT64 *node_stime = lock_get_wait_stime (group->tran_index);

  if (!in_group)
    {
      *node_stime = 0;
      return;
    }
  pthread_mutex_lock (&group->mutex);
  owner = __atomic_load_n (&group->owner, __ATOMIC_ACQUIRE);
  guest = group->guest;
  sibling = (owner == thread_p) ? guest : owner;
  *node_stime = (sibling != NULL && sibling->lockwait != NULL) ? sibling->lockwait_stime : 0;
  pthread_mutex_unlock (&group->mutex);
}

/* an interrupt one sleeping thread of the transaction consumed, for the deadlock detector to wake the other with */
bool
lock_group_holds_interrupt (int tran_index)
{
  const LK_GROUP *group;

  if (tran_index <= LOG_SYSTEM_TRAN_INDEX)
    {
      return false;
    }
  group = lock_get_tran_group (tran_index);
  return lock_group_in_section (group) && __atomic_load_n (&group->interrupted, __ATOMIC_ACQUIRE);
}

/* the owner sleeps for another transaction: a conditional request no longer waits for it */
void
lock_group_owner_sleeps (LK_GROUP * group, const LK_RES_KEY * wait_key, UINT64 held_row)
{
  pthread_mutex_lock (&group->mutex);
  group->owner_sleeping = true;
  __atomic_store_n (&group->owner_blocked, true, __ATOMIC_RELEASE);
  group->owner_wait_key = *wait_key;
  group->owner_held_row = held_row;
  if (__atomic_load_n (&group->waiters, __ATOMIC_SEQ_CST) > 0)
    {
      pthread_cond_broadcast (&group->cond);
    }
  pthread_mutex_unlock (&group->mutex);
}

/*
 * back from a lock wait: no guest is let in once the owner is awake, and an interrupt that ended the wait stays for the
 * siblings, as the deadlock detector cleared the transaction's own
 */
void
lock_group_wakes (LK_GROUP * group, bool is_owner, bool interrupted)
{
  if (is_owner)
    {
      pthread_mutex_lock (&group->mutex);
      group->owner_sleeping = false;
      /* still blocked only by a guest let in while it slept; none comes in from now */
      __atomic_store_n (&group->owner_blocked, group->guest != NULL, __ATOMIC_RELEASE);
      pthread_mutex_unlock (&group->mutex);
    }
  if (interrupted)
    {
      lock_group_keep_interrupt (group);
    }
}

#if !defined (NDEBUG)
/* outside its sections the threads of a transaction enter unchecked; a second one inside at once is a bug */
static bool
lock_group_solo_enter (LK_GROUP * group, THREAD_ENTRY * thread_p)
{
  THREAD_ENTRY *expected = NULL;
  THREAD_ENTRY *owner = __atomic_load_n (&group->owner, __ATOMIC_ACQUIRE);
  THREAD_ENTRY *guest = __atomic_load_n (&group->guest, __ATOMIC_ACQUIRE);
  THREAD_ENTRY *inside = (owner != NULL && owner != thread_p) ? owner : ((guest != thread_p) ? guest : NULL);

  /* a pool thread runs only while its leader's section is open */
  assert (!thread_is_px_worker (thread_p));

  if (__atomic_load_n (&group->solo_owner, __ATOMIC_ACQUIRE) == thread_p)
    {
      group->solo_depth++;
      return true;
    }
  if (inside != NULL
      || !__atomic_compare_exchange_n (&group->solo_owner, &expected, thread_p, false, __ATOMIC_ACQ_REL,
				       __ATOMIC_ACQUIRE))
    {
      _er_log_debug (ARG_FILE_LINE,
		     "lock_group: thread %d of transaction %d enters the lock manager while thread %d is in it\n",
		     thread_p->index, group->tran_index, (inside != NULL) ? inside->index : expected->index);
      assert (false);
      return false;
    }
  group->solo_depth = 1;
  return true;
}

static void
lock_group_solo_leave (LK_GROUP * group)
{
  if (--group->solo_depth == 0)
    {
      __atomic_store_n (&group->solo_owner, (THREAD_ENTRY *) NULL, __ATOMIC_RELEASE);
    }
}

/* a victim waiting for its siblings lets them in (lock_suspend_internal ()) */
int
lock_group_solo_step_out (LK_GROUP * group, THREAD_ENTRY * thread_p)
{
  int depth = 0;

  if (__atomic_load_n (&group->solo_owner, __ATOMIC_ACQUIRE) == thread_p)
    {
      depth = group->solo_depth;
      group->solo_depth = 0;
      __atomic_store_n (&group->solo_owner, (THREAD_ENTRY *) NULL, __ATOMIC_RELEASE);
    }
  return depth;
}

void
lock_group_solo_step_back (LK_GROUP * group, THREAD_ENTRY * thread_p, int depth)
{
  if (depth > 0 && lock_group_solo_enter (group, thread_p))
    {
      group->solo_depth = depth;
    }
}

/* nothing else of the transaction is in the lock manager: a walk of its hold lists needs no hold_mutex */
bool
lock_group_is_alone (THREAD_ENTRY * thread_p, int tran_index)
{
  const LK_GROUP *group = lock_get_tran_group (tran_index);

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }
  if (!lock_group_in_section (group))
    {
      return true;
    }
  if (__atomic_load_n (&group->owner, __ATOMIC_ACQUIRE) == thread_p)
    {
      return __atomic_load_n (&group->guest, __ATOMIC_ACQUIRE) == NULL;
    }
  return (__atomic_load_n (&group->guest, __ATOMIC_ACQUIRE) == thread_p
	  && __atomic_load_n (&group->owner_sleeping, __ATOMIC_ACQUIRE));
}
#endif /* !NDEBUG */

lock_group_guard::lock_group_guard (THREAD_ENTRY *thread_p, int tran_index, LK_GROUP_REQUEST request,
				    const LK_RES_KEY *guest_key, const LK_PENDING_RELEASE *release)
  : m_group (NULL)
  , m_thread_p (thread_p)
  , m_state (NOT_NEEDED)
  , m_lock_result (LK_GRANTED)
{
  LK_PENDING_RELEASE *handed = NULL;
  int error;

  if (tran_index <= LOG_SYSTEM_TRAN_INDEX)
    {
      /* the system transaction is many threads' and opens no section */
      return;
    }
  if (m_thread_p == NULL)
    {
      m_thread_p = thread_get_thread_entry_info ();
    }
  m_group = lock_get_tran_group (tran_index);

  if (!lock_group_is_gated (m_group))
    {
#if !defined (NDEBUG)
      if (lock_group_solo_enter (m_group, m_thread_p))
	{
	  m_state = SOLO;
	}
#endif
      return;
    }
  if (__atomic_load_n (&m_group->owner, __ATOMIC_ACQUIRE) == m_thread_p)
    {
      m_group->depth++;
      m_state = ENTERED;
      return;
    }
  if (__atomic_load_n (&m_group->guest, __ATOMIC_ACQUIRE) == m_thread_p)
    {
      m_group->guest_depth++;
      m_state = ENTERED;
      return;
    }

  /*
   * The entrance free: taken with no mutex. Owner NULL implies no guest, the owner neither asleep nor blocked, and no
   * release handed over, as only a blocked owner is handed one and every way out drains them first. An acquisition
   * checks for an abort again once it is the owner: a deferred abort can land between the check and the CAS, the spin
   * widening that window, and lock_group_defer_abort () has returned by then.
   */
  if ((request == LK_GROUP_RELEASE || lock_group_abort_reason (m_group) == NO_ERROR)
      && (lock_group_try_own (m_group, m_thread_p) || lock_group_spin_to_own (m_group, m_thread_p)))
    {
      error = (request == LK_GROUP_RELEASE) ? NO_ERROR : lock_group_abort_reason (m_group);
      if (error != NO_ERROR)
	{
	  lock_group_give_back (m_group);
	  lock_group_set_abort_error (tran_index, error);
	  m_lock_result = (error == ER_INTERRUPTED) ? LK_NOTGRANTED_DUE_ERROR : LK_NOTGRANTED_DUE_ABORTED;
	  return;
	}
      m_group->depth = 1;
      m_state = ENTERED;
      return;
    }

  if (release != NULL && pgbuf_has_perm_pages_fixed (m_thread_p))
    {
      /* allocated before the mutex, under which no other lock is taken */
      handed = (LK_PENDING_RELEASE *) malloc (sizeof (LK_PENDING_RELEASE));
      if (handed != NULL)
	{
	  *handed = *release;
	}
    }

  pthread_mutex_lock (&m_group->mutex);
  while (true)
    {
      if (request != LK_GROUP_RELEASE && lock_group_in_section (m_group))
	{
	  /* also when nothing was waited for: a sibling may have left the entrance as a victim */
	  error = lock_group_abort_reason (m_group);
	  if (error != NO_ERROR)
	    {
	      pthread_mutex_unlock (&m_group->mutex);
	      lock_group_set_abort_error (tran_index, error);
	      m_lock_result = (error == ER_INTERRUPTED) ? LK_NOTGRANTED_DUE_ERROR : LK_NOTGRANTED_DUE_ABORTED;
	      return;
	    }
	}
      if (lock_group_try_own (m_group, m_thread_p))
	{
	  m_group->depth = 1;
	  break;
	}
      if (m_group->owner_sleeping && guest_key != NULL && m_group->guest == NULL
	  && lock_res_key_compare ((void *) guest_key, &m_group->owner_wait_key) != 0
	  && (guest_key->type != LOCK_RESOURCE_INSTANCE
	      || lock_pack_row_oid (&guest_key->oid) != m_group->owner_held_row))
	{
	  __atomic_store_n (&m_group->guest, m_thread_p, __ATOMIC_RELEASE);
	  m_group->guest_depth = 1;
	  break;
	}
      /* the owner awake but waiting for a guest asleep behind a catalog updater is no shorter a wait */
      if (__atomic_load_n (&m_group->owner_blocked, __ATOMIC_ACQUIRE))
	{
	  if (request == LK_GROUP_ACQUIRE_COND)
	    {
	      /* the owner's sleep is another transaction's to end; a conditional request does not share it */
	      pthread_mutex_unlock (&m_group->mutex);
	      m_lock_result = LK_NOTGRANTED_DUE_TIMEOUT;
	      free (handed);
#if !defined (NDEBUG)
	      er_print_callstack (ARG_FILE_LINE, "lock_group: tran %d: conditional request met the owner asleep\n",
				  tran_index);
#endif
	      return;
	    }
	  /* the deadlock detector would not see a page fixed while waiting for an owner asleep */
	  if (handed != NULL)
	    {
	      handed->next = __atomic_load_n (&m_group->pending_release, __ATOMIC_RELAXED);
	      __atomic_store_n (&m_group->pending_release, handed, __ATOMIC_RELEASE);
	      pthread_mutex_unlock (&m_group->mutex);
	      m_lock_result = LK_NOTGRANTED;
	      return;
	    }
	  /* a guest-family request waits behind the guest, or the owner on its own resource: a catalog updater that
	   * needs no user page, as lock_is_safe_lock_with_page () assumes */
	  assert (request == LK_GROUP_RELEASE || guest_key != NULL || !pgbuf_has_perm_pages_fixed (m_thread_p));
	}
      (void) lock_group_wait_for_owner (m_group);
    }
  assert (m_group->guest == NULL || __atomic_load_n (&m_group->owner, __ATOMIC_ACQUIRE) != NULL);
  pthread_mutex_unlock (&m_group->mutex);
  m_state = ENTERED;
  free (handed);
}

lock_group_guard::~lock_group_guard ()
{
#if !defined (NDEBUG)
  if (m_state == SOLO)
    {
      lock_group_solo_leave (m_group);
      return;
    }
#endif
  if (m_state != ENTERED)
    {
      return;
    }
  /* by the role held now: a deadlock victim may come back in the other one (lock_suspend_internal ()) */
  if (__atomic_load_n (&m_group->owner, __ATOMIC_ACQUIRE) == m_thread_p)
    {
      if (--m_group->depth > 0)
	{
	  return;
	}
      /* no release can be handed over from here on: the owner is awake with no guest, so not blocked */
      if (__atomic_load_n (&m_group->pending_release, __ATOMIC_ACQUIRE) != NULL)
	{
	  lock_group_leave (m_group, m_thread_p);
	  return;
	}
      lock_group_give_back (m_group);
    }
  else
    {
      assert (__atomic_load_n (&m_group->guest, __ATOMIC_ACQUIRE) == m_thread_p);
      if (--m_group->guest_depth == 0)
	{
	  lock_group_guest_leave (m_group);
	}
    }
}
#endif /* SERVER_MODE */

/*
 * lock_group_section_begin () - a parallel section of the transaction opens: its threads enter the lock manager one
 *				 at a time until it closes
 *   tran_index(in): the transaction of the thread that reserved the parallel workers
 *
 * Note: called with no thread of the transaction in the lock manager, so none of them entered unchecked.
 */
void
lock_group_section_begin (int tran_index)
{
#if defined (SERVER_MODE)
  if (tran_index > LOG_SYSTEM_TRAN_INDEX)
    {
      __atomic_add_fetch (&lock_get_tran_group (tran_index)->sections, 1, __ATOMIC_SEQ_CST);
    }
#endif /* SERVER_MODE */
}

/*
 * lock_group_section_end () - a parallel section of the transaction closes, its workers all joined
 *   tran_index(in): as given to lock_group_section_begin ()
 */
void
lock_group_section_end (int tran_index)
{
#if defined (SERVER_MODE)
  LK_GROUP *group;
  int sections;

  if (tran_index <= LOG_SYSTEM_TRAN_INDEX)
    {
      return;
    }
  group = lock_get_tran_group (tran_index);
  pthread_mutex_lock (&group->mutex);
  sections = __atomic_sub_fetch (&group->sections, 1, __ATOMIC_SEQ_CST);
  assert (sections >= 0);
  if (sections == 0)
    {
      __atomic_store_n (&group->interrupted, false, __ATOMIC_RELEASE);
    }
  pthread_mutex_unlock (&group->mutex);
#endif /* SERVER_MODE */
}

/*
 * lock_group_clear () - no thread of the transaction index is left: its client unregisters, or the index is given
 *			 back. A section still open is leaked, as no owner is left to close it: it would defer the
 *			 client's abort, and make the next transaction of the index take turns for good.
 */
void
lock_group_clear (int tran_index)
{
#if defined (SERVER_MODE)
  LK_GROUP *group;
  THREAD_ENTRY *owner, *guest;
  LK_PENDING_RELEASE *release, *next;
  int sections;

  if (tran_index <= LOG_SYSTEM_TRAN_INDEX || !lock_has_tran_lock (tran_index))
    {
      return;
    }
  group = lock_get_tran_group (tran_index);

  pthread_mutex_lock (&group->mutex);
  sections = __atomic_exchange_n (&group->sections, 0, __ATOMIC_SEQ_CST);
  owner = __atomic_exchange_n (&group->owner, (THREAD_ENTRY *) NULL, __ATOMIC_SEQ_CST);
  guest = __atomic_exchange_n (&group->guest, (THREAD_ENTRY *) NULL, __ATOMIC_SEQ_CST);
  group->depth = 0;
  group->guest_depth = 0;
  group->owner_sleeping = false;
  __atomic_store_n (&group->owner_blocked, false, __ATOMIC_RELEASE);
  __atomic_store_n (&group->interrupted, false, __ATOMIC_RELEASE);
  __atomic_store_n (&group->abort_deferred, false, __ATOMIC_RELEASE);
  release = __atomic_exchange_n (&group->pending_release, (LK_PENDING_RELEASE *) NULL, __ATOMIC_ACQ_REL);
  pthread_mutex_unlock (&group->mutex);

  for (; release != NULL; release = next)
    {
      next = release->next;
      free (release);
    }
  if (sections != 0 || owner != NULL || guest != NULL)
    {
      _er_log_debug (ARG_FILE_LINE, "lock_group: transaction index %d is cleared with %d parallel sections open%s\n",
		     tran_index, sections, (owner != NULL || guest != NULL) ? " and a thread inside" : "");
      assert (false);
    }
#endif /* SERVER_MODE */
}

/*
 * lock_group_has_open_section () - whether a parallel section of the transaction is open: then only its owner ends
 *				    the transaction, after the section closes
 */
bool
lock_group_has_open_section (int tran_index)
{
#if defined (SERVER_MODE)
  return tran_index > LOG_SYSTEM_TRAN_INDEX && lock_group_in_section (lock_get_tran_group (tran_index));
#else /* SERVER_MODE */
  return false;
#endif /* SERVER_MODE */
}

#if defined (SERVER_MODE)
/*
 * lock_group_defer_abort_internal () - leave the abort of a transaction to a request that ends it later
 *   to_pl_caller(in): also with no section open, for a deadlock victim's abort (lock_group_defer_abort_to_pl_caller ())
 *
 * Note: under the mutex lock_group_section_end () takes, so a section closing meanwhile keeps no stale interrupt.
 *	 With no section, no interrupt: the PL calls would report it as ER_INTERRUPTED over the deadlock error. Their
 *	 statements fail as they return instead (lock_group_fail_if_abort_deferred ()).
 */
static bool
lock_group_defer_abort_internal (THREAD_ENTRY * thread_p, int tran_index, bool to_pl_caller)
{
  LK_GROUP *group = lock_get_tran_group (tran_index);
  bool in_section;

  pthread_mutex_lock (&group->mutex);
  in_section = lock_group_in_section (group);
  /* with no section, only a deadlock victim's abort: not that of a transaction a COMMIT under the PL has just
   * ended and restarted (xtran_end_is_held_by_parallel_section ()) */
  if (!in_section && ! (to_pl_caller && (lock_is_tran_deadlock_victim (tran_index)
					 || __atomic_load_n (&group->abort_deferred, __ATOMIC_ACQUIRE))))
    {
      pthread_mutex_unlock (&group->mutex);
      return false;
    }
  __atomic_store_n (&group->abort_deferred, true, __ATOMIC_RELEASE);
  if (!in_section)
    {
      pthread_mutex_unlock (&group->mutex);
      return true;
    }
  __atomic_store_n (&group->interrupted, true, __ATOMIC_RELEASE);
  pthread_cond_broadcast (&group->cond);
  pthread_mutex_unlock (&group->mutex);

  /* the owner's query stops at its next row even if it asks for no lock */
  logtb_set_tran_index_interrupt (thread_p, tran_index, true);
  return true;
}
#endif /* SERVER_MODE */

/*
 * lock_group_defer_abort () - leave the abort of a transaction with a parallel section open to the section owner
 *   return: true if deferred: the transaction stays active, its lock requests in the section and its next interrupt
 *	     check fail, and its next end is an abort (lock_group_is_abort_deferred ())
 *
 * Note: the caller is not the section owner, which joins its workers before it ends the transaction. That an owner
 *	 is still there is not checked: a section left open by a client gone is cleared by lock_group_clear () when the
 *	 client unregisters or the index is freed, and the abort then runs.
 */
bool
lock_group_defer_abort (THREAD_ENTRY * thread_p, int tran_index)
{
#if defined (SERVER_MODE)
  return tran_index > LOG_SYSTEM_TRAN_INDEX && lock_group_defer_abort_internal (thread_p, tran_index, false);
#else /* SERVER_MODE */
  return false;
#endif /* SERVER_MODE */
}

/*
 * lock_group_defer_abort_to_pl_caller () - leave the abort of a transaction to the request a PL callback returns to
 *   return: true if deferred, as lock_group_defer_abort (); with no section open, only a deadlock victim's abort is
 */
bool
lock_group_defer_abort_to_pl_caller (THREAD_ENTRY * thread_p, int tran_index)
{
#if defined (SERVER_MODE)
  return tran_index > LOG_SYSTEM_TRAN_INDEX && lock_group_defer_abort_internal (thread_p, tran_index, true);
#else /* SERVER_MODE */
  return false;
#endif /* SERVER_MODE */
}

/*
 * lock_group_fail_if_abort_deferred () - fail a statement whose transaction's abort is left to a request that ends it
 *   return: ER_LK_UNILATERALLY_ABORTED (set) or NO_ERROR
 */
int
lock_group_fail_if_abort_deferred (int tran_index)
{
#if defined (SERVER_MODE)
  if (lock_group_is_abort_deferred (tran_index))
    {
      lock_set_error_for_aborted (tran_index);
      return ER_LK_UNILATERALLY_ABORTED;
    }
#endif /* SERVER_MODE */
  return NO_ERROR;
}

/*
 * lock_group_is_abort_deferred () - whether an abort of the transaction waits for it to end (lock_group_defer_abort (),
 *   lock_group_defer_abort_to_pl_caller ())
 */
bool
lock_group_is_abort_deferred (int tran_index)
{
#if defined (SERVER_MODE)
  return (tran_index > LOG_SYSTEM_TRAN_INDEX
	  && __atomic_load_n (&lock_get_tran_group (tran_index)->abort_deferred, __ATOMIC_ACQUIRE));
#else /* SERVER_MODE */
  return false;
#endif /* SERVER_MODE */
}
