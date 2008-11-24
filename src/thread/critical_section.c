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
 * critical_section.c - critical section support
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>

#include "critical_section.h"
#include "connection_defs.h"
#include "thread_impl.h"
#include "connection_error.h"

#define CRITICAL_SECTION_COUNT	CSECT_LAST

#ifdef CSECT_STATISTICS
#if !defined(ADD_TIMEVAL)
#define ADD_TIMEVAL(total, start, end) do {	\
  total.tv_usec +=                              \
    (end.tv_usec - start.tv_usec) >= 0 ?        \
      (end.tv_usec-start.tv_usec)               \
    : (1000000 + (end.tv_usec-start.tv_usec));  \
  total.tv_sec +=                               \
    (end.tv_usec - start.tv_usec) >= 0 ?        \
      (end.tv_sec-start.tv_sec)                 \
    : (end.tv_sec-start.tv_sec-1);              \
  total.tv_sec +=                               \
    total.tv_usec/1000000;                      \
  total.tv_usec %= 1000000;                     \
} while(0)
#endif /* !ADD_TIMEVAL */
#endif /* CSECT_STATISTICS */

/* define critical section array */
CSS_CRITICAL_SECTION css_Csect_array[CRITICAL_SECTION_COUNT];

#ifdef CSECT_STATISTICS
static char *css_Csect_name[CRITICAL_SECTION_COUNT] = {
  "CSECT_ER_LOG_FILE",
  "CSECT_ER_MSG_CACHE",
  "CSECT_CONN_LIST",
  "CSECT_CONN_MAP",
  "CSECT_FILEIO_CACHE_MOUNTED",
  "CSECT_PGBUF_POOL",
  "CSECT_LOCK_OBJECT_TABLE",
  "CSECT_LOCK_PAGE_TABLE",
  "CSECT_WFG",
  "CSECT_LOG",
  "CSECT_LOCATOR_SR_CLASSNAME_TABLE",
  "CSECT_FILE_NEWFILE",
  "CSECT_QP_QUERY_TABLE",
  "CSECT_QP_QFILE_PGCNT",
  "CSECT_QP_XASL_CACHE",
  "CSECT_QP_LIST_CACHE",
  "CSECT_BOOT_SR_DBPARM",
  "CSECT_HEAP_CLASSREPR",
  "CSECT_DISK_REFRESH_GOODVOL",
  "CSECT_LDB_CACHE_VALUE",
  "CSECT_LDB_DRIVER_STATUS",
  "CSECT_LDB_DRIVER_ADD",
  "CSECT_LDB_DRIVER_UNBIND",
  "CSECT_CNV_FMT_LEXER",
  "CSECT_HEAP_CHNGUESS",
  "CSECT_SPAGE_SAVESPACE",
  "CSECT_TRAN_TABLE",
  "CSECT_CT_OID_TABLE",
  "CSECT_SCANID_BITMAP",
  "CSECT_LOG_FLUSH",
};
#endif /* CSECT_STATISTICS */

static int csect_initialize_entry (int cs_index);
static int csect_finalize_entry (int cs_index);
static int csect_enter_critical_section_internal (THREAD_ENTRY * thrd,
						  CSS_CRITICAL_SECTION *
						  cs_ptr, int wait_secs);
static int
csect_enter_critical_section_as_reader_internal (THREAD_ENTRY * thread_p,
						 CSS_CRITICAL_SECTION *
						 cs_ptr, int wait_secs);
static int csect_exit_critical_section_internal (CSS_CRITICAL_SECTION *
						 cs_ptr);

/*
 * csect_initialize_critical_section() - initialize critical section
 *   return: 0 if success, or error code
 *   cs_ptr(in): critical section
 */
