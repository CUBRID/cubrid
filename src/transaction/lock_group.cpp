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

/* the clock of m_cond's timed waits: a wall clock set back would stretch them */
#if defined (WINDOWS)
#define LK_GROUP_COND_CLOCK CLOCK_REALTIME
#else
#define LK_GROUP_COND_CLOCK CLOCK_MONOTONIC
#endif

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

lock_group::lock_group (int tran_index)
  : m_tran_index (tran_index)
  , m_sections (0)
  , m_owner (NULL)
  , m_depth (0)
  , m_guest (NULL)
  , m_guest_depth (0)
  , m_waiters (0)
  , m_owner_sleeping (false)
  , m_owner_blocked (false)
  , m_owner_wait_key ()
  , m_owner_held_row (0)
  , m_interrupted (false)
  , m_abort_deferred (false)
  , m_pending_release (NULL)
#if !defined (NDEBUG)
  , m_solo_owner (NULL)
  , m_solo_depth (0)
#endif
{
  pthread_mutex_init (&m_mutex, NULL);
#if defined (WINDOWS)
  pthread_cond_init (&m_cond, NULL);
#else
  pthread_condattr_t attr;

  pthread_condattr_init (&attr);
  pthread_condattr_setclock (&attr, LK_GROUP_COND_CLOCK);
  pthread_cond_init (&m_cond, &attr);
  pthread_condattr_destroy (&attr);
#endif
}

lock_group::~lock_group ()
{
  pthread_mutex_destroy (&m_mutex);
  pthread_cond_destroy (&m_cond);
}

/*
 * abort_reason () - why the transaction in a section may not wait for, or take, another lock
 *   return: ER_LK_UNILATERALLY_ABORTED, ER_INTERRUPTED or NO_ERROR
 *
 * Note: reads, takes no lock. The transaction's interrupt is read as it is, not through logtb_is_interrupted_tran (),
 *	 which looks up the PL session: lock_suspend () asks with the thread entry locked.
 */
int
lock_group::abort_reason () const
{
  LOG_TDES *tdes = LOG_FIND_TDES (m_tran_index);

  if (lock_is_tran_deadlock_victim (m_tran_index) || m_abort_deferred.load (std::memory_order_acquire)
      || (tdes != NULL && tdes->tran_abort_reason != TRAN_NORMAL))
    {
      return ER_LK_UNILATERALLY_ABORTED;
    }
  if (m_interrupted.load (std::memory_order_acquire) || (tdes != NULL && tdes->interrupt))
    {
      return ER_INTERRUPTED;
    }
  return NO_ERROR;
}

/*
 * the entrance taken as the owner: only a CAS from NULL, so a thread that finds the owner gone takes it with no mutex
 * (enter ()), and every other taker, holding the mutex or not, competes the same way
 */
inline bool
lock_group::try_own (THREAD_ENTRY *thread_p)
{
  THREAD_ENTRY *expected = NULL;

  return m_owner.compare_exchange_strong (expected, thread_p, std::memory_order_acq_rel, std::memory_order_acquire);
}

/*
 * An owner awake leaves within a lock call, much sooner than the mutex and a timed wait take: spin a little for it.
 * Not for an owner blocked, which waits for another transaction.
 */
inline bool
lock_group::spin_to_own (THREAD_ENTRY *thread_p)
{
  for (int i = 0; i < LK_GROUP_OWN_SPINS; i++)
    {
      if (m_owner_blocked.load (std::memory_order_acquire))
	{
	  return false;
	}
      LK_GROUP_CPU_PAUSE ();
      if (m_owner.load (std::memory_order_relaxed) == NULL && try_own (thread_p))
	{
	  return true;
	}
    }
  return false;
}

/* at most 10 ms, with m_mutex held: no one signals an abort reason found by the deadlock detector */
void
lock_group::timed_wait ()
{
  struct timespec until;

  clock_gettime (LK_GROUP_COND_CLOCK, &until);
  until.tv_nsec += 10 * 1000 * 1000;
  if (until.tv_nsec >= 1000 * 1000 * 1000)
    {
      until.tv_sec++;
      until.tv_nsec -= 1000 * 1000 * 1000;
    }
  (void) pthread_cond_timedwait (&m_cond, &m_mutex, &until);
}

