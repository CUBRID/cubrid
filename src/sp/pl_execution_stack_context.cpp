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
    , m_client_header (-1,  METHOD_REQUEST_CALLBACK /* default */, 0)
    , m_java_header (-1,  SP_CODE_INTERNAL_JDBC /* default */, 0)
    , m_connection {nullptr}
    , m_req_id {0}
  {
    m_tid = logtb_find_current_tranid (thread_p);
    m_is_running = false;

    session *sess = get_session ();
    if (sess)
      {
	m_client_header.id = sess->get_id ();
	m_java_header.id = sess->get_id ();
      }
  }

  execution_stack::~execution_stack ()
  {
    // use local variable
    session *sess = get_session ();
    if (sess)
      {
	destory_all_cursors (sess);

	if (m_connection && (sess->is_interrupted () || er_errid () != NO_ERROR))
	  {
	    m_connection->invalidate ();
	  }

	sess->release_connection (m_connection); // release connection to session
	sess->pop_and_destroy_stack (get_id ());
      }
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

  void
  execution_stack::reset_query_handlers ()
  {
    if (m_stack_handler_id.empty ())
      {
	// do nothing
	return;
      }

    set_cs_command (METHOD_REQUEST_END);
    std::vector<int> handler_vec (m_stack_handler_id.begin (), m_stack_handler_id.end ());
    send_data_to_client (handler_vec);
    m_stack_handler_id.clear ();

    // restore to callback mode
    set_cs_command (METHOD_REQUEST_CALLBACK);
  }

  int
  execution_stack::add_cursor (int handler_id, QUERY_ID query_id, bool oid_included)
  {
    if (query_id == NULL_QUERY_ID || query_id >= SHRT_MAX)
      {
	// false query e.g) SELECT * FROM db_class WHERE 0 <> 0
	return ER_FAILED;
      }

    m_stack_cursor_id.insert (query_id);
    m_stack_cursor_map[query_id] = handler_id;

    query_cursor *cursor = nullptr;
    session *sess = get_session ();
    if (sess)
      {
	cursor = sess->create_cursor (m_thread_p, query_id, oid_included);
      }

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

    session *sess = get_session ();
    if (sess)
      {
	cursor = sess->get_cursor (m_thread_p, query_id);
	if (cursor == nullptr)
	  {
	    if (sess->is_session_cursor (query_id))
	      {
		cursor = sess->create_cursor (m_thread_p, query_id, false);
		if (cursor)
		  {
		    // add to the clearing list at the end of stack
		    sess->remove_session_cursor (m_thread_p, query_id);
		    m_stack_cursor_id.insert (query_id);
		  }
	      }
	  }
      }

    return cursor;
  }

  void
  execution_stack::promote_to_session_cursor (QUERY_ID query_id)
  {
    if (query_id == NULL_QUERY_ID)
      {
	return;
      }

    session *sess = get_session ();
    if (sess)
      {
	sess->add_session_cursor (m_thread_p, query_id);
      }

    // remove from stack resource
    m_stack_cursor_id.erase (query_id);
    m_stack_handler_id.erase (m_stack_cursor_map[query_id]);
  }

  void
  execution_stack::destory_all_cursors (session *sess)
  {
    for (auto &cursor_it : m_stack_cursor_id)
      {
	// If the cursor is received from the child function and is not returned to the parent function, the cursor remains in m_cursor_set.
	// So here trying to find the cursor Id in the global returning cursor storage and remove it if exists.
	sess->remove_session_cursor (m_thread_p, cursor_it);

	sess->destroy_cursor (m_thread_p, cursor_it);
      }

    m_stack_cursor_id.clear ();
  }

  connection_view &
  execution_stack::get_connection ()
  {
    if (m_connection == nullptr)
      {
	session *sess = get_session ();
	if (sess)
	  {
	    m_connection = sess->claim_connection ();
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
	m_connection->invalidate ();
	session *sess = get_session ();
	if (sess)
	  {
	    sess->set_local_error_for_interrupt ();
	    return sess->get_interrupt_id ();
	  }
	else
	  {
	    return ER_INTERRUPTING;
	  }
      }
    return NO_ERROR;
  }
}
