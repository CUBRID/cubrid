/*
 * Copyright 2008 Search Solution Corporation
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
 * dblink_2pc_coordinator.c - 2PC Coordinator Daemon Thread
 * 
 * This module implements a daemon thread that coordinates distributed transactions
 * using a circular queue for communication with dblink DML transactions.
 * Supports non-blocking retry mechanism for failed commit/abort operations.
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#include "thread_manager.hpp"
#include "dblink_2pc_coordinator.h"
#include "dblink_2pc_log.h"
#include "dblink_2pc.h"
#include "critical_section.h"
#include "log_impl.h"
#include "transaction_sr.h"
#include "xserver_interface.h"
#include "log_manager.h"
#include "porting.h"
#include "dbtype.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* Circular queue size */
#define GTRAN_2PC_QUEUE_SIZE 1024

/* Retry queue size */
#define GTRAN_2PC_RETRY_QUEUE_SIZE 512

/* Retry interval in milliseconds */
#define GTRAN_2PC_RETRY_INTERVAL_MS 1000

/* Maximum retry attempts before logging warning */
#define GTRAN_2PC_MAX_RETRY_WARNING 100

/* Request types */
#define GTRAN_2PC_REQ_INSERT      1
#define GTRAN_2PC_REQ_UPDATE      2
#define GTRAN_2PC_REQ_COMMIT      3
#define GTRAN_2PC_REQ_ABORT       4
#define GTRAN_2PC_REQ_SHUTDOWN    99

/*
 * GTRAN_2PC_REQUEST - Request structure for coordinator daemon
 */
typedef struct dblink_2pc_request GTRAN_2PC_REQUEST;

/*
 * GTRAN_2PC_RETRY_ENTRY - Entry for retry queue
 */
typedef struct dblink_2pc_retry_entry GTRAN_2PC_RETRY_ENTRY;
struct dblink_2pc_retry_entry
{
  int gtrid;			/* Global Transaction ID */
  int req_type;			/* GTRAN_2PC_REQ_COMMIT or GTRAN_2PC_REQ_ABORT */
  PARTICIPANT_INFO participant;	/* Participant information */
  time_t last_retry_time;	/* Last retry timestamp */
  int retry_count;		/* Number of retry attempts */
};

/*
 * GTRAN_2PC_CIRCULAR_QUEUE - Circular queue for coordinator requests
 */
typedef struct dblink_2pc_circular_queue GTRAN_2PC_CIRCULAR_QUEUE;
struct dblink_2pc_circular_queue
{
  GTRAN_2PC_REQUEST *requests[GTRAN_2PC_QUEUE_SIZE];
  int head;			/* Consumer position */
  int tail;			/* Producer position */
  int count;			/* Number of items in queue */

  pthread_mutex_t mutex;
  pthread_cond_t not_empty;	/* Signal when queue has items */
  pthread_cond_t not_full;	/* Signal when queue has space */
};

/*
 * GTRAN_2PC_RETRY_QUEUE - Queue for retry operations
 */
typedef struct dblink_2pc_retry_queue GTRAN_2PC_RETRY_QUEUE;
struct dblink_2pc_retry_queue
{
  GTRAN_2PC_RETRY_ENTRY entries[GTRAN_2PC_RETRY_QUEUE_SIZE];
  int count;			/* Number of entries */
  pthread_mutex_t mutex;
  pthread_cond_t not_full;	/* Signal when queue has space */
};

/* Global coordinator daemon state */
static struct
{
  pthread_t daemon_thread;
  bool is_running;
  GTRAN_2PC_CIRCULAR_QUEUE queue;
  GTRAN_2PC_RETRY_QUEUE retry_queue;
} dblink_2pc_Coordinator_daemon;

/* Forward declarations */
static void *dblink_2pc_coordinator_daemon_thread (void *arg);
static int dblink_2pc_process_request (THREAD_ENTRY * thread_p, GTRAN_2PC_REQUEST * req);
static int dblink_2pc_handle_insert_request (THREAD_ENTRY * thread_p, GTRAN_2PC_REQUEST * req);
static int dblink_2pc_handle_update_request (THREAD_ENTRY * thread_p, GTRAN_2PC_REQUEST * req);
static int dblink_2pc_handle_commit_request (THREAD_ENTRY * thread_p, GTRAN_2PC_REQUEST * req);
static int dblink_2pc_handle_abort_request (THREAD_ENTRY * thread_p, GTRAN_2PC_REQUEST * req);
static int dblink_2pc_add_to_retry_queue (int gtrid, int req_type, const PARTICIPANT_INFO * participant);
static void dblink_2pc_process_retry_queue (THREAD_ENTRY * thread_p);
static int dblink_2pc_retry_commit_abort (THREAD_ENTRY * thread_p, GTRAN_2PC_RETRY_ENTRY * entry);

