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
#include <assert.h>

#include "porting.h"
#include "connection_error.h"
#include "job_queue.h"
#include "thread.h"
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
#include "log_compress.h"
#include "perf_monitor.h"
#include "session.h"
#if defined(WINDOWS)
#include "wintcp.h"
#else /* WINDOWS */
#include "tcp.h"
#endif /* WINDOWS */

#if defined(WINDOWS)
#include "heartbeat.h"
#endif


#if defined(HPUX)
#define thread_initialize_key()
#endif /* HPUX */


/* Thread Manager structure */
typedef struct thread_manager THREAD_MANAGER;
struct thread_manager
{
  THREAD_ENTRY *thread_array;	/* thread entry array */
  int num_total;
  int num_workers;
  int num_daemons;
  bool initialized;
};

/* deadlock + checkpoint + oob + page flush + log flush + flush control
 * + session control + purge archive logs + log clock */
#if defined(HAVE_ATOMIC_BUILTINS)
static const int PREDEFINED_DAEMON_THREAD_NUM = 9;
#define USE_LOG_CLOCK_THREAD
#else /* HAVE_ATOMIC_BUILTINS */
static const int PREDEFINED_DAEMON_THREAD_NUM = 8;
#endif /* HAVE_ATOMIC_BUILTINS */

static const int THREAD_RETRY_MAX_SLAM_TIMES = 10;

#if defined(HPUX)
static __thread THREAD_ENTRY *tsd_ptr;
#else /* HPUX */
static pthread_key_t css_Thread_key;
#endif /* HPUX */

static THREAD_MANAGER thread_Manager;

/*
 * For special Purpose Threads: deadlock detector, checkpoint daemon
 *    Under the win32-threads system, *_cond variables are an auto-reset event
 */
static DAEMON_THREAD_MONITOR thread_Deadlock_detect_thread =
  { 0, true, false, false, PTHREAD_MUTEX_INITIALIZER,
  PTHREAD_COND_INITIALIZER
};
static DAEMON_THREAD_MONITOR thread_Checkpoint_thread =
  { 0, false, false, false, PTHREAD_MUTEX_INITIALIZER,
  PTHREAD_COND_INITIALIZER
};
static DAEMON_THREAD_MONITOR thread_Purge_archive_logs_thread =
  { 0, false, false, false, PTHREAD_MUTEX_INITIALIZER,
  PTHREAD_COND_INITIALIZER
};
static DAEMON_THREAD_MONITOR thread_Oob_thread =
  { 0, true, true, false, PTHREAD_MUTEX_INITIALIZER,
  PTHREAD_COND_INITIALIZER
};
static DAEMON_THREAD_MONITOR thread_Page_flush_thread =
  { 0, false, false, false, PTHREAD_MUTEX_INITIALIZER,
  PTHREAD_COND_INITIALIZER
};
static DAEMON_THREAD_MONITOR thread_Flush_control_thread =
  { 0, false, false, false, PTHREAD_MUTEX_INITIALIZER,
  PTHREAD_COND_INITIALIZER
};
static DAEMON_THREAD_MONITOR thread_Session_control_thread =
  { 0, false, false, false, PTHREAD_MUTEX_INITIALIZER,
  PTHREAD_COND_INITIALIZER
};
DAEMON_THREAD_MONITOR thread_Log_flush_thread =
  { 0, false, false, false, PTHREAD_MUTEX_INITIALIZER,
  PTHREAD_COND_INITIALIZER
};

#if defined(USE_LOG_CLOCK_THREAD)
static DAEMON_THREAD_MONITOR thread_Log_clock_thread =
  { 0, false, false, false, PTHREAD_MUTEX_INITIALIZER,
  PTHREAD_COND_INITIALIZER
};
#endif /* USE_LOG_CLOCK_THREAD */

static int thread_initialize_entry (THREAD_ENTRY * entry_ptr);
static int thread_finalize_entry (THREAD_ENTRY * entry_ptr);

static THREAD_ENTRY *thread_find_entry_by_tran_index (int tran_index);

static THREAD_RET_T THREAD_CALLING_CONVENTION
thread_deadlock_detect_thread (void *);
static THREAD_RET_T THREAD_CALLING_CONVENTION
thread_checkpoint_thread (void *);
static THREAD_RET_T THREAD_CALLING_CONVENTION
thread_purge_archive_logs_thread (void *);
static THREAD_RET_T THREAD_CALLING_CONVENTION
thread_page_flush_thread (void *);
static THREAD_RET_T THREAD_CALLING_CONVENTION
thread_flush_control_thread (void *);
static THREAD_RET_T THREAD_CALLING_CONVENTION
thread_log_flush_thread (void *);
static THREAD_RET_T THREAD_CALLING_CONVENTION
thread_session_control_thread (void *);
#if defined(USE_LOG_CLOCK_THREAD)
static THREAD_RET_T THREAD_CALLING_CONVENTION
thread_log_clock_thread (void *);
#endif /* USE_LOG_CLOCK_THREAD */

static int css_initialize_sync_object (void);
static int thread_wakeup_internal (THREAD_ENTRY * thread_p, int resume_reason,
				   bool had_mutex);
static int thread_get_LFT_min_wait_time_in_msec (void);

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

  r = pthread_key_create (&css_Thread_key, NULL);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_KEY_CREATE, 0);
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

  r = pthread_setspecific (css_Thread_key, (void *) entry_p);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_SETSPECIFIC, 0);
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

  p = pthread_getspecific (css_Thread_key);
#if defined (SERVER_MODE)
  assert (p != NULL);
#endif
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
  return thread_Manager.initialized;
}

/*
 * thread_initialize_manager() - Create and initialize all necessary threads.
 *   return: 0 if no error, or error code
 *
 * Note: It includes a main thread, service handler, a deadlock detector
 *       and a checkpoint daemon. Some other threads like signal handler
 *       might be needed later.
 */
int
thread_initialize_manager (void)
{
  int i, r;
  size_t size;
#if !defined(HPUX)
  THREAD_ENTRY *tsd_ptr;
#endif /* not HPUX */

  assert (PRM_CSS_MAX_CLIENTS >= 10);

  if (thread_Manager.initialized == false)
    {
      r = thread_initialize_key ();
      if (r != NO_ERROR)
	{
	  return r;
	}

#if defined(WINDOWS)
      r = pthread_mutex_init (&css_Internal_mutex_for_mutex_initialize, NULL);
      if (r != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_CSS_PTHREAD_MUTEX_INIT, 0);
	  return ER_CSS_PTHREAD_MUTEX_INIT;
	}
      css_initialize_sync_object ();
#endif /* WINDOWS */

    }
  else
    {
      /* destroy mutex and cond */
      for (i = 1; i < thread_Manager.num_total; i++)
	{
	  r = thread_finalize_entry (&thread_Manager.thread_array[i]);
	  if (r != NO_ERROR)
	    {
	      return r;
	    }
	}
      r = thread_finalize_entry (&thread_Manager.thread_array[0]);
      if (r != NO_ERROR)
	{
	  return r;
	}
      free_and_init (thread_Manager.thread_array);
    }

  /* calculate the number of thread from the number of clients */
  thread_Manager.num_workers = PRM_CSS_MAX_CLIENTS * 2;
  thread_Manager.num_daemons = PREDEFINED_DAEMON_THREAD_NUM;
  thread_Manager.num_total =
    thread_Manager.num_workers + thread_Manager.num_daemons
    + 1 /* master thread */ ;

  size = thread_Manager.num_total * sizeof (THREAD_ENTRY);
  tsd_ptr = thread_Manager.thread_array = (THREAD_ENTRY *) malloc (size);
  if (tsd_ptr == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* initialize master thread */
  r = thread_initialize_entry (tsd_ptr);
  if (r != NO_ERROR)
    {
      return r;
    }

  tsd_ptr->index = 0;
  tsd_ptr->tid = pthread_self ();
  tsd_ptr->status = TS_RUN;
  tsd_ptr->resume_status = THREAD_RESUME_NONE;
  tsd_ptr->tran_index = 0;	/* system transaction */
  thread_set_thread_entry_info (tsd_ptr);

  /* init worker/deadlock-detection/checkpoint daemon/audit-flush
     oob-handler thread/page flush thread/log flush thread
     thread_mgr.thread_array[0] is used for main thread */
  for (i = 1; i < thread_Manager.num_total; i++)
    {
      r = thread_initialize_entry (&thread_Manager.thread_array[i]);
      if (r != NO_ERROR)
	{
	  return r;
	}
      thread_Manager.thread_array[i].index = i;
    }

  thread_Manager.initialized = true;

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
  int thread_index, r;
  THREAD_ENTRY *thread_p;
  pthread_attr_t thread_attr;
#if defined(_POSIX_THREAD_ATTR_STACKSIZE)
  size_t ts_size;
#endif /* _POSIX_THREAD_ATTR_STACKSIZE */

  assert (thread_Manager.initialized == true);

#if !defined(WINDOWS)
  r = pthread_attr_init (&thread_attr);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_ATTR_INIT, 0);
      return ER_CSS_PTHREAD_ATTR_INIT;
    }

  r = pthread_attr_setdetachstate (&thread_attr, PTHREAD_CREATE_DETACHED);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_ATTR_SETDETACHSTATE, 0);
      return ER_CSS_PTHREAD_ATTR_SETDETACHSTATE;
    }

