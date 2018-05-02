/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * thread.c - Thread management module at the server
 */

#ident "$Id$"

#if !defined(WINDOWS)
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#endif

#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>

#if defined(WINDOWS)
#include <process.h>
#else /* WINDOWS */
#include <sys/time.h>
#include <unistd.h>
#endif /* WINDOWS */

#include "thread.h"
#include "boot_sr.h"
#include "server_support.h"
#include "session.h"
#if defined(WINDOWS)
#include "wintcp.h"
#else /* WINDOWS */
#include "tcp.h"
#endif /* WINDOWS */
#include "vacuum.h"

#if defined(WINDOWS)
#include "heartbeat.h"
#endif

#include "thread_manager.hpp"

/* Thread Manager structure */
typedef struct thread_manager THREAD_MANAGER;
struct thread_manager
{
  int num_total;
  int num_workers;
  bool initialized;
};

static const int THREAD_RETRY_MAX_SLAM_TIMES = 10;

#if defined(HPUX)
static __thread THREAD_ENTRY *tsd_ptr;
#else /* HPUX */
static pthread_key_t thread_Thread_key;
#endif /* HPUX */

static THREAD_MANAGER thread_Manager;

#define THREAD_RC_TRACK_VMEM_THRESHOLD_AMOUNT	      32767
#define THREAD_RC_TRACK_PGBUF_THRESHOLD_AMOUNT	      1024
#define THREAD_RC_TRACK_QLIST_THRESHOLD_AMOUNT	      1024
#define THREAD_RC_TRACK_CS_THRESHOLD_AMOUNT	      1024

#define THREAD_METER_DEMOTED_READER_FLAG	      0x40
#define THREAD_METER_MAX_ENTER_COUNT		      0x3F

#define THREAD_METER_IS_DEMOTED_READER(rlock) \
	(rlock & THREAD_METER_DEMOTED_READER_FLAG)
#define THREAD_METER_STRIP_DEMOTED_READER_FLAG(rlock) \
	(rlock & ~THREAD_METER_DEMOTED_READER_FLAG)
#define THREAD_METER_WITH_DEMOTED_READER_FLAG(rlock) \
	(rlock | THREAD_METER_DEMOTED_READER_FLAG)

#define THREAD_RC_TRACK_ASSERT(thread_p, outfp, cond) \
  do \
    { \
      if (!(cond)) \
        { \
          thread_rc_track_dump_all (thread_p, outfp); \
        } \
      assert_release (cond); \
    } \
  while (0)

#define THREAD_RC_TRACK_METER_ASSERT(thread_p, outfp, meter, cond) \
  do \
    { \
      if (!(cond)) \
        { \
          thread_rc_track_meter_dump (thread_p, outfp, meter); \
        } \
      assert_release (cond); \
    } \
  while (0)

static int thread_wakeup_internal (THREAD_ENTRY * thread_p, int resume_reason, bool had_mutex);

static void thread_rc_track_clear_all (THREAD_ENTRY * thread_p);
static int thread_rc_track_meter_check (THREAD_ENTRY * thread_p, THREAD_RC_METER * meter, THREAD_RC_METER * prev_meter);
static int thread_rc_track_check (THREAD_ENTRY * thread_p, int id);
static THREAD_RC_TRACK *thread_rc_track_alloc (THREAD_ENTRY * thread_p);
static void thread_rc_track_free (THREAD_ENTRY * thread_p, int id);
static INT32 thread_rc_track_amount_helper (THREAD_ENTRY * thread_p, int rc_idx);
static const char *thread_rc_track_rcname (int rc_idx);
static const char *thread_rc_track_mgrname (int mgr_idx);
static void thread_rc_track_meter_dump (THREAD_ENTRY * thread_p, FILE * outfp, THREAD_RC_METER * meter);
static void thread_rc_track_dump (THREAD_ENTRY * thread_p, FILE * outfp, THREAD_RC_TRACK * track, int depth);
static int thread_check_kill_tran_auth (THREAD_ENTRY * thread_p, int tran_id, bool * has_authoriation);
static INT32 thread_rc_track_threshold_amount (int rc_idx);
static bool thread_rc_track_is_enabled (THREAD_ENTRY * thread_p);

#if !defined(NDEBUG)
static void thread_rc_track_meter_at (THREAD_RC_METER * meter, const char *caller_file, int caller_line, int amount,
				      void *ptr);
static void thread_rc_track_meter_assert_csect_dependency (THREAD_ENTRY * thread_p, THREAD_RC_METER * meter, int amount,
							   void *ptr);
static void thread_rc_track_meter_assert_csect_usage (THREAD_ENTRY * thread_p, THREAD_RC_METER * meter, int enter_mode,
						      void *ptr);
#endif /* !NDEBUG */

/*
 * Thread Specific Data management
 *
 * All kind of thread has its own information like request id, error code,
 * synchronization informations, etc. We use THREAD_ENTRY structure
 * which saved as TSD(thread specific data) to manage these informations.
 * Global thread manager(thread_mgr) has an array of these entries which is
 * initialized by the 'thread_mgr'.
 * Each worker thread picks one up from this array.
 */

#if defined(HPUX)
/*
 * thread_get_thread_entry_info() - retrieve TSD of its own.
 *   return: thread entry
 */
THREAD_ENTRY *
thread_get_thread_entry_info ()
{
#if defined (SERVER_MODE)
  assert (tsd_ptr != NULL);
#endif
  if (tsd_ptr == NULL)
    {
      return &cubthread::get_manager ()->get_entry ();
    }
  else
    {
      return tsd_ptr;
    }
}
#else /* HPUX */
/*
 * thread_initialize_key() - allocates a key for TSD
 *   return: 0 if no error, or error code
 */
int
thread_initialize_key (void)
{
  int r;

  r = pthread_key_create (&thread_Thread_key, NULL);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_KEY_CREATE, 0);
      return ER_CSS_PTHREAD_KEY_CREATE;
    }
  return r;
}

/*
 * thread_set_thread_entry_info() - associates TSD with entry
 *   return: 0 if no error, or error code
 *   entry_p(in): thread entry
 */
int
thread_set_thread_entry_info (THREAD_ENTRY * entry_p)
{
  int r;

  r = pthread_setspecific (thread_Thread_key, (void *) entry_p);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_SETSPECIFIC, 0);
      return ER_CSS_PTHREAD_SETSPECIFIC;
    }

  return r;
}

/*
 * thread_get_thread_entry_info() - retrieve TSD of its own.
 *   return: thread entry
 */
THREAD_ENTRY *
thread_get_thread_entry_info (void)
{
  void *p;

  p = pthread_getspecific (thread_Thread_key);
#if defined (SERVER_MODE)
  if (p == NULL)
    {
      p = (void *) &(cubthread::get_manager ()->get_entry ());
    }
  assert (p != NULL);
#endif
  return (THREAD_ENTRY *) p;
}
#endif /* HPUX */

/*
 * Thread Manager related functions
 *
 * Global thread manager, thread_mgr, related functions. It creates/destroys
 * TSD and takes control over actual threads, for example master, worker.
 */

/*
 * thread_is_manager_initialized() -
 *   return:
 */
int
thread_is_manager_initialized (void)
{
  return thread_Manager.initialized;
}

/*
 * thread_initialize_manager() - Create and initialize all necessary threads.
 *   return: 0 if no error, or error code
 *
 * Note: It includes a main thread, service handler etc.
 *       Some other threads like signal handler might be needed later.
 */
int
thread_initialize_manager (size_t & total_thread_count)
{
  int i, r;

  assert (NUM_NORMAL_TRANS >= 10);
  assert (!thread_Manager.initialized);

  thread_Manager.num_workers = 0;
  thread_Manager.num_total = thread_Manager.num_workers;

  /* initialize lock-free transaction systems */
  r = lf_initialize_transaction_systems (thread_Manager.num_total + (int) cubthread::get_max_thread_count ());
  if (r != NO_ERROR)
    {
      return r;
    }

  thread_Manager.initialized = true;

  total_thread_count = thread_Manager.num_total;

  return NO_ERROR;
}

/*
 * thread_final_manager() -
 *   return: void
 */
void
thread_final_manager (void)
{
  lf_destroy_transaction_systems ();

#ifndef HPUX
  pthread_key_delete (thread_Thread_key);
#endif /* not HPUX */
}

/*
 * thread_return_transaction_entry() - return previously requested entries
 *   return: error code
 *   entry_p(in): thread entry
 */
int
thread_return_transaction_entry (THREAD_ENTRY * entry_p)
{
  int i, error = NO_ERROR;

  for (i = 0; i < THREAD_TS_COUNT; i++)
    {
      if (entry_p->tran_entries[i] != NULL)
	{
	  error = lf_tran_return_entry (entry_p->tran_entries[i]);
	  if (error != NO_ERROR)
	    {
	      break;
	    }
	  entry_p->tran_entries[i] = NULL;
	}
    }
  return error;
}

/*
 * thread_return_all_transactions_entries() - return previously requested entries for all transactions
 *   return:
 */
int
thread_return_all_transactions_entries (void)
{
  int error = NO_ERROR;

  if (!thread_Manager.initialized)
    {
      return NO_ERROR;
    }

  thread_return_transaction_entry (cubthread::get_main_entry ());
  for (THREAD_ENTRY * entry_iter = thread_iterate (NULL); entry_iter != NULL; entry_iter = thread_iterate (entry_iter))
    {
      error = thread_return_transaction_entry (entry_iter);
      if (error != NO_ERROR)
	{
	  assert (false);
	  break;
	}
    }

  return error;
}

/*
 * Thread entry related functions
 * Information retrieval modules.
 * Inter thread synchronization modules.
 */

/*
 * thread_find_entry_by_tran_index_except_me() -
 *   return:
 *   tran_index(in):
 */
THREAD_ENTRY *
thread_find_entry_by_tran_index_except_me (int tran_index)
{
  for (THREAD_ENTRY * thread_p = thread_iterate (NULL); thread_p != NULL; thread_p = thread_iterate (thread_p))
    {
      if (thread_p->tran_index == tran_index && !thread_p->is_on_current_thread ())
	{
	  return thread_p;
	}
    }
  return NULL;
}

