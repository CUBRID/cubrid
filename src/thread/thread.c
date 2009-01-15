/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

/*
 * thread.c - Thread management module at the server
 */

#ident "$Id$"

#include "config.h"

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

#include "porting.h"
#include "connection_error.h"
#include "job_queue.h"
#include "thread_impl.h"
#include "critical_section.h"
#include "system_parameter.h"
#include "memory_alloc.h"
#include "environment_variable.h"
#include "connection_defs.h"
#include "storage_common.h"
#include "page_buffer.h"
#include "lock_manager.h"
#include "log_impl.h"
#include "log_manager.h"
#include "boot_sr.h"
#include "transaction_sr.h"
#include "boot_sr.h"
#include "connection_sr.h"
#include "server_support.h"
#if defined(WINDOWS)
#include "wintcp.h"
#else /* WINDOWS */
#include "tcp.h"
#endif /* WINDOWS */

#define NUM_WORKER_THREADS (css_Thread_manager.nthreads)
#define NUM_DAEMON_THREADS (NUM_PRE_DEFINED_THREADS)
#define NUM_WORKER_THREADS_PLUS_DAEMON_THREADS (NUM_WORKER_THREADS \
                                               + NUM_DAEMON_THREADS)
#define NUM_THREADS (NUM_WORKER_THREADS_PLUS_DAEMON_THREADS + 1)

#if defined(HPUX)
#define thread_initialize_key()
#endif /* HPUX */

/* Structure used to hold on to relevant information for oob requests */
typedef struct css_deferred_request CSS_DEFERRED_REQUEST;
struct css_deferred_request
{
  int client_id;
  int transaction_id;
  int request_id;
};

/* Thread Manager structure */
typedef struct css_thread_manager CSS_THREAD_MANAGER;
struct css_thread_manager
{
  int nthreads;
  THREAD_ENTRY *thread_array;	/* thread entry array */
  bool initialized;
};

static const int CSS_SLAM_STATUS_INTERRUPTED = 0;
static const int CSS_RETRY_MAX_SLAM_TIMES = 10;

#if defined(HPUX)
static __thread THREAD_ENTRY *tsd_ptr;
#else /* HPUX */
static THREAD_KEY_T css_Thread_key;
#endif /* HPUX */

static CSS_THREAD_MANAGER css_Thread_manager;

/*
 * For special Purpose Threads: deadlock detector, checkpoint daemon
 *    Under the win32-threads system, *_cond variables are an auto-reset event
 */
static DAEMON_THREAD_MONITOR css_Deadlock_detect_thread =
  { 0, true, false, false, MUTEX_INITIALIZER, COND_INITIALIZER };
static DAEMON_THREAD_MONITOR css_Checkpoint_thread =
  { 0, false, false, false, MUTEX_INITIALIZER, COND_INITIALIZER };
static DAEMON_THREAD_MONITOR css_Oob_thread =
  { 0, true, true, false, MUTEX_INITIALIZER, COND_INITIALIZER };
static DAEMON_THREAD_MONITOR css_Page_flush_thread =
  { 0, false, false, false, MUTEX_INITIALIZER, COND_INITIALIZER };
DAEMON_THREAD_MONITOR css_Log_flush_thread =
  { 0, false, false, false, MUTEX_INITIALIZER, COND_INITIALIZER };

#if defined(WINDOWS)
/*
 * Because WINDOWS threads don't have static mutex initializer,
 * we must initialize mutex when MUTEX_LOCK() is called at first time.
 * This variable is used in that moment to syncronize CREATE_MUTEX.
 */
HANDLE css_Internal_mutex_for_mutex_initialize;
#endif /* WINDOWS */

static int thread_initialize_entry (THREAD_ENTRY * entry_ptr);
static int thread_finalize_entry (THREAD_ENTRY * entry_ptr);

static THREAD_ENTRY *thread_find_entry_by_tran_index (int tran_index);
static int thread_slam_tran_index (THREAD_ENTRY * thread_p, int tran_index);

#if defined(WINDOWS)
static unsigned __stdcall thread_deadlock_detect_thread (void *);
static unsigned __stdcall thread_checkpoint_thread (void *);
static unsigned __stdcall thread_page_flush_thread (void *);
static unsigned __stdcall thread_log_flush_thread (void *);
static int css_initialize_sync_object (void);
#else /* WINDOWS */
static void *thread_deadlock_detect_thread (void *);
static void *thread_checkpoint_thread (void *);
static void *thread_page_flush_thread (void *);
static void *thread_log_flush_thread (void *);
#endif /* WINDOWS */

/* AsyncCommit */
static int thread_get_LFT_min_wait_time ();


/*
 * Thread Specific Data management
 *
 * All kind of thread has it's own information like request id, error code,
 * synchronization informations, etc. We use THREAD_ENTRY structure
 * which saved as TSD(thread specific data) to manage these informations.
 * Global thread manager(thread_mgr) has an array of these entries which is
 * initialized by the 'thread_mgr'.
 * Each worker thread picks one up from this array.
 */

#if defined(HPUX)
/*
 * thread_set_current_thread_info() - assign transaction infomation to current
 *                                 thread entry
 *   return: void
 *   client_id(in): client id
 *   rid(in): request id
 *   tran_index(in): transaction index
 */
void
thread_set_current_thread_info (int client_id, unsigned int request_id,
				int tran_index)
{
  tsd_ptr->client_id = client_id;
  tsd_ptr->rid = request_id;
  tsd_ptr->tran_index = tran_index;
  tsd_ptr->victim_request_fail = false;
  tsd_ptr->next_wait_thrd = NULL;
  tsd_ptr->lockwait = NULL;
  tsd_ptr->lockwait_state = -1;
  tsd_ptr->query_entry = NULL;
  tsd_ptr->tran_next_wait = NULL;
}

/*
 * thread_get_thread_entry_info() - retrieve TSD of it's own.
 *   return: thread entry
 */
THREAD_ENTRY *
thread_get_thread_entry_info ()
{
  return tsd_ptr;
}
#else /* HPUX */
/*
 * thread_initialize_key() - allocates a key for TSD
 *   return: 0 if no error, or error code
 */
static int
thread_initialize_key (void)
{
  int r;

  r = TLS_KEY_ALLOC (css_Thread_key, NULL);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_KEY_CREATE);

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
  int r = NO_ERROR;

  r = TLS_SET_SPECIFIC (css_Thread_key, (void *) entry_p);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_SETSPECIFIC);

  return r;
}

/*
 * thread_set_current_thread_info() - assign transaction infomation to current
 *                                 thread entry
 *   return: void
 *   client_id(in): client id
 *   rid(in): request id
 *   tran_index(in): transaction index
 */
void
thread_set_current_thread_info (int client_id, unsigned int request_id,
				int tran_index)
{
  THREAD_ENTRY *thread_p;

  thread_p = thread_get_thread_entry_info ();
  CSS_ASSERT (thread_p != NULL);

  thread_p->client_id = client_id;
  thread_p->rid = request_id;
  thread_p->tran_index = tran_index;

  /* for page latch */
  thread_p->victim_request_fail = false;
  thread_p->next_wait_thrd = NULL;

  thread_p->lockwait = NULL;
  thread_p->lockwait_state = -1;
  thread_p->query_entry = NULL;
  thread_p->tran_next_wait = NULL;
}

/*
 * thread_get_thread_entry_info() - retrieve TSD of it's own.
 *   return: thread entry
 */
THREAD_ENTRY *
thread_get_thread_entry_info (void)
{
  void *p;

  p = TLS_GET_SPECIFIC (css_Thread_key);
  return (THREAD_ENTRY *) p;
}
#endif /* HPUX */

/*
 * Thread Manager related functions
 *
 * Global thread manager, thread_mgr, related functions. It creates/destroys
 * TSD and takes control over actual threads, for example master, worker,
 * oob-handler.
 */

/*
 * thread_is_manager_initialized() -
 *   return:
 */
int
thread_is_manager_initialized (void)
{
  return css_Thread_manager.initialized;
}

/*
 * thread_initialize_manager() - Create and initialize all necessary threads.
 *   return: 0 if no error, or error code
 *   nthreads(in): number of service handler thread
 *
 * Note: It includes a main thread, service handler, a deadlock detector
 *       and a checkpoint daemon. Some other threads like signal handler
 *       might be needed later.
 */