/*
 * dblink_2pc_initialize_queue() - Initialize circular queue
 *   return: Error code
 */
static int
dblink_2pc_initialize_queue (void)
{
  int i;

  memset (&dblink_2pc_Coordinator_daemon.queue, 0, sizeof (GTRAN_2PC_CIRCULAR_QUEUE));

  dblink_2pc_Coordinator_daemon.queue.head = 0;
  dblink_2pc_Coordinator_daemon.queue.tail = 0;
  dblink_2pc_Coordinator_daemon.queue.count = 0;

  pthread_mutex_init (&dblink_2pc_Coordinator_daemon.queue.mutex, NULL);
  pthread_cond_init (&dblink_2pc_Coordinator_daemon.queue.not_empty, NULL);
  pthread_cond_init (&dblink_2pc_Coordinator_daemon.queue.not_full, NULL);

  for (i = 0; i < GTRAN_2PC_QUEUE_SIZE; i++)
    {
      dblink_2pc_Coordinator_daemon.queue.requests[i] = NULL;
    }

  return NO_ERROR;
}

/*
 * dblink_2pc_initialize_retry_queue() - Initialize retry queue
 *   return: Error code
 */
static int
dblink_2pc_initialize_retry_queue (void)
{
  memset (&dblink_2pc_Coordinator_daemon.retry_queue, 0, sizeof (GTRAN_2PC_RETRY_QUEUE));

  dblink_2pc_Coordinator_daemon.retry_queue.count = 0;
  pthread_mutex_init (&dblink_2pc_Coordinator_daemon.retry_queue.mutex, NULL);
  pthread_cond_init (&dblink_2pc_Coordinator_daemon.retry_queue.not_full, NULL);

  return NO_ERROR;
}

/*
 * dblink_2pc_destroy_queue() - Destroy circular queue
 */
static void
dblink_2pc_destroy_queue (void)
{
  int i;

  pthread_mutex_lock (&dblink_2pc_Coordinator_daemon.queue.mutex);

  for (i = 0; i < GTRAN_2PC_QUEUE_SIZE; i++)
    {
      if (dblink_2pc_Coordinator_daemon.queue.requests[i] != NULL)
	{
	  pthread_mutex_destroy (&dblink_2pc_Coordinator_daemon.queue.requests[i]->ack_mutex);
	  pthread_cond_destroy (&dblink_2pc_Coordinator_daemon.queue.requests[i]->ack_cond);
	  free (dblink_2pc_Coordinator_daemon.queue.requests[i]);
	  dblink_2pc_Coordinator_daemon.queue.requests[i] = NULL;
	}
    }

  pthread_mutex_unlock (&dblink_2pc_Coordinator_daemon.queue.mutex);

  pthread_mutex_destroy (&dblink_2pc_Coordinator_daemon.queue.mutex);
  pthread_cond_destroy (&dblink_2pc_Coordinator_daemon.queue.not_empty);
  pthread_cond_destroy (&dblink_2pc_Coordinator_daemon.queue.not_full);
}

/*
 * dblink_2pc_destroy_retry_queue() - Destroy retry queue
 */
static void
dblink_2pc_destroy_retry_queue (void)
{
  pthread_cond_destroy (&dblink_2pc_Coordinator_daemon.retry_queue.not_full);
  pthread_mutex_destroy (&dblink_2pc_Coordinator_daemon.retry_queue.mutex);
}

/*
 * dblink_2pc_enqueue_request() - Enqueue a request to coordinator daemon
 *   return: Error code
 *   req(in): Request to enqueue
 *   wait_ack(in): Whether to wait for ACK
 */
int
dblink_2pc_enqueue_request (GTRAN_2PC_REQUEST * req, bool wait_ack)
{
  int error = NO_ERROR;

  if (req == NULL)
    {
      return ER_FAILED;
    }

  pthread_mutex_lock (&dblink_2pc_Coordinator_daemon.queue.mutex);

  /* Wait if queue is full */
  while (dblink_2pc_Coordinator_daemon.queue.count >= GTRAN_2PC_QUEUE_SIZE)
    {
      pthread_cond_wait (&dblink_2pc_Coordinator_daemon.queue.not_full, &dblink_2pc_Coordinator_daemon.queue.mutex);
    }

  /* Enqueue request */
  dblink_2pc_Coordinator_daemon.queue.requests[dblink_2pc_Coordinator_daemon.queue.tail] = req;
  dblink_2pc_Coordinator_daemon.queue.tail = (dblink_2pc_Coordinator_daemon.queue.tail + 1) % GTRAN_2PC_QUEUE_SIZE;
  dblink_2pc_Coordinator_daemon.queue.count++;

  /* Signal that queue is not empty */
  pthread_cond_signal (&dblink_2pc_Coordinator_daemon.queue.not_empty);

  pthread_mutex_unlock (&dblink_2pc_Coordinator_daemon.queue.mutex);

  /* Wait for ACK if requested */
  if (wait_ack)
    {
      pthread_mutex_lock (&req->ack_mutex);
      while (!req->ack_received)
	{
	  pthread_cond_wait (&req->ack_cond, &req->ack_mutex);
	}
      error = req->result;
      pthread_mutex_unlock (&req->ack_mutex);
    }

  return error;
}