/*
 * thread_find_entry_by_tran_index() -
 *   return:
 *   tran_index(in):
 */
THREAD_ENTRY *
thread_find_entry_by_tran_index (int tran_index)
{
  // todo: is this safe? afaik, we could have multiple threads for same transaction
  for (THREAD_ENTRY * thread_p = thread_iterate (NULL); thread_p != NULL; thread_p = thread_iterate (thread_p))
    {
      if (thread_p->tran_index == tran_index)
	{
	  return thread_p;
	}
    }
  return NULL;
}

/*
 * thread_get_current_entry_index() -
 *   return:
 */
int
thread_get_current_entry_index (void)
{
  THREAD_ENTRY *thread_p;

  thread_p = thread_get_thread_entry_info ();
  assert (thread_p != NULL);

  return thread_p->index;
}

/*
 * thread_get_current_session_id () - get session id for current thread
 *   return: session id
 */
SESSION_ID
thread_get_current_session_id (void)
{
  THREAD_ENTRY *thread_p;

  thread_p = thread_get_thread_entry_info ();
  assert (thread_p != NULL);

  if (thread_p->conn_entry == NULL)
    {
      return 0;
    }
  return thread_p->conn_entry->session_id;
}

/*
 * thread_get_current_tran_index() - get transaction index if current
 *                                       thread
 *   return:
 */
int
thread_get_current_tran_index (void)
{
  THREAD_ENTRY *thread_p;

  thread_p = thread_get_thread_entry_info ();
  assert (thread_p != NULL);

  return thread_p->tran_index;
}

/*
 * thread_set_current_tran_index() -
 *   return: void
 *   tran_index(in):
 */
void
thread_set_current_tran_index (THREAD_ENTRY * thread_p, int tran_index)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert (thread_p != NULL);

  thread_p->tran_index = tran_index;
}

/*
 * thread_get_current_conn_entry() -
 *   return:
 */
CSS_CONN_ENTRY *
thread_get_current_conn_entry (void)
{
  THREAD_ENTRY *thread_p;

  thread_p = thread_get_thread_entry_info ();
  assert (thread_p != NULL);

  return thread_p->conn_entry;
}

/*
 * thread_lock_entry() -
 *   return:
 *   thread_p(in):
 */
int
thread_lock_entry (THREAD_ENTRY * thread_p)
{
  int r;

  if (thread_p == NULL)
    {
      assert (thread_p != NULL);	/* expects callers pass thread handle */
      thread_p = thread_get_thread_entry_info ();
    }
  assert (thread_p != NULL);

  r = pthread_mutex_lock (&thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_LOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_LOCK;
    }

  return r;
}

/*
 * thread_unlock_entry() -
 *   return:
 *   thread_p(in):
 */
int
thread_unlock_entry (THREAD_ENTRY * thread_p)
{
  int r;

  if (thread_p == NULL)
    {
      assert (thread_p != NULL);	/* expects callers pass thread handle */
      thread_p = thread_get_thread_entry_info ();
    }
  assert (thread_p != NULL);

  r = pthread_mutex_unlock (&thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
    }

  return r;
}

/*
 * thread_suspend_wakeup_and_unlock_entry() -
 *   return:
 *   thread_p(in):
 *   suspended_reason(in):
 *
 * Note: this function must be called by current thread
 *       also, the lock must have already been acquired.
 */
int
thread_suspend_wakeup_and_unlock_entry (THREAD_ENTRY * thread_p, int suspended_reason)
{
  int r;
  int old_status;

  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;

  assert (thread_p->status == TS_RUN || thread_p->status == TS_CHECK);
  old_status = thread_p->status;
  thread_p->status = TS_WAIT;

  thread_p->resume_status = suspended_reason;

  if (thread_p->event_stats.trace_slow_query == true)
    {
      tsc_getticks (&start_tick);
    }

  r = pthread_cond_wait (&thread_p->wakeup_cond, &thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_WAIT, 0);
      return ER_CSS_PTHREAD_COND_WAIT;
    }

  if (thread_p->event_stats.trace_slow_query == true)
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);

      if (suspended_reason == THREAD_LOCK_SUSPENDED)
	{
	  TSC_ADD_TIMEVAL (thread_p->event_stats.lock_waits, tv_diff);
	}
      else if (suspended_reason == THREAD_PGBUF_SUSPENDED)
	{
	  TSC_ADD_TIMEVAL (thread_p->event_stats.latch_waits, tv_diff);
	}
    }

  thread_p->status = old_status;

  r = pthread_mutex_unlock (&thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
    }

  return NO_ERROR;
}

/*
 * thread_suspend_timeout_wakeup_and_unlock_entry() -
 *   return:
 *   thread_p(in):
 *   time_p(in):
 *   suspended_reason(in):
 */
int
thread_suspend_timeout_wakeup_and_unlock_entry (THREAD_ENTRY * thread_p, struct timespec *time_p, int suspended_reason)
{
  int r;
  int old_status;
  int error = NO_ERROR;

  assert (thread_p->status == TS_RUN || thread_p->status == TS_CHECK);
  old_status = thread_p->status;
  thread_p->status = TS_WAIT;

  thread_p->resume_status = suspended_reason;

  r = pthread_cond_timedwait (&thread_p->wakeup_cond, &thread_p->th_entry_lock, time_p);

  if (r != 0 && r != ETIMEDOUT)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_TIMEDWAIT, 0);
      return ER_CSS_PTHREAD_COND_TIMEDWAIT;
    }

  if (r == ETIMEDOUT)
    {
      error = ER_CSS_PTHREAD_COND_TIMEDOUT;
    }

  thread_p->status = old_status;

  r = pthread_mutex_unlock (&thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
    }

  return error;
}

/*
 * thread_wakeup_internal () -
 *   return:
 *   thread_p(in/out):
 *   resume_reason:
 */
static int
thread_wakeup_internal (THREAD_ENTRY * thread_p, int resume_reason, bool had_mutex)
{
  int r = NO_ERROR;

  if (had_mutex == false)
    {
      r = thread_lock_entry (thread_p);
      if (r != 0)
	{
	  return r;
	}
    }

  r = pthread_cond_signal (&thread_p->wakeup_cond);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_SIGNAL, 0);
      if (had_mutex == false)
	{
	  thread_unlock_entry (thread_p);
	}
      return ER_CSS_PTHREAD_COND_SIGNAL;
    }

  thread_p->resume_status = resume_reason;

  if (had_mutex == false)
    {
      r = thread_unlock_entry (thread_p);
    }

  return r;
}

/*
 * thread_check_suspend_reason_and_wakeup_internal () -
 *   return:
 *   thread_p(in):
 *   resume_reason:
 *   suspend_reason:
 *   had_mutex:
 */
static int
thread_check_suspend_reason_and_wakeup_internal (THREAD_ENTRY * thread_p, int resume_reason, int suspend_reason,
						 bool had_mutex)
{
  int r = NO_ERROR;

  if (had_mutex == false)
    {
      r = thread_lock_entry (thread_p);
      if (r != 0)
	{
	  return r;
	}
    }

  if (thread_p->resume_status != suspend_reason)
    {
      r = thread_unlock_entry (thread_p);
      return (r == NO_ERROR) ? ER_FAILED : r;
    }

  r = pthread_cond_signal (&thread_p->wakeup_cond);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_SIGNAL, 0);
      thread_unlock_entry (thread_p);
      return ER_CSS_PTHREAD_COND_SIGNAL;
    }

  thread_p->resume_status = resume_reason;

  r = thread_unlock_entry (thread_p);

  return r;
}


/*
 * thread_wakeup () -
 *   return:
 *   thread_p(in/out):
 *   resume_reason:
 */
int
thread_wakeup (THREAD_ENTRY * thread_p, int resume_reason)
{
  return thread_wakeup_internal (thread_p, resume_reason, false);
}

int
thread_check_suspend_reason_and_wakeup (THREAD_ENTRY * thread_p, int resume_reason, int suspend_reason)
{
  return thread_check_suspend_reason_and_wakeup_internal (thread_p, resume_reason, suspend_reason, false);
}

/*
 * thread_wakeup_already_had_mutex () -
 *   return:
 *   thread_p(in/out):
 *   resume_reason:
 */
int
thread_wakeup_already_had_mutex (THREAD_ENTRY * thread_p, int resume_reason)
{
  return thread_wakeup_internal (thread_p, resume_reason, true);
}

/*
 * thread_wakeup_with_tran_index() -
 *   return:
 *   tran_index(in):
 */
int
thread_wakeup_with_tran_index (int tran_index, int resume_reason)
{
  THREAD_ENTRY *thread_p;
  int r = NO_ERROR;

  thread_p = thread_find_entry_by_tran_index_except_me (tran_index);
  if (thread_p == NULL)
    {
      return r;
    }

  r = thread_wakeup (thread_p, resume_reason);

  return r;
}

/*
 * thread_suspend_with_other_mutex() -
 *   return: 0 if no error, or error code
 *   thread_p(in):
 *   mutex_p():
 *   timeout(in):
 *   to(in):
 *   suspended_reason(in):
 */
int
thread_suspend_with_other_mutex (THREAD_ENTRY * thread_p, pthread_mutex_t * mutex_p, int timeout, struct timespec *to,
				 int suspended_reason)
{
  int r;
  int old_status;
  int error = NO_ERROR;

  assert (thread_p != NULL);
  old_status = thread_p->status;

  r = pthread_mutex_lock (&thread_p->th_entry_lock);
  if (r != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_LOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_LOCK;
    }

  thread_p->status = TS_WAIT;
  thread_p->resume_status = suspended_reason;

  r = pthread_mutex_unlock (&thread_p->th_entry_lock);
  if (r != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
    }

  if (timeout == INF_WAIT)
    {
      r = pthread_cond_wait (&thread_p->wakeup_cond, mutex_p);
    }
  else
    {
      r = pthread_cond_timedwait (&thread_p->wakeup_cond, mutex_p, to);
    }

  /* we should restore thread's status */
  if (r != NO_ERROR)
    {
      error = (r == ETIMEDOUT) ? ER_CSS_PTHREAD_COND_TIMEDOUT : ER_CSS_PTHREAD_COND_WAIT;
      if (timeout == INF_WAIT || r != ETIMEDOUT)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
    }

  r = pthread_mutex_lock (&thread_p->th_entry_lock);
  if (r != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_LOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_LOCK;
    }

  thread_p->status = old_status;

  r = pthread_mutex_unlock (&thread_p->th_entry_lock);
  if (r != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
    }

  return error;
}