int
thread_initialize_manager (int nthreads)
{
  int i, r;
#if !defined(HPUX)
  THREAD_ENTRY *tsd_ptr;
#endif /* not HPUX */

  if (css_Thread_manager.initialized == false)
    {
      r = thread_initialize_key ();
      if (r != NO_ERROR)
	{
	  return r;
	}

#if defined(WINDOWS)
      r = MUTEX_INIT (css_Internal_mutex_for_mutex_initialize);
      CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_INIT);
      css_initialize_sync_object ();
#endif /* WINDOWS */

#ifdef CHECK_MUTEX
      r = MUTEXATTR_SETTYPE (mattr, PTHREAD_MUTEX_ERRORCHECK);
      CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEXATTR_SETTYPE);
#endif /* CHECK_MUTEX */
    }
  else
    {
      /* destroy mutex and cond */
      for (i = 1; i <= NUM_WORKER_THREADS_PLUS_DAEMON_THREADS; i++)
	{
	  r = thread_finalize_entry (&css_Thread_manager.thread_array[i]);
	  if (r != NO_ERROR)
	    {
	      return r;
	    }
	}
      r = thread_finalize_entry (&css_Thread_manager.thread_array[0]);
      if (r != NO_ERROR)
	{
	  return r;
	}
      free_and_init (css_Thread_manager.thread_array);
      css_Thread_manager.thread_array = NULL;
    }

  css_Thread_manager.nthreads = nthreads;

  /* main thread + nthreads * service thread + deadlock detector
     + checkpoint daemon + sig handler
     + page flush thread + log flush thread */
  tsd_ptr = css_Thread_manager.thread_array =
    (THREAD_ENTRY *) malloc (NUM_THREADS * sizeof (THREAD_ENTRY));
  if (tsd_ptr == NULL)
    {
      CSS_CHECK_RETURN_ERROR (-1, ER_OUT_OF_VIRTUAL_MEMORY);
    }

  /* init master thread */
  r = thread_initialize_entry (tsd_ptr);
  if (r != NO_ERROR)
    {
      return r;
    }

  tsd_ptr->index = 0;
  tsd_ptr->tid = THREAD_ID ();
  tsd_ptr->status = TS_RUN;
  tsd_ptr->resume_status = RESUME_OK;
  tsd_ptr->tran_index = 0;	/* system transaction */
  thread_set_thread_entry_info (tsd_ptr);

  /* init worker/deadlock-detection/checkpoint daemon/audit-flush
     oob-handler thread/page flush thread/log flush thread
     thread_mgr.thread_array[0] is used for main thread */
  for (i = 1; i <= NUM_WORKER_THREADS_PLUS_DAEMON_THREADS; i++)
    {
      r = thread_initialize_entry (&css_Thread_manager.thread_array[i]);
      if (r != NO_ERROR)
	{
	  return r;
	}

      css_Thread_manager.thread_array[i].index = i;
    }

  css_Thread_manager.initialized = true;

  return NO_ERROR;
}

/*
 * thread_start_workers() - Boot up every threads.
 *   return: 0 if no error, or error code
 *
 * Note: All threads are set ready to execute when activation condition is
 *       satisfied.
 */
int
thread_start_workers (void)
{
  int i, r;
  THREAD_ENTRY *thread_p;
#if !defined(WINDOWS)
  THREAD_ATTR_T thread_attr;
  size_t ts_size;
#endif /* not WINDOWS */

#if !defined(WINDOWS)
  r = THREAD_ATTR_INIT (thread_attr);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_ATTR_INIT);

  r = THREAD_ATTR_SETDETACHSTATE (thread_attr, PTHREAD_CREATE_DETACHED);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_ATTR_SETDETACHSTATE);

#if defined(AIX)
  /* AIX's pthread is slightly different from other systems.
     Its performance highly depends on the pthread's scope and it's related
     kernel parameters. */
  r = THREAD_ATTR_SETSCOPE (thread_attr,
			    PRM_PTHREAD_SCOPE_PROCESS ?
			    PTHREAD_SCOPE_PROCESS : PTHREAD_SCOPE_SYSTEM);
#else /* AIX */
  r = THREAD_ATTR_SETSCOPE (thread_attr, PTHREAD_SCOPE_SYSTEM);
#endif /* AIX */
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_ATTR_SETSCOPE);

/* Sun Solaris allocates 1M for a thread stack, and it is quite enough */
#if !defined(sun) && !defined(SOLARIS)
#if defined(_POSIX_THREAD_ATTR_STACKSIZE)
  r = THREAD_ATTR_GETSTACKSIZE (thread_attr, ts_size);
  if (ts_size < (size_t) PRM_THREAD_STACKSIZE)
    {
      r = THREAD_ATTR_SETSTACKSIZE (thread_attr, PRM_THREAD_STACKSIZE);
      CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_ATTR_SETSTACKSIZE);

      THREAD_ATTR_GETSTACKSIZE (thread_attr, ts_size);
    }
#endif /* _POSIX_THREAD_ATTR_STACKSIZE */
#endif /* not sun && not SOLARIS */
#endif /* WINDOWS */

  /* start worker thread */
  for (i = 1; i <= NUM_WORKER_THREADS; i++)
    {
      thread_p = &css_Thread_manager.thread_array[i];
      MUTEX_LOCK (r, thread_p->th_entry_lock);
      CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_LOCK);
      /* If win32, then "thread_attr" is ignored, else "p->thread_handle". */
      r = THREAD_CREATE (thread_p->thread_handle, &thread_attr,
			 thread_worker, thread_p, &(thread_p->tid));
      CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_CREATE);

      r = MUTEX_UNLOCK (thread_p->th_entry_lock);
      CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_UNLOCK);
    }

  /* start deadlock detection thread */
  css_Deadlock_detect_thread.thread_index = i++;	/* thread_mgr.nthreads+1 */
  thread_p =
    &css_Thread_manager.thread_array[css_Deadlock_detect_thread.thread_index];
  MUTEX_LOCK (r, thread_p->th_entry_lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_LOCK);
  r = THREAD_CREATE (thread_p->thread_handle, &thread_attr,
		     thread_deadlock_detect_thread, thread_p,
		     &(thread_p->tid));
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_CREATE);
  r = MUTEX_UNLOCK (thread_p->th_entry_lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_UNLOCK);

  /* start checkpoint daemon thread */
  css_Checkpoint_thread.thread_index = i++;	/* thread_mgr.nthreads+2 */
  thread_p =
    &css_Thread_manager.thread_array[css_Checkpoint_thread.thread_index];
  MUTEX_LOCK (r, thread_p->th_entry_lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_LOCK);
  r = THREAD_CREATE (thread_p->thread_handle, &thread_attr,
		     thread_checkpoint_thread, thread_p, &(thread_p->tid));
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_CREATE);
  r = MUTEX_UNLOCK (thread_p->th_entry_lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_UNLOCK);

  /* start oob-handler thread */
  css_Oob_thread.thread_index = i++;	/* thread_mgr.nthreads+3 */
  thread_p = &css_Thread_manager.thread_array[css_Oob_thread.thread_index];
  MUTEX_LOCK (r, thread_p->th_entry_lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_LOCK);
  r = THREAD_CREATE (thread_p->thread_handle, &thread_attr,
		     css_oob_handler_thread, thread_p, &(thread_p->tid));
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_CREATE);
  r = MUTEX_UNLOCK (thread_p->th_entry_lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_UNLOCK);

  /* start page flush daemon thread */
  css_Page_flush_thread.thread_index = i++;	/* thread_mgr.nthreads+4 */
  thread_p =
    &css_Thread_manager.thread_array[css_Page_flush_thread.thread_index];
  MUTEX_LOCK (r, thread_p->th_entry_lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_LOCK);
  r = THREAD_CREATE (thread_p->thread_handle, &thread_attr,
		     thread_page_flush_thread, thread_p, &(thread_p->tid));
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_CREATE);
  r = MUTEX_UNLOCK (thread_p->th_entry_lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_UNLOCK);

  /* start log flush thread */
  css_Log_flush_thread.thread_index = i++;	/* thread_mgr.nthreads+5 */
  thread_p =
    &css_Thread_manager.thread_array[css_Log_flush_thread.thread_index];
  MUTEX_LOCK (r, thread_p->th_entry_lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_LOCK);
  r = THREAD_CREATE (thread_p->thread_handle, &thread_attr,
		     thread_log_flush_thread, thread_p, &(thread_p->tid));
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_CREATE);
  r = MUTEX_UNLOCK (thread_p->th_entry_lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_UNLOCK);

  /* destroy thread_attribute */
  r = THREAD_ATTR_DESTROY (thread_attr);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_ATTR_DESTROY);

  return NO_ERROR;
}