/*
 * dblink_2pc_dequeue_request() - Dequeue a request from coordinator daemon with timeout
 *   return: Request pointer or NULL
 *   timeout_ms(in): Timeout in milliseconds
 */
static GTRAN_2PC_REQUEST *
dblink_2pc_dequeue_request (int timeout_ms)
{
  GTRAN_2PC_REQUEST *req = NULL;
  struct timespec ts;
  int ret;

  pthread_mutex_lock (&dblink_2pc_Coordinator_daemon.queue.mutex);

  /* Calculate absolute timeout */
  clock_gettime (CLOCK_REALTIME, &ts);
  ts.tv_sec += timeout_ms / 1000;
  ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
  if (ts.tv_nsec >= 1000000000L)
    {
      ts.tv_sec++;
      ts.tv_nsec -= 1000000000L;
    }

  /* Wait if queue is empty with timeout */
  while (dblink_2pc_Coordinator_daemon.queue.count == 0 && dblink_2pc_Coordinator_daemon.is_running)
    {
      ret = pthread_cond_timedwait (&dblink_2pc_Coordinator_daemon.queue.not_empty,
				    &dblink_2pc_Coordinator_daemon.queue.mutex, &ts);
      if (ret == ETIMEDOUT)
	{
	  break;		/* Timeout - return NULL to process retry queue */
	}
    }

  if (dblink_2pc_Coordinator_daemon.queue.count > 0)
    {
      /* Dequeue request */
      req = dblink_2pc_Coordinator_daemon.queue.requests[dblink_2pc_Coordinator_daemon.queue.head];
      dblink_2pc_Coordinator_daemon.queue.requests[dblink_2pc_Coordinator_daemon.queue.head] = NULL;
      dblink_2pc_Coordinator_daemon.queue.head = (dblink_2pc_Coordinator_daemon.queue.head + 1) % GTRAN_2PC_QUEUE_SIZE;
      dblink_2pc_Coordinator_daemon.queue.count--;

      /* Signal that queue is not full */
      pthread_cond_signal (&dblink_2pc_Coordinator_daemon.queue.not_full);
    }

  pthread_mutex_unlock (&dblink_2pc_Coordinator_daemon.queue.mutex);

  return req;
}

/*
 * dblink_2pc_send_ack() - Send ACK to waiting transaction
 *   return: void
 *   req(in): Request to acknowledge
 *   result(in): Result code
 */
static void
dblink_2pc_send_ack (GTRAN_2PC_REQUEST * req, int result)
{
  if (req == NULL)
    {
      return;
    }

  pthread_mutex_lock (&req->ack_mutex);
  req->result = result;
  req->ack_received = true;
  pthread_cond_signal (&req->ack_cond);
  pthread_mutex_unlock (&req->ack_mutex);
}

/*
 * dblink_2pc_add_to_retry_queue() - Add failed commit/abort to retry queue
 *   return: Error code
 *   gtrid(in): Global transaction ID
 *   req_type(in): GTRAN_2PC_REQ_COMMIT or GTRAN_2PC_REQ_ABORT
 *   participant(in): Participant information
 */
