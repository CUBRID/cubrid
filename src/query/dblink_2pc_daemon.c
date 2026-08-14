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
 * dblink_2pc_daemon.c - send_2pc_decision_daemon for coordinator recovery
 *
 * global_tran_queue: coordinator -> daemon (participant data for _db_global_tran insert/update).
 * Daemon: 1) recovery from _db_global_tran (state 'A'/'C'); 2) wait on queue; 3) process: persist and send decision.
 */

#ident "$Id$"

#ifdef CCI_XA

#include "dblink_2pc_daemon.h"
#include "dblink_2pc.h"
#include "dblink_global_tran_catalog.h"
#include "dblink_scan.h"
#include "error_manager.h"
#include "log_impl.h"
#include "log_manager.h"
#include "memory_alloc.h"
#ifndef SA_MODE
#include "thread_daemon.hpp"
#endif
#include "thread_entry_task.hpp"
#include "thread_looper.hpp"
#include "thread_manager.hpp"
#include "xserver_interface.h"
#include "fault_injection.h"

#include <assert.h>
#include <chrono>
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#include <pthread.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#ifdef SERVER_MODE

/* Initial and increment size for dynamic queue */
#define GLOBAL_TRAN_QUEUE_INIT_SIZE  64
#define GLOBAL_TRAN_QUEUE_GROW_SIZE  64

/* Dynamic circular queue */
static GLOBAL_TRAN_QUEUE_ENTRY *global_tran_queue = NULL;
static int global_tran_queue_size = GLOBAL_TRAN_QUEUE_INIT_SIZE;	/* allocated size */
static int global_tran_queue_head = 0;
static int global_tran_queue_tail = 0;
static int global_tran_queue_count = 0;
static pthread_mutex_t global_tran_queue_mutex = PTHREAD_MUTEX_INITIALIZER;

/* *INDENT-OFF* */
class dblink_2pc_daemon_context_manager:public cubthread::daemon_entry_manager
{
private:
  void on_daemon_retire (cubthread::entry & context) final
  {
    if (context.get_system_tdes () != NULL)
      {
	context.retire_system_worker ();
      }
  }
};

static cubthread::daemon * dblink_2pc_Daemon = NULL;
static dblink_2pc_daemon_context_manager * dblink_2pc_Daemon_context_manager = NULL;
/* *INDENT-ON* */

/*
 * Decision completion
 *
 * Lets the commit path wait until the daemon has delivered this transaction's decisions, so that
 * the remote changes are visible to the next statement of the same session.  See the contract in
 * dblink_2pc_daemon.h; the whole implementation is kept together here on purpose.
 */

/*
 * dblink_2pc_completion_create - Create a completion for num_participants decisions.
 *
 * return: the completion, or NULL if it could not be created
 *
 * Note: NULL is a usable outcome, not an error - the caller then skips the wait and the daemon
 *       delivers the decision as it does today.  Every other completion function accepts NULL.
 */
DBLINK_2PC_COMPLETION *
dblink_2pc_completion_create (int num_participants)
{
  DBLINK_2PC_COMPLETION *completion;

  assert (num_participants > 0);

  completion = (DBLINK_2PC_COMPLETION *) malloc (sizeof (DBLINK_2PC_COMPLETION));
  if (completion == NULL)
    {
      return NULL;
    }

  if (pthread_mutex_init (&completion->mutex, NULL) != 0)
    {
      free (completion);
      return NULL;
    }

  if (pthread_cond_init (&completion->cond, NULL) != 0)
    {
      (void) pthread_mutex_destroy (&completion->mutex);
      free (completion);
      return NULL;
    }

  completion->remaining = num_participants;
  completion->refcount = 1;	/* the commit path; each queue entry adds its own */

  return completion;
}

/*
 * dblink_2pc_completion_ref - Take a reference before attaching the completion to a queue entry.
 */
void
dblink_2pc_completion_ref (DBLINK_2PC_COMPLETION * completion)
{
  if (completion == NULL)
    {
      return;
    }

  pthread_mutex_lock (&completion->mutex);
  assert (completion->refcount > 0);
  completion->refcount++;
  pthread_mutex_unlock (&completion->mutex);
}

/*
 * dblink_2pc_completion_unref - Drop one reference, freeing the completion with the last one.
 */