/*
 * thread_sleep() - Halts the currently running thread for <milliseconds>
 *   return: void
 *   milliseconds(in): The number of milliseconds for the thread to sleep
 *
 *  Note: Used to temporarly halt the current process.
 */
void
thread_sleep (double milliseconds)
{
#if defined(WINDOWS)
  Sleep ((int) milliseconds);
#else /* WINDOWS */
  struct timeval to;

  to.tv_sec = (int) (milliseconds / 1000);
  to.tv_usec = ((int) (milliseconds * 1000)) % 1000000;

  select (0, NULL, NULL, NULL, &to);
#endif /* WINDOWS */
}

/*
 * thread_get_client_id() - returns the unique client identifier
 *   return: returns the unique client identifier, on error, returns -1
 *
 * Note: WARN: this function doesn't lock on thread_entry
 */
int
thread_get_client_id (THREAD_ENTRY * thread_p)
{
  CSS_CONN_ENTRY *conn_p;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert (thread_p != NULL);

  conn_p = thread_p->conn_entry;
  if (conn_p != NULL)
    {
      return conn_p->client_id;
    }
  else
    {
      return -1;
    }
}

/*
 * thread_get_comm_request_id() - returns the request id that started the current thread
 *   return: returns the comm system request id for the client request that
 *           started the thread. On error, returns -1
 *
 * Note: WARN: this function doesn't lock on thread_entry
 */
unsigned int
thread_get_comm_request_id (THREAD_ENTRY * thread_p)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert (thread_p != NULL);

  return thread_p->rid;
}

bool
thread_belongs_to (THREAD_ENTRY * thread_p, int tran_index, int client_id)
{
  CSS_CONN_ENTRY *conn_p;
  bool does_belong = false;

  (void) pthread_mutex_lock (&thread_p->tran_index_lock);
  if (!thread_p->is_on_current_thread () && thread_p->status != TS_DEAD && thread_p->status != TS_FREE
      && thread_p->status != TS_CHECK)
    {
      conn_p = thread_p->conn_entry;
      if (tran_index == NULL_TRAN_INDEX)
	{
	  // exact match client ID is required
	  does_belong = conn_p != NULL && conn_p->client_id == client_id;
	}
      else if (tran_index == thread_p->tran_index)
	{
	  // match client ID or null connection
	  does_belong = conn_p == NULL || conn_p->client_id == client_id;
	}
    }
  pthread_mutex_unlock (&thread_p->tran_index_lock);
  return does_belong;
}

/*
 * thread_has_threads() - check if any thread is processing job of transaction
 *                          tran_index
 *   return:
 *   tran_index(in):
 *   client_id(in):
 *
 * Note: WARN: this function doesn't lock thread_mgr
 */
int
thread_has_threads (THREAD_ENTRY * caller, int tran_index, int client_id)
{
  int n = 0;

  for (THREAD_ENTRY * thread_p = thread_iterate (NULL); thread_p != NULL; thread_p = thread_iterate (thread_p))
    {
      if (thread_p == caller || thread_p->type != TT_WORKER)
	{
	  continue;
	}
      if (thread_belongs_to (thread_p, tran_index, client_id))
	{
	  n++;
	}
    }

  return n;
}

/*
 * thread_get_info_threads() - get statistics of threads
 *   return: void
 *   num_total_threads(out):
 *   num_worker_threads(out):
 *   num_free_threads(out):
 *   num_suspended_threads(out):
 *
 * Note: Find the number of threads, number of suspended threads, and maximum
 *       of threads that can be created.
 *       WARN: this function doesn't lock threadmgr
 */
void
thread_get_info_threads (int *num_total_threads, int *num_worker_threads, int *num_free_threads,
			 int *num_suspended_threads)
{
  THREAD_ENTRY *thread_p = NULL;

  if (num_total_threads)
    {
      *num_total_threads = thread_num_total_threads ();
    }
  if (num_worker_threads)
    {
      *num_worker_threads = thread_num_worker_threads ();
    }
  if (num_free_threads)
    {
      *num_free_threads = 0;
    }
  if (num_suspended_threads)
    {
      *num_suspended_threads = 0;
    }
  if (num_free_threads || num_suspended_threads)
    {
      for (thread_p = thread_iterate (NULL); thread_p != NULL; thread_p = thread_iterate (thread_p))
	{
	  if (num_free_threads && thread_p->status == TS_FREE)
	    {
	      (*num_free_threads)++;
	    }
	  if (num_suspended_threads && thread_p->status == TS_WAIT)
	    {
	      (*num_suspended_threads)++;
	    }
	}
    }
}

int
thread_num_worker_threads (void)
{
  return thread_Manager.num_workers;
}

int
thread_num_total_threads (void)
{
  return thread_Manager.num_total + (int) cubthread::get_max_thread_count ();
}

/*
 * css_get_private_heap () -
 *   return:
 *   thread_p(in):
 */
HL_HEAPID
css_get_private_heap (THREAD_ENTRY * thread_p)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert (thread_p != NULL);

  return thread_p->private_heap_id;
}

/*
 * css_set_private_heap() -
 *   return:
 *   thread_p(in):
 *   heap_id(in):
 */
HL_HEAPID
css_set_private_heap (THREAD_ENTRY * thread_p, HL_HEAPID heap_id)
{
  HL_HEAPID old_heap_id = 0;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert (thread_p != NULL);

  old_heap_id = thread_p->private_heap_id;
  thread_p->private_heap_id = heap_id;

  return old_heap_id;
}

/*
 * css_get_cnv_adj_buffer() -
 *   return:
 *   idx(in):
 */
ADJ_ARRAY *
css_get_cnv_adj_buffer (int idx)
{
  THREAD_ENTRY *thread_p;

  thread_p = thread_get_thread_entry_info ();
  assert (thread_p != NULL);

  return thread_p->cnv_adj_buffer[idx];
}

/*
 * css_set_cnv_adj_buffer() -
 *   return: void
 *   idx(in):
 *   buffer_p(in):
 */
void
css_set_cnv_adj_buffer (int idx, ADJ_ARRAY * buffer_p)
{
  THREAD_ENTRY *thread_p;

  thread_p = thread_get_thread_entry_info ();
  assert (thread_p != NULL);

  thread_p->cnv_adj_buffer[idx] = buffer_p;
}

/*
 * thread_set_sort_stats_active() -
 *   return:
 *   flag(in):
 */
bool
thread_set_sort_stats_active (THREAD_ENTRY * thread_p, bool flag)
{
  bool old_val = false;

  if (BO_IS_SERVER_RESTARTED ())
    {
      if (thread_p == NULL)
	{
	  thread_p = thread_get_thread_entry_info ();
	}

      old_val = thread_p->sort_stats_active;
      thread_p->sort_stats_active = flag;
    }

  return old_val;
}

/*
 * thread_get_tran_entry () - get specific lock free transaction entry
 *   returns: transaction entry or NULL on error
 *   thread_p(in): thread entry or NULL for current thread
 *   entry(in): transaction entry index
 */
LF_TRAN_ENTRY *
thread_get_tran_entry (THREAD_ENTRY * thread_p, int entry_idx)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);

  if (entry_idx >= 0 && entry_idx < THREAD_TS_LAST)
    {
      return thread_p->tran_entries[entry_idx];
    }
  else
    {
      assert (false);
      return NULL;
    }
}

/*
 * thread_get_sort_stats_active() -
 *   return:
 */
bool
thread_get_sort_stats_active (THREAD_ENTRY * thread_p)
{
  bool ret_val = false;

  if (BO_IS_SERVER_RESTARTED ())
    {
      if (thread_p == NULL)
	{
	  thread_p = thread_get_thread_entry_info ();
	}

      ret_val = thread_p->sort_stats_active;
    }

  return ret_val;
}

/*
 * thread_set_check_interrupt() -
 *   return:
 *   flag(in):
 */
bool
thread_set_check_interrupt (THREAD_ENTRY * thread_p, bool flag)
{
  bool old_val = true;

  if (BO_IS_SERVER_RESTARTED ())
    {
      if (thread_p == NULL)
	{
	  thread_p = thread_get_thread_entry_info ();
	}

      /* safe guard: vacuum workers should not check for interrupt */
      assert (flag == false || !VACUUM_IS_THREAD_VACUUM (thread_p));
      old_val = thread_p->check_interrupt;
      thread_p->check_interrupt = flag;
    }

  return old_val;
}

/*
 * thread_get_check_interrupt() -
 *   return:
 */
bool
thread_get_check_interrupt (THREAD_ENTRY * thread_p)
{
  bool ret_val = true;

  if (BO_IS_SERVER_RESTARTED ())
    {
      if (thread_p == NULL)
	{
	  thread_p = thread_get_thread_entry_info ();
	}

      ret_val = thread_p->check_interrupt;
    }

  return ret_val;
}

/*
 * thread_slam_tran_index() -
 *   return:
 *   tran_index(in):
 */
void
thread_slam_tran_index (THREAD_ENTRY * thread_p, int tran_index)
{
  logtb_set_tran_index_interrupt (thread_p, tran_index, true);
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_CONN_SHUTDOWN, 0);
  css_shutdown_conn_by_tran_index (tran_index);
}

/*
 * xthread_kill_tran_index() - Kill given transaction.
 *   return:
 *   kill_tran_index(in):
 *   kill_user(in):
 *   kill_host(in):
 *   kill_pid(id):
 */
