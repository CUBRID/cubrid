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

#include "cas.h"
#include "cas_net_buf.h"
#include "cas_handle.h"
#if defined(CAS_FOR_ORACLE)
#include "cas_oracle.h"
#elif defined(CAS_FOR_MYSQL)
#include "cas_mysql.h"
#else /* CAS_FOR_MYSQL */
#include "cas_db_inc.h"
#endif /* CAS_FOR_ORACLE */

#define CAS_TYPE_SET(TYPE)		((TYPE) | CCI_CODE_SET)
#define CAS_TYPE_MULTISET(TYPE)		((TYPE) | CCI_CODE_MULTISET)
#define CAS_TYPE_SEQUENCE(TYPE)		((TYPE) | CCI_CODE_SEQUENCE)

#define CAS_TYPE_COLLECTION(DB_TYPE, SET_TYPE)		\
	(((DB_TYPE) == DB_TYPE_SET) ? (CAS_TYPE_SET(SET_TYPE)) : \
	(((DB_TYPE) == DB_TYPE_MULTISET) ? (CAS_TYPE_MULTISET(SET_TYPE)) : \
	(CAS_TYPE_SEQUENCE(SET_TYPE))))

#define IS_ERROR_INFO_SET() is_error_info_set()
#define ERROR_INFO_SET(ERR_CODE, ERR_INDICATOR)\
	error_info_set(ERR_CODE, ERR_INDICATOR, __FILE__, __LINE__)
#define ERROR_INFO_SET_FORCE(ERR_CODE, ERR_INDICATOR)\
	error_info_set_force(ERR_CODE, ERR_INDICATOR, __FILE__, __LINE__)
#define ERROR_INFO_SET_WITH_MSG(ERR_CODE, ERR_INDICATOR, ERR_MSG)\
	error_info_set_with_msg(ERR_CODE, ERR_INDICATOR, ERR_MSG, false, __FILE__, __LINE__)
#define NET_BUF_ERR_SET(NET_BUF)	\
	err_msg_set(NET_BUF, __FILE__, __LINE__)

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
typedef struct t_fk_info_result T_FK_INFO_RESULT;
struct t_fk_info_result
{
  struct t_fk_info_result *prev;
  struct t_fk_info_result *next;

  char *pktable_name;
  char *pkcolumn_name;
  char *fktable_name;
  char *fkcolumn_name;
  short key_seq;
  SM_FOREIGN_KEY_ACTION update_action;
  SM_FOREIGN_KEY_ACTION delete_action;
  char *fk_name;
  char *pk_name;
};
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

extern int ux_check_connection (void);

extern int ux_database_connect (char *db_name, char *db_user, char *db_passwd, char **db_err_msg);
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
extern int ux_database_reconnect (void);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

extern int ux_is_database_connected (void);
#if defined(CAS_FOR_CGW)
extern int ux_cgw_prepare (char *sql_stmt, int flag, char auto_commit_mode, T_NET_BUF * net_buf, T_REQ_INFO * req_info,
			   unsigned int query_seq_num);
#else
extern int ux_prepare (char *sql_stmt, int flag, char auto_commit_mode, T_NET_BUF * ne_buf, T_REQ_INFO * req_info,
		       unsigned int query_seq_num);
