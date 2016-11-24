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
 * critical_section.c - critical section support
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <assert.h>
#if !defined(WINDOWS)
#include <sys/time.h>
#endif /* !WINDOWS */

#include "porting.h"
#include "critical_section.h"
#include "connection_defs.h"
#include "thread.h"
#include "connection_error.h"
#include "perf_monitor.h"
#include "system_parameter.h"
#include "tsc_timer.h"
#include "show_scan.h"
#include "numeric_opfunc.h"

#undef csect_initialize_critical_section
#undef csect_finalize_critical_section
#undef csect_enter
#undef csect_enter_as_reader
#undef csect_exit
#undef csect_enter_critical_section
#undef csect_exit_critical_section

#define TOTAL_AND_MAX_TIMEVAL(total, max, elapsed) \
  do \
    { \
      (total).tv_sec += elapsed.tv_sec; \
      (total).tv_usec += elapsed.tv_usec; \
      (total).tv_sec += (total).tv_usec / 1000000; \
      (total).tv_usec %= 1000000; \
      if (((max).tv_sec < elapsed.tv_sec) || ((max).tv_sec == elapsed.tv_sec && (max).tv_usec < elapsed.tv_usec)) \
        { \
          (max).tv_sec = elapsed.tv_sec; \
          (max).tv_usec = elapsed.tv_usec; \
        } \
    } \
  while (0)

/* define critical section array */
SYNC_CRITICAL_SECTION csectgl_Critical_sections[CRITICAL_SECTION_COUNT];

static const char *csect_Names[] = {
  "ER_LOG_FILE",
  "ER_MSG_CACHE",
  "WFG",
  "LOG",
  "LOCATOR_CLASSNAME_TABLE",
  "QPROC_QUERY_TABLE",
  "QPROC_LIST_CACHE",
  "DISK_CHECK",
  "CNV_FMT_LEXER",
  "HEAP_CHNGUESS",
  "TRAN_TABLE",
  "CT_OID_TABLE",
  "HA_SERVER_STATE",
  "COMPACTDB_ONE_INSTANCE",
  "ACL",
  "PARTITION_CACHE",
  "EVENT_LOG_FILE",
  "TEMPFILE_CACHE",
  "LOG_ARCHIVE",
  "ACCESS_STATUS"
};

#define CSECT_NAME(c) ((c)->name ? (c)->name : "UNKNOWN")

/* 
 * Synchronization Primitives Statistics Monitor
 */

#define NUM_ENTRIES_OF_SYNC_STATS_BLOCK 256

typedef struct sync_stats_chunk SYNC_STATS_CHUNK;
struct sync_stats_chunk
{
  SYNC_STATS block[NUM_ENTRIES_OF_SYNC_STATS_BLOCK];
  SYNC_STATS_CHUNK *next;
  int hint_free_entry_idx;
  int num_entry_in_use;
};

SYNC_STATS_CHUNK sync_Stats;
pthread_mutex_t sync_Stats_lock;

static int csect_wait_on_writer_queue (THREAD_ENTRY * thread_p, SYNC_CRITICAL_SECTION * csect, int timeout,
				       struct timespec *to);
static int csect_wait_on_promoter_queue (THREAD_ENTRY * thread_p, SYNC_CRITICAL_SECTION * csect, int timeout,
					 struct timespec *to);
static int csect_wakeup_waiting_writer (SYNC_CRITICAL_SECTION * csect);
static int csect_wakeup_waiting_promoter (SYNC_CRITICAL_SECTION * csect);
static int csect_demote_critical_section (THREAD_ENTRY * thread_p, SYNC_CRITICAL_SECTION * csect, int wait_secs);
static int csect_promote_critical_section (THREAD_ENTRY * thread_p, SYNC_CRITICAL_SECTION * csect, int wait_secs);
static int csect_check_own_critical_section (THREAD_ENTRY * thread_p, SYNC_CRITICAL_SECTION * csect);

static SYNC_STATS_CHUNK *sync_allocate_sync_stats_chunk (void);
static int sync_initialize_sync_stats_chunk (SYNC_STATS_CHUNK * sync_stats_chunk);
static SYNC_STATS *sync_consume_sync_stats_from_pool (SYNC_STATS_CHUNK * sync_stats_chunk, int idx,
						      SYNC_PRIMITIVE_TYPE sync_prim_type, const char *name);
static int sync_return_sync_stats_to_pool (SYNC_STATS_CHUNK * sync_stats_chunk, int idx);
static SYNC_STATS *sync_allocate_sync_stats (SYNC_PRIMITIVE_TYPE sync_prim_type, const char *name);
static int sync_deallocate_sync_stats (SYNC_STATS * stats);
static void sync_reset_stats_metrics (SYNC_STATS * stats);

/*
 * csect_initialize_critical_section() - initialize critical section
 *   return: 0 if success, or error code
 *   csect(in): critical section
 */
int
csect_initialize_critical_section (SYNC_CRITICAL_SECTION * csect, const char *name)
{
  int error_code = NO_ERROR;

  assert (csect != NULL);

  csect->cs_index = -1;

  error_code = pthread_mutex_init (&csect->lock, NULL);

  if (error_code != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_INIT, 0);
      assert (0);
      return ER_CSS_PTHREAD_MUTEX_INIT;
    }

  error_code = pthread_cond_init (&csect->readers_ok, NULL);
  if (error_code != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_INIT, 0);
      assert (0);
      return ER_CSS_PTHREAD_COND_INIT;
    }

  csect->name = name;
  csect->rwlock = 0;
  csect->owner = ((pthread_t) 0);
  csect->tran_index = -1;
  csect->waiting_readers = 0;
  csect->waiting_writers = 0;
  csect->waiting_writers_queue = NULL;
  csect->waiting_promoters_queue = NULL;

  csect->stats = sync_allocate_sync_stats (SYNC_TYPE_CSECT, name);
  if (csect->stats == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }

  return NO_ERROR;
}

/*
 * csect_finalize_critical_section() - free critical section
 *   return: 0 if success, or error code
 *   csect(in): critical section
 */
int
csect_finalize_critical_section (SYNC_CRITICAL_SECTION * csect)
{
  int error_code = NO_ERROR;

  error_code = pthread_mutex_destroy (&csect->lock);
  if (error_code != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_DESTROY, 0);
      assert (0);
      return ER_CSS_PTHREAD_MUTEX_DESTROY;
    }

  error_code = pthread_cond_destroy (&csect->readers_ok);
  if (error_code != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_DESTROY, 0);
      assert (0);
      return ER_CSS_PTHREAD_COND_DESTROY;
    }

  csect->name = NULL;
  csect->rwlock = 0;
  csect->owner = ((pthread_t) 0);
  csect->tran_index = -1;
  csect->waiting_readers = 0;
  csect->waiting_writers = 0;
  csect->waiting_writers_queue = NULL;
  csect->waiting_promoters_queue = NULL;

  error_code = sync_deallocate_sync_stats (csect->stats);
  csect->stats = NULL;

  return NO_ERROR;
}

/*
 * csect_initialize_static_critical_sections() - initialize all the critical section lock structures
 *   return: 0 if success, or error code
 */
int
csect_initialize_static_critical_sections (void)
{
  SYNC_CRITICAL_SECTION *csect;
  int i, error_code = NO_ERROR;

  for (i = 0; i < CRITICAL_SECTION_COUNT; i++)
    {
      csect = &csectgl_Critical_sections[i];

      error_code = csect_initialize_critical_section (csect, csect_Names[i]);
      if (error_code != NO_ERROR)
	{
	  break;
	}

      csect->cs_index = i;
    }

  return error_code;
}

/*
 * csect_finalize_static_critical_sections() - free all the critical section lock structures
 *   return: 0 if success, or error code
 */
int
csect_finalize_static_critical_sections (void)
{
  SYNC_CRITICAL_SECTION *csect;
  int i, error_code = NO_ERROR;

  for (i = 0; i < CRITICAL_SECTION_COUNT; i++)
    {
      csect = &csectgl_Critical_sections[i];
      assert (csect->cs_index == i);

      error_code = csect_finalize_critical_section (csect);
      if (error_code != NO_ERROR)
	{
	  break;
	}

      csect->cs_index = -1;
    }

  return error_code;
}

static int
csect_wait_on_writer_queue (THREAD_ENTRY * thread_p, SYNC_CRITICAL_SECTION * csect, int timeout, struct timespec *to)
{
  THREAD_ENTRY *prev_thread_p = NULL;
  int err = NO_ERROR;

  thread_p->next_wait_thrd = NULL;

  if (csect->waiting_writers_queue == NULL)
    {
      /* nobody is waiting. */
      csect->waiting_writers_queue = thread_p;
    }
  else
    {
      /* waits on the rear of the queue */
      prev_thread_p = csect->waiting_writers_queue;
      while (prev_thread_p->next_wait_thrd != NULL)
	{
	  assert (prev_thread_p != thread_p);

	  prev_thread_p = prev_thread_p->next_wait_thrd;
	}

      assert (prev_thread_p != thread_p);
      prev_thread_p->next_wait_thrd = thread_p;
    }

  while (true)
    {
      err = thread_suspend_with_other_mutex (thread_p, &csect->lock, timeout, to, THREAD_CSECT_WRITER_SUSPENDED);

      if (DOES_THREAD_RESUME_DUE_TO_SHUTDOWN (thread_p))
	{
	  /* check if i'm in the queue */
	  prev_thread_p = csect->waiting_writers_queue;
	  while (prev_thread_p != NULL)
	    {
	      if (prev_thread_p == thread_p)
		{
		  break;
		}
	      prev_thread_p = prev_thread_p->next_wait_thrd;
	    }

	  if (prev_thread_p != NULL)
	    {
	      continue;
	    }

	  /* In case I wake up by shutdown thread and there's not me in the writers Q, I proceed anyway assuming that
	   * followings occurred in order. 1. Critical section holder wakes me up after removing me from the writers
	   * Q.(mutex lock is not released yet). 2. I wake up and then wait for the mutex to be released. 3. The
	   * shutdown thread wakes me up by a server shutdown command.  (resume_status is changed to
	   * THREAD_RESUME_DUE_TO_INTERRUPT) 4. Critical section holder releases the mutex lock. 5. I wake up with
	   * holding the mutex. Currently, resume_status of mine is THREAD_RESUME_DUE_TO_INTERRUPT and there's not me
	   * in the writers Q. */
	}
      else if (thread_p->resume_status != THREAD_CSECT_WRITER_RESUMED)
	{
	  assert (0);
	}

      break;
    }

  return err;
}

