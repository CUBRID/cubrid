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
 * cas_execute.h -
 */

#ifndef	_CAS_EXECUTE_H_
#define	_CAS_EXECUTE_H_

#ident "$Id$"

#include "cas_common_execute.h"


extern int ux_check_connection (void);
extern int ux_database_connect (char *db_name, char *db_user, char *db_passwd, char **db_err_msg);
extern int ux_database_reconnect (void);
extern int ux_is_database_connected (void);
extern int ux_prepare (char *sql_stmt, int flag, char auto_commit_mode, T_NET_BUF * ne_buf, T_REQ_INFO * req_info,
		       unsigned int query_seq_num);
extern int ux_end_tran (int tran_type, bool reset_con_status, bool ddl_audit_log);
extern int ux_end_session (void);
extern int ux_get_row_count (T_NET_BUF * net_buf);
extern int ux_get_last_insert_id (T_NET_BUF * net_buf);
extern int ux_auto_commit (T_NET_BUF * CAS_FN_ARG_NET_BUF, T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int ux_execute (T_SRV_HANDLE * srv_handle, char flag, int max_col_size, int max_row, int argc, void **argv,
		       T_NET_BUF *, T_REQ_INFO * req_info, CACHE_TIME * clt_cache_time, int *clt_cache_reusable);
extern void ux_get_tran_setting (int *lock_wait, int *isol_level);
extern int ux_set_isolation_level (int isol_level, T_NET_BUF * net_buf);
extern void ux_set_lock_timeout (int lock_timeout);
extern void ux_set_cas_change_mode (int mode, T_NET_BUF * net_buf);
extern int ux_fetch (T_SRV_HANDLE * srv_handle, int cursor_pos, int fetch_count, char fetch_flag, int result_set_index,
		     T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern int ux_oid_get (int argc, void **argv, T_NET_BUF * net_buf);
extern int ux_cursor (int srv_h_id, int offset, int origin, T_NET_BUF * net_buf);
extern void ux_database_shutdown (bool request_server);
extern int ux_get_class_num_objs (char *class_name, int flag, T_NET_BUF * net_buf);
extern void ux_col_get (DB_COLLECTION * col, char col_type, char ele_type, DB_DOMAIN * ele_domain, T_NET_BUF * net_buf);
extern void ux_col_size (DB_COLLECTION * col, T_NET_BUF * net_buf);
extern int ux_col_set_drop (DB_COLLECTION * col, DB_VALUE * ele_val, T_NET_BUF * net_buf);
extern int ux_col_set_add (DB_COLLECTION * col, DB_VALUE * ele_val, T_NET_BUF * net_buf);
extern int ux_col_seq_drop (DB_COLLECTION * col, int index, T_NET_BUF * net_buf);
extern int ux_col_seq_insert (DB_COLLECTION * col, int index, DB_VALUE * ele_val, T_NET_BUF * net_buf);
extern int ux_col_seq_put (DB_COLLECTION * col, int index, DB_VALUE * ele_val, T_NET_BUF * net_buf);
extern char get_set_domain (DB_DOMAIN * col, int *precision, short *scale, char *db_type, char *charset);
extern int ux_next_result (T_SRV_HANDLE * srv_h_id, char flag, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern int ux_execute_all (T_SRV_HANDLE * srv_handle, char flag, int max_col_size, int max_row, int argc, void **argv,
			   T_NET_BUF * net_buf, T_REQ_INFO * req_info, CACHE_TIME * clt_cache_time,
			   int *clt_cache_reusable);
extern int ux_execute_array (T_SRV_HANDLE * srv_h_id, int argc, void **argv, T_NET_BUF * net_buf,
			     T_REQ_INFO * req_info);
extern int ux_execute_batch (int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info, char auto_commit_mode);
extern int ux_cursor_update (T_SRV_HANDLE * srv_handle, int cursor_pos, int argc, void **argv, T_NET_BUF * net_buf);
extern void ux_cursor_close (T_SRV_HANDLE * srv_handle);
extern int ux_oid_put (int argc, void **argv, T_NET_BUF * net_buf);
extern int make_bind_value (int num_bind, int argc, void **argv, DB_VALUE ** ret_val, T_NET_BUF * net_buf,
			    char desired_type);
extern int ux_get_attr_type_str (char *class_name, char *attr_name, T_NET_BUF * net_buf, T_REQ_INFO *);
extern int ux_get_query_info (int srv_h_id, char info_type, T_NET_BUF * net_buf);
extern int ux_get_parameter_info (int srv_h_id, T_NET_BUF * net_buf);
extern void ux_get_default_setting (void);
extern void ux_set_default_setting (void);
extern int ux_check_object (DB_OBJECT * obj, T_NET_BUF * net_buf);
extern int ux_schema_info (int schema_type, char *arg1, char *arg2, char flag, T_NET_BUF * net_buf,
			   T_REQ_INFO * req_info, unsigned int query_seq_num);
extern int ux_execute_call (T_SRV_HANDLE * srv_handle, char flag, int max_col_size, int max_row, int argc, void **argv,
			    T_NET_BUF * net_buf, T_REQ_INFO * req_info, CACHE_TIME * clt_cache_time,
			    int *clt_cache_reusable);
extern void ux_call_info_cp_param_mode (T_SRV_HANDLE * srv_handle, char *param_mode, int num_param);
extern int ux_make_out_rs (DB_BIGINT query_id, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern int ux_create_srv_handle_with_method_query_result (DB_QUERY_RESULT * result, int stmt_type, int num_column,
							  DB_QUERY_TYPE * column_info, bool is_holdable);
extern int ux_get_generated_keys (T_SRV_HANDLE * srv_handle, T_NET_BUF * net_buf);
extern SESSION_ID ux_get_session_id (void);
extern void ux_set_session_id (const SESSION_ID session_id);

extern int ux_lob_new (int lob_type, T_NET_BUF * net_buf);
extern int ux_lob_write (DB_VALUE * lob_dbval, int64_t offset, int size, char *data, T_NET_BUF * net_buf);
extern int ux_lob_read (DB_VALUE * lob_dbval, int64_t offset, int size, T_NET_BUF * net_buf);

extern int get_tuple_count (T_SRV_HANDLE * srv_handle);

#endif /* _CAS_EXECUTE_H_ */