int
csect_initialize_critical_section (CSS_CRITICAL_SECTION * cs_ptr)
{
  int error_code = NO_ERROR, r1;
#if !defined(WINDOWS)
  MUTEXATTR_T mattr;

  error_code = MUTEXATTR_INIT (mattr);
  CSS_CHECK_RETURN_ERROR (error_code, ER_CSS_PTHREAD_MUTEXATTR_INIT);

#ifdef CHECK_MUTEX
  error_code = MUTEXATTR_SETTYPE (mattr, PTHREAD_MUTEX_ERRORCHECK);
  CSS_CHECK_RETURN_ERROR (error_code, ER_CSS_PTHREAD_MUTEXATTR_SETTYPE);
#endif /* CHECK_MUTEX */
#endif /* !WINDOWS */

#if defined(CHECK_MUTEX) && !defined(WINDOWS)
  error_code = MUTEX_INIT_WITH_ATT (cs_ptr->lock, mattr);
#else /* CHECK_MUTEX && !WINDOWS */
  error_code = MUTEX_INIT (cs_ptr->lock);
#endif /* CHECK_MUTEX && !WINDOWS */

  CSS_CHECK_RETURN_ERROR (error_code, ER_CSS_PTHREAD_MUTEX_INIT);

  error_code = COND_INIT (cs_ptr->readers_ok);
  CSS_CHECK_RETURN_ERROR (error_code, ER_CSS_PTHREAD_COND_INIT);
  error_code = COND_INIT (cs_ptr->writer_ok);
  CSS_CHECK_RETURN_ERROR (error_code, ER_CSS_PTHREAD_COND_INIT);

  cs_ptr->rwlock = 0;
  cs_ptr->owner = NULL_THREAD_T;
  cs_ptr->tran_index = -1;
  cs_ptr->waiting_writers = 0;

#ifdef CSECT_STATISTICS
  cs_ptr->total_enter = 0;
  cs_ptr->total_nwaits = 0;

  cs_ptr->mutex_wait.tv_sec = 0;
  cs_ptr->mutex_wait.tv_usec = 0;
  cs_ptr->total_wait.tv_sec = 0;
  cs_ptr->total_wait.tv_usec = 0;
#endif /* CSECT_STATISTICS */

#if !defined(WINDOWS)
  r1 = MUTEXATTR_DESTROY (mattr);
  CSS_CHECK_RETURN_ERROR (r1, ER_CSS_PTHREAD_MUTEXATTR_DESTROY);
#endif /* !WINDOWS */

  return NO_ERROR;
}

/*
 * csect_initialize_entry() - initialize critical section entry
 *   return: 0 if success, or error code
 *   cs_index(in): critical section entry index
 */
static int
csect_initialize_entry (int cs_index)
{
  CSS_CRITICAL_SECTION *cs_ptr;
  int error_code = NO_ERROR;

  if (0 <= cs_index && cs_index < CRITICAL_SECTION_COUNT)
    {
      cs_ptr = &css_Csect_array[cs_index];
      error_code = csect_initialize_critical_section (cs_ptr);
    }
  else
    {
      error_code = ER_CS_INVALID_INDEX;
    }

  return error_code;
}

/*
 * csect_finalize_critical_section() - free critical section
 *   return: 0 if success, or error code
 *   cs_ptr(in): critical section
 */
int
csect_finalize_critical_section (CSS_CRITICAL_SECTION * cs_ptr)
{
  int error_code = NO_ERROR;

#ifdef CSECT_STATISTICS
  fprintf (stderr, "%23s |%10d |%10d | %6d.%06d | %6d.%06d\n",
	   css_Csect_name[cs_index], cs_ptr->total_enter,
	   cs_ptr->total_nwaits, cs_ptr->mutex_wait.tv_sec,
	   cs_ptr->mutex_wait.tv_usec, cs_ptr->total_wait.tv_sec,
	   cs_ptr->total_wait.tv_usec);
#endif /* CSECT_STATISTICS */

  error_code = MUTEX_DESTROY (cs_ptr->lock);
  CSS_CHECK_RETURN_ERROR (error_code, ER_CSS_PTHREAD_MUTEX_DESTROY);

  error_code = COND_DESTROY (cs_ptr->readers_ok);
  CSS_CHECK_RETURN_ERROR (error_code, ER_CSS_PTHREAD_COND_DESTROY);
  error_code = COND_DESTROY (cs_ptr->writer_ok);
  CSS_CHECK_RETURN_ERROR (error_code, ER_CSS_PTHREAD_COND_DESTROY);

  cs_ptr->rwlock = 0;
  cs_ptr->owner = NULL_THREAD_T;
  cs_ptr->tran_index = -1;
  cs_ptr->waiting_writers = 0;

#ifdef CSECT_STATISTICS
  cs_ptr->total_enter = 0;
  cs_ptr->total_nwaits = 0;

  cs_ptr->mutex_wait.tv_sec = 0;
  cs_ptr->mutex_wait.tv_usec = 0;
  cs_ptr->total_wait.tv_sec = 0;
  cs_ptr->total_wait.tv_usec = 0;
#endif /* CSECT_STATISTICS */

  return NO_ERROR;
}

