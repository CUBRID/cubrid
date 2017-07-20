/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

#ifndef _LOG_COMM_H_
#define _LOG_COMM_H_

#ident "$Id$"

#include <stdio.h>
#include "storage_common.h"
#include "dbdef.h"
#include "object_representation.h"

#define LOG_USERNAME_MAX        (DB_MAX_USER_LENGTH + 1)

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
