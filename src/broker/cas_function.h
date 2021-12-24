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
 * cas_function.h -
 */

#ifndef	_CAS_FUNCTION_H_
#define	_CAS_FUNCTION_H_

#ident "$Id$"

#include "cas_net_buf.h"

typedef enum
{
  FN_KEEP_CONN = 0,
  FN_CLOSE_CONN = -1,
  FN_KEEP_SESS = -2,
  FN_GRACEFUL_DOWN = -3
} FN_RETURN;

typedef FN_RETURN (*T_SERVER_FUNC) (SOCKET, int, void **, T_NET_BUF *, T_REQ_INFO *);

extern FN_RETURN fn_end_tran (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL) && !defined(CAS_FOR_CGW)
extern FN_RETURN fn_end_session (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_get_row_count (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_get_last_insert_id (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf,
					T_REQ_INFO * req_info);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL && !CAS_FOR_CGW */

extern FN_RETURN fn_prepare (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_execute (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL) && !defined(CAS_FOR_CGW)
extern FN_RETURN fn_get_db_parameter (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf,
				      T_REQ_INFO * req_info);
extern FN_RETURN fn_set_db_parameter (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf,
				      T_REQ_INFO * req_info);
extern FN_RETURN fn_set_cas_change_mode (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf,
					 T_REQ_INFO * req_info);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL && !CAS_FOR_CGW */
extern FN_RETURN fn_close_req_handle (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf,
				      T_REQ_INFO * req_info);
extern FN_RETURN fn_cursor (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_fetch (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL) && !defined(CAS_FOR_CGW)
extern FN_RETURN fn_schema_info (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL && !CAS_FOR_CGW */
extern FN_RETURN fn_get_db_version (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_next_result (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_execute_batch (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_execute_array (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_get_attr_type_str (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf,
				       T_REQ_INFO * req_info);
extern FN_RETURN fn_xa_prepare (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_xa_recover (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_xa_end_tran (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_con_close (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_check_cas (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_make_out_rs (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_get_generated_keys (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf,
					T_REQ_INFO * req_info);
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL) && !defined(CAS_FOR_CGW)
extern FN_RETURN fn_oid_get (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_oid_put (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_get_class_num_objs (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf,
					T_REQ_INFO * req_info);
extern FN_RETURN fn_oid (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_collection (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL && !CAS_FOR_CGW */

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
extern FN_RETURN fn_cursor_close (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

extern FN_RETURN fn_cursor_update (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_get_query_info (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL) && !defined(CAS_FOR_CGW)
extern FN_RETURN fn_savepoint (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_parameter_info (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL && !CAS_FOR_CGW */

extern FN_RETURN fn_not_supported (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL) && !defined(CAS_FOR_CGW)
extern FN_RETURN fn_lob_new (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_lob_write (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_lob_read (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);

extern FN_RETURN fn_deprecated (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);

extern FN_RETURN fn_prepare_and_execute (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf,
					 T_REQ_INFO * req_info);

#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL && !CAS_FOR_CGW */
#endif /* _CAS_FUNCTION_H_ */