int
xthread_kill_tran_index (THREAD_ENTRY * thread_p, int kill_tran_index, char *kill_user_p, char *kill_host_p,
			 int kill_pid)
{
  char *slam_progname_p;	/* Client program name for tran */
  char *slam_user_p;		/* Client user name for tran */
  char *slam_host_p;		/* Client host for tran */
  int slam_pid;			/* Client process id for tran */
  bool signaled = false;
  int error_code = NO_ERROR;
  bool killed = false;
  int i;

  if (kill_tran_index == NULL_TRAN_INDEX || kill_user_p == NULL || kill_host_p == NULL || strcmp (kill_user_p, "") == 0
      || strcmp (kill_host_p, "") == 0)
    {
      /* 
       * Not enough information to kill specific transaction..
       *
       * For now.. I am setting an er_set..since I have so many files out..and
       * I cannot compile more junk..
       */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_KILL_BAD_INTERFACE, 0);
      return ER_CSS_KILL_BAD_INTERFACE;
    }

  if (kill_tran_index == LOG_SYSTEM_TRAN_INDEX)
    {
      // cannot kill system transaction; not even if this is dba
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_KILL_TR_NOT_ALLOWED, 1, kill_tran_index);
      return ER_KILL_TR_NOT_ALLOWED;
    }

  signaled = false;
  for (i = 0; i < THREAD_RETRY_MAX_SLAM_TIMES && error_code == NO_ERROR && !killed; i++)
    {
      if (logtb_find_client_name_host_pid (kill_tran_index, &slam_progname_p, &slam_user_p, &slam_host_p, &slam_pid) !=
	  NO_ERROR)
	{
	  if (signaled == false)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_KILL_UNKNOWN_TRANSACTION, 4, kill_tran_index,
		      kill_user_p, kill_host_p, kill_pid);
	      error_code = ER_CSS_KILL_UNKNOWN_TRANSACTION;
	    }
	  else
	    {
	      killed = true;
	    }
	  break;
	}

      if (kill_pid == slam_pid && strcmp (kill_user_p, slam_user_p) == 0 && strcmp (kill_host_p, slam_host_p) == 0)
	{
	  thread_slam_tran_index (thread_p, kill_tran_index);
	  signaled = true;
	}
      else
	{
	  if (signaled == false)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_KILL_DOES_NOTMATCH, 8, kill_tran_index, kill_user_p,
		      kill_host_p, kill_pid, kill_tran_index, slam_user_p, slam_host_p, slam_pid);
	      error_code = ER_CSS_KILL_DOES_NOTMATCH;
	    }
	  else
	    {
	      killed = true;
	    }
	  break;
	}
      thread_sleep (1000);	/* 1000 msec */
    }

  if (error_code == NO_ERROR && !killed)
    {
      error_code = ER_FAILED;	/* timeout */
    }

  return error_code;
}

/*
 * xthread_kill_or_interrupt_tran() -
 *   return:
 *   thread_p(in):
 *   tran_index(in):
 *   is_dba_group_member(in):
 *   kill_query_only(in):
 */
int
xthread_kill_or_interrupt_tran (THREAD_ENTRY * thread_p, int tran_index, bool is_dba_group_member, bool interrupt_only)
{
  int i, error;
  bool interrupt, has_authorization;
  bool is_trx_exists;
  KILLSTMT_TYPE kill_type;

  if (tran_index == LOG_SYSTEM_TRAN_INDEX)
    {
      // cannot kill system transaction; not even if this is dba
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_KILL_TR_NOT_ALLOWED, 1, tran_index);
      return ER_KILL_TR_NOT_ALLOWED;
    }

  if (!is_dba_group_member)
    {
      error = thread_check_kill_tran_auth (thread_p, tran_index, &has_authorization);
      if (error != NO_ERROR)
	{
	  return error;
	}

      if (has_authorization == false)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_KILL_TR_NOT_ALLOWED, 1, tran_index);
	  return ER_KILL_TR_NOT_ALLOWED;
	}
    }

  is_trx_exists = logtb_set_tran_index_interrupt (thread_p, tran_index, true);

  kill_type = interrupt_only ? KILLSTMT_QUERY : KILLSTMT_TRAN;
  if (kill_type == KILLSTMT_TRAN)
    {
      css_shutdown_conn_by_tran_index (tran_index);
    }

  for (i = 0; i < THREAD_RETRY_MAX_SLAM_TIMES; i++)
    {
      thread_sleep (1000);	/* 1000 msec */

      if (logtb_find_interrupt (tran_index, &interrupt) != NO_ERROR)
	{
	  break;
	}
      if (interrupt == false)
	{
	  break;
	}
    }

  if (i == THREAD_RETRY_MAX_SLAM_TIMES)
    {
      return ER_FAILED;		/* timeout */
    }

  if (is_trx_exists == false)
    {
      /* 
       * Note that the following error will be ignored by
       * sthread_kill_or_interrupt_tran().
       */
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * thread_find_first_lockwait_entry() -
 *   return:
 *   thread_index_p(in):
 */
THREAD_ENTRY *
thread_find_first_lockwait_entry (int *thread_index_p)
{
  for (THREAD_ENTRY * thread_p = thread_iterate (NULL); thread_p != NULL; thread_p = thread_iterate (thread_p))
    {
      if (thread_p->status == TS_DEAD || thread_p->status == TS_FREE)
	{
	  continue;
	}
      if (thread_p->lockwait != NULL)
	{			/* found */
	  *thread_index_p = thread_p->index;
	  return thread_p;
	}
    }
  return (THREAD_ENTRY *) NULL;
}

/*
 * thread_find_next_lockwait_entry() -
 *   return:
 *   thread_index_p(in):
 */
THREAD_ENTRY *
thread_find_next_lockwait_entry (int *thread_index_p)
{
  int start_index = (*thread_index_p) + 1;
  if (start_index == thread_num_total_threads ())
    {
      // no other threads
      return NULL;
    }
  // iterate threads
  for (THREAD_ENTRY * thread_p = thread_find_entry_by_index (start_index);
       thread_p != NULL; thread_p = thread_iterate (thread_p))
    {
      if (thread_p->status == TS_DEAD || thread_p->status == TS_FREE)
	{
	  continue;
	}
      if (thread_p->lockwait != NULL)
	{			/* found */
	  *thread_index_p = thread_p->index;
	  return thread_p;
	}
    }
  return (THREAD_ENTRY *) NULL;
}

/*
 * thread_find_entry_by_index() -
 *   return:
 *   thread_index(in):
 */
THREAD_ENTRY *
thread_find_entry_by_index (int thread_index)
{
  assert (thread_index >= 0 && thread_index < thread_num_total_threads ());

  THREAD_ENTRY *thread_p;
  if (thread_index == 0)
    {
      // this is the index of main thread entry. we don't want to expose it by thread_find_entry_by_index
      assert (false);
      return NULL;
    }
  else
    {
      thread_p = &(cubthread::get_manager ()->get_all_entries ()[thread_index - thread_Manager.num_total - 1]);
    }
  assert (thread_p->index == thread_index);

  return thread_p;
}

/*
 * thread_find_entry_by_tid() -
 *   return:
 *   tid(in)
 */
THREAD_ENTRY *
thread_find_entry_by_tid (thread_id_t tid)
{
  for (THREAD_ENTRY * thread_p = thread_iterate (NULL); thread_p != NULL; thread_p = thread_iterate (thread_p))
    {
      if (thread_p->get_id () == tid)
	{
	  return thread_p;
	}
    }

  return NULL;
}

/*
 * thread_get_lockwait_entry() -
 *   return:
 *   tran_index(in):
 *   thread_array_p(in):
 */
int
thread_get_lockwait_entry (int tran_index, THREAD_ENTRY ** thread_array_p)
{
  THREAD_ENTRY *thread_p;
  int thread_count;

  thread_count = 0;
  for (thread_p = thread_iterate (NULL); thread_p != NULL; thread_p = thread_iterate (thread_p))
    {
      if (thread_p->status == TS_DEAD || thread_p->status == TS_FREE)
	{
	  continue;
	}
      if (thread_p->tran_index == tran_index && thread_p->lockwait != NULL)
	{
	  thread_array_p[thread_count] = thread_p;
	  thread_count++;
	  if (thread_count >= 10)
	    {
	      assert (false);
	      break;
	    }
	}
    }
  /* TODO: transfer to new manager */

  return thread_count;
}

/*
 * thread_set_info () -
 *   return:
 *   thread_p(out):
 *   client_id(in):
 *   rid(in):
 *   tran_index(in):
 */
void
thread_set_info (THREAD_ENTRY * thread_p, int client_id, int rid, int tran_index, int net_request_index)
{
  thread_p->client_id = client_id;
  thread_p->rid = rid;
  thread_p->tran_index = tran_index;
  thread_p->net_request_index = net_request_index;
  thread_p->victim_request_fail = false;
  thread_p->next_wait_thrd = NULL;
  thread_p->wait_for_latch_promote = false;
  thread_p->lockwait = NULL;
  thread_p->lockwait_state = -1;
  thread_p->query_entry = NULL;
  thread_p->tran_next_wait = NULL;

  (void) thread_rc_track_clear_all (thread_p);
  thread_clear_recursion_depth (thread_p);
}

/*
 * thread_rc_track_meter_check () -
 *   return:
 *   thread_p(in):
 */
static int
thread_rc_track_meter_check (THREAD_ENTRY * thread_p, THREAD_RC_METER * meter, THREAD_RC_METER * prev_meter)
{
#if !defined(NDEBUG)
  int i;
#endif

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);
  assert_release (meter != NULL);

  /* assert (meter->m_amount >= 0); assert (meter->m_amount <= meter->m_threshold); */
  if (meter->m_amount < 0 || meter->m_amount > meter->m_threshold)
    {
      THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, false);
      return ER_FAILED;
    }

  if (prev_meter != NULL)
    {
      /* assert (meter->m_amount == prev_meter->m_amount); */
      if (meter->m_amount != prev_meter->m_amount)
	{
	  THREAD_RC_TRACK_ASSERT (thread_p, stderr, false);
	  return ER_FAILED;
	}
    }
  else
    {
      /* assert (meter->m_amount == 0); */
      if (meter->m_amount != 0)
	{
	  THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, false);
	  return ER_FAILED;
	}
    }