#endif /* CAS_FOR_CGW */
extern int ux_end_tran (int tran_type, bool reset_con_status);
extern int ux_end_session (void);
extern int ux_get_row_count (T_NET_BUF * net_buf);
extern int ux_get_last_insert_id (T_NET_BUF * net_buf);
extern int ux_auto_commit (T_NET_BUF * CAS_FN_ARG_NET_BUF, T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
#if defined(CAS_FOR_CGW)
extern int ux_cgw_execute (T_SRV_HANDLE * srv_handle, char flag, int max_col_size, int max_row, int argc, void **argv,
			   T_NET_BUF *, T_REQ_INFO * req_info, CACHE_TIME * clt_cache_time, int *clt_cache_reusable);
#else
extern int ux_execute (T_SRV_HANDLE * srv_handle, char flag, int max_col_size, int max_row, int argc, void **argv,
		       T_NET_BUF *, T_REQ_INFO * req_info, CACHE_TIME * clt_cache_time, int *clt_cache_reusable);
#endif /* CAS_FOR_CGW */
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
extern void ux_get_tran_setting (int *lock_wait, int *isol_level);
extern int ux_set_isolation_level (int isol_level, T_NET_BUF * net_buf);
extern void ux_set_lock_timeout (int lock_timeout);
extern void ux_set_cas_change_mode (int mode, T_NET_BUF * net_buf);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
extern int ux_fetch (T_SRV_HANDLE * srv_handle, int cursor_pos, int fetch_count, char fetch_flag, int result_set_index,
		     T_NET_BUF * net_buf, T_REQ_INFO * req_info);
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
extern int ux_oid_get (int argc, void **argv, T_NET_BUF * net_buf);
extern int ux_cursor (int srv_h_id, int offset, int origin, T_NET_BUF * net_buf);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
extern void ux_database_shutdown (void);
extern int ux_get_db_version (T_NET_BUF * net_buf, T_REQ_INFO * req_info);
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
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
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
extern int ux_execute_all (T_SRV_HANDLE * srv_handle, char flag, int max_col_size, int max_row, int argc, void **argv,
			   T_NET_BUF * net_buf, T_REQ_INFO * req_info, CACHE_TIME * clt_cache_time,
			   int *clt_cache_reusable);
extern int ux_execute_array (T_SRV_HANDLE * srv_h_id, int argc, void **argv, T_NET_BUF * net_buf,
			     T_REQ_INFO * req_info);
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
extern int ux_execute_batch (int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info, char auto_commit_mode);
extern int ux_cursor_update (T_SRV_HANDLE * srv_handle, int cursor_pos, int argc, void **argv, T_NET_BUF * net_buf);
extern void ux_cursor_close (T_SRV_HANDLE * srv_handle);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
extern int ux_oid_put (int argc, void **argv, T_NET_BUF * net_buf);
extern int make_bind_value (int num_bind, int argc, void **argv, DB_VALUE ** ret_val, T_NET_BUF * net_buf,
			    char desired_type);

extern int ux_get_attr_type_str (char *class_name, char *attr_name, T_NET_BUF * net_buf, T_REQ_INFO *);
extern int ux_get_query_info (int srv_h_id, char info_type, T_NET_BUF * net_buf);
extern int ux_get_parameter_info (int srv_h_id, T_NET_BUF * net_buf);
extern void ux_get_default_setting (void);
extern void ux_get_system_parameter (const char *param, bool * value);
extern void ux_set_default_setting (void);
extern int ux_check_object (DB_OBJECT * obj, T_NET_BUF * net_buf);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
extern void ux_free_result (void *res);
#if defined(CAS_FOR_MYSQL)
extern char ux_db_type_to_cas_type (int db_type, unsigned int flags, T_BROKER_VERSION client_version);
#else /* CAS_FOR_MYSQL */
extern char ux_db_type_to_cas_type (int db_type);
#endif /* !CAS_FOR_MYSQL */

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
extern void ux_set_utype_for_enum (char u_type);
extern void ux_set_utype_for_timestamptz (char u_type);
extern void ux_set_utype_for_datetimetz (char u_type);
extern void ux_set_utype_for_timestampltz (char u_type);
extern void ux_set_utype_for_datetimeltz (char u_type);
extern void ux_set_utype_for_json (char u_type);
extern int ux_schema_info (int schema_type, char *arg1, char *arg2, char flag, T_NET_BUF * net_buf,
			   T_REQ_INFO * req_info, unsigned int query_seq_num);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
extern void ux_prepare_call_info_free (T_PREPARE_CALL_INFO * call_info);
extern int ux_execute_call (T_SRV_HANDLE * srv_handle, char flag, int max_col_size, int max_row, int argc, void **argv,
			    T_NET_BUF * net_buf, T_REQ_INFO * req_info, CACHE_TIME * clt_cache_time,
			    int *clt_cache_reusable);
#if !defined(CAS_FOR_MYSQL)
extern void ux_call_info_cp_param_mode (T_SRV_HANDLE * srv_handle, char *param_mode, int num_param);

extern int ux_make_out_rs (DB_BIGINT query_id, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern int ux_create_srv_handle_with_method_query_result (DB_QUERY_RESULT * result, int stmt_type, int num_column,
							  DB_QUERY_TYPE * column_info, bool is_holdable);
#endif /* !CAS_FOR_MYSQL */


#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
extern int ux_get_generated_keys (T_SRV_HANDLE * srv_handle, T_NET_BUF * net_buf);

extern SESSION_ID ux_get_session_id (void);
extern void ux_set_session_id (const SESSION_ID session_id);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
extern bool is_server_alive (void);
#endif

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
extern void release_all_fk_info_results (T_FK_INFO_RESULT * fk_res);
#endif

/*****************************
  cas_error.c function list
 *****************************/
extern int is_error_info_set (void);
extern void err_msg_set (T_NET_BUF * net_buf, const char *file, int line);
extern int error_info_set (int err_number, int err_indicator, const char *file, int line);
extern int error_info_set_force (int err_number, int err_indicator, const char *file, int line);
extern int error_info_set_with_msg (int err_number, int err_indicator, const char *err_msg, bool force,
				    const char *file, int line);
extern void error_info_clear (void);
extern void set_server_aborted (bool is_aborted);
extern bool is_server_aborted (void);

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
/*****************************
  move from cas_log.c
 *****************************/
extern void cas_log_error_handler (unsigned int eid);
extern void cas_log_error_handler_begin (void);
extern void cas_log_error_handler_end (void);
extern void cas_log_error_handler_clear (void);
extern char *cas_log_error_handler_asprint (char *buf, size_t bufsz, bool clear);

/*****************************
  move from cas_sql_log2.c
 *****************************/
extern void set_optimization_level (int level);
extern void reset_optimization_level_as_saved (void);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
extern int ux_lob_new (int lob_type, T_NET_BUF * net_buf);
extern int ux_lob_write (DB_VALUE * lob_dbval, int64_t offset, int size, char *data, T_NET_BUF * net_buf);
extern int ux_lob_read (DB_VALUE * lob_dbval, int64_t offset, int size, T_NET_BUF * net_buf);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

extern int get_tuple_count (T_SRV_HANDLE * srv_handle);
#endif /* _CAS_EXECUTE_H_ */