/*
 * thread_stop_active_workers() - Stop active work thread.
 *   return: 0 if no error, or error code
 *
 * Node: This function is invoked when system is going shut down.
 */
int
thread_stop_active_workers (void)
{
  int i, count;
  bool repeat_loop;
  THREAD_ENTRY *thread_p;

  css_block_all_active_conn ();

  count = 0;
loop:
  for (i = 1; i <= NUM_WORKER_THREADS; i++)
    {
      thread_p = &css_Thread_manager.thread_array[i];
      if (thread_p->tran_index != -1)
	{
	  logtb_set_tran_index_interrupt (NULL, thread_p->tran_index, 1);
	  if (thread_p->status == TS_WAIT)
	    {
	      thread_wakeup (thread_p);
	    }
	  thread_sleep (0, 100);
	}
    }

  thread_sleep (0, 100);
  lock_force_timeout_lock_wait_transactions ();

  /* Signal for blocked on job queue */
  /* css_broadcast_shutdown_thread(); */

  repeat_loop = false;
  for (i = 1; i <= NUM_WORKER_THREADS; i++)
    {
      thread_p = &css_Thread_manager.thread_array[i];
      if (thread_p->status != TS_FREE)
	{
	  repeat_loop = true;
	}
    }

  if (repeat_loop)
    {
      if (count++ > 60)
	{
	  /* exit process after some tries */
	  _exit (0);
	}
      thread_sleep (1, 0);
      goto loop;
    }

  return NO_ERROR;
}

/*
 * thread_stop_active_daemons() - Stop deadlock detector/checkpoint threads
 *   return: 0 if no error, or error code
 */
int
thread_stop_active_daemons (void)
{
  int i, count;
  bool repeat_loop;
  int idx;
  THREAD_ENTRY *thread_p;

  count = 0;
  for (i = 0; i < NUM_DAEMON_THREADS; i++)
    {
      idx = NUM_WORKER_THREADS + i + 1;	/* 1 for system thread */
      thread_p = &css_Thread_manager.thread_array[idx];
      thread_p->shutdown = true;
    }

  thread_wakeup_deadlock_detect_thread ();
  thread_wakeup_checkpoint_thread ();
  thread_wakeup_oob_handler_thread ();
  thread_wakeup_page_flush_thread ();
  thread_wakeup_log_flush_thread ();

loop:
  repeat_loop = false;
  for (i = 0; i < NUM_DAEMON_THREADS; i++)
    {
      idx = NUM_WORKER_THREADS + i + 1;	/* 1 for system thread */
      thread_p = &css_Thread_manager.thread_array[idx];
      if (thread_p->status != TS_DEAD)
	{
	  repeat_loop = true;
	}
    }

  if (repeat_loop)
    {
      if (count++ > 30)
	{
	  /* exit process after some tries */
	  _exit (0);
	}
      thread_sleep (1, 0);
      goto loop;
    }

  return NO_ERROR;
}

/*
 * thread_kill_all_workers() - Signal all worker threads to exit.
 *   return: 0 if no error, or error code
 */
int
thread_kill_all_workers (void)
{
  int i, count;
  bool repeat_loop;
  THREAD_ENTRY *thread_p;

  for (i = 1; i <= NUM_WORKER_THREADS; i++)
    {
      thread_p = &css_Thread_manager.thread_array[i];
      thread_p->interrupted = true;
      thread_p->shutdown = true;
    }

  count = 0;
#if !defined(WINDOWS)
loop:
#endif /* !WINDOWS */

  /* Signal for blocked on job queue */
  css_broadcast_shutdown_thread ();

#if defined(WINDOWS)
loop:
#endif /* WINDOWS */
  repeat_loop = false;
  for (i = 1; i <= NUM_WORKER_THREADS; i++)
    {
      thread_p = &css_Thread_manager.thread_array[i];
      if (thread_p->status != TS_DEAD)
	{
	  repeat_loop = true;
	}
    }

  if (repeat_loop)
    {
      if (count++ > 60)
	{
	  /* exit process after some tries */
	  _exit (0);
	}
      thread_sleep (1, 0);
      goto loop;
    }

  return NO_ERROR;
}

/*
 * thread_final_manager() -
 *   return: void
 */
void
thread_final_manager (void)
{
  int i;

  for (i = 1; i <= NUM_WORKER_THREADS_PLUS_DAEMON_THREADS; i++)
    {
      (void) thread_finalize_entry (&css_Thread_manager.thread_array[i]);
    }
  (void) thread_finalize_entry (&css_Thread_manager.thread_array[0]);
  free_and_init (css_Thread_manager.thread_array);
  css_Thread_manager.thread_array = NULL;

#ifndef HPUX
  TLS_KEY_FREE (css_Thread_key);
#endif /* not HPUX */
}

/*
 * thread_initialize_entry() - Initialize thread entry
 *   return: void
 *   entry_ptr(in): thread entry to initialize
 */
static int
thread_initialize_entry (THREAD_ENTRY * entry_p)
{
  int r;

  entry_p->index = -1;
  entry_p->tid = NULL_THREAD_T;
  entry_p->client_id = -1;
  entry_p->tran_index = -1;
  r = MUTEX_INIT (entry_p->tran_index_lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_INIT);

  entry_p->rid = 0;
  entry_p->status = TS_DEAD;
  entry_p->interrupted = false;
  entry_p->shutdown = false;
  entry_p->cnv_adj_buffer[0] = NULL;
  entry_p->cnv_adj_buffer[1] = NULL;
  entry_p->cnv_adj_buffer[2] = NULL;
  entry_p->conn_entry = NULL;
  entry_p->worker_thrd_list = NULL;

#ifdef CHECK_MUTEX
  r = MUTEX_INIT_WITH_ATT (entry_p->th_entry_lock, mattr);
#else /* not CHECK_MUTEX */
  r = MUTEX_INIT (entry_p->th_entry_lock);
#endif /* not CHECK_MUTEX */
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_INIT);

  r = COND_INIT (entry_p->wakeup_cond);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_COND_INIT);

  entry_p->resume_status = RESUME_OK;
  entry_p->er_Msg = NULL;
  entry_p->victim_request_fail = false;
  entry_p->next_wait_thrd = NULL;

  entry_p->lockwait = NULL;
  entry_p->lockwait_state = -1;
  entry_p->query_entry = NULL;
  entry_p->tran_next_wait = NULL;

  entry_p->check_interrupt = true;
  entry_p->type = TT_WORKER;	/* init */

  entry_p->private_heap_id = db_create_private_heap ();
  if (entry_p->private_heap_id == 0)
    {
      return ER_CSS_ALLOC;
    }

  entry_p->instant_heap_id = db_create_instant_heap ();
  if (entry_p->instant_heap_id == 0)
    {
      return ER_CSS_ALLOC;
    }

  return NO_ERROR;
}

/*
 * thread_finalize_entry() -
 *   return:
 *   entry_p(in):
 */
static int
thread_finalize_entry (THREAD_ENTRY * entry_p)
{
  int r, i;

  entry_p->index = -1;
  entry_p->tid = NULL_THREAD_T;
  entry_p->client_id = -1;
  entry_p->tran_index = -1;
  entry_p->rid = 0;
  entry_p->status = TS_DEAD;
  entry_p->interrupted = false;
  entry_p->shutdown = false;

  for (i = 0; i < 3; i++)
    {
      if (entry_p->cnv_adj_buffer[i] != NULL)
	{
	  adj_ar_free (entry_p->cnv_adj_buffer[i]);
	  entry_p->cnv_adj_buffer[i] = NULL;
	}
    }

  entry_p->conn_entry = NULL;

  r = MUTEX_DESTROY (entry_p->tran_index_lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_DESTROY);
  r = MUTEX_DESTROY (entry_p->th_entry_lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_DESTROY);
  r = COND_DESTROY (entry_p->wakeup_cond);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_COND_DESTROY);
  entry_p->resume_status = RESUME_OK;

  entry_p->check_interrupt = true;

  db_destroy_private_heap (entry_p, entry_p->private_heap_id);
  db_destroy_instant_heap (entry_p, entry_p->instant_heap_id);

  return NO_ERROR;
}

