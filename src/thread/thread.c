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
#include "show_scan.h"
#include "network.h"
#include "db_date.h"
#if defined(WINDOWS)
#include "wintcp.h"
#else /* WINDOWS */
#include "tcp.h"
#endif /* WINDOWS */
#include "vacuum.h"

#if defined(WINDOWS)
#include "heartbeat.h"
#endif

#include "tsc_timer.h"

#include "fault_injection.h"

#include "thread_manager.hpp"

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

static const int THREAD_RETRY_MAX_SLAM_TIMES = 10;

#if defined(HPUX)
static __thread THREAD_ENTRY *tsd_ptr;
#else /* HPUX */
static pthread_key_t thread_Thread_key;
#endif /* HPUX */

static THREAD_MANAGER thread_Manager;

/*
 * For special Purpose Threads: deadlock detector, checkpoint daemon
 *    Under the win32-threads system, *_cond variables are an auto-reset event
 */
static DAEMON_THREAD_MONITOR thread_Deadlock_detect_thread = DAEMON_THREAD_MONITOR_INITIALIZER;
static DAEMON_THREAD_MONITOR thread_Checkpoint_thread = DAEMON_THREAD_MONITOR_INITIALIZER;
static DAEMON_THREAD_MONITOR thread_Purge_archive_logs_thread = DAEMON_THREAD_MONITOR_INITIALIZER;
static DAEMON_THREAD_MONITOR thread_Oob_thread = DAEMON_THREAD_MONITOR_INITIALIZER;
static DAEMON_THREAD_MONITOR thread_Page_flush_thread = DAEMON_THREAD_MONITOR_INITIALIZER;
static DAEMON_THREAD_MONITOR thread_Flush_control_thread = DAEMON_THREAD_MONITOR_INITIALIZER;
static DAEMON_THREAD_MONITOR thread_Session_control_thread = DAEMON_THREAD_MONITOR_INITIALIZER;
DAEMON_THREAD_MONITOR thread_Log_flush_thread = DAEMON_THREAD_MONITOR_INITIALIZER;
static DAEMON_THREAD_MONITOR thread_Check_ha_delay_info_thread = DAEMON_THREAD_MONITOR_INITIALIZER;
static DAEMON_THREAD_MONITOR thread_Auto_volume_expansion_thread = DAEMON_THREAD_MONITOR_INITIALIZER;
static DAEMON_THREAD_MONITOR thread_Log_clock_thread = DAEMON_THREAD_MONITOR_INITIALIZER;
static DAEMON_THREAD_MONITOR thread_Page_maintenance_thread = DAEMON_THREAD_MONITOR_INITIALIZER;
static DAEMON_THREAD_MONITOR thread_Page_post_flush_thread = DAEMON_THREAD_MONITOR_INITIALIZER;

static void thread_stop_oob_handler_thread (void);
static void thread_stop_daemon (DAEMON_THREAD_MONITOR * daemon_monitor);
static void thread_wakeup_daemon_thread (DAEMON_THREAD_MONITOR * daemon_monitor);
static int thread_compare_shutdown_sequence_of_daemon (const void *p1, const void *p2);

static THREAD_RET_T THREAD_CALLING_CONVENTION thread_deadlock_detect_thread (void *);
static THREAD_RET_T THREAD_CALLING_CONVENTION thread_checkpoint_thread (void *);
static THREAD_RET_T THREAD_CALLING_CONVENTION thread_purge_archive_logs_thread (void *);
static THREAD_RET_T THREAD_CALLING_CONVENTION thread_page_flush_thread (void *);
static THREAD_RET_T THREAD_CALLING_CONVENTION thread_flush_control_thread (void *);
static THREAD_RET_T THREAD_CALLING_CONVENTION thread_log_flush_thread (void *);
static THREAD_RET_T THREAD_CALLING_CONVENTION thread_session_control_thread (void *);
static THREAD_RET_T THREAD_CALLING_CONVENTION thread_check_ha_delay_info_thread (void *);
static THREAD_RET_T THREAD_CALLING_CONVENTION thread_auto_volume_expansion_thread (void *);
static THREAD_RET_T THREAD_CALLING_CONVENTION thread_log_clock_thread (void *);
static THREAD_RET_T THREAD_CALLING_CONVENTION thread_page_buffer_maintenance_thread (void *);
static THREAD_RET_T THREAD_CALLING_CONVENTION thread_page_post_flush_thread (void *);

typedef enum
{
  /* All the single threads */
  THREAD_DAEMON_CSS_OOB_HANDLER,
  THREAD_DAEMON_DEADLOCK_DETECT,
  THREAD_DAEMON_PURGE_ARCHIVE_LOGS,
  THREAD_DAEMON_CHECKPOINT,
  THREAD_DAEMON_SESSION_CONTROL,
  THREAD_DAEMON_CHECK_HA_DELAY_INFO,
  THREAD_DAEMON_AUTO_VOLUME_EXPANSION,
  THREAD_DAEMON_LOG_CLOCK,
  THREAD_DAEMON_PAGE_FLUSH,
  THREAD_DAEMON_FLUSH_CONTROL,
  THREAD_DAEMON_LOG_FLUSH,
  THREAD_DAEMON_PAGE_MAINTENANCE,
  THREAD_DAEMON_PAGE_POST_FLUSH,

  THREAD_DAEMON_NUM_SINGLE_THREADS
} THREAD_DAEMON_TYPE;

typedef struct thread_daemon THREAD_DAEMON;
struct thread_daemon
{
  DAEMON_THREAD_MONITOR *daemon_monitor;
  THREAD_DAEMON_TYPE type;
  int shutdown_sequence;
    THREAD_RET_T (THREAD_CALLING_CONVENTION * daemon_function) (void *);
};

static THREAD_DAEMON *thread_Daemons = NULL;

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

#if defined(WINDOWS)
static int thread_initialize_sync_object (void);
#endif /* WINDOWS */
static int thread_wakeup_internal (THREAD_ENTRY * thread_p, int resume_reason, bool had_mutex);
static void thread_initialize_daemon_monitor (DAEMON_THREAD_MONITOR * monitor);

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

STATIC_INLINE void thread_daemon_wait (DAEMON_THREAD_MONITOR * daemon) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool thread_daemon_timedwait (DAEMON_THREAD_MONITOR * daemon, int wait_msec)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void thread_daemon_start (DAEMON_THREAD_MONITOR * daemon, THREAD_ENTRY * thread_p,
					THREAD_TYPE thread_type) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void thread_daemon_stop (DAEMON_THREAD_MONITOR * daemon, THREAD_ENTRY * thread_p)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void thread_daemon_wakeup (DAEMON_THREAD_MONITOR * daemon) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void thread_daemon_try_wakeup (DAEMON_THREAD_MONITOR * daemon) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void thread_daemon_wakeup_onereq (DAEMON_THREAD_MONITOR * daemon) __attribute__ ((ALWAYS_INLINE));

#if !defined(NDEBUG)
static void thread_rc_track_meter_at (THREAD_RC_METER * meter, const char *caller_file, int caller_line, int amount,
				      void *ptr);
static void thread_rc_track_meter_assert_csect_dependency (THREAD_ENTRY * thread_p, THREAD_RC_METER * meter, int amount,
							   void *ptr);
static void thread_rc_track_meter_assert_csect_usage (THREAD_ENTRY * thread_p, THREAD_RC_METER * meter, int enter_mode,
						      void *ptr);
#endif /* !NDEBUG */