/*
 * A timed wait for the owner to leave, with m_mutex held: the owner leaves with no mutex, storing NULL and then
 * reading m_waiters, while a waiter counts itself and then reads m_owner. Both seq_cst, so one of the two sees the
 * other: the waiter does not wait, or the owner broadcasts, which it can do only once the waiter is in the wait, as
 * the waiter holds the mutex until pthread_cond_timedwait () releases it.
 *   return: false if the owner was found gone instead
 */
bool
lock_group::wait_for_owner ()
{
  m_waiters.fetch_add (1, std::memory_order_seq_cst);
  if (m_owner.load (std::memory_order_seq_cst) == NULL)
    {
      m_waiters.fetch_sub (1, std::memory_order_seq_cst);
      return false;
    }
  timed_wait ();
  m_waiters.fetch_sub (1, std::memory_order_seq_cst);
  return true;
}

/* the owner, with no guest and no release handed to it, gives the entrance back with no mutex */
inline void
lock_group::give_back ()
{
  assert (m_guest.load (std::memory_order_acquire) == NULL);
  assert (m_pending_release.load (std::memory_order_acquire) == NULL);
  m_owner.store (NULL, std::memory_order_seq_cst);
  /* see wait_for_owner () */
  if (m_waiters.load (std::memory_order_seq_cst) > 0)
    {
      pthread_mutex_lock (&m_mutex);
      pthread_cond_broadcast (&m_cond);
      pthread_mutex_unlock (&m_mutex);
    }
}

/* a release handed over under the mutex is taken before the owner is cleared under it, so none is left behind */
void
lock_group::leave (THREAD_ENTRY *thread_p)
{
  LK_PENDING_RELEASE *release, *next, *handed;

  while (true)
    {
      pthread_mutex_lock (&m_mutex);
      release = m_pending_release.exchange (NULL, std::memory_order_acq_rel);
      if (release == NULL)
	{
	  assert (m_guest.load (std::memory_order_relaxed) == NULL);
	  m_owner.store (NULL, std::memory_order_seq_cst);
	  /* all: a waiter woken alone could return on an abort without passing the entrance on */
	  pthread_cond_broadcast (&m_cond);
	  pthread_mutex_unlock (&m_mutex);
	  return;
	}
      pthread_mutex_unlock (&m_mutex);

      /* handed at the head: run in the order handed, so an instance is released before its class */
      for (handed = NULL; release != NULL; release = next)
	{
	  next = release->next;
	  release->next = handed;
	  handed = release;
	}

      /* each re-enters as the owner */
      m_depth = 1;
      for (release = handed; release != NULL; release = next)
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
      m_depth = 0;
    }
}

/* the owner, or a page-fixed sibling, may wait for the guest */
void
lock_group::guest_leave ()
{
  pthread_mutex_lock (&m_mutex);
  m_guest.store (NULL, std::memory_order_release);
  pthread_cond_broadcast (&m_cond);
  pthread_mutex_unlock (&m_mutex);
}

/*
 * wait, with m_mutex held, until the guest has left. Once the transaction is to abort the guest's lock wait is ended
 * here: the deadlock detector reads the victim's waiters before it marks the victim, so a guest that goes to sleep in
 * between is woken by no one else.
 */
void
lock_group::wait_guest_locked ()
{
  THREAD_ENTRY *guest;

  while ((guest = m_guest.load (std::memory_order_relaxed)) != NULL)
    {
      if (abort_reason () != NO_ERROR)
	{
	  /* the guest's thread mutex is not taken under the group mutex */
	  pthread_mutex_unlock (&m_mutex);
	  lock_interrupt_lockwait (guest, m_tran_index);
	  pthread_mutex_lock (&m_mutex);
	  if (m_guest.load (std::memory_order_relaxed) != guest)
	    {
	      continue;
	    }
	}
      m_waiters.fetch_add (1, std::memory_order_seq_cst);
      timed_wait ();
      m_waiters.fetch_sub (1, std::memory_order_seq_cst);
    }
}

