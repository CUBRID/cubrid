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
 * Public defines for value array scans
 */

#ifndef _DBLINK_SCAN_H_
#define _DBLINK_SCAN_H_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "dbtype_def.h"
#include "storage_common.h"
#include "thread_compat.hpp"

typedef enum
{
  DBLINK_SUCCESS = 1,
  DBLINK_EOF,
  DBLINK_ERROR
} DBLINK_STATUS;

struct regu_variable_list_node;

typedef struct dblink_scan_buffer DBLINK_SCAN_BUFFER;
struct dblink_scan_buffer
{				/* value array scanbuf */
 int conn_handle;
 int stmt_handle;
 int col_cnt;
 void *col_info;
};

extern int dblink_open_scan (THREAD_ENTRY * thread_p, DBLINK_SCAN_BUFFER * scan_buffer_p,
			char *conn_url, char *user_name, char *password, char *sql_text);
extern int dblink_close_scan (THREAD_ENTRY * thread_p, DBLINK_SCAN_BUFFER * scan_buf);
extern SCAN_CODE dblink_scan_next (THREAD_ENTRY * thread_p, DBLINK_SCAN_BUFFER * scan_buffer_p, regu_variable_list_node * value_list_p);

#endif