extern int catcls_get_apply_info_log_record_time (THREAD_ENTRY * thread_p, time_t * log_record_time);

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
thread_initialize_manager (size_t & total_thread_count)
{
  int i, r;
  int daemon_index;
  int shutdown_sequence;
  size_t size;

  assert (NUM_NORMAL_TRANS >= 10);
  assert (thread_Manager.initialized == false);

  /* Initialize daemons */
  thread_Manager.num_daemons = THREAD_DAEMON_NUM_SINGLE_THREADS;
  size = thread_Manager.num_daemons * sizeof (THREAD_DAEMON);
  thread_Daemons = (THREAD_DAEMON *) malloc (size);
  if (thread_Daemons == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* IMPORTANT NOTE: Daemons are shutdown in the same order as they are created here. */
  daemon_index = 0;
  shutdown_sequence = 0;

  /* Initialize CSS OOB Handler daemon */
  thread_Daemons[daemon_index].type = THREAD_DAEMON_CSS_OOB_HANDLER;
  thread_Daemons[daemon_index].daemon_monitor = &thread_Oob_thread;
  thread_Daemons[daemon_index].shutdown_sequence = shutdown_sequence++;
  thread_Daemons[daemon_index++].daemon_function = css_oob_handler_thread;

  /* Initialize deadlock detect daemon */
  thread_Daemons[daemon_index].type = THREAD_DAEMON_DEADLOCK_DETECT;
  thread_Daemons[daemon_index].daemon_monitor = &thread_Deadlock_detect_thread;
  thread_Daemons[daemon_index].shutdown_sequence = shutdown_sequence++;
  thread_Daemons[daemon_index++].daemon_function = thread_deadlock_detect_thread;

  /* Initialize purge archive logs daemon */
  thread_Daemons[daemon_index].type = THREAD_DAEMON_PURGE_ARCHIVE_LOGS;
  thread_Daemons[daemon_index].daemon_monitor = &thread_Purge_archive_logs_thread;
  thread_Daemons[daemon_index].shutdown_sequence = shutdown_sequence++;
  thread_Daemons[daemon_index++].daemon_function = thread_purge_archive_logs_thread;

  /* Initialize checkpoint daemon */
  thread_Daemons[daemon_index].type = THREAD_DAEMON_CHECKPOINT;
  thread_Daemons[daemon_index].daemon_monitor = &thread_Checkpoint_thread;
  thread_Daemons[daemon_index].shutdown_sequence = shutdown_sequence++;
  thread_Daemons[daemon_index++].daemon_function = thread_checkpoint_thread;

  /* Initialize session control daemon */
  thread_Daemons[daemon_index].type = THREAD_DAEMON_SESSION_CONTROL;
  thread_Daemons[daemon_index].daemon_monitor = &thread_Session_control_thread;
  thread_Daemons[daemon_index].shutdown_sequence = shutdown_sequence++;
  thread_Daemons[daemon_index++].daemon_function = thread_session_control_thread;

  /* Initialize check HA delay info daemon */
  thread_Daemons[daemon_index].type = THREAD_DAEMON_CHECK_HA_DELAY_INFO;
  thread_Daemons[daemon_index].daemon_monitor = &thread_Check_ha_delay_info_thread;
  thread_Daemons[daemon_index].shutdown_sequence = shutdown_sequence++;
  thread_Daemons[daemon_index++].daemon_function = thread_check_ha_delay_info_thread;

  /* Initialize auto volume expansion daemon */
  thread_Daemons[daemon_index].type = THREAD_DAEMON_AUTO_VOLUME_EXPANSION;
  thread_Daemons[daemon_index].daemon_monitor = &thread_Auto_volume_expansion_thread;
  thread_Daemons[daemon_index].shutdown_sequence = shutdown_sequence++;
  thread_Daemons[daemon_index++].daemon_function = thread_auto_volume_expansion_thread;

  /* Initialize log clock daemon */
  thread_Daemons[daemon_index].type = THREAD_DAEMON_LOG_CLOCK;
  thread_Daemons[daemon_index].daemon_monitor = &thread_Log_clock_thread;
  thread_Daemons[daemon_index].shutdown_sequence = shutdown_sequence++;
  thread_Daemons[daemon_index++].daemon_function = thread_log_clock_thread;

  /* Leave these five daemons at the end. These are to be shutdown latest */
  /* Initialize page buffer maintenance daemon */
  thread_Daemons[daemon_index].type = THREAD_DAEMON_PAGE_MAINTENANCE;
  thread_Daemons[daemon_index].daemon_monitor = &thread_Page_maintenance_thread;
  thread_Daemons[daemon_index].shutdown_sequence = INT_MAX - 4;
  thread_Daemons[daemon_index++].daemon_function = thread_page_buffer_maintenance_thread;

  /* Initialize page flush daemon */
  thread_Daemons[daemon_index].type = THREAD_DAEMON_PAGE_FLUSH;
  thread_Daemons[daemon_index].daemon_monitor = &thread_Page_flush_thread;
  thread_Daemons[daemon_index].shutdown_sequence = INT_MAX - 3;
  thread_Daemons[daemon_index++].daemon_function = thread_page_flush_thread;

  /* Initialize page post flush daemon */
  thread_Daemons[daemon_index].type = THREAD_DAEMON_PAGE_POST_FLUSH;
  thread_Daemons[daemon_index].daemon_monitor = &thread_Page_post_flush_thread;
  thread_Daemons[daemon_index].shutdown_sequence = INT_MAX - 2;
  thread_Daemons[daemon_index++].daemon_function = thread_page_post_flush_thread;

  /* Initialize flush control daemon */
  thread_Daemons[daemon_index].type = THREAD_DAEMON_FLUSH_CONTROL;
  thread_Daemons[daemon_index].daemon_monitor = &thread_Flush_control_thread;
  thread_Daemons[daemon_index].shutdown_sequence = INT_MAX - 1;
  thread_Daemons[daemon_index++].daemon_function = thread_flush_control_thread;

  /* Initialize log flush daemon */
  thread_Daemons[daemon_index].type = THREAD_DAEMON_LOG_FLUSH;
  thread_Daemons[daemon_index].daemon_monitor = &thread_Log_flush_thread;
  thread_Daemons[daemon_index].shutdown_sequence = INT_MAX;
  thread_Daemons[daemon_index++].daemon_function = thread_log_flush_thread;

  /* Add new daemons before page flush daemon */

  assert (daemon_index == thread_Manager.num_daemons);

#if defined(WINDOWS)
  thread_initialize_sync_object ();
#endif /* WINDOWS */

  thread_Manager.num_workers = NUM_NON_SYSTEM_TRANS * 2;

  thread_Manager.num_total = (thread_Manager.num_workers + thread_Manager.num_daemons);

  /* initialize lock-free transaction systems */
  r = lf_initialize_transaction_systems (thread_Manager.num_total + (int) cubthread::get_max_thread_count ());
  if (r != NO_ERROR)
    {
      return r;
    }

  /* allocate threads */
  thread_Manager.thread_array = new THREAD_ENTRY[thread_Manager.num_total];

  /* init worker/deadlock-detection/checkpoint daemon/audit-flush oob-handler thread/page flush thread/log flush thread
   * thread_mgr.thread_array[0] is used for main thread */
  for (i = 0; i < thread_Manager.num_total; i++)
    {
      thread_Manager.thread_array[i].index = i + 1;
      thread_Manager.thread_array[i].request_lock_free_transactions ();
    }

  thread_Manager.initialized = true;

  total_thread_count = thread_Manager.num_total;

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
  int i;
  int thread_index, r;
  THREAD_ENTRY *thread_p = NULL;
  pthread_attr_t thread_attr;
#if defined(_POSIX_THREAD_ATTR_STACKSIZE)
  size_t ts_size;
#endif /* _POSIX_THREAD_ATTR_STACKSIZE */

  assert (thread_Manager.initialized == true);

#if !defined(WINDOWS)
  r = pthread_attr_init (&thread_attr);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_ATTR_INIT, 0);
      return ER_CSS_PTHREAD_ATTR_INIT;
    }

  r = pthread_attr_setdetachstate (&thread_attr, PTHREAD_CREATE_DETACHED);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_ATTR_SETDETACHSTATE, 0);
      return ER_CSS_PTHREAD_ATTR_SETDETACHSTATE;
    }

#if defined(AIX)
  /* AIX's pthread is slightly different from other systems. Its performance highly depends on the pthread's scope and
   * its related kernel parameters. */
  r =
    pthread_attr_setscope (&thread_attr,
			   prm_get_bool_value (PRM_ID_PTHREAD_SCOPE_PROCESS) ? PTHREAD_SCOPE_PROCESS :
			   PTHREAD_SCOPE_SYSTEM);
#else /* AIX */
  r = pthread_attr_setscope (&thread_attr, PTHREAD_SCOPE_SYSTEM);
#endif /* AIX */
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_ATTR_SETSCOPE, 0);
      return ER_CSS_PTHREAD_ATTR_SETSCOPE;
    }

#if defined(_POSIX_THREAD_ATTR_STACKSIZE)
  r = pthread_attr_getstacksize (&thread_attr, &ts_size);
  if (ts_size != (size_t) prm_get_bigint_value (PRM_ID_THREAD_STACKSIZE))
    {
      r = pthread_attr_setstacksize (&thread_attr, prm_get_bigint_value (PRM_ID_THREAD_STACKSIZE));
      if (r != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_ATTR_SETSTACKSIZE, 0);
	  return ER_CSS_PTHREAD_ATTR_SETSTACKSIZE;
	}
    }