static int
csect_wait_on_promoter_queue (THREAD_ENTRY * thread_p, SYNC_CRITICAL_SECTION * csect, int timeout, struct timespec *to)
{
  THREAD_ENTRY *prev_thread_p = NULL;
  int err = NO_ERROR;

  thread_p->next_wait_thrd = NULL;

  if (csect->waiting_promoters_queue == NULL)
    {
      /* nobody is waiting. */
      csect->waiting_promoters_queue = thread_p;
    }
  else
    {
      /* waits on the rear of the queue */
      prev_thread_p = csect->waiting_promoters_queue;
      while (prev_thread_p->next_wait_thrd != NULL)
	{
	  assert (prev_thread_p != thread_p);

	  prev_thread_p = prev_thread_p->next_wait_thrd;
	}

      assert (prev_thread_p != thread_p);
      prev_thread_p->next_wait_thrd = thread_p;
    }

  while (1)
    {
      err = thread_suspend_with_other_mutex (thread_p, &csect->lock, timeout, to, THREAD_CSECT_PROMOTER_SUSPENDED);

      if (DOES_THREAD_RESUME_DUE_TO_SHUTDOWN (thread_p))
	{
	  /* check if i'm in the queue */
	  prev_thread_p = csect->waiting_promoters_queue;
	  while (prev_thread_p != NULL)
	    {
	      if (prev_thread_p == thread_p)
		{
		  break;
		}
	      prev_thread_p = prev_thread_p->next_wait_thrd;
	    }

	  if (prev_thread_p != NULL)
	    {
	      continue;
	    }

	  /* In case I wake up by shutdown thread and there's not me in the promoters Q, I proceed anyway assuming that 
	   * followings occurred in order. 1. Critical section holder wakes me up after removing me from the promoters 
	   * Q.(mutex lock is not released yet). 2. I wake up and then wait for the mutex to be released. 3. The
	   * shutdown thread wakes me up by a server shutdown command.  (resume_status is changed to
	   * THREAD_RESUME_DUE_TO_INTERRUPT) 4. Critical section holder releases the mutex lock. 5. I wake up with
	   * holding the mutex. Currently, resume_status of mine is THREAD_RESUME_DUE_TO_INTERRUPT and there's not me
	   * in the promoters Q. */
	}
      else if (thread_p->resume_status != THREAD_CSECT_PROMOTER_RESUMED)
	{
	  assert (0);
	}

      break;
    }

  return err;
}

static int
csect_wakeup_waiting_writer (SYNC_CRITICAL_SECTION * csect)
{
  THREAD_ENTRY *waiting_thread_p = NULL;
  int error_code = NO_ERROR;

  waiting_thread_p = csect->waiting_writers_queue;

  if (waiting_thread_p != NULL)
    {
      csect->waiting_writers_queue = waiting_thread_p->next_wait_thrd;
      waiting_thread_p->next_wait_thrd = NULL;

      error_code = thread_wakeup (waiting_thread_p, THREAD_CSECT_WRITER_RESUMED);
    }

  return error_code;
}

static int
csect_wakeup_waiting_promoter (SYNC_CRITICAL_SECTION * csect)
{
  THREAD_ENTRY *waiting_thread_p = NULL;
  int error_code = NO_ERROR;

  waiting_thread_p = csect->waiting_promoters_queue;

  if (waiting_thread_p != NULL)
    {
      csect->waiting_promoters_queue = waiting_thread_p->next_wait_thrd;
      waiting_thread_p->next_wait_thrd = NULL;

      error_code = thread_wakeup (waiting_thread_p, THREAD_CSECT_PROMOTER_RESUMED);
    }

  return error_code;
}

/*
 * csect_enter_critical_section() - lock critical section
 *   return: 0 if success, or error code
 *   csect(in): critical section
 *   wait_secs(in): timeout second
 */
int
csect_enter_critical_section (THREAD_ENTRY * thread_p, SYNC_CRITICAL_SECTION * csect, int wait_secs)
{
  int error_code = NO_ERROR, r;
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;

  TSC_TICKS wait_start_tick, wait_end_tick;
  TSCTIMEVAL wait_tv_diff;

  assert (csect != NULL);

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

#if !defined (NDEBUG)
  thread_rc_track_meter (thread_p, __FILE__, __LINE__, THREAD_TRACK_CSECT_ENTER_AS_WRITER, &(csect->cs_index), RC_CS,
			 MGR_DEF);
#endif /* NDEBUG */

  csect->stats->nenter++;

  tsc_getticks (&start_tick);

  error_code = pthread_mutex_lock (&csect->lock);
  if (error_code != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_LOCK, 0);
      assert (0);
      return ER_CSS_PTHREAD_MUTEX_LOCK;
    }

  while (csect->rwlock != 0 || csect->owner != ((pthread_t) 0))
    {
      if (csect->rwlock < 0 && csect->owner == thread_p->tid)
	{
	  /* 
	   * I am holding the csect, and reenter it again as writer.
	   * Note that rwlock will be decremented.
	   */
	  csect->stats->nreenter++;
	  break;
	}
      else
	{
	  if (wait_secs == INF_WAIT)
	    {
	      csect->waiting_writers++;
	      csect->stats->nwait++;

	      if (thread_p->event_stats.trace_slow_query == true)
		{
		  tsc_getticks (&wait_start_tick);
		}

	      error_code = csect_wait_on_writer_queue (thread_p, csect, INF_WAIT, NULL);
	      if (thread_p->event_stats.trace_slow_query == true)
		{
		  tsc_getticks (&wait_end_tick);
		  tsc_elapsed_time_usec (&wait_tv_diff, wait_end_tick, wait_start_tick);
		  TSC_ADD_TIMEVAL (thread_p->event_stats.cs_waits, wait_tv_diff);
		}

	      csect->waiting_writers--;
	      if (error_code != NO_ERROR)
		{
		  r = pthread_mutex_unlock (&csect->lock);
		  if (r != NO_ERROR)
		    {
		      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
		      assert (0);
		      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
		    }
		  assert (0);
		  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_WAIT, 0);
		  return ER_CSS_PTHREAD_COND_WAIT;
		}
	      if (csect->owner != ((pthread_t) 0) && csect->waiting_writers > 0)
		{
		  /* 
		   * There's one waiting to be promoted.
		   * Note that 'owner' was not reset while demoting.
		   * I have to yield to the waiter
		   */
		  error_code = csect_wakeup_waiting_promoter (csect);
		  if (error_code != NO_ERROR)
		    {
		      r = pthread_mutex_unlock (&csect->lock);
		      if (r != NO_ERROR)
			{
			  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
			  assert (0);
			  return ER_CSS_PTHREAD_MUTEX_UNLOCK;
			}
		      assert (0);
		      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_SIGNAL, 0);
		      return ER_CSS_PTHREAD_COND_SIGNAL;
		    }
		  continue;
		}
	    }
	  else if (wait_secs > 0)
	    {
	      struct timespec to;
	      to.tv_sec = time (NULL) + wait_secs;
	      to.tv_nsec = 0;

	      csect->waiting_writers++;

	      if (thread_p->event_stats.trace_slow_query == true)
		{
		  tsc_getticks (&wait_start_tick);
		}

	      error_code = csect_wait_on_writer_queue (thread_p, csect, NOT_WAIT, &to);
	      if (thread_p->event_stats.trace_slow_query == true)
		{
		  tsc_getticks (&wait_end_tick);
		  tsc_elapsed_time_usec (&wait_tv_diff, wait_end_tick, wait_start_tick);
		  TSC_ADD_TIMEVAL (thread_p->event_stats.cs_waits, wait_tv_diff);
		}

	      csect->waiting_writers--;
	      if (error_code != NO_ERROR)
		{
		  r = pthread_mutex_unlock (&csect->lock);
		  if (r != NO_ERROR)
		    {
		      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
		      assert (0);
		      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
		    }
		  if (error_code != ER_CSS_PTHREAD_COND_WAIT)
		    {
		      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_WAIT, 0);
		      assert (0);
		      return ER_CSS_PTHREAD_COND_WAIT;
		    }

		  return error_code;
		}
	    }
	  else
	    {
	      error_code = pthread_mutex_unlock (&csect->lock);
	      if (error_code != NO_ERROR)
		{
		  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
		  assert (0);
		  return ER_CSS_PTHREAD_MUTEX_UNLOCK;
		}
	      return ETIMEDOUT;
	    }
	}
    }

  /* rwlock will be < 0. It denotes that a writer owns the csect. */
  csect->rwlock--;

  /* record that I am the writer of the csect. */
  csect->owner = thread_p->tid;
  csect->tran_index = thread_p->tran_index;

  tsc_getticks (&end_tick);
  tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
  TOTAL_AND_MAX_TIMEVAL (csect->stats->total_elapsed, csect->stats->max_elapsed, tv_diff);

  error_code = pthread_mutex_unlock (&csect->lock);
  if (error_code != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
      assert (0);
      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
    }

  if (MONITOR_WAITING_THREAD (tv_diff))
    {
      if (csect->cs_index > 0)
	{
	  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_MNT_WAITING_THREAD, 2, CSECT_NAME (csect),
		  prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD));
	}
      er_log_debug (ARG_FILE_LINE,
		    "csect_enter_critical_section_as_reader: %6d.%06d"
		    " %s total_enter %d ntotal_elapsed %d max_elapsed %d.%06d total_elapsed %d.06d\n", tv_diff.tv_sec,
		    tv_diff.tv_usec, CSECT_NAME (csect), csect->stats->nenter, csect->stats->nwait,
		    csect->stats->max_elapsed.tv_sec, csect->stats->max_elapsed.tv_usec,
		    csect->stats->total_elapsed.tv_sec, csect->stats->total_elapsed.tv_usec);
    }

  return NO_ERROR;
}