void
dblink_2pc_completion_unref (DBLINK_2PC_COMPLETION * completion)
{
  int refcount;

  if (completion == NULL)
    {
      return;
    }

  pthread_mutex_lock (&completion->mutex);
  assert (completion->refcount > 0);
  refcount = --completion->refcount;
  pthread_mutex_unlock (&completion->mutex);

  if (refcount == 0)
    {
      (void) pthread_cond_destroy (&completion->cond);
      (void) pthread_mutex_destroy (&completion->mutex);
      free (completion);
    }
}

/*
 * dblink_2pc_completion_settle - Settle one entry and consume its reference.
 *
 * Note: called when a decision was delivered, and when an entry that took a reference never made it
 *       into the queue - so remaining is never left counting an entry that no longer exists.  It is
 *       deliberately not called on a failed delivery: that entry is re-enqueued and keeps its slot.
 *       The signal is sent while the mutex is held; the completion cannot be freed underneath us
 *       because this call still owns a reference until it releases one at the end.
 */
void
dblink_2pc_completion_settle (DBLINK_2PC_COMPLETION * completion)
{
  if (completion == NULL)
    {
      return;
    }

  pthread_mutex_lock (&completion->mutex);
  assert (completion->remaining > 0);
  if (completion->remaining > 0)
    {
      if (--completion->remaining == 0)
	{
	  pthread_cond_signal (&completion->cond);
	}
    }
  pthread_mutex_unlock (&completion->mutex);

  dblink_2pc_completion_unref (completion);
}

/*
 * dblink_2pc_completion_wait - Wait for every decision to be settled, up to timeout_msec.
 *
 * return: true if all were settled, false on timeout
 *
 * Note: false is not an error.  The decisions stay queued and the daemon keeps delivering them,
 *       which is the pre-existing asynchronous behaviour.  A timeout_msec of 0 or less yields a
 *       deadline in the past, which polls rather than blocks - that is a usable meaning, so it is
 *       documented instead of rejected.
 */
bool
dblink_2pc_completion_wait (DBLINK_2PC_COMPLETION * completion, int timeout_msec)
{
  struct timespec deadline;
  bool settled;

  if (completion == NULL)
    {
      return false;
    }

  /* pthread_cond_timedwait takes an absolute CLOCK_REALTIME deadline, so compute it once and let
   * the loop below re-check it on every wakeup - that absorbs spurious wakeups and keeps the total
   * wait at the bound however many times we wake, which recomputing a relative timeout would not.
   * The existing waits in the tree take the same approach - see broker.c. */
  clock_gettime (CLOCK_REALTIME, &deadline);
  deadline.tv_sec += timeout_msec / 1000;
  deadline.tv_nsec += (long) (timeout_msec % 1000) * 1000000L;
  if (deadline.tv_nsec >= 1000000000L)
    {
      deadline.tv_sec++;
      deadline.tv_nsec -= 1000000000L;
    }

  pthread_mutex_lock (&completion->mutex);
  while (completion->remaining > 0)
    {
      if (pthread_cond_timedwait (&completion->cond, &completion->mutex, &deadline) == ETIMEDOUT)
	{
	  break;
	}
    }
  settled = (completion->remaining == 0);
  pthread_mutex_unlock (&completion->mutex);

  return settled;
}

/*
 * global_tran_queue_expand - Expand queue by GLOBAL_TRAN_QUEUE_GROW_SIZE entries
 * Must be called with mutex held.
 * Returns: NO_ERROR on success, ER_OUT_OF_VIRTUAL_MEMORY on failure.
 *
 * Note: We use malloc + copy instead of realloc because the circular buffer
 * may have wrapped around (head > tail). In this case, we need to linearize
 * the data anyway, so realloc would not save any copying. This approach also
 * resets head to 0, making subsequent accesses more cache-friendly.
 */