#endif /* _POSIX_THREAD_ATTR_STACKSIZE */
#endif /* WINDOWS */

  /* start worker thread */
  for (thread_index = 0; thread_index < thread_Manager.num_workers; thread_index++)
    {
      thread_p = &thread_Manager.thread_array[thread_index];

      r = pthread_mutex_lock (&thread_p->th_entry_lock);
      if (r != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_LOCK, 0);
	  return ER_CSS_PTHREAD_MUTEX_LOCK;
	}

      /* If win32, then "thread_attr" is ignored, else "p->thread_handle". */
      r = pthread_create (&thread_p->tid, &thread_attr, thread_worker, thread_p);
      if (r != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_CREATE, 0);
	  pthread_mutex_unlock (&thread_p->th_entry_lock);
	  return ER_CSS_PTHREAD_CREATE;
	}

      r = pthread_mutex_unlock (&thread_p->th_entry_lock);
      if (r != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
	  return ER_CSS_PTHREAD_MUTEX_UNLOCK;
	}
    }

  for (i = 0; thread_index < thread_Manager.num_total; thread_index++, i++)
    {
      thread_Daemons[i].daemon_monitor->thread_index = thread_index;
      thread_p = &thread_Manager.thread_array[thread_index];

      r = pthread_mutex_lock (&thread_p->th_entry_lock);
      if (r != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_LOCK, 0);
	  return ER_CSS_PTHREAD_MUTEX_LOCK;
	}

      /* If win32, then "thread_attr" is ignored, else "p->thread_handle". */
      r = pthread_create (&thread_p->tid, &thread_attr, thread_Daemons[i].daemon_function, thread_p);
      if (r != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_CREATE, 0);
	  pthread_mutex_unlock (&thread_p->th_entry_lock);
	  return ER_CSS_PTHREAD_CREATE;
	}

      r = pthread_mutex_unlock (&thread_p->th_entry_lock);
      if (r != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
	  return ER_CSS_PTHREAD_MUTEX_UNLOCK;
	}
    }

  /* destroy thread_attribute */
  r = pthread_attr_destroy (&thread_attr);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_ATTR_DESTROY, 0);
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
  int i;
  int r;
  bool repeat_loop;
  THREAD_ENTRY *thread_p;
  CSS_CONN_ENTRY *conn_p;

  assert (thread_Manager.initialized == true);

  if (stop_phase == THREAD_STOP_WORKERS_EXCEPT_LOGWR)
    {
      css_block_all_active_conn (stop_phase);
    }