static int
dblink_2pc_add_to_retry_queue (int gtrid, int req_type, const PARTICIPANT_INFO * participant)
{
  int i;
  GTRAN_2PC_RETRY_ENTRY *entry = NULL;

  if (participant == NULL)
    {
      return ER_FAILED;
    }

  pthread_mutex_lock (&dblink_2pc_Coordinator_daemon.retry_queue.mutex);

  /* Check if already in retry queue */
  for (i = 0; i < dblink_2pc_Coordinator_daemon.retry_queue.count; i++)
    {
      if (dblink_2pc_Coordinator_daemon.retry_queue.entries[i].gtrid == gtrid &&
	  dblink_2pc_Coordinator_daemon.retry_queue.entries[i].participant.bqual == participant->bqual)
	{
	  /* Already exists - just update retry time */
	  dblink_2pc_Coordinator_daemon.retry_queue.entries[i].last_retry_time = time (NULL);
	  pthread_mutex_unlock (&dblink_2pc_Coordinator_daemon.retry_queue.mutex);
	  return NO_ERROR;
	}
    }

  /* Wait if retry queue is full */
  while (dblink_2pc_Coordinator_daemon.retry_queue.count >= GTRAN_2PC_RETRY_QUEUE_SIZE)
    {
      er_log_debug (ARG_FILE_LINE,
		    "Retry queue is full (%d entries), waiting for space to add gtrid=%d bqual=%d",
		    dblink_2pc_Coordinator_daemon.retry_queue.count, gtrid, participant->bqual);
      pthread_cond_wait (&dblink_2pc_Coordinator_daemon.retry_queue.not_full,
			 &dblink_2pc_Coordinator_daemon.retry_queue.mutex);
    }

  /* Add new entry */
  entry = &dblink_2pc_Coordinator_daemon.retry_queue.entries[dblink_2pc_Coordinator_daemon.retry_queue.count];
  entry->gtrid = gtrid;
  entry->req_type = req_type;
  entry->last_retry_time = time (NULL);
  entry->retry_count = 0;

  /* Copy participant information */
  memcpy (&entry->participant, participant, sizeof (PARTICIPANT_INFO));

  dblink_2pc_Coordinator_daemon.retry_queue.count++;

  pthread_mutex_unlock (&dblink_2pc_Coordinator_daemon.retry_queue.mutex);

  er_log_debug (ARG_FILE_LINE, "Added gtrid=%d bqual=%d to retry queue (type=%d)", gtrid, participant->bqual, req_type);

  return NO_ERROR;
}

/*
 * dblink_2pc_remove_from_retry_queue() - Remove entry from retry queue
 *   return: void
 *   index(in): Index to remove
 */
static void
dblink_2pc_remove_from_retry_queue (int index)
{
  int i;

  if (index < 0 || index >= dblink_2pc_Coordinator_daemon.retry_queue.count)
    {
      return;
    }

  /* Shift remaining entries */
  for (i = index; i < dblink_2pc_Coordinator_daemon.retry_queue.count - 1; i++)
    {
      dblink_2pc_Coordinator_daemon.retry_queue.entries[i] = dblink_2pc_Coordinator_daemon.retry_queue.entries[i + 1];
    }

  dblink_2pc_Coordinator_daemon.retry_queue.count--;

  /* Clear last entry */
  memset (&dblink_2pc_Coordinator_daemon.retry_queue.entries[dblink_2pc_Coordinator_daemon.retry_queue.count],
	  0, sizeof (GTRAN_2PC_RETRY_ENTRY));

  /* Signal that queue has space */
  pthread_cond_signal (&dblink_2pc_Coordinator_daemon.retry_queue.not_full);
}

/*
 * dblink_2pc_retry_commit_abort() - Retry commit or abort operation
 *   return: Error code (NO_ERROR if successful)
 *   thread_p(in): Thread entry
 *   entry(in): Retry entry
 */
static int
dblink_2pc_retry_commit_abort (THREAD_ENTRY * thread_p, GTRAN_2PC_RETRY_ENTRY * entry)
{
  int error = NO_ERROR;

  if (entry->req_type == GTRAN_2PC_REQ_COMMIT)
    {
      dblink_2pc_end_tran (thread_p, entry->gtrid, 1, true, &entry->participant);
    }
  else if (entry->req_type == GTRAN_2PC_REQ_ABORT)
    {
      dblink_2pc_end_tran (thread_p, entry->gtrid, 1, false, &entry->participant);
    }

  return error;
}

/*
 * dblink_2pc_process_retry_queue() - Process retry queue
 *   return: void
 *   thread_p(in): Thread entry
 */