/*
 * thread_print_entry_info() -
 *   return: void
 *   thread_p(in):
 */
void
thread_print_entry_info (THREAD_ENTRY * thread_p)
{
  fprintf (stderr,
	   "THREAD_ENTRY(tid(%ld),client_id(%d),tran_index(%d),rid(%d),status(%d))\n",
	   thread_p->tid, thread_p->client_id, thread_p->tran_index,
	   thread_p->rid, thread_p->status);

  if (thread_p->conn_entry != NULL)
    {
      css_print_conn_entry_info (thread_p->conn_entry);
    }
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
static THREAD_ENTRY *
thread_find_entry_by_tran_index_except_me (int tran_index)
{
  THREAD_ENTRY *thread_p;
  int i;
  THREAD_T me = THREAD_ID ();

  for (i = 0; i <= NUM_WORKER_THREADS; i++)
    {
      thread_p = &css_Thread_manager.thread_array[i];
      if (thread_p->tran_index == tran_index && thread_p->tid != me)
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
static THREAD_ENTRY *
thread_find_entry_by_tran_index (int tran_index)
{
  THREAD_ENTRY *thread_p;
  int i;

  for (i = 0; i <= NUM_WORKER_THREADS; i++)
    {
      thread_p = &css_Thread_manager.thread_array[i];
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
  CSS_ASSERT (thread_p != NULL);

  return thread_p->index;
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
  CSS_ASSERT (thread_p != NULL);

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

  CSS_ASSERT (thread_p != NULL);

  thread_p->tran_index = tran_index;
}

void
thread_set_tran_index (THREAD_ENTRY * thread_p, int tran_index)
{
  if (thread_p == NULL)
    {
      thread_set_current_tran_index (thread_p, tran_index);
    }
  else
    {
      thread_p->tran_index = tran_index;
    }
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
  CSS_ASSERT (thread_p != NULL);

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

  CSS_ASSERT (thread_p != NULL);

  MUTEX_LOCK (r, thread_p->th_entry_lock);
  if (r < 0)
    {
      CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_LOCK);
    }

  return r;
}

/*
 * thread_lock_entry_with_tran_index() -
 *   return:
 *   tran_index(in):
 */
int
thread_lock_entry_with_tran_index (int tran_index)
{
  int r;
  THREAD_ENTRY *thread_p;

  thread_p = thread_find_entry_by_tran_index (tran_index);
  CSS_ASSERT (thread_p != NULL);

  MUTEX_LOCK (r, thread_p->th_entry_lock);
  if (r < 0)
    {
      CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_LOCK);
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

  CSS_ASSERT (thread_p != NULL);

  r = MUTEX_UNLOCK (thread_p->th_entry_lock);
  if (r < 0)
    {
      CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_UNLOCK);
    }

  return r;
}

/*
 * thread_suspend_wakeup_and_unlock_entry() -
 *   return:
 *   thread_p(in):
 *
 * Note: this function must be called by current thread
 *       also, the lock must have already been acquired.
 */
int
thread_suspend_wakeup_and_unlock_entry (THREAD_ENTRY * thread_p)
{
  int r;

  CSS_ASSERT (thread_p->status == TS_RUN);
  thread_p->status = TS_WAIT;

  r = COND_WAIT (thread_p->wakeup_cond, thread_p->th_entry_lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_COND_WAIT);

#if defined(WINDOWS)
  MUTEX_LOCK (r, thread_p->th_entry_lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_LOCK);
#endif /* WINDOWS */
  thread_p->status = TS_RUN;

  r = MUTEX_UNLOCK (thread_p->th_entry_lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_UNLOCK);

  return NO_ERROR;
}

/*
 * thread_suspend_timeout_wakeup_and_unlock_entry() -
 *   return:
 *   thread_p(in):
 *   time_p(in):
 */
int
thread_suspend_timeout_wakeup_and_unlock_entry (THREAD_ENTRY *
						thread_p, void *time_p)
{
  int r1, r2;
#if defined(WINDOWS)
  int tmp_timespec;
#else /* WINDOWS */
  struct timespec tmp_timespec;
#endif /* WINDOWS */

#if defined(WINDOWS) && defined(SERVER_MODE)
  int rv;
#endif /* WINDOWS && SERVER_MODE */

  CSS_ASSERT (thread_p->status == TS_RUN);
  thread_p->status = TS_WAIT;

#if defined(WINDOWS)
  tmp_timespec = *(int *) time_p;
#else /* WINDOWS */
  memcpy (&tmp_timespec, time_p, sizeof (struct timespec));
#endif /* WINDOWS */

  r1 = COND_TIMEDWAIT (thread_p->wakeup_cond, thread_p->th_entry_lock,
		       tmp_timespec);
  if (r1 != ETIMEDOUT)
    {
      CSS_CHECK_RETURN_ERROR (r1, ER_CSS_PTHREAD_COND_TIMEDWAIT);
      r1 = NO_ERROR;
    }
#if defined(WINDOWS)
  MUTEX_LOCK (rv, thread_p->th_entry_lock);
#endif /* WINDOWS */

  thread_p->status = TS_RUN;

  r2 = MUTEX_UNLOCK (thread_p->th_entry_lock);
  CSS_CHECK_RETURN_ERROR (r2, ER_CSS_PTHREAD_MUTEX_UNLOCK);

  return r1;
}

/*
 * thread_suspend_wakeup_and_unlock_entry_with_tran_index() -
 *   return: 0 if no error, or error code
 *   tran_index(in):
 */
int
thread_suspend_wakeup_and_unlock_entry_with_tran_index (int tran_index)
{
  THREAD_ENTRY *thread_p;
  int r;

  thread_p = thread_find_entry_by_tran_index (tran_index);

  /*
   * this function must be called by current thread
   * also, the lock must have already been acquired before.
   */
  CSS_ASSERT (thread_p->status == TS_RUN);
  thread_p->status = TS_WAIT;

  r = COND_WAIT (thread_p->wakeup_cond, thread_p->th_entry_lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_COND_WAIT);
#if defined(WINDOWS)
  MUTEX_LOCK (r, thread_p->th_entry_lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_LOCK);
#endif /* WINDOWS */

  thread_p->status = TS_RUN;

  r = MUTEX_UNLOCK (thread_p->th_entry_lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_UNLOCK);

  return NO_ERROR;
}

/*
 * thread_wakeup() -
 *   return:
 *   thread_p(in):
 */
int
thread_wakeup (THREAD_ENTRY * thread_p)
{
  int r = NO_ERROR;

  r = thread_lock_entry (thread_p);
  CSS_CHECK_RETURN_ERROR (r, r);

  r = COND_SIGNAL (thread_p->wakeup_cond);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_COND_SIGNAL);

  r = thread_unlock_entry (thread_p);
  CSS_CHECK_RETURN_ERROR (r, r);

  return r;
}

/*
 * thread_wakeup_with_tran_index() -
 *   return:
 *   tran_index(in):
 */
int
thread_wakeup_with_tran_index (int tran_index)
{
  THREAD_ENTRY *thread_p;
  int r = NO_ERROR;

  thread_p = thread_find_entry_by_tran_index_except_me (tran_index);
  if (thread_p == NULL)
    {
      return r;
    }

  r = thread_lock_entry (thread_p);
  CSS_CHECK_RETURN_ERROR (r, r);

  r = COND_SIGNAL (thread_p->wakeup_cond);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_COND_SIGNAL);

  thread_unlock_entry (thread_p);
  CSS_CHECK_RETURN_ERROR (r, r);

  return r;
}

/*
 * thread_wait() - wait until func return TRUE.
 *   return: void
 *   func(in) : a pointer to a function that will return a non-zero value when
 *	        the thread should resume execution.
 *   arg(in)  : an integer argument to be passed to func.
 *
 * Note: The thread is blocked for execution until func returns a non-zero
 *       value. Halts exection of the currently running thread.
 */
void
thread_wait (THREAD_ENTRY * thread_p, CSS_THREAD_FN func, CSS_THREAD_ARG arg)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  while ((*func) (thread_p, arg) == false && thread_p->interrupted != true
	 && thread_p->shutdown != true)
    {
      thread_sleep (0, 10);
    }
}

/*
 * thread_suspend_with_other_mutex() -
 *   return: 0 if no error, or error code
 *   thread_p(in):
 *   mutex_p():
 */
