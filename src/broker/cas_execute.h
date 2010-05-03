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
#endif /* CAS_FOR_MYSQL */

#define CAS_TYPE_SET(TYPE)		((TYPE) | CCI_CODE_SET)
#define CAS_TYPE_MULTISET(TYPE)		((TYPE) | CCI_CODE_MULTISET)
#define CAS_TYPE_SEQUENCE(TYPE)		((TYPE) | CCI_CODE_SEQUENCE)

#define CAS_TYPE_COLLECTION(DB_TYPE, SET_TYPE)		\
	(((DB_TYPE) == DB_TYPE_SET) ? (CAS_TYPE_SET(SET_TYPE)) : \
	(((DB_TYPE) == DB_TYPE_MULTISET) ? (CAS_TYPE_MULTISET(SET_TYPE)) : \
	(CAS_TYPE_SEQUENCE(SET_TYPE))))

#define IS_SET_TYPE(DB_TYPE)	\
	((DB_TYPE) == DB_TYPE_SET || (DB_TYPE) == DB_TYPE_MULTISET || (DB_TYPE) == DB_TYPE_LIST)

#ifdef CAS_FOR_DBMS
#define ERROR_INFO_SET(ERR_CODE, ERR_INDICATOR)\
	error_info_set(ERR_CODE, ERR_INDICATOR, __FILE__, __LINE__)
#define ERROR_INFO_SET_WITH_MSG(ERR_CODE, ERR_INDICATOR, ERR_MSG)\
	error_info_set_with_msg(ERR_CODE, ERR_INDICATOR, ERR_MSG, __FILE__, __LINE__)
#define NET_BUF_ERR_SET(NET_BUF)	\
	err_msg_set(NET_BUF, __FILE__, __LINE__)
#else
#define DB_ERR_MSG_SET(NET_BUF, ERR_CODE)	\
	db_err_msg_set(NET_BUF, ERR_CODE, __FILE__, __LINE__)
#endif

extern int ux_check_connection (void);
#ifndef LIBCAS_FOR_JSP
extern int ux_database_connect (char *db_name, char *db_user, char *db_passwd,
				char **db_err_msg);
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
extern int ux_database_reconnect (void);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
#endif /* !LIBCAS_FOR_JSP */
extern int ux_is_database_connected (void);
extern int ux_prepare (char *sql_stmt, int flag, bool auto_commit_mode,
		       T_NET_BUF * ne_buf, T_REQ_INFO * req_info,
		       unsigned int query_seq_num);