loop:
  for (i = 0; i < thread_Manager.num_workers; i++)
    {
      thread_p = &thread_Manager.thread_array[i];

      conn_p = thread_p->conn_entry;
      if ((stop_phase == THREAD_STOP_LOGWR && conn_p == NULL) || (conn_p && conn_p->stop_phase != stop_phase))
	{
	  continue;
	}

      if (thread_p->tran_index != -1)
	{
	  if (stop_phase == THREAD_STOP_WORKERS_EXCEPT_LOGWR)
	    {
	      logtb_set_tran_index_interrupt (NULL, thread_p->tran_index, 1);
	    }

	  if (thread_p->status == TS_WAIT && logtb_is_current_active (thread_p))
	    {
	      if (stop_phase == THREAD_STOP_WORKERS_EXCEPT_LOGWR)
		{
		  thread_lock_entry (thread_p);

		  /* The worker thread may have been waked up by others. Check it again. */
		  if (thread_p->tran_index != -1 && thread_p->status == TS_WAIT && thread_p->lockwait == NULL
		      && thread_p->check_interrupt == true)
		    {
		      thread_p->interrupted = true;
		      thread_wakeup_already_had_mutex (thread_p, THREAD_RESUME_DUE_TO_INTERRUPT);
		    }

		  thread_unlock_entry (thread_p);
		}
	      else if (stop_phase == THREAD_STOP_LOGWR)
		{
		  /* 
		   * we can only wakeup LWT when waiting on THREAD_LOGWR_SUSPENDED.
		   */
		  r = thread_check_suspend_reason_and_wakeup (thread_p, THREAD_RESUME_DUE_TO_INTERRUPT,
							      THREAD_LOGWR_SUSPENDED);
		  if (r == NO_ERROR)
		    {
		      thread_p->interrupted = true;
		    }
		}
	    }
	}
    }

  thread_sleep (50);		/* 50 msec */

  lock_force_timeout_lock_wait_transactions (stop_phase);

  /* Signal for blocked on job queue */
  /* css_broadcast_shutdown_thread(); */

  repeat_loop = false;
  for (i = 0; i < thread_Manager.num_workers; i++)
    {
      thread_p = &thread_Manager.thread_array[i];

      conn_p = thread_p->conn_entry;
      if ((stop_phase == THREAD_STOP_LOGWR && conn_p == NULL) || (conn_p && conn_p->stop_phase != stop_phase))
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
      if (css_is_shutdown_timeout_expired ())
	{
#if CUBRID_DEBUG
	  logtb_dump_trantable (NULL, stderr);
#endif
	  er_log_debug (ARG_FILE_LINE, "thread_stop_active_workers: _exit(0)\n");
	  /* exit process after some tries */
	  _exit (0);
	}

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
 * thread_wakeup_daemon_thread() -
 *
 */
static void
thread_wakeup_daemon_thread (DAEMON_THREAD_MONITOR * daemon_monitor)
{
  int rv;

  rv = pthread_mutex_lock (&daemon_monitor->lock);
  pthread_cond_signal (&daemon_monitor->cond);
  pthread_mutex_unlock (&daemon_monitor->lock);
}

/*
 * thread_stop_daemon() -
 *
 */
static void
thread_stop_daemon (DAEMON_THREAD_MONITOR * daemon_monitor)
{
  THREAD_ENTRY *thread_p;

  thread_p = &thread_Manager.thread_array[daemon_monitor->thread_index];
  thread_p->shutdown = true;

  while (thread_p->status != TS_DEAD)
    {
      thread_wakeup_daemon_thread (daemon_monitor);

      if (css_is_shutdown_timeout_expired ())
	{
	  er_log_debug (ARG_FILE_LINE, "thread_stop_daemon(%d): _exit(0)\n", daemon_monitor->thread_index);
	  /* exit process after some tries */
	  _exit (0);
	}
      thread_sleep (10);	/* 10 msec */
    }
}

/*
 * thread_compare_shutdown_sequence_of_daemon () -
 *   return: p1 - p2
 *   p1(in): daemon thread 1
 *   p2(in): daemon thread 2
 */
static int
thread_compare_shutdown_sequence_of_daemon (const void *p1, const void *p2)
{
  THREAD_DAEMON *daemon1, *daemon2;

  daemon1 = (THREAD_DAEMON *) p1;
  daemon2 = (THREAD_DAEMON *) p2;

  return daemon1->shutdown_sequence - daemon2->shutdown_sequence;
}

/*
 * thread_stop_oob_handler_thread () -
 */
static void
thread_stop_oob_handler_thread (void)
{
  THREAD_ENTRY *thread_p;

  thread_p = &thread_Manager.thread_array[thread_Oob_thread.thread_index];
  thread_p->shutdown = true;

  while (thread_p->status != TS_DEAD)
    {
      thread_wakeup_oob_handler_thread ();

      if (css_is_shutdown_timeout_expired ())
	{
	  er_log_debug (ARG_FILE_LINE, "thread_stop_oob_handler_thread: _exit(0)\n");
	  /* exit process after some tries */
	  _exit (0);
	}
      thread_sleep (10);	/* 10 msec */
    }
}

/*
 * thread_stop_active_daemons() - Stop deadlock detector/checkpoint threads
 *   return: NO_ERROR
 */
int
thread_stop_active_daemons (void)
{
  int i;

  thread_stop_oob_handler_thread ();

  for (i = 0; i < thread_Manager.num_daemons; i++)
    {
      assert ((i == 0) || (thread_Daemons[i - 1].shutdown_sequence < thread_Daemons[i].shutdown_sequence));

      thread_stop_daemon (thread_Daemons[i].daemon_monitor);
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
  int i;
  bool repeat_loop;
  THREAD_ENTRY *thread_p;

  for (i = 0; i < thread_Manager.num_workers; i++)
    {
      thread_p = &thread_Manager.thread_array[i];
      thread_p->interrupted = true;
      thread_p->shutdown = true;
    }

loop:

  /* Signal for blocked on job queue */
  css_broadcast_shutdown_thread ();

  repeat_loop = false;
  for (i = 0; i < thread_Manager.num_workers; i++)
    {
      thread_p = &thread_Manager.thread_array[i];
      if (thread_p->status != TS_DEAD)
	{
	  if (thread_p->status == TS_FREE && thread_p->resume_status == THREAD_JOB_QUEUE_SUSPENDED)
	    {
	      /* Defence code of a bug. wake up a thread which is not in the list of Job queue, but is waiting for a
	       * job. */
	      thread_wakeup (thread_p, THREAD_RESUME_DUE_TO_SHUTDOWN);
	    }
	  repeat_loop = true;
	}
    }

  if (repeat_loop)
    {
      if (css_is_shutdown_timeout_expired ())
	{
#if CUBRID_DEBUG
	  xlogtb_dump_trantable (NULL, stderr);
#endif
	  er_log_debug (ARG_FILE_LINE, "thread_kill_all_workers: _exit(0)\n");
	  /* exit process after some tries */
	  _exit (0);
	}
      thread_sleep (1000);	/* 1000 msec */
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
  /* *INDENT-OFF* */
  delete [] thread_Manager.thread_array;
  thread_Manager.thread_array = NULL;
  /* *INDENT-ON* */

  free_and_init (thread_Daemons);

  lf_destroy_transaction_systems ();

#ifndef HPUX
  pthread_key_delete (thread_Thread_key);
#endif /* not HPUX */
}

static void
thread_initialize_daemon_monitor (DAEMON_THREAD_MONITOR * monitor)
{
  assert (monitor != NULL);

  monitor->thread_index = 0;
  monitor->is_available = false;
  monitor->is_running = false;
  monitor->nrequestors = 0;
  pthread_mutex_init (&monitor->lock, NULL);
  pthread_cond_init (&monitor->cond, NULL);
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
 * thread_print_entry_info() -
 *   return: void
 *   thread_p(in):
 */
void
thread_print_entry_info (THREAD_ENTRY * thread_p)
{
  fprintf (stderr, "THREAD_ENTRY(tid(%lld),client_id(%d),tran_index(%d),rid(%d),status(%d))\n",
	   (long long) thread_p->tid, thread_p->client_id, thread_p->tran_index, thread_p->rid, thread_p->status);

  if (thread_p->conn_entry != NULL)
    {
      css_print_conn_entry_info (thread_p->conn_entry);
    }

  fflush (stderr);
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
  pthread_t me = pthread_self ();

  for (THREAD_ENTRY * thread_p = thread_iterate (NULL); thread_p != NULL; thread_p = thread_iterate (thread_p))
    {
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

bool
thread_belongs_to (THREAD_ENTRY * thread_p, int tran_index, int client_id)
{
  CSS_CONN_ENTRY *conn_p;
  bool does_belong = false;

  (void) pthread_mutex_lock (&thread_p->tran_index_lock);
  if (thread_p->tid != pthread_self () && thread_p->status != TS_DEAD && thread_p->status != TS_FREE
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

  for (THREAD_ENTRY * thread_p = thread_Manager.thread_array;
       thread_p < thread_Manager.thread_array + thread_Manager.num_workers; thread_p++)
    {
      if (thread_p == caller)
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
 * thread_dump_threads() - dump all thread
 *   return: void
 *
 * Note: for debug
 *       WARN: this function doesn't lock threadmgr
 */
void
thread_dump_threads (void)
{
  const char *status[] = { "dead", "free", "run", "wait", "check" };
  THREAD_ENTRY *thread_p;

  for (thread_p = thread_iterate (cubthread::get_main_entry ()); thread_p != NULL; thread_p = thread_iterate (thread_p))
    {
      fprintf (stderr, "thread %d(tid(%lld),client_id(%d),tran_index(%d),rid(%d),status(%s),interrupt(%d))\n",
	       thread_p->index, (long long) thread_p->tid, thread_p->client_id, thread_p->tran_index, thread_p->rid,
	       status[thread_p->status], thread_p->interrupted);

      (void) thread_rc_track_dump_all (thread_p, stderr);
    }

  fflush (stderr);
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
 * thread_worker() - Dequeue request from job queue and then call handler
 *                       function
 *   return:
 *   arg_p(in):
 */
THREAD_RET_T THREAD_CALLING_CONVENTION
thread_worker (void *arg_p)
{
#if !defined(HPUX)
  THREAD_ENTRY *tsd_ptr;
#endif /* !HPUX */
  CSS_JOB_ENTRY *job_entry_p;
  CSS_THREAD_FN handler_func;
  CSS_THREAD_ARG handler_func_arg;
  int jobq_index;
  int rv;

  tsd_ptr = (THREAD_ENTRY *) arg_p;

  /* wait until THREAD_CREATE() finish */
  rv = pthread_mutex_lock (&tsd_ptr->th_entry_lock);
  pthread_mutex_unlock (&tsd_ptr->th_entry_lock);

  thread_set_thread_entry_info (tsd_ptr);	/* save TSD */
  tsd_ptr->type = TT_WORKER;	/* not defined yet */
  tsd_ptr->status = TS_FREE;	/* set thread stat as free */

  tsd_ptr->get_error_context ().register_thread_local ();

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
      CSS_TRACE4 ("processing job_entry(%p, %p, %p)\n", job_entry_p->conn_entry, job_entry_p->func, job_entry_p->arg);
#endif /* _TRACE_THREADS_ */

      /* set tsd_ptr information */
      tsd_ptr->conn_entry = job_entry_p->conn_entry;
      if (tsd_ptr->conn_entry != NULL && tsd_ptr->conn_entry->session_p != NULL)
	{
	  tsd_ptr->private_lru_index = session_get_private_lru_idx (tsd_ptr->conn_entry->session_p);
	}
      else
	{
	  tsd_ptr->private_lru_index = -1;
	}

      tsd_ptr->status = TS_RUN;	/* set thread status as running */

      handler_func = job_entry_p->func;
      handler_func_arg = job_entry_p->arg;
      jobq_index = job_entry_p->jobq_index;

      css_incr_job_queue_counter (jobq_index, handler_func);

      css_free_job_entry (job_entry_p);

      handler_func (tsd_ptr, handler_func_arg);	/* invoke request handler */

      /* reset tsd_ptr information */
      tsd_ptr->conn_entry = NULL;
      tsd_ptr->status = TS_FREE;

      css_decr_job_queue_counter (jobq_index, handler_func);

      rv = pthread_mutex_lock (&tsd_ptr->tran_index_lock);
      tsd_ptr->tran_index = -1;
      pthread_mutex_unlock (&tsd_ptr->tran_index_lock);
      tsd_ptr->check_interrupt = true;

      memset (&(tsd_ptr->event_stats), 0, sizeof (EVENT_STAT));
      tsd_ptr->on_trace = false;
    }

  er_final (ER_THREAD_FINAL);

  tsd_ptr->conn_entry = NULL;
  tsd_ptr->tran_index = -1;
  tsd_ptr->status = TS_DEAD;

  return (THREAD_RET_T) 0;
}

/* Special Purpose Threads
   deadlock detector, check point daemon */

#if defined(WINDOWS)
/*
 * thread_initialize_sync_object() -
 *   return:
 */
static int
thread_initialize_sync_object (void)
{
  int r, i;

  r = NO_ERROR;

  for (i = 0; i < thread_Manager.num_daemons; i++)
    {
      r = pthread_cond_init (&thread_Daemons[i].daemon_monitor->cond, NULL);
      if (r != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_INIT, 0);
	  return ER_CSS_PTHREAD_COND_INIT;
	}
      r = pthread_mutex_init (&thread_Daemons[i].daemon_monitor->lock, NULL);
      if (r != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_INIT, 0);
	  return ER_CSS_PTHREAD_MUTEX_INIT;
	}
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

  thread_daemon_start (&thread_Deadlock_detect_thread, tsd_ptr, TT_DAEMON);

  /* during server is active */
  while (!tsd_ptr->shutdown)
    {
      thread_sleep (100);	/* 100 msec */
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

	  if (tsd_ptr->shutdown)
	    {
	      pthread_mutex_unlock (&thread_Deadlock_detect_thread.lock);
	      break;
	    }
	  pthread_cond_wait (&thread_Deadlock_detect_thread.cond, &thread_Deadlock_detect_thread.lock);

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

  rv = pthread_mutex_lock (&thread_Deadlock_detect_thread.lock);
  thread_Deadlock_detect_thread.is_running = false;
  thread_Deadlock_detect_thread.is_available = false;
  pthread_mutex_unlock (&thread_Deadlock_detect_thread.lock);

  er_final (ER_THREAD_FINAL);
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

  thread_daemon_start (&thread_Session_control_thread, tsd_ptr, TT_DAEMON);

  while (!tsd_ptr->shutdown)
    {
      er_clear ();

      gettimeofday (&timeout, NULL);
      to.tv_sec = timeout.tv_sec + 60;

      rv = pthread_mutex_lock (&thread_Session_control_thread.lock);
      pthread_cond_timedwait (&thread_Session_control_thread.cond, &thread_Session_control_thread.lock, &to);
      pthread_mutex_unlock (&thread_Session_control_thread.lock);

      if (tsd_ptr->shutdown)
	{
	  break;
	}

      session_remove_expired_sessions (&timeout);
    }
  rv = pthread_mutex_lock (&thread_Session_control_thread.lock);
  thread_Session_control_thread.is_available = false;
  thread_Session_control_thread.is_running = false;
  pthread_mutex_unlock (&thread_Session_control_thread.lock);

  er_final (ER_THREAD_FINAL);
  tsd_ptr->status = TS_DEAD;

  return (THREAD_RET_T) 0;
}

#if defined(ENABLE_UNUSED_FUNCTION)
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
#endif

/*
 * thread_checkpoint_thread() -
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

  thread_daemon_start (&thread_Checkpoint_thread, tsd_ptr, TT_DAEMON);

  /* during server is active */
  while (!tsd_ptr->shutdown)
    {
      er_clear ();

      to.tv_sec = (int) (time (NULL) + prm_get_integer_value (PRM_ID_LOG_CHECKPOINT_INTERVAL_SECS));

      rv = pthread_mutex_lock (&thread_Checkpoint_thread.lock);
      pthread_cond_timedwait (&thread_Checkpoint_thread.cond, &thread_Checkpoint_thread.lock, &to);
      pthread_mutex_unlock (&thread_Checkpoint_thread.lock);
      if (tsd_ptr->shutdown)
	{
	  break;
	}

      logpb_checkpoint (tsd_ptr);
    }

  rv = pthread_mutex_lock (&thread_Checkpoint_thread.lock);
  thread_Checkpoint_thread.is_available = false;
  thread_Checkpoint_thread.is_running = false;
  pthread_mutex_unlock (&thread_Checkpoint_thread.lock);

  er_final (ER_THREAD_FINAL);
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
 * thread_purge_archive_logs_thread() -
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

  thread_daemon_start (&thread_Purge_archive_logs_thread, tsd_ptr, TT_DAEMON);

  /* during server is active */
  while (!tsd_ptr->shutdown)
    {
      er_clear ();

      if (prm_get_integer_value (PRM_ID_REMOVE_LOG_ARCHIVES_INTERVAL) > 0)
	{
	  to.tv_sec = (int) time (NULL);
	  if (to.tv_sec > last_deleted_time + prm_get_integer_value (PRM_ID_REMOVE_LOG_ARCHIVES_INTERVAL))
	    {
	      to.tv_sec += prm_get_integer_value (PRM_ID_REMOVE_LOG_ARCHIVES_INTERVAL);
	    }
	  else
	    {
	      to.tv_sec = (int) (last_deleted_time + prm_get_integer_value (PRM_ID_REMOVE_LOG_ARCHIVES_INTERVAL));
	    }
	}

      rv = pthread_mutex_lock (&thread_Purge_archive_logs_thread.lock);
      if (prm_get_integer_value (PRM_ID_REMOVE_LOG_ARCHIVES_INTERVAL) > 0)
	{
	  pthread_cond_timedwait (&thread_Purge_archive_logs_thread.cond, &thread_Purge_archive_logs_thread.lock, &to);
	}
      else
	{
	  pthread_cond_wait (&thread_Purge_archive_logs_thread.cond, &thread_Purge_archive_logs_thread.lock);
	}
      pthread_mutex_unlock (&thread_Purge_archive_logs_thread.lock);
      if (tsd_ptr->shutdown)
	{
	  break;
	}

      if (prm_get_integer_value (PRM_ID_REMOVE_LOG_ARCHIVES_INTERVAL) > 0)
	{
	  cur_time = time (NULL);
	  if (cur_time - last_deleted_time < prm_get_integer_value (PRM_ID_REMOVE_LOG_ARCHIVES_INTERVAL))
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
  rv = pthread_mutex_lock (&thread_Purge_archive_logs_thread.lock);
  thread_Purge_archive_logs_thread.is_available = false;
  thread_Purge_archive_logs_thread.is_running = false;
  pthread_mutex_unlock (&thread_Purge_archive_logs_thread.lock);

  er_final (ER_THREAD_FINAL);
  tsd_ptr->status = TS_DEAD;

  return (THREAD_RET_T) 0;
}

/*
 * thread_wakeup_purge_archive_logs_thread() -
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
 * thread_check_ha_delay_info_thread() -
 *   return:
 *   arg_p(in):
 */

static THREAD_RET_T THREAD_CALLING_CONVENTION
thread_check_ha_delay_info_thread (void *arg_p)
{
#if !defined(HPUX)
  THREAD_ENTRY *tsd_ptr;
#endif /* !HPUX */
  struct timeval cur_time = { 0, 0 };
  struct timespec wakeup_time = { 0, 0 };

  int rv;
  INT64 tmp_usec;
  int wakeup_interval = 1000;
#if !defined(WINDOWS)
  time_t log_record_time = 0;
  int error_code;
  int delay_limit_in_secs;
  int acceptable_delay_in_secs;
  int curr_delay_in_secs;
  HA_SERVER_STATE server_state;
#endif

  tsd_ptr = (THREAD_ENTRY *) arg_p;

  thread_daemon_start (&thread_Check_ha_delay_info_thread, tsd_ptr, TT_DAEMON);

  while (!tsd_ptr->shutdown)
    {
      er_clear ();

      gettimeofday (&cur_time, NULL);
      wakeup_time.tv_sec = cur_time.tv_sec + (wakeup_interval / 1000);
      tmp_usec = cur_time.tv_usec + (wakeup_interval % 1000) * 1000;

      if (tmp_usec >= 1000000)
	{
	  wakeup_time.tv_sec += 1;
	  tmp_usec -= 1000000;
	}
      wakeup_time.tv_nsec = ((int) tmp_usec) * 1000;

      rv = pthread_mutex_lock (&thread_Check_ha_delay_info_thread.lock);
      thread_Check_ha_delay_info_thread.is_running = false;

      do
	{
	  rv =
	    pthread_cond_timedwait (&thread_Check_ha_delay_info_thread.cond, &thread_Check_ha_delay_info_thread.lock,
				    &wakeup_time);
	}
      while (rv == 0 && tsd_ptr->shutdown == false);

      thread_Check_ha_delay_info_thread.is_running = true;

      pthread_mutex_unlock (&thread_Check_ha_delay_info_thread.lock);

      if (tsd_ptr->shutdown == true)
	{
	  break;
	}

#if defined(WINDOWS)
      continue;
#else /* WINDOWS */

      /* do its job */
      csect_enter (tsd_ptr, CSECT_HA_SERVER_STATE, INF_WAIT);

      server_state = css_ha_server_state ();

      if (server_state == HA_SERVER_STATE_ACTIVE || server_state == HA_SERVER_STATE_TO_BE_STANDBY)
	{
	  css_unset_ha_repl_delayed ();
	  perfmon_set_stat (tsd_ptr, PSTAT_HA_REPL_DELAY, 0, true);

	  log_append_ha_server_state (tsd_ptr, server_state);

	  csect_exit (tsd_ptr, CSECT_HA_SERVER_STATE);
	}
      else
	{
	  csect_exit (tsd_ptr, CSECT_HA_SERVER_STATE);

	  delay_limit_in_secs = prm_get_integer_value (PRM_ID_HA_DELAY_LIMIT_IN_SECS);
	  acceptable_delay_in_secs = delay_limit_in_secs - prm_get_integer_value (PRM_ID_HA_DELAY_LIMIT_DELTA_IN_SECS);

	  if (acceptable_delay_in_secs < 0)
	    {
	      acceptable_delay_in_secs = 0;
	    }

	  error_code = catcls_get_apply_info_log_record_time (tsd_ptr, &log_record_time);

	  if (error_code == NO_ERROR && log_record_time > 0)
	    {
	      curr_delay_in_secs = time (NULL) - log_record_time;
	      if (curr_delay_in_secs > 0)
		{
		  curr_delay_in_secs -= HA_DELAY_ERR_CORRECTION;
		}

	      if (delay_limit_in_secs > 0)
		{
		  if (curr_delay_in_secs > delay_limit_in_secs)
		    {
		      if (!css_is_ha_repl_delayed ())
			{
			  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HA_REPL_DELAY_DETECTED, 2,
				  curr_delay_in_secs, delay_limit_in_secs);

			  css_set_ha_repl_delayed ();
			}
		    }
		  else if (curr_delay_in_secs <= acceptable_delay_in_secs)
		    {
		      if (css_is_ha_repl_delayed ())
			{
			  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HA_REPL_DELAY_RESOLVED, 2,
				  curr_delay_in_secs, acceptable_delay_in_secs);

			  css_unset_ha_repl_delayed ();
			}
		    }
		}

	      perfmon_set_stat (tsd_ptr, PSTAT_HA_REPL_DELAY, curr_delay_in_secs, true);
	    }
	}
#endif /* WINDOWS */
    }

  rv = pthread_mutex_lock (&thread_Check_ha_delay_info_thread.lock);
  thread_Check_ha_delay_info_thread.is_running = false;
  thread_Check_ha_delay_info_thread.is_available = false;
  pthread_mutex_unlock (&thread_Check_ha_delay_info_thread.lock);

  er_final (ER_THREAD_FINAL);
  tsd_ptr->status = TS_DEAD;

  return (THREAD_RET_T) 0;
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
  int wakeup_interval;
  PERF_UTIME_TRACKER perf_track;
  bool force_one_run = false;
  bool stop_iteration = false;

  tsd_ptr = (THREAD_ENTRY *) arg_p;
  thread_daemon_start (&thread_Page_flush_thread, tsd_ptr, TT_DAEMON);

  PERF_UTIME_TRACKER_START (tsd_ptr, &perf_track);
  while (!tsd_ptr->shutdown)
    {
      /* flush pages as long as necessary */
      while (!tsd_ptr->shutdown && (force_one_run || pgbuf_keep_victim_flush_thread_running ()))
	{
	  pgbuf_flush_victim_candidates (tsd_ptr, prm_get_float_value (PRM_ID_PB_BUFFER_FLUSH_RATIO), &perf_track,
					 &stop_iteration);
	  force_one_run = false;
	  if (stop_iteration)
	    {
	      break;
	    }
	}

      /* wait */
      wakeup_interval = prm_get_integer_value (PRM_ID_PAGE_BG_FLUSH_INTERVAL_MSECS);
      if (wakeup_interval > 0)
	{
	  if (!thread_daemon_timedwait (&thread_Page_flush_thread, wakeup_interval))
	    {
	      /* did not timeout, someone requested flush... run at least once */
	      force_one_run = true;
	    }
	}
      else
	{
	  thread_daemon_wait (&thread_Page_flush_thread);
	  /* did not timeout, someone requested flush... run at least once */
	  force_one_run = true;
	}

      /* performance tracking */
      if (perf_track.is_perf_tracking)
	{
	  /* register sleep time. */
	  PERF_UTIME_TRACKER_TIME_AND_RESTART (tsd_ptr, &perf_track, PSTAT_PB_FLUSH_SLEEP);

	  /* update is_perf_tracking */
	  perf_track.is_perf_tracking = perfmon_is_perf_tracking ();
	}
      else
	{
	  /* update is_perf_tracking and start timer if it became true */
	  PERF_UTIME_TRACKER_START (tsd_ptr, &perf_track);
	}
    }
  thread_daemon_stop (&thread_Page_flush_thread, tsd_ptr);

  return (THREAD_RET_T) 0;
}

/*
 * thread_wakeup_page_flush_thread() - wakeup page flush no matter what
 */
void
thread_wakeup_page_flush_thread (void)
{
  thread_daemon_wakeup (&thread_Page_flush_thread);
}

/*
 * thread_try_wakeup_page_flush_thread () - wakeup page flush thread by trying to lock it
 */
void
thread_try_wakeup_page_flush_thread (void)
{
  thread_daemon_try_wakeup (&thread_Page_flush_thread);
}

/*
 * thread_is_page_flush_thread_available() -
 *   return:
 */
bool
thread_is_page_flush_thread_available (void)
{
  int rv;
  bool is_available;

  rv = pthread_mutex_lock (&thread_Page_flush_thread.lock);
  is_available = thread_Page_flush_thread.is_available;
  pthread_mutex_unlock (&thread_Page_flush_thread.lock);

  return is_available;
}

/*
 * thread_page_buffer_maintenance_thread () - page buffer maintenance thread loop. wakes up regularly and adjust private
 *                                            lists quota's.
 *
 * return     : THREAD_RET_T
 * arg_p (in) : thread entry
 */
static THREAD_RET_T THREAD_CALLING_CONVENTION
thread_page_buffer_maintenance_thread (void *arg_p)
{
#define THREAD_PGBUF_MAINTENANCE_WAKEUP_MSEC 100
#if !defined(HPUX)
  THREAD_ENTRY *tsd_ptr;
#endif /* !HPUX */

  tsd_ptr = (THREAD_ENTRY *) arg_p;
  thread_daemon_start (&thread_Page_maintenance_thread, tsd_ptr, TT_DAEMON);
  while (!tsd_ptr->shutdown)
    {
      /* reset request count */
      (void) ATOMIC_TAS_32 (&thread_Page_maintenance_thread.nrequestors, 0);

      /* page buffer maintenance thread adjust quota's based on thread activity. */
      pgbuf_adjust_quotas (tsd_ptr);

      /* search lists and assign victims directly */
      pgbuf_direct_victims_maintenance (tsd_ptr);

      /* wait THREAD_PGBUF_MAINTENANCE_WAKEUP_MSEC */
      (void) thread_daemon_timedwait (&thread_Page_maintenance_thread, THREAD_PGBUF_MAINTENANCE_WAKEUP_MSEC);
    }
  thread_daemon_stop (&thread_Page_maintenance_thread, tsd_ptr);

  return (THREAD_RET_T) 0;

#undef THREAD_PGBUF_MAINTENANCE_WAKEUP_MSEC
}

/*
 * thread_wakeup_page_buffer_maintenance_thread () - wakeup page maintenance thread
 */
void
thread_wakeup_page_buffer_maintenance_thread (void)
{
  thread_daemon_wakeup_onereq (&thread_Page_maintenance_thread);
}

/*
 * thread_page_post_flush_thread () - post-flush thread. process bcb's for pages flushed by page flush thread and assign
 *                                    them as victims or mark them as flushed.
 *
 * return     : THREAD_RET_T
 * arg_p (in) : thread entry
 */
static THREAD_RET_T THREAD_CALLING_CONVENTION
thread_page_post_flush_thread (void *arg_p)
{
#if !defined(HPUX)
  THREAD_ENTRY *tsd_ptr;
#endif /* !HPUX */
  int count_no_activity = 0;

  tsd_ptr = (THREAD_ENTRY *) arg_p;
  /* start */
  thread_daemon_start (&thread_Page_post_flush_thread, tsd_ptr, TT_DAEMON);
  while (!tsd_ptr->shutdown)
    {
      /* reset requesters */
      (void) ATOMIC_TAS_32 (&thread_Page_post_flush_thread.nrequestors, 0);
      /* assign flushed pages */
      if (!pgbuf_assign_flushed_pages (tsd_ptr))
	{
	  /* no activity for post-flush. escalate sleep-time to avoid spinning uselessly. */
	  switch (++count_no_activity)
	    {
	    case 1:
	      /* sleep 1 msec */
	      (void) thread_daemon_timedwait (&thread_Page_post_flush_thread, 1);
	      break;
	    case 2:
	      /* sleep 10 msec */
	      (void) thread_daemon_timedwait (&thread_Page_post_flush_thread, 10);
	      break;
	    case 3:
	      /* sleep 100 msec */
	      (void) thread_daemon_timedwait (&thread_Page_post_flush_thread, 100);
	      break;
	    default:
	      /* sleep indefinitely. if the thread is required, flush will wake it */
	      thread_daemon_wait (&thread_Page_post_flush_thread);
	      break;
	    }
	}
      else
	{
	  /* reset no activity counter and be prepared to start over */
	  count_no_activity = 0;
	}
    }
  /* make sure all remaining are handled. */
  pgbuf_assign_flushed_pages (tsd_ptr);
  /* stop */
  thread_daemon_stop (&thread_Page_post_flush_thread, tsd_ptr);

  return (THREAD_RET_T) 0;
}

/*
 * thread_wakeup_page_post_flush_thread () - wakeup post-flush thread
 */
void
thread_wakeup_page_post_flush_thread (void)
{
  thread_daemon_wakeup_onereq (&thread_Page_post_flush_thread);
}

/*
 * thread_is_page_post_flush_thread_available () - is post-flush thread available?
 *
 * return : true/false
 */
bool
thread_is_page_post_flush_thread_available (void)
{
  return thread_Page_post_flush_thread.is_available;
}

static THREAD_RET_T THREAD_CALLING_CONVENTION
thread_flush_control_thread (void *arg_p)
{
#if !defined(HPUX)
  THREAD_ENTRY *tsd_ptr;
#endif /* !HPUX */
  int rv;

  struct timespec wakeup_time = { 0, 0 };

  struct timeval begin_tv, end_tv, diff_tv;
  INT64 diff_usec;
  int wakeup_interval_in_msec = 50;	/* 1 msec */

  int token_gen = 0;
  int token_consumed = 0;

  tsd_ptr = (THREAD_ENTRY *) arg_p;

  thread_daemon_start (&thread_Flush_control_thread, tsd_ptr, TT_DAEMON);

  rv = fileio_flush_control_initialize ();
  if (rv != NO_ERROR)
    {
      goto error;
    }

  while (!tsd_ptr->shutdown)
    {
      INT64 tmp_usec;

      (void) gettimeofday (&begin_tv, NULL);
      er_clear ();

      wakeup_time.tv_sec = begin_tv.tv_sec + (wakeup_interval_in_msec / 1000LL);
      tmp_usec = begin_tv.tv_usec + (wakeup_interval_in_msec % 1000LL) * 1000LL;
      if (tmp_usec >= 1000000)
	{
	  wakeup_time.tv_sec += 1;
	  tmp_usec -= 1000000;
	}
      wakeup_time.tv_nsec = ((int) tmp_usec) * 1000;

      rv = pthread_mutex_lock (&thread_Flush_control_thread.lock);
      thread_Flush_control_thread.is_running = false;

      pthread_cond_timedwait (&thread_Flush_control_thread.cond, &thread_Flush_control_thread.lock, &wakeup_time);

      thread_Flush_control_thread.is_running = true;

      pthread_mutex_unlock (&thread_Flush_control_thread.lock);

      if (tsd_ptr->shutdown)
	{
	  break;
	}

      (void) gettimeofday (&end_tv, NULL);
      DIFF_TIMEVAL (begin_tv, end_tv, diff_tv);
      diff_usec = diff_tv.tv_sec * 1000000LL + diff_tv.tv_usec;

      /* Do it's job */
      (void) fileio_flush_control_add_tokens (tsd_ptr, diff_usec, &token_gen, &token_consumed);
    }
  rv = pthread_mutex_lock (&thread_Flush_control_thread.lock);
  thread_Flush_control_thread.is_available = false;
  thread_Flush_control_thread.is_running = false;
  pthread_mutex_unlock (&thread_Flush_control_thread.lock);

  fileio_flush_control_finalize ();
  er_final (ER_THREAD_FINAL);

error:
  tsd_ptr->status = TS_DEAD;

  thread_Flush_control_thread.is_running = false;

  return (THREAD_RET_T) 0;
}

void
thread_wakeup_flush_control_thread (void)
{
  int rv;

  rv = pthread_mutex_lock (&thread_Flush_control_thread.lock);
  if (!thread_Flush_control_thread.is_running)
    {
      pthread_cond_signal (&thread_Flush_control_thread.cond);
    }
  pthread_mutex_unlock (&thread_Flush_control_thread.lock);
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
  int rv, ret;

  struct timespec LFT_wakeup_time = { 0, 0 };
  struct timeval wakeup_time = { 0, 0 };
  struct timeval wait_time = { 0, 0 };
  struct timeval tmp_timeval = { 0, 0 };

  int working_time, remained_time, total_elapsed_time, param_refresh_remained;
  int gc_interval, wakeup_interval;
  int param_refresh_interval = 3000;
  int max_wait_time = 1000;

  LOG_GROUP_COMMIT_INFO *group_commit_info = &log_Gl.group_commit_info;

  tsd_ptr = (THREAD_ENTRY *) arg_p;

  thread_daemon_start (&thread_Log_flush_thread, tsd_ptr, TT_DAEMON);

  gettimeofday (&wakeup_time, NULL);
  total_elapsed_time = 0;
  param_refresh_remained = param_refresh_interval;

  tsd_ptr->event_stats.trace_log_flush_time = prm_get_integer_value (PRM_ID_LOG_TRACE_FLUSH_TIME_MSECS);

  while (!tsd_ptr->shutdown)
    {
      er_clear ();

      gc_interval = prm_get_integer_value (PRM_ID_LOG_GROUP_COMMIT_INTERVAL_MSECS);

      wakeup_interval = max_wait_time;
      if (gc_interval > 0)
	{
	  wakeup_interval = MIN (gc_interval, wakeup_interval);
	}

      gettimeofday (&wait_time, NULL);
      working_time = (int) timeval_diff_in_msec (&wait_time, &wakeup_time);
      total_elapsed_time += working_time;

      remained_time = MAX ((int) (wakeup_interval - working_time), 0);
      (void) timeval_add_msec (&tmp_timeval, &wait_time, remained_time);
      (void) timeval_to_timespec (&LFT_wakeup_time, &tmp_timeval);

      rv = pthread_mutex_lock (&thread_Log_flush_thread.lock);

      ret = 0;
      if (thread_Log_flush_thread.nrequestors == 0 || gc_interval > 0)
	{
	  thread_Log_flush_thread.is_running = false;
	  ret = pthread_cond_timedwait (&thread_Log_flush_thread.cond, &thread_Log_flush_thread.lock, &LFT_wakeup_time);
	  thread_Log_flush_thread.is_running = true;
	}

      rv = pthread_mutex_unlock (&thread_Log_flush_thread.lock);

      gettimeofday (&wakeup_time, NULL);
      total_elapsed_time += (int) timeval_diff_in_msec (&wakeup_time, &wait_time);

      if (tsd_ptr->shutdown)
	{
	  break;
	}

      if (ret == ETIMEDOUT)
	{
	  if (total_elapsed_time < gc_interval)
	    {
	      continue;
	    }
	}

      /* to prevent performance degradation */
      param_refresh_remained -= total_elapsed_time;
      if (param_refresh_remained < 0)
	{
	  tsd_ptr->event_stats.trace_log_flush_time = prm_get_integer_value (PRM_ID_LOG_TRACE_FLUSH_TIME_MSECS);

	  param_refresh_remained = param_refresh_interval;
	}

      LOG_CS_ENTER (tsd_ptr);
      logpb_flush_pages_direct (tsd_ptr);
      LOG_CS_EXIT (tsd_ptr);

      log_Stat.gc_flush_count++;
      total_elapsed_time = 0;

      rv = pthread_mutex_lock (&group_commit_info->gc_mutex);
      pthread_cond_broadcast (&group_commit_info->gc_cond);
      (void) ATOMIC_TAS_32 (&thread_Log_flush_thread.nrequestors, 0);
      pthread_mutex_unlock (&group_commit_info->gc_mutex);

#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE, "css_log_flush_thread: [%d]send signal - waiters\n", (int) THREAD_ID ());
#endif /* CUBRID_DEBUG */
    }

  rv = pthread_mutex_lock (&thread_Log_flush_thread.lock);
  thread_Log_flush_thread.is_available = false;
  thread_Log_flush_thread.is_running = false;
  pthread_mutex_unlock (&thread_Log_flush_thread.lock);

  er_final (ER_THREAD_FINAL);
  tsd_ptr->status = TS_DEAD;

#if defined(CUBRID_DEBUG)
  er_log_debug (ARG_FILE_LINE, "css_log_flush_thread: [%d]end \n", (int) THREAD_ID ());
#endif /* CUBRID_DEBUG */

  return (THREAD_RET_T) 0;
}


/*
 * thread_wakeup_log_flush_thread() - wakeup log flush thread.
 */
void
thread_wakeup_log_flush_thread (void)
{
  thread_daemon_wakeup_onereq (&thread_Log_flush_thread);
}

/*
 * thread_is_log_flush_thread_available () - is log flush thread available?
 *
 * return : true/false
 */
bool
thread_is_log_flush_thread_available (void)
{
  return thread_Log_flush_thread.is_available;
}

INT64
thread_get_log_clock_msec (void)
{
  struct timeval tv;
#if defined(HAVE_ATOMIC_BUILTINS)

  if (thread_Log_clock_thread.is_available == true)
    {
      return log_Clock_msec;
    }
#endif
  gettimeofday (&tv, NULL);

  return (tv.tv_sec * 1000LL) + (tv.tv_usec / 1000LL);
}

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

#if defined(HAVE_ATOMIC_BUILTINS)
  assert (sizeof (log_Clock_msec) >= sizeof (now.tv_sec));
#endif /* HAVE_ATOMIC_BUILTINS */
  tsd_ptr = (THREAD_ENTRY *) arg_p;

  thread_daemon_start (&thread_Log_clock_thread, tsd_ptr, TT_DAEMON);

  while (!tsd_ptr->shutdown)
    {
#if defined(HAVE_ATOMIC_BUILTINS)
      INT64 clock_milli_sec;
      er_clear ();

      /* set time for every 200 ms */
      gettimeofday (&now, NULL);
      clock_milli_sec = (now.tv_sec * 1000LL) + (now.tv_usec / 1000LL);
      ATOMIC_TAS_64 (&log_Clock_msec, clock_milli_sec);
      thread_sleep (200);	/* 200 msec */
#else /* HAVE_ATOMIC_BUILTINS */
      int wakeup_interval = 1000;
      struct timespec wakeup_time;
      INT64 tmp_usec;

      er_clear ();
      gettimeofday (&now, NULL);
      wakeup_time.tv_sec = now.tv_sec + (wakeup_interval / 1000);
      tmp_usec = now.tv_usec + (wakeup_interval % 1000) * 1000;

      if (tmp_usec >= 1000000)
	{
	  wakeup_time.tv_sec += 1;
	  tmp_usec -= 1000000;
	}
      wakeup_time.tv_nsec = tmp_usec * 1000;

      rv = pthread_mutex_lock (&thread_Log_clock_thread.lock);
      thread_Log_clock_thread.is_running = false;

      do
	{
	  rv = pthread_cond_timedwait (&thread_Log_clock_thread.cond, &thread_Log_clock_thread.lock, &wakeup_time);
	}
      while (rv == 0 && tsd_ptr->shutdown == false);

      thread_Log_clock_thread.is_running = true;

      pthread_mutex_unlock (&thread_Log_clock_thread.lock);
#endif /* HAVE_ATOMIC_BUILTINS */
    }

  thread_Log_clock_thread.is_available = false;
  thread_Log_clock_thread.is_running = false;

  er_final (ER_THREAD_FINAL);
  tsd_ptr->status = TS_DEAD;

  return (THREAD_RET_T) 0;
}

/*
 * thread_auto_volume_expansion_thread() -
 *   return:
 */
static THREAD_RET_T THREAD_CALLING_CONVENTION
thread_auto_volume_expansion_thread (void *arg_p)
{
#define THREAD_AUTO_VOL_WAKEUP_TIME_SEC     60
#if !defined(HPUX)
  THREAD_ENTRY *tsd_ptr;
#endif /* !HPUX */
  int rv;

  struct timeval time_crt;
  struct timespec to = { 0, 0 };

  tsd_ptr = (THREAD_ENTRY *) arg_p;

  thread_daemon_start (&thread_Auto_volume_expansion_thread, tsd_ptr, TT_DAEMON);

  while (!tsd_ptr->shutdown)
    {
      gettimeofday (&time_crt, NULL);
      to.tv_sec = time_crt.tv_sec + THREAD_AUTO_VOL_WAKEUP_TIME_SEC;

      rv = pthread_mutex_lock (&thread_Auto_volume_expansion_thread.lock);
      thread_Auto_volume_expansion_thread.is_running = false;
      pthread_cond_timedwait (&thread_Auto_volume_expansion_thread.cond, &thread_Auto_volume_expansion_thread.lock,
			      &to);

      if (tsd_ptr->shutdown)
	{
	  pthread_mutex_unlock (&thread_Auto_volume_expansion_thread.lock);
	  break;
	}

      thread_Auto_volume_expansion_thread.is_running = true;
      pthread_mutex_unlock (&thread_Auto_volume_expansion_thread.lock);

      (void) disk_auto_expand (tsd_ptr);
    }

  er_final (ER_THREAD_FINAL);
  thread_Auto_volume_expansion_thread.is_available = false;
  tsd_ptr->status = TS_DEAD;

  return (THREAD_RET_T) 0;
#undef THREAD_AUTO_VOL_WAKEUP_TIME_SEC
}

/*
 * thread_auto_volume_expansion_thread_is_running () -
 *   return:
 */
bool
thread_auto_volume_expansion_thread_is_running (void)
{
  int rv;
  bool ret;

  rv = pthread_mutex_lock (&thread_Auto_volume_expansion_thread.lock);
  ret = thread_Auto_volume_expansion_thread.is_running;
  pthread_mutex_unlock (&thread_Auto_volume_expansion_thread.lock);

  return ret;
}

/*
 * thread_is_auto_volume_expansion_thread_available () -
 *   return:
 *
 *   NOTE: This is used in boot_add_auto_volume_extension()
 *         to tell whether the thread is working or not.
 *         When restart server, in log_recovery phase, the thread may be unavailable.
 */
bool
thread_is_auto_volume_expansion_thread_available (void)
{
  return thread_Auto_volume_expansion_thread.is_available;
}

/*
 *  thread_wakeup_auto_volume_expansion_thread() -
 *   return:
 */
void
thread_wakeup_auto_volume_expansion_thread (void)
{
  int rv;

  rv = pthread_mutex_lock (&thread_Auto_volume_expansion_thread.lock);
  if (!thread_Auto_volume_expansion_thread.is_running)
    {
      pthread_cond_signal (&thread_Auto_volume_expansion_thread.cond);
    }
  pthread_mutex_unlock (&thread_Auto_volume_expansion_thread.lock);
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * thread_wakeup_check_ha_delay_info_thread() -
 *   return:
 */
void
thread_wakeup_check_ha_delay_info_thread (void)
{
  int rv;

  rv = pthread_mutex_lock (&thread_Check_ha_delay_info_thread.lock);
  if (!thread_Check_ha_delay_info_thread.is_running)
    {
      pthread_cond_signal (&thread_Check_ha_delay_info_thread.cond);
    }
  pthread_mutex_unlock (&thread_Check_ha_delay_info_thread.lock);
}
#endif

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
  if (thread_index == 0)
    {
      return cubthread::get_main_entry ();
    }
  else if (thread_index <= thread_Manager.num_total)
    {
      return (&thread_Manager.thread_array[thread_index - 1]);
    }
  else
    {
      return &(cubthread::get_manager ()->get_all_entries ()[thread_index - thread_Manager.num_total - 1]);
    }
}

/*
 * thread_find_entry_by_tid() -
 *   return:
 *   tid(in)
 */
THREAD_ENTRY *
thread_find_entry_by_tid (pthread_t tid)
{
  for (THREAD_ENTRY * thread_p = thread_iterate (NULL); thread_p != NULL; thread_p = thread_iterate (thread_p))
    {
      if (thread_p->tid == tid)
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
  int i;

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

	  /* CSECT_ER_LOG_FILE -> X_CS -> [Y_CS] -> CSECT_ER_LOG_FILE is NOK */
	  /* X_CS -> CSECT_ER_LOG_FILE -> [Y_CS] -> CSECT_ER_LOG_FILE is NOK */
	case CSECT_ER_LOG_FILE:
	  if (meter->m_hold_buf[CSECT_ER_LOG_FILE] > 1)
	    {
	      for (i = 0; i < meter->m_hold_buf_size && i < CRITICAL_SECTION_COUNT; i++)
		{
		  if (i == cs_idx)
		    {
		      continue;	/* skip myself */
		    }
		  THREAD_RC_TRACK_METER_ASSERT (thread_p, stderr, meter, meter->m_hold_buf[i] == 0);
		}
	    }
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
 * thread_daemon_wait () - wait until woken
 *
 * return      : void
 * daemon (in) : daemon thread monitor
 */
STATIC_INLINE void
thread_daemon_wait (DAEMON_THREAD_MONITOR * daemon)
{
  (void) pthread_mutex_lock (&daemon->lock);
  daemon->is_running = false;
  (void) pthread_cond_wait (&daemon->cond, &daemon->lock);
  daemon->is_running = true;
  pthread_mutex_unlock (&daemon->lock);
}

/*
 * thread_daemon_timedwait () - wait until woken or up to given milliseconds
 *
 * return         : void
 * daemon (in)    : daemon thread monitor
 * wait_msec (in) : maximum wait time
 */
STATIC_INLINE bool
thread_daemon_timedwait (DAEMON_THREAD_MONITOR * daemon, int wait_msec)
{
  struct timeval timeval_crt;
  struct timespec timespec_wakeup;
  long usec_tmp;
  const int usec_onesec = 1000 * 1000;	/* nano-seconds in one second */
  int rv;

  gettimeofday (&timeval_crt, NULL);

  timespec_wakeup.tv_sec = timeval_crt.tv_sec + (wait_msec / 1000);
  usec_tmp = timeval_crt.tv_usec + (wait_msec * 1000);
  if (usec_tmp >= usec_onesec)
    {
      timespec_wakeup.tv_sec++;
      usec_tmp -= usec_onesec;
    }
  timespec_wakeup.tv_nsec = usec_tmp * 1000;

  (void) pthread_mutex_lock (&daemon->lock);
  daemon->is_running = false;
  rv = pthread_cond_timedwait (&daemon->cond, &daemon->lock, &timespec_wakeup);
  daemon->is_running = true;
  pthread_mutex_unlock (&daemon->lock);

  return rv == ETIMEDOUT;
}

/*
 * thread_daemon_start () - start daemon thread
 *
 * return           : void
 * daemon (in)      : daemon thread monitor
 * thread_p (in)    : thread entry
 * thread_type (in) : thread type
 */
STATIC_INLINE void
thread_daemon_start (DAEMON_THREAD_MONITOR * daemon, THREAD_ENTRY * thread_p, THREAD_TYPE thread_type)
{
  /* wait until THREAD_CREATE() finishes */
  pthread_mutex_lock (&thread_p->th_entry_lock);
  pthread_mutex_unlock (&thread_p->th_entry_lock);

  thread_set_thread_entry_info (thread_p);	/* save TSD */
  thread_p->type = thread_type;	/* daemon thread */
  thread_p->status = TS_RUN;	/* set thread stat as RUN */
  thread_p->get_error_context ().register_thread_local ();

  daemon->is_running = true;
  daemon->is_available = true;

  thread_set_current_tran_index (thread_p, LOG_SYSTEM_TRAN_INDEX);
}

/*
 * thread_daemon_stop () - stop daemon thread
 *
 * return        : void
 * daemon (in)   : daemon thread monitor
 * thread_p (in) : thread entry
 */
STATIC_INLINE void
thread_daemon_stop (DAEMON_THREAD_MONITOR * daemon, THREAD_ENTRY * thread_p)
{
  (void) pthread_mutex_lock (&daemon->lock);
  daemon->is_running = false;
  daemon->is_available = false;
  pthread_mutex_unlock (&daemon->lock);

  er_final (ER_THREAD_FINAL);
  thread_p->status = TS_DEAD;
}

/*
 * thread_daemon_wakeup () - Wakeup daemon thread.
 *
 * return      : void
 * daemon (in) : daemon thread monitor
 */
STATIC_INLINE void
thread_daemon_wakeup (DAEMON_THREAD_MONITOR * daemon)
{
  pthread_mutex_lock (&daemon->lock);
  if (!daemon->is_running)
    {
      /* signal wakeup */
      pthread_cond_signal (&daemon->cond);
    }
  pthread_mutex_unlock (&daemon->lock);
}

/*
 * thread_daemon_try_wakeup () - Wakeup daemon thread if lock is conditionally obtained
 *
 * return      : void
 * daemon (in) : daemon thread monitor
 */
STATIC_INLINE void
thread_daemon_try_wakeup (DAEMON_THREAD_MONITOR * daemon)
{
  if (pthread_mutex_trylock (&daemon->lock) != 0)
    {
      /* give up */
      return;
    }
  if (!daemon->is_running)
    {
      /* signal wakeup */
      pthread_cond_signal (&daemon->cond);
    }
  pthread_mutex_unlock (&daemon->lock);
}

/*
 * thread_daemon_wakeup_onereq () - request daemon thread wakeup if not already requested
 *
 * return      : void
 * daemon (in) : daemon thread monitor
 */
STATIC_INLINE void
thread_daemon_wakeup_onereq (DAEMON_THREAD_MONITOR * daemon)
{
  if (daemon->nrequestors > 0)
    {
      /* we register only one request per wakeup */
      return;
    }
  /* increment requesters */
  ++daemon->nrequestors;
  pthread_mutex_lock (&daemon->lock);
  if (!daemon->is_running)
    {
      /* signal wakeup */
      pthread_cond_signal (&daemon->cond);
    }
  pthread_mutex_unlock (&daemon->lock);
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
  int index = 0;

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
