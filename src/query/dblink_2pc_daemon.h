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
 * 1) On startup: recovery - read _db_global_tran (state 'A'/'C'), send abort/commit decision, delete on success.
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

/* State for _db_global_tran: 'P' = before Prepare, 'A' = Abort decision, 'C' = Commit decision */
#define DBLINK_2PC_STATE_PREPARE   'P'
#define DBLINK_2PC_STATE_ABORT    'A'
#define DBLINK_2PC_STATE_COMMIT   'C'

/*
 * Enqueue participant data for daemon to persist to _db_global_tran and/or send decision.
 * - Before prepare: state = DBLINK_2PC_STATE_PREPARE -> daemon inserts (gtrid, bqual, conn_url, user, password, 'P').
 * - After prepare (decision phase): state = DBLINK_2PC_STATE_ABORT or DBLINK_2PC_STATE_COMMIT
 *   -> daemon updates state and sends abort/commit decision to each participant.
 * block_particps_ids is copied by the function; caller can free after return.
 * Returns NO_ERROR on success, ER_* on failure (e.g. queue full).
 */
extern int dblink_2pc_daemon_enqueue (int gtrid, char state, int num_participants, void *block_particps_ids);

/* Start the send_2pc_decision daemon thread. Called during server boot. */
extern void dblink_2pc_daemon_start (void);

/* Stop the daemon thread. Called during server shutdown. */
extern void dblink_2pc_daemon_stop (void);

/* Run recovery: read _db_global_tran (state 'A'/'C'), send decision, delete on success. Called by daemon on startup. */
extern void dblink_2pc_daemon_recovery (void);

/*
 * Run recovery using thread_p (catalog access). Call from log_recovery before daemon_start.
 * Scans _db_global_tran for state 'A'/'C', sends decision to each participant, deletes row on success.
 */
extern void dblink_2pc_daemon_recovery_with_thread (THREAD_ENTRY * thread_p);

#endif /* CCI_XA */

#endif /* _DBLINK_2PC_DAEMON_H_ */