/* an interrupt for the siblings that the transaction's own may no longer show; kept until the sections close */
void
lock_group::keep_interrupt ()
{
  pthread_mutex_lock (&m_mutex);
  if (in_section ())
    {
      m_interrupted.store (true, std::memory_order_release);
      pthread_cond_broadcast (&m_cond);
    }
  pthread_mutex_unlock (&m_mutex);
}

/*
 * the owner wakes to the transaction's state only once the guest has left. This wait closes no cycle: the guest never
 * waits for the owner, and the only page it may fix in here, the root class heap page it reads for a lock timeout
 * message (lock_set_error_for_timeout ()), it reads with a read latch released before it returns; the owner holds no
 * page here but one safe to hold in a lock wait (lock_suspend ()).
 */
void
lock_group::wait_for_guest ()
{
  pthread_mutex_lock (&m_mutex);
  wait_guest_locked ();
  m_owner_blocked.store (false, std::memory_order_release);
  pthread_mutex_unlock (&m_mutex);
}

/*
 * step_out () - a thread leaves the entrance it holds as owner or guest, keeping its depth to come back with
 *   return: the depth, or 0 if it held none
 *
 * Note: an owner lets its guest leave first, so no one finds the owner gone while a guest stays.
 */
int
lock_group::step_out (THREAD_ENTRY *thread_p)
{
  int depth = 0;

  pthread_mutex_lock (&m_mutex);
  if (m_owner.load (std::memory_order_acquire) == thread_p)
    {
      wait_guest_locked ();
      m_owner_blocked.store (false, std::memory_order_release);
      depth = m_depth;
      pthread_mutex_unlock (&m_mutex);
      /* the releases handed to it go with it, not to whoever enters next */
      leave (thread_p);
      return depth;
    }
  else if (m_guest.load (std::memory_order_relaxed) == thread_p)
    {
      depth = m_guest_depth;
      m_guest_depth = 0;
      m_guest.store (NULL, std::memory_order_release);
      pthread_cond_broadcast (&m_cond);
    }
  pthread_mutex_unlock (&m_mutex);
  return depth;
}

/* back in after step_out (), as owner or guest, whichever is free first; no abort check, as it cleans up */
void
lock_group::step_back (THREAD_ENTRY *thread_p, int depth)
{
  if (depth == 0)
    {
      return;
    }
  pthread_mutex_lock (&m_mutex);
  while (true)
    {
      if (try_own (thread_p))
	{
	  m_depth = depth;
	  break;
	}
      if (m_owner_sleeping.load (std::memory_order_relaxed) && m_guest.load (std::memory_order_relaxed) == NULL)
	{
	  m_guest.store (thread_p, std::memory_order_release);
	  m_guest_depth = depth;
	  break;
	}
      (void) wait_for_owner ();
    }
  pthread_mutex_unlock (&m_mutex);
}

/*
 * The transaction's one wait start in the WFG, while its owner and its guest may both sleep: the earlier start, as the
 * deadlock detector skips an edge older than the node's wait, and 0 only once neither waits. A thread of the group
 * updates it under the group mutex, as the two can sleep or wake at once; a waker clears a thread's lockwait before
 * that thread's end_wait (), and leaves the slot to it (lock_wake_mvccid_waiters ()).
 */
void
lock_group::start_wait (bool in_group, INT64 stime)
{
  INT64 *node_stime = lock_get_wait_stime (m_tran_index);

  if (in_group)
    {
      pthread_mutex_lock (&m_mutex);
    }
  if (*node_stime == 0 || *node_stime > stime)
    {
      *node_stime = stime;
    }
  if (in_group)
    {
      pthread_mutex_unlock (&m_mutex);
    }
}

void
lock_group::end_wait (bool in_group, const THREAD_ENTRY *thread_p)
{
  const THREAD_ENTRY *owner, *guest, *sibling;
  INT64 *node_stime = lock_get_wait_stime (m_tran_index);

  if (!in_group)
    {
      *node_stime = 0;
      return;
    }
  pthread_mutex_lock (&m_mutex);
  owner = m_owner.load (std::memory_order_acquire);
  guest = m_guest.load (std::memory_order_relaxed);
  sibling = (owner == thread_p) ? guest : owner;
  *node_stime = (sibling != NULL && sibling->lockwait != NULL) ? sibling->lockwait_stime : 0;
  pthread_mutex_unlock (&m_mutex);
}