static void
dblink_2pc_process_retry_queue (THREAD_ENTRY * thread_p)
{
  int i;
  time_t current_time;
  int error;
  int tran_index;
  int old_tran_index;

  TRAN_STATE tran_state;

  pthread_mutex_lock (&dblink_2pc_Coordinator_daemon.retry_queue.mutex);

  current_time = time (NULL);

  /* Process each entry in retry queue */
  for (i = 0; i < dblink_2pc_Coordinator_daemon.retry_queue.count;)
    {
      GTRAN_2PC_RETRY_ENTRY *entry = &dblink_2pc_Coordinator_daemon.retry_queue.entries[i];

      /* Check if enough time has passed since last retry */
      if (difftime (current_time, entry->last_retry_time) < (GTRAN_2PC_RETRY_INTERVAL_MS / 1000.0))
	{
	  i++;
	  continue;
	}

      /* Update retry time and count */
      entry->last_retry_time = current_time;
      entry->retry_count++;

      /* Log warning if retry count is high */
      if (entry->retry_count % GTRAN_2PC_MAX_RETRY_WARNING == 0)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 1,
		  "2PC retry count exceeded warning threshold");
	  er_log_debug (ARG_FILE_LINE,
			"gtrid=%s has been retried %d times (type=%d)",
			entry->gtrid, entry->retry_count, entry->req_type);
	}

      /* Retry the operation */
      error = dblink_2pc_retry_commit_abort (thread_p, entry);

      if (error == NO_ERROR)
	{
	  /* Success - delete catalog entry and remove from retry queue */
	  old_tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

	  tran_index = logtb_assign_tran_index (thread_p, NULL_TRANID, TRAN_ACTIVE,
						NULL, &tran_state, TRAN_LOCK_INFINITE_WAIT,
						TRAN_DEFAULT_ISOLATION_LEVEL ());
	  if (tran_index >= 0)
	    {
	      error = dblink_2pc_log_delete (thread_p, entry->gtrid, entry->participant.bqual);
	      if (error == NO_ERROR)
		{
		  xtran_server_commit (thread_p, false);
		  er_log_debug (ARG_FILE_LINE,
				"Successfully retried and deleted gtrid=%d bqual=%d after %d attempts",
				entry->gtrid, entry->participant.bqual, entry->retry_count);
		}
	      else
		{
		  xtran_server_abort (thread_p);
		}

	      logtb_free_tran_index (thread_p, tran_index);
	      LOG_SET_CURRENT_TRAN_INDEX (thread_p, old_tran_index);
	    }

	  if (error == NO_ERROR)
	    {
	      /* Remove from retry queue */
	      dblink_2pc_remove_from_retry_queue (i);
	      /* Don't increment i since we removed the current entry */
	      continue;
	    }
	}

      /* Failed - keep in retry queue */
      i++;
    }

  pthread_mutex_unlock (&dblink_2pc_Coordinator_daemon.retry_queue.mutex);
}

/*
 * dblink_2pc_coordinator_daemon_thread() - Main coordinator daemon thread function
 *   return: NULL
 *   arg(in): Thread argument (unused)
 */
static void *
dblink_2pc_coordinator_daemon_thread (void *arg)
{
  THREAD_ENTRY *thread_p;
  GTRAN_2PC_REQUEST *req;
  int error;

  /* Get thread entry */
  thread_p = thread_get_thread_entry_info ();
  if (thread_p == NULL)
    {
      return NULL;
    }

  /* Set thread type */
  thread_p->type = TT_DAEMON;
  thread_p->event_stats.trace_slow_query = false;

  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_NOTIFICATION_SEVERITY, 0, "2PC Coordinator daemon started");

  /* Main loop */
  while (dblink_2pc_Coordinator_daemon.is_running)
    {
      /* Dequeue request with timeout to allow retry processing */
      req = dblink_2pc_dequeue_request (GTRAN_2PC_RETRY_INTERVAL_MS);

      if (req != NULL)
	{
	  /* Check for shutdown request */
	  if (req->req_type == GTRAN_2PC_REQ_SHUTDOWN)
	    {
	      dblink_2pc_send_ack (req, NO_ERROR);
	      break;
	    }

	  /* Process request */
	  error = dblink_2pc_process_request (thread_p, req);

	  /* Send ACK */
	  dblink_2pc_send_ack (req, error);
	}

      /* Process retry queue (even if no new request) */
      dblink_2pc_process_retry_queue (thread_p);
    }

  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_NOTIFICATION_SEVERITY, 0, "2PC Coordinator daemon stopped");

  return NULL;
}

/*
 * dblink_2pc_process_request() - Process a coordinator request
 *   return: Error code
 *   thread_p(in): Thread entry
 *   req(in): Request to process
 */
static int
dblink_2pc_process_request (THREAD_ENTRY * thread_p, GTRAN_2PC_REQUEST * req)
{
  int error = NO_ERROR;

  switch (req->req_type)
    {
    case GTRAN_2PC_REQ_INSERT:
      error = dblink_2pc_handle_insert_request (thread_p, req);
      break;

    case GTRAN_2PC_REQ_UPDATE:
      error = dblink_2pc_handle_update_request (thread_p, req);
      break;

    case GTRAN_2PC_REQ_COMMIT:
      error = dblink_2pc_handle_commit_request (thread_p, req);
      break;

    case GTRAN_2PC_REQ_ABORT:
      error = dblink_2pc_handle_abort_request (thread_p, req);
      break;

    default:
      error = ER_FAILED;
      break;
    }

  return error;
}

/*
 * dblink_2pc_handle_insert_request() - Handle INSERT request (log START state)
 *   return: Error code
 *   thread_p(in): Thread entry
 *   req(in): Request
 */