#if defined(AIX)
  /* AIX's pthread is slightly different from other systems.
     Its performance highly depends on the pthread's scope and its related
     kernel parameters. */
  r = pthread_attr_setscope (&thread_attr,
			     PRM_PTHREAD_SCOPE_PROCESS ?
			     PTHREAD_SCOPE_PROCESS : PTHREAD_SCOPE_SYSTEM);
#else /* AIX */
  r = pthread_attr_setscope (&thread_attr, PTHREAD_SCOPE_SYSTEM);
#endif /* AIX */
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_ATTR_SETSCOPE, 0);
      return ER_CSS_PTHREAD_ATTR_SETSCOPE;
    }

#if defined(_POSIX_THREAD_ATTR_STACKSIZE)
  r = pthread_attr_getstacksize (&thread_attr, &ts_size);
  if (ts_size != (size_t) PRM_THREAD_STACKSIZE)
    {
      r = pthread_attr_setstacksize (&thread_attr, PRM_THREAD_STACKSIZE);
      if (r != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_CSS_PTHREAD_ATTR_SETSTACKSIZE, 0);
	  return ER_CSS_PTHREAD_ATTR_SETSTACKSIZE;
	}
    }
#endif /* _POSIX_THREAD_ATTR_STACKSIZE */
#endif /* WINDOWS */

  /* start worker thread */
  for (thread_index = 1; thread_index <= thread_Manager.num_workers;
       thread_index++)
    {
      thread_p = &thread_Manager.thread_array[thread_index];
      r = pthread_mutex_lock (&thread_p->th_entry_lock);
      if (r != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_CSS_PTHREAD_MUTEX_LOCK, 0);
	  return ER_CSS_PTHREAD_MUTEX_LOCK;
	}
      /* If win32, then "thread_attr" is ignored, else "p->thread_handle". */
      r =
	pthread_create (&thread_p->tid, &thread_attr, thread_worker,
			thread_p);
      if (r != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_CSS_PTHREAD_CREATE, 0);
	  pthread_mutex_unlock (&thread_p->th_entry_lock);
	  return ER_CSS_PTHREAD_CREATE;
	}

      r = pthread_mutex_unlock (&thread_p->th_entry_lock);
      if (r != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
	  return ER_CSS_PTHREAD_MUTEX_UNLOCK;
	}
    }

  /* start deadlock detection thread */
  thread_Deadlock_detect_thread.thread_index = thread_index++;
  thread_p =
    &thread_Manager.thread_array[thread_Deadlock_detect_thread.thread_index];
  r = pthread_mutex_lock (&thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_LOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_LOCK;
    }

  r = pthread_create (&thread_p->tid, &thread_attr,
		      thread_deadlock_detect_thread, thread_p);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_CREATE, 0);
      pthread_mutex_unlock (&thread_p->th_entry_lock);
      return ER_CSS_PTHREAD_CREATE;
    }

  r = pthread_mutex_unlock (&thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
    }

  /* start checkpoint daemon thread */
  thread_Checkpoint_thread.thread_index = thread_index++;
  thread_p =
    &thread_Manager.thread_array[thread_Checkpoint_thread.thread_index];
  r = pthread_mutex_lock (&thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_LOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_LOCK;
    }

  r =
    pthread_create (&thread_p->tid, &thread_attr, thread_checkpoint_thread,
		    thread_p);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_CREATE, 0);
      pthread_mutex_unlock (&thread_p->th_entry_lock);
      return ER_CSS_PTHREAD_CREATE;
    }

  r = pthread_mutex_unlock (&thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
    }

  /* start purge archive logs daemon thread */
  thread_Purge_archive_logs_thread.thread_index = thread_index++;
  thread_p =
    &thread_Manager.thread_array[thread_Purge_archive_logs_thread.
				 thread_index];
  r = pthread_mutex_lock (&thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_LOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_LOCK;
    }

  r =
    pthread_create (&thread_p->tid, &thread_attr,
		    thread_purge_archive_logs_thread, thread_p);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_CREATE, 0);
      pthread_mutex_unlock (&thread_p->th_entry_lock);
      return ER_CSS_PTHREAD_CREATE;
    }

  r = pthread_mutex_unlock (&thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
    }

  /* start oob-handler thread */
  thread_Oob_thread.thread_index = thread_index++;
  thread_p = &thread_Manager.thread_array[thread_Oob_thread.thread_index];
  r = pthread_mutex_lock (&thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_LOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_LOCK;
    }

  r = pthread_create (&thread_p->tid, &thread_attr,
		      css_oob_handler_thread, thread_p);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_CREATE, 0);
      pthread_mutex_unlock (&thread_p->th_entry_lock);
      return ER_CSS_PTHREAD_CREATE;
    }

  r = pthread_mutex_unlock (&thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
    }

  /* start page flush daemon thread */
  thread_Page_flush_thread.thread_index = thread_index++;
  thread_p =
    &thread_Manager.thread_array[thread_Page_flush_thread.thread_index];
  r = pthread_mutex_lock (&thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_LOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_LOCK;
    }

  r = pthread_create (&thread_p->tid, &thread_attr,
		      thread_page_flush_thread, thread_p);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_CREATE, 0);
      pthread_mutex_unlock (&thread_p->th_entry_lock);
      return ER_CSS_PTHREAD_CREATE;
    }

  r = pthread_mutex_unlock (&thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
    }

  /* start flush control daemon thread */
  thread_Flush_control_thread.thread_index = thread_index++;
  thread_p =
    &thread_Manager.thread_array[thread_Flush_control_thread.thread_index];
  r = pthread_mutex_lock (&thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_LOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_LOCK;
    }

  r = pthread_create (&thread_p->tid, &thread_attr,
		      thread_flush_control_thread, thread_p);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_CREATE, 0);
      pthread_mutex_unlock (&thread_p->th_entry_lock);
      return ER_CSS_PTHREAD_CREATE;
    }

  r = pthread_mutex_unlock (&thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
    }

  /* start log flush thread */
  thread_Log_flush_thread.thread_index = thread_index++;
  thread_p =
    &thread_Manager.thread_array[thread_Log_flush_thread.thread_index];
  r = pthread_mutex_lock (&thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_LOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_LOCK;
    }

  r = pthread_create (&thread_p->tid, &thread_attr,
		      thread_log_flush_thread, thread_p);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_CREATE, 0);
      pthread_mutex_unlock (&thread_p->th_entry_lock);
      return ER_CSS_PTHREAD_CREATE;
    }

  r = pthread_mutex_unlock (&thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
    }

  /* start session control thread */
  thread_Session_control_thread.thread_index = thread_index++;
  thread_p =
    &thread_Manager.thread_array[thread_Session_control_thread.thread_index];
  r = pthread_mutex_lock (&thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_LOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_LOCK;
    }

  r = pthread_create (&thread_p->tid, &thread_attr,
		      thread_session_control_thread, thread_p);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_CREATE, 0);
      pthread_mutex_unlock (&thread_p->th_entry_lock);
      return ER_CSS_PTHREAD_CREATE;
    }

  pthread_mutex_unlock (&thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
    }