/*
 * csect_enter() - lock out other threads from concurrent execution
 * 		through a critical section of code
 *   return: 0 if success, or error code
 *   cs_index(in): identifier of the section to lock
 *   wait_secs(in): timeout second
 *
 * Note: locks the critical section, or suspends the thread until the critical
 *       section has been freed by another thread
 */
int
csect_enter (THREAD_ENTRY * thread_p, int cs_index, int wait_secs)
{
  SYNC_CRITICAL_SECTION *csect;

  assert (cs_index >= 0);
  assert (cs_index < CRITICAL_SECTION_COUNT);

  csect = &csectgl_Critical_sections[cs_index];
#if defined (SERVER_MODE)
  assert (csect->cs_index == cs_index);
#endif

  return csect_enter_critical_section (thread_p, csect, wait_secs);
}

/*
 * csect_enter_critical_section_as_reader () - acquire a read lock
 *   return: 0 if success, or error code
 *   csect(in): critical section
 *   wait_secs(in): timeout second
 */
int
csect_enter_critical_section_as_reader (THREAD_ENTRY * thread_p, SYNC_CRITICAL_SECTION * csect, int wait_secs)
{
  int error_code = NO_ERROR, r;
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
  TSC_TICKS wait_start_tick, wait_end_tick;
  TSCTIMEVAL wait_tv_diff;

  assert (csect != NULL);

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

#if !defined (NDEBUG)
  thread_rc_track_meter (thread_p, __FILE__, __LINE__, THREAD_TRACK_CSECT_ENTER_AS_READER, &(csect->cs_index), RC_CS,
			 MGR_DEF);
#endif /* NDEBUG */

  csect->stats->nenter++;

  tsc_getticks (&start_tick);

  error_code = pthread_mutex_lock (&csect->lock);
  if (error_code != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_LOCK, 0);
      assert (0);
      return ER_CSS_PTHREAD_MUTEX_LOCK;
    }

  if (csect->rwlock < 0 && csect->owner == thread_p->tid)
    {
      /* writer reenters the csect as a reader. treat as writer. */
      csect->rwlock--;
      csect->stats->nreenter++;
    }
  else
    {
      /* reader can enter this csect without waiting writer(s) when the csect had been demoted by the other */
      while (csect->rwlock < 0 || (csect->waiting_writers > 0 && csect->owner == ((pthread_t) 0)))
	{
	  /* reader should wait writer(s). */
	  if (wait_secs == INF_WAIT)
	    {
	      csect->waiting_readers++;
	      csect->stats->nwait++;
	      thread_p->resume_status = THREAD_CSECT_READER_SUSPENDED;

	      if (thread_p->event_stats.trace_slow_query == true)
		{
		  tsc_getticks (&wait_start_tick);
		}

	      error_code = pthread_cond_wait (&csect->readers_ok, &csect->lock);
	      if (thread_p->event_stats.trace_slow_query == true)
		{
		  tsc_getticks (&wait_end_tick);
		  tsc_elapsed_time_usec (&wait_tv_diff, wait_end_tick, wait_start_tick);
		  TSC_ADD_TIMEVAL (thread_p->event_stats.cs_waits, wait_tv_diff);
		}

	      csect->waiting_readers--;

	      if (error_code != NO_ERROR)
		{
		  r = pthread_mutex_unlock (&csect->lock);
		  if (r != NO_ERROR)
		    {
		      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_LOCK, 0);
		      assert (0);
		      return ER_CSS_PTHREAD_MUTEX_LOCK;
		    }
		  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_WAIT, 0);
		  assert (0);
		  return ER_CSS_PTHREAD_COND_WAIT;
		}
	      if (thread_p->resume_status == THREAD_RESUME_DUE_TO_INTERRUPT)
		{
		  r = pthread_mutex_unlock (&csect->lock);
		  if (r != NO_ERROR)
		    {
		      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
		      assert (0);
		      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
		    }
		  continue;
		}
	    }
	  else if (wait_secs > 0)
	    {
	      struct timespec to;
	      to.tv_sec = time (NULL) + wait_secs;
	      to.tv_nsec = 0;

	      csect->waiting_readers++;
	      thread_p->resume_status = THREAD_CSECT_READER_SUSPENDED;

	      if (thread_p->event_stats.trace_slow_query == true)
		{
		  tsc_getticks (&wait_start_tick);
		}

	      error_code = pthread_cond_timedwait (&csect->readers_ok, &csect->lock, &to);
	      if (thread_p->event_stats.trace_slow_query == true)
		{
		  tsc_getticks (&wait_end_tick);
		  tsc_elapsed_time_usec (&wait_tv_diff, wait_end_tick, wait_start_tick);
		  TSC_ADD_TIMEVAL (thread_p->event_stats.cs_waits, wait_tv_diff);
		}

	      csect->waiting_readers--;

	      if (error_code != 0)
		{
		  r = pthread_mutex_unlock (&csect->lock);
		  if (r != NO_ERROR)
		    {
		      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
		      assert (0);
		      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
		    }
		  if (error_code != ETIMEDOUT)
		    {
		      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_WAIT, 0);
		      assert (0);
		      return ER_CSS_PTHREAD_COND_WAIT;
		    }
		  return error_code;
		}
	      if (thread_p->resume_status == THREAD_RESUME_DUE_TO_INTERRUPT)
		{
		  r = pthread_mutex_unlock (&csect->lock);
		  if (r != NO_ERROR)
		    {
		      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
		      assert (0);
		      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
		    }
		  continue;
		}
	    }
	  else
	    {
	      error_code = pthread_mutex_unlock (&csect->lock);
	      if (error_code != NO_ERROR)
		{
		  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
		  assert (0);
		  return ER_CSS_PTHREAD_MUTEX_UNLOCK;
		}
	      return ETIMEDOUT;
	    }
	}

      /* rwlock will be > 0. record that a reader enters the csect. */
      csect->rwlock++;
    }

  tsc_getticks (&end_tick);
  tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
  TOTAL_AND_MAX_TIMEVAL (csect->stats->total_elapsed, csect->stats->max_elapsed, tv_diff);

  error_code = pthread_mutex_unlock (&csect->lock);
  if (error_code != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
      assert (0);
      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
    }

  if (MONITOR_WAITING_THREAD (tv_diff))
    {
      if (csect->cs_index > 0)
	{
	  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_MNT_WAITING_THREAD, 2, CSECT_NAME (csect),
		  prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD));
	}
      er_log_debug (ARG_FILE_LINE,
		    "csect_enter_critical_section: %6d.%06d %s total_enter %d ntotal_elapsed %d max_elapsed %d.%06d"
		    " total_elapsed %d.06d\n", tv_diff.tv_sec, tv_diff.tv_usec, CSECT_NAME (csect),
		    csect->stats->nenter, csect->stats->nwait, csect->stats->max_elapsed.tv_sec,
		    csect->stats->max_elapsed.tv_usec, csect->stats->total_elapsed.tv_sec,
		    csect->stats->total_elapsed.tv_usec);
    }

  return NO_ERROR;
}

/*
 * csect_enter_as_reader() - acquire a read lock
 *   return: 0 if success, or error code
 *   cs_index(in): identifier of the section to lock
 *   wait_secs(in): timeout second
 *
 * Note: Multiple readers go if there are no writers.
 */
int
csect_enter_as_reader (THREAD_ENTRY * thread_p, int cs_index, int wait_secs)
{
  SYNC_CRITICAL_SECTION *csect;

  assert (cs_index >= 0);
  assert (cs_index < CRITICAL_SECTION_COUNT);

  csect = &csectgl_Critical_sections[cs_index];
#if defined (SERVER_MODE)
  assert (csect->cs_index == cs_index);
#endif

  return csect_enter_critical_section_as_reader (thread_p, csect, wait_secs);
}

/*
 * csect_demote_critical_section () - acquire a read lock when it has write lock
 *   return: 0 if success, or error code
 *   csect(in): critical section
 *   wait_secs(in): timeout second
 *
 * Note: Always successful because I have the write lock.
 */