static int
dblink_2pc_handle_insert_request (THREAD_ENTRY * thread_p, GTRAN_2PC_REQUEST * req)
{
  int error = NO_ERROR;
  int tran_index;
  int old_tran_index;
  TRAN_STATE tran_state;

  /* Save current transaction index */
  old_tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  /* Assign a new transaction index for catalog operation */
  tran_index = logtb_assign_tran_index (thread_p, NULL_TRANID, TRAN_ACTIVE,
					NULL, &tran_state, TRAN_LOCK_INFINITE_WAIT, TRAN_DEFAULT_ISOLATION_LEVEL ());
  if (tran_index < 0)
    {
      return ER_FAILED;
    }

  /* Insert log entry with state 'S' (Started) */
  error = dblink_2pc_log_insert (thread_p, req->gtrid, &req->participant, GTRAN_2PC_STATE_STARTED);
  if (error != NO_ERROR)
    {
      xtran_server_abort (thread_p);
      logtb_free_tran_index (thread_p, tran_index);
      LOG_SET_CURRENT_TRAN_INDEX (thread_p, old_tran_index);
      return error;
    }

  /* Commit the catalog transaction */
  error = xtran_server_commit (thread_p, false);

  /* Free transaction index */
  logtb_free_tran_index (thread_p, tran_index);

  /* Restore original transaction index */
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, old_tran_index);

  return error;
}

/*
 * dblink_2pc_handle_update_request() - Handle UPDATE request (log PREPARING state)
 *   return: Error code
 *   thread_p(in): Thread entry
 *   req(in): Request
 */
static int
dblink_2pc_handle_update_request (THREAD_ENTRY * thread_p, GTRAN_2PC_REQUEST * req)
{
  int error = NO_ERROR;
  int tran_index;
  int old_tran_index;
  TRAN_STATE tran_state;

  /* Save current transaction index */
  old_tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  /* Assign a new transaction index for catalog operation */
  tran_index = logtb_assign_tran_index (thread_p, NULL_TRANID, TRAN_ACTIVE,
					NULL, &tran_state, TRAN_LOCK_INFINITE_WAIT, TRAN_DEFAULT_ISOLATION_LEVEL ());
  if (tran_index < 0)
    {
      return ER_FAILED;
    }

  /* Update log entry with new state */
  error = dblink_2pc_log_update_state (thread_p, req->gtrid, req->participant.bqual, req->state);
  if (error != NO_ERROR)
    {
      xtran_server_abort (thread_p);
      logtb_free_tran_index (thread_p, tran_index);
      LOG_SET_CURRENT_TRAN_INDEX (thread_p, old_tran_index);
      return error;
    }

  /* Commit the catalog transaction */
  error = xtran_server_commit (thread_p, false);

  /* Free transaction index */
  logtb_free_tran_index (thread_p, tran_index);

  /* Restore original transaction index */
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, old_tran_index);

  return error;
}

/*
 * dblink_2pc_handle_commit_request() - Handle COMMIT decision
 *   return: Error code
 *   thread_p(in): Thread entry
 *   req(in): Request
 */
static int
dblink_2pc_handle_commit_request (THREAD_ENTRY * thread_p, GTRAN_2PC_REQUEST * req)
{
  int error = NO_ERROR;
  int tran_index;
  int old_tran_index;
  TRAN_STATE tran_state;

  /* Save current transaction index */
  old_tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  /* Assign a new transaction index for catalog operation */
  tran_index = logtb_assign_tran_index (thread_p, NULL_TRANID, TRAN_ACTIVE,
					NULL, &tran_state, TRAN_LOCK_INFINITE_WAIT, TRAN_DEFAULT_ISOLATION_LEVEL ());
  if (tran_index < 0)
    {
      return ER_FAILED;
    }

  /* Update state to 'C' (Commit decision) */
  error = dblink_2pc_log_update_state (thread_p, req->gtrid, req->participant.bqual, GTRAN_2PC_STATE_COMMIT);
  if (error != NO_ERROR)
    {
      xtran_server_abort (thread_p);
      logtb_free_tran_index (thread_p, tran_index);
      LOG_SET_CURRENT_TRAN_INDEX (thread_p, old_tran_index);
      return error;
    }

  /* Commit the catalog transaction */
  error = xtran_server_commit (thread_p, false);
  if (error != NO_ERROR)
    {
      logtb_free_tran_index (thread_p, tran_index);
      LOG_SET_CURRENT_TRAN_INDEX (thread_p, old_tran_index);
      return error;
    }

  /* Free transaction index */
  logtb_free_tran_index (thread_p, tran_index);

  /* Restore original transaction index */
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, old_tran_index);

  /* Try to send COMMIT to all participants via dblink */
  dblink_2pc_end_tran (thread_p, req->gtrid, 1, true, &req->participant);
  if (error != NO_ERROR)
    {
      /* Failed - add to retry queue for background processing */
      er_log_debug (ARG_FILE_LINE,
		    "Failed to send commit to participant gtrid=%d bqual=%d, adding to retry queue",
		    req->gtrid, req->participant.bqual);
      dblink_2pc_add_to_retry_queue (req->gtrid, GTRAN_2PC_REQ_COMMIT, &req->participant);
      /* Return success to unblock the client transaction */
      return NO_ERROR;
    }

  /* Success - delete catalog entry immediately */
  old_tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  tran_index = logtb_assign_tran_index (thread_p, NULL_TRANID, TRAN_ACTIVE,
					NULL, &tran_state, TRAN_LOCK_INFINITE_WAIT, TRAN_DEFAULT_ISOLATION_LEVEL ());
  if (tran_index < 0)
    {
      /* Failed to get transaction - add to retry queue for cleanup */
      dblink_2pc_add_to_retry_queue (req->gtrid, GTRAN_2PC_REQ_COMMIT, &req->participant);
      return NO_ERROR;
    }

  /* Delete the log entry after successful commit */
  error = dblink_2pc_log_delete (thread_p, req->gtrid, req->participant.bqual);
  if (error != NO_ERROR)
    {
      xtran_server_abort (thread_p);
      logtb_free_tran_index (thread_p, tran_index);
      LOG_SET_CURRENT_TRAN_INDEX (thread_p, old_tran_index);
      /* Add to retry queue for cleanup */
      dblink_2pc_add_to_retry_queue (req->gtrid, GTRAN_2PC_REQ_COMMIT, &req->participant);
      return NO_ERROR;
    }

  /* Commit the delete transaction */
  error = xtran_server_commit (thread_p, false);

  /* Free transaction index */
  logtb_free_tran_index (thread_p, tran_index);

  /* Restore original transaction index */
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, old_tran_index);

  return error;
}