#if defined(USE_LOG_CLOCK_THREAD)
  /* start clock thread */
  thread_Log_clock_thread.thread_index = thread_index++;
  thread_p =
    &thread_Manager.thread_array[thread_Log_clock_thread.thread_index];
  r = pthread_mutex_lock (&thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_LOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_LOCK;
    }

  r = pthread_create (&thread_p->tid, &thread_attr,
		      thread_log_clock_thread, thread_p);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_CREATE, 0);
      pthread_mutex_unlock (&thread_p->th_entry_lock);
      return ER_CSS_PTHREAD_CREATE;
    }

  pthread_mutex_unlock (&thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
    }
#endif /* USE_LOG_CLOCK_THREAD */

  /* destroy thread_attribute */
  r = pthread_attr_destroy (&thread_attr);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_ATTR_DESTROY, 0);
      return ER_CSS_PTHREAD_ATTR_DESTROY;
    }

  return NO_ERROR;
}

/*
 * thread_stop_active_workers() - Stop active work thread.
 *   return: 0 if no error, or error code
 *
 * Node: This function is invoked when system is going shut down.
 */
int
thread_stop_active_workers (unsigned short stop_phase)
{
  int i, count;
  int r;
  bool repeat_loop;
  THREAD_ENTRY *thread_p;
  CSS_CONN_ENTRY *conn_p;

  assert (thread_Manager.initialized == true);

  if (stop_phase == THREAD_STOP_WORKERS_EXCEPT_LOGWR)
    {
      css_block_all_active_conn (stop_phase);
    }

  count = 0;
loop:
  for (i = 1; i <= thread_Manager.num_workers; i++)
    {
      thread_p = &thread_Manager.thread_array[i];

      conn_p = thread_p->conn_entry;
      if ((stop_phase == THREAD_STOP_LOGWR && conn_p == NULL)
	  || (conn_p && conn_p->stop_phase != stop_phase))
	{
	  continue;
	}

      if (thread_p->tran_index != -1)
	{
	  if (stop_phase == THREAD_STOP_WORKERS_EXCEPT_LOGWR)
	    {
	      logtb_set_tran_index_interrupt (NULL, thread_p->tran_index, 1);
	    }

	  if (thread_p->status == TS_WAIT)
	    {
	      if (stop_phase == THREAD_STOP_WORKERS_EXCEPT_LOGWR)
		{
		  thread_p->interrupted = true;
		  thread_wakeup (thread_p, THREAD_RESUME_DUE_TO_INTERRUPT);
		}
	      else if (stop_phase == THREAD_STOP_LOGWR)
		{
		  /* 
		   * we can only wakeup LWT when waiting on THREAD_LOGWR_SUSPENDED.
		   */
		  r =
		    thread_check_suspend_reason_and_wakeup (thread_p,
							    THREAD_RESUME_DUE_TO_INTERRUPT,
							    THREAD_LOGWR_SUSPENDED);
		  if (r == NO_ERROR)
		    {
		      thread_p->interrupted = true;
		    }
		}
	    }
	  thread_sleep (0, 10000);	/* 10 msec */
	}
    }

  thread_sleep (0, 10000);
  lock_force_timeout_lock_wait_transactions (stop_phase);

  /* Signal for blocked on job queue */
  /* css_broadcast_shutdown_thread(); */

  repeat_loop = false;
  for (i = 1; i <= thread_Manager.num_workers; i++)
    {
      thread_p = &thread_Manager.thread_array[i];

      conn_p = thread_p->conn_entry;
      if ((stop_phase == THREAD_STOP_LOGWR && conn_p == NULL)
	  || (conn_p && conn_p->stop_phase != stop_phase))
	{
	  continue;
	}

      if (thread_p->status != TS_FREE)
	{
	  repeat_loop = true;
	}
    }

  if (repeat_loop)
    {
      if (count++ > PRM_SHUTDOWN_WAIT_TIME_IN_SECS)
	{
#if CUBRID_DEBUG
	  logtb_dump_trantable (NULL, stderr);
#endif
	  er_log_debug (ARG_FILE_LINE,
			"thread_stop_active_workers: _exit(0)\n");
	  /* exit process after some tries */
	  _exit (0);
	}
      thread_sleep (1, 0);
      goto loop;
    }

  /* 
   * we must not block active connection before terminating log writer thread.
   */
  if (stop_phase == THREAD_STOP_LOGWR)
    {
      css_block_all_active_conn (stop_phase);
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

  assert (thread_Manager.initialized == true);

  count = 0;
  for (i = 0; i < PREDEFINED_DAEMON_THREAD_NUM; i++)
    {
      idx = thread_Manager.num_workers + i + 1;	/* 1 for master thread */
      thread_p = &thread_Manager.thread_array[idx];
      thread_p->shutdown = true;
    }

  thread_wakeup_deadlock_detect_thread ();
  thread_wakeup_checkpoint_thread ();
  thread_wakeup_purge_archive_logs_thread ();
  thread_wakeup_oob_handler_thread ();
  thread_wakeup_page_flush_thread ();
  thread_wakeup_flush_control_thread ();
  thread_wakeup_log_flush_thread ();
  thread_wakeup_session_control_thread ();

loop:
  repeat_loop = false;
  for (i = 0; i < PREDEFINED_DAEMON_THREAD_NUM; i++)
    {
      idx = thread_Manager.num_workers + i + 1;	/* 1 for master thread */
      thread_p = &thread_Manager.thread_array[idx];
      if (thread_p->status != TS_DEAD)
	{
	  repeat_loop = true;
	}
    }

  if (repeat_loop)
    {
      if (count++ > 30)
	{
#if CUBRID_DEBUG
	  xlogtb_dump_trantable (NULL, stderr);
#endif
	  er_log_debug (ARG_FILE_LINE,
			"thread_stop_active_daemons: _exit(0)\n");
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

  for (i = 1; i <= thread_Manager.num_workers; i++)
    {
      thread_p = &thread_Manager.thread_array[i];
      thread_p->interrupted = true;
      thread_p->shutdown = true;
    }

  count = 0;

loop:

  /* Signal for blocked on job queue */
  css_broadcast_shutdown_thread ();

  repeat_loop = false;
  for (i = 1; i <= thread_Manager.num_workers; i++)
    {
      thread_p = &thread_Manager.thread_array[i];
      if (thread_p->status != TS_DEAD)
	{
	  repeat_loop = true;
	}
    }

  if (repeat_loop)
    {
      if (count++ > PRM_SHUTDOWN_WAIT_TIME_IN_SECS)
	{
#if CUBRID_DEBUG
	  xlogtb_dump_trantable (NULL, stderr);
#endif
	  er_log_debug (ARG_FILE_LINE, "thread_kill_all_workers: _exit(0)\n");
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

  for (i = 1; i < thread_Manager.num_total; i++)
    {
      (void) thread_finalize_entry (&thread_Manager.thread_array[i]);
    }
  (void) thread_finalize_entry (&thread_Manager.thread_array[0]);
  free_and_init (thread_Manager.thread_array);

#ifndef HPUX
  pthread_key_delete (css_Thread_key);
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
  entry_p->tid = ((pthread_t) 0);
  entry_p->client_id = -1;
  entry_p->tran_index = -1;
  r = pthread_mutex_init (&entry_p->tran_index_lock, NULL);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_INIT, 0);
      return ER_CSS_PTHREAD_MUTEX_INIT;
    }

  entry_p->rid = 0;
  entry_p->status = TS_DEAD;
  entry_p->interrupted = false;
  entry_p->shutdown = false;
  entry_p->cnv_adj_buffer[0] = NULL;
  entry_p->cnv_adj_buffer[1] = NULL;
  entry_p->cnv_adj_buffer[2] = NULL;
  entry_p->conn_entry = NULL;
  entry_p->worker_thrd_list = NULL;

  r = pthread_mutex_init (&entry_p->th_entry_lock, NULL);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_INIT, 0);
      return ER_CSS_PTHREAD_MUTEX_INIT;
    }

  r = pthread_cond_init (&entry_p->wakeup_cond, NULL);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_COND_INIT, 0);
      return ER_CSS_PTHREAD_COND_INIT;
    }

  entry_p->resume_status = THREAD_RESUME_NONE;
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

  entry_p->log_zip_undo = NULL;
  entry_p->log_zip_redo = NULL;
  entry_p->log_data_length = 0;
  entry_p->log_data_ptr = NULL;

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
  int r, i, error = NO_ERROR;

  entry_p->index = -1;
  entry_p->tid = ((pthread_t) 0);;
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

  r = pthread_mutex_destroy (&entry_p->tran_index_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_DESTROY, 0);
      error = ER_CSS_PTHREAD_MUTEX_DESTROY;
    }
  r = pthread_mutex_destroy (&entry_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_DESTROY, 0);
      error = ER_CSS_PTHREAD_MUTEX_DESTROY;
    }
  r = pthread_cond_destroy (&entry_p->wakeup_cond);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_COND_DESTROY, 0);
      error = ER_CSS_PTHREAD_COND_DESTROY;
    }
  entry_p->resume_status = THREAD_RESUME_NONE;

  entry_p->check_interrupt = true;

  if (entry_p->log_zip_undo)
    {
      log_zip_free (entry_p->log_zip_undo);
      entry_p->log_zip_undo = NULL;
    }
  if (entry_p->log_zip_redo)
    {
      log_zip_free (entry_p->log_zip_redo);
      entry_p->log_zip_redo = NULL;
    }
  if (entry_p->log_data_ptr)
    {
      free_and_init (entry_p->log_data_ptr);
      entry_p->log_data_length = 0;
    }

  db_destroy_private_heap (entry_p, entry_p->private_heap_id);

  return error;
}

