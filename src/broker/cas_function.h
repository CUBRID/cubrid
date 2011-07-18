/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
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
  FN_KEEP_SESS = -2
} FN_RETURN;

typedef FN_RETURN (*T_SERVER_FUNC) (SOCKET, int, void **, T_NET_BUF *,
				    T_REQ_INFO *);

extern FN_RETURN fn_end_tran (SOCKET sock_fd, int argc, void **argv,
			      T_NET_BUF * net_buf, T_REQ_INFO * req_info);
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
extern FN_RETURN fn_end_session (SOCKET sock_fd, int argc, void **argv,
				 T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_get_row_count (SOCKET sock_fd, int argc, void **argv,
				   T_NET_BUF * net_buf,
				   T_REQ_INFO * req_info);
extern FN_RETURN fn_get_last_insert_id (SOCKET sock_fd, int argc, void **argv,
					T_NET_BUF * net_buf,
					T_REQ_INFO * req_info);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

extern FN_RETURN fn_prepare (SOCKET sock_fd, int argc, void **argv,
			     T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_execute (SOCKET sock_fd, int argc, void **argv,
			     T_NET_BUF * net_buf, T_REQ_INFO * req_info);
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
extern FN_RETURN fn_get_db_parameter (SOCKET sock_fd, int argc, void **argv,
				      T_NET_BUF * net_buf,
				      T_REQ_INFO * req_info);
extern FN_RETURN fn_set_db_parameter (SOCKET sock_fd, int argc, void **argv,
				      T_NET_BUF * net_buf,
				      T_REQ_INFO * req_info);
#endif
extern FN_RETURN fn_close_req_handle (SOCKET sock_fd, int argc, void **argv,
				      T_NET_BUF * net_buf,
				      T_REQ_INFO * req_info);
extern FN_RETURN fn_cursor (SOCKET sock_fd, int argc, void **argv,
			    T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_fetch (SOCKET sock_fd, int argc, void **argv,
			   T_NET_BUF * net_buf, T_REQ_INFO * req_info);
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
extern FN_RETURN fn_schema_info (SOCKET sock_fd, int argc, void **argv,
				 T_NET_BUF * net_buf, T_REQ_INFO * req_info);
#endif
extern FN_RETURN fn_get_db_version (SOCKET sock_fd, int argc, void **argv,
				    T_NET_BUF * net_buf,
				    T_REQ_INFO * req_info);
extern FN_RETURN fn_next_result (SOCKET sock_fd, int argc, void **argv,
				 T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_execute_batch (SOCKET sock_fd, int argc, void **argv,
				   T_NET_BUF * net_buf,
				   T_REQ_INFO * req_info);
extern FN_RETURN fn_execute_array (SOCKET sock_fd, int argc, void **argv,
				   T_NET_BUF * net_buf,
				   T_REQ_INFO * req_info);
extern FN_RETURN fn_get_attr_type_str (SOCKET sock_fd, int argc, void **argv,
				       T_NET_BUF * net_buf,
				       T_REQ_INFO * req_info);
extern FN_RETURN fn_xa_prepare (SOCKET sock_fd, int argc, void **argv,
				T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_xa_recover (SOCKET sock_fd, int argc, void **argv,
				T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_xa_end_tran (SOCKET sock_fd, int argc, void **argv,
				 T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_con_close (SOCKET sock_fd, int argc, void **argv,
			       T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_check_cas (SOCKET sock_fd, int argc, void **argv,
			       T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_make_out_rs (SOCKET sock_fd, int argc, void **argv,
				 T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_get_generated_keys (SOCKET sock_fd, int argc, void **argv,
					T_NET_BUF * net_buf,
					T_REQ_INFO * req_info);
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
extern FN_RETURN fn_oid_get (SOCKET sock_fd, int argc, void **argv,
			     T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_oid_put (SOCKET sock_fd, int argc, void **argv,
			     T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_get_class_num_objs (SOCKET sock_fd, int argc, void **argv,
					T_NET_BUF * net_buf,
					T_REQ_INFO * req_info);
extern FN_RETURN fn_oid (SOCKET sock_fd, int argc, void **argv,
			 T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_collection (SOCKET sock_fd, int argc, void **argv,
				T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_cursor_close (SOCKET sock_fd, int argc, void **argv,
				  T_NET_BUF * net_buf, T_REQ_INFO * req_info);
#endif
extern FN_RETURN fn_cursor_update (SOCKET sock_fd, int argc, void **argv,
				   T_NET_BUF * net_buf,
				   T_REQ_INFO * req_info);
extern FN_RETURN fn_get_query_info (SOCKET sock_fd, int argc, void **argv,
				    T_NET_BUF * net_buf,
				    T_REQ_INFO * req_info);
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
extern FN_RETURN fn_savepoint (SOCKET sock_fd, int argc, void **argv,
			       T_NET_BUF * net_buf, T_REQ_INFO * req_info);
#endif
extern FN_RETURN fn_parameter_info (SOCKET sock_fd, int argc, void **argv,
				    T_NET_BUF * net_buf,
				    T_REQ_INFO * req_info);
#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
extern FN_RETURN fn_not_supported (SOCKET sock_fd, int argc, void **argv,
				   T_NET_BUF * net_buf,
				   T_REQ_INFO * req_info);
#endif

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
extern FN_RETURN fn_lob_new (SOCKET sock_fd, int argc, void **argv,
			     T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_lob_write (SOCKET sock_fd, int argc, void **argv,
			       T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern FN_RETURN fn_lob_read (SOCKET sock_fd, int argc, void **argv,
			      T_NET_BUF * net_buf, T_REQ_INFO * req_info);

extern FN_RETURN fn_deprecated (SOCKET sock_fd, int argc, void **argv,
				T_NET_BUF * net_buf, T_REQ_INFO * req_info);
#endif
#endif /* _CAS_FUNCTION_H_ */
