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
 * cas_cgw_execute.h - 
 */

#ifndef _CAS_CGW_EXECUTE_H_
#define _CAS_CGW_EXECUTE_H_

#ident "$Id$"

#include "cas_common_execute.h"

/* ========================================================================
 * Function Declarations
 * ======================================================================== */
extern int ux_cgw_check_connection (void);
extern int ux_cgw_prepare (char *sql_stmt, int flag, char auto_commit_mode, T_NET_BUF * net_buf, T_REQ_INFO * req_info,
			   unsigned int query_seq_num);
extern int ux_cgw_end_tran (int tran_type, bool reset_con_status, bool ddl_audit_log);
extern int ux_cgw_auto_commit (T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern int ux_cgw_execute (T_SRV_HANDLE * srv_handle, char flag, int max_col_size, int max_row, int argc, void **argv,
			   T_NET_BUF *, T_REQ_INFO * req_info, CACHE_TIME * clt_cache_time, int *clt_cache_reusable);
extern int ux_cgw_fetch (T_SRV_HANDLE * srv_handle, int cursor_pos, int fetch_count, char fetch_flag,
			 int result_set_index, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern int ux_cgw_cursor (int srv_h_id, int offset, int origin, T_NET_BUF * net_buf);
extern void ux_cgw_cursor_close (T_SRV_HANDLE * srv_handle);
extern void ux_cgw_free_stmt (T_SRV_HANDLE * srv_handle);
extern int get_cgw_tuple_count (T_SRV_HANDLE * srv_handle);
extern int ux_cgw_is_database_connected (void);


#endif /* _CAS_CGW_EXECUTE_H_ */
