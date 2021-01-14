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

#ifndef _LOG_COMM_H_
#define _LOG_COMM_H_

#ident "$Id$"

#include "dbtran_def.h"
#include "storage_common.h"

#include <stdio.h>

#define TRAN_LOCK_INFINITE_WAIT (-1)

#define TIME_SIZE_OF_DUMP_LOG_INFO 30

/*
 * STATES OF TRANSACTIONS
 */
typedef enum
{
  TRAN_RECOVERY,		/* State of a system transaction which is used for recovery purposes. For example , set
				 * lock for damaged pages. */
  TRAN_ACTIVE,			/* Active transaction */
  TRAN_UNACTIVE_COMMITTED,	/* Transaction is in the commit process or has been committed */
  TRAN_UNACTIVE_WILL_COMMIT,	/* Transaction will be committed */
  TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE,	/* Transaction has been committed, but it is still executing postpone
						 * operations */
  TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE,	/* In the process of executing postpone top system operations */
  TRAN_UNACTIVE_ABORTED,	/* Transaction is in the abort process or has been aborted */
  TRAN_UNACTIVE_UNILATERALLY_ABORTED,	/* Transaction was active a the time of a system crash. The transaction is
					 * unilaterally aborted by the system */
  TRAN_UNACTIVE_2PC_PREPARE,	/* Local part of the distributed transaction is ready to commit. (It will not be
				 * unilaterally aborted by the system) */

  TRAN_UNACTIVE_2PC_COLLECTING_PARTICIPANT_VOTES,	/* First phase of 2PC protocol. Transaction is collecting votes
							 * from participants */

  TRAN_UNACTIVE_2PC_ABORT_DECISION,	/* Second phase of 2PC protocol. Transaction needs to be aborted both locally
					 * and globally. */

  TRAN_UNACTIVE_2PC_COMMIT_DECISION,	/* Second phase of 2PC protocol. Transaction needs to be committed both locally
					 * and globally. */

  TRAN_UNACTIVE_COMMITTED_INFORMING_PARTICIPANTS,	/* Transaction has been committed, and it is informing
							 * participants about the decision. */
  TRAN_UNACTIVE_ABORTED_INFORMING_PARTICIPANTS,	/* Transaction has been aborted, and it is informing participants about
						 * the decision. */

  TRAN_UNACTIVE_UNKNOWN		/* Unknown state. */
} TRAN_STATE;

/*
 * RESULT OF NESTED TOP OPERATION
 */

typedef enum
{
  LOG_RESULT_TOPOP_COMMIT,
  LOG_RESULT_TOPOP_ABORT,
  LOG_RESULT_TOPOP_ATTACH_TO_OUTER
} LOG_RESULT_TOPOP;

/* name used by the internal modules */
typedef DB_TRAN_ISOLATION TRAN_ISOLATION;

extern const int LOG_MIN_NBUFFERS;

extern const char *log_state_string (TRAN_STATE state);
extern const char *log_state_short_string (TRAN_STATE state);
extern const char *log_isolation_string (TRAN_ISOLATION isolation);
extern int log_dump_log_info (const char *logname_info, bool also_stdout, const char *fmt, ...);
extern bool log_does_allow_replication (void);

#endif /* _LOG_COMM_H_ */
