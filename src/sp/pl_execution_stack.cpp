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

#include "pl_execution_stack.hpp"

#include "session.h"

namespace cubpl
{
  execution_stack::execution_stack (cubthread::entry *thread_p)
    : m_id ((std::uint64_t) this)
    , m_thread_p (thread_p)
  {
    // init runtime context
    session_get_pl_session (thread_p, m_rctx);
    session_get_session_id (thread_p, &m_sid);

    m_tid = logtb_find_current_tranid (thread_p);

    m_is_running = false;
  }

  void
  execution_stack::register_stack_query_handler (int handler_id)
  {
    m_stack_handler_id.insert (handler_id);
  }

  query_cursor *
  execution_stack::create_cursor (QUERY_ID query_id, bool oid_included)
  {
    if (query_id == NULL_QUERY_ID || query_id >= SHRT_MAX)
      {
	// false query e.g) SELECT * FROM db_class WHERE 0 <> 0
	return nullptr;
      }

    m_stack_cursor_id.insert (query_id);
    return get_pl_session ()->create_cursor (m_thread_p, query_id, oid_included);
  }

  cubmethod::connection *
  execution_stack::get_connection () const
  {
    // TODO
    return nullptr;
  }

  void
  execution_stack::register_returning_cursor (QUERY_ID query_id)
  {
    get_pl_session ()->register_returning_cursor (m_thread_p, query_id);
    m_stack_cursor_id.erase (query_id);
  }

  query_cursor *
  execution_stack::get_cursor (QUERY_ID query_id)
  {
    return get_pl_session ()->get_cursor (m_thread_p, query_id);
  }

  void
  execution_stack::destory_all_cursors ()
  {
    for (auto &cursor_it : m_stack_cursor_id)
      {
	// If the cursor is received from the child function and is not returned to the parent function, the cursor remains in m_cursor_set.
	// So here trying to find the cursor Id in the global returning cursor storage and remove it if exists.
	get_pl_session ()->deregister_returning_cursor (m_thread_p, cursor_it);

	get_pl_session ()->destroy_cursor (m_thread_p, cursor_it);
      }

    m_stack_cursor_id.clear ();
  }

  PL_STACK_ID
  execution_stack::get_id () const
  {
    return m_id;
  }

  SESSION_ID
  execution_stack::get_session_id () const
  {
    return m_sid;
  }

  TRANID
  execution_stack::get_tran_id ()
  {
    m_tid = logtb_find_current_tranid (m_thread_p);
    return m_tid;
  }

  cubpl::session *
  execution_stack::get_pl_session () const
  {
    return m_rctx;
  }

  cubthread::entry *
  execution_stack::get_thread_entry () const
  {
    return m_thread_p;
  }


  const std::unordered_set <int> *
  execution_stack::get_stack_query_handler () const
  {
    return &m_stack_handler_id;
  }

  const std::unordered_set <std::uint64_t> *
  execution_stack::get_stack_cursor () const
  {
    return &m_stack_cursor_id;
  }
}