int
thread_suspend_with_other_mutex (THREAD_ENTRY * thread_p, MUTEX_T * mutex_p)
{
  int r;

  CSS_ASSERT (thread_p != NULL);
  CSS_ASSERT (thread_p->status == TS_RUN);

  MUTEX_LOCK (r, thread_p->th_entry_lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_LOCK);

  thread_p->status = TS_WAIT;

  r = MUTEX_UNLOCK (*mutex_p);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_UNLOCK);

  r = COND_WAIT (thread_p->wakeup_cond, thread_p->th_entry_lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_COND_WAIT);
#if defined(WINDOWS)
  MUTEX_LOCK (r, thread_p->th_entry_lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_LOCK);
#endif /* WINDOWS */

  MUTEX_LOCK (r, *mutex_p);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_LOCK);

  thread_p->status = TS_RUN;

  r = MUTEX_UNLOCK (thread_p->th_entry_lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_UNLOCK);

  return NO_ERROR;
}

/*
 * thread_sleep() - Halts the currently running thread for <seconds> +
 *                      <microseconds>
 *   return: void
 *   seconds(in): The number of seconds for the thread to sleep
 *   microseconds(in): The number of microseconds for the thread to sleep
 *
 *  Note: Used to temporarly halt the current process.
 */
void
thread_sleep (int seconds, int microseconds)
{
#if defined(WINDOWS)
  int to;

  if (microseconds < 1000)
    {
      microseconds = 1000;
    }

  to = seconds * 1000 + microseconds / 1000;
  Sleep (to);
#else /* WINDOWS */
  struct timeval to;

  to.tv_sec = seconds;
  to.tv_usec = microseconds;

  select (0, NULL, NULL, NULL, &to);
#endif /* WINDOWS */
}

/*
 * thread_exit() - The program will exit.
 *   return: void
 *   exit_id(in): an integer argument to be returned as the exit value.
 */
void
thread_exit (int exit_id)
{
  THREAD_EXIT (exit_id);
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

  CSS_ASSERT (thread_p != NULL);

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

  CSS_ASSERT (thread_p != NULL);

  return thread_p->rid;
}

/*
 * thread_set_comm_request_id() - sets the comm system request id to the client request
  *                     that started the thread
 *   return: void
 *   request_id(in): the comm request id to save for thread_get_comm_request_id
 *
 * Note: WARN: this function doesn't lock on thread_entry
 */
void
thread_set_comm_request_id (unsigned int request_id)
{
  THREAD_ENTRY *thread_p;

  thread_p = thread_get_thread_entry_info ();
  CSS_ASSERT (thread_p != NULL);

  thread_p->rid = request_id;
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
thread_has_threads (int tran_index, int client_id)
{
  int i, n, rv;
  THREAD_ENTRY *thread_p;
  CSS_CONN_ENTRY *conn_p;

  if (tran_index == -1)
    {
      return 0;
    }

  for (i = 0, n = 0; i <= NUM_WORKER_THREADS; i++)
    {
      thread_p = &css_Thread_manager.thread_array[i];

      MUTEX_LOCK (rv, thread_p->tran_index_lock);
      n += (thread_p->tid != THREAD_ID ()
	    && thread_p->status != TS_DEAD
	    && thread_p->status != TS_FREE
	    && thread_p->status != TS_CHECK
	    && tran_index == thread_p->tran_index
	    && ((conn_p = thread_p->conn_entry) == NULL
		|| conn_p->client_id == client_id));
      MUTEX_UNLOCK (thread_p->tran_index_lock);
    }

  return n;
}

/*
 * thread_get_info_threads() - get statistics of threads
 *   return: void
 *   num_threads(out):
 *   num_free_threads(out):
 *   num_suspended_threads(out):
 *
 * Note: Find the number of threads, number of suspended threads, and maximum
 *       of threads that can be created.
 *       WARN: this function doesn't lock threadmgr
 */
void
thread_get_info_threads (int *num_threads_p,
			 int *num_free_threads_p,
			 int *num_suspended_threads_p)
{
  int i;
  THREAD_ENTRY *thread_p;

  *num_threads_p = css_Thread_manager.nthreads;
  *num_free_threads_p = 0;
  *num_suspended_threads_p = 0;

  for (i = 1; i <= NUM_WORKER_THREADS; i++)
    {
      thread_p = &css_Thread_manager.thread_array[i];
      *num_free_threads_p += (thread_p->status == TS_FREE);
      *num_suspended_threads_p += (thread_p->status == TS_WAIT);
    }
}

/*
 * thread_dump_threads() - dump all thread
 *   return: void
 *
 * Note: for debug
 *       WARN: this function doesn't lock threadmgr
 */
void
thread_dump_threads (void)
{
  const char *status[] = { "dead", "free", "run", "wait" };
  int i;
  THREAD_ENTRY *thread_p;

  for (i = 0; i <= NUM_WORKER_THREADS; i++)
    {
      thread_p = &css_Thread_manager.thread_array[i];

      fprintf (stderr,
	       "thread %d(tid(%ld),client_id(%d),tran_index(%d),"
	       "rid(%d),status(%s),interrupt(%d))\n",
	       thread_p->index, thread_p->tid, thread_p->client_id,
	       thread_p->tran_index, thread_p->rid,
	       status[thread_p->status], thread_p->interrupted);
    }
}

/*
 * css_get_private_heap () -
 *   return:
 *   thread_p(in):
 */
unsigned int
css_get_private_heap (THREAD_ENTRY * thread_p)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  CSS_ASSERT (thread_p != NULL);

  return thread_p->private_heap_id;
}

/*
 * css_set_private_heap() -
 *   return:
 *   thread_p(in):
 *   heap_id(in):
 */
unsigned int
css_set_private_heap (THREAD_ENTRY * thread_p, unsigned int heap_id)
{
  unsigned int old_heap_id = 0;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  CSS_ASSERT (thread_p != NULL);

  old_heap_id = thread_p->private_heap_id;
  thread_p->private_heap_id = heap_id;

  return old_heap_id;
}

/*
 * css_get_instant_heap () -
 *   return:
 *   thread_p(in):
 */
unsigned int
css_get_instant_heap (THREAD_ENTRY * thread_p)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  CSS_ASSERT (thread_p != NULL);

  return thread_p->instant_heap_id;
}

/*
 * css_set_instant_heap() -
 *   return:
 *   thread_p(in):
 *   heap_id(in):
 */
