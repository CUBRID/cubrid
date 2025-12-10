/*
 *
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

#include "pl_session.hpp"

#include "pl_comm.h"
#include "pl_query_cursor.hpp"
#include "pl_sr.h"
#include "query_manager.h"
#include "session.h"
#include "xserver_interface.h"
#include "thread_manager.hpp"
#include "method_error.hpp"

#include "method_struct_parameter_info.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define CONN_EPOCH_NEVER    (-2)
#define CONN_EPOCH_NONE     (-1)

namespace cubpl
{
//////////////////////////////////////////////////////////////////////////
// Global interface
//////////////////////////////////////////////////////////////////////////

  session *get_session ()
  {
    session *s = nullptr;
    cubthread::entry *thread_p = thread_get_thread_entry_info ();
#if defined (SERVER_MODE)
    // only worker thread can access session
    if (thread_p && thread_p->type != TT_WORKER)
      {
	return nullptr;
      }
#endif

    int error = session_get_pl_session (thread_p, s);
    if (error != NO_ERROR)
      {
	// session expired or internal error
	er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTING, 1, thread_p->tran_index);
      }
    return s;   // s can be null on error cases, especially, on session expriation
  }

//////////////////////////////////////////////////////////////////////////
// Runtime Context
//////////////////////////////////////////////////////////////////////////

  session::session (SESSION_ID id)
    : m_mutex_stack ()
    , m_mutex_connection ()
    , m_mutex_cursor ()
    , m_session_cursors {}
    , m_stack_map {}
    , m_exec_stack {}
    , m_cursor_map {}
    , m_req_id {0}
    , m_param_info {nullptr}
    , m_all_session_params_required {true}
    , m_last_conn_epoch (CONN_EPOCH_NEVER)
    , m_stack_idx {-1}
    , m_interrupt_id (NO_ERROR)
    , m_id (id)
  {
    m_exec_stack.reserve (METHOD_MAX_RECURSION_DEPTH + 1);
  }

  session::~session ()
  {
    er_log_debug (ARG_FILE_LINE, "pl_session (delete): %d\n", m_id);
    destroy_pl_context_jvm ();

    std::lock_guard<std::mutex> lock (m_mutex_connection);
    m_session_connections.clear ();
    // TODO: delete other resources
  }

  execution_stack *
  session::create_and_push_stack (cubthread::entry *thread_p)
  {
    if (thread_p == nullptr)
      {
	thread_p = thread_get_thread_entry_info ();
      }

    std::lock_guard<std::mutex> lock (m_mutex_stack);

    if (m_stack_idx >= METHOD_MAX_RECURSION_DEPTH)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_TOO_MANY_NESTED_CALL, 0);
	set_interrupt (ER_SP_TOO_MANY_NESTED_CALL);
	return nullptr;
      }

    if (m_stack_idx == -1)
      {
	// clear previous interrupt state
	clear_interrupt ();
      }
    else
      {

	assert (m_stack_idx >= 0);

	// check interrupt
	if (m_interrupt_id != NO_ERROR)
	  {
	    // block creating a new stack
	    set_local_error_for_interrupt ();
	    return nullptr;
	  }
      }

    execution_stack *stack = new execution_stack (thread_p, this);
    if (stack)
      {
	// update stack index
	m_stack_idx++;

	// push to exec_stack
	PL_STACK_ID stack_id = stack->get_id ();
	m_stack_map [stack_id] = stack;

	int stack_size = (int) m_exec_stack.size ();
	if (m_stack_idx < stack_size)
	  {
	    m_exec_stack [m_stack_idx] = stack_id;
	  }
	else
	  {
	    m_exec_stack.emplace_back (stack_id);
	  }
      }
    else
      {
	set_interrupt (ER_OUT_OF_VIRTUAL_MEMORY);
      }

    return stack;
  }

  void
  session::pop_and_destroy_stack (const PL_STACK_ID sid)
  {
    auto target_stack_is_at_top = [&] () -> bool
    {
      // condition to check
      return m_exec_stack[m_stack_idx] == sid;
    };

    std::unique_lock<std::mutex> ulock (m_mutex_stack);

    // Guaranteed to be removed from the topmost element
    m_cond_target_stack_at_top.wait (ulock, target_stack_is_at_top);

    assert (m_stack_idx >= 0);
    m_exec_stack[m_stack_idx] = -1;
    m_stack_idx--;
    m_cond_target_stack_at_top.notify_all();

    m_stack_map.erase (sid);

    if (m_stack_idx == -1)
      {
	clear_interrupt ();
	m_cond_pl_session_done.notify_all();
      }
    else
      {
	assert (m_stack_idx >= 0);
      }
  }

  execution_stack *
  session::top_stack ()
  {
    std::lock_guard<std::mutex> lock (m_mutex_stack);

    if (m_stack_idx == -1)
      {
	return nullptr;
      }

    assert (m_stack_idx >= 0);

    PL_STACK_ID top = m_exec_stack[m_stack_idx];
    const auto &it = m_stack_map.find (top);
    if (it == m_stack_map.end ())
      {
	// should not happended
	assert (false);
	return nullptr;
      }

    return it->second;
  }

  connection_view
  session::claim_connection ()
  {
    std::lock_guard<std::mutex> lock (m_mutex_connection);

    connection_view cv = nullptr;
    if (m_session_connections.empty ())
      {
	connection_pool *pool = get_connection_pool ();
	if (pool)
	  {
	    m_session_connections.emplace_back (std::move (pool->claim ()));
	  }
      }

    if (!m_session_connections.empty ())
      {
	cv = std::move (m_session_connections.front());
	m_session_connections.pop_front();
      }

    if (check_reloading_pl_context_required (cv))
      {
	set_session_params_all_required (true);
      }

    return cv;
  }

  void
  session::release_connection (connection_view &conn)
  {
    std::lock_guard<std::mutex> lock (m_mutex_connection);

    if (conn != nullptr)
      {
	// if there was no error, update the connection epoch and session parameters state
	if (conn->get_last_error () == NO_ERROR)
	  {
	    if (m_last_conn_epoch == conn->get_epoch ())
	      {
		set_session_params_all_required (false);
	      }
	    m_last_conn_epoch = conn->get_epoch ();
	  }

	m_session_connections.emplace_back (std::move (conn));
      }
  }

  void
  session::destroy_pl_context_jvm ()
  {
    if (m_last_conn_epoch != CONN_EPOCH_NEVER)
      {

	cubmethod::header header (m_id, SP_CODE_DESTROY);
	m_last_conn_epoch = CONN_EPOCH_NONE;

	connection_view cv = claim_connection ();
	if (cv)
	  {
	    if (cv->is_valid ())
	      {
		cv->send_buffer_args (header);
	      }
	    release_connection (cv);
	  }
      }
  }

  bool
  session::is_thread_involved (thread_id_t id)
  {
    std::lock_guard<std::mutex> lock (m_mutex_stack);

    for (const auto &it : m_stack_map)
      {
	execution_stack *stack = it.second;
	if (stack->get_thread_entry () && id == stack->get_thread_entry ()->get_id ())
	  {
	    return true;
	  }
      }

    return false;
  }

  void
  session::set_interrupt (int reason, std::string msg)
  {
    if (m_interrupt_id != NO_ERROR)
      {
	// do not overwrite interrupt
	return;
      }

    switch (reason)
      {
      /* no arg */
      case ER_INTERRUPTED:
      case ER_SP_TOO_MANY_NESTED_CALL:
      case ER_NET_SERVER_SHUTDOWN:
      case ER_SP_NOT_RUNNING_PL_SERVER:
      case ER_SES_SESSION_EXPIRED:
	m_interrupt_id = reason;
	m_interrupt_msg.assign ("");
	break;

      /* 1 arg */
      case ER_SP_CANNOT_CONNECT_PL_SERVER:
      case ER_SP_NETWORK_ERROR:
      case ER_OUT_OF_VIRTUAL_MEMORY:
	m_interrupt_id = reason;
	m_interrupt_msg.assign (msg);
	break;
      default:
	/* do nothing */
	break;
      }