#if defined(CUBRID_DEBUG)
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
#endif

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
  THREAD_ENTRY *thread_p;
  int i;
  pthread_t me = pthread_self ();

  for (i = 1; i <= thread_Manager.num_workers; i++)
    {
      thread_p = &thread_Manager.thread_array[i];
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

  for (i = 1; i <= thread_Manager.num_workers; i++)
    {
      thread_p = &thread_Manager.thread_array[i];
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

#if defined (ENABLE_UNUSED_FUNCTION)
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
#endif /* ENABLE_UNUSED_FUNCTION */

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

  assert (thread_p != NULL);

  r = pthread_mutex_lock (&thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_LOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_LOCK;
    }

  return r;
}

#if defined (ENABLE_UNUSED_FUNCTION)
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
  assert (thread_p != NULL);

  r = pthread_mutex_lock (&thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_LOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_LOCK;
    }

  return r;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * thread_unlock_entry() -
 *   return:
 *   thread_p(in):
 */
int
thread_unlock_entry (THREAD_ENTRY * thread_p)
{
  int r;

  assert (thread_p != NULL);

  r = pthread_mutex_unlock (&thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
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
thread_suspend_wakeup_and_unlock_entry (THREAD_ENTRY * thread_p,
					int suspended_reason)
{
  int r;
  int old_status;

  assert (thread_p->status == TS_RUN || thread_p->status == TS_CHECK);
  old_status = thread_p->status;
  thread_p->status = TS_WAIT;

  thread_p->resume_status = suspended_reason;

  r = pthread_cond_wait (&thread_p->wakeup_cond, &thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_COND_WAIT, 0);
      return ER_CSS_PTHREAD_COND_WAIT;
    }


  thread_p->status = old_status;

  r = pthread_mutex_unlock (&thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
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
thread_suspend_timeout_wakeup_and_unlock_entry (THREAD_ENTRY * thread_p,
						struct timespec *time_p,
						int suspended_reason)
{
  int r;
  int old_status;

  assert (thread_p->status == TS_RUN || thread_p->status == TS_CHECK);
  old_status = thread_p->status;
  thread_p->status = TS_WAIT;

  thread_p->resume_status = suspended_reason;

  r =
    pthread_cond_timedwait (&thread_p->wakeup_cond, &thread_p->th_entry_lock,
			    time_p);
  if (r == ETIMEDOUT)
    {
      r = 0;
    }
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_COND_TIMEDWAIT, 0);
      return ER_CSS_PTHREAD_COND_TIMEDWAIT;
    }

  thread_p->status = old_status;

  r = pthread_mutex_unlock (&thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
    }

  return r;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * thread_suspend_wakeup_and_unlock_entry_with_tran_index() -
 *   return: 0 if no error, or error code
 *   tran_index(in):
 */
int
thread_suspend_wakeup_and_unlock_entry_with_tran_index (int tran_index,
							int suspended_reason)
{
  THREAD_ENTRY *thread_p;
  int r;
  int old_status;

  thread_p = thread_find_entry_by_tran_index (tran_index);
  if (thread_p == NULL)
    {
      return ER_FAILED;
    }

  /*
   * this function must be called by current thread
   * also, the lock must have already been acquired before.
   */
  assert (thread_p->status == TS_RUN || thread_p->status == TS_CHECK);
  old_status = thread_p->status;
  thread_p->status = TS_WAIT;

  thread_p->resume_status = suspended_reason;

  r = pthread_cond_wait (&thread_p->wakeup_cond, &thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_COND_WAIT, 0);
      return ER_CSS_PTHREAD_COND_WAIT;
    }

  thread_p->status = old_status;

  r = pthread_mutex_unlock (&thread_p->th_entry_lock);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
    }

  return NO_ERROR;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * thread_wakeup_internal () -
 *   return:
 *   thread_p(in/out):
 *   resume_reason:
 */
static int
thread_wakeup_internal (THREAD_ENTRY * thread_p, int resume_reason,
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

  r = pthread_cond_signal (&thread_p->wakeup_cond);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_COND_SIGNAL, 0);
      thread_unlock_entry (thread_p);
      return ER_CSS_PTHREAD_COND_SIGNAL;
    }

  thread_p->resume_status = resume_reason;

  r = thread_unlock_entry (thread_p);

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
thread_check_suspend_reason_and_wakeup_internal (THREAD_ENTRY * thread_p,
						 int resume_reason,
						 int suspend_reason,
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
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_COND_SIGNAL, 0);
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
thread_check_suspend_reason_and_wakeup (THREAD_ENTRY * thread_p,
					int resume_reason, int suspend_reason)
{
  return thread_check_suspend_reason_and_wakeup_internal (thread_p,
							  resume_reason,
							  suspend_reason,
							  false);
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
      thread_sleep (0, 10000);	/* 10 msec */
    }
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
thread_suspend_with_other_mutex (THREAD_ENTRY * thread_p,
				 pthread_mutex_t * mutex_p, int timeout,
				 struct timespec *to, int suspended_reason)
{
  int r;
  int old_status;
  int error = NO_ERROR;

  assert (thread_p != NULL);
  old_status = thread_p->status;

  r = pthread_mutex_lock (&thread_p->th_entry_lock);
  if (r != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_LOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_LOCK;
    }

  thread_p->status = TS_WAIT;
  thread_p->resume_status = suspended_reason;

  r = pthread_mutex_unlock (&thread_p->th_entry_lock);
  if (r != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
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
      error = ER_CSS_PTHREAD_COND_WAIT;
      if (timeout == INF_WAIT || r != ETIMEDOUT)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
    }

  r = pthread_mutex_lock (&thread_p->th_entry_lock);
  if (r != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_LOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_LOCK;
    }

  thread_p->status = old_status;

  r = pthread_mutex_unlock (&thread_p->th_entry_lock);
  if (r != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
    }

  return error;
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

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * thread_exit() - The program will exit.
 *   return: void
 *   exit_id(in): an integer argument to be returned as the exit value.
 */
void
thread_exit (int exit_id)
{
  UINTPTR thread_exit_id = exit_id;

  THREAD_EXIT (thread_exit_id);
}
#endif

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

#if defined (ENABLE_UNUSED_FUNCTION)
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
  assert (thread_p != NULL);

  thread_p->rid = request_id;
}
#endif /* ENABLE_UNUSED_FUNCTION */

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
  int i, n, rv;
  THREAD_ENTRY *thread_p;
  CSS_CONN_ENTRY *conn_p;

  for (i = 1, n = 0; i <= thread_Manager.num_workers; i++)
    {
      thread_p = &thread_Manager.thread_array[i];
      if (thread_p == caller)
	{
	  continue;
	}
      rv = pthread_mutex_lock (&thread_p->tran_index_lock);
      if (thread_p->tid != pthread_self () && thread_p->status != TS_DEAD
	  && thread_p->status != TS_FREE && thread_p->status != TS_CHECK)
	{
	  conn_p = thread_p->conn_entry;
	  if (tran_index == NULL_TRAN_INDEX
	      && (conn_p != NULL && conn_p->client_id == client_id))
	    {
	      n++;
	    }
	  else if (tran_index == thread_p->tran_index
		   && (conn_p == NULL || conn_p->client_id == client_id))
	    {
	      n++;
	    }
	}
      pthread_mutex_unlock (&thread_p->tran_index_lock);
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
thread_get_info_threads (int *num_total_threads, int *num_worker_threads,
			 int *num_free_threads, int *num_suspended_threads)
{
  THREAD_ENTRY *thread_p;
  int i;

  if (num_total_threads)
    {
      *num_total_threads = thread_Manager.num_total;
    }
  if (num_worker_threads)
    {
      *num_worker_threads = thread_Manager.num_workers;
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
      for (i = 1; i <= thread_Manager.num_workers; i++)
	{
	  thread_p = &thread_Manager.thread_array[i];
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
  return thread_Manager.num_total;
}

#if defined(CUBRID_DEBUG)
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
  const char *status[] = {
    "dead", "free", "run", "wait", "check"
  };
  THREAD_ENTRY *thread_p;
  int i;

  for (i = 1; i <= thread_Manager.num_workers; i++)
    {
      thread_p = &thread_Manager.thread_array[i];

      fprintf (stderr,
	       "thread %d(tid(%ld),client_id(%d),tran_index(%d),"
	       "rid(%d),status(%s),interrupt(%d))\n",
	       thread_p->index, thread_p->tid, thread_p->client_id,
	       thread_p->tran_index, thread_p->rid,
	       status[thread_p->status], thread_p->interrupted);
    }
}
#endif

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
  rv = pthread_mutex_lock (&tsd_ptr->th_entry_lock);
  pthread_mutex_unlock (&tsd_ptr->th_entry_lock);

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
	  pthread_mutex_unlock (&tsd_ptr->tran_index_lock);
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
      rv = pthread_mutex_lock (&tsd_ptr->tran_index_lock);
      tsd_ptr->tran_index = -1;
      pthread_mutex_unlock (&tsd_ptr->tran_index_lock);
      tsd_ptr->check_interrupt = true;
    }

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

  r = pthread_cond_init (&thread_Deadlock_detect_thread.cond, NULL);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_COND_INIT, 0);
      return ER_CSS_PTHREAD_COND_INIT;
    }
  r = pthread_mutex_init (&thread_Deadlock_detect_thread.lock, NULL);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_INIT, 0);
      return ER_CSS_PTHREAD_MUTEX_INIT;
    }

  r = pthread_cond_init (&thread_Checkpoint_thread.cond, NULL);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_COND_INIT, 0);
      return ER_CSS_PTHREAD_COND_INIT;
    }
  r = pthread_mutex_init (&thread_Checkpoint_thread.lock, NULL);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_INIT, 0);
      return ER_CSS_PTHREAD_MUTEX_INIT;
    }

  r = pthread_cond_init (&thread_Purge_archive_logs_thread.cond, NULL);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_COND_INIT, 0);
      return ER_CSS_PTHREAD_COND_INIT;
    }
  r = pthread_mutex_init (&thread_Purge_archive_logs_thread.lock, NULL);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_INIT, 0);
      return ER_CSS_PTHREAD_MUTEX_INIT;
    }

  r = pthread_cond_init (&thread_Oob_thread.cond, NULL);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_COND_INIT, 0);
      return ER_CSS_PTHREAD_COND_INIT;
    }
  r = pthread_mutex_init (&thread_Oob_thread.lock, NULL);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_INIT, 0);
      return ER_CSS_PTHREAD_MUTEX_INIT;
    }

  r = pthread_cond_init (&thread_Page_flush_thread.cond, NULL);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_COND_INIT, 0);
      return ER_CSS_PTHREAD_COND_INIT;
    }
  r = pthread_mutex_init (&thread_Page_flush_thread.lock, NULL);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_INIT, 0);
      return ER_CSS_PTHREAD_MUTEX_INIT;
    }

  r = pthread_cond_init (&thread_Flush_control_thread.cond, NULL);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_COND_INIT, 0);
      return ER_CSS_PTHREAD_COND_INIT;
    }
  r = pthread_mutex_init (&thread_Flush_control_thread.lock, NULL);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_INIT, 0);
      return ER_CSS_PTHREAD_MUTEX_INIT;
    }

  r = pthread_cond_init (&thread_Log_flush_thread.cond, NULL);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_COND_INIT, 0);
      return ER_CSS_PTHREAD_COND_INIT;
    }
  r = pthread_mutex_init (&thread_Log_flush_thread.lock, NULL);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_INIT, 0);
      return ER_CSS_PTHREAD_MUTEX_INIT;
    }

  r = pthread_cond_init (&thread_Session_control_thread.cond, NULL);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_COND_INIT, 0);
      return ER_CSS_PTHREAD_COND_INIT;
    }
  r = pthread_mutex_init (&thread_Session_control_thread.lock, NULL);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_INIT, 0);
      return ER_CSS_PTHREAD_MUTEX_INIT;
    }

  return r;
}
#endif /* WINDOWS */