/*
 * csect_finalize_entry() - free critical section entry
 *   return: 0 if success, or error code
 *   cs_index(in): critical section entry index
 */
static int
csect_finalize_entry (int cs_index)
{
  CSS_CRITICAL_SECTION *cs_ptr;
  int error_code = NO_ERROR;

  if (0 <= cs_index && cs_index < CRITICAL_SECTION_COUNT)
    {
      cs_ptr = &css_Csect_array[cs_index];
      error_code = csect_finalize_critical_section (cs_ptr);
    }
  else
    {
      error_code = ER_CS_INVALID_INDEX;
    }

  return error_code;
}

/*
 * csect_initialize() - initialize all the critical section lock structures
 *   return: 0 if success, or error code
 */
int
csect_initialize (void)
{
  int i, error_code = NO_ERROR;

  for (i = 0; i < CRITICAL_SECTION_COUNT; i++)
    {
      error_code = csect_initialize_entry (i);
      if (error_code != NO_ERROR)
	{
	  break;
	}
    }

  return error_code;
}

/*
 * csect_finalize() - free all the critical section lock structures
 *   return: 0 if success, or error code
 */
int
csect_finalize (void)
{
  int i, error_code = NO_ERROR;

#ifdef CSECT_STATISTICS
  fprintf (stderr,
	   "             CS Name    |Total Enter|Total Wait |mutex lock time| Time to enter\n");
#endif /* CSECT_STATISTICS */

  for (i = 0; i < CRITICAL_SECTION_COUNT; i++)
    {
      error_code = csect_finalize_entry (i);
      if (error_code != NO_ERROR)
	{
	  break;
	}
    }

  return error_code;
}

/*
 * csect_enter_critical_section_internal() - lock critical section
 *   return: 0 if success, or error code
 *   cs_ptr(in): critical section
 *   wait_secs(in): timeout second
 */
