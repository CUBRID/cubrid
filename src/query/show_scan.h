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
 * show_scan.h
 */
#ifndef _SHOW_SCAN_H_
#define _SHOW_SCAN_H_

#ident "$Id$"

#include "dbtype_def.h"
#include "thread_compat.hpp"

typedef struct showstmt_array_context SHOWSTMT_ARRAY_CONTEXT;
struct showstmt_array_context
{
  DB_VALUE **tuples;		/* tuples, each tuple is composed by DB_VALUE arrays */
  int num_cols;			/* columns count */
  int num_used;			/* used tuples count */
  int num_total;		/* total allocated tuples count */
};

extern SHOWSTMT_ARRAY_CONTEXT *showstmt_alloc_array_context (THREAD_ENTRY * thread_p, int num_capacity, int num_cols);
extern void showstmt_free_array_context (THREAD_ENTRY * thread_p, SHOWSTMT_ARRAY_CONTEXT * ctx);
extern DB_VALUE *showstmt_alloc_tuple_in_context (THREAD_ENTRY * thread_p, SHOWSTMT_ARRAY_CONTEXT * ctx);

extern int thread_start_scan (THREAD_ENTRY * thread_p, int type, DB_VALUE ** arg_values, int arg_cnt, void **ctx);

#endif /* _SHOW_SCAN_H_ */