#if !defined(NDEBUG)
  /* check hold_buf */
  if (meter->m_hold_buf_size > 0)
    {
      for (i = 0; i < meter->m_hold_buf_size; i++)
	{
	  if (meter->m_hold_buf[i] != '\0')
	    {
	      THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, false);
	      return ER_FAILED;
	    }
	}
    }
#endif

  return NO_ERROR;
}

/*
 * thread_rc_track_check () -
 *   return:
 *   thread_p(in):
 */
static int
thread_rc_track_check (THREAD_ENTRY * thread_p, int id)
{
  int i, j;
  THREAD_RC_TRACK *track, *prev_track;
  THREAD_RC_METER *meter, *prev_meter;
  int num_invalid_meter;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);
  assert_release (thread_p->track != NULL);
  assert_release (id == thread_p->track_depth);

  num_invalid_meter = 0;	/* init */

  if (thread_p->track != NULL)
    {
      assert_release (id >= 0);

      track = thread_p->track;
      prev_track = track->prev;

      for (i = 0; i < RC_LAST; i++)
	{
#if 1				/* TODO - */
	  /* skip out qlist check; is checked separately */
	  if (i == RC_QLIST)
	    {
	      continue;
	    }
#endif

	  for (j = 0; j < MGR_LAST; j++)
	    {
	      meter = &(track->meter[i][j]);

	      if (prev_track != NULL)
		{
		  prev_meter = &(prev_track->meter[i][j]);
		}
	      else
		{
		  prev_meter = NULL;
		}

	      if (thread_rc_track_meter_check (thread_p, meter, prev_meter) != NO_ERROR)
		{
		  num_invalid_meter++;
		}
	    }			/* for */
	}			/* for */
    }

  return (num_invalid_meter == 0) ? NO_ERROR : ER_FAILED;
}

/*
 * thread_rc_track_clear_all () -
 *   return:
 *   thread_p(in):
 */
static void
thread_rc_track_clear_all (THREAD_ENTRY * thread_p)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);

  /* pop/free every track info */
  while (thread_p->track != NULL)
    {
      (void) thread_rc_track_free (thread_p, thread_p->track_depth);
    }

  assert_release (thread_p->track_depth == -1);

  thread_p->track_depth = -1;	/* defence */
}

/*
 * thread_rc_track_initialize () -
 *   return:
 *   thread_p(in):
 */
void
thread_rc_track_initialize (THREAD_ENTRY * thread_p)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);

  thread_p->track = NULL;
  thread_p->track_depth = -1;
  thread_p->track_threshold = 0x7F;	/* 127 */
  thread_p->track_free_list = NULL;

  (void) thread_rc_track_clear_all (thread_p);
}

/*
 * thread_rc_track_finalize () -
 *   return:
 *   thread_p(in):
 */
void
thread_rc_track_finalize (THREAD_ENTRY * thread_p)
{
  THREAD_RC_TRACK *track;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);

  (void) thread_rc_track_clear_all (thread_p);

  while (thread_p->track_free_list != NULL)
    {
      track = thread_p->track_free_list;
      thread_p->track_free_list = track->prev;
      track->prev = NULL;	/* cut-off */

      free (track);
    }
}

/*
 * thread_rc_track_rcname () - TODO
 *   return:
 *   rc_idx(in):
 */
static const char *
thread_rc_track_rcname (int rc_idx)
{
  const char *name;

  assert_release (rc_idx >= 0);
  assert_release (rc_idx < RC_LAST);

  switch (rc_idx)
    {
    case RC_VMEM:
      name = "Virtual Memory";
      break;
    case RC_PGBUF:
      name = "Page Buffer";
      break;
    case RC_QLIST:
      name = "List File";
      break;
    case RC_CS:
      name = "Critical Section";
      break;
    default:
      name = "**UNKNOWN_RESOURCE**";
      break;
    }

  return name;
}

/*
 * thread_rc_track_mgrname () - TODO
 *   return:
 *   mgr_idx(in):
 */
static const char *
thread_rc_track_mgrname (int mgr_idx)
{
  const char *name;

  assert_release (mgr_idx >= 0);
  assert_release (mgr_idx < MGR_LAST);

  switch (mgr_idx)
    {
#if 0
    case MGR_BTREE:
      name = "Index Manager";
      break;
    case MGR_QUERY:
      name = "Query Manager";
      break;
    case MGR_SPAGE:
      name = "Slotted-Page Manager";
      break;
#endif
    case MGR_DEF:
      name = "Default Manager";
      break;
    default:
      name = "**UNKNOWN_MANAGER**";
      break;
    }

  return name;
}

/*
 * thread_rc_track_threshold_amount () - Get the maximum amount for different
 *					 trackers.
 *
 * return	 :
 * thread_p (in) :
 * rc_idx (in)	 :
 */
static INT32
thread_rc_track_threshold_amount (int rc_idx)
{
  switch (rc_idx)
    {
    case RC_VMEM:
      return THREAD_RC_TRACK_VMEM_THRESHOLD_AMOUNT;
    case RC_PGBUF:
      return THREAD_RC_TRACK_PGBUF_THRESHOLD_AMOUNT;
    case RC_QLIST:
      return THREAD_RC_TRACK_QLIST_THRESHOLD_AMOUNT;
    case RC_CS:
      return THREAD_RC_TRACK_CS_THRESHOLD_AMOUNT;
    default:
      assert_release (false);
      return -1;
    }
}

/*
 * thread_rc_track_alloc ()
 *   return:
 *   thread_p(in):
 */
static THREAD_RC_TRACK *
thread_rc_track_alloc (THREAD_ENTRY * thread_p)
{
  int i, j;
#if !defined(NDEBUG)
  int max_tracked_res;
  THREAD_TRACKED_RESOURCE *tracked_res_chunk = NULL, *tracked_res_ptr = NULL;
#endif /* !NDEBUG */
  THREAD_RC_TRACK *new_track;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);
  assert_release (thread_p->track_depth < thread_p->track_threshold);

  new_track = NULL;		/* init */

#if !defined (NDEBUG)
  /* Compute the required size for tracked resources */
  max_tracked_res = 0;
  /* Compute the size required for one manager */
  for (i = 0; i < RC_LAST; i++)
    {
      max_tracked_res += thread_rc_track_threshold_amount (i);
    }
  /* Compute the size required for all managers */
  max_tracked_res *= MGR_LAST;

  /* Allocate a chunk of memory for all tracked resources */
  tracked_res_chunk = (THREAD_TRACKED_RESOURCE *) malloc (max_tracked_res * sizeof (THREAD_TRACKED_RESOURCE));
  if (tracked_res_chunk == NULL)
    {
      assert_release (false);
      goto error;
    }
  tracked_res_ptr = tracked_res_chunk;
#endif /* !NDEBUG */

  if (thread_p->track_depth < thread_p->track_threshold)
    {
      if (thread_p->track_free_list != NULL)
	{
	  new_track = thread_p->track_free_list;
	  thread_p->track_free_list = new_track->prev;
	}
      else
	{
	  new_track = (THREAD_RC_TRACK *) malloc (sizeof (THREAD_RC_TRACK));
	  if (new_track == NULL)
	    {
	      assert_release (false);
	      goto error;
	    }
	}
      assert_release (new_track != NULL);

      if (new_track != NULL)
	{
	  /* keep current track info */

	  /* id of thread private memory allocator */
	  new_track->private_heap_id = thread_p->private_heap_id;

	  for (i = 0; i < RC_LAST; i++)
	    {
	      for (j = 0; j < MGR_LAST; j++)
		{
		  if (thread_p->track != NULL)
		    {
		      new_track->meter[i][j].m_amount = thread_p->track->meter[i][j].m_amount;
		    }
		  else
		    {
		      new_track->meter[i][j].m_amount = 0;
		    }
		  new_track->meter[i][j].m_threshold = thread_rc_track_threshold_amount (i);
		  new_track->meter[i][j].m_add_file_name = NULL;
		  new_track->meter[i][j].m_add_line_no = -1;
		  new_track->meter[i][j].m_sub_file_name = NULL;
		  new_track->meter[i][j].m_sub_line_no = -1;
#if !defined(NDEBUG)
		  new_track->meter[i][j].m_hold_buf[0] = '\0';
		  new_track->meter[i][j].m_rwlock_buf[0] = '\0';
		  new_track->meter[i][j].m_hold_buf_size = 0;

		  /* init Critical Section hold_buf */
		  if (i == RC_CS)
		    {
		      memset (new_track->meter[i][j].m_hold_buf, 0, ONE_K);
		      memset (new_track->meter[i][j].m_rwlock_buf, 0, ONE_K);
		    }

		  /* Initialize tracked resources */
		  new_track->meter[i][j].m_tracked_res_capacity = thread_rc_track_threshold_amount (i);
		  new_track->meter[i][j].m_tracked_res_count = 0;
		  new_track->meter[i][j].m_tracked_res = tracked_res_ptr;
		  /* Advance pointer in preallocated chunk of resources */
		  tracked_res_ptr += new_track->meter[i][j].m_tracked_res_capacity;
#endif /* !NDEBUG */
		}
	    }

#if !defined (NDEBUG)
	  assert ((tracked_res_ptr - tracked_res_chunk) == max_tracked_res);
	  new_track->tracked_resources = tracked_res_chunk;
#endif /* !NDEBUG */

	  /* push current track info */
	  new_track->prev = thread_p->track;
	  thread_p->track = new_track;

	  thread_p->track_depth++;
	}
    }

  return new_track;

error:

  if (new_track != NULL)
    {
      free (new_track);
    }

#if !defined (NDEBUG)
  if (tracked_res_chunk != NULL)
    {
      free (tracked_res_chunk);
    }
#endif /* !NDEBUG */
  return NULL;
}

/*
 * thread_rc_track_free ()
 *   return:
 *   thread_p(in):
 */