/*
 * dblink_2pc_handle_abort_request() - Handle ABORT decision
 *   return: Error code
 *   thread_p(in): Thread entry
 *   req(in): Request
 */
static int
dblink_2pc_handle_abort_request (THREAD_ENTRY * thread_p, GTRAN_2PC_REQUEST * req)
{
  int error = NO_ERROR;
  int tran_index;
  int old_tran_index;
  TRAN_STATE tran_state;

  /* Save current transaction index */
  old_tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  /* Assign a new transaction index for catalog operation */
  tran_index = logtb_assign_tran_index (thread_p, NULL_TRANID, TRAN_ACTIVE,
					NULL, &tran_state, TRAN_LOCK_INFINITE_WAIT, TRAN_DEFAULT_ISOLATION_LEVEL ());
  if (tran_index < 0)
    {
      return ER_FAILED;
    }

  /* Update state to 'A' (Abort decision) */
  error = dblink_2pc_log_update_state (thread_p, req->gtrid, req->participant.bqual, GTRAN_2PC_STATE_ABORT);
  if (error != NO_ERROR)
    {
      xtran_server_abort (thread_p);
      logtb_free_tran_index (thread_p, tran_index);
      LOG_SET_CURRENT_TRAN_INDEX (thread_p, old_tran_index);
      return error;
    }

  /* Commit the catalog transaction */
  error = xtran_server_commit (thread_p, false);
  if (error != NO_ERROR)
    {
      logtb_free_tran_index (thread_p, tran_index);
      LOG_SET_CURRENT_TRAN_INDEX (thread_p, old_tran_index);
      return error;
    }

  /* Free transaction index */
  logtb_free_tran_index (thread_p, tran_index);

  /* Restore original transaction index */
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, old_tran_index);

  /* Try to send ABORT to all participants via dblink */
  dblink_2pc_end_tran (thread_p, req->gtrid, 1, false, &req->participant);
  if (error != NO_ERROR)
    {
      /* Failed - add to retry queue for background processing */
      er_log_debug (ARG_FILE_LINE,
		    "Failed to send abort to participant gtrid=%d bqual=%d, adding to retry queue",
		    req->gtrid, req->participant.bqual);
      dblink_2pc_add_to_retry_queue (req->gtrid, GTRAN_2PC_REQ_ABORT, &req->participant);
      /* Return success to unblock the client transaction */
      return NO_ERROR;
    }

  /* Success - delete catalog entry immediately */
  old_tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  tran_index = logtb_assign_tran_index (thread_p, NULL_TRANID, TRAN_ACTIVE,
					NULL, &tran_state, TRAN_LOCK_INFINITE_WAIT, TRAN_DEFAULT_ISOLATION_LEVEL ());
  if (tran_index < 0)
    {
      /* Failed to get transaction - add to retry queue for cleanup */
      dblink_2pc_add_to_retry_queue (req->gtrid, GTRAN_2PC_REQ_ABORT, &req->participant);
      return NO_ERROR;
    }

  /* Delete the log entry after successful abort */
  error = dblink_2pc_log_delete (thread_p, req->gtrid, req->participant.bqual);
  if (error != NO_ERROR)
    {
      xtran_server_abort (thread_p);
      logtb_free_tran_index (thread_p, tran_index);
      LOG_SET_CURRENT_TRAN_INDEX (thread_p, old_tran_index);
      /* Add to retry queue for cleanup */
      dblink_2pc_add_to_retry_queue (req->gtrid, GTRAN_2PC_REQ_ABORT, &req->participant);
      return NO_ERROR;
    }

  /* Commit the delete transaction */
  error = xtran_server_commit (thread_p, false);

  /* Free transaction index */
  logtb_free_tran_index (thread_p, tran_index);

  /* Restore original transaction index */
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, old_tran_index);

  return error;
}