/*
 * thread_deadlock_detect_thread() -
 *   return:
 */
static THREAD_RET_T THREAD_CALLING_CONVENTION
thread_deadlock_detect_thread (void *arg_p)
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
  rv = pthread_mutex_lock (&tsd_ptr->th_entry_lock);
  pthread_mutex_unlock (&tsd_ptr->th_entry_lock);

  thread_set_thread_entry_info (tsd_ptr);	/* save TSD */
  tsd_ptr->type = TT_DAEMON;
  tsd_ptr->status = TS_RUN;	/* set thread stat as RUN */

  thread_set_current_tran_index (tsd_ptr, LOG_SYSTEM_TRAN_INDEX);

  /* during server is active */
  while (!tsd_ptr->shutdown)
    {
      thread_sleep (0, 100000);
      if (!lock_check_local_deadlock_detection ())
	{
	  continue;
	}

      er_clear ();

      /* check if the lock-wait thread exists */
      thread_p = thread_find_first_lockwait_entry (&thrd_index);
      if (thread_p == (THREAD_ENTRY *) NULL)
	{
	  /* none is lock-waiting */
	  rv = pthread_mutex_lock (&thread_Deadlock_detect_thread.lock);
	  thread_Deadlock_detect_thread.is_running = false;
	  pthread_cond_wait (&thread_Deadlock_detect_thread.cond,
			     &thread_Deadlock_detect_thread.lock);

	  thread_Deadlock_detect_thread.is_running = true;

	  pthread_mutex_unlock (&thread_Deadlock_detect_thread.lock);
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

      if (lockwait_count >= 2)
	{
	  (void) lock_detect_local_deadlock (tsd_ptr);
	}
    }

  er_clear ();
  tsd_ptr->status = TS_DEAD;

  return (THREAD_RET_T) 0;
}

/*
 * thread_wakeup_deadlock_detect_thread() -
 *   return:
 */
void
thread_wakeup_deadlock_detect_thread (void)
{
  int rv;

  rv = pthread_mutex_lock (&thread_Deadlock_detect_thread.lock);
  if (thread_Deadlock_detect_thread.is_running == false)
    {
      pthread_cond_signal (&thread_Deadlock_detect_thread.cond);
    }
  pthread_mutex_unlock (&thread_Deadlock_detect_thread.lock);
}

static THREAD_RET_T THREAD_CALLING_CONVENTION
thread_session_control_thread (void *arg_p)
{
#if !defined(HPUX)
  THREAD_ENTRY *tsd_ptr = NULL;
#endif /* !HPUX */
  struct timeval timeout;
  struct timespec to = {
    0, 0
  };
  int rv = 0;

  tsd_ptr = (THREAD_ENTRY *) arg_p;

  /* wait until THREAD_CREATE() finishes */
  rv = pthread_mutex_lock (&tsd_ptr->th_entry_lock);
  pthread_mutex_unlock (&tsd_ptr->th_entry_lock);

  thread_set_thread_entry_info (tsd_ptr);	/* save TSD */
  tsd_ptr->type = TT_DAEMON;
  tsd_ptr->status = TS_RUN;	/* set thread stat as RUN */
  thread_Session_control_thread.is_running = true;

  thread_set_current_tran_index (tsd_ptr, LOG_SYSTEM_TRAN_INDEX);

  while (!tsd_ptr->shutdown)
    {
      er_clear ();

      gettimeofday (&timeout, NULL);
      to.tv_sec = timeout.tv_sec + 60;

      rv = pthread_mutex_lock (&thread_Session_control_thread.lock);
      pthread_cond_timedwait (&thread_Session_control_thread.cond,
			      &thread_Session_control_thread.lock, &to);
      pthread_mutex_unlock (&thread_Session_control_thread.lock);

      if (tsd_ptr->shutdown)
	{
	  break;
	}

      session_remove_expired_sessions (&timeout);
    }

  er_clear ();
  tsd_ptr->status = TS_DEAD;

  return (THREAD_RET_T) 0;
}