static int
csect_enter_critical_section_internal (THREAD_ENTRY * thread_p,
				       CSS_CRITICAL_SECTION * cs_ptr,
				       int wait_secs)
{
  int error_code = NO_ERROR, r2;
#ifdef CSECT_STATISTICS
  struct timeval start_val, end_val;
#endif /* CSECT_STATISTICS */

  CSS_ASSERT (cs_ptr != NULL);

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

#ifdef CSECT_STATISTICS
  cs_ptr->total_enter++;
  gettimeofday (&start_val, NULL);
#endif /* CSECT_STATISTICS */

  MUTEX_LOCK (error_code, cs_ptr->lock);

#ifdef CSECT_STATISTICS
  gettimeofday (&end_val, NULL);
  ADD_TIMEVAL (cs_ptr->mutex_wait, start_val, end_val);
#endif /* CSECT_STATISTICS */
  CSS_CHECK_RETURN_ERROR (error_code, ER_CSS_PTHREAD_MUTEX_LOCK);

  while (cs_ptr->rwlock != 0)
    {
      if (cs_ptr->rwlock < 0 && cs_ptr->owner == thread_p->tid)
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
	      cs_ptr->waiting_writers++;
#ifdef CSECT_STATISTICS
	      cs_ptr->total_nwaits++;
#endif /* CSECT_STATISTICS */

	      error_code = COND_WAIT (cs_ptr->writer_ok, cs_ptr->lock);
#if defined(WINDOWS)
	      MUTEX_LOCK (r2, cs_ptr->lock);
	      CSS_CHECK_RETURN_ERROR (r2, ER_CSS_PTHREAD_MUTEX_LOCK);
#endif /* WINDOWS */

	      if (error_code != 0)
		{
		  r2 = MUTEX_UNLOCK (cs_ptr->lock);
		  CSS_CHECK_RETURN_ERROR (r2, ER_CSS_PTHREAD_MUTEX_UNLOCK);
		}
	      CSS_CHECK_RETURN_ERROR (error_code, ER_CSS_PTHREAD_COND_WAIT);

	      cs_ptr->waiting_writers--;
	    }
	  else if (wait_secs > 0)
	    {
#if defined(WINDOWS)
	      int to;
	      to = wait_secs * 1000;
#else /* WINDOWS */
	      struct timespec to;
	      to.tv_sec = time (NULL) + wait_secs;
	      to.tv_nsec = 0;
#endif /* WINDOWS */

	      cs_ptr->waiting_writers++;

	      error_code =
		COND_TIMEDWAIT (cs_ptr->writer_ok, cs_ptr->lock, to);
#if defined(WINDOWS)
	      MUTEX_LOCK (r2, cs_ptr->lock);
	      CSS_CHECK_RETURN_ERROR (r2, ER_CSS_PTHREAD_MUTEX_LOCK);
#endif /* WINDOWS */

	      cs_ptr->waiting_writers--;

	      if (error_code != TIMEDWAIT_GET_LK)
		{
		  r2 = MUTEX_UNLOCK (cs_ptr->lock);
		  CSS_CHECK_RETURN_ERROR (r2, ER_CSS_PTHREAD_MUTEX_UNLOCK);
		  if (error_code != TIMEDWAIT_TIMEOUT)
		    {
		      CSS_CHECK_RETURN_ERROR (error_code,
					      ER_CSS_PTHREAD_COND_WAIT);
		    }
		  return error_code;
		}
	    }
	  else
	    {
	      error_code = MUTEX_UNLOCK (cs_ptr->lock);
	      CSS_CHECK_RETURN_ERROR (error_code,
				      ER_CSS_PTHREAD_MUTEX_UNLOCK);
	      return ETIMEDOUT;
	    }
	}
    }

  /* rwlock will be < 0. It denotes that a writer owns the csect. */
  cs_ptr->rwlock--;
  /* record that I am the writer of the csect. */
  cs_ptr->owner = thread_p->tid;
  cs_ptr->tran_index = thread_p->tran_index;

#ifdef CSECT_STATISTICS
  gettimeofday (&end_val, NULL);
  ADD_TIMEVAL (cs_ptr->total_wait, start_val, end_val);
#endif /* CSECT_STATISTICS */

  error_code = MUTEX_UNLOCK (cs_ptr->lock);
  CSS_CHECK_RETURN_ERROR (error_code, ER_CSS_PTHREAD_MUTEX_UNLOCK);

  return error_code;
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
  CSS_CRITICAL_SECTION *cs_ptr;

  if (0 <= cs_index && cs_index < CRITICAL_SECTION_COUNT)
    {
      cs_ptr = &css_Csect_array[cs_index];
    }
  else
    {
      CSS_CHECK_RETURN_ERROR (ER_CS_INVALID_INDEX, ER_CS_INVALID_INDEX);
    }

  return csect_enter_critical_section_internal (thread_p, cs_ptr, wait_secs);
}

/*
 * csect_enter_critical_section_as_reader_internal () - acquire a read lock
 *   return: 0 if success, or error code
 *   cs_ptr(in): critical section
 *   wait_secs(in): timeout second
 */