static int
csect_demote_critical_section (THREAD_ENTRY * thread_p, SYNC_CRITICAL_SECTION * csect, int wait_secs)
{
  int error_code = NO_ERROR, r;
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
  TSC_TICKS wait_start_tick, wait_end_tick;
  TSCTIMEVAL wait_tv_diff;

  assert (csect != NULL);

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

#if !defined (NDEBUG)
  thread_rc_track_meter (thread_p, __FILE__, __LINE__, THREAD_TRACK_CSECT_DEMOTE, &(csect->cs_index), RC_CS, MGR_DEF);
#endif /* NDEBUG */

  csect->stats->nenter++;

  tsc_getticks (&start_tick);

  error_code = pthread_mutex_lock (&csect->lock);
  if (error_code != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_LOCK, 0);
      assert (0);
      return ER_CSS_PTHREAD_MUTEX_LOCK;
    }

  if (csect->rwlock < 0 && csect->owner == thread_p->tid)
    {
      /* 
       * I have write lock. I was entered before as a writer.
       * Every others are waiting on either 'reader_ok', if it is waiting as
       * a reader, or 'writers_queue' with 'waiting_writers++', if waiting as
       * a writer.
       */

      csect->rwlock++;		/* releasing */
      if (csect->rwlock < 0)
	{
	  /* 
	   * In the middle of an outer critical section, it is not possible
	   * to be a reader. Treat as same as csect_enter_critical_section_as_reader().
	   */
	  csect->rwlock--;	/* entering as a writer */
	}
      else
	{
	  /* rwlock == 0 */
	  csect->rwlock++;	/* entering as a reader */
#if 0
	  csect->owner = (pthread_t) 0;
	  csect->tran_index = -1;
#endif
	}
    }
  else
    {
      /* 
       * I don't have write lock. Act like a normal reader request.
       */
      while (csect->rwlock < 0 || csect->waiting_writers > 0)
	{
	  /* reader should wait writer(s). */
	  if (wait_secs == INF_WAIT)
	    {
	      csect->waiting_readers++;
	      csect->stats->nwait++;
	      thread_p->resume_status = THREAD_CSECT_READER_SUSPENDED;

	      if (thread_p->event_stats.trace_slow_query == true)
		{
		  tsc_getticks (&wait_start_tick);
		}

	      error_code = pthread_cond_wait (&csect->readers_ok, &csect->lock);
	      if (thread_p->event_stats.trace_slow_query == true)
		{
		  tsc_getticks (&wait_end_tick);
		  tsc_elapsed_time_usec (&wait_tv_diff, wait_end_tick, wait_start_tick);
		  TSC_ADD_TIMEVAL (thread_p->event_stats.cs_waits, wait_tv_diff);
		}

	      csect->waiting_readers--;

	      if (error_code != NO_ERROR)
		{
		  r = pthread_mutex_unlock (&csect->lock);
		  if (r != NO_ERROR)
		    {
		      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_LOCK, 0);
		      assert (0);
		      return ER_CSS_PTHREAD_MUTEX_LOCK;
		    }
		  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_WAIT, 0);
		  assert (0);
		  return ER_CSS_PTHREAD_COND_WAIT;
		}
	      if (thread_p->resume_status == THREAD_RESUME_DUE_TO_INTERRUPT)
		{
		  r = pthread_mutex_unlock (&csect->lock);
		  if (r != NO_ERROR)
		    {
		      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
		      assert (0);
		      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
		    }
		  continue;
		}
	    }
	  else if (wait_secs > 0)
	    {
	      struct timespec to;
	      to.tv_sec = time (NULL) + wait_secs;
	      to.tv_nsec = 0;

	      csect->waiting_readers++;
	      thread_p->resume_status = THREAD_CSECT_READER_SUSPENDED;

	      if (thread_p->event_stats.trace_slow_query == true)
		{
		  tsc_getticks (&wait_start_tick);
		}

	      error_code = pthread_cond_timedwait (&csect->readers_ok, &csect->lock, &to);
	      if (thread_p->event_stats.trace_slow_query == true)
		{
		  tsc_getticks (&wait_end_tick);
		  tsc_elapsed_time_usec (&wait_tv_diff, wait_end_tick, wait_start_tick);
		  TSC_ADD_TIMEVAL (thread_p->event_stats.cs_waits, wait_tv_diff);
		}

	      csect->waiting_readers--;

	      if (error_code != 0)
		{
		  r = pthread_mutex_unlock (&csect->lock);
		  if (r != NO_ERROR)
		    {
		      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
		      assert (0);
		      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
		    }
		  if (error_code != ETIMEDOUT)
		    {
		      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_WAIT, 0);
		      assert (0);
		      return ER_CSS_PTHREAD_COND_WAIT;
		    }
		  return error_code;
		}
	      if (thread_p->resume_status == THREAD_RESUME_DUE_TO_INTERRUPT)
		{
		  r = pthread_mutex_unlock (&csect->lock);
		  if (r != NO_ERROR)
		    {
		      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
		      assert (0);
		      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
		    }
		  continue;
		}
	    }
	  else
	    {
	      error_code = pthread_mutex_unlock (&csect->lock);
	      if (error_code != NO_ERROR)
		{
		  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
		  assert (0);
		  return ER_CSS_PTHREAD_MUTEX_UNLOCK;
		}
	      return ETIMEDOUT;
	    }
	}

      /* rwlock will be > 0. record that a reader enters the csect. */
      csect->rwlock++;
    }

  tsc_getticks (&end_tick);
  tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
  TOTAL_AND_MAX_TIMEVAL (csect->stats->total_elapsed, csect->stats->max_elapsed, tv_diff);

  /* Someone can wait for being reader. Wakeup all readers. */
  error_code = pthread_cond_broadcast (&csect->readers_ok);
  if (error_code != NO_ERROR)
    {
      r = pthread_mutex_unlock (&csect->lock);
      if (r != NO_ERROR)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
	  assert (0);
	  return ER_CSS_PTHREAD_MUTEX_UNLOCK;
	}

      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_BROADCAST, 0);
      assert (0);
      return ER_CSS_PTHREAD_COND_BROADCAST;
    }

  error_code = pthread_mutex_unlock (&csect->lock);
  if (error_code != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
      assert (0);
      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
    }

  if (MONITOR_WAITING_THREAD (tv_diff))
    {
      if (csect->cs_index > 0)
	{
	  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_MNT_WAITING_THREAD, 2, CSECT_NAME (csect),
		  prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD));
	}
      er_log_debug (ARG_FILE_LINE,
		    "csect_demote_critical_section: %6d.%06d %s total_enter %d ntotal_elapsed %d max_elapsed %d.%06d"
		    " total_elapsed %d.06d\n", tv_diff.tv_sec, tv_diff.tv_usec, CSECT_NAME (csect),
		    csect->stats->nenter, csect->stats->nwait, csect->stats->max_elapsed.tv_sec,
		    csect->stats->max_elapsed.tv_usec, csect->stats->total_elapsed.tv_sec,
		    csect->stats->total_elapsed.tv_usec);
    }

  return NO_ERROR;
}

/*
 * csect_demote () - acquire a read lock when it has write lock
 *   return: 0 if success, or error code
 *   cs_index(in): identifier of the section to lock
 *   wait_secs(in): timeout second
 *
 * Note: Always successful because I have the write lock.
 */
int
csect_demote (THREAD_ENTRY * thread_p, int cs_index, int wait_secs)
{
  SYNC_CRITICAL_SECTION *csect;

  assert (cs_index >= 0);
  assert (cs_index < CRITICAL_SECTION_COUNT);

  csect = &csectgl_Critical_sections[cs_index];
  return csect_demote_critical_section (thread_p, csect, wait_secs);
}

/*
 * csect_promote_critical_section () - acquire a write lock when it has read lock
 *   return: 0 if success, or error code
 *   csect(in): critical section
 *   wait_secs(in): timeout second
 *
 * Note: Always successful because I have the write lock.
 */
