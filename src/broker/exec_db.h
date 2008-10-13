/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * exec_db.h - 
 */

#ifndef	_EXEC_DB_H_
#define	_EXEC_DB_H_

#ident "$Id$"

#include "cas.h"
#include "net_buf.h"
#include "handle_mng.h"
#include "db_inc.h"

#define UX_DATABASE_CONNECT(DBNAME, DBUSER, DBPASSWD, DBERRMSG, CAS_MM_ARG) \
	ux_database_connect(DBNAME, DBUSER, DBPASSWD, DBERRMSG)
#define UX_SET_DEFAULT_SETTING()	\
	ux_set_default_setting()
#define UX_DATABASE_SHUTDOWN(CAS_MM_ARG)	\
	ux_database_shutdown()
#define UX_END_TRAN(TRAN_TYPE, CAS_MM_ARG)	\
	ux_end_tran(TRAN_TYPE, TRUE)

#define CAS_TYPE_SET(TYPE)		((TYPE) | CCI_CODE_SET)
#define CAS_TYPE_MULTISET(TYPE)		((TYPE) | CCI_CODE_MULTISET)
#define CAS_TYPE_SEQUENCE(TYPE)		((TYPE) | CCI_CODE_SEQUENCE)

#define CAS_TYPE_COLLECTION(DB_TYPE, SET_TYPE)		\
	(((DB_TYPE) == DB_TYPE_SET) ? (CAS_TYPE_SET(SET_TYPE)) : \
	(((DB_TYPE) == DB_TYPE_MULTISET) ? (CAS_TYPE_MULTISET(SET_TYPE)) : \
	(CAS_TYPE_SEQUENCE(SET_TYPE))))

#define IS_SET_TYPE(DB_TYPE)	\
	((DB_TYPE) == DB_TYPE_SET || (DB_TYPE) == DB_TYPE_MULTISET || (DB_TYPE) == DB_TYPE_LIST)

#ifdef CAS_DEBUG
#define DB_ERR_MSG_SET(NET_BUF, ERR_CODE)	\
	db_err_msg_set_debug(NET_BUF, ERR_CODE, __FILE__, __LINE__)
#else
#define DB_ERR_MSG_SET(NET_BUF, ERR_CODE)	\
	db_err_msg_set(NET_BUF, ERR_CODE)
#endif

#ifdef CAS_AUTOCOMMIT
#define EXEC_AUTOCOMMIT_TRUE	1
#define EXEC_AUTOCOMMIT_FALSE	0
#endif

extern int ux_check_connection (void);
#ifndef LIBCAS_FOR_JSP
extern int ux_database_connect (char *db_name, char *db_user, char *db_passwd,
				char **db_err_msg);
#endif
extern int ux_is_database_connected (void);
extern int ux_prepare (char *sql_stmt, char flag, T_NET_BUF * ne_buf,
		       T_REQ_INFO * req_info, unsigned int query_seq_num);
extern int ux_end_tran (char tran_type, char reset_con_status);
extern int ux_execute (T_SRV_HANDLE * srv_handle, char flag, int max_col_size,
		       int max_row, int argc, void **argv, T_NET_BUF *,
		       T_REQ_INFO * req_info, CACHE_TIME * clt_cache_time,
		       int *clt_cache_reusable);
extern void ux_get_tran_setting (int *lock_wait, int *isol_level);
extern int ux_set_isolation_level (int isol_level, T_NET_BUF * net_buf);
extern void ux_set_lock_timeout (int lock_timeout);
extern int ux_fetch (T_SRV_HANDLE * srv_handle, int cursor_pos,
		     int fetch_count, char fetch_flag, int result_set_index,
		     T_NET_BUF * net_buf);
extern int ux_oid_get (int argc, void **argv, T_NET_BUF * net_buf);
extern int ux_glo_new (char *class_name, char *filename, T_NET_BUF * net_buf);
extern int ux_glo_save (DB_OBJECT * obj, char *filename, T_NET_BUF * net_buf);
extern int ux_glo_load (int sock_fd, DB_OBJECT * obj, T_NET_BUF * net_buf);
extern int ux_cursor (int srv_h_id, int offset, char origin,
		      T_NET_BUF * net_buf);
extern void ux_database_shutdown (void);
extern int ux_get_db_version (T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern int ux_get_class_num_objs (char *class_name, char flag,
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
extern int ux_execute_all (T_SRV_HANDLE * srv_handle, char flag,
			   int max_col_size, int max_row, int argc,
			   void **argv, T_NET_BUF * net_buf,
			   T_REQ_INFO * req_info, CACHE_TIME * clt_cache_time,
			   int *clt_cache_reusable);
extern int ux_execute_array (T_SRV_HANDLE * srv_h_id, int argc, void **argv,
			     T_NET_BUF * net_buf, T_REQ_INFO * req_info);
extern int ux_execute_batch (int argc, void **argv, T_NET_BUF * net_buf,
			     T_REQ_INFO * req_info);
extern int ux_cursor_update (T_SRV_HANDLE * srv_handle, int cursor_pos,
			     int argc, void **argv, T_NET_BUF * net_buf);

#ifdef CAS_DEBUG
extern void db_err_msg_set_debug (T_NET_BUF * net_buf, int err_code,
				  char *file, int line);
#else
extern void db_err_msg_set (T_NET_BUF * net_buf, int err_code);
#endif

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
			       DB_OBJECT * glo_obj, char *method_name,
			       DB_VALUE * ret_val, DB_VALUE ** args);
extern int ux_check_object (DB_OBJECT * obj, T_NET_BUF * net_buf);
extern void ux_free_result (void *res);
extern char ux_db_type_to_cas_type (int db_type);

extern int ux_schema_info (int schema_type, char *class_name, char *attr_name,
			   char flag, T_NET_BUF * net_buf,
			   T_REQ_INFO * req_info, unsigned int query_seq_num);
extern void ux_prepare_call_info_free (T_PREPARE_CALL_INFO * call_info);
extern int ux_execute_call (T_SRV_HANDLE * srv_handle, char flag,
			    int max_col_size, int max_row, int argc,
			    void **argv, T_NET_BUF * net_buf,
			    T_REQ_INFO * req_info,
			    CACHE_TIME * clt_cache_time,
			    int *clt_cache_reusable);
extern void ux_call_info_cp_param_mode (T_SRV_HANDLE * srv_handle,
					char *param_mode, int num_param);
extern int ux_glo_new2 (char *class_name, char glo_type, char *filename,
			T_NET_BUF * net_buf);

extern int ux_make_out_rs (int srv_h_id, T_NET_BUF * net_buf,
			   T_REQ_INFO * req_info);
extern int ux_get_generated_keys (T_SRV_HANDLE * srv_handle,
				  T_NET_BUF * net_buf);

#endif /* _EXEC_DB_H_ */