extern int ux_end_tran (int tran_type, bool reset_con_status);
extern int ux_auto_commit (T_NET_BUF * CAS_FN_ARG_NET_BUF,
			   T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int ux_execute (T_SRV_HANDLE * srv_handle, char flag, int max_col_size,
		       int max_row, int argc, void **argv, T_NET_BUF *,
		       T_REQ_INFO * req_info, CACHE_TIME * clt_cache_time,
		       int *clt_cache_reusable);
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
extern void ux_get_tran_setting (int *lock_wait, int *isol_level);
extern int ux_set_isolation_level (int isol_level, T_NET_BUF * net_buf);
extern void ux_set_lock_timeout (int lock_timeout);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
extern int ux_fetch (T_SRV_HANDLE * srv_handle, int cursor_pos,
		     int fetch_count, char fetch_flag, int result_set_index,
		     T_NET_BUF * net_buf, T_REQ_INFO * req_info);
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
extern int ux_oid_get (int argc, void **argv, T_NET_BUF * net_buf);
extern int ux_glo_new (char *class_name, char *filename, T_NET_BUF * net_buf);
extern int ux_glo_save (DB_OBJECT * obj, char *filename, T_NET_BUF * net_buf);
extern int ux_glo_load (SOCKET sock_fd, DB_OBJECT * obj, T_NET_BUF * net_buf);
extern int ux_cursor (int srv_h_id, int offset, int origin,
		      T_NET_BUF * net_buf);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
extern void ux_database_shutdown (void);
extern int ux_get_db_version (T_NET_BUF * net_buf, T_REQ_INFO * req_info);
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
extern int ux_get_class_num_objs (char *class_name, int flag,
				  T_NET_BUF * net_buf);
extern void ux_col_get (DB_COLLECTION * col, char col_type, char ele_type,
			DB_DOMAIN * ele_domain, T_NET_BUF * net_buf);
extern void ux_col_size (DB_COLLECTION * col, T_NET_BUF * net_buf);
extern int ux_col_set_drop (DB_COLLECTION * col, DB_VALUE * ele_val,
			    T_NET_BUF * net_buf);
extern int ux_col_set_add (DB_COLLECTION * col, DB_VALUE * ele_val,
			   T_NET_BUF * net_buf);
extern int ux_col_seq_drop (DB_COLLECTION * col, int index,
			    T_NET_BUF * net_buf);
extern int ux_col_seq_insert (DB_COLLECTION * col, int index,
			      DB_VALUE * ele_val, T_NET_BUF * net_buf);
extern int ux_col_seq_put (DB_COLLECTION * col, int index, DB_VALUE * ele_val,
			   T_NET_BUF * net_buf);

extern char get_set_domain (DB_DOMAIN * col, int *precision, short *scale,
			    char *db_type);

extern int ux_next_result (T_SRV_HANDLE * srv_h_id, char flag,
			   T_NET_BUF * net_buf, T_REQ_INFO * req_info);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
extern int ux_execute_all (T_SRV_HANDLE * srv_handle, char flag,
			   int max_col_size, int max_row, int argc,
			   void **argv, T_NET_BUF * net_buf,
			   T_REQ_INFO * req_info, CACHE_TIME * clt_cache_time,
			   int *clt_cache_reusable);
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
extern int ux_execute_array (T_SRV_HANDLE * srv_h_id, int argc, void **argv,
			     T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern int ux_execute_batch (int argc, void **argv, T_NET_BUF * net_buf,
			     T_REQ_INFO * req_info);
extern int ux_cursor_update (T_SRV_HANDLE * srv_handle, int cursor_pos,
			     int argc, void **argv, T_NET_BUF * net_buf);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

#if defined(CAS_FOR_ORACLE)
extern void ux_free_result (void *res);
#elif defined(CAS_FOR_MYSQL)
extern int make_bind_value (int num_bind, int argc, void **argv,
			    MYSQL_BIND ** ret_val, T_NET_BUF * net_buf,
			    char desired_type);
extern void ux_free_result (T_QUERY_RESULT * result);
#else /* CAS_FOR_MYSQL */
extern int ux_oid_put (int argc, void **argv, T_NET_BUF * net_buf);
extern int make_bind_value (int num_bind, int argc, void **argv,
			    DB_VALUE ** ret_val, T_NET_BUF * net_buf,
			    char desired_type);

extern int ux_get_attr_type_str (char *class_name, char *attr_name,
				 T_NET_BUF * net_buf, T_REQ_INFO *);
extern int ux_get_query_info (int srv_h_id, char info_type,
			      T_NET_BUF * net_buf);
extern int ux_get_parameter_info (int srv_h_id, T_NET_BUF * net_buf);
extern void ux_get_default_setting (void);
extern void ux_set_default_setting (void);
extern int ux_glo_method_call (T_NET_BUF * net_buf, char check_ret,
			       DB_OBJECT * glo_obj, const char *method_name,
			       DB_VALUE * ret_val, DB_VALUE ** args);
extern int ux_check_object (DB_OBJECT * obj, T_NET_BUF * net_buf);
extern void ux_free_result (void *res);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
extern char ux_db_type_to_cas_type (int db_type);

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
extern int ux_schema_info (int schema_type, char *class_name, char *attr_name,
			   char flag, T_NET_BUF * net_buf,
			   T_REQ_INFO * req_info, unsigned int query_seq_num);
extern void ux_prepare_call_info_free (T_PREPARE_CALL_INFO * call_info);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
extern int ux_execute_call (T_SRV_HANDLE * srv_handle, char flag,
			    int max_col_size, int max_row, int argc,
			    void **argv, T_NET_BUF * net_buf,
			    T_REQ_INFO * req_info,
			    CACHE_TIME * clt_cache_time,
			    int *clt_cache_reusable);
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
extern void ux_call_info_cp_param_mode (T_SRV_HANDLE * srv_handle,
					char *param_mode, int num_param);
extern int ux_glo_new2 (char *class_name, char glo_type, char *filename,
			T_NET_BUF * net_buf);

extern int ux_make_out_rs (int srv_h_id, T_NET_BUF * net_buf,
			   T_REQ_INFO * req_info);
extern int ux_get_generated_keys (T_SRV_HANDLE * srv_handle,
				  T_NET_BUF * net_buf);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
extern void set_db_connect_status (int status);

#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
extern bool is_server_alive ();
#endif

/*****************************
  cas_error.c function list 
 *****************************/
#ifdef CAS_FOR_DBMS
extern void err_msg_set (T_NET_BUF * net_buf, const char *file, int line);
extern int error_info_set (int err_number, int err_indicator,
			   const char *file, int line);
extern int error_info_set_with_msg (int err_number, int err_indicator,
				    const char *err_msg, const char *file,
				    int line);
extern void error_info_clear ();
extern void glo_err_msg_set (T_NET_BUF * net_buf, int err_code,
			     const char *method_nm);
extern void glo_err_msg_get (int err_code, char *err_msg);
#else /* CAS_FOR_DBMS */
extern void db_err_msg_set (T_NET_BUF * net_buf, int err_code,
			    const char *file, int line);
#endif /* CAS_FOR_DBMS */
extern void set_server_aborted (bool is_aborted);
extern bool is_server_aborted ();

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
/***************************** 
  move from cas_log.c 
 *****************************/
extern void cas_log_error_handler (unsigned int eid);
extern void cas_log_error_handler_begin ();
extern void cas_log_error_handler_end (void);
extern void cas_log_error_handler_clear (void);
extern char *cas_log_error_handler_asprint (char *buf, size_t bufsz,
					    bool clear);

/***************************** 
  move from cas_sql_log2.c 
 *****************************/
extern void set_optimization_level (int level);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

#endif /* _CAS_EXECUTE_H_ */