static int
csect_promote_critical_section (THREAD_ENTRY * thread_p, SYNC_CRITICAL_SECTION * csect, int wait_secs)
{
  int error_code = NO_ERROR, r;
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
  TSC_TICKS wait_start_tick, wait_end_tick;
  TSCTIMEVAL wait_tv_diff;

  assert (csect != NULL);

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

#if !defined (NDEBUG)
  thread_rc_track_meter (thread_p, __FILE__, __LINE__, THREAD_TRACK_CSECT_PROMOTE, &(csect->cs_index), RC_CS, MGR_DEF);
#endif /* NDEBUG */

  csect->stats->nenter++;

  tsc_getticks (&start_tick);

  error_code = pthread_mutex_lock (&csect->lock);
  if (error_code != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_LOCK, 0);
      assert (0);
      return ER_CSS_PTHREAD_MUTEX_LOCK;
    }

  if (csect->rwlock > 0)
    {
      /* 
       * I am a reader so that no writer is in this csect but reader(s) could be.
       * All writers are waiting on 'writers_queue' with 'waiting_writers++'.
       */
      csect->rwlock--;		/* releasing */
    }
  else
    {
      csect->rwlock++;		/* releasing */
      /* 
       * I don't have read lock. Act like a normal writer request.
       */
    }

  while (csect->rwlock != 0)
    {
      /* There's another readers. So I have to wait as a writer. */
      if (csect->rwlock < 0 && csect->owner == thread_p->tid)
	{
	  /* 
	   * I am holding the csect, and reenter it again as writer.
	   * Note that rwlock will be decremented.
	   */
	  break;
	}
      else
	{
	  if (wait_secs == INF_WAIT)
	    {
	      csect->waiting_writers++;
	      csect->stats->nwait++;

	      if (thread_p->event_stats.trace_slow_query == true)
		{
		  tsc_getticks (&wait_start_tick);
		}

	      error_code = csect_wait_on_promoter_queue (thread_p, csect, INF_WAIT, NULL);
	      if (thread_p->event_stats.trace_slow_query == true)
		{
		  tsc_getticks (&wait_end_tick);
		  tsc_elapsed_time_usec (&wait_tv_diff, wait_end_tick, wait_start_tick);
		  TSC_ADD_TIMEVAL (thread_p->event_stats.cs_waits, wait_tv_diff);
		}

	      csect->waiting_writers--;
	      if (error_code != NO_ERROR)
		{
		  r = pthread_mutex_unlock (&csect->lock);
		  if (r != NO_ERROR)
		    {
		      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
		      assert (0);
		      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
		    }
		  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_WAIT, 0);
		  assert (0);
		  return ER_CSS_PTHREAD_COND_WAIT;
		}
	    }
	  else if (wait_secs > 0)
	    {
	      struct timespec to;
	      to.tv_sec = time (NULL) + wait_secs;
	      to.tv_nsec = 0;

	      csect->waiting_writers++;

	      if (thread_p->event_stats.trace_slow_query == true)
		{
		  tsc_getticks (&wait_start_tick);
		}

	      error_code = csect_wait_on_promoter_queue (thread_p, csect, NOT_WAIT, &to);
	      if (thread_p->event_stats.trace_slow_query == true)
		{
		  tsc_getticks (&wait_end_tick);
		  tsc_elapsed_time_usec (&wait_tv_diff, wait_end_tick, wait_start_tick);
		  TSC_ADD_TIMEVAL (thread_p->event_stats.cs_waits, wait_tv_diff);
		}

	      csect->waiting_writers--;
	      if (error_code != NO_ERROR)
		{
		  r = pthread_mutex_unlock (&csect->lock);
		  if (r != NO_ERROR)
		    {
		      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
		      assert (0);
		      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
		    }
		  if (error_code != ER_CSS_PTHREAD_COND_WAIT && error_code != ER_CSS_PTHREAD_COND_TIMEDOUT)
		    {
		      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_WAIT, 0);
		      assert (0);
		      return ER_CSS_PTHREAD_COND_WAIT;
		    }
		  return error_code;
		}
	    }
	  else
	    {
	      error_code = pthread_mutex_unlock (&csect->lock);
	      if (error_code != NO_ERROR)
		{
		  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
		  assert (0);
		  return ER_CSS_PTHREAD_MUTEX_UNLOCK;
		}
	      return ETIMEDOUT;
	    }
	}
    }

  /* rwlock will be < 0. It denotes that a writer owns the csect. */
  csect->rwlock--;
  /* record that I am the writer of the csect. */
  csect->owner = thread_p->tid;
  csect->tran_index = thread_p->tran_index;

  tsc_getticks (&end_tick);
  tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
  TOTAL_AND_MAX_TIMEVAL (csect->stats->total_elapsed, csect->stats->max_elapsed, tv_diff);

  error_code = pthread_mutex_unlock (&csect->lock);
  if (error_code != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
      assert (0);
      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
    }

  if (MONITOR_WAITING_THREAD (tv_diff))
    {
      if (csect->cs_index > 0)
	{
	  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_MNT_WAITING_THREAD, 2, CSECT_NAME (csect),
		  prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD));
	}
      er_log_debug (ARG_FILE_LINE,
		    "csect_promote_critical_section: %6d.%06d %s total_enter %d ntotal_elapsed %d max_elapsed %d.%06d"
		    " total_elapsed %d.06d\n", tv_diff.tv_sec, tv_diff.tv_usec, CSECT_NAME (csect),
		    csect->stats->nenter, csect->stats->nwait, csect->stats->max_elapsed.tv_sec,
		    csect->stats->max_elapsed.tv_usec, csect->stats->total_elapsed.tv_sec,
		    csect->stats->total_elapsed.tv_usec);
    }

  return NO_ERROR;
}

/*
 * csect_promote () - acquire a write lock when it has read lock
 *   return: 0 if success, or error code
 *   cs_index(in): identifier of the section to lock
 *   wait_secs(in): timeout second
 *
 * Note: Always successful because I have the write lock.
 */
int
csect_promote (THREAD_ENTRY * thread_p, int cs_index, int wait_secs)
{
  SYNC_CRITICAL_SECTION *csect;

  assert (cs_index >= 0);
  assert (cs_index < CRITICAL_SECTION_COUNT);

  csect = &csectgl_Critical_sections[cs_index];
  return csect_promote_critical_section (thread_p, csect, wait_secs);
}

/*
 * csect_exit_critical_section() - unlock critical section
 *   return:  0 if success, or error code
 *   csect(in): critical section
 */
int
csect_exit_critical_section (THREAD_ENTRY * thread_p, SYNC_CRITICAL_SECTION * csect)
{
  int error_code = NO_ERROR;
  bool ww, wr, wp;

  assert (csect != NULL);

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

#if !defined (NDEBUG)
  thread_rc_track_meter (thread_p, __FILE__, __LINE__, THREAD_TRACK_CSECT_EXIT, &(csect->cs_index), RC_CS, MGR_DEF);
#endif /* NDEBUG */

  error_code = pthread_mutex_lock (&csect->lock);
  if (error_code != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_LOCK, 0);
      assert (0);
      return ER_CSS_PTHREAD_MUTEX_LOCK;
    }

  if (csect->rwlock < 0)
    {				/* rwlock < 0 if locked for writing */
      csect->rwlock++;
      if (csect->rwlock < 0)
	{
	  /* in the middle of an outer critical section */
	  error_code = pthread_mutex_unlock (&csect->lock);
	  if (error_code != NO_ERROR)
	    {
	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
	      assert (0);
	      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
	    }
	  return NO_ERROR;
	}
      else
	{
	  assert (csect->rwlock == 0);
	  csect->owner = ((pthread_t) 0);
	  csect->tran_index = -1;
	}
    }
  else if (csect->rwlock > 0)
    {
      csect->rwlock--;
    }
  else
    {
      /* csect->rwlock == 0 */
      error_code = pthread_mutex_unlock (&csect->lock);
      if (error_code != NO_ERROR)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
	  assert (0);
	  return ER_CSS_PTHREAD_MUTEX_UNLOCK;
	}

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CS_UNLOCKED_BEFORE, 0);
      assert (0);
      return ER_CS_UNLOCKED_BEFORE;
    }

  /* 
   * Keep flags that show if there are waiting readers or writers
   * so that we can wake them up outside the monitor lock.
   */
  ww = (csect->waiting_writers > 0 && csect->rwlock == 0 && csect->owner == ((pthread_t) 0));
  wp = (csect->waiting_writers > 0 && csect->rwlock == 0 && csect->owner != ((pthread_t) 0));
  wr = (csect->waiting_writers == 0);

  /* wakeup a waiting writer first. Otherwise wakeup all readers. */
  if (wp == true)
    {
      error_code = csect_wakeup_waiting_promoter (csect);
      if (error_code != NO_ERROR)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_SIGNAL, 0);
	  pthread_mutex_unlock (&csect->lock);
	  assert (0);
	  return ER_CSS_PTHREAD_COND_SIGNAL;
	}
    }
  else if (ww == true)
    {
      error_code = csect_wakeup_waiting_writer (csect);
      if (error_code != NO_ERROR)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_SIGNAL, 0);
	  pthread_mutex_unlock (&csect->lock);
	  assert (0);
	  return ER_CSS_PTHREAD_COND_SIGNAL;
	}
    }
  else
    {
      error_code = pthread_cond_broadcast (&csect->readers_ok);
      if (error_code != NO_ERROR)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_BROADCAST, 0);
	  pthread_mutex_unlock (&csect->lock);
	  assert (0);
	  return ER_CSS_PTHREAD_COND_BROADCAST;
	}
    }

  error_code = pthread_mutex_unlock (&csect->lock);
  if (error_code != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
      assert (0);
      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
    }

  return NO_ERROR;
}

/*
 * csect_exit() - free a lock that prevents other threads from
 *             concurrent execution through a critical section of code
 *   return: 0 if success, or error code
 *   cs_index(in): identifier of the section to unlock
 *
 * Note: unlocks the critical section, which may restart another thread that
 *       is suspended and waiting for the critical section.
 */
int
csect_exit (THREAD_ENTRY * thread_p, int cs_index)
{
  SYNC_CRITICAL_SECTION *csect;

  assert (cs_index >= 0);
  assert (cs_index < CRITICAL_SECTION_COUNT);

  csect = &csectgl_Critical_sections[cs_index];
#if defined (SERVER_MODE)
  assert (csect->cs_index == cs_index);
#endif

  return csect_exit_critical_section (thread_p, csect);
}

/*
 * csect_dump_statistics() - dump critical section statistics
 *   return: void
 */