#if !defined (NDEBUG)
    if (m_interrupt_id != NO_ERROR)
      {
	er_log_debug (ARG_FILE_LINE, "pl_session (interrupted): %d\n", m_id);
      }
#endif

    destroy_pl_context_jvm ();
  }

  void
  session::set_local_error_for_interrupt ()
  {
    cubmethod::handle_method_error (get_interrupt_id (), get_interrupt_msg ());
  }

  bool
  session::is_interrupted ()
  {
    return m_interrupt_id != NO_ERROR;
  }

  int
  session::get_interrupt_id ()
  {
    return m_interrupt_id;
  }

  std::string
  session::get_interrupt_msg ()
  {
    return m_interrupt_msg;
  }

  void
  session::clear_interrupt ()
  {
    m_interrupt_id = NO_ERROR;
    m_interrupt_msg.clear ();
  }

  void
  session::wait_until_pl_session_done ()
  {
    auto pl_session_is_not_running = [this] () -> bool
    {
      // condition of finish
      return m_stack_idx == -1; // not running
    };

    std::unique_lock<std::mutex> ulock (m_mutex_stack);
    m_cond_pl_session_done.wait (ulock, pl_session_is_not_running);
  }

  SESSION_ID
  session::get_id ()
  {
    return m_id;
  }

  bool
  session::is_sp_running ()
  {
    std::lock_guard<std::mutex> lock (m_mutex_stack);
    return m_stack_idx >= 0;
  }

  query_cursor *
  session::get_cursor (cubthread::entry *thread_p, QUERY_ID query_id)
  {
    if (query_id == NULL_QUERY_ID)
      {
	return nullptr;
      }

    std::lock_guard<std::mutex> lock (m_mutex_cursor);

    // find in map
    auto search = m_cursor_map.find (query_id);
    if (search != m_cursor_map.end ())
      {
	// found
	return search->second;
      }

    return nullptr;
  }

  query_cursor *
  session::create_cursor (cubthread::entry *thread_p, QUERY_ID query_id, bool is_oid_included)
  {
    if (query_id == NULL_QUERY_ID || query_id >= SHRT_MAX)
      {
	// false query e.g) SELECT * FROM db_class WHERE 0 <> 0
	return nullptr;
      }

    std::lock_guard<std::mutex> lock (m_mutex_cursor);
    query_cursor *cursor = nullptr;

    // find in map
    auto search = m_cursor_map.find (query_id);
    if (search != m_cursor_map.end ())
      {
	// found
	cursor = search->second;
	assert (cursor != nullptr);

	cursor->change_owner (thread_p);
	return cursor;
      }
    else
      {
	// not found, create a new server-side cursor
	cursor = new query_cursor (thread_p, query_id, is_oid_included);

	// store a new cursor in map
	m_cursor_map [query_id] = cursor;

	assert (cursor != nullptr);
      }

    return cursor;
  }

  void
  session::destroy_cursor (cubthread::entry *thread_p, QUERY_ID query_id)
  {
    if (query_id == NULL_QUERY_ID)
      {
	/* do nothing */
	return;
      }

    std::lock_guard<std::mutex> lock (m_mutex_cursor);

    // find in map
    auto search = m_cursor_map.find (query_id);
    if (search != m_cursor_map.end ())
      {
	delete search->second;  // search->second cannot be null
	m_cursor_map.erase (search);
      }
  }

  void
  session::add_session_cursor (cubthread::entry *thread_p, QUERY_ID query_id)
  {
    if (query_id == NULL_QUERY_ID)
      {
	/* do nothing */
	return;
      }

    std::lock_guard<std::mutex> lock (m_mutex_cursor);
    m_session_cursors.insert (query_id);
  }

  void
  session::remove_session_cursor (cubthread::entry *thread_p, QUERY_ID query_id)
  {
    if (query_id == NULL_QUERY_ID)
      {
	/* do nothing */
	return;
      }

    std::lock_guard<std::mutex> lock (m_mutex_cursor);
    m_session_cursors.erase (query_id);
  }

  bool
  session::is_session_cursor (QUERY_ID query_id)
  {
    std::lock_guard<std::mutex> lock (m_mutex_cursor);
    return (m_session_cursors.find (query_id) != m_session_cursors.end ());
  }

  void
  session::destroy_all_cursors ()
  {
    std::lock_guard<std::mutex> lock (m_mutex_cursor);

    for (auto &it : m_cursor_map)
      {
	query_cursor *cursor = it.second;
	if (cursor)
	  {
	    delete cursor;
	  }
      }
    m_cursor_map.clear ();
    m_session_cursors.clear ();
  }

  cubmethod::db_parameter_info *
  session::get_db_parameter_info () const
  {
    return m_param_info;
  }

  void
  session::set_db_parameter_info (cubmethod::db_parameter_info *param_info)
  {
    m_param_info = param_info;
  }

  bool
  session::check_reloading_pl_context_required (const connection_view &conn)
  {
    return m_last_conn_epoch < 0 || !conn || m_last_conn_epoch != conn->get_epoch () || !conn->is_valid ();
  }

  const std::vector <sys_param>
  session::obtain_session_parameters (const connection_view &conn)
  {
    std::vector<sys_param> changed_sys_params;

    if (check_reloading_pl_context_required (conn))
      {
	set_session_params_all_required (true);
      }

    if (m_session_param_changed_ids.size () == 0 && !m_all_session_params_required)
      {
	// there was no change in session parameters
	return changed_sys_params;
      }

    // obtain DB's session parameters
    SYSPRM_ASSIGN_VALUE *session_params = xsysprm_get_pl_context_parameters (PRM_USER_CHANGE | PRM_FOR_SESSION);
    SYSPRM_ASSIGN_VALUE *next_param = session_params;
    while (next_param != NULL)
      {
	bool is_param_changed = m_session_param_changed_ids.find (next_param->prm_id) != m_session_param_changed_ids.end ();
	if (m_all_session_params_required || is_param_changed)
	  {
	    changed_sys_params.emplace_back (next_param);
	  }

	next_param = next_param->next;
      }

    // obtain PL's session parameters
    auto to_int = [] (sys_param_id id)
    {
      return static_cast<std::underlying_type_t<sys_param_id>> (id);
    };

    for (auto i = to_int (sys_param_id::PRM_ID_BEGIN);
	 i < to_int (sys_param_id::PRM_ID_END); ++i)
      {
	bool is_param_changed = m_session_param_changed_ids.find (i) != m_session_param_changed_ids.end ();
	if (m_all_session_params_required || is_param_changed)
	  {
	    auto param = m_session_params.find (i);
	    if (param != m_session_params.end ())
	      {
		changed_sys_params.emplace_back (param->second);
	      }
	  }
      }

    if (session_params)
      {
	sysprm_free_assign_values (&session_params);
      }

    if (changed_sys_params.size () > 0)
      {
	for (auto &param : changed_sys_params)
	  {
	    set_session_param (param);
	  }
      }

    return changed_sys_params;
  }

  void
  session::mark_session_param_changed (int prm_id)
  {
    m_session_param_changed_ids.insert (prm_id);
  }

  void
  session::set_session_param (const sys_param &param)
  {
    m_session_params.insert_or_assign (param.prm_id, param);
  }

  void
  session::set_session_params_all_required (bool is_required)
  {
    m_all_session_params_required = is_required;
    if (!m_all_session_params_required)
      {
	m_session_param_changed_ids.clear ();
      }
  }

  sys_param::sys_param (const SYSPRM_ASSIGN_VALUE *db_param)
    : sys_param (GET_PRM (db_param->prm_id))
  {
    //
  }

  sys_param::sys_param (const SYSPRM_PARAM *db_param)
    : sys_param ((int) db_param->id, (int) GET_PRM (db_param->id)->datatype, "")
  {
    if (prm_id < static_cast<int> (sys_param_id::PRM_ID_BEGIN))
      {
	set_prm_value (db_param);
      }
  }

  sys_param::sys_param (int prm_id, int prm_type, std::string prm_value)
    : prm_id (prm_id), prm_type (prm_type), prm_value (prm_value)
  {

  }

  void
  sys_param::set_prm_value (const SYSPRM_PARAM *prm)
  {
    if (prm_type == PRM_BOOLEAN)
      {
	bool val = prm_get_bool_value (prm->id);
	prm_value = val ? "t" : "f";
      }
    else if (prm_type == PRM_STRING)
      {
	const char *val = prm_get_string_value (prm->id);
	if (val == NULL)
	  {
	    switch (prm->id)
	      {
	      case PRM_ID_INTL_COLLATION:
		val = lang_get_collation_name (LANG_GET_BINARY_COLLATION (LANG_SYS_CODESET));
		break;
	      case PRM_ID_INTL_DATE_LANG:
	      case PRM_ID_INTL_NUMBER_LANG:
		val = lang_get_Lang_name ();
		break;
	      case PRM_ID_TIMEZONE:
		val = prm_get_string_value (PRM_ID_SERVER_TIMEZONE);
		break;
	      default:
		/* do nothing */
		break;
	      }
	  }
	prm_value = val;
      }
    else if (prm_type == PRM_INTEGER)
      {
	int val = prm_get_integer_value (prm->id);
	prm_value = std::to_string (val);
      }
    else if (prm_type == PRM_BIGINT)
      {
	UINT64 val = prm_get_bigint_value (prm->id);
	prm_value = std::to_string (val);
      }
    else if (prm_type == PRM_FLOAT)
      {
	float val = prm_get_float_value (prm->id);
	prm_value = std::to_string (val);
      }
    else
      {
	assert (false);
	prm_value = "*unknown*";
      }
  }

#define SYS_PARAM_PACKER_ARGS() \
  prm_id, prm_type, prm_value

  void
  sys_param::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_all (SYS_PARAM_PACKER_ARGS());
  }

  size_t
  sys_param::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    return serializator.get_all_packed_size_starting_offset (start_offset, SYS_PARAM_PACKER_ARGS ());
  }

  void
  sys_param::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_all (SYS_PARAM_PACKER_ARGS ());
  }
} // cubmethod