/* an interrupt one sleeping thread of the transaction consumed, for the deadlock detector to wake the other with */
bool
lock_group::holds_interrupt () const
{
  return in_section () && m_interrupted.load (std::memory_order_acquire);
}

/* the owner sleeps for another transaction: a conditional request no longer waits for it */
void
lock_group::owner_sleeps (const LK_RES_KEY *wait_key, UINT64 held_row)
{
  pthread_mutex_lock (&m_mutex);
  m_owner_sleeping.store (true, std::memory_order_relaxed);
  m_owner_blocked.store (true, std::memory_order_release);
  m_owner_wait_key = *wait_key;
  m_owner_held_row = held_row;
  if (m_waiters.load (std::memory_order_seq_cst) > 0)
    {
      pthread_cond_broadcast (&m_cond);
    }
  pthread_mutex_unlock (&m_mutex);
}

/*
 * back from a lock wait: no guest is let in once the owner is awake, and an interrupt that ended the wait stays for the
 * siblings, as the deadlock detector cleared the transaction's own
 */
void
lock_group::wakes (bool is_owner, bool interrupted)
{
  if (is_owner)
    {
      pthread_mutex_lock (&m_mutex);
      m_owner_sleeping.store (false, std::memory_order_relaxed);
      /* still blocked only by a guest let in while it slept; none comes in from now */
      m_owner_blocked.store (m_guest.load (std::memory_order_relaxed) != NULL, std::memory_order_release);
      pthread_mutex_unlock (&m_mutex);
    }
  if (interrupted)
    {
      keep_interrupt ();
    }
}

#if !defined (NDEBUG)
/* outside its sections the threads of a transaction enter unchecked; a second one inside at once is a bug */
bool
lock_group::solo_enter (THREAD_ENTRY *thread_p)
{
  THREAD_ENTRY *expected = NULL;
  THREAD_ENTRY *owner, *guest, *inside;

  if (m_tran_index <= LOG_SYSTEM_TRAN_INDEX)
    {
      /* the system transaction is many threads' and opens no section */
      return false;
    }
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }
  owner = m_owner.load (std::memory_order_acquire);
  guest = m_guest.load (std::memory_order_acquire);
  inside = (owner != NULL && owner != thread_p) ? owner : ((guest != thread_p) ? guest : NULL);

  /* a pool thread runs only while its leader's section is open */
  assert (!thread_is_px_worker (thread_p));

  if (m_solo_owner.load (std::memory_order_acquire) == thread_p)
    {
      m_solo_depth++;
      return true;
    }
  if (inside != NULL
      || !m_solo_owner.compare_exchange_strong (expected, thread_p, std::memory_order_acq_rel,
	  std::memory_order_acquire))
    {
      _er_log_debug (ARG_FILE_LINE,
		     "lock_group: thread %d of transaction %d enters the lock manager while thread %d is in it\n",
		     thread_p->index, m_tran_index, (inside != NULL) ? inside->index : expected->index);
      assert (false);
      return false;
    }
  m_solo_depth = 1;
  return true;
}

void
lock_group::solo_leave ()
{
  if (--m_solo_depth == 0)
    {
      m_solo_owner.store (NULL, std::memory_order_release);
    }
}

/* a victim waiting for its siblings lets them in (lock_suspend_internal ()) */
int
lock_group::solo_step_out (THREAD_ENTRY *thread_p)
{
  int depth = 0;

  if (m_solo_owner.load (std::memory_order_acquire) == thread_p)
    {
      depth = m_solo_depth;
      m_solo_depth = 0;
      m_solo_owner.store (NULL, std::memory_order_release);
    }
  return depth;
}

void
lock_group::solo_step_back (THREAD_ENTRY *thread_p, int depth)
{
  if (depth > 0 && solo_enter (thread_p))
    {
      m_solo_depth = depth;
    }
}