static void
thread_rc_track_free (THREAD_ENTRY * thread_p, int id)
{
  THREAD_RC_TRACK *prev_track;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);
  assert_release (id == thread_p->track_depth);

  if (thread_p->track != NULL)
    {
      assert_release (id >= 0);

#if !defined (NDEBUG)
      if (thread_p->track->tracked_resources != NULL)
	{
	  free_and_init (thread_p->track->tracked_resources);
	}
#endif

      prev_track = thread_p->track->prev;

      /* add to free list */
      thread_p->track->prev = thread_p->track_free_list;
      thread_p->track_free_list = thread_p->track;

      /* pop previous track info */
      thread_p->track = prev_track;

      thread_p->track_depth--;
    }
}

/*
 * thread_rc_track_is_enabled () - check if is enabled
 *   return:
 *   thread_p(in):
 */
static bool
thread_rc_track_is_enabled (THREAD_ENTRY * thread_p)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);

  if (prm_get_bool_value (PRM_ID_USE_SYSTEM_MALLOC))
    {
      /* disable tracking */
      assert_release (thread_p->track == NULL);
      assert_release (thread_p->track_depth == -1);

      return false;
    }

  return true;
}

/*
 * thread_rc_track_need_to_trace () - check if is track valid
 *   return:
 *   thread_p(in):
 */
bool
thread_rc_track_need_to_trace (THREAD_ENTRY * thread_p)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);

  /* If it reaches the threshold, cubrid stop tracking and clean thread_p->track. See thread_rc_track_meter. */
  return thread_rc_track_is_enabled (thread_p) && thread_p->track != NULL;
}

/*
 * thread_rc_track_enter () - save current track info
 *   return:
 *   thread_p(in):
 */
int
thread_rc_track_enter (THREAD_ENTRY * thread_p)
{
  THREAD_RC_TRACK *track;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);

  if (thread_rc_track_is_enabled (thread_p))
    {
      track = thread_rc_track_alloc (thread_p);
      assert_release (track != NULL);
      if (track == NULL)
	{
	  return ER_FAILED;
	}
    }

  return thread_p->track_depth;
}

/*
 * thread_rc_track_exit () -
 *   return:
 *   thread_p(in):
 *   id(in): saved track id
 */
int
thread_rc_track_exit (THREAD_ENTRY * thread_p, int id)
{
  int ret = NO_ERROR;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);

  if (thread_rc_track_need_to_trace (thread_p))
    {
      assert_release (id == thread_p->track_depth);
      assert_release (id >= 0);

      ret = thread_rc_track_check (thread_p, id);
#if !defined(NDEBUG)
      if (ret != NO_ERROR)
	{
	  (void) thread_rc_track_dump_all (thread_p, stderr);
	}
#endif

      (void) thread_rc_track_free (thread_p, id);
    }

  return ret;
}

/*
 * thread_rc_track_amount_helper () -
 *   return:
 *   thread_p(in):
 */
static INT32
thread_rc_track_amount_helper (THREAD_ENTRY * thread_p, int rc_idx)
{
  INT32 amount;
  THREAD_RC_TRACK *track;
  THREAD_RC_METER *meter;
  int j;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);

  assert_release (rc_idx >= 0);
  assert_release (rc_idx < RC_LAST);

  amount = 0;			/* init */

  track = thread_p->track;
  if (track != NULL)
    {
      for (j = 0; j < MGR_LAST; j++)
	{
	  meter = &(track->meter[rc_idx][j]);
	  amount += meter->m_amount;
	}
    }

  THREAD_RC_TRACK_ASSERT (thread_p, stderr, amount >= 0);

  return amount;
}

/*
 * thread_rc_track_amount_pgbuf () -
 *   return:
 *   thread_p(in):
 */
int
thread_rc_track_amount_pgbuf (THREAD_ENTRY * thread_p)
{
  return thread_rc_track_amount_helper (thread_p, RC_PGBUF);
}

/*
 * thread_rc_track_amount_qlist () -
 *   return:
 *   thread_p(in):
 */
int
thread_rc_track_amount_qlist (THREAD_ENTRY * thread_p)
{
  return thread_rc_track_amount_helper (thread_p, RC_QLIST);
}

#if !defined(NDEBUG)
/*
 * thread_rc_track_meter_assert_csect_dependency () -
 *   return:
 *   meter(in):
 */
static void
thread_rc_track_meter_assert_csect_dependency (THREAD_ENTRY * thread_p, THREAD_RC_METER * meter, int amount, void *ptr)
{
  int cs_idx;

  assert (meter != NULL);
  assert (amount != 0);
  assert (ptr != NULL);

  cs_idx = *((int *) ptr);

  /* TODO - skip out too many CS */
  if (cs_idx >= ONE_K)
    {
      return;
    }

  assert (cs_idx >= 0);
  assert (cs_idx < ONE_K);

  /* check CS dependency */
  if (amount > 0)
    {
      switch (cs_idx)
	{
	  /* CSECT_CT_OID_TABLE -> CSECT_LOCATOR_SR_CLASSNAME_TABLE is NOK */
	  /* CSECT_LOCATOR_SR_CLASSNAME_TABLE -> CSECT_CT_OID_TABLE is OK */
	case CSECT_LOCATOR_SR_CLASSNAME_TABLE:
	  THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, meter->m_hold_buf[CSECT_CT_OID_TABLE] == 0);
	  break;

	default:
	  break;
	}
    }
}

/*
 * thread_rc_track_meter_assert_csect_usage () -  assert enter mode of critical
 * 	section with the existed lock state for each thread.
 *   return:
 *   meter(in):
 *   enter_mode(in):
 *   ptr(in):
 */
static void
thread_rc_track_meter_assert_csect_usage (THREAD_ENTRY * thread_p, THREAD_RC_METER * meter, int enter_mode, void *ptr)
{
  int cs_idx;
  unsigned char enter_count;

  assert (meter != NULL);
  assert (ptr != NULL);

  cs_idx = *((int *) ptr);

  /* TODO - skip out too many CS */
  if (cs_idx >= ONE_K)
    {
      return;
    }

  assert (cs_idx >= 0);
  assert (cs_idx < ONE_K);

  switch (enter_mode)
    {
    case THREAD_TRACK_CSECT_ENTER_AS_READER:
      if (meter->m_rwlock_buf[cs_idx] >= 0)
	{
	  if (THREAD_METER_IS_DEMOTED_READER (meter->m_rwlock_buf[cs_idx]))
	    {
	      enter_count = THREAD_METER_STRIP_DEMOTED_READER_FLAG (meter->m_rwlock_buf[cs_idx]);
	      assert (enter_count < THREAD_METER_MAX_ENTER_COUNT);

	      /* demoted-reader can re-enter as reader again */
	      meter->m_rwlock_buf[cs_idx]++;
	    }
	  else
	    {
	      /* enter as reader first time or reader re-enter */
	      meter->m_rwlock_buf[cs_idx]++;

	      /* reader re-enter is not allowed. */
	      THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, meter->m_rwlock_buf[cs_idx] <= 1);
	    }
	}
      else
	{
	  enter_count = -meter->m_rwlock_buf[cs_idx];
	  assert (enter_count < THREAD_METER_MAX_ENTER_COUNT);

	  /* I am a writer already. re-enter as reader, treat as re-enter as writer */
	  meter->m_rwlock_buf[cs_idx]--;
	}
      break;

    case THREAD_TRACK_CSECT_ENTER_AS_WRITER:
      if (meter->m_rwlock_buf[cs_idx] <= 0)
	{
	  enter_count = -meter->m_rwlock_buf[cs_idx];
	  assert (enter_count < THREAD_METER_MAX_ENTER_COUNT);

	  /* enter as writer first time or writer re-enter */
	  meter->m_rwlock_buf[cs_idx]--;
	}
      else
	{
	  /* I am a reader or demoted-reader already. re-enter as writer is not allowed */
	  THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, false);
	}
      break;

    case THREAD_TRACK_CSECT_PROMOTE:
      /* If I am not a reader or demoted-reader, promote is not allowed */
      THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, meter->m_rwlock_buf[cs_idx] > 0);

      enter_count = THREAD_METER_STRIP_DEMOTED_READER_FLAG (meter->m_rwlock_buf[cs_idx]);
      assert (enter_count != 0);

      if (enter_count == 1)
	{
	  /* promote reader or demoted-reader to writer */
	  meter->m_rwlock_buf[cs_idx] = -1;
	}
      else
	{
	  /* In the middle of citical session, promote is not allowed. only when demote-reader re-enter as reader can
	   * go to here. */
	  THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, false);
	}
      break;

    case THREAD_TRACK_CSECT_DEMOTE:
      /* if I am not a writer, demote is not allowed */
      THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, meter->m_rwlock_buf[cs_idx] < 0);

      if (meter->m_rwlock_buf[cs_idx] == -1)
	{
	  /* demote writer to demoted-reader */
	  enter_count = 1;
	  meter->m_rwlock_buf[cs_idx] = THREAD_METER_WITH_DEMOTED_READER_FLAG (enter_count);
	}
      else
	{
	  /* In the middle of citical session, demote is not allowed */
	  THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, false);
	}
      break;

    case THREAD_TRACK_CSECT_EXIT:
      /* without entered before, exit is not allowed. */
      THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, meter->m_rwlock_buf[cs_idx] != 0);

      if (meter->m_rwlock_buf[cs_idx] > 0)
	{
	  if (THREAD_METER_IS_DEMOTED_READER (meter->m_rwlock_buf[cs_idx]))
	    {
	      enter_count = THREAD_METER_STRIP_DEMOTED_READER_FLAG (meter->m_rwlock_buf[cs_idx]);
	      assert (enter_count != 0);

	      enter_count--;
	      if (enter_count != 0)
		{
		  /* still keep demoted-reader flag */
		  meter->m_rwlock_buf[cs_idx] = THREAD_METER_WITH_DEMOTED_READER_FLAG (enter_count);
		}
	      else
		{
		  meter->m_rwlock_buf[cs_idx] = 0;
		}
	    }
	  else
	    {
	      /* reader exit */
	      meter->m_rwlock_buf[cs_idx]--;
	    }
	}
      else
	{			/* (meter->m_rwlock_buf[cs_idx] < 0 */
	  meter->m_rwlock_buf[cs_idx]++;
	}
      break;
    default:
      assert (false);
      break;
    }
}
#endif

