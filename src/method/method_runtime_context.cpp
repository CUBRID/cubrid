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

#include "method_runtime_context.hpp"

#include "method_query_cursor.hpp"
#include "query_manager.h"
#include "session.h"
#include "xserver_interface.h"
#include "thread_manager.hpp"
#include "method_error.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubmethod
{
//////////////////////////////////////////////////////////////////////////
// Global interface
//////////////////////////////////////////////////////////////////////////

  runtime_context *get_rctx (cubthread::entry *thread_p)
  {
    method_runtime_context *rctx = nullptr;
    session_get_method_runtime_context (thread_p, rctx);
    return rctx;
  }

//////////////////////////////////////////////////////////////////////////
// Runtime Context
//////////////////////////////////////////////////////////////////////////

  runtime_context::runtime_context ()
    : m_mutex ()
    , m_group_stack {}
    , m_returning_cursors {}
    , m_group_map {}
    , m_cursor_map {}
    , m_is_interrupted (false)
    , m_interrupt_id (NO_ERROR)
    , m_is_running (false)
    , m_conn_pool (METHOD_MAX_RECURSION_DEPTH + 1)
  {
    //
  }

  runtime_context::~runtime_context ()
  {
    destroy_all_groups ();
  }

  method_invoke_group *
  runtime_context::create_invoke_group (cubthread::entry *thread_p, const method_sig_list &sig_list, bool is_scan)
  {
    std::unique_lock<std::mutex> ulock (m_mutex);

    method_invoke_group *group = new cubmethod::method_invoke_group (thread_p, sig_list, is_scan);
    if (group)
      {
	m_group_map [group->get_id ()] = group;
      }
    return group;
  }

  void
  runtime_context::push_stack (cubthread::entry *thread_p, method_invoke_group *group)
  {
    std::unique_lock<std::mutex> ulock (m_mutex);

    m_is_running = true;
    m_group_stack.push_back (group->get_id ());
  }

  void
  runtime_context::pop_stack (cubthread::entry *thread_p, method_invoke_group *claimed)
  {
    std::unique_lock<std::mutex> ulock (m_mutex);

    if (claimed->is_for_scan () && m_group_stack.back() != claimed->get_id ())
      {
	// push deferred
	// When beginning method_invoke_group with method scan, method_invoke_group belonging to child node in XASL is pushed first (postorder)
	// When method_invoke_group is ended while clearing XASL by qexec_clear_xasl(), method_invoke_group belonging to the parent node in XASL is poped first (preorder)
	// Because of these differences, I've introduced the m_deferred_free_stack structure to follow the order of clearing according to the XASL structure when clearing method_invoke_groups from the m_group_stack.
	m_deferred_free_stack.push_back (claimed->get_id ());
	return;
      }

    auto pred = [&] () -> bool
    {
      // condition to check
      return m_group_stack.back() == claimed->get_id ();
    };

    // Guaranteed to be removed from the topmost element
    m_cond_var.wait (ulock, pred);

    if (pred ())
      {
	destroy_group (m_group_stack.back ());
	m_group_stack.pop_back ();
      }

    // should be freed for all XASL structure
    while (m_deferred_free_stack.empty () == false && m_deferred_free_stack.back () == m_group_stack.back())
      {
	destroy_group (m_group_stack.back ());
	m_group_stack.pop_back ();
	m_deferred_free_stack.pop_back ();
      }

    if (m_group_stack.empty())
      {
	// reset interrupt state
	m_is_interrupted = false;
	m_interrupt_id = NO_ERROR;
	m_is_running = false;

	// notify m_group_stack becomes empty ();
	ulock.unlock ();
	m_cond_var.notify_all ();
      }
  }

  method_invoke_group *
  runtime_context::top_stack ()
  {
    std::unique_lock<std::mutex> ulock (m_mutex);
    if (m_group_stack.empty())
      {
	return nullptr;
      }

    METHOD_GROUP_ID top = m_group_stack.back ();
    const auto &it = m_group_map.find (top);
    if (it == m_group_map.end ())
      {
	// should not happended
	assert (false);
	return nullptr;
      }

    return it->second;
  }

  void
  runtime_context::set_interrupt (int reason, std::string msg)
  {
    switch (reason)
      {
      /* no arg */
      case ER_INTERRUPTED:
      case ER_SP_TOO_MANY_NESTED_CALL:
      case ER_NET_SERVER_SHUTDOWN:
      case ER_SP_NOT_RUNNING_JVM:
      case ER_SES_SESSION_EXPIRED:
	m_is_interrupted = true;
	m_interrupt_id = reason;
	m_interrupt_msg.assign ("");
	break;

      /* 1 arg */
      case ER_SP_CANNOT_CONNECT_JVM:
      case ER_SP_NETWORK_ERROR:
      case ER_OUT_OF_VIRTUAL_MEMORY:
	m_is_interrupted = true;
	m_interrupt_id = reason;
	m_interrupt_msg.assign (msg);
	break;
      default:
	/* do nothing */
	break;
      }
  }

  void
  runtime_context::set_local_error_for_interrupt ()
  {
    handle_method_error (get_interrupt_id (), get_interrupt_msg ());
  }

  bool
  runtime_context::is_interrupted ()
  {
    return m_is_interrupted;
  }

  int
  runtime_context::get_interrupt_id ()
  {
    return m_interrupt_id;
  }

  std::string
  runtime_context::get_interrupt_msg ()
  {
    return m_interrupt_msg;
  }

  void
  runtime_context::wait_for_interrupt ()
  {
    auto pred = [this] () -> bool
    {
      // condition of finish
      return m_group_stack.empty () && is_running () == false;
    };

    if (pred ())
      {
	return;
      }

    std::unique_lock<std::mutex> ulock (m_mutex);
    m_cond_var.wait (ulock, pred);
  }

  bool
  runtime_context::is_running ()
  {
    return m_is_running;
  }

  query_cursor *
  runtime_context::get_cursor (cubthread::entry *thread_p, QUERY_ID query_id)
  {
    if (query_id == NULL_QUERY_ID)
      {
	return nullptr;
      }

    std::unique_lock<std::mutex> ulock (m_mutex);

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
  runtime_context::create_cursor (cubthread::entry *thread_p, QUERY_ID query_id, bool is_oid_included)
  {
    if (query_id == NULL_QUERY_ID || query_id >= SHRT_MAX)
      {
	// false query e.g) SELECT * FROM db_class WHERE 0 <> 0
	return nullptr;
      }

    std::unique_lock<std::mutex> ulock (m_mutex);
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
	int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
	QMGR_QUERY_ENTRY *query_entry_p = qmgr_get_query_entry (thread_p, query_id, tran_index);
	if (query_entry_p != NULL)
	  {
	    // m_list_id is going to be destoryed on server-side, so that qlist_count has to be updated
	    qfile_update_qlist_count (thread_p, query_entry_p->list_id, 1);

	    // store a new cursor in map
	    cursor = new query_cursor (thread_p, query_entry_p, is_oid_included);
	    m_cursor_map [query_id] = cursor;

	    assert (cursor != nullptr);
	  }
      }

    return cursor;
  }

  void
  runtime_context::destroy_cursor (cubthread::entry *thread_p, QUERY_ID query_id)
  {
    if (query_id == NULL_QUERY_ID)
      {
	/* do nothing */
	return;
      }

    std::unique_lock<std::mutex> ulock (m_mutex);

    // find in map
    auto search = m_cursor_map.find (query_id);
    if (search != m_cursor_map.end ())
      {
	query_cursor *cursor = search->second;
	if (cursor)
	  {
	    cursor->close ();
	    if (query_id > 0)
	      {
		(void) xqmgr_end_query (thread_p, query_id);
	      }
	    delete cursor;
	  }

	m_cursor_map.erase (search);
      }
  }

  void
  runtime_context::register_returning_cursor (cubthread::entry *thread_p, QUERY_ID query_id)
  {
    if (query_id == NULL_QUERY_ID)
      {
	/* do nothing */
	return;
      }

    std::unique_lock<std::mutex> ulock (m_mutex);

    m_returning_cursors.insert (query_id);
    // m_cursor_map.erase (query_id);
  }

  void
  runtime_context::deregister_returning_cursor (cubthread::entry *thread_p, QUERY_ID query_id)
  {
    if (query_id == NULL_QUERY_ID)
      {
	/* do nothing */
	return;
      }

    std::unique_lock<std::mutex> ulock (m_mutex);

    m_returning_cursors.erase (query_id);
  }

  void
  runtime_context::destroy_group (METHOD_GROUP_ID id)
  {
    // assume that lock is already acquired
    // std::unique_lock<std::mutex> ulock (m_mutex);

    // find in map
    auto search = m_group_map.find (id);
    if (search != m_group_map.end ())
      {
	method_invoke_group *group = search->second;
	if (group)
	  {
	    delete group;
	  }
	m_group_map.erase (search);
      }
  }

  void
  runtime_context::destroy_all_groups ()
  {
    std::unique_lock<std::mutex> ulock (m_mutex);
    for (auto &it : m_group_map)
      {
	if (it.second)
	  {
	    delete it.second;
	  }
      }
    m_group_map.clear ();
  }

  void
  runtime_context::destroy_all_cursors ()
  {
    std::unique_lock<std::mutex> ulock (m_mutex);
    for (auto &it : m_cursor_map)
      {
	/*
	if (cubthread::get_manager () != NULL)
	  {
	    destroy_cursor (&cubthread::get_entry (), it.first);
	  }
	*/
	if (it.second)
	  {
	    delete it.second;
	  }
      }
    m_cursor_map.clear ();
    m_returning_cursors.clear ();
  }

  connection_pool &
  runtime_context::get_connection_pool ()
  {
    return m_conn_pool;
  }

} // cubmethod