/* nothing else of the transaction is in the lock manager: a walk of its hold lists needs no hold_mutex */
bool
lock_group::is_alone (THREAD_ENTRY *thread_p) const
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }
  if (!in_section ())
    {
      return true;
    }
  if (m_owner.load (std::memory_order_acquire) == thread_p)
    {
      return m_guest.load (std::memory_order_acquire) == NULL;
    }
  return m_guest.load (std::memory_order_acquire) == thread_p && m_owner_sleeping.load (std::memory_order_acquire);
}
#endif /* !NDEBUG */

void
lock_group::section_begin ()
{
  m_sections.fetch_add (1, std::memory_order_seq_cst);
}

void
lock_group::section_end ()
{
  int sections;

  pthread_mutex_lock (&m_mutex);
  sections = m_sections.fetch_sub (1, std::memory_order_seq_cst) - 1;
  assert (sections >= 0);
  if (sections == 0)
    {
      m_interrupted.store (false, std::memory_order_release);
    }
  pthread_mutex_unlock (&m_mutex);
}

void
lock_group::clear ()
{
  THREAD_ENTRY *owner, *guest;
  LK_PENDING_RELEASE *release, *next;
  int sections;

  pthread_mutex_lock (&m_mutex);
  sections = m_sections.exchange (0, std::memory_order_seq_cst);
  owner = m_owner.exchange (NULL, std::memory_order_seq_cst);
  guest = m_guest.exchange (NULL, std::memory_order_seq_cst);
  m_depth = 0;
  m_guest_depth = 0;
  m_owner_sleeping.store (false, std::memory_order_relaxed);
  m_owner_blocked.store (false, std::memory_order_release);
  m_interrupted.store (false, std::memory_order_release);
  m_abort_deferred.store (false, std::memory_order_release);
  release = m_pending_release.exchange (NULL, std::memory_order_acq_rel);
  pthread_mutex_unlock (&m_mutex);

  for (; release != NULL; release = next)
    {
      next = release->next;
      free (release);
    }
  if (sections != 0 || owner != NULL || guest != NULL)
    {
      _er_log_debug (ARG_FILE_LINE, "lock_group: transaction index %d is cleared with %d parallel sections open%s\n",
		     m_tran_index, sections, (owner != NULL || guest != NULL) ? " and a thread inside" : "");
      assert (false);
    }
}

/*
 * defer_abort () - leave the abort of the transaction to a request that ends it later
 *   to_pl_caller(in): also with no section open, for a deadlock victim's abort (lock_group_defer_abort_to_pl_caller ())
 *
 * Note: under the mutex section_end () takes, so a section closing meanwhile keeps no stale interrupt. With no
 *	 section, no interrupt: the PL calls would report it as ER_INTERRUPTED over the deadlock error. Their statements
 *	 fail as they return instead (lock_group_fail_if_abort_deferred ()).
 */
bool
lock_group::defer_abort (THREAD_ENTRY *thread_p, bool to_pl_caller)
{
  bool section;

  pthread_mutex_lock (&m_mutex);
  section = in_section ();
  /* with no section, only a deadlock victim's abort, not one for another error such as ER_DB_NO_MODIFICATIONS */
  if (!section && ! (to_pl_caller && (lock_is_tran_deadlock_victim (m_tran_index) || is_abort_deferred ())))
    {
      pthread_mutex_unlock (&m_mutex);
      return false;
    }
  m_abort_deferred.store (true, std::memory_order_release);
  if (!section)
    {
      pthread_mutex_unlock (&m_mutex);
      return true;
    }
  m_interrupted.store (true, std::memory_order_release);
  pthread_cond_broadcast (&m_cond);
  pthread_mutex_unlock (&m_mutex);

  /* the owner's query stops at its next row even if it asks for no lock */
  logtb_set_tran_index_interrupt (thread_p, m_tran_index, true);
  return true;
}

/*
 * enter () - a lock call of the transaction while it is gated
 *   return: LK_GRANTED once inside, or why not: LK_NOTGRANTED_DUE_ABORTED or _ERROR with the error set,
 *	     LK_NOTGRANTED_DUE_TIMEOUT with none for a conditional request that met the owner asleep, or LK_NOTGRANTED
 *	     for a release handed to the owner, which runs it before it leaves
 *   guest_key(in): the resource, for a request that may enter as the guest
 *   release(in): a release that may be handed to the owner
 */
