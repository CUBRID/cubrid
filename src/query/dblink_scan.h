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
struct val_list_node;

typedef struct
{
  int count;
  int *index;
} DBLINK_HOST_VARS;

typedef struct dblink_scan_info DBLINK_SCAN_INFO;
struct dblink_scan_info
{
  int conn_handle;		/* connection handle for dblink */
  int stmt_handle;		/* statement handle for dblink */
  int col_cnt;			/* column count of dblink query result */
  char cursor;			/* cursor position T_CCI_CURSOR_POS */
  void *col_info;		/* column information T_CCI_COL_INFO */
};

extern int dblink_execute_query (struct access_spec_node *spec, VAL_DESCR * vd, DBLINK_HOST_VARS * host_vars);
extern int dblink_open_scan (DBLINK_SCAN_INFO * scan_info, struct access_spec_node *spec,
			     VAL_DESCR * vd, DBLINK_HOST_VARS * host_vars);
extern int dblink_close_scan (DBLINK_SCAN_INFO * scan_info);
extern SCAN_CODE dblink_scan_next (DBLINK_SCAN_INFO * scan_info, val_list_node * val_list);
extern SCAN_CODE dblink_scan_reset (DBLINK_SCAN_INFO * scan_info);

#endif
