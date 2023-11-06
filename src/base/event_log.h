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
 * event_log.h - event log module (server)
 */

#ifndef _EVENT_LOG_H_
#define _EVENT_LOG_H_

#ident "$Id$"

#include "query_list.h"
#include "thread_compat.hpp"

#include <stdio.h>

// forward declarations
struct clientids;

#define EVENT_EMPTY_QUERY "***EMPTY***"

extern void event_log_init (const char *db_name);
extern void event_log_final (void);
extern FILE *event_log_start (THREAD_ENTRY * thread_p, const char *event_name);
extern void event_log_end (THREAD_ENTRY * thread_p);
extern void event_log_print_client_info (int tran_index, int indent);
extern void event_log_sql_string (THREAD_ENTRY * thread_p, FILE * log_fp, XASL_ID * xasl_id, int indent);
extern void event_log_bind_values (THREAD_ENTRY * thread_p, FILE * log_fp, int tran_index, int bind_index);
extern void event_log_log_flush_thr_wait (THREAD_ENTRY * thread_p, int flush_count, clientids * client_info,
					  int flush_time, int flush_wait_time, int writer_time);
extern void event_log_sql_without_user_oid (FILE * fp, const char *format, int indent, const char *hash_text);
#endif /* _EVENT_LOG_H_ */