int
lock_group::enter (THREAD_ENTRY *thread_p, LK_GROUP_REQUEST request, const LK_RES_KEY *guest_key,
		   const LK_PENDING_RELEASE *release)
{
  LK_PENDING_RELEASE *handed = NULL;
  int error;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }
  if (is_owner (thread_p))
    {
      m_depth++;
      return LK_GRANTED;
    }
  if (is_guest (thread_p))
    {
      m_guest_depth++;
      return LK_GRANTED;
    }

  /*
   * The entrance free: taken with no mutex. Owner NULL implies no guest, the owner neither asleep nor blocked, and no
   * release handed over, as only a blocked owner is handed one and every way out drains them first. An acquisition
   * checks for an abort again once it is the owner: a deferred abort can land between the check and the CAS, the spin
   * widening that window, and lock_group_defer_abort () has returned by then.
   */
  if ((request == LK_GROUP_RELEASE || abort_reason () == NO_ERROR) && (try_own (thread_p) || spin_to_own (thread_p)))
    {
      error = (request == LK_GROUP_RELEASE) ? NO_ERROR : abort_reason ();
      if (error != NO_ERROR)
	{
	  give_back ();
	  lock_group_set_abort_error (m_tran_index, error);
	  return (error == ER_INTERRUPTED) ? LK_NOTGRANTED_DUE_ERROR : LK_NOTGRANTED_DUE_ABORTED;
	}
      m_depth = 1;
      return LK_GRANTED;
    }

  if (release != NULL && pgbuf_has_perm_pages_fixed (thread_p))
    {
      /* allocated before the mutex, under which no other lock is taken */
      handed = (LK_PENDING_RELEASE *) malloc (sizeof (LK_PENDING_RELEASE));
      if (handed != NULL)
	{
	  *handed = *release;
	}
    }

  pthread_mutex_lock (&m_mutex);
  while (true)
    {
      if (request != LK_GROUP_RELEASE && in_section ())
	{
	  /* also when nothing was waited for: a sibling may have left the entrance as a victim */
	  error = abort_reason ();
	  if (error != NO_ERROR)
	    {
	      pthread_mutex_unlock (&m_mutex);
	      lock_group_set_abort_error (m_tran_index, error);
	      return (error == ER_INTERRUPTED) ? LK_NOTGRANTED_DUE_ERROR : LK_NOTGRANTED_DUE_ABORTED;
	    }
	}
      if (try_own (thread_p))
	{
	  m_depth = 1;
	  break;
	}
      if (m_owner_sleeping.load (std::memory_order_relaxed) && guest_key != NULL
	  && m_guest.load (std::memory_order_relaxed) == NULL
	  && lock_res_key_compare ((void *) guest_key, &m_owner_wait_key) != 0
	  && (guest_key->type != LOCK_RESOURCE_INSTANCE || lock_pack_row_oid (&guest_key->oid) != m_owner_held_row))
	{
	  m_guest.store (thread_p, std::memory_order_release);
	  m_guest_depth = 1;
	  break;
	}
      /* the owner awake but waiting for a guest asleep behind a catalog updater is no shorter a wait */
      if (m_owner_blocked.load (std::memory_order_acquire))
	{
	  if (request == LK_GROUP_ACQUIRE_COND)
	    {
	      /* the owner's sleep is another transaction's to end; a conditional request does not share it */
	      pthread_mutex_unlock (&m_mutex);
	      free (handed);
#if !defined (NDEBUG)
	      er_print_callstack (ARG_FILE_LINE, "lock_group: tran %d: conditional request met the owner asleep\n",
				  m_tran_index);
#endif
	      return LK_NOTGRANTED_DUE_TIMEOUT;
	    }
	  /* the deadlock detector would not see a page fixed while waiting for an owner asleep */
	  if (handed != NULL)
	    {
	      handed->next = m_pending_release.load (std::memory_order_relaxed);
	      m_pending_release.store (handed, std::memory_order_release);
	      pthread_mutex_unlock (&m_mutex);
	      return LK_NOTGRANTED;
	    }
	  /* a guest-family request waits behind the guest, or the owner on its own resource: a catalog updater that
	   * needs no user page, as lock_is_safe_lock_with_page () assumes */
	  assert (request == LK_GROUP_RELEASE || guest_key != NULL || !pgbuf_has_perm_pages_fixed (thread_p));
	}
      (void) wait_for_owner ();
    }
  assert (m_guest.load (std::memory_order_relaxed) == NULL || m_owner.load (std::memory_order_acquire) != NULL);
  pthread_mutex_unlock (&m_mutex);
  free (handed);
  return LK_GRANTED;
}

