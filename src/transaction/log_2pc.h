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

//
// Log 2 phase commit
//

#ifndef _LOG_2PC_H_
#define _LOG_2PC_H_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Wrong module
#endif

#include "log_comm.h"
#include "log_lsa.hpp"
#include "log_storage.hpp"
#include "thread_compat.hpp"

#include <cstdio>

// forward declarations
struct log_tdes;

const int LOG_2PC_NULL_GTRID = -1;
const bool LOG_2PC_OBTAIN_LOCKS = true;
const bool LOG_2PC_DONT_OBTAIN_LOCKS = false;

enum log_2pc_execute
{
  LOG_2PC_EXECUTE_FULL,		/* For the root coordinator */
  LOG_2PC_EXECUTE_PREPARE,	/* For a participant that is also a non root coordinator execute the first phase of 2PC */
  LOG_2PC_EXECUTE_COMMIT_DECISION,	/* For a participant that is also a non root coordinator execute the second
					 * phase of 2PC. The root coordinator has decided a commit decision */
  LOG_2PC_EXECUTE_ABORT_DECISION	/* For a participant that is also a non root coordinator execute the second
					 * phase of 2PC. The root coordinator has decided an abort decision with or
					 * without going to the first phase (i.e., prepare) of the 2PC */
};
typedef enum log_2pc_execute LOG_2PC_EXECUTE;

typedef struct log_2pc_gtrinfo LOG_2PC_GTRINFO;
struct log_2pc_gtrinfo
{				/* Global transaction user information */
  int info_length;
  void *info_data;
};

typedef struct log_2pc_coordinator LOG_2PC_COORDINATOR;
struct log_2pc_coordinator
{				/* Coordinator maintains this info */
  int num_particps;		/* Number of participating sites */
  int particp_id_length;	/* Length of a participant identifier */
  void *block_particps_ids;	/* A block of participants identifiers */
  int *ack_received;		/* Acknowledgment received vector */
};

char *log_2pc_sprintf_particp (void *particp_id);
void log_2pc_dump_participants (FILE * fp, int block_length, void *block_particps_ids);
bool log_2pc_send_prepare (int gtrid, int num_particps, void *block_particps_ids);
bool log_2pc_send_commit_decision (int gtrid, int num_particps, int *particps_indices, void *block_particps_ids);
bool log_2pc_send_abort_decision (int gtrid, int num_particps, int *particps_indices, void *block_particps_ids,
				  bool collect);
TRAN_STATE log_2pc_commit (THREAD_ENTRY * thread_p, log_tdes * tdes, LOG_2PC_EXECUTE execute_2pc_type, bool * decision);
int log_2pc_set_global_tran_info (THREAD_ENTRY * thread_p, int gtrid, void *info, int size);
int log_2pc_get_global_tran_info (THREAD_ENTRY * thread_p, int gtrid, void *buffer, int size);
int log_2pc_start (THREAD_ENTRY * thread_p);
TRAN_STATE log_2pc_prepare (THREAD_ENTRY * thread_p);
int log_2pc_recovery_prepared (THREAD_ENTRY * thread_p, int gtrids[], int size);
int log_2pc_attach_global_tran (THREAD_ENTRY * thread_p, int gtrid);

TRAN_STATE log_2pc_prepare_global_tran (THREAD_ENTRY * thread_p, int gtrid);
void log_2pc_read_prepare (THREAD_ENTRY * thread_p, int acquire_locks, log_tdes * tdes, LOG_LSA * lsa,
			   LOG_PAGE * log_pgptr);
void log_2pc_dump_gtrinfo (FILE * fp, int length, void *data);
void log_2pc_dump_acqobj_locks (FILE * fp, int length, void *data);
log_tdes *log_2pc_alloc_coord_info (log_tdes * tdes, int num_particps, int particp_id_length, void *block_particps_ids);
void log_2pc_free_coord_info (log_tdes * tdes);
void log_2pc_recovery_analysis_info (THREAD_ENTRY * thread_p, log_tdes * tdes, LOG_LSA * upto_chain_lsa);
void log_2pc_recovery (THREAD_ENTRY * thread_p);
bool log_2pc_is_tran_distributed (log_tdes * tdes);
bool log_2pc_clear_and_is_tran_distributed (log_tdes * tdes);

#endif // !_LOG_2PC_H_
