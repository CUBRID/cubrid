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
 * session.h - Session state api
 */
#ifndef _SESSION_H_
#define _SESSION_H_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "dbtype_def.h"
#include "load_session.hpp"
#include "query_list.h"
#include "query_manager.h"
#include "system_parameter.h"
#include "thread_compat.hpp"
#include "tz_support.h"

// forward definitions
struct xasl_cache_ent;

extern void session_states_init (THREAD_ENTRY * thread_p);
extern void session_states_finalize (THREAD_ENTRY * thread_p);
extern int session_state_create (THREAD_ENTRY * thread_p, SESSION_ID * id);
extern int session_state_destroy (THREAD_ENTRY * thread_p, const SESSION_ID id);
extern int session_check_session (THREAD_ENTRY * thread_p, const SESSION_ID id);
extern int session_get_session_id (THREAD_ENTRY * thread_p, SESSION_ID * id);
extern int session_get_last_insert_id (THREAD_ENTRY * thread_p, DB_VALUE * value, bool update_last_insert_id);
extern int session_set_cur_insert_id (THREAD_ENTRY * thread_p, const DB_VALUE * value, bool force);
extern int session_reset_cur_insert_id (THREAD_ENTRY * thread_p);
extern int session_begin_insert_values (THREAD_ENTRY * thread_p);
extern int session_set_trigger_state (THREAD_ENTRY * thread_p, bool is_trigger);
extern int session_get_row_count (THREAD_ENTRY * thread_p, int *row_count);
extern int session_set_row_count (THREAD_ENTRY * thread_p, const int row_count);
extern int session_get_session_parameters (THREAD_ENTRY * thread_p, SESSION_PARAM ** session_parameters);
extern int session_set_session_parameters (THREAD_ENTRY * thread_p, SESSION_PARAM * session_parameters);
extern int session_create_prepared_statement (THREAD_ENTRY * thread_p, char *name, char *alias_print, SHA1Hash * sha1,
					      char *info, int info_len);
extern int session_get_prepared_statement (THREAD_ENTRY * thread_p, const char *name, char **info, int *info_len,
					   xasl_cache_ent ** xasl_entry);
extern int session_delete_prepared_statement (THREAD_ENTRY * thread_p, const char *name);
extern int login_user (THREAD_ENTRY * thread_p, const char *username);
extern int session_set_session_variables (THREAD_ENTRY * thread_p, DB_VALUE * values, const int count);
extern int session_get_variable (THREAD_ENTRY * thread_p, const DB_VALUE * name, DB_VALUE * result);
extern int session_get_variable_no_copy (THREAD_ENTRY * thread_p, const DB_VALUE * name, DB_VALUE ** result);
extern int session_define_variable (THREAD_ENTRY * thread_p, DB_VALUE * name, DB_VALUE * value, DB_VALUE * result);
extern int session_drop_session_variables (THREAD_ENTRY * thread_p, DB_VALUE * values, const int count);
extern void session_states_dump (THREAD_ENTRY * thread_p);
extern void session_store_query_entry_info (THREAD_ENTRY * thread_p, QMGR_QUERY_ENTRY * qentry_p);
extern int session_load_query_entry_info (THREAD_ENTRY * thread_p, QMGR_QUERY_ENTRY * qentry_p);
extern int session_remove_query_entry_info (THREAD_ENTRY * thread_p, const QUERY_ID query_id);
extern void session_remove_query_entry_all (THREAD_ENTRY * thread_p);
extern int session_clear_query_entry_info (THREAD_ENTRY * thread_p, const QUERY_ID query_id);
extern bool session_is_queryid_idle (THREAD_ENTRY * thread_p, const QUERY_ID query_id, QUERY_ID * max_query_id_uses);

extern int session_get_exec_stats_and_clear (THREAD_ENTRY * thread_p, const DB_VALUE * name, DB_VALUE * result);
extern SESSION_PARAM *session_get_session_parameter (THREAD_ENTRY * thread_p, PARAM_ID id);
#if defined (SERVER_MODE)
extern int session_state_increase_ref_count (THREAD_ENTRY * thread_p, struct session_state *state_p);
extern int session_state_decrease_ref_count (THREAD_ENTRY * thread_p, struct session_state *state_p);
#endif
extern int session_get_trace_stats (THREAD_ENTRY * thread_p, DB_VALUE * result);
extern int session_set_trace_stats (THREAD_ENTRY * thread_p, char *scan_stats, int format);
extern int session_clear_trace_stats (THREAD_ENTRY * thread_p);
extern TZ_REGION *session_get_session_tz_region (THREAD_ENTRY * thread_p);
extern int session_get_number_of_holdable_cursors (void);
extern int session_get_private_lru_idx (const void *session_p);
extern int session_set_tran_auto_commit (THREAD_ENTRY * thread_p, bool auto_commit);

extern int session_set_load_session (THREAD_ENTRY * thread_p, load_session * load_session_p);
extern int session_get_load_session (THREAD_ENTRY * thread_p, REFPTR (load_session, load_session_ref_ptr));
extern void session_stop_attached_threads (void *session);
#endif /* _SESSION_H_ */