void
csect_dump_statistics (FILE * fp)
{
  SYNC_CRITICAL_SECTION *csect;
  int i;

  fprintf (fp, "         CS Name        |Total Enter|Total Wait |Total Reenter|   Max elapsed |  Total elapsed\n");

  for (i = 0; i < CRITICAL_SECTION_COUNT; i++)
    {
      csect = &csectgl_Critical_sections[i];

      fprintf (fp, "%-23s |%10d |%10d |  %10d | %6ld.%06ld | %6ld.%06ld\n",
	       CSECT_NAME (csect), csect->stats->nenter, csect->stats->nreenter,
	       csect->stats->nwait, csect->stats->max_elapsed.tv_sec, csect->stats->max_elapsed.tv_usec,
	       csect->stats->total_elapsed.tv_sec, csect->stats->total_elapsed.tv_usec);

      sync_reset_stats_metrics (csect->stats);
    }

  fflush (fp);
}

/*
 * csect_check_own() - check if current thread is critical section owner
 *   return: true if cs's owner is me, false if not
 *   cs_index(in): csectgl_Critical_sections's index
 */
int
csect_check_own (THREAD_ENTRY * thread_p, int cs_index)
{
  SYNC_CRITICAL_SECTION *csect;
  int error_code = NO_ERROR;

  assert (cs_index >= 0);
  assert (cs_index < CRITICAL_SECTION_COUNT);

  csect = &csectgl_Critical_sections[cs_index];

  return csect_check_own_critical_section (thread_p, csect);
}

/*
 * csect_check_own_critical_section() - check if current thread is critical section owner
 *   return: true if cs's owner is me, false if not
 *   csect(in): critical section
 */
static int
csect_check_own_critical_section (THREAD_ENTRY * thread_p, SYNC_CRITICAL_SECTION * csect)
{
  int error_code = NO_ERROR, return_code;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  error_code = pthread_mutex_lock (&csect->lock);
  if (error_code != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_LOCK, 0);
      assert (0);
      return ER_CSS_PTHREAD_MUTEX_LOCK;
    }

  if (csect->rwlock < 0 && csect->owner == thread_p->tid)
    {
      /* has the write lock */
      return_code = 1;
    }
  else if (csect->rwlock > 0)
    {
      /* has the read lock */
      return_code = 2;
    }
  else
    {
      return_code = 0;
    }

  error_code = pthread_mutex_unlock (&csect->lock);
  if (error_code != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_LOCK, 0);
      assert (0);
      return ER_CSS_PTHREAD_MUTEX_LOCK;
    }

  return return_code;
}

/*
 * csect_start_scan () -  start scan function for
 *                        show global critical sections
 *   return: NO_ERROR, or ER_code
 *
 *   thread_p(in):
 *   show_type(in):
 *   arg_values(in):
 *   arg_cnt(in):
 *   ptr(in/out): 
 */
int
csect_start_scan (THREAD_ENTRY * thread_p, int show_type, DB_VALUE ** arg_values, int arg_cnt, void **ptr)
{
  SHOWSTMT_ARRAY_CONTEXT *ctx = NULL;
  int i, idx, error = NO_ERROR;
  DB_VALUE *vals = NULL;
  SYNC_CRITICAL_SECTION *csect;
  char buf[256] = { 0 };
  double msec;
  DB_VALUE db_val;
  DB_DATA_STATUS data_status;
  pthread_t owner_tid;
  int ival;
  THREAD_ENTRY *thread_entry = NULL;
  int num_cols = 12;

  *ptr = NULL;
  ctx = showstmt_alloc_array_context (thread_p, CRITICAL_SECTION_COUNT, num_cols);
  if (ctx == NULL)
    {
      error = er_errid ();
      goto exit_on_error;
    }

  for (i = 0; i < CRITICAL_SECTION_COUNT; i++)
    {
      idx = 0;
      vals = showstmt_alloc_tuple_in_context (thread_p, ctx);
      if (vals == NULL)
	{
	  error = er_errid ();
	  goto exit_on_error;
	}

      csect = &csectgl_Critical_sections[i];

      /* The index of the critical section */
      db_make_int (&vals[idx], csect->cs_index);
      idx++;

      /* The name of the critical section */
      db_make_string (&vals[idx], CSECT_NAME (csect));
      idx++;

      /* 'N readers', '1 writer', 'none' */
      ival = csect->rwlock;
      if (ival > 0)
	{
	  snprintf (buf, sizeof (buf), "%d readers", ival);
	}
      else if (ival < 0)
	{
	  snprintf (buf, sizeof (buf), "1 writer");
	}
      else
	{
	  snprintf (buf, sizeof (buf), "none");
	}

      error = db_make_string_copy (&vals[idx], buf);
      idx++;
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* The number of waiting readers */
      db_make_int (&vals[idx], csect->waiting_readers);
      idx++;

      /* The number of waiting writers */
      db_make_int (&vals[idx], csect->waiting_writers);
      idx++;

      /* The thread index of CS owner writer, NULL if no owner */
      owner_tid = csect->owner;
      if (owner_tid == 0)
	{
	  db_make_null (&vals[idx]);
	}
      else
	{
	  thread_entry = thread_find_entry_by_tid (owner_tid);
	  if (thread_entry != NULL)
	    {
	      db_make_bigint (&vals[idx], thread_entry->index);
	    }
	  else
	    {
	      db_make_null (&vals[idx]);
	    }
	}
      idx++;

      /* Transaction id of CS owner writer, NULL if no owner */
      ival = csect->tran_index;
      if (ival == -1)
	{
	  db_make_null (&vals[idx]);
	}
      else
	{
	  db_make_int (&vals[idx], ival);
	}
      idx++;

      /* Total count of enters */
      db_make_bigint (&vals[idx], csect->stats->nenter);
      idx++;

      /* Total count of waiters */
      db_make_bigint (&vals[idx], csect->stats->nwait);
      idx++;

      /* The thread index of waiting promoter, NULL if no waiting promoter */
      thread_entry = csect->waiting_promoters_queue;
      if (thread_entry != NULL)
	{
	  db_make_int (&vals[idx], thread_entry->index);
	}
      else
	{
	  db_make_null (&vals[idx]);
	}
      idx++;

      /* Maximum waiting time (millisecond) */
      msec = csect->stats->max_elapsed.tv_sec * 1000 + csect->stats->max_elapsed.tv_usec / 1000.0;
      db_make_double (&db_val, msec);
      db_value_domain_init (&vals[idx], DB_TYPE_NUMERIC, 10, 3);
      error = numeric_db_value_coerce_to_num (&db_val, &vals[idx], &data_status);
      idx++;
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* Total waiting time (millisecond) */
      msec = csect->stats->total_elapsed.tv_sec * 1000 + csect->stats->total_elapsed.tv_usec / 1000.0;
      db_make_double (&db_val, msec);
      db_value_domain_init (&vals[idx], DB_TYPE_NUMERIC, 10, 3);
      error = numeric_db_value_coerce_to_num (&db_val, &vals[idx], &data_status);
      idx++;
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}

      assert (idx == num_cols);
    }

  *ptr = ctx;
  return NO_ERROR;

exit_on_error:

  if (ctx != NULL)
    {
      showstmt_free_array_context (thread_p, ctx);
    }

  return error;
}


/*
 * rwlock_initialize () - initialize a rwlock 
 *   return: NO_ERROR, or ER_code
 *
 *   rwlock(in/out):
 *   name(in):
 */
int
rwlock_initialize (SYNC_RWLOCK * rwlock, const char *name)
{
  int error_code = NO_ERROR;

  assert (rwlock != NULL && name != NULL);

  rwlock->stats = NULL;

  error_code = pthread_mutex_init (&rwlock->read_lock, NULL);
  if (error_code != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_INIT, 0);
      assert (0);
      return ER_CSS_PTHREAD_MUTEX_INIT;
    }

  error_code = pthread_mutex_init (&rwlock->global_lock, NULL);
  if (error_code != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_INIT, 0);
      assert (0);
      return ER_CSS_PTHREAD_MUTEX_INIT;
    }

  rwlock->name = name;
  rwlock->num_readers = 0;

  rwlock->stats = sync_allocate_sync_stats (SYNC_TYPE_RWLOCK, rwlock->name);
  if (rwlock->stats == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }

  return error_code;
}

/*
 * rwlock_finalize () - finalize a rwlock 
 *   return: NO_ERROR, or ER_code
 *
 *   rwlock(in/out):
 */
int
rwlock_finalize (SYNC_RWLOCK * rwlock)
{
  int error_code = NO_ERROR;

  assert (rwlock != NULL && rwlock->num_readers == 0);

  rwlock->num_readers = 0;
  rwlock->name = NULL;

  error_code = pthread_mutex_destroy (&rwlock->read_lock);
  if (error_code != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_DESTROY, 0);
      assert (0);
      return ER_CSS_PTHREAD_MUTEX_DESTROY;
    }

  error_code = pthread_mutex_destroy (&rwlock->global_lock);
  if (error_code != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_DESTROY, 0);
      assert (0);
      return ER_CSS_PTHREAD_MUTEX_DESTROY;
    }

  error_code = sync_deallocate_sync_stats (rwlock->stats);
  rwlock->stats = NULL;

  return error_code;
}

/*
 * rwlock_read_lock () - acquire a read-lock of the given rwlock
 *   return: NO_ERROR, or ER_code
 *
 *   rwlock(in/out):
 */