unsigned int
css_set_instant_heap (THREAD_ENTRY * thread_p, unsigned int heap_id)
{
  unsigned int old_heap_id = 0;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  CSS_ASSERT (thread_p != NULL);

  old_heap_id = thread_p->instant_heap_id;
  thread_p->instant_heap_id = heap_id;

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
  CSS_ASSERT (thread_p != NULL);

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
  CSS_ASSERT (thread_p != NULL);

  thread_p->cnv_adj_buffer[idx] = buffer_p;
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

  if (BO_ISSERVER_RESTARTED ())
    {
      if (thread_p == NULL)
	{
	  thread_p = thread_get_thread_entry_info ();
	}

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

  if (BO_ISSERVER_RESTARTED ())
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
 * thread_worker() - Dequeue request from job queue and then call handler
 *                       function
 *   return:
 *   arg_p(in):
 */
#if defined(WINDOWS)
unsigned __stdcall
thread_worker (void *arg_p)
#else /* WINDOWS */
void *
thread_worker (void *arg_p)
#endif				/* WINDOWS */
{
#if !defined(HPUX)
  THREAD_ENTRY *tsd_ptr;
#endif /* !HPUX */
  CSS_JOB_ENTRY *job_entry_p;
  CSS_THREAD_FN handler_func;
  CSS_THREAD_ARG handler_func_arg;
  int rv;

  tsd_ptr = (THREAD_ENTRY *) arg_p;
  /* wait until THREAD_CREATE() finish */
  MUTEX_LOCK (rv, tsd_ptr->th_entry_lock);
  MUTEX_UNLOCK (tsd_ptr->th_entry_lock);

  thread_set_thread_entry_info (tsd_ptr);	/* save TSD */
  tsd_ptr->type = TT_WORKER;	/* not defined yet */
  tsd_ptr->status = TS_FREE;	/* set thread stat as free */

  /* during server is active */
  while (!tsd_ptr->shutdown)
    {
      er_stack_clearall ();
      er_clear ();
      job_entry_p = css_get_new_job ();	/* get new job entry */

      if (job_entry_p == NULL)
	{
	  /* if there was no job to process */
	  MUTEX_UNLOCK (tsd_ptr->tran_index_lock);
	  continue;
	}

#ifdef _TRACE_THREADS_
      CSS_TRACE4 ("processing job_entry(%p, %p, %p)\n",
		  job_entry_p->conn_entry, job_entry_p->func,
		  job_entry_p->arg);
#endif /* _TRACE_THREADS_ */

      /* set tsd_ptr information */
      tsd_ptr->conn_entry = job_entry_p->conn_entry;

      tsd_ptr->status = TS_RUN;	/* set thread status as running */

      handler_func = job_entry_p->func;
      handler_func_arg = job_entry_p->arg;
      css_free_job_entry (job_entry_p);

      handler_func (tsd_ptr, handler_func_arg);	/* invoke request handler */

      /* reset tsd_ptr information */
      tsd_ptr->conn_entry = NULL;
      tsd_ptr->status = TS_FREE;
      MUTEX_LOCK (rv, tsd_ptr->tran_index_lock);
      tsd_ptr->tran_index = -1;
      MUTEX_UNLOCK (tsd_ptr->tran_index_lock);
      tsd_ptr->check_interrupt = true;
    }

#if defined(WINDOWS)
  css_broadcast_shutdown_thread ();
#endif /* WINDOWS */
  er_final (0);

  tsd_ptr->conn_entry = NULL;
  tsd_ptr->tran_index = -1;
  tsd_ptr->status = TS_DEAD;

#if defined(WINDOWS)
  return 0;
#else /* WINDOWS */
  return NULL;
#endif /* WINDOWS */
}

/* Special Purpose Threads
   deadlock detector, check point deamon */

#if defined(WINDOWS)
/*
 * css_initialize_sync_object() -
 *   return:
 */
static int
css_initialize_sync_object (void)
{
  int r;

  r = COND_INIT (css_Deadlock_detect_thread.cond);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_COND_INIT);
  r = MUTEX_INIT (css_Deadlock_detect_thread.lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_INIT);

  r = COND_INIT (css_Checkpoint_thread.cond);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_COND_INIT);
  r = MUTEX_INIT (css_Checkpoint_thread.lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_INIT);

  r = COND_INIT (css_Oob_thread.cond);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_COND_INIT);
  r = MUTEX_INIT (css_Oob_thread.lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_INIT);

  r = COND_INIT (css_Page_flush_thread.cond);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_COND_INIT);
  r = MUTEX_INIT (css_Page_flush_thread.lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_INIT);

  r = COND_INIT (css_Log_flush_thread.cond);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_COND_INIT);
  r = MUTEX_INIT (css_Log_flush_thread.lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_INIT);

  return r;
}
#endif /* WINDOWS */

/*
 * thread_deadlock_detect_thread() -
 *   return:
 */
#if defined(WINDOWS)
static unsigned __stdcall
thread_deadlock_detect_thread (void *arg_p)
#else /* WINDOWS */
static void *
thread_deadlock_detect_thread (void *arg_p)
#endif				/* WINDOWS */
{
#if !defined(HPUX)
  THREAD_ENTRY *tsd_ptr;
#endif /* !HPUX */
  int rv;
  THREAD_ENTRY *thread_p;
  int thrd_index;
  bool state;
  int lockwait_count;

  tsd_ptr = (THREAD_ENTRY *) arg_p;
  /* wait until THREAD_CREATE() finish */
  MUTEX_LOCK (rv, tsd_ptr->th_entry_lock);
  MUTEX_UNLOCK (tsd_ptr->th_entry_lock);

  thread_set_thread_entry_info (tsd_ptr);	/* save TSD */
  tsd_ptr->type = TT_DAEMON;
  tsd_ptr->status = TS_RUN;	/* set thread stat as RUN */

  thread_set_current_tran_index (tsd_ptr, LOG_SYSTEM_TRAN_INDEX);

  /* during server is active */
  while (!tsd_ptr->shutdown)
    {
      er_clear ();

      /* check if the lock-wait thread exists */
      thread_p = thread_find_first_lockwait_entry (&thrd_index);
      if (thread_p == (THREAD_ENTRY *) NULL)
	{
	  /* none is lock-waiting */
	  MUTEX_LOCK (rv, css_Deadlock_detect_thread.lock);
	  css_Deadlock_detect_thread.is_running = false;
	  COND_WAIT (css_Deadlock_detect_thread.cond,
		     css_Deadlock_detect_thread.lock);
	  css_Deadlock_detect_thread.is_running = true;
#if !defined(WINDOWS)
	  MUTEX_UNLOCK (css_Deadlock_detect_thread.lock);
#endif /* !WINDOWS */
	  continue;
	}

      /* One or more threads are lock-waiting */
      lockwait_count = 0;
      while (thread_p != (THREAD_ENTRY *) NULL)
	{
	  /*
	   * The transaction, for which the current thread is working,
	   * might be interrupted. The interrupt checking is also performed
	   * within lock_force_timeout_expired_wait_transactions().
	   */
	  state = lock_force_timeout_expired_wait_transactions (thread_p);
	  if (state == false)
	    {
	      lockwait_count++;
	    }
	  thread_p = thread_find_next_lockwait_entry (&thrd_index);
	}

      if (lockwait_count >= 2 && lock_check_local_deadlock_detection ())
	{
	  (void) lock_detect_local_deadlock (tsd_ptr);
	}
      thread_sleep (0, 500000);
    }

  er_clear ();
  tsd_ptr->status = TS_DEAD;

#if defined(WINDOWS)
  return 0;
#else /* WINDOWS */
  return NULL;
#endif /* WINDOWS */
}

/*
 * thread_wakeup_deadlock_detect_thread() -
 *   return:
 */
void
thread_wakeup_deadlock_detect_thread (void)
{
  int rv;

  MUTEX_LOCK (rv, css_Deadlock_detect_thread.lock);
  if (css_Deadlock_detect_thread.is_running == false)
    {
      COND_SIGNAL (css_Deadlock_detect_thread.cond);
    }
  MUTEX_UNLOCK (css_Deadlock_detect_thread.lock);
}

/*
 * css_checkpoint_thread() -
 *   return:
 *   arg_p(in):
 */
#if defined(WINDOWS)
static unsigned __stdcall
thread_checkpoint_thread (void *arg_p)
#else /* WINDOWS */
static void *
thread_checkpoint_thread (void *arg_p)
#endif				/* WINDOWS */
{
#if !defined(HPUX)
  THREAD_ENTRY *tsd_ptr;
#endif /* !HPUX */
  int rv;
#if defined(WINDOWS)
  int to = 0;
#else /* WINDOWS */
  struct timespec to = { 0, 0 };
#endif /* WINDOWS */

  tsd_ptr = (THREAD_ENTRY *) arg_p;
  /* wait until THREAD_CREATE() finish */
  MUTEX_LOCK (rv, tsd_ptr->th_entry_lock);
  MUTEX_UNLOCK (tsd_ptr->th_entry_lock);

  thread_set_thread_entry_info (tsd_ptr);	/* save TSD */
  tsd_ptr->type = TT_DAEMON;
  tsd_ptr->status = TS_RUN;	/* set thread stat as RUN */

  thread_set_current_tran_index (tsd_ptr, LOG_SYSTEM_TRAN_INDEX);

  /* during server is active */
  while (!tsd_ptr->shutdown)
    {
      er_clear ();

#if defined(WINDOWS)
      to = PRM_LOG_CHECKPOINT_INTERVAL_MINUTES * 60 * 1000;
#else /* WINDOWS */
      to.tv_sec = time (NULL) + PRM_LOG_CHECKPOINT_INTERVAL_MINUTES * 60;
#endif /* WINDOWS */

      MUTEX_LOCK (rv, css_Checkpoint_thread.lock);
      COND_TIMEDWAIT (css_Checkpoint_thread.cond,
		      css_Checkpoint_thread.lock, to);
#if !defined(WINDOWS)
      MUTEX_UNLOCK (css_Checkpoint_thread.lock);
#endif /* !WINDOWS */
      if (tsd_ptr->shutdown)
	{
	  break;
	}

      logpb_checkpoint (tsd_ptr);
    }

  er_clear ();
  tsd_ptr->status = TS_DEAD;

#if defined(WINDOWS)
  return 0;
#else /* WINDOWS */
  return NULL;
#endif /* WINDOWS */
}