static int
global_tran_queue_expand (void)
{
  GLOBAL_TRAN_QUEUE_ENTRY *new_queue;
  int new_size, i, j;

  new_size = global_tran_queue_size + GLOBAL_TRAN_QUEUE_GROW_SIZE;
  new_queue = (GLOBAL_TRAN_QUEUE_ENTRY *) malloc (new_size * sizeof (GLOBAL_TRAN_QUEUE_ENTRY));
  if (new_queue == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* Copy existing entries to new queue (linearize circular buffer) */
  for (i = 0, j = global_tran_queue_head; i < global_tran_queue_count; i++)
    {
      new_queue[i] = global_tran_queue[j];
      j = (j + 1) % global_tran_queue_size;
    }

  /* Initialize remaining entries */
  for (i = global_tran_queue_count; i < new_size; i++)
    {
      memset (&new_queue[i], 0, sizeof (GLOBAL_TRAN_QUEUE_ENTRY));
    }

  assert (global_tran_queue != NULL);

  /* Free old queue and update pointers */
  free (global_tran_queue);

  global_tran_queue = new_queue;
  global_tran_queue_size = new_size;
  global_tran_queue_head = 0;
  global_tran_queue_tail = global_tran_queue_count;

  return NO_ERROR;
}

/* Callback for dblink_global_tran_scan_for_recovery: enqueue participant data to daemon */
static bool
dblink_2pc_recovery_callback (const DBLINK_GLOBAL_TRAN_ROW * row_data)
{
  DBLINK_CONN_INFO participant;
  char state;

  /* For 'P' state (before decision), use ABORT for recovery */
  if (row_data->state == DBLINK_2PC_STATE_PREPARE)
    {
      state = DBLINK_2PC_STATE_ABORT;
    }
  else
    {
      state = row_data->state;
    }

  /* Build participant info from row data */
  memset (&participant, 0, sizeof (participant));
  participant.conn_handle = row_data->bqual;
  snprintf (participant.conn_url, sizeof (participant.conn_url), "%s", row_data->conn_url);
  snprintf (participant.user_name, sizeof (participant.user_name), "%s", row_data->user_name);
  snprintf (participant.password, sizeof (participant.password), "%s", row_data->password);

  /* Enqueue to daemon for processing (one entry per participant) */
  /* Recovery replays decisions nobody is waiting for - the sessions that produced them are gone. */
  (void) dblink_2pc_daemon_enqueue (row_data->gtrid, state, &participant, NULL);

  return true;			/* continue to next row */
}

void
dblink_2pc_daemon_recovery_with_thread (THREAD_ENTRY * thread_p)
{
  int tran_index, error;

  if (thread_p == NULL)
    {
      return;
    }

  /* Run the recovery scan under its own transaction (rather than the caller's system/recovery
   * tran index) so that the class lock taken by the heap scan is released via the normal
   * commit/abort path. The system tran index is never committed/aborted or unlocked, so a scan
   * performed directly on it would leak its class lock until server shutdown. */
  tran_index = logtb_assign_tran_index (thread_p, NULL_TRANID, TRAN_ACTIVE, NULL, NULL,
					TRAN_LOCK_INFINITE_WAIT, TRAN_READ_COMMITTED);
  if (tran_index == NULL_TRAN_INDEX)
    {
      return;
    }

  error = dblink_global_tran_scan_for_recovery (thread_p, dblink_2pc_recovery_callback);
  if (error == NO_ERROR)
    {
      xtran_server_commit (thread_p, false);
    }
  else
    {
      (void) xtran_server_abort (thread_p);
    }
  logtb_free_tran_index (thread_p, tran_index);
}

static void
dblink_2pc_daemon_execute (cubthread::entry & thread_ref)
{
  GLOBAL_TRAN_QUEUE_ENTRY e;
  int ret;
  char send_state;
  THREAD_ENTRY *thread_p;

  if (thread_ref.get_system_tdes () == NULL)
    {
      if (!LOG_ISRESTARTED ())
	{
	  return;
	}
      thread_ref.claim_system_worker ();
    }

  while (true)
    {
      /* Dequeue one entry (one participant per entry) */
      ret = dblink_2pc_daemon_dequeue (&e);

      /* Dequeu error or empty: will be retried by looper */
      if (ret != NO_ERROR || e.state == DBLINK_2PC_STATE_EMPTY)
	{
	  return;
	}

      /* Determine decision state */
      if (e.state == DBLINK_2PC_STATE_PREPARE)
	{
	  send_state = DBLINK_2PC_STATE_ABORT;
	}
      else
	{
	  send_state = e.state;
	}

      /* Send decision to this single participant */
      ret = dblink_2pc_send_decision_one_participant (e.gtrid, &e.participant, (send_state == DBLINK_2PC_STATE_COMMIT));

      if (ret != NO_ERROR)
	{
	  /* Error: re-enqueue this single participant for retry.  The completion travels with the entry and
	   * keeps its slot, so the commit path stays blocked until the retry succeeds or its bound
	   * expires - it must not be woken by a decision that was not delivered.
	   *
	   * If the entry cannot go back on the queue, settle it here instead: it took a reference and a
	   * slot when it was queued, and it no longer exists to release them.  Leaving them behind would
	   * make the commit path wait out its whole bound and leak the completion.  The decision itself is
	   * not lost - its _db_global_tran row is still there for recovery to replay. */
	  if (dblink_2pc_daemon_enqueue (e.gtrid, send_state, &e.participant, e.completion) != NO_ERROR)
	    {
	      dblink_2pc_completion_settle (e.completion);
	    }
	  return;
	}

      /* Delivered: the remote changes are visible now, so release the commit path before doing the
       * catalog cleanup below.  That cleanup is recovery bookkeeping and nobody waits on it. */
      dblink_2pc_completion_settle (e.completion);

      thread_p = &thread_ref;
      /* P5: Crash after (6) send decision, before (7) DELETE - recovery: daemon resends decision then DELETE */
      FI_TEST (thread_p, FI_TEST_DBLINK_2PC_CRASH_BETWEEN_6_7, 0);
      /* Use a regular (worker) transaction so that delete runs with normal lock/MVCC semantics. */
      int tran_index = logtb_assign_tran_index (thread_p, NULL_TRANID, TRAN_ACTIVE, NULL, NULL,
						TRAN_LOCK_INFINITE_WAIT, TRAN_READ_COMMITTED);
      if (tran_index != NULL_TRAN_INDEX)
	{
	  int del_error = dblink_global_tran_delete_row (thread_p, e.gtrid, e.participant.conn_handle);
	  if (del_error == NO_ERROR)
	    {
	      xtran_server_commit (thread_p, false);
	      logtb_free_tran_index (thread_p, tran_index);
	    }
	  else
	    {
	      (void) xtran_server_abort (thread_p);
	      logtb_free_tran_index (thread_p, tran_index);
	    }
	}
    }
}

int
dblink_2pc_daemon_dequeue (GLOBAL_TRAN_QUEUE_ENTRY * e)
{
  pthread_mutex_lock (&global_tran_queue_mutex);

  if (global_tran_queue == NULL || e == NULL)
    {
      pthread_mutex_unlock (&global_tran_queue_mutex);
      assert (global_tran_queue != NULL && e != NULL);
      return ER_FAILED;
    }

  /* init state */
  e->state = DBLINK_2PC_STATE_EMPTY;

  if (global_tran_queue_count > 0)
    {
      *e = global_tran_queue[global_tran_queue_head];
      global_tran_queue_head = (global_tran_queue_head + 1) % global_tran_queue_size;
      global_tran_queue_count--;
    }

  pthread_mutex_unlock (&global_tran_queue_mutex);

  return NO_ERROR;
}

int
dblink_2pc_daemon_enqueue (int gtrid, char state, const DBLINK_CONN_INFO * participant,
			   DBLINK_2PC_COMPLETION * completion)
{
  assert (participant != NULL);

  pthread_mutex_lock (&global_tran_queue_mutex);

  if (global_tran_queue == NULL)
    {
      pthread_mutex_unlock (&global_tran_queue_mutex);
      assert (global_tran_queue != NULL);
      return ER_FAILED;
    }

  /* check: queue is full */
  if (global_tran_queue_count >= global_tran_queue_size)
    {
      if (global_tran_queue_expand () != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  (size_t) GLOBAL_TRAN_QUEUE_GROW_SIZE * sizeof (GLOBAL_TRAN_QUEUE_ENTRY));
	  pthread_mutex_unlock (&global_tran_queue_mutex);
	  assert (false);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }

  global_tran_queue[global_tran_queue_tail].gtrid = gtrid;
  global_tran_queue[global_tran_queue_tail].state = state;
  global_tran_queue[global_tran_queue_tail].participant = *participant;
  global_tran_queue[global_tran_queue_tail].completion = completion;
  global_tran_queue_tail = (global_tran_queue_tail + 1) % global_tran_queue_size;
  global_tran_queue_count++;

  pthread_mutex_unlock (&global_tran_queue_mutex);

  if (dblink_2pc_Daemon != NULL)
    {
      dblink_2pc_Daemon->wakeup ();
    }

  return NO_ERROR;
}

REGISTER_DAEMON (dblink_2pc_daemon);

void
dblink_2pc_daemon_init (void)
{
  global_tran_queue_head = 0;
  global_tran_queue_tail = 0;
  global_tran_queue_count = 0;
  global_tran_queue_size = 0;
  global_tran_queue = NULL;

  global_tran_queue =
    (GLOBAL_TRAN_QUEUE_ENTRY *) malloc (GLOBAL_TRAN_QUEUE_INIT_SIZE * sizeof (GLOBAL_TRAN_QUEUE_ENTRY));
  if (global_tran_queue == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      GLOBAL_TRAN_QUEUE_INIT_SIZE * sizeof (GLOBAL_TRAN_QUEUE_ENTRY));
      /* falls through to exit/abort below */
    }
  else
    {
      global_tran_queue_size = GLOBAL_TRAN_QUEUE_INIT_SIZE;
      memset (global_tran_queue, 0, global_tran_queue_size * sizeof (GLOBAL_TRAN_QUEUE_ENTRY));

      {
	cubthread::looper looper = cubthread::looper (std::chrono::seconds (1));
	cubthread::entry_callable_task * daemon_task = new cubthread::entry_callable_task (dblink_2pc_daemon_execute);

	dblink_2pc_Daemon_context_manager = new dblink_2pc_daemon_context_manager ();
	dblink_2pc_Daemon =
	  cubthread::get_manager ()->create_daemon (looper, daemon_task, "dblink_2pc_daemon",
						    dblink_2pc_Daemon_context_manager);
	if (dblink_2pc_Daemon == NULL)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
	    delete daemon_task;
	    delete dblink_2pc_Daemon_context_manager;
	    dblink_2pc_Daemon_context_manager = NULL;
	  }
      }
    }

  if (dblink_2pc_Daemon == NULL)
    {
#if defined(NDEBUG)
      exit (EXIT_FAILURE);
#else
      abort ();
#endif
    }
}