/*
 * dblink_2pc_start_coordinator_daemon() - Start coordinator daemon thread
 *   return: Error code
 */
int
dblink_2pc_start_coordinator_daemon (void)
{
  int error;

  if (dblink_2pc_Coordinator_daemon.is_running)
    {
      return NO_ERROR;		/* Already running */
    }

  /* Initialize queue */
  error = dblink_2pc_initialize_queue ();
  if (error != NO_ERROR)
    {
      return error;
    }

  /* Initialize retry queue */
  error = dblink_2pc_initialize_retry_queue ();
  if (error != NO_ERROR)
    {
      dblink_2pc_destroy_queue ();
      return error;
    }

  /* Set running flag */
  dblink_2pc_Coordinator_daemon.is_running = true;

  /* Create daemon thread */
  if (pthread_create (&dblink_2pc_Coordinator_daemon.daemon_thread, NULL,
		      dblink_2pc_coordinator_daemon_thread, NULL) != 0)
    {
      dblink_2pc_Coordinator_daemon.is_running = false;
      dblink_2pc_destroy_retry_queue ();
      dblink_2pc_destroy_queue ();
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * dblink_2pc_stop_coordinator_daemon() - Stop coordinator daemon thread
 *   return: Error code
 */
int
dblink_2pc_stop_coordinator_daemon (void)
{
  GTRAN_2PC_REQUEST *shutdown_req;

  if (!dblink_2pc_Coordinator_daemon.is_running)
    {
      return NO_ERROR;		/* Already stopped */
    }

  /* Create shutdown request */
  shutdown_req = (GTRAN_2PC_REQUEST *) malloc (sizeof (GTRAN_2PC_REQUEST));
  if (shutdown_req == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  memset (shutdown_req, 0, sizeof (GTRAN_2PC_REQUEST));
  shutdown_req->req_type = GTRAN_2PC_REQ_SHUTDOWN;
  pthread_mutex_init (&shutdown_req->ack_mutex, NULL);
  pthread_cond_init (&shutdown_req->ack_cond, NULL);
  shutdown_req->ack_received = false;

  /* Enqueue shutdown request and wait for ACK */
  dblink_2pc_enqueue_request (shutdown_req, true);

  /* Wait for daemon thread to finish */
  pthread_join (dblink_2pc_Coordinator_daemon.daemon_thread, NULL);

  /* Cleanup */
  pthread_mutex_destroy (&shutdown_req->ack_mutex);
  pthread_cond_destroy (&shutdown_req->ack_cond);
  free (shutdown_req);

  dblink_2pc_destroy_retry_queue ();
  dblink_2pc_destroy_queue ();
  dblink_2pc_Coordinator_daemon.is_running = false;

  return NO_ERROR;
}

/*
 * dblink_2pc_create_request() - Create a new request
 *   return: Request pointer or NULL
 *   req_type(in): Request type
 *   gtrid(in): Global transaction ID
 *   trid(in): Transaction ID
 *   state(in): State
 *   participant(in): Participant information
 */
GTRAN_2PC_REQUEST *
dblink_2pc_create_request (int req_type, int gtrid, int trid, char state, const PARTICIPANT_INFO * participant)
{
  GTRAN_2PC_REQUEST *req;

  if (participant == NULL)
    {
      return NULL;
    }

  req = (GTRAN_2PC_REQUEST *) malloc (sizeof (GTRAN_2PC_REQUEST));
  if (req == NULL)
    {
      return NULL;
    }

  memset (req, 0, sizeof (GTRAN_2PC_REQUEST));

  req->req_type = req_type;
  req->gtrid = gtrid;
  req->trid = trid;
  req->state = state;

  /* Copy participant information */
  memcpy (&req->participant, participant, sizeof (PARTICIPANT_INFO));

  pthread_mutex_init (&req->ack_mutex, NULL);
  pthread_cond_init (&req->ack_cond, NULL);
  req->ack_received = false;
  req->result = NO_ERROR;

  return req;
}

/*
 * dblink_2pc_free_request() - Free request structure
 *   return: void
 *   req(in): Request to free
 */
void
dblink_2pc_free_request (GTRAN_2PC_REQUEST * req)
{
  if (req == NULL)
    {
      return;
    }

  pthread_mutex_destroy (&req->ack_mutex);
  pthread_cond_destroy (&req->ack_cond);

  free (req);
}