/*
 * thread_wakeup_checkpoint_thread() -
 *   return:
 */
void
thread_wakeup_checkpoint_thread (void)
{
  int rv;

  MUTEX_LOCK (rv, css_Checkpoint_thread.lock);
  COND_SIGNAL (css_Checkpoint_thread.cond);
  MUTEX_UNLOCK (css_Checkpoint_thread.lock);
}

/*
 * thread_wakeup_oob_handler_thread() -
 *  return:
 */
void
thread_wakeup_oob_handler_thread (void)
{
#if !defined(WINDOWS)
  THREAD_ENTRY *thread_p;

  thread_p = &css_Thread_manager.thread_array[css_Oob_thread.thread_index];
  pthread_kill (thread_p->tid, SIGURG);
#endif /* !WINDOWS */
}

/*
 * css_page_flush_thread() -
 *   return:
 *   arg_p(in):
 */
#if defined(WINDOWS)
static unsigned __stdcall
thread_page_flush_thread (void *arg_p)
#else /* WINDOWS */
static void *
thread_page_flush_thread (void *arg_p)
#endif				/* WINDOWS */
{
#if !defined(HPUX)
  THREAD_ENTRY *tsd_ptr;
#endif /* !HPUX */
  int rv;

  tsd_ptr = (THREAD_ENTRY *) arg_p;
  /* wait until THREAD_CREATE() finishes */
  MUTEX_LOCK (rv, tsd_ptr->th_entry_lock);
  MUTEX_UNLOCK (tsd_ptr->th_entry_lock);

  thread_set_thread_entry_info (tsd_ptr);	/* save TSD */
  tsd_ptr->type = TT_DAEMON;	/* daemon thread */
  tsd_ptr->status = TS_RUN;	/* set thread stat as RUN */

  css_Page_flush_thread.is_running = true;

  thread_set_current_tran_index (tsd_ptr, LOG_SYSTEM_TRAN_INDEX);

  while (!tsd_ptr->shutdown)
    {
      er_clear ();

      MUTEX_LOCK (rv, css_Page_flush_thread.lock);
      css_Page_flush_thread.is_running = false;
      COND_WAIT (css_Page_flush_thread.cond, css_Page_flush_thread.lock);
      css_Page_flush_thread.is_running = true;
#if !defined(WINDOWS)
      MUTEX_UNLOCK (css_Page_flush_thread.lock);
#endif /* !WINDOWS */

      if (tsd_ptr->shutdown)
	{
	  break;
	}

      /* do its job */
      pgbuf_flush_victim_candidate (tsd_ptr);
    }

  er_clear ();
  tsd_ptr->status = TS_DEAD;

  css_Page_flush_thread.is_running = false;

#if defined(WINDOWS)
  return 0;
#else /* WINDOWS */
  return NULL;
#endif /* WINDOWS */
}

/*
 * thread_wakeup_page_flush_thread() -
 *   return:
 */
void
thread_wakeup_page_flush_thread (void)
{
  int rv;

  if (css_Page_flush_thread.is_running)
    {
      return;
    }

  MUTEX_LOCK (rv, css_Page_flush_thread.lock);
  if (!css_Page_flush_thread.is_running)
    {
      COND_SIGNAL (css_Page_flush_thread.cond);
    }
  MUTEX_UNLOCK (css_Page_flush_thread.lock);
}

/* AsyncCommit */
/*
 * thread_get_LFT_min_wait_time() - get LFT's minimum wait time
 *  return:
 *
 * Note: LFT can wakeup in 3 cases: group commit, bg flush, log hdr flush
 *       If they are not on, LFT has to be waked up by signal only.
 */
static int
thread_get_LFT_min_wait_time ()
{
  int flush_interval;
  int gc_time = PRM_LOG_GROUP_COMMIT_INTERVAL_MSECS;
  int bg_time = PRM_LOG_BG_FLUSH_INTERVAL_MSECS;

  if (gc_time == 0)
    {
      gc_time = INT_MAX;
    }
  if (bg_time == 0)
    {
      bg_time = INT_MAX;
    }

  flush_interval = bg_time;

  if (gc_time < flush_interval)
    {
      flush_interval = gc_time;
    }

  return (flush_interval != INT_MAX ? flush_interval : 0);
}


/* AsyncCommit */
/*
 * thread_log_flush_thread() - flushed dirty log pages in background
 *   return:
 *   arg(in) : thread entry information
 *
 */
#if defined(WINDOWS)
static unsigned __stdcall
thread_log_flush_thread (void *arg_p)
#else /* WINDOWS */
static void *
thread_log_flush_thread (void *arg_p)
#endif				/* WINDOWS */
{
#if !defined(HPUX)
  THREAD_ENTRY *tsd_ptr;
#endif /* !HPUX */
  int rv;

#if defined(WINDOWS)
  int LFT_wait_time = 0;
#else /* WINDOWS */
  struct timespec LFT_wait_time = { 0, 0 };
#endif /* WINDOWS */

  struct timeval start_time = { 0, 0 };
  struct timeval end_time = { 0, 0 };
  struct timeval work_time = { 0, 0 };

  double curr_elapsed = 0;
  double gc_elapsed = 0;
  double repl_elapsed = 0;
  double work_elapsed = 0;
  int diff_wait_time;
  int min_wait_time;
  int ret;
  bool have_wake_up_thread;
  int flushed;
  int temp_wait_usec;
  bool is_background_flush = true;

  tsd_ptr = (THREAD_ENTRY *) arg_p;
  /* wait until THREAD_CREATE() finishes */
  MUTEX_LOCK (rv, tsd_ptr->th_entry_lock);
  MUTEX_UNLOCK (tsd_ptr->th_entry_lock);

  thread_set_thread_entry_info (tsd_ptr);	/* save TSD */
  tsd_ptr->type = TT_DAEMON;	/* daemon thread */
  tsd_ptr->status = TS_RUN;	/* set thread stat as RUN */

  css_Log_flush_thread.is_valid = true;

  thread_set_current_tran_index (tsd_ptr, LOG_SYSTEM_TRAN_INDEX);

  min_wait_time = thread_get_LFT_min_wait_time ();
  gettimeofday (&start_time, NULL);

  MUTEX_LOCK (rv, css_Log_flush_thread.lock);

  while (!tsd_ptr->shutdown)
    {
      er_clear ();

      gettimeofday (&work_time, NULL);
      work_elapsed = LOG_GET_ELAPSED_TIME (work_time, start_time);

      diff_wait_time = (double) min_wait_time - work_elapsed * 1000;
      if (diff_wait_time < 0)
	{
	  diff_wait_time = 0;
	}

#if defined(WINDOWS)
      LFT_wait_time = diff_wait_time;
#else /* WINDOWS */
      LFT_wait_time.tv_sec = work_time.tv_sec + (diff_wait_time / 1000);

      temp_wait_usec = work_time.tv_usec + ((diff_wait_time % 1000) * 1000);

      if (temp_wait_usec >= 1000000)
	{
	  LFT_wait_time.tv_sec += 1;
	  temp_wait_usec -= 1000000;
	}
      LFT_wait_time.tv_nsec = temp_wait_usec * 1000;
#endif /* WINDOWS */

      css_Log_flush_thread.is_log_flush_force = false;
      css_Log_flush_thread.is_running = false;

      ret = COND_TIMEDWAIT (css_Log_flush_thread.cond,
			    css_Log_flush_thread.lock, LFT_wait_time);

      css_Log_flush_thread.is_running = true;

#if defined(WINDOWS)
      MUTEX_LOCK (rv, css_Log_flush_thread.lock);
#endif /* WINDOWS */

      if (tsd_ptr->shutdown)
	{
	  break;
	}

      gettimeofday (&end_time, NULL);

      curr_elapsed = LOG_GET_ELAPSED_TIME (end_time, start_time);

      gc_elapsed += curr_elapsed;
      repl_elapsed += curr_elapsed;

      start_time = end_time;

#if (LOG_DEBUG & LOG_DEBUG_MSG)
      er_log_debug (ARG_FILE_LINE,
		    "css_log_flush_thread: "
		    "[%d]curr_elapsed(%f) gc_elapsed(%f) repl_elapsed(%f) work_elapsed(%f) diff_wait_time(%d)\n",
		    (int) THREAD_ID (),
		    curr_elapsed,
		    gc_elapsed, repl_elapsed, work_elapsed, diff_wait_time);
#endif /* LOG_DEBUG & LOG_DEBUG_MSG */

      MUTEX_LOCK (rv, log_Gl.group_commit_info.gc_mutex);

      is_background_flush = false;
      have_wake_up_thread = false;
      if (ret == TIMEDWAIT_TIMEOUT)
	{
	  if (css_Log_flush_thread.is_log_flush_force)
	    {
	      is_background_flush = false;
	    }
	  else if (!LOG_IS_GROUP_COMMIT_ACTIVE ())
	    {
	      is_background_flush = true;
	    }
	  else if (PRM_LOG_ASYNC_COMMIT)
	    {
	      if (gc_elapsed * 1000 >= PRM_LOG_GROUP_COMMIT_INTERVAL_MSECS)
		{
		  is_background_flush = false;
		}
	      else
		{
		  is_background_flush = true;
		}
	    }
	  else
	    {
	      if (gc_elapsed * 1000 >= PRM_LOG_GROUP_COMMIT_INTERVAL_MSECS
		  && log_Gl.group_commit_info.waiters > 0)
		{
		  is_background_flush = false;
		}
	      else
		{
		  is_background_flush = true;
		}
	    }

	  if (PRM_REPLICATION_MODE
	      && (repl_elapsed >= (double) PRM_LOG_HEADER_FLUSH_INTERVAL))
	    {
	      LOG_CS_ENTER (tsd_ptr);
	      logpb_flush_header (tsd_ptr);
	      LOG_CS_EXIT ();

	      repl_elapsed = 0;
	    }
	}

      if (is_background_flush)
	{
	  logpb_flush_pages_background (tsd_ptr);
	}
      else
	{
	  LOG_CS_ENTER (tsd_ptr);
	  log_Gl.flush_info.flush_type = LOG_FLUSH_DIRECT;
	  flushed = logpb_flush_all_append_pages_low (tsd_ptr);
	  log_Gl.flush_info.flush_type = LOG_FLUSH_NORMAL;
	  LOG_CS_EXIT ();

	  if (flushed > 0)
	    {
	      ++log_Stat.gc_flush_count;
	      gc_elapsed = 0;
	    }
	  have_wake_up_thread = true;
	}

      if (have_wake_up_thread)
	{
#if defined(WINDOWS)
	  int loop;

	  for (loop = 0; loop != log_Gl.group_commit_info.waiters; ++loop)
	    {
	      COND_BROADCAST (log_Gl.group_commit_info.gc_cond);
	    }
#else /* WINDOWS */
	  COND_BROADCAST (log_Gl.group_commit_info.gc_cond);
#endif /* WINDOWS */

#if (LOG_DEBUG & LOG_DEBUG_MSG)
	  er_log_debug (ARG_FILE_LINE,
			"css_log_flush_thread: "
			"[%d]send signal - waiters(%d) \n",
			(int) THREAD_ID (), log_Gl.group_commit_info.waiters);
#endif /*LOG_DEBUG & LOG_DEBUG_MSG */
	  log_Gl.group_commit_info.waiters = 0;
	}
      MUTEX_UNLOCK (log_Gl.group_commit_info.gc_mutex);
    }

  css_Log_flush_thread.is_valid = false;
  css_Log_flush_thread.is_running = false;
  MUTEX_UNLOCK (css_Log_flush_thread.lock);

  er_clear ();
  tsd_ptr->status = TS_DEAD;

#if (LOG_DEBUG & LOG_DEBUG_MSG)
  er_log_debug (ARG_FILE_LINE,
		"css_log_flush_thread: " "[%d]end \n", (int) THREAD_ID ());
#endif /*LOG_DEBUG & LOG_DEBUG_MSG */

#if defined(WINDOWS)
  return 0;
#else /* WINDOWS */
  return NULL;
#endif /* WINDOWS */
}


