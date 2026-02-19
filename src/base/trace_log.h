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
 * trace_log.h - trace log module (server)
 */

#ifndef _TRACE_LOG_H_
#define _TRACE_LOG_H_

#ident "$Id$"

#include "query_list.h"
#include "thread_compat.hpp"

#include <stdio.h>

// forward declarations
struct clientids;

#define EVENT_EMPTY_QUERY "***EMPTY***"

extern void trace_log_init (const char *db_name);
extern void trace_log_final (void);
extern FILE *trace_log_start (THREAD_ENTRY * thread_p, const char *event_name);
extern void trace_log_end (THREAD_ENTRY * thread_p);
extern void trace_log_print_client_info (int tran_index, int indent);
extern void trace_log_bind_values (THREAD_ENTRY * thread_p, FILE * log_fp, int tran_index, int bind_index);

#define TRACE_LOG_LEVEL_OFF     0
#define TRACE_LOG_LEVEL_SIMPLE  1
#define TRACE_LOG_LEVEL_DETAIL  2

#endif /* _EVENT_LOG_H_ */
