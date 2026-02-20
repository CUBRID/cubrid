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

#include "config.h"
#include "dblink_2pc_daemon.h"
#include "dblink_2pc.h"
#include "dblink_global_tran_catalog.h"
#include "dblink_scan.h"
#include "error_manager.h"
#include "log_manager.h"
#include "memory_alloc.h"
#include "thread_manager.hpp"

#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#if defined(WINDOWS)
#include "porting.h"
#endif
#include <pthread.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* Initial and increment size for dynamic queue */
#define GLOBAL_TRAN_QUEUE_INIT_SIZE  64
#define GLOBAL_TRAN_QUEUE_GROW_SIZE  64

typedef struct global_tran_queue_entry GLOBAL_TRAN_QUEUE_ENTRY;
struct global_tran_queue_entry
{
  int gtrid;
  char state;			/* DBLINK_2PC_STATE_PREPARE / ABORT / COMMIT */
  int num_participants;
  DBLINK_CONN_INFO *participants;	/* malloc'd copy, daemon frees */
};

/* Dynamic circular queue */
static GLOBAL_TRAN_QUEUE_ENTRY *global_tran_queue = NULL;
static int global_tran_queue_size = 0;	/* allocated size */
static int global_tran_queue_head = 0;
static int global_tran_queue_tail = 0;
static int global_tran_queue_count = 0;
static pthread_mutex_t global_tran_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t global_tran_queue_cond = PTHREAD_COND_INITIALIZER;

static volatile int daemon_stop_requested;
static pthread_t daemon_thread_id;
static bool daemon_thread_started = false;

static void
global_tran_queue_entry_free (GLOBAL_TRAN_QUEUE_ENTRY * e)
{
  if (e->participants != NULL)
    {
      free_and_init (e->participants);
    }
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
  int new_size;
  int i, j;

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

  /* Free old queue and update pointers */
  if (global_tran_queue != NULL)
    {
      free (global_tran_queue);
    }

  global_tran_queue = new_queue;
  global_tran_queue_size = new_size;
  global_tran_queue_head = 0;
  global_tran_queue_tail = global_tran_queue_count;

  return NO_ERROR;
}

/*
 * Coordinator (log_2pc.c) already updated _db_global_tran state to 'A' or 'C'.
 * Daemon sends decision to each participant.
 * Returns: NO_ERROR if all succeeded, ER_FAILED if any failed.
 */
static int
dblink_2pc_daemon_send_decision (int gtrid, char state, int num_participants, DBLINK_CONN_INFO * participants)
{
  int i;
  int ret;
  int error_count = 0;
  bool is_commit = (state == DBLINK_2PC_STATE_COMMIT);

  for (i = 0; i < num_participants; i++)
    {
      ret = dblink_2pc_send_decision_one_participant (gtrid, participants[i].conn_handle,
						      participants[i].conn_url,
						      participants[i].user_name, participants[i].password, is_commit);
      if (ret != NO_ERROR)
	{
	  error_count++;
	}
    }

  return (error_count > 0) ? ER_FAILED : NO_ERROR;
}

/* Callback for dblink_global_tran_scan_for_recovery: enqueue participant data to daemon */
static bool
dblink_2pc_recovery_callback (void *arg, const DBLINK_GLOBAL_TRAN_ROW * row_data)
{
  DBLINK_CONN_INFO participant;
  char state;

  (void) arg;

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

  /* Enqueue to daemon for processing */
  (void) dblink_2pc_daemon_enqueue (row_data->gtrid, state, 1, &participant);

  return true;			/* continue to next row */
}

/* Run by daemon thread on startup; actual recovery runs in log_recovery with thread_p (recovery_with_thread). */
void
dblink_2pc_daemon_recovery (void)
{
  /* Recovery is done in log_recovery via dblink_2pc_daemon_recovery_with_thread(thread_p) before daemon_start */
}

void
dblink_2pc_daemon_recovery_with_thread (THREAD_ENTRY * thread_p)
{
  if (thread_p == NULL)
    {
      return;
    }
  (void) dblink_global_tran_scan_for_recovery (thread_p, dblink_2pc_recovery_callback, (void *) thread_p);
}