/*
 * thread_wakeup_log_flush_thread() -
 *   return:
 */
void
thread_wakeup_log_flush_thread (void)
{
  int rv;

  if (css_Log_flush_thread.is_running)
    {
      return;
    }

  MUTEX_LOCK (rv, css_Log_flush_thread.lock);
  if (!css_Log_flush_thread.is_running)
    {
      COND_SIGNAL (css_Log_flush_thread.cond);
    }
  MUTEX_UNLOCK (css_Log_flush_thread.lock);
}

/*
 * thread_slam_tran_index() -
 *   return:
 *   tran_index(in):
 */
static int
thread_slam_tran_index (THREAD_ENTRY * thread_p, int tran_index)
{
  logtb_set_tran_index_interrupt (thread_p, tran_index, true);
  css_shutdown_conn_by_tran_index (tran_index);

  return CSS_SLAM_STATUS_INTERRUPTED;
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
xthread_kill_tran_index (THREAD_ENTRY * thread_p, int kill_tran_index,
			 char *kill_user_p, char *kill_host_p, int kill_pid)
{
  char *slam_progname_p;	/* Client program name for tran */
  char *slam_user_p;		/* Client user name for tran    */
  char *slam_host_p;		/* Client host for tran         */
  int slam_pid;			/* Client process id for tran   */
  bool signaled = false;
  int error_code = NO_ERROR;
  bool killed = false;
  int i;

  if (kill_tran_index == NULL_TRAN_INDEX
      || kill_user_p == NULL
      || kill_host_p == NULL
      || strcmp (kill_user_p, "") == 0 || strcmp (kill_host_p, "") == 0)
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

  signaled = false;
  for (i = 0;
       i < CSS_RETRY_MAX_SLAM_TIMES && error_code == NO_ERROR && !killed; i++)
    {
      if (logtb_find_client_name_host_pid (kill_tran_index, &slam_progname_p,
					   &slam_user_p, &slam_host_p,
					   &slam_pid) != NO_ERROR)
	{
	  if (signaled == false)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_CSS_KILL_UNKNOWN_TRANSACTION, 4,
		      kill_tran_index, kill_user_p, kill_host_p, kill_pid);
	      error_code = ER_CSS_KILL_UNKNOWN_TRANSACTION;
	    }
	  else
	    {
	      killed = true;
	    }
	  break;
	}

      if (kill_pid == slam_pid
	  && strcmp (kill_user_p, slam_user_p) == 0
	  && strcmp (kill_host_p, slam_host_p) == 0)
	{
	  thread_slam_tran_index (thread_p, kill_tran_index);
	  signaled = true;
	}
      else
	{
	  if (signaled == false)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_CSS_KILL_DOES_NOTMATCH, 8,
		      kill_tran_index, kill_user_p, kill_host_p, kill_pid,
		      kill_tran_index, slam_user_p, slam_host_p, slam_pid);
	      error_code = ER_CSS_KILL_DOES_NOTMATCH;
	    }
	  else
	    {
	      killed = true;
	    }
	  break;
	}
      thread_sleep (1, 0);
    }

  if (error_code == NO_ERROR && !killed)
    {
      error_code = ER_FAILED;	/* timeout */
    }

  return error_code;
}

/*
 * thread_find_first_lockwait_entry() -
 *   return:
 *   thread_index_p(in):
 */
THREAD_ENTRY *
thread_find_first_lockwait_entry (int *thread_index_p)
{
  THREAD_ENTRY *thread_p;
  int i;

  for (i = 1; i <= NUM_WORKER_THREADS; i++)
    {
      thread_p = &(css_Thread_manager.thread_array[i]);
      if (thread_p->status == TS_DEAD || thread_p->status == TS_FREE)
	{
	  continue;
	}
      if (thread_p->lockwait != NULL)
	{			/* found */
	  *thread_index_p = i;
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
  THREAD_ENTRY *thread_p;
  int i;

  for (i = (*thread_index_p + 1); i <= NUM_WORKER_THREADS; i++)
    {
      thread_p = &(css_Thread_manager.thread_array[i]);
      if (thread_p->status == TS_DEAD || thread_p->status == TS_FREE)
	{
	  continue;
	}
      if (thread_p->lockwait != NULL)
	{			/* found */
	  *thread_index_p = i;
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
  return (&css_Thread_manager.thread_array[thread_index]);
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
  int i, thread_count;

  thread_count = 0;
  for (i = 1; i <= NUM_WORKER_THREADS; i++)
    {
      thread_p = &(css_Thread_manager.thread_array[i]);
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
	      break;
	    }
	}
    }

  return thread_count;
}