static int
csect_enter_critical_section_as_reader_internal (THREAD_ENTRY * thread_p,
						 CSS_CRITICAL_SECTION *
						 cs_ptr, int wait_secs)
{
  int r = NO_ERROR, r2;
#ifdef CSECT_STATISTICS
  struct timeval start_val, end_val;
#endif /* CSECT_STATISTICS */

  CSS_ASSERT (cs_ptr != NULL);

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

#ifdef CSECT_STATISTICS
  cs_ptr->total_enter++;
  gettimeofday (&start_val, NULL);
#endif /* CSECT_STATISTICS */

  MUTEX_LOCK (r, cs_ptr->lock);

#ifdef CSECT_STATISTICS
  gettimeofday (&end_val, NULL);
  ADD_TIMEVAL (cs_ptr->mutex_wait, start_val, end_val);
#endif /* CSECT_STATISTICS */
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_LOCK);

  if (cs_ptr->rwlock < 0 && cs_ptr->owner == thread_p->tid)
    {
      /* writer reenters the csect as a reader. treat as writer. */
      cs_ptr->rwlock--;
    }
  else
    {
      while (cs_ptr->rwlock < 0 || cs_ptr->waiting_writers)
	{
	  /* reader should wait writer(s). */
	  if (wait_secs == INF_WAIT)
	    {
#ifdef CSECT_STATISTICS
	      cs_ptr->total_nwaits++;
#endif /* CSECT_STATISTICS */

	      r = COND_WAIT (cs_ptr->readers_ok, cs_ptr->lock);

#if defined(WINDOWS)
	      MUTEX_LOCK (r2, cs_ptr->lock);
	      CSS_CHECK_RETURN_ERROR (r2, ER_CSS_PTHREAD_MUTEX_LOCK);
#endif /* WINDOWS */
	      if (r != 0)
		{
		  r2 = MUTEX_UNLOCK (cs_ptr->lock);
		  CSS_CHECK_RETURN_ERROR (r2, ER_CSS_PTHREAD_MUTEX_UNLOCK);
		}
	      CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_COND_WAIT);
	    }
	  else if (wait_secs > 0)
	    {
#if defined(WINDOWS)
	      int to;
	      to = wait_secs * 1000;
#else /* WINDOWS */
	      struct timespec to;
	      to.tv_sec = time (NULL) + wait_secs;
	      to.tv_nsec = 0;
#endif /* WINDOWS */

	      cs_ptr->waiting_writers++;

	      r = COND_TIMEDWAIT (cs_ptr->readers_ok, cs_ptr->lock, to);

#if defined(WINDOWS)
	      MUTEX_LOCK (r2, cs_ptr->lock);
	      CSS_CHECK_RETURN_ERROR (r2, ER_CSS_PTHREAD_MUTEX_LOCK);
#endif /* WINDOWS */

	      if (r != TIMEDWAIT_GET_LK)
		{
		  r2 = MUTEX_UNLOCK (cs_ptr->lock);
		  CSS_CHECK_RETURN_ERROR (r2, ER_CSS_PTHREAD_MUTEX_UNLOCK);
		  if (r != TIMEDWAIT_TIMEOUT)
		    {
		      CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_COND_WAIT);
		    }
		  return r;
		}
	    }
	  else
	    {
	      r = MUTEX_UNLOCK (cs_ptr->lock);
	      CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_UNLOCK);
	      return ETIMEDOUT;
	    }
	}

      /* rwlock will be > 0. record that a reader enters the csect. */
      cs_ptr->rwlock++;
    }

#ifdef CSECT_STATISTICS
  gettimeofday (&end_val, NULL);
  ADD_TIMEVAL (cs_ptr->total_wait, start_val, end_val);
#endif /* CSECT_STATISTICS */

  r = MUTEX_UNLOCK (cs_ptr->lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_UNLOCK);

  return r;
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
  CSS_CRITICAL_SECTION *cs_ptr;

  if (0 <= cs_index && cs_index < CRITICAL_SECTION_COUNT)
    {
      cs_ptr = &css_Csect_array[cs_index];
    }
  else
    {
      CSS_CHECK_RETURN_ERROR (ER_CS_INVALID_INDEX, ER_CS_INVALID_INDEX);
    }

  return csect_enter_critical_section_as_reader_internal (thread_p, cs_ptr,
							  wait_secs);
}

/*
 * csect_exit_critical_section_internal() - unlock critical section
 *   return:  0 if success, or error code
 *   cs_ptr(in): critical section
 */