/*
 * thread_wakeup_session_control_thread() -
 *   return:
 */
void
thread_wakeup_session_control_thread (void)
{
  pthread_mutex_lock (&thread_Session_control_thread.lock);
  pthread_cond_signal (&thread_Session_control_thread.cond);
  pthread_mutex_unlock (&thread_Session_control_thread.lock);
}

/*
 * css_checkpoint_thread() -
 *   return:
 *   arg_p(in):
 */

static THREAD_RET_T THREAD_CALLING_CONVENTION
thread_checkpoint_thread (void *arg_p)
{
#if !defined(HPUX)
  THREAD_ENTRY *tsd_ptr;
#endif /* !HPUX */
  int rv;

  struct timespec to = {
    0, 0
  };

  tsd_ptr = (THREAD_ENTRY *) arg_p;
  /* wait until THREAD_CREATE() finish */
  rv = pthread_mutex_lock (&tsd_ptr->th_entry_lock);
  pthread_mutex_unlock (&tsd_ptr->th_entry_lock);

  thread_set_thread_entry_info (tsd_ptr);	/* save TSD */
  tsd_ptr->type = TT_DAEMON;
  tsd_ptr->status = TS_RUN;	/* set thread stat as RUN */

  thread_set_current_tran_index (tsd_ptr, LOG_SYSTEM_TRAN_INDEX);

  /* during server is active */
  while (!tsd_ptr->shutdown)
    {
      er_clear ();

      to.tv_sec = time (NULL) + PRM_LOG_CHECKPOINT_INTERVAL_MINUTES * 60;

      rv = pthread_mutex_lock (&thread_Checkpoint_thread.lock);
      pthread_cond_timedwait (&thread_Checkpoint_thread.cond,
			      &thread_Checkpoint_thread.lock, &to);
      pthread_mutex_unlock (&thread_Checkpoint_thread.lock);
      if (tsd_ptr->shutdown)
	{
	  break;
	}

      logpb_checkpoint (tsd_ptr);
    }

  er_clear ();
  tsd_ptr->status = TS_DEAD;

  return (THREAD_RET_T) 0;
}

/*
 * thread_wakeup_checkpoint_thread() -
 *   return:
 */
void
thread_wakeup_checkpoint_thread (void)
{
  int rv;

  rv = pthread_mutex_lock (&thread_Checkpoint_thread.lock);
  pthread_cond_signal (&thread_Checkpoint_thread.cond);
  pthread_mutex_unlock (&thread_Checkpoint_thread.lock);
}

/*
 * css_purge_archive_logs_thread() -
 *   return:
 *   arg_p(in):
 */

static THREAD_RET_T THREAD_CALLING_CONVENTION
thread_purge_archive_logs_thread (void *arg_p)
{
#if !defined(HPUX)
  THREAD_ENTRY *tsd_ptr;
#endif /* !HPUX */
  int rv;
  time_t cur_time, last_deleted_time = 0;
  struct timespec to = {
    0, 0
  };

  tsd_ptr = (THREAD_ENTRY *) arg_p;
  /* wait until THREAD_CREATE() finish */
  rv = pthread_mutex_lock (&tsd_ptr->th_entry_lock);
  pthread_mutex_unlock (&tsd_ptr->th_entry_lock);

  thread_set_thread_entry_info (tsd_ptr);	/* save TSD */
  tsd_ptr->type = TT_DAEMON;
  tsd_ptr->status = TS_RUN;	/* set thread stat as RUN */

  thread_set_current_tran_index (tsd_ptr, LOG_SYSTEM_TRAN_INDEX);

  /* during server is active */
  while (!tsd_ptr->shutdown)
    {
      er_clear ();

      if (PRM_REMOVE_LOG_ARCHIVES_INTERVAL > 0)
	{
	  to.tv_sec = time (NULL);
	  if (to.tv_sec >
	      last_deleted_time + PRM_REMOVE_LOG_ARCHIVES_INTERVAL)
	    {
	      to.tv_sec += PRM_REMOVE_LOG_ARCHIVES_INTERVAL;
	    }
	  else
	    {
	      to.tv_sec =
		last_deleted_time + PRM_REMOVE_LOG_ARCHIVES_INTERVAL;
	    }
	}

      rv = pthread_mutex_lock (&thread_Purge_archive_logs_thread.lock);
      if (PRM_REMOVE_LOG_ARCHIVES_INTERVAL > 0)
	{
	  pthread_cond_timedwait (&thread_Purge_archive_logs_thread.cond,
				  &thread_Purge_archive_logs_thread.lock,
				  &to);
	}
      else
	{
	  pthread_cond_wait (&thread_Purge_archive_logs_thread.cond,
			     &thread_Purge_archive_logs_thread.lock);
	}
      pthread_mutex_unlock (&thread_Purge_archive_logs_thread.lock);
      if (tsd_ptr->shutdown)
	{
	  break;
	}

      if (PRM_REMOVE_LOG_ARCHIVES_INTERVAL > 0)
	{
	  cur_time = time (NULL);
	  if (cur_time - last_deleted_time < PRM_REMOVE_LOG_ARCHIVES_INTERVAL)
	    {
	      /* do not delete logs. wait more time */
	      continue;
	    }
	  /* remove a log */
	  if (logpb_remove_archive_logs_exceed_limit (tsd_ptr, 1) > 0)
	    {
	      /* A log was deleted */
	      last_deleted_time = time (NULL);
	    }
	}
      else
	{
	  /* remove all unnecessary logs */
	  logpb_remove_archive_logs_exceed_limit (tsd_ptr, 0);
	}

    }

  er_clear ();
  tsd_ptr->status = TS_DEAD;

  return (THREAD_RET_T) 0;
}

/*
 * thread_wakeup_checkpoint_thread() -
 *   return:
 */
void
thread_wakeup_purge_archive_logs_thread (void)
{
  int rv;

  rv = pthread_mutex_lock (&thread_Purge_archive_logs_thread.lock);
  pthread_cond_signal (&thread_Purge_archive_logs_thread.cond);
  pthread_mutex_unlock (&thread_Purge_archive_logs_thread.lock);
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

  thread_p = &thread_Manager.thread_array[thread_Oob_thread.thread_index];
  pthread_kill (thread_p->tid, SIGURG);
#endif /* !WINDOWS */
}

/*
 * thread_page_flush_thread() -
 *   return:
 *   arg_p(in):
 */

