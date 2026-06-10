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
  (void) dblink_2pc_daemon_enqueue (row_data->gtrid, state, &participant);

  return true;			/* continue to next row */
}

void
dblink_2pc_daemon_recovery_with_thread (THREAD_ENTRY * thread_p)
{
  if (thread_p == NULL)
    {
      return;
    }
  (void) dblink_global_tran_scan_for_recovery (thread_p, dblink_2pc_recovery_callback);
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
	  /* Error: re-enqueue this single participant for retry */
	  (void) dblink_2pc_daemon_enqueue (e.gtrid, send_state, &e.participant);
	  return;
	}

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
dblink_2pc_daemon_enqueue (int gtrid, char state, const DBLINK_CONN_INFO * participant)
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
  global_tran_queue_tail = (global_tran_queue_tail + 1) % global_tran_queue_size;
  global_tran_queue_count++;

  pthread_mutex_unlock (&global_tran_queue_mutex);

  if (dblink_2pc_Daemon != NULL)
    {
      dblink_2pc_Daemon->wakeup ();
    }

  return NO_ERROR;
}

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

  if (global_tran_queue != NULL)
    {
      free_and_init (global_tran_queue);
    }
  global_tran_queue_size = 0;
  global_tran_queue_head = 0;
  global_tran_queue_tail = 0;
  global_tran_queue_count = 0;
}
#endif
#endif /* CCI_XA */
