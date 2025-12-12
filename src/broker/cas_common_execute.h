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
 * cas_common_execute.h
 */

#ifndef	_CAS_COMMON_EXECUTE_H_
#define	_CAS_COMMON_EXECUTE_H_

#ident "$Id$"

#include "cas_util.h"

#define CAS_TYPE_SET(TYPE)		((TYPE) | CCI_CODE_SET)
#define CAS_TYPE_MULTISET(TYPE)		((TYPE) | CCI_CODE_MULTISET)
#define CAS_TYPE_SEQUENCE(TYPE)		((TYPE) | CCI_CODE_SEQUENCE)

#define CAS_TYPE_COLLECTION(DB_TYPE, SET_TYPE)		\
	(((DB_TYPE) == DB_TYPE_SET) ? (CAS_TYPE_SET(SET_TYPE)) : \
	(((DB_TYPE) == DB_TYPE_MULTISET) ? (CAS_TYPE_MULTISET(SET_TYPE)) : \
	(CAS_TYPE_SEQUENCE(SET_TYPE))))

extern char ux_db_type_to_cas_type (int db_type);

extern void ux_set_utype_for_enum (char u_type);
extern void ux_set_utype_for_timestamptz (char u_type);
extern void ux_set_utype_for_datetimetz (char u_type);
extern void ux_set_utype_for_timestampltz (char u_type);
extern void ux_set_utype_for_datetimeltz (char u_type);
extern void ux_set_utype_for_json (char u_type);

extern char get_stmt_type (char *stmt);
extern int ux_get_db_version (T_NET_BUF * net_buf, T_REQ_INFO * req_info);

extern char *consume_tokens (char *stmt, STATEMENT_STATUS stmt_status);
extern int get_num_markers (char *stmt);

extern void ux_end_tran_cleanup (int tran_type);

extern void update_query_execution_count (T_APPL_SERVER_INFO * as_info_p, char stmt_type);
extern void update_error_query_count (T_APPL_SERVER_INFO * as_info_p, const T_ERROR_INFO * err_info_p);

extern bool has_stmt_result_set (char stmt_type);

extern bool check_auto_commit_after_getting_result (T_SRV_HANDLE * srv_handle);

extern void prepare_column_info_set (T_NET_BUF * net_buf, char ut, short scale, int prec, char charset,
				     const char *col_name, const char *default_value, char auto_increment,
				     char unique_key, char primary_key, char reverse_index, char reverse_unique,
				     char foreign_key, char shared, const char *attr_name, const char *class_name,
				     char is_non_null, T_BROKER_VERSION client_version);

typedef struct cas_error_log_handle_context_s CAS_ERROR_LOG_HANDLE_CONTEXT;
struct cas_error_log_handle_context_s
{
  unsigned int from;
  unsigned int to;
};

/* Error log handler callback (registered with db_register_error_log_handler) */
extern void cas_log_error_handler (unsigned int eid);
extern void cas_log_error_handler_begin (void);
extern void cas_log_error_handler_end (void);
extern void cas_log_error_handler_clear (void);
/* Error log handler utility functions */
extern char *cas_log_error_handler_asprint (char *buf, size_t bufsz, bool clear);
extern char *get_error_log_eids (int err);

#endif /* _CAS_COMMON_EXECUTE_H_ */