/* by the role held now: a deadlock victim may come back in the other one (lock_suspend_internal ()) */
void
lock_group::exit (THREAD_ENTRY *thread_p)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }
  if (is_owner (thread_p))
    {
      if (--m_depth > 0)
	{
	  return;
	}
      /* no release can be handed over from here on: the owner is awake with no guest, so not blocked */
      if (m_pending_release.load (std::memory_order_acquire) != NULL)
	{
	  leave (thread_p);
	  return;
	}
      give_back ();
    }
  else
    {
      assert (is_guest (thread_p));
      if (--m_guest_depth == 0)
	{
	  guest_leave ();
	}
    }
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

static void
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

/* the key of a catalog directory OID lock, which may enter as the guest, or NULL */
static const LK_RES_KEY *
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
static const LK_RES_KEY *
lock_group_row_release_key (const OID * oid, const OID * class_oid, LK_RES_KEY * key)
{
  if (oid == NULL || class_oid == NULL || OID_IS_ROOTOID (oid) || OID_IS_ROOTOID (class_oid))
    {
      return NULL;
    }
  *key = lock_create_search_key (oid, class_oid);
  return key;
}

/* enter () for an acquisition of an object; its guest key is made here, only while the transaction is gated */
int
lock_group::enter_object (THREAD_ENTRY *thread_p, LK_GROUP_REQUEST request, const OID *oid, const OID *class_oid)
{
  LK_RES_KEY key;

  return enter (thread_p, request, lock_group_dir_oid_key (oid, class_oid, &key), NULL);
}

/* enter () for a release of an object; a transient one changes the statement's counts and never enters as the guest */
int
lock_group::enter_release (THREAD_ENTRY *thread_p, LK_PENDING_RELEASE_TYPE type, const OID *oid, const OID *class_oid,
			   LOCK lock, bool force)
{
  LK_PENDING_RELEASE release;
  LK_RES_KEY key;

  lock_pending_release_init (&release, type, oid, class_oid, lock, force);
  return enter (thread_p, LK_GROUP_RELEASE,
		(type == LK_PENDING_UNLOCK_TRANSIENT) ? NULL : lock_group_row_release_key (oid, class_oid, &key),
		&release);
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
      lock_get_tran_group (tran_index)->section_begin ();
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
  if (tran_index > LOG_SYSTEM_TRAN_INDEX)
    {
      lock_get_tran_group (tran_index)->section_end ();
    }
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
  if (tran_index > LOG_SYSTEM_TRAN_INDEX && lock_has_tran_lock (tran_index))
    {
      lock_get_tran_group (tran_index)->clear ();
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
  return tran_index > LOG_SYSTEM_TRAN_INDEX && lock_get_tran_group (tran_index)->in_section ();
#else /* SERVER_MODE */
  return false;
#endif /* SERVER_MODE */
}

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
  return tran_index > LOG_SYSTEM_TRAN_INDEX && lock_get_tran_group (tran_index)->defer_abort (thread_p, false);
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
  return tran_index > LOG_SYSTEM_TRAN_INDEX && lock_get_tran_group (tran_index)->defer_abort (thread_p, true);
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
  return tran_index > LOG_SYSTEM_TRAN_INDEX && lock_get_tran_group (tran_index)->is_abort_deferred ();
#else /* SERVER_MODE */
  return false;
#endif /* SERVER_MODE */
}