static THREAD_RET_T THREAD_CALLING_CONVENTION
thread_page_flush_thread (void *arg_p)
{
#if !defined(HPUX)
  THREAD_ENTRY *tsd_ptr;
#endif /* !HPUX */
  int rv;
  struct timeval cur_time = {
    0, 0
  };

  struct timespec wakeup_time = {
    0, 0
  };
  int tmp_usec;
  int wakeup_interval;

  tsd_ptr = (THREAD_ENTRY *) arg_p;
  /* wait until THREAD_CREATE() finishes */
  rv = pthread_mutex_lock (&tsd_ptr->th_entry_lock);
  pthread_mutex_unlock (&tsd_ptr->th_entry_lock);

  thread_set_thread_entry_info (tsd_ptr);	/* save TSD */
  tsd_ptr->type = TT_DAEMON;	/* daemon thread */
  tsd_ptr->status = TS_RUN;	/* set thread stat as RUN */

  thread_Page_flush_thread.is_running = true;

  thread_set_current_tran_index (tsd_ptr, LOG_SYSTEM_TRAN_INDEX);

  while (!tsd_ptr->shutdown)
    {
      er_clear ();

      wakeup_interval = PRM_PAGE_BG_FLUSH_INTERVAL_MSEC;

      if (wakeup_interval > 0)
	{
	  gettimeofday (&cur_time, NULL);

	  wakeup_time.tv_sec = cur_time.tv_sec + (wakeup_interval / 1000);
	  tmp_usec = cur_time.tv_usec + (wakeup_interval % 1000) * 1000;
	  if (tmp_usec >= 1000000)
	    {
	      wakeup_time.tv_sec += 1;
	      tmp_usec -= 1000000;
	    }
	  wakeup_time.tv_nsec = tmp_usec * 1000;
	}

      rv = pthread_mutex_lock (&thread_Page_flush_thread.lock);
      thread_Page_flush_thread.is_running = false;

      if (wakeup_interval > 0)
	{
	  do
	    {
	      rv = pthread_cond_timedwait (&thread_Page_flush_thread.cond,
					   &thread_Page_flush_thread.lock,
					   &wakeup_time);
	    }
	  while (rv == 0);
	}
      else
	{
	  pthread_cond_wait (&thread_Page_flush_thread.cond,
			     &thread_Page_flush_thread.lock);
	}

      thread_Page_flush_thread.is_running = true;

      pthread_mutex_unlock (&thread_Page_flush_thread.lock);

      if (tsd_ptr->shutdown)
	{
	  break;
	}

      pgbuf_flush_victim_candidate (tsd_ptr, PRM_PB_BUFFER_FLUSH_RATIO);
    }

  er_clear ();
  tsd_ptr->status = TS_DEAD;

  thread_Page_flush_thread.is_running = false;

  return (THREAD_RET_T) 0;
}

/*
 * thread_wakeup_page_flush_thread() -
 *   return:
 */
void
thread_wakeup_page_flush_thread (void)
{
  int rv;

  if (thread_Page_flush_thread.is_running)
    {
      return;
    }

  rv = pthread_mutex_lock (&thread_Page_flush_thread.lock);
  if (!thread_Page_flush_thread.is_running)
    {
      pthread_cond_signal (&thread_Page_flush_thread.cond);
    }
  pthread_mutex_unlock (&thread_Page_flush_thread.lock);
}

static THREAD_RET_T THREAD_CALLING_CONVENTION
thread_flush_control_thread (void *arg_p)
{
#if !defined(HPUX)
  THREAD_ENTRY *tsd_ptr;
#endif /* !HPUX */
  int rv;

  struct timespec wakeup_time = {
    0, 0
  };

  struct timeval begin_tv, end_tv, diff_tv;
  int diff_usec;
  int wakeup_interval_in_msec = 50;	/* 1 msec */

  int elapsed_usec = 0;
  int usec_consumed = 0;
  int usec_consumed_sum = 0;
  int token_gen = 0;
  int token_gen_sum = 0;
  int token_shared = 0;
  int token_consumed = 0;
  int token_consumed_sum = 0;

  tsd_ptr = (THREAD_ENTRY *) arg_p;

  /* wait until THREAD_CREATE() finishes */
  rv = pthread_mutex_lock (&tsd_ptr->th_entry_lock);
  pthread_mutex_unlock (&tsd_ptr->th_entry_lock);

  thread_set_thread_entry_info (tsd_ptr);	/* save TSD */
  tsd_ptr->type = TT_DAEMON;	/* daemon thread */
  tsd_ptr->status = TS_RUN;	/* set thread stat as RUN */

  thread_Flush_control_thread.is_running = true;

  thread_set_current_tran_index (tsd_ptr, LOG_SYSTEM_TRAN_INDEX);

  rv = fileio_flush_control_initialize ();
  if (rv != NO_ERROR)
    {
      goto error;
    }

  while (!tsd_ptr->shutdown)
    {
      int tmp_usec;

      (void) gettimeofday (&begin_tv, NULL);
      er_clear ();

      wakeup_time.tv_sec = begin_tv.tv_sec + (wakeup_interval_in_msec / 1000);
      tmp_usec = begin_tv.tv_usec + (wakeup_interval_in_msec % 1000) * 1000;
      if (tmp_usec >= 1000000)
	{
	  wakeup_time.tv_sec += 1;
	  tmp_usec -= 1000000;
	}
      wakeup_time.tv_nsec = tmp_usec * 1000;

      rv = pthread_mutex_lock (&thread_Flush_control_thread.lock);
      thread_Flush_control_thread.is_running = false;

      pthread_cond_timedwait (&thread_Flush_control_thread.cond,
			      &thread_Flush_control_thread.lock,
			      &wakeup_time);

      thread_Flush_control_thread.is_running = true;

      pthread_mutex_unlock (&thread_Flush_control_thread.lock);

      if (tsd_ptr->shutdown)
	{
	  break;
	}

      (void) gettimeofday (&end_tv, NULL);
      DIFF_TIMEVAL (begin_tv, end_tv, diff_tv);
      diff_usec = diff_tv.tv_sec * 1000000 + diff_tv.tv_usec;

      /* Do it's job */
      (void) fileio_flush_control_add_tokens (tsd_ptr, diff_usec, &token_gen,
					      &token_consumed);
    }

  fileio_flush_control_finalize ();
  er_clear ();

error:
  tsd_ptr->status = TS_DEAD;

  thread_Flush_control_thread.is_running = false;

  return (THREAD_RET_T) 0;
}

void
thread_wakeup_flush_control_thread (void)
{
  int rv;

  if (thread_Flush_control_thread.is_running)
    {
      return;
    }

  rv = pthread_mutex_lock (&thread_Flush_control_thread.lock);
  if (!thread_Flush_control_thread.is_running)
    {
      pthread_cond_signal (&thread_Flush_control_thread.cond);
    }
  pthread_mutex_unlock (&thread_Flush_control_thread.lock);
}

/*
 * thread_get_LFT_min_wait_time_in_msec() - get LFT's minimum wait time
 *  return:
 *
 * Note: LFT can wakeup in 3 cases: group commit, bg flush, log hdr flush
 *       If they are not on, LFT has to be waked up by signal only.
 */
static int
thread_get_LFT_min_wait_time_in_msec (void)
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

  if (flush_interval != INT_MAX)
    {
      return flush_interval;
    }
  else
    {
      return 1000;		/* default wake-up time: 1000 msec */
    }
}

/*
 * thread_log_flush_thread() - flushed dirty log pages in background
 *   return:
 *   arg(in) : thread entry information
 *
 */