/*
 * thread_rc_track_meter () -
 *   return:
 *   thread_p(in):
 */
void
thread_rc_track_meter (THREAD_ENTRY * thread_p, const char *file_name, const int line_no, int amount, void *ptr,
		       int rc_idx, int mgr_idx)
{
  THREAD_RC_TRACK *track;
  THREAD_RC_METER *meter;
  int enter_mode = -1;
  static bool report_track_cs_overflow = false;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);

  if (thread_p->track == NULL)
    {
      return;			/* not in track enter */
    }

  assert_release (thread_p->track != NULL);

  assert_release (0 <= rc_idx);
  assert_release (rc_idx < RC_LAST);
  assert_release (0 <= mgr_idx);
  assert_release (mgr_idx < MGR_LAST);

  assert_release (amount != 0);

  track = thread_p->track;

  if (track != NULL && 0 <= rc_idx && rc_idx < RC_LAST && 0 <= mgr_idx && mgr_idx < MGR_LAST)
    {
      if (rc_idx == RC_CS)
	{
	  enter_mode = amount;

	  /* recover the amount for RC_CS */
	  switch (enter_mode)
	    {
	    case THREAD_TRACK_CSECT_ENTER_AS_READER:
	      amount = 1;
	      break;
	    case THREAD_TRACK_CSECT_ENTER_AS_WRITER:
	      amount = 1;
	      break;
	    case THREAD_TRACK_CSECT_PROMOTE:
	      amount = 1;
	      break;
	    case THREAD_TRACK_CSECT_DEMOTE:
	      amount = -1;
	      break;
	    case THREAD_TRACK_CSECT_EXIT:
	      amount = -1;
	      break;
	    default:
	      assert_release (false);
	      break;
	    }
	}

      /* check iff is tracking one */
      if (rc_idx == RC_VMEM)
	{
	  if (track->private_heap_id != thread_p->private_heap_id)
	    {
	      return;		/* ignore */
	    }
	}

      meter = &(track->meter[rc_idx][mgr_idx]);

      /* If it reaches the threshold just stop tracking and clear */
      if (meter->m_amount + amount > meter->m_threshold)
	{
#if 0
	  assert (0);
#endif
	  thread_rc_track_finalize (thread_p);
	  return;
	}

      meter->m_amount += amount;

#if 1				/* TODO - */
      /* skip out qlist check; is checked separately */
      if (rc_idx == RC_QLIST)
	{
	  ;			/* nop */
	}
      else
	{
	  THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, 0 <= meter->m_amount);
	}
#else
      THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, 0 <= meter->m_amount);
#endif

#if !defined(NDEBUG)
      switch (rc_idx)
	{
	case RC_PGBUF:
	  /* check fix/unfix protocol */
	  {
	    assert (ptr != NULL);

	    if (amount > 0)
	      {
		assert_release (amount == 1);
	      }
	    else
	      {
		assert_release (amount == -1);
	      }
	  }
	  break;

	case RC_CS:
	  /* check Critical Section cycle and keep current hold info */
	  {
	    int cs_idx;

	    assert (ptr != NULL);

	    cs_idx = *((int *) ptr);

	    /* TODO - skip out too many CS */
	    if (cs_idx < ONE_K)
	      {
		assert (cs_idx >= 0);
		assert (cs_idx < ONE_K);

		THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, meter->m_hold_buf[cs_idx] >= 0);
		if (amount > 0)
		  {
		    assert_release (amount == 1);
		  }
		else
		  {
		    assert_release (amount == -1);
		  }

		meter->m_hold_buf[cs_idx] += amount;

		THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, meter->m_hold_buf[cs_idx] >= 0);

		/* re-set buf size */
		meter->m_hold_buf_size = MAX (meter->m_hold_buf_size, cs_idx + 1);
		assert (meter->m_hold_buf_size <= ONE_K);
	      }
	    else if (report_track_cs_overflow == false)
	      {
		report_track_cs_overflow = true;	/* report only once */
		er_log_debug (ARG_FILE_LINE, "thread_rc_track_meter: hold_buf overflow: buf_size=%d, idx=%d",
			      meter->m_hold_buf_size, cs_idx);
	      }
	  }
	  break;

	default:
	  break;
	}
#endif

      if (amount > 0)
	{
	  meter->m_add_file_name = file_name;
	  meter->m_add_line_no = line_no;
	}
      else if (amount < 0)
	{
	  meter->m_sub_file_name = file_name;
	  meter->m_sub_line_no = line_no;
	}

#if !defined(NDEBUG)
      (void) thread_rc_track_meter_at (meter, file_name, line_no, amount, ptr);

      if (rc_idx == RC_CS)
	{
	  (void) thread_rc_track_meter_assert_csect_dependency (thread_p, meter, amount, ptr);

	  (void) thread_rc_track_meter_assert_csect_usage (thread_p, meter, enter_mode, ptr);
	}
#endif
    }
}

/*
 * thread_rc_track_meter_dump () -
 *   return:
 *   thread_p(in):
 *   outfp(in):
 *   meter(in):
 */
static void
thread_rc_track_meter_dump (THREAD_ENTRY * thread_p, FILE * outfp, THREAD_RC_METER * meter)
{
  int i;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);
  assert_release (meter != NULL);

  if (outfp == NULL)
    {
      outfp = stderr;
    }

  if (meter != NULL)
    {
      fprintf (outfp, "         +--- amount = %d (threshold = %d)\n", meter->m_amount, meter->m_threshold);
      fprintf (outfp, "         +--- add_file_line = %s:%d\n", meter->m_add_file_name, meter->m_add_line_no);
      fprintf (outfp, "         +--- sub_file_line = %s:%d\n", meter->m_sub_file_name, meter->m_sub_line_no);
#if !defined(NDEBUG)
      /* dump hold_buf */
      if (meter->m_hold_buf_size > 0)
	{
	  fprintf (outfp, "            +--- hold_at = ");
	  for (i = 0; i < meter->m_hold_buf_size; i++)
	    {
	      if (meter->m_hold_buf[i] != '\0')
		{
		  fprintf (outfp, "[%d]:%c ", i, meter->m_hold_buf[i]);
		}
	    }
	  fprintf (outfp, "\n");
	  fprintf (outfp, "            +--- hold_buf_size = %d\n", meter->m_hold_buf_size);

	  fprintf (outfp, "            +--- read/write lock = ");
	  for (i = 0; i < meter->m_hold_buf_size; i++)
	    {
	      if (meter->m_rwlock_buf[i] != '\0')
		{
		  fprintf (outfp, "[%d]:%d ", i, (int) meter->m_rwlock_buf[i]);
		}
	    }
	  fprintf (outfp, "\n");
	  fprintf (outfp, "            +--- read/write lock size = %d\n", meter->m_hold_buf_size);
	}

      /* dump tracked resources */
      if (meter->m_tracked_res_count > 0)
	{
	  fprintf (outfp, "            +--- tracked res = ");
	  for (i = 0; i < meter->m_tracked_res_count; i++)
	    {
	      fprintf (outfp, "res_ptr=%p amount=%d first_caller=%s:%d\n", meter->m_tracked_res[i].res_ptr,
		       meter->m_tracked_res[i].amount, meter->m_tracked_res[i].caller_file,
		       meter->m_tracked_res[i].caller_line);
	    }
	  fprintf (outfp, "            +--- tracked res count = %d\n", meter->m_tracked_res_count);
	}
#endif /* !NDEBUG */
    }
}

/*
 * thread_rc_track_dump () -
 *   return:
 *   thread_p(in):
 *   outfp(in):
 *   track(in):
 *   depth(in):
 */
static void
thread_rc_track_dump (THREAD_ENTRY * thread_p, FILE * outfp, THREAD_RC_TRACK * track, int depth)
{
  int i, j;
  const char *rcname, *mgrname;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);
  assert_release (track != NULL);
  assert_release (depth >= 0);

  if (outfp == NULL)
    {
      outfp = stderr;
    }

  if (track != NULL)
    {
      fprintf (outfp, "+--- track depth = %d\n", depth);
      for (i = 0; i < RC_LAST; i++)
	{
	  rcname = thread_rc_track_rcname (i);
	  fprintf (outfp, "   +--- %s\n", rcname);

	  for (j = 0; j < MGR_LAST; j++)
	    {
	      mgrname = thread_rc_track_mgrname (j);
	      fprintf (outfp, "      +--- %s\n", mgrname);

	      (void) thread_rc_track_meter_dump (thread_p, outfp, &(track->meter[i][j]));
	    }
	}

      fflush (outfp);
    }
}

/*
 * thread_check_kill_tran_auth () - User who is not DBA can kill only own transaction
 *   return: NO_ERROR or error code
 *   thread_p(in):
 *   tran_id(in):
 *   has_authorization(out):
 */
static int
thread_check_kill_tran_auth (THREAD_ENTRY * thread_p, int tran_id, bool * has_authorization)
{
  char *tran_client_name;
  char *current_client_name;

  assert (has_authorization);

  *has_authorization = false;

  if (logtb_am_i_dba_client (thread_p) == true)
    {
      *has_authorization = true;
      return NO_ERROR;
    }

  tran_client_name = logtb_find_client_name (tran_id);
  current_client_name = logtb_find_current_client_name (thread_p);

  if (tran_client_name == NULL || current_client_name == NULL)
    {
      return ER_CSS_KILL_UNKNOWN_TRANSACTION;
    }

  if (strcasecmp (tran_client_name, current_client_name) == 0)
    {
      *has_authorization = true;
    }

  return NO_ERROR;
}

/*
 * thread_type_to_string () - Translate thread type into string 
 *                            representation
 *   return:
 *   type(in): thread type
 */
const char *
thread_type_to_string (int type)
{
  switch (type)
    {
    case TT_MASTER:
      return "MASTER";
    case TT_SERVER:
      return "SERVER";
    case TT_WORKER:
      return "WORKER";
    case TT_DAEMON:
      return "DAEMON";
    case TT_VACUUM_MASTER:
      return "VACUUM_MASTER";
    case TT_VACUUM_WORKER:
      return "VACUUM_WORKER";
    case TT_NONE:
      return "NONE";
    }
  return "UNKNOWN";
}

