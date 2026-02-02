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

/* Circular queue for coordinator -> daemon participant data */
#define GLOBAL_TRAN_QUEUE_SIZE  64

typedef struct global_tran_queue_entry GLOBAL_TRAN_QUEUE_ENTRY;
struct global_tran_queue_entry
{
  int gtrid;
  char state;			/* DBLINK_2PC_STATE_PREPARE / ABORT / COMMIT */
  int num_participants;
  DBLINK_CONN_INFO *participants;	/* malloc'd copy, daemon frees */
};

static GLOBAL_TRAN_QUEUE_ENTRY global_tran_queue[GLOBAL_TRAN_QUEUE_SIZE];
static int global_tran_queue_head;
static int global_tran_queue_tail;
static int global_tran_queue_count;
static pthread_mutex_t global_tran_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t global_tran_queue_cond = PTHREAD_COND_INITIALIZER;

static volatile int daemon_stop_requested;
static pthread_t daemon_thread_id;

static void
global_tran_queue_entry_free (GLOBAL_TRAN_QUEUE_ENTRY * e)
{
  if (e->participants != NULL)
    {
      free_and_init (e->participants);
    }
}

/*
 * Insert into _db_global_tran (state 'P') is done by coordinator (log_2pc.c) before enqueue.
 * Daemon only needs to process queue; no catalog access in daemon thread (no THREAD_ENTRY).
 */
static void
dblink_2pc_daemon_insert_global_tran_prepare (int gtrid, int num_participants, DBLINK_CONN_INFO * participants)
{
  (void) gtrid;
  (void) num_participants;
  (void) participants;
}

/*
 * Coordinator (log_2pc.c) already updated _db_global_tran state to 'A' or 'C'.
 * Daemon only sends decision to each participant.
 */
static void
dblink_2pc_daemon_send_decision (int gtrid, char state, int num_participants, DBLINK_CONN_INFO * participants)
{
  int i;
  bool is_commit = (state == DBLINK_2PC_STATE_COMMIT);

  for (i = 0; i < num_participants; i++)
    {
      (void) dblink_2pc_send_decision_one_participant (gtrid, participants[i].conn_handle,
						       participants[i].conn_url,
						       participants[i].user_name, participants[i].password, is_commit);
    }
}

/* Callback for dblink_global_tran_scan_for_recovery: send decision, delete row on success */
static bool
dblink_2pc_recovery_callback (void *arg, const DBLINK_GLOBAL_TRAN_ROW * row_data, OID * row_oid)
{
  THREAD_ENTRY *thread_p = (THREAD_ENTRY *) arg;
  bool is_commit;
  int ret;

  (void) row_oid;

  /* For 'P' state (before decision), send ABORT for recovery */
  if (row_data->state == DBLINK_2PC_STATE_PREPARE)
    {
      is_commit = false;
    }
  else
    {
      is_commit = (row_data->state == DBLINK_2PC_STATE_COMMIT);
    }

  ret = dblink_2pc_send_decision_one_participant (row_data->gtrid, row_data->bqual,
						  row_data->conn_url, row_data->user_name,
						  row_data->password, is_commit);
  if (ret == NO_ERROR)
    {
      /* Delete row using server transaction */
      log_sysop_start (thread_p);
      (void) dblink_global_tran_delete_row (thread_p, row_data->gtrid, row_data->bqual);
      log_sysop_commit (thread_p);
    }
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
      global_tran_queue_head = (global_tran_queue_head + 1) % GLOBAL_TRAN_QUEUE_SIZE;
      global_tran_queue_count--;
      pthread_mutex_unlock (&global_tran_queue_mutex);

      if (e.state == DBLINK_2PC_STATE_PREPARE)
	{
	  /* PREPARE state before decision: send ABORT for recovery */
	  dblink_2pc_daemon_send_decision (e.gtrid, DBLINK_2PC_STATE_ABORT, e.num_participants, e.participants);
	}
      else if (e.state == DBLINK_2PC_STATE_ABORT || e.state == DBLINK_2PC_STATE_COMMIT)
	{
	  dblink_2pc_daemon_send_decision (e.gtrid, e.state, e.num_participants, e.participants);
	}

      global_tran_queue_entry_free (&e);
    }

  return NULL;
}

int
dblink_2pc_daemon_enqueue (int gtrid, char state, int num_participants, void *block_particps_ids)
{
  int tail_next;
  size_t block_size;
  DBLINK_CONN_INFO *copy;

  if (block_particps_ids == NULL || num_participants <= 0)
    {
      return ER_FAILED;
    }

  block_size = (size_t) num_participants *sizeof (DBLINK_CONN_INFO);
  copy = (DBLINK_CONN_INFO *) malloc (block_size);
  if (copy == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  memcpy (copy, block_particps_ids, block_size);

  pthread_mutex_lock (&global_tran_queue_mutex);

  if (global_tran_queue_count >= GLOBAL_TRAN_QUEUE_SIZE)
    {
      pthread_mutex_unlock (&global_tran_queue_mutex);
      free_and_init (copy);
      return ER_FAILED;
    }

  tail_next = global_tran_queue_tail;
  global_tran_queue[tail_next].gtrid = gtrid;
  global_tran_queue[tail_next].state = state;
  global_tran_queue[tail_next].num_participants = num_participants;
  global_tran_queue[tail_next].participants = copy;
  global_tran_queue_tail = (global_tran_queue_tail + 1) % GLOBAL_TRAN_QUEUE_SIZE;
  global_tran_queue_count++;

  pthread_cond_signal (&global_tran_queue_cond);
  pthread_mutex_unlock (&global_tran_queue_mutex);

  return NO_ERROR;
}

void
dblink_2pc_daemon_start (void)
{
  daemon_stop_requested = 0;
  global_tran_queue_head = 0;
  global_tran_queue_tail = 0;
  global_tran_queue_count = 0;

  if (pthread_create (&daemon_thread_id, NULL, dblink_2pc_daemon_thread, NULL) != 0)
    {
      assert (false);
    }
}

void
dblink_2pc_daemon_stop (void)
{
  daemon_stop_requested = 1;
  pthread_cond_signal (&global_tran_queue_cond);
  pthread_join (daemon_thread_id, NULL);
}

#endif /* CCI_XA */