void
dblink_2pc_daemon_stop (void)
{
  int i;

  if (dblink_2pc_Daemon != NULL)
    {
      cubthread::get_manager ()->destroy_daemon (dblink_2pc_Daemon);
    }
  if (dblink_2pc_Daemon_context_manager != NULL)
    {
      delete dblink_2pc_Daemon_context_manager;
      dblink_2pc_Daemon_context_manager = NULL;
    }

  /* Settle whatever is still queued before the queue goes away.  These decisions were not delivered
   * and recovery will replay them from _db_global_tran, but their completions must not be left counting
   * entries that no longer exist: any commit path still blocked here would otherwise sit out its
   * full bound, and the completions themselves would never be freed.
   *
   * This depends on the ordering above: destroy_daemon() joins the daemon thread, so by now no entry
   * is in flight - the one being processed at stop time either completed (and settled itself) or
   * failed and went back on the queue, where the loop below finds it.  Draining before the join
   * would miss it.
   *
   * A completion settled here can wake its commit path with everything "settled" even though nothing was
   * delivered.  That is accepted: the caller ignores the result and treats it exactly like the
   * timeout, and recovery redelivers.  Waking it is better than making it wait out the bound while
   * the server goes down. */
  if (global_tran_queue != NULL)
    {
      pthread_mutex_lock (&global_tran_queue_mutex);
      for (i = 0; i < global_tran_queue_count; i++)
	{
	  int nth = (global_tran_queue_head + i) % global_tran_queue_size;

	  dblink_2pc_completion_settle (global_tran_queue[nth].completion);
	  global_tran_queue[nth].completion = NULL;
	}
      pthread_mutex_unlock (&global_tran_queue_mutex);

      free_and_init (global_tran_queue);
    }
  global_tran_queue_size = 0;
  global_tran_queue_head = 0;
  global_tran_queue_tail = 0;
  global_tran_queue_count = 0;
}
#endif
#endif /* CCI_XA */