/*
 * thread_status_to_string () - Translate thread status into string
 *                              representation
 *   return:
 *   type(in): thread type
 */
const char *
thread_status_to_string (int status)
{
  switch (status)
    {
    case TS_DEAD:
      return "DEAD";
    case TS_FREE:
      return "FREE";
    case TS_RUN:
      return "RUN";
    case TS_WAIT:
      return "WAIT";
    case TS_CHECK:
      return "CHECK";
    }
  return "UNKNOWN";
}

/*
 * thread_resume_status_to_string () - Translate thread resume status into
 *                                     string representation
 *   return:
 *   type(in): thread type
 */
const char *
thread_resume_status_to_string (int resume_status)
{
  switch (resume_status)
    {
    case THREAD_RESUME_NONE:
      return "RESUME_NONE";
    case THREAD_RESUME_DUE_TO_INTERRUPT:
      return "RESUME_DUE_TO_INTERRUPT";
    case THREAD_RESUME_DUE_TO_SHUTDOWN:
      return "RESUME_DUE_TO_SHUTDOWN";
    case THREAD_PGBUF_SUSPENDED:
      return "PGBUF_SUSPENDED";
    case THREAD_PGBUF_RESUMED:
      return "PGBUF_RESUMED";
    case THREAD_JOB_QUEUE_SUSPENDED:
      return "JOB_QUEUE_SUSPENDED";
    case THREAD_JOB_QUEUE_RESUMED:
      return "JOB_QUEUE_RESUMED";
    case THREAD_CSECT_READER_SUSPENDED:
      return "CSECT_READER_SUSPENDED";
    case THREAD_CSECT_READER_RESUMED:
      return "CSECT_READER_RESUMED";
    case THREAD_CSECT_WRITER_SUSPENDED:
      return "CSECT_WRITER_SUSPENDED";
    case THREAD_CSECT_WRITER_RESUMED:
      return "CSECT_WRITER_RESUMED";
    case THREAD_CSECT_PROMOTER_SUSPENDED:
      return "CSECT_PROMOTER_SUSPENDED";
    case THREAD_CSECT_PROMOTER_RESUMED:
      return "CSECT_PROMOTER_RESUMED";
    case THREAD_CSS_QUEUE_SUSPENDED:
      return "CSS_QUEUE_SUSPENDED";
    case THREAD_CSS_QUEUE_RESUMED:
      return "CSS_QUEUE_RESUMED";
    case THREAD_HEAP_CLSREPR_SUSPENDED:
      return "HEAP_CLSREPR_SUSPENDED";
    case THREAD_HEAP_CLSREPR_RESUMED:
      return "HEAP_CLSREPR_RESUMED";
    case THREAD_LOCK_SUSPENDED:
      return "LOCK_SUSPENDED";
    case THREAD_LOCK_RESUMED:
      return "LOCK_RESUMED";
    case THREAD_LOGWR_SUSPENDED:
      return "LOGWR_SUSPENDED";
    case THREAD_LOGWR_RESUMED:
      return "LOGWR_RESUMED";
    }
  return "UNKNOWN";
}

/*
 * thread_rc_track_dump_all () -
 *   return:
 *   thread_p(in):
 */
void
thread_rc_track_dump_all (THREAD_ENTRY * thread_p, FILE * outfp)
{
  THREAD_RC_TRACK *track;
  int depth;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert_release (thread_p != NULL);

  if (outfp == NULL)
    {
      outfp = stderr;
    }

  fprintf (outfp, "------------ Thread[%d] Resource Track Info: ------------\n", thread_p->index);

  fprintf (outfp, "track_depth = %d\n", thread_p->track_depth);
  fprintf (outfp, "track_threshold = %d\n", thread_p->track_threshold);
  for (track = thread_p->track_free_list, depth = 0; track != NULL; track = track->prev, depth++)
    {
      ;
    }
  fprintf (outfp, "track_free_list size = %d\n", depth);

  for (track = thread_p->track, depth = thread_p->track_depth; track != NULL; track = track->prev, depth--)
    {
      (void) thread_rc_track_dump (thread_p, outfp, track, depth);
    }
  assert_release (depth == -1);

  fprintf (outfp, "\n");

  fflush (outfp);
}

#if !defined(NDEBUG)
/*
 * thread_rc_track_meter_at () -
 *   return:
 *   meter(in):
 */
static void
thread_rc_track_meter_at (THREAD_RC_METER * meter, const char *caller_file, int caller_line, int amount, void *ptr)
{
  const char *p = NULL;
  int min, max, mid, mem_size;
  bool found = false;

  assert_release (meter != NULL);
  assert_release (amount != 0);

  /* Truncate path to file name */
  p = (char *) caller_file + strlen (caller_file);
  while (p)
    {
      if (p == caller_file)
	{
	  break;
	}

      if (*p == '/' || *p == '\\')
	{
	  p++;
	  break;
	}

      p--;
    }

  /* TODO: A binary search function that could also return the rightful position for an entry that was not found is
   * really necessary. */

  /* There are three possible actions here: 1. Resource doesn't exist and must be added. 2. Resource exists and we
   * update amount to a non-zero value. 3. Resource exists and new amount is 0 and it must be removed from tracked
   * array. */
  /* The array is ordered by resource pointer and binary search is used. */
  min = 0;
  max = meter->m_tracked_res_count - 1;
  mid = 0;

  while (min <= max)
    {
      /* Get middle */
      mid = (min + max) >> 1;
      if (ptr == meter->m_tracked_res[mid].res_ptr)
	{
	  found = true;
	  break;
	}
      if (ptr < meter->m_tracked_res[mid].res_ptr)
	{
	  /* Set search range to [min, mid - 1] */
	  max = mid - 1;
	}
      else
	{
	  /* Set search range to [min + 1, max] */
	  min = ++mid;
	}
    }

  if (found)
    {
      assert_release (mid < meter->m_tracked_res_count);

      /* Update amount for resource */
      meter->m_tracked_res[mid].amount += amount;
      if (meter->m_tracked_res[mid].amount == 0)
	{
	  /* Remove tracked resource */
	  mem_size = (meter->m_tracked_res_count - 1 - mid) * sizeof (THREAD_TRACKED_RESOURCE);

	  if (mem_size > 0)
	    {
	      memmove (&meter->m_tracked_res[mid], &meter->m_tracked_res[mid + 1], mem_size);
	    }
	  meter->m_tracked_res_count--;
	}
    }
  else
    {
      /* Add new tracked resource */
      assert_release (mid <= meter->m_tracked_res_count);
      if (meter->m_tracked_res_count == meter->m_tracked_res_capacity)
	{
	  /* No more room for new resources */
	  return;
	}
      /* Try to free the memory space for new resource */
      mem_size = (meter->m_tracked_res_count - mid) * sizeof (THREAD_TRACKED_RESOURCE);
      if (mem_size > 0)
	{
	  memmove (&meter->m_tracked_res[mid + 1], &meter->m_tracked_res[mid], mem_size);
	}
      /* Save new resource */
      meter->m_tracked_res[mid].res_ptr = ptr;
      meter->m_tracked_res[mid].amount = amount;
      meter->m_tracked_res[mid].caller_line = caller_line;
      strncpy (meter->m_tracked_res[mid].caller_file, p, THREAD_TRACKED_RES_CALLER_FILE_MAX_SIZE);
      meter->m_tracked_res[mid].caller_file[THREAD_TRACKED_RES_CALLER_FILE_MAX_SIZE - 1] = '\0';
      meter->m_tracked_res_count++;
    }
}
#endif /* !NDEBUG */

/*
 * thread_trace_on () -
 *   return:
 *   thread_p(in):
 */
void
thread_trace_on (THREAD_ENTRY * thread_p)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  thread_p->on_trace = true;
}

/*
 * thread_set_trace_format () -
 *   return:
 *   thread_p(in):
 *   format(in):
 */
void
thread_set_trace_format (THREAD_ENTRY * thread_p, int format)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  thread_p->trace_format = format;
}

/*
 * thread_is_on_trace () -
 *   return:
 *   thread_p(in):
 */
bool
thread_is_on_trace (THREAD_ENTRY * thread_p)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  return thread_p->on_trace;
}

/*
 * thread_set_clear_trace () -
 *   return:
 *   thread_p(in):
 *   clear(in):
 */
void
thread_set_clear_trace (THREAD_ENTRY * thread_p, bool clear)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  thread_p->clear_trace = clear;
}

/*
 * thread_need_clear_trace() -
 *   return:
 *   thread_p(in):
 */
bool
thread_need_clear_trace (THREAD_ENTRY * thread_p)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  return thread_p->clear_trace;
}

/*
 * thread_get_recursion_depth() -
 */
int
thread_get_recursion_depth (THREAD_ENTRY * thread_p)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  return thread_p->xasl_recursion_depth;
}

/*
 * thread_inc_recursion_depth() -
 */
void
thread_inc_recursion_depth (THREAD_ENTRY * thread_p)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  thread_p->xasl_recursion_depth++;
}

/*
 * thread_dec_recursion_depth() -
 */
void
thread_dec_recursion_depth (THREAD_ENTRY * thread_p)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  thread_p->xasl_recursion_depth--;
}

/*
 * thread_clear_recursion_depth() -
 */
void
thread_clear_recursion_depth (THREAD_ENTRY * thread_p)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  thread_p->xasl_recursion_depth = 0;
}

/*
 * thread_iterate () - thread iterator
 *
 * return        : next thread entry
 * thread_p (in) : current thread entry
 */
THREAD_ENTRY *
thread_iterate (THREAD_ENTRY * thread_p)
{
  int index = 1;		// iteration starts with thread index = 1

  if (!thread_Manager.initialized)
    {
      assert (false);
      return NULL;
    }

  if (thread_p != NULL)
    {
      index = thread_p->index + 1;
    }
  if (index >= thread_num_total_threads ())
    {
      assert (index == thread_num_total_threads ());
      return NULL;
    }
  return thread_find_entry_by_index (index);
}
