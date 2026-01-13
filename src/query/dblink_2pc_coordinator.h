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
 * transaction_2pc_coordinator.h - 2PC Coordinator Daemon Thread Header
 */

#ifndef _TRANSACTION_2PC_COORDINATOR_H_
#define _TRANSACTION_2PC_COORDINATOR_H_

#include <pthread.h>
#include <stdbool.h>
#include "dblink_2pc_log.h"

#ident "$Id$"

/*
 * TPC_REQUEST - Request structure for coordinator daemon
 */
typedef struct dblink_2pc_request TPC_REQUEST;
struct dblink_2pc_request
{
  int req_type;			/* Request type */
  int gtrid;			/* Global Transaction ID */
  int trid;			/* Transaction ID */
  char state;			/* State ('S', 'P', 'C', 'A') */
  PARTICIPANT_INFO participant;	/* Participant information */

  /* Synchronization for ACK */
  pthread_mutex_t ack_mutex;
  pthread_cond_t ack_cond;
  bool ack_received;
  int result;			/* Result code */
};

/* Request types */
#define TPC_REQ_INSERT      1
#define TPC_REQ_UPDATE      2
#define TPC_REQ_COMMIT      3
#define TPC_REQ_ABORT       4

/*
 * External function declarations for dblink 2PC operations
 * These should be implemented in the dblink module
 */
extern int dblink_2pc_send_commit (int gtrid, const PARTICIPANT_INFO * participant);
extern int dblink_2pc_send_abort (int gtrid, const PARTICIPANT_INFO * participant);

/*
 * Coordinator daemon management functions
 */

/* Start the coordinator daemon thread */
extern int dblink_2pc_start_coordinator_daemon (void);

/* Stop the coordinator daemon thread */
extern int dblink_2pc_stop_coordinator_daemon (void);

/*
 * Request management functions
 */

/* Create a new request */
extern TPC_REQUEST *dblink_2pc_create_request (int req_type, int gtrid,
					       int trid, char state, const PARTICIPANT_INFO * participant);

/* Enqueue a request to coordinator daemon */
extern int dblink_2pc_enqueue_request (TPC_REQUEST * req, bool wait_ack);

/* Free request structure */
extern void dblink_2pc_free_request (TPC_REQUEST * req);

#endif /* _TRANSACTION_2PC_COORDINATOR_H_ */