int
rwlock_read_lock (SYNC_RWLOCK * rwlock)
{
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
  int error_code;

  assert (rwlock != NULL);

  tsc_getticks (&start_tick);

  /* hold the reader lock */
  error_code = pthread_mutex_lock (&rwlock->read_lock);
  if (error_code != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_LOCK, 0);
      assert (0);
      return ER_CSS_PTHREAD_MUTEX_LOCK;
    }

  /* increment the number of readers */
  rwlock->num_readers++;

  /* hold the global lock if it is the first reader */
  if (rwlock->num_readers == 1)
    {
      error_code = pthread_mutex_lock (&rwlock->global_lock);
      if (error_code != NO_ERROR)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_LOCK, 0);
	  assert (0);

	  (void) pthread_mutex_unlock (&rwlock->read_lock);

	  return ER_CSS_PTHREAD_MUTEX_LOCK;
	}
    }

  /* collect statistics */
  tsc_getticks (&end_tick);
  tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
  TOTAL_AND_MAX_TIMEVAL (rwlock->stats->total_elapsed, rwlock->stats->max_elapsed, tv_diff);

  rwlock->stats->nenter++;

  /* release the reader lock */
  error_code = pthread_mutex_unlock (&rwlock->read_lock);
  if (error_code != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
      assert (0);
      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
    }

  return NO_ERROR;
}

/*
 * rwlock_read_unlock () - release a read-lock of the given rwlock
 *   return: NO_ERROR, or ER_code
 *
 *   rwlock(in/out):
 */
int
rwlock_read_unlock (SYNC_RWLOCK * rwlock)
{
  int error_code;

  assert (rwlock != NULL);

  /* hold the reader lock */
  error_code = pthread_mutex_lock (&rwlock->read_lock);
  if (error_code != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_LOCK, 0);
      assert (0);
      return ER_CSS_PTHREAD_MUTEX_LOCK;
    }

  /* decrement the number of readers */
  rwlock->num_readers--;

  /* release the global lock if it is the last reader */
  if (rwlock->num_readers == 0)
    {
      error_code = pthread_mutex_unlock (&rwlock->global_lock);
      if (error_code != NO_ERROR)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
	  assert (0);

	  (void) pthread_mutex_unlock (&rwlock->read_lock);

	  return ER_CSS_PTHREAD_MUTEX_UNLOCK;
	}
    }

  /* release the reader lock */
  error_code = pthread_mutex_unlock (&rwlock->read_lock);
  if (error_code != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
      assert (0);
      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
    }

  return NO_ERROR;
}

/*
 * rwlock_write_lock () - acquire write-lock of the given rwlock
 *   return: NO_ERROR, or ER_code
 *
 *   rwlock(in/out):
 */
int
rwlock_write_lock (SYNC_RWLOCK * rwlock)
{
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
  int error_code;

  assert (rwlock != NULL);

  tsc_getticks (&start_tick);

  error_code = pthread_mutex_lock (&rwlock->global_lock);
  if (error_code != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_LOCK, 0);
      assert (0);
      return ER_CSS_PTHREAD_MUTEX_LOCK;
    }

  /* collect statistics */
  tsc_getticks (&end_tick);
  tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
  TOTAL_AND_MAX_TIMEVAL (rwlock->stats->total_elapsed, rwlock->stats->max_elapsed, tv_diff);

  rwlock->stats->nenter++;

  return NO_ERROR;
}

/*
 * rwlock_write_unlock () - release write-lock of the given rwlock
 *   return: NO_ERROR, or ER_code
 *
 *   rwlock(in/out):
 */
int
rwlock_write_unlock (SYNC_RWLOCK * rwlock)
{
  int error_code;

  assert (rwlock != NULL);

  error_code = pthread_mutex_unlock (&rwlock->global_lock);
  if (error_code != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
      assert (0);
      return ER_CSS_PTHREAD_MUTEX_UNLOCK;
    }

  return NO_ERROR;
}

/*
 * rwlock_dump_statistics() - dump rwlock statistics
 *   return: void
 */
void
rwlock_dump_statistics (FILE * fp)
{
  SYNC_STATS_CHUNK *p;
  SYNC_STATS *stats;
  int i, cnt;

  fprintf (fp, "\n         RWlock Name          |Total Enter|  Max elapsed  |  Total elapsed\n");

  pthread_mutex_lock (&sync_Stats_lock);

  p = &sync_Stats;
  while (p != NULL)
    {
      for (i = 0, cnt = 0; cnt < p->num_entry_in_use && i < NUM_ENTRIES_OF_SYNC_STATS_BLOCK; i++)
	{
	  stats = &p->block[i];
	  if (stats->type == SYNC_TYPE_RWLOCK)
	    {
	      cnt++;

	      fprintf (fp, "%-29s |%10d | %6ld.%06ld | %6ld.%06ld\n", stats->name, stats->nenter,
		       stats->max_elapsed.tv_sec, stats->max_elapsed.tv_usec,
		       stats->total_elapsed.tv_sec, stats->total_elapsed.tv_usec);

	      /* reset statistics */
	      sync_reset_stats_metrics (stats);
	    }
	}

      p = p->next;
    }

  pthread_mutex_unlock (&sync_Stats_lock);

  fflush (fp);
}

/*
 * rmutex_initialize () - initialize a reentrant mutex 
 *   return: NO_ERROR, or ER_code
 *
 *   rmutex(in/out):
 *   name(in):
 */
int
rmutex_initialize (SYNC_RMUTEX * rmutex, const char *name)
{
  int error_code = NO_ERROR;

  assert (rmutex != NULL);

  error_code = pthread_mutex_init (&rmutex->lock, NULL);
  if (error_code != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_INIT, 0);
      assert (0);
      return ER_CSS_PTHREAD_MUTEX_INIT;
    }

  rmutex->owner = (pthread_t) 0;
  rmutex->lock_cnt = 0;

  rmutex->stats = sync_allocate_sync_stats (SYNC_TYPE_RMUTEX, name);
  if (rmutex->stats == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }

  return NO_ERROR;
}

/*
 * rmutex_finalize () - finalize a rmutex 
 *   return: NO_ERROR, or ER_code
 *
 *   rmutex(in/out):
 */
int
rmutex_finalize (SYNC_RMUTEX * rmutex)
{
  int err;

  assert (rmutex != NULL);

  err = pthread_mutex_destroy (&rmutex->lock);
  if (err != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_DESTROY, 0);
      assert (0);
      return ER_CSS_PTHREAD_MUTEX_DESTROY;
    }

  err = sync_deallocate_sync_stats (rmutex->stats);
  rmutex->stats = NULL;

  return err;
}

/*
 * rmutex_lock () - acquire lock of the given rmutex. The owner is allowed to hold it again.
 *   return: NO_ERROR, or ER_code
 *
 *   thread_p(in):
 *   rmutex(in/out):
 */
int
rmutex_lock (THREAD_ENTRY * thread_p, SYNC_RMUTEX * rmutex)
{
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;

  if (!thread_is_manager_initialized ())
    {
      /* Regard the resource is available, since system is working as a single thread. */
      return NO_ERROR;
    }

  assert (rmutex != NULL);

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  if (rmutex->owner == thread_p->tid)
    {
      assert (rmutex->lock_cnt > 0);
      rmutex->lock_cnt++;

      rmutex->stats->nenter++;
      rmutex->stats->nreenter++;
    }
  else
    {
      tsc_getticks (&start_tick);

      if (pthread_mutex_lock (&rmutex->lock) != NO_ERROR)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_LOCK, 0);
	  assert (0);
	  return ER_CSS_PTHREAD_MUTEX_LOCK;
	}

      /* collect statistics */
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
      TOTAL_AND_MAX_TIMEVAL (rmutex->stats->total_elapsed, rmutex->stats->max_elapsed, tv_diff);

      rmutex->stats->nenter++;

      assert (rmutex->lock_cnt == 0);
      rmutex->lock_cnt++;

      rmutex->owner = thread_p->tid;
    }

  return NO_ERROR;
}

/*
 * rmutex_unlock () - decrement lock_cnt and release the given rmutex when lock_cnt returns to 0
 *   return: NO_ERROR, or ER_code
 *
 *   rwlock(in/out):
 */
int
rmutex_unlock (THREAD_ENTRY * thread_p, SYNC_RMUTEX * rmutex)
{
  if (!thread_is_manager_initialized ())
    {
      /* Regard the resource is available, since system is working as a single thread. */
      return NO_ERROR;
    }

  assert (rmutex != NULL && rmutex->lock_cnt > 0);

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert (rmutex->owner == thread_p->tid);

  rmutex->lock_cnt--;

  if (rmutex->lock_cnt == 0)
    {
      rmutex->owner = (pthread_t) 0;

      pthread_mutex_unlock (&rmutex->lock);
    }

  return NO_ERROR;
}

/*
 * sync_reset_stats_metrics () - reset stats metrics
 *   return: void
 *
 */
static void
sync_reset_stats_metrics (SYNC_STATS * stats)
{
  assert (stats != NULL);

  stats->total_elapsed.tv_sec = 0;
  stats->total_elapsed.tv_usec = 0;

  stats->max_elapsed.tv_sec = 0;
  stats->max_elapsed.tv_usec = 0;

  stats->nenter = 0;
  stats->nwait = 0;
  stats->nreenter = 0;
}

/*
 * sync_initialize_sync_stats () - initialize synchronization primitives stats monitor
 *   return: NO_ERROR
 *
 *   called during server startup 
 */
