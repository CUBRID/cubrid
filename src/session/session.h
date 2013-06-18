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
 * session.h - Session state api
 */
#ifndef _SESSION_H_
#define _SESSION_H_

#include "dbtype.h"
#include "thread.h"
#include "query_list.h"
#include "query_manager.h"

extern bool sessions_is_states_table_initialized (void);
extern int session_states_init (THREAD_ENTRY * thread_p);
extern void session_states_finalize (THREAD_ENTRY * thread_p);
extern int session_state_create (THREAD_ENTRY * thread_p, SESSION_KEY * key);
extern int session_state_destroy (THREAD_ENTRY * thread_p,
				  const SESSION_KEY * key);
extern int session_check_session (THREAD_ENTRY * thread_p,
				  const SESSION_KEY * key);
extern int session_set_session_key (THREAD_ENTRY * thread_p,
				    const SESSION_KEY * key);
extern int session_remove_expired_sessions (struct timeval *timeout);
extern int session_get_session_id (THREAD_ENTRY * thread_p,
				   SESSION_KEY * key);
extern int session_get_last_insert_id (THREAD_ENTRY * thread_p,
				       DB_VALUE * value,
				       bool update_last_insert_id);
extern int session_set_cur_insert_id (THREAD_ENTRY * thread_p,
				      const DB_VALUE * value, bool force);
extern int session_reset_cur_insert_id (THREAD_ENTRY * thread_p);
extern int session_begin_insert_values (THREAD_ENTRY * thread_p);
extern int session_get_row_count (THREAD_ENTRY * thread_p, int *row_count);
extern int session_set_row_count (THREAD_ENTRY * thread_p,
				  const int row_count);
extern int session_get_session_parameters (THREAD_ENTRY * thread_p,
					   SESSION_PARAM **
					   session_parameters);
extern int session_set_session_parameters (THREAD_ENTRY * thread_p,
					   SESSION_PARAM *
					   session_parameters);
extern int session_create_prepared_statement (THREAD_ENTRY * thread_p,
					      OID user, char *name,
					      char *alias_print, char *info,
					      int info_len);
extern int session_get_prepared_statement (THREAD_ENTRY * thread_p,
					   const char *name, char **info,
					   int *info_len, XASL_ID * xasl_id);
extern int session_delete_prepared_statement (THREAD_ENTRY * thread_p,
					      const char *name);
extern int login_user (THREAD_ENTRY * thread_p, const char *username);
extern int session_set_session_variables (THREAD_ENTRY * thread_p,
					  DB_VALUE * values, const int count);
extern int session_get_variable (THREAD_ENTRY * thread_p,
				 const DB_VALUE * name, DB_VALUE * result);
extern int session_get_variable_no_copy (THREAD_ENTRY * thread_p,
					 const DB_VALUE * name,
					 DB_VALUE ** result);
extern int session_define_variable (THREAD_ENTRY * thread_p, DB_VALUE * name,
				    DB_VALUE * value, DB_VALUE * result);
extern int session_drop_session_variables (THREAD_ENTRY * thread_p,
					   DB_VALUE * values,
					   const int count);
extern void session_states_dump (THREAD_ENTRY * thread_p);
extern void session_store_query_entry_info (THREAD_ENTRY * thread_p,
					    QMGR_QUERY_ENTRY * qentry_p);
extern int session_load_query_entry_info (THREAD_ENTRY * thread_p,
					  QMGR_QUERY_ENTRY * qentry_p);
extern int session_remove_query_entry_info (THREAD_ENTRY * thread_p,
					    const QUERY_ID query_id);
extern int session_clear_query_entry_info (THREAD_ENTRY * thread_p,
					   const QUERY_ID query_id);

extern int session_get_exec_stats_and_clear (THREAD_ENTRY * thread_p,
					     const DB_VALUE * name,
					     DB_VALUE * result);
extern SESSION_PARAM *session_get_session_parameter (THREAD_ENTRY * thread_p,
						     PARAM_ID id);
extern int session_get_trace_stats (THREAD_ENTRY * thread_p,
                                   DB_VALUE * result);
extern int session_set_trace_stats (THREAD_ENTRY * thread_p, char *scan_stats,
                                   int format);
#endif /* _SESSION_H_ */
