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
 * dblink_2pc_daemon.h - send_2pc_decision_daemon for coordinator recovery
 *
 * Daemon thread that:
 * 1) On startup: recovery - read _db_global_tran (state 'P'/'A'/'C'), send abort/commit decision, delete on success.
 * 2) Then: wait on global_tran_queue for participant data from coordinator.
 * 3) When data received: persist to _db_global_tran (insert 'P' or update 'A'/'C'), then send decision for 'A'/'C'.
 */

#ifndef _DBLINK_2PC_DAEMON_H_
#define _DBLINK_2PC_DAEMON_H_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif

#include "thread_compat.hpp"

#ifdef CCI_XA

#include "dblink_scan.h"

#include <pthread.h>

/* State for _db_global_tran: 'P' = before Prepare, 'A' = Abort decision, 'C' = Commit decision */
#define DBLINK_2PC_STATE_PREPARE   'P'
#define DBLINK_2PC_STATE_ABORT    'A'
#define DBLINK_2PC_STATE_COMMIT   'C'
#define DBLINK_2PC_STATE_EMPTY    ' '

/* How long the commit path waits for the 2PC daemon to deliver its decisions before answering the
 * client.  The wait is what makes the remote changes visible to the next statement of the same
 * session; the bound is what keeps a slow participant from stalling the commit response.  On
 * timeout the decision stays queued and the daemon keeps delivering it, which is exactly the
 * pre-existing asynchronous behaviour.  Deliberately generous: this is a safety net for a
 * participant that stopped responding, not a knob for trimming a slow one.  Tightening it makes
 * healthy-but-busy transactions fall back, which brings the visibility gap back. */
#define DBLINK_2PC_DECISION_WAIT_MSEC 1000

/*
 * Decision completion: one per coordinator transaction, shared by that transaction's queue entries.
 *
 * The commit path creates it with remaining = number of participants, enqueues one entry per
 * participant carrying a pointer to it, then waits for remaining to reach 0.  The daemon calls
 * dblink_2pc_completion_done() once per participant, right after the decision is actually delivered.
 *
 * Lifetime is refcounted because neither side reliably outlives the other: the commit path may give
 * up on the bound while entries are still queued, and an entry may be retried long after.  refcount
 * starts at 1 for the commit path; each queue entry adds one.  The last release frees it.
 */
typedef struct dblink_2pc_completion DBLINK_2PC_COMPLETION;
struct dblink_2pc_completion
{
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  int remaining;		/* participants whose decision is not delivered yet */
  int refcount;			/* commit path (1) + entries still referencing this completion */
};

typedef struct global_tran_queue_entry GLOBAL_TRAN_QUEUE_ENTRY;
struct global_tran_queue_entry
{
  int gtrid;
  char state;			/* DBLINK_2PC_STATE_PREPARE / ABORT / COMMIT */
  DBLINK_CONN_INFO participant;	/* single participant (embedded) */
  DBLINK_2PC_COMPLETION *completion;	/* commit path waiting on this decision, NULL if nobody waits */
};

/*
 * Ownership rule for the two counters.  Attaching the completion to a queue entry takes a reference
 * with _ref(); that entry is then settled with exactly one _done(), which drops both remaining and
 * the reference.  Concretely there are three cases, and only the first two settle an entry:
 *
 *   decision delivered      the daemon calls _done()
 *   delivery failed         nothing is called - the entry keeps its reference and its slot in
 *                           remaining, and is re-enqueued for retry as it is today
 *   never enqueued          the caller that took the reference calls _done() itself, so a queue
 *                           that refused the entry does not leave the commit path waiting for it
 *
 * Every function tolerates a NULL completion, so callers do not need to branch on allocation failure.
 *
 * Create a completion for num_participants decisions.  Returns NULL on allocation failure; that is not
 * an error condition - the caller simply does not wait, and the daemon still delivers the decision.
 */
extern DBLINK_2PC_COMPLETION *dblink_2pc_completion_create (int num_participants);

/* Take a reference before attaching the completion to a queue entry. */
extern void dblink_2pc_completion_ref (DBLINK_2PC_COMPLETION * completion);

/* Drop one reference; frees the completion when the last one goes away. */
extern void dblink_2pc_completion_unref (DBLINK_2PC_COMPLETION * completion);

/*
 * Settle one entry - see the three cases above.  Signals the completion when none are left, then consumes
 * that entry's reference, so the caller must not touch the completion afterwards.  Not called on a
 * failed delivery: that entry is retried and stays counted.
 */
extern void dblink_2pc_completion_done (DBLINK_2PC_COMPLETION * completion);

/*
 * Wait until every participant's decision has been settled or timeout_msec elapses.
 * Returns true if all were settled - which means there is nothing left to wait for, not that every
 * decision reached its participant: server shutdown settles the entries it drops.  A false return
 * is not an error either: the decisions remain queued and the daemon keeps delivering them.  Either
 * way the caller just stops waiting, which is why callers ignore the result.
 * A timeout_msec of 0 or less polls - it reports whether everything is already settled without
 * blocking.
 */
extern bool dblink_2pc_completion_wait (DBLINK_2PC_COMPLETION * completion, int timeout_msec);

/*
 * Enqueue one participant for daemon to persist to _db_global_tran and/or send decision.
 * Call once per participant (per gtrid/state) for efficiency: only failed participants are retried.
 * - Before prepare: state = DBLINK_2PC_STATE_PREPARE -> daemon inserts (gtrid, bqual, conn_url, user, password, 'P').
 * - After prepare (decision phase): state = DBLINK_2PC_STATE_ABORT or DBLINK_2PC_STATE_COMMIT
 *   -> daemon sends abort/commit decision to this participant.
 * participant is copied by the function; caller can free after return.
 * completion may be NULL.  When it is not, the caller must already hold a reference for this entry
 * (dblink_2pc_completion_ref); on failure the entry is not queued, so the caller settles that reference
 * itself with dblink_2pc_completion_done.
 * Returns NO_ERROR on success, ER_* on failure (e.g. queue full).
 */
extern int dblink_2pc_daemon_enqueue (int gtrid, char state, const DBLINK_CONN_INFO * participant,
				      DBLINK_2PC_COMPLETION * completion);
extern int dblink_2pc_daemon_dequeue (GLOBAL_TRAN_QUEUE_ENTRY * e);

/* Start the send_2pc_decision daemon thread. Called during server boot.
 * Returns NO_ERROR on success, ER_OUT_OF_VIRTUAL_MEMORY if queue alloc failed,
 * ER_FAILED if thread creation failed. On failure, caller should not proceed (e.g. fatal error).
 */
extern void dblink_2pc_daemon_init (void);

/* Stop the daemon thread. Called during server shutdown. */
extern void dblink_2pc_daemon_stop (void);

/*
 * Run recovery using thread_p (catalog access). Call from log_recovery before daemon_start.
 * Scans _db_global_tran for state 'A'/'C', sends decision to each participant, deletes row on success.
 */
extern void dblink_2pc_daemon_recovery_with_thread (THREAD_ENTRY * thread_p);

#endif /* CCI_XA */

#endif /* _DBLINK_2PC_DAEMON_H_ */
