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
 * query_dump.h - Query processor printer
 */

#ifndef _QUERY_DUMP_H_
#define _QUERY_DUMP_H_

#include "dbtype_def.h"
#include "storage_common.h"

#include <cstdio>

// forward definitions
struct json_t;
struct xasl_node;

extern bool qdump_print_xasl (xasl_node * xasl);
#if defined (SERVER_MODE)
extern void qdump_print_stats_json (xasl_node * xasl_p, json_t * parent);
extern void qdump_print_stats_text (FILE * fp, xasl_node * xasl_p, int indent);
#endif /* SERVER_MODE */
extern const char *qdump_operator_type_string (OPERATOR_TYPE optype);
extern const char *qdump_default_expression_string (DB_DEFAULT_EXPR_TYPE default_expr_type);

#endif /* _QUERY_DUMP_H_ */
