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

typedef int (*T_SERVER_FUNC) (int, int, void **, T_NET_BUF *, T_REQ_INFO *);

#define CAS_FUNC_PROTOTYPE(FUNC_NAME)	\
	int FUNC_NAME(int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC, void **CAS_FN_ARG_ARGV, T_NET_BUF *CAS_FN_ARG_NET_BUF, T_REQ_INFO *CAS_FN_ARG_REQ_INFO)
#define CAS_FN_ARG_SOCK_FD		sock_fd
#define CAS_FN_ARG_ARGC			argc
#define CAS_FN_ARG_ARGV			argv
#define CAS_FN_ARG_NET_BUF		net_buf
#define CAS_FN_ARG_REQ_INFO		req_info

extern CAS_FUNC_PROTOTYPE (fn_end_tran);
extern CAS_FUNC_PROTOTYPE (fn_prepare);
extern CAS_FUNC_PROTOTYPE (fn_execute);
extern CAS_FUNC_PROTOTYPE (fn_get_db_parameter);
extern CAS_FUNC_PROTOTYPE (fn_set_db_parameter);
extern CAS_FUNC_PROTOTYPE (fn_close_req_handle);
extern CAS_FUNC_PROTOTYPE (fn_cursor);
extern CAS_FUNC_PROTOTYPE (fn_fetch);
extern CAS_FUNC_PROTOTYPE (fn_schema_info);
extern CAS_FUNC_PROTOTYPE (fn_glo_new);
extern CAS_FUNC_PROTOTYPE (fn_glo_save);
extern CAS_FUNC_PROTOTYPE (fn_glo_load);
extern CAS_FUNC_PROTOTYPE (fn_get_db_version);
extern CAS_FUNC_PROTOTYPE (fn_next_result);
extern CAS_FUNC_PROTOTYPE (fn_execute_batch);
extern CAS_FUNC_PROTOTYPE (fn_execute_array);
extern CAS_FUNC_PROTOTYPE (fn_get_attr_type_str);
extern CAS_FUNC_PROTOTYPE (fn_xa_prepare);
extern CAS_FUNC_PROTOTYPE (fn_xa_recover);
extern CAS_FUNC_PROTOTYPE (fn_xa_end_tran);
extern CAS_FUNC_PROTOTYPE (fn_con_close);
extern CAS_FUNC_PROTOTYPE (fn_check_cas);
extern CAS_FUNC_PROTOTYPE (fn_make_out_rs);
extern CAS_FUNC_PROTOTYPE (fn_get_generated_keys);

extern CAS_FUNC_PROTOTYPE (fn_oid_get);
extern CAS_FUNC_PROTOTYPE (fn_oid_put);
extern CAS_FUNC_PROTOTYPE (fn_get_class_num_objs);
extern CAS_FUNC_PROTOTYPE (fn_oid);
extern CAS_FUNC_PROTOTYPE (fn_collection);
extern CAS_FUNC_PROTOTYPE (fn_cursor_update);
extern CAS_FUNC_PROTOTYPE (fn_get_query_info);
extern CAS_FUNC_PROTOTYPE (fn_savepoint);
extern CAS_FUNC_PROTOTYPE (fn_parameter_info);
extern CAS_FUNC_PROTOTYPE (fn_glo_cmd);

#endif /* _CAS_FUNCTION_H_ */