static int
csect_exit_critical_section_internal (CSS_CRITICAL_SECTION * cs_ptr)
{
  int r = NO_ERROR;
  int ww, wr;
#ifdef CSECT_STATISTICS
  struct timeval start_val, end_val;
#endif /* CSECT_STATISTICS */

  CSS_ASSERT (cs_ptr != NULL);

#ifdef CSECT_STATISTICS
  gettimeofday (&start_val, NULL);
#endif /* CSECT_STATISTICS */

  MUTEX_LOCK (r, cs_ptr->lock);

#ifdef CSECT_STATISTICS
  gettimeofday (&end_val, NULL);
  ADD_TIMEVAL (cs_ptr->mutex_wait, start_val, end_val);
#endif /* CSECT_STATISTICS */
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_LOCK);

  if (cs_ptr->rwlock < 0)
    {				/* rwlock < 0 if locked for writing */
      cs_ptr->rwlock++;
      if (cs_ptr->rwlock < 0)
	{
	  /* in the middle of an outer critical section */
	  MUTEX_UNLOCK (cs_ptr->lock);
	  return NO_ERROR;
	}
      else
	{
	  /* cs_ptr->rwlock should be 0. */
	  cs_ptr->owner = NULL_THREAD_T;
	  cs_ptr->tran_index = -1;
	}
    }
  else if (cs_ptr->rwlock > 0)
    {
      cs_ptr->rwlock--;
    }
  else
    {
      /* cs_ptr->rwlock == 0 */
      MUTEX_UNLOCK (cs_ptr->lock);

      CSS_CHECK_RETURN_ERROR (ER_CS_UNLOCKED_BEFORE, ER_CS_UNLOCKED_BEFORE);
    }

  /*
   * Keep flags that show if there are waiting readers or writers
   * so that we can wake them up outside the monitor lock.
   */
  ww = (cs_ptr->waiting_writers && cs_ptr->rwlock == 0);
  wr = (cs_ptr->waiting_writers == 0);
  MUTEX_UNLOCK (cs_ptr->lock);

  /* wakeup a waiting writer first. Otherwise wakeup all readers. */
  if (ww)
    {
      COND_SIGNAL (cs_ptr->writer_ok);
    }
  else
    {
      COND_BROADCAST (cs_ptr->readers_ok);
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
csect_exit (int cs_index)
{
  CSS_CRITICAL_SECTION *cs_ptr;

  if (0 <= cs_index && cs_index < CRITICAL_SECTION_COUNT)
    {
      cs_ptr = &css_Csect_array[cs_index];
    }
  else
    {
      CSS_CHECK_RETURN_ERROR (ER_CS_INVALID_INDEX, ER_CS_INVALID_INDEX);
    }

  return csect_exit_critical_section_internal (cs_ptr);
}

#ifdef CSECT_STATISTICS
/*
 * csect_dump_statistics() - dump critical section statistics
 *   return: void
 */
void
csect_dump_statistics (void)
{
  CSS_CRITICAL_SECTION *cs_ptr;
  int r = NO_ERROR, i;

  fprintf (stderr,
	   "             CS Name    |Total Enter|Total Wait |mutex lock time| Time to enter\n");

  for (i = 0; i < CRITICAL_SECTION_COUNT; i++)
    {
      cs_ptr = &css_Csect_array[i];
      fprintf (stderr,
	       "%23s |%10d |%10d | %6d.%06d | %6d.%06d\n",
	       css_Csect_name[i], cs_ptr->total_enter, cs_ptr->total_nwaits,
	       cs_ptr->mutex_wait.tv_sec, cs_ptr->mutex_wait.tv_usec,
	       cs_ptr->total_wait.tv_sec, cs_ptr->total_wait.tv_usec);
    }
}

/*
 * csect_dump_statistics_and_initialize() - dump critical section statistics and
 *                                 initialize it
 *   return: void
 *   fp(in): file pointer to dump
 */
void
csect_dump_statistics_and_initialize (FILE * fp)
{
  CSS_CRITICAL_SECTION *cs_ptr;
  int r = NO_ERROR, i;

  fprintf (fp,
	   "             CS Name    |Total Enter|Total Wait |mutex lock time| Time to enter \n");

  for (i = 0; i < CRITICAL_SECTION_COUNT; i++)
    {
      cs_ptr = &css_Csect_array[i];
      fprintf (fp, "%23s |%10d |%10d | %6d.%06d | %6d.%06d\n",
	       css_Csect_name[i], cs_ptr->total_enter, cs_ptr->total_nwaits,
	       cs_ptr->mutex_wait.tv_sec, cs_ptr->mutex_wait.tv_usec,
	       cs_ptr->total_wait.tv_sec, cs_ptr->total_wait.tv_usec);
      fprintf (stderr, "%23s |%10d |%10d | %6d.%06d | %6d.%06d\n",
	       css_Csect_name[i], cs_ptr->total_enter, cs_ptr->total_nwaits,
	       cs_ptr->mutex_wait.tv_sec, cs_ptr->mutex_wait.tv_usec,
	       cs_ptr->total_wait.tv_sec, cs_ptr->total_wait.tv_usec);

      cs_ptr->total_enter = 0;
      cs_ptr->total_nwaits = 0;
      cs_ptr->mutex_wait.tv_sec = 0;
      cs_ptr->mutex_wait.tv_usec = 0;

      cs_ptr->total_wait.tv_sec = 0;
      cs_ptr->total_wait.tv_usec = 0;
    }
}
#endif /* CSECT_STATISTICS */

/*
 * csect_enter_critical_section() - lock critical section without name
 *   return: 0 if success, or error code
 *   cs_ptr(in): critical section to lock
 *   wait_secs(in): timeout section
 */
int
csect_enter_critical_section (THREAD_ENTRY * thread_p,
			      CSS_CRITICAL_SECTION * cs_ptr, int wait_secs)
{
  return csect_enter_critical_section_internal (thread_p, cs_ptr, wait_secs);
}

/*
 * csect_enter_critical_section_as_reader() - acquire read lock without criticak section name
 *   return: 0 if success, or error code
 *   cs_ptr(in): critical section to lock
 *   wait_secs(in): timeout section
 */
int
csect_enter_critical_section_as_reader (THREAD_ENTRY * thread_p,
					CSS_CRITICAL_SECTION * cs_ptr,
					int wait_secs)
{
  return csect_enter_critical_section_as_reader_internal (thread_p, cs_ptr,
							  wait_secs);
}

/*
 * csect_exit_critical_section() - unlock critical section without name
 *   return: 0 if success, or error code
 *   cs_ptr(in): critical section to unlock
 */
int
csect_exit_critical_section (CSS_CRITICAL_SECTION * cs_ptr)
{
  return csect_exit_critical_section_internal (cs_ptr);
}

#ifdef LOG_DEBUG
/*
 * csect_check_own() - check if current thread is critical section owner
 *   return: true if cs's owner is me, false if not
 *   cs_index(in): css_Csect_array's index
 */
bool
csect_check_own (int cs_index)
{
  CSS_CRITICAL_SECTION *cs_ptr;
  THREAD_ENTRY *thrd;
  bool is_owner;
  int r = NO_ERROR, r2;

  thrd = thread_get_thread_entry_info ();
  CSS_ASSERT (thrd != NULL);

  if (0 <= cs_index && cs_index < CRITICAL_SECTION_COUNT)
    {
      cs_ptr = &css_Csect_array[cs_index];
    }
  else
    {
      CSS_CHECK_RETURN_ERROR (ER_CS_INVALID_INDEX, ER_CS_INVALID_INDEX);
    }

  MUTEX_LOCK (r, cs_ptr->lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_LOCK);

  is_owner = false;

  if (cs_ptr->rwlock < 0 && cs_ptr->owner == thrd->tid)
    {
      is_owner = true;
    }

  r = MUTEX_UNLOCK (cs_ptr->lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_UNLOCK);

  return is_owner;
}
#endif /* LOG_DEBUG */