int
sync_initialize_sync_stats (void)
{
  int error_code = NO_ERROR;

  error_code = pthread_mutex_init (&sync_Stats_lock, NULL);
  if (error_code != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_INIT, 0);
      assert (0);
      return ER_CSS_PTHREAD_MUTEX_INIT;
    }

  return sync_initialize_sync_stats_chunk (&sync_Stats);
}

/*
 * sync_finalize_sync_stats () - finalize synchronization primitives stats monitor
 *   return: NO_ERROR
 *
 *   called during server shutdown 
 */
int
sync_finalize_sync_stats (void)
{
  SYNC_STATS_CHUNK *p, *next;

  p = &sync_Stats;

  /* the head entry will be kept. */
  for (p = p->next; p != NULL; p = next)
    {
      next = p->next;

      /* may require assertions on the chunk entry here. */
      free_and_init (p);
    }

  /* clear the head entry */
  (void) sync_initialize_sync_stats_chunk (&sync_Stats);

  if (pthread_mutex_destroy (&sync_Stats_lock) != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_DESTROY, 0);
      assert (0);
      return ER_CSS_PTHREAD_MUTEX_DESTROY;
    }

  return NO_ERROR;
}

/*
 * sync_allocate_sync_stats_chunk () - allocate a sync stats chunk
 *   return: the allocated sync stats entry or NULL
 *
 */
static SYNC_STATS_CHUNK *
sync_allocate_sync_stats_chunk (void)
{
  SYNC_STATS_CHUNK *p;

  p = (SYNC_STATS_CHUNK *) malloc (sizeof (SYNC_STATS_CHUNK));
  if (p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (SYNC_STATS_CHUNK));
      return NULL;
    }

  sync_initialize_sync_stats_chunk (p);

  return p;
}

/*
 * sync_initialize_sync_stats_chunk () - initialize a sync stats chunk
 *   return: NO_ERROR
 *
 */
static int
sync_initialize_sync_stats_chunk (SYNC_STATS_CHUNK * sync_stats_chunk)
{
  assert (sync_stats_chunk != NULL);

  memset (sync_stats_chunk->block, 0, sizeof (SYNC_STATS) * NUM_ENTRIES_OF_SYNC_STATS_BLOCK);
  sync_stats_chunk->next = NULL;
  sync_stats_chunk->hint_free_entry_idx = 0;
  sync_stats_chunk->num_entry_in_use = 0;

  return NO_ERROR;
}

/*
 * sync_consume_sync_stats_from_pool () - 
 *   return: stats buffer
 *
 */
static SYNC_STATS *
sync_consume_sync_stats_from_pool (SYNC_STATS_CHUNK * sync_stats_chunk, int idx, SYNC_PRIMITIVE_TYPE sync_prim_type,
				   const char *name)
{
  SYNC_STATS *stats;

  assert (sync_stats_chunk != NULL);
  assert (SYNC_TYPE_NONE < sync_prim_type && sync_prim_type <= SYNC_TYPE_LAST);
  assert (0 <= idx && idx < NUM_ENTRIES_OF_SYNC_STATS_BLOCK);
  assert (sync_stats_chunk->block[idx].type == SYNC_TYPE_NONE);
  assert (0 <= sync_stats_chunk->num_entry_in_use
	  && sync_stats_chunk->num_entry_in_use < NUM_ENTRIES_OF_SYNC_STATS_BLOCK);

  stats = &sync_stats_chunk->block[idx];

  stats->name = name;
  stats->type = sync_prim_type;
  sync_reset_stats_metrics (stats);

  sync_stats_chunk->num_entry_in_use++;
  sync_stats_chunk->hint_free_entry_idx = (idx + 1) % NUM_ENTRIES_OF_SYNC_STATS_BLOCK;

  return stats;
}

/*
 * sync_return_sync_stats_to_pool () - 
 *   return: NO_ERROR
 *
 */
static int
sync_return_sync_stats_to_pool (SYNC_STATS_CHUNK * sync_stats_chunk, int idx)
{
  assert (sync_stats_chunk != NULL);
  assert (0 <= idx && idx < NUM_ENTRIES_OF_SYNC_STATS_BLOCK);
  assert (SYNC_TYPE_NONE < sync_stats_chunk->block[idx].type && sync_stats_chunk->block[idx].type <= SYNC_TYPE_LAST);
  assert (0 < sync_stats_chunk->num_entry_in_use
	  && sync_stats_chunk->num_entry_in_use <= NUM_ENTRIES_OF_SYNC_STATS_BLOCK);

  sync_stats_chunk->block[idx].type = SYNC_TYPE_NONE;
  sync_stats_chunk->block[idx].name = NULL;

  sync_stats_chunk->num_entry_in_use--;
  sync_stats_chunk->hint_free_entry_idx = idx;

  return NO_ERROR;
}

/*
 * sync_allocate_sync_stats () - 
 *   return: NO_ERROR
 *
 */
static SYNC_STATS *
sync_allocate_sync_stats (SYNC_PRIMITIVE_TYPE sync_prim_type, const char *name)
{
  SYNC_STATS_CHUNK *p, *last_chunk, *new_chunk;
  SYNC_STATS *stats = NULL;
  int i, idx;

  pthread_mutex_lock (&sync_Stats_lock);

  p = &sync_Stats;
  while (p != NULL)
    {
      if (p->num_entry_in_use < NUM_ENTRIES_OF_SYNC_STATS_BLOCK)
	{
	  assert (0 <= p->hint_free_entry_idx && p->hint_free_entry_idx < NUM_ENTRIES_OF_SYNC_STATS_BLOCK);

	  for (i = 0, idx = p->hint_free_entry_idx; i < NUM_ENTRIES_OF_SYNC_STATS_BLOCK; i++)
	    {
	      if (p->block[idx].type == SYNC_TYPE_NONE)
		{
		  stats = sync_consume_sync_stats_from_pool (p, idx, sync_prim_type, name);

		  pthread_mutex_unlock (&sync_Stats_lock);
		  return stats;
		}

	      idx = (idx + 1) % NUM_ENTRIES_OF_SYNC_STATS_BLOCK;
	    }
	}

      last_chunk = p;

      p = p->next;
    }

  /* none is available. allocate a block */
  new_chunk = sync_allocate_sync_stats_chunk ();
  if (new_chunk == NULL)
    {
      /* error was set */
      pthread_mutex_unlock (&sync_Stats_lock);
      return NULL;
    }

  last_chunk->next = new_chunk;

  stats = sync_consume_sync_stats_from_pool (new_chunk, 0, sync_prim_type, name);

  pthread_mutex_unlock (&sync_Stats_lock);

  return stats;
}

/*
 * sync_deallocate_sync_stats () - 
 *   return: NO_ERROR
 *
 */
static int
sync_deallocate_sync_stats (SYNC_STATS * stats)
{
  SYNC_STATS_CHUNK *p;
  int idx;
  bool found = false;

  assert (stats != NULL);

  pthread_mutex_lock (&sync_Stats_lock);

  p = &sync_Stats;
  while (p != NULL)
    {
      if (0 < p->num_entry_in_use && p->block <= stats && stats <= p->block + NUM_ENTRIES_OF_SYNC_STATS_BLOCK)
	{
	  idx = (int) (stats - p->block);

	  assert (SYNC_TYPE_NONE < p->block[idx].type && p->block[idx].type <= SYNC_TYPE_LAST);

	  sync_return_sync_stats_to_pool (p, idx);

	  found = true;
	  break;
	}

      p = p->next;
    }

  pthread_mutex_unlock (&sync_Stats_lock);

  assert (found == true);

  return NO_ERROR;
}

/*
 * rmutex_dump_statistics() - dump rmutex statistics
 *   return: void
 */
void
rmutex_dump_statistics (FILE * fp)
{
  SYNC_STATS_CHUNK *p;
  SYNC_STATS *stats;
  int i, cnt;

  fprintf (fp, "\n         RMutex Name         |Total Enter|Total Reenter|  Max elapsed  |  Total elapsed\n");

  pthread_mutex_lock (&sync_Stats_lock);

  p = &sync_Stats;
  while (p != NULL)
    {
      for (i = 0, cnt = 0; cnt < p->num_entry_in_use && i < NUM_ENTRIES_OF_SYNC_STATS_BLOCK; i++)
	{
	  stats = &p->block[i];
	  if (stats->type == SYNC_TYPE_RMUTEX)
	    {
	      cnt++;

	      fprintf (fp, "%-28s |%10d |  %10d | %6ld.%06ld | %6ld.%06ld\n", stats->name, stats->nenter,
		       stats->nreenter, stats->max_elapsed.tv_sec, stats->max_elapsed.tv_usec,
		       stats->total_elapsed.tv_sec, stats->total_elapsed.tv_usec);

	      /* reset statistics */
	      sync_reset_stats_metrics (stats);
	    }
	}

      p = p->next;
    }

  pthread_mutex_unlock (&sync_Stats_lock);

  fflush (fp);
}

/*
 * sync_dump_statistics() - dump statistics of synchronization primitives
 *   return: void
 */
void
sync_dump_statistics (FILE * fp, SYNC_PRIMITIVE_TYPE type)
{
  if (type == SYNC_TYPE_ALL || type == SYNC_TYPE_CSECT)
    {
      csect_dump_statistics (fp);
    }

  if (type == SYNC_TYPE_ALL || type == SYNC_TYPE_RWLOCK)
    {
      rwlock_dump_statistics (fp);
    }

  if (type == SYNC_TYPE_ALL || type == SYNC_TYPE_RMUTEX)
    {
      rmutex_dump_statistics (fp);
    }
}