static void *
dblink_2pc_daemon_thread (void *arg)
{
  GLOBAL_TRAN_QUEUE_ENTRY e;
  struct timespec ts;
  int ret;
  char send_state;
  THREAD_ENTRY *thread_p;
  int i;

  (void) arg;

  /* First run recovery */
  dblink_2pc_daemon_recovery ();

  while (!daemon_stop_requested)
    {
      pthread_mutex_lock (&global_tran_queue_mutex);

      while (global_tran_queue_count == 0 && !daemon_stop_requested)
	{
	  ts.tv_sec = time (NULL) + 5;
	  ts.tv_nsec = 0;
	  ret = pthread_cond_timedwait (&global_tran_queue_cond, &global_tran_queue_mutex, &ts);
	  (void) ret;
	}

      if (daemon_stop_requested)
	{
	  pthread_mutex_unlock (&global_tran_queue_mutex);
	  break;
	}

      /* Dequeue one entry */
      e = global_tran_queue[global_tran_queue_head];
      global_tran_queue[global_tran_queue_head].participants = NULL;
      global_tran_queue_head = (global_tran_queue_head + 1) % global_tran_queue_size;
      global_tran_queue_count--;
      pthread_mutex_unlock (&global_tran_queue_mutex);

      /* Determine decision state */
      if (e.state == DBLINK_2PC_STATE_PREPARE)
	{
	  /* PREPARE state before decision: send ABORT for recovery */
	  send_state = DBLINK_2PC_STATE_ABORT;
	}
      else
	{
	  send_state = e.state;
	}

      /* Send decision to participants */
      ret = dblink_2pc_daemon_send_decision (e.gtrid, send_state, e.num_participants, e.participants);

      if (ret == NO_ERROR)
	{
	  /* Success: delete from _db_global_tran catalog */
	  thread_p = thread_get_thread_entry_info ();
	  if (thread_p != NULL)
	    {
	      log_sysop_start (thread_p);
	      for (i = 0; i < e.num_participants; i++)
		{
		  (void) dblink_global_tran_delete_row (thread_p, e.gtrid, e.participants[i].conn_handle);
		}
	      log_sysop_commit (thread_p);
	    }
	  global_tran_queue_entry_free (&e);
	}
      else
	{
	  /* Error: re-enqueue for retry */
	  pthread_mutex_lock (&global_tran_queue_mutex);

	  /* Expand queue if full */
	  if (global_tran_queue_count >= global_tran_queue_size)
	    {
	      if (global_tran_queue_expand () != NO_ERROR)
		{
		  /* Failed to expand, free entry (will be recovered on next restart) */
		  pthread_mutex_unlock (&global_tran_queue_mutex);
		  global_tran_queue_entry_free (&e);
		  sleep (1);
		  continue;
		}
	    }

	  /* Enqueue for retry */
	  global_tran_queue[global_tran_queue_tail].gtrid = e.gtrid;
	  global_tran_queue[global_tran_queue_tail].state = send_state;
	  global_tran_queue[global_tran_queue_tail].num_participants = e.num_participants;
	  global_tran_queue[global_tran_queue_tail].participants = e.participants;	/* transfer ownership */
	  global_tran_queue_tail = (global_tran_queue_tail + 1) % global_tran_queue_size;
	  global_tran_queue_count++;

	  pthread_mutex_unlock (&global_tran_queue_mutex);

	  /* Wait a bit before retry */
	  sleep (1);
	}
    }

  return NULL;
}

int
dblink_2pc_daemon_enqueue (int gtrid, char state, int num_participants, void *block_particps_ids)
{
  size_t block_size;
  DBLINK_CONN_INFO *copy;

  if (block_particps_ids == NULL || num_participants <= 0)
    {
      return ER_FAILED;
    }

  if (!daemon_thread_started)
    {
      int err = dblink_2pc_daemon_start ();

      if (err != NO_ERROR)
	{
	  return err;
	}
    }

  if (global_tran_queue == NULL)
    {
      return ER_FAILED;		/* daemon not started (e.g. start failed) */
    }

  block_size = (size_t) num_participants *sizeof (DBLINK_CONN_INFO);
  copy = (DBLINK_CONN_INFO *) malloc (block_size);
  if (copy == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  memcpy (copy, block_particps_ids, block_size);

  pthread_mutex_lock (&global_tran_queue_mutex);

  /* Expand queue if full */
  if (global_tran_queue_count >= global_tran_queue_size)
    {
      if (global_tran_queue_expand () != NO_ERROR)
	{
	  pthread_mutex_unlock (&global_tran_queue_mutex);
	  free_and_init (copy);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }

  global_tran_queue[global_tran_queue_tail].gtrid = gtrid;
  global_tran_queue[global_tran_queue_tail].state = state;
  global_tran_queue[global_tran_queue_tail].num_participants = num_participants;
  global_tran_queue[global_tran_queue_tail].participants = copy;
  global_tran_queue_tail = (global_tran_queue_tail + 1) % global_tran_queue_size;
  global_tran_queue_count++;

  pthread_cond_signal (&global_tran_queue_cond);
  pthread_mutex_unlock (&global_tran_queue_mutex);

  return NO_ERROR;
}

int
dblink_2pc_daemon_start (void)
{
  int err;

  daemon_stop_requested = 0;
  daemon_thread_started = false;
  global_tran_queue_head = 0;
  global_tran_queue_tail = 0;
  global_tran_queue_count = 0;
  global_tran_queue_size = 0;
  global_tran_queue = NULL;

  /* Allocate initial queue before creating thread; avoid daemon accessing NULL queue */
  global_tran_queue =
    (GLOBAL_TRAN_QUEUE_ENTRY *) malloc (GLOBAL_TRAN_QUEUE_INIT_SIZE * sizeof (GLOBAL_TRAN_QUEUE_ENTRY));
  if (global_tran_queue == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  global_tran_queue_size = GLOBAL_TRAN_QUEUE_INIT_SIZE;
  memset (global_tran_queue, 0, global_tran_queue_size * sizeof (GLOBAL_TRAN_QUEUE_ENTRY));

  err = pthread_create (&daemon_thread_id, NULL, dblink_2pc_daemon_thread, NULL);
  if (err != 0)
    {
      free (global_tran_queue);
      global_tran_queue = NULL;
      global_tran_queue_size = 0;
      return ER_FAILED;
    }
  daemon_thread_started = true;
  return NO_ERROR;
}

void
dblink_2pc_daemon_stop (void)
{
  int i;

  daemon_stop_requested = 1;
  if (daemon_thread_started)
    {
      pthread_cond_signal (&global_tran_queue_cond);
      pthread_join (daemon_thread_id, NULL);
      daemon_thread_started = false;
    }

  /* Free remaining queue entries */
  if (global_tran_queue != NULL)
    {
      for (i = 0; i < global_tran_queue_size; i++)
	{
	  global_tran_queue_entry_free (&global_tran_queue[i]);
	}
      free (global_tran_queue);
      global_tran_queue = NULL;
    }
  global_tran_queue_size = 0;
  global_tran_queue_head = 0;
  global_tran_queue_tail = 0;
  global_tran_queue_count = 0;
}

#endif /* CCI_XA */
