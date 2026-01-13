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
 * transaction_2pc_catalog.h - 2PC Coordinator Catalog Interface Header
 */

#ifndef _TRANSACTION_2PC_CATALOG_H_
#define _TRANSACTION_2PC_CATALOG_H_

#include <time.h>

#ident "$Id$"

/* 
 * PARTICIPANT_INFO - Participant information structure
 */
typedef struct participant_info PARTICIPANT_INFO;
struct participant_info
{
  int bqual;			/* Branch Qualifier */
  char conn_url[512];		/* Connection URL */
  char user[32];		/* User name */
  char password[32];		/* Password (plain text in memory) */
};

/* 
 * GTRAN_2PC_LOG_ENTRY - Structure to hold 2PC transaction log entry
 */
typedef struct dblink_2pc_log_entry GTRAN_2PC_LOG_ENTRY;
struct dblink_2pc_log_entry
{
  int gtrid;			/* Global Transaction ID */
  int bqual;			/* Branch Qualifier */
  char conn_url[512];		/* Connection URL */
  char user[32];		/* User name */
  char password[64];		/* Encoded password */
  char state;			/* State: 'S', 'P', 'C', 'A' */
  time_t created_time;		/* Creation timestamp */
  time_t updated_time;		/* Last update timestamp */
};

/* State constants */
#define GTRAN_2PC_STATE_STARTED    'S'
#define GTRAN_2PC_STATE_PREPARE    'P'
#define GTRAN_2PC_STATE_COMMIT     'C'
#define GTRAN_2PC_STATE_ABORT      'A'

/*
 * Function prototypes
 */

/* Insert a new 2PC transaction log */
extern int dblink_2pc_log_insert (THREAD_ENTRY * thread_p, int gtrid, const PARTICIPANT_INFO * participant, char state);

/* Update state of existing log */
extern int dblink_2pc_log_update_state (THREAD_ENTRY * thread_p, int gtrid, int bqual, char new_state);

/* Read a log entry by GTRID and BQUAL */
extern int dblink_2pc_log_read (THREAD_ENTRY * thread_p, int gtrid, int bqual, GTRAN_2PC_LOG_ENTRY * log_entry);

/* Delete a log entry */
extern int dblink_2pc_log_delete (THREAD_ENTRY * thread_p, int gtrid, int bqual);

/* Password encoding/decoding functions */
extern int dblink_2pc_encode_password (const char *plain_password, char *encoded_password, int encoded_size);
extern int dblink_2pc_decode_password (const char *encoded_password, char *plain_password, int plain_size);

#endif /* _TRANSACTION_2PC_CATALOG_H_ */