static THREAD_RET_T THREAD_CALLING_CONVENTION
thread_log_flush_thread (void *arg_p)
{
#if !defined(HPUX)
  THREAD_ENTRY *tsd_ptr;
#endif /* !HPUX */
  int rv;

  struct timespec LFT_wait_time = {
    0, 0
  };

  struct timeval start_time = {
    0, 0
  };
  struct timeval end_time = {
    0, 0
  };
  struct timeval work_time = {
    0, 0
  };

  double curr_elapsed = 0;
  double gc_elapsed = 0;
#if 0				/* disabled temporarily */
  double repl_elapsed = 0;
#endif
  int elapsed_msec;
  int remained_msec;
  int wakeup_interval;
  int ret;
  int temp_wait_usec;
  bool is_background_flush = true;
  LOG_GROUP_COMMIT_INFO *group_commit_info = &log_Gl.group_commit_info;
#if defined(WINDOWS)
  int loop;
#endif


  tsd_ptr = (THREAD_ENTRY *) arg_p;
  /* wait until THREAD_CREATE() finishes */
  rv = pthread_mutex_lock (&tsd_ptr->th_entry_lock);
  pthread_mutex_unlock (&tsd_ptr->th_entry_lock);

  thread_set_thread_entry_info (tsd_ptr);	/* save TSD */
  tsd_ptr->type = TT_DAEMON;	/* daemon thread */
  tsd_ptr->status = TS_RUN;	/* set thread stat as RUN */

  thread_Log_flush_thread.is_valid = true;

  thread_set_current_tran_index (tsd_ptr, LOG_SYSTEM_TRAN_INDEX);

  gettimeofday (&start_time, NULL);

  rv = pthread_mutex_lock (&thread_Log_flush_thread.lock);

  while (!tsd_ptr->shutdown)
    {
      er_clear ();

      gettimeofday (&work_time, NULL);
      elapsed_msec =
	(int) (LOG_GET_ELAPSED_TIME (work_time, start_time) * 1000);

      wakeup_interval = thread_get_LFT_min_wait_time_in_msec ();
      remained_msec = (int) (wakeup_interval - elapsed_msec);
      if (remained_msec < 0)
	{
	  remained_msec = 0;
	}

      LFT_wait_time.tv_sec = work_time.tv_sec + (remained_msec / 1000);

      temp_wait_usec = work_time.tv_usec + ((remained_msec % 1000) * 1000);

      if (temp_wait_usec >= 1000000)
	{
	  LFT_wait_time.tv_sec += 1;
	  temp_wait_usec -= 1000000;
	}
      LFT_wait_time.tv_nsec = temp_wait_usec * 1000;

      thread_Log_flush_thread.is_log_flush_force = false;
      thread_Log_flush_thread.is_running = false;

      ret = pthread_cond_timedwait (&thread_Log_flush_thread.cond,
				    &thread_Log_flush_thread.lock,
				    &LFT_wait_time);

      thread_Log_flush_thread.is_running = true;

      if (tsd_ptr->shutdown)
	{
	  break;
	}

      gettimeofday (&end_time, NULL);

      curr_elapsed = LOG_GET_ELAPSED_TIME (end_time, start_time);

      gc_elapsed += curr_elapsed;
#if 0				/* disabled temporarily */
      repl_elapsed += curr_elapsed;
#endif

      start_time = end_time;

#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "css_log_flush_thread: "
		    "[%d]curr_elapsed(%f) gc_elapsed(%f) repl_elapsed(%f) "
		    "elapsed_msec(%d) remained_msec(%d)\n",
		    (int) THREAD_ID (), curr_elapsed, gc_elapsed,
		    repl_elapsed, elapsed_msec, remained_msec);
#endif /* CUBRID_DEBUG */

      rv = pthread_mutex_lock (&group_commit_info->gc_mutex);

      is_background_flush = false;
      if (ret == ETIMEDOUT)
	{
	  if (thread_Log_flush_thread.is_log_flush_force
	      || (LOG_IS_GROUP_COMMIT_ACTIVE ()
		  && gc_elapsed * 1000 >=
		  PRM_LOG_GROUP_COMMIT_INTERVAL_MSECS))
	    {
	      ;			/* is normal log flush */
	    }
	  else if (PRM_LOG_BG_FLUSH_INTERVAL_MSECS > 0)
	    {
	      is_background_flush = true;
	    }
	  else
	    {
	      /* dummy wake-up */
	      pthread_mutex_unlock (&group_commit_info->gc_mutex);
	      continue;
	    }

#if 0				/* disabled temporarily */
	  if (PRM_HA_MODE != HA_MODE_OFF
	      && (repl_elapsed >= (double) PRM_LOG_HEADER_FLUSH_INTERVAL))
	    {
	      LOG_CS_ENTER (tsd_ptr);
	      logpb_flush_header (tsd_ptr);
	      LOG_CS_EXIT ();

	      repl_elapsed = 0;
	    }
#endif
	}

      if (is_background_flush)
	{
	  rv = pthread_mutex_lock (&log_Gl.flush_info.flush_mutex);
	  if (log_Gl.flush_info.num_toflush > 2)
	    {
	      pthread_mutex_unlock (&log_Gl.flush_info.flush_mutex);
	      LOG_CS_ENTER (tsd_ptr);
	      logpb_flush_pages_background (tsd_ptr);
	      LOG_CS_EXIT ();
	    }
	  else
	    {
	      pthread_mutex_unlock (&log_Gl.flush_info.flush_mutex);
	    }
	}
      else
	{
	  LOG_CS_ENTER (tsd_ptr);
	  logpb_flush_all_append_pages (tsd_ptr, LOG_FLUSH_DIRECT, NULL);
	  LOG_CS_EXIT ();

	  log_Stat.gc_flush_count++;
	  gc_elapsed = 0;

#if defined(WINDOWS)
	  for (loop = 0; loop != group_commit_info->waiters; ++loop)
	    {
	      pthread_cond_broadcast (&group_commit_info->gc_cond);
	    }
#else /* WINDOWS */
	  pthread_cond_broadcast (&group_commit_info->gc_cond);
#endif /* WINDOWS */

#if defined(CUBRID_DEBUG)
	  er_log_debug (ARG_FILE_LINE,
			"css_log_flush_thread: "
			"[%d]send signal - waiters(%d) \n",
			(int) THREAD_ID (), group_commit_info->waiters);
#endif /* CUBRID_DEBUG */
	  group_commit_info->waiters = 0;
	}

      pthread_mutex_unlock (&group_commit_info->gc_mutex);
    }

  thread_Log_flush_thread.is_valid = false;
  thread_Log_flush_thread.is_running = false;
  pthread_mutex_unlock (&thread_Log_flush_thread.lock);

  er_clear ();
  tsd_ptr->status = TS_DEAD;

#if defined(CUBRID_DEBUG)
  er_log_debug (ARG_FILE_LINE,
		"css_log_flush_thread: " "[%d]end \n", (int) THREAD_ID ());
#endif /* CUBRID_DEBUG */

  return (THREAD_RET_T) 0;
}


/*
 * thread_wakeup_log_flush_thread() -
 *   return:
 */
void
thread_wakeup_log_flush_thread (void)
{
  pthread_cond_signal (&thread_Log_flush_thread.cond);
}

#if defined(USE_LOG_CLOCK_THREAD)
/*
 * thread_log_clock_thread() - set time for every 500 ms
 *   return:
 *   arg(in) : thread entry information
 *
 */
static THREAD_RET_T THREAD_CALLING_CONVENTION
thread_log_clock_thread (void *arg_p)
{
#if !defined(HPUX)
  THREAD_ENTRY *tsd_ptr = NULL;
#endif /* !HPUX */
  int rv = 0;
  struct timeval now;

  assert (sizeof (log_Clock) >= sizeof (now.tv_sec));
  tsd_ptr = (THREAD_ENTRY *) arg_p;

  /* wait until THREAD_CREATE() finishes */
  rv = pthread_mutex_lock (&tsd_ptr->th_entry_lock);
  pthread_mutex_unlock (&tsd_ptr->th_entry_lock);

  thread_set_thread_entry_info (tsd_ptr);	/* save TSD */
  tsd_ptr->type = TT_DAEMON;
  tsd_ptr->status = TS_RUN;	/* set thread stat as RUN */
  thread_Log_clock_thread.is_running = true;

  while (!tsd_ptr->shutdown)
    {
      er_clear ();

      /* set time for every 500 ms */
      gettimeofday (&now, NULL);
      ATOMIC_TAS_64 (&log_Clock, now.tv_sec);
      thread_sleep (0, 500000);

      if (tsd_ptr->shutdown)
	{
	  break;
	}
    }

  er_clear ();
  tsd_ptr->status = TS_DEAD;

  return (THREAD_RET_T) 0;
}
#endif /* USE_LOG_CLOCK_THREAD */
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
       i < THREAD_RETRY_MAX_SLAM_TIMES && error_code == NO_ERROR && !killed;
       i++)
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

  for (i = 1; i <= thread_Manager.num_workers; i++)
    {
      thread_p = &(thread_Manager.thread_array[i]);
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

  for (i = (*thread_index_p + 1); i <= thread_Manager.num_workers; i++)
    {
      thread_p = &(thread_Manager.thread_array[i]);
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
  return (&thread_Manager.thread_array[thread_index]);
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
  for (i = 1; i <= thread_Manager.num_workers; i++)
    {
      thread_p = &(thread_Manager.thread_array[i]);
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

/*
 * thread_set_info () -
 *   return:
 *   thread_p(out):
 *   client_id(in):
 *   rid(in):
 *   tran_index(in):
 */
void
thread_set_info (THREAD_ENTRY * thread_p, int client_id, int rid,
		 int tran_index)
{
  thread_p->client_id = client_id;
  thread_p->rid = rid;
  thread_p->tran_index = tran_index;
  thread_p->victim_request_fail = false;
  thread_p->next_wait_thrd = NULL;
  thread_p->lockwait = NULL;
  thread_p->lockwait_state = -1;
  thread_p->query_entry = NULL;
  thread_p->tran_next_wait = NULL;
}
