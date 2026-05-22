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
 * Public defines for value array scans
 */

#ifndef _DBLINK_2PC_H_
#define _DBLINK_2PC_H_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "thread_compat.hpp"

extern int dblink_2pc_get_participants (THREAD_ENTRY * thread_p, int *particp_id_length, void **block_particps_ids);
extern bool dblink_2pc_send_prepare (THREAD_ENTRY * thread_p, int gtrid, int num_partcps, void *block_particps_ids);
extern void dblink_2pc_end_tran (THREAD_ENTRY * thread_p, int gtrid, int num_particps, bool is_commit,
				 void *block_particps_ids);
extern void dblink_2pc_dump_participants (FILE * fp, int block_length, void *block_particps_ids);
/* For coordinator recovery: send commit/abort decision to one participant by reconnecting. */
extern int dblink_2pc_send_decision_one_participant (int gtrid, DBLINK_CONN_INFO * participant, bool is_commit);
#endif
