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

#include "pl_execution_stack_context.hpp"

#include "session.h"
#include "pl_sr.h"
#include "pl_comm.h"
#include "pl_connection.hpp"
#include "pl_query_cursor.hpp"

#include "log_impl.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"
namespace cubpl
{
  execution_stack::execution_stack (cubthread::entry *thread_p)
    : m_id ((std::uint64_t) this)
    , m_thread_p (thread_p)
    , m_session {get_session ()}
    , m_client_header (-1,  METHOD_REQUEST_CALLBACK /* default */, 0)
    , m_java_header (-1,  SP_CODE_INTERNAL_JDBC /* default */, 0)
    , m_connection {nullptr}
    , m_req_id {0}
  {
    m_tid = logtb_find_current_tranid (thread_p);
    m_is_running = false;

    if (m_session)
      {
	m_client_header.id = m_session->get_id ();
      }
  }

  execution_stack::~execution_stack ()
  {
    if (m_session)
      {
	// retire connection
	if (m_connection)
	  {
	    m_connection.reset ();
	  }
      }

    destory_all_cursors ();

    m_session->pop_and_destroy_stack (get_id ());
  }

  void
  execution_stack::add_query_handler (int handler_id)
  {
    m_stack_handler_id.insert (handler_id);
  }

  void
  execution_stack::remove_query_handler (int handler_id)
  {
    m_stack_handler_id.erase (handler_id);
  }

  int
  execution_stack::add_cursor (QUERY_ID query_id, bool oid_included)
  {
    if (query_id == NULL_QUERY_ID || query_id >= SHRT_MAX)
      {
	// false query e.g) SELECT * FROM db_class WHERE 0 <> 0
	return ER_FAILED;
      }

    m_stack_cursor_id.insert (query_id);

    query_cursor *cursor = m_session->create_cursor (m_thread_p, query_id, oid_included);
    return cursor ? NO_ERROR : ER_FAILED;
  }

  query_cursor *
  execution_stack::get_cursor (QUERY_ID query_id)
  {
    query_cursor *cursor = nullptr;

    if (query_id == NULL_QUERY_ID || query_id >= SHRT_MAX)
      {
	// false query e.g) SELECT * FROM db_class WHERE 0 <> 0
	return cursor;
      }

    cursor = m_session->get_cursor (m_thread_p, query_id);

    /*
    if (cursor == nullptr)
    {
        if (m_session->is_session_cursor (query_id))
        {
            cursor = m_session->create_cursor (m_thread_p, query_id, false);
            if (cursor)
                {
                  // add to the clearing list at the end of stack
                  m_stack_cursor_id.insert (query_id);
                }
        }
    }
    */

    return cursor;
  }

  void
  execution_stack::promote_to_session_cursor (QUERY_ID query_id)
  {
    m_session->add_session_cursor (m_thread_p, query_id);

    query_cursor *cursor = m_session->get_cursor (m_thread_p, query_id);
    if (cursor)
      {
	cursor->change_owner (nullptr);
      }

    m_stack_cursor_id.erase (query_id);
  }

  void
  execution_stack::destory_all_cursors ()
  {
    for (auto &cursor_it : m_stack_cursor_id)
      {
	// If the cursor is received from the child function and is not returned to the parent function, the cursor remains in m_cursor_set.
	// So here trying to find the cursor Id in the global returning cursor storage and remove it if exists.
	m_session->remove_session_cursor (m_thread_p, cursor_it);

	m_session->destroy_cursor (m_thread_p, cursor_it);
      }

    m_stack_cursor_id.clear ();
  }

  connection_view &
  execution_stack::get_connection ()
  {
    if (m_connection == nullptr)
      {
	connection_pool *pool = get_connection_pool ();
	if (pool)
	  {
	    m_connection = pool->claim ();
	  }
      }

    return m_connection;
  }

  PL_STACK_ID
  execution_stack::get_id () const
  {
    return m_id;
  }

  TRANID
  execution_stack::get_tran_id ()
  {
    m_tid = logtb_find_current_tranid (m_thread_p);
    return m_tid;
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

  void
  execution_stack::read_payload_block (cubpacking::unpacker &unpacker)
  {
    char *aligned_ptr = PTR_ALIGN (unpacker.get_curr_ptr(), MAX_ALIGNMENT);
    size_t payload_size = (size_t) (unpacker.get_buffer_end() - aligned_ptr);
    cubmem::block payload_blk (payload_size, aligned_ptr);
    m_data_queue.emplace (std::move (payload_blk));
  }

  std::queue<cubmem::block> &
  execution_stack::get_data_queue ()
  {
    return m_data_queue;
  }

  int
  execution_stack::interrupt_handler ()
  {
    bool dummy_continue;
    if (logtb_is_interrupted (m_thread_p, true, &dummy_continue))
      {
	m_session->set_local_error_for_interrupt ();
	m_connection->invalidate ();
	return m_session->get_interrupt_id ();
      }
    return NO_ERROR;
  }
}
