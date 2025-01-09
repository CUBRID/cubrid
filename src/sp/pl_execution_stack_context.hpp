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

/*
 * pl_execution_stack_context.hpp: managing subprograms of a server task
 */

#ifndef _PL_EXECUTION_STACK_HPP_
#define _PL_EXECUTION_STACK_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include <unordered_set>
#include <map>

#include "dbtype_def.h"
#include "error_manager.h"
#include "query_list.h"
#include "query_executor.h"
#include "mem_block.hpp"
#include "packer.hpp"

#include "network_callback_sr.hpp"
#include "method_struct_invoke.hpp"
#include "pl_connection.hpp"

// thread_entry.hpp
namespace cubthread
{
  class entry;
}

using PL_STACK_ID = uint64_t;

namespace cubpl
{
  class session;
  class query_cursor;

  class execution_stack
  {
    protected:
      PL_STACK_ID m_id;
      TRANID m_tid;

      cubthread::entry *m_thread_p;
      session *m_session;

      /* resources */
      std::unordered_set <int> m_stack_handler_id;
      std::unordered_set <std::uint64_t> m_stack_cursor_id;
      std::map <std::uint64_t, int> m_stack_cursor_map;

      connection_view m_connection;

      /* error */
      std::string m_error_message;

      bool m_is_running;

      int interrupt_handler ();

    public:
      execution_stack () = delete; // Not DefaultConstructible
      execution_stack (cubthread::entry *thread_p);

      execution_stack (execution_stack &&other) = delete; // Not MoveConstructible
      execution_stack (const execution_stack &copy) = delete; // Not CopyConstructible

      execution_stack &operator= (execution_stack &&other) = delete; // Not MoveAssignable
      execution_stack &operator= (const execution_stack &copy) = delete; // Not CopyAssignable

      ~execution_stack ();

      /* getters */
      PL_STACK_ID get_id () const;
      TRANID get_tran_id ();

      /* session and thread */
      cubthread::entry *get_thread_entry () const;

      /* connection */
      connection_view &get_connection ();
      std::queue<cubmem::block> &get_data_queue ();

      /* resource management */
      int add_cursor (int handler_id, QUERY_ID query_id, bool oid_included);
      void remove_cursor (QUERY_ID query_id);
      query_cursor *get_cursor (QUERY_ID query_id);
      void promote_to_session_cursor (QUERY_ID query_id);
      void destory_all_cursors ();

      /* query handler */
      void add_query_handler (int handler_id);
      void remove_query_handler (int handler_id);
      void reset_query_handlers ();

      const std::unordered_set <int> *get_stack_query_handler () const;
      const std::unordered_set <std::uint64_t> *get_stack_cursor () const;

      // runtime (temporary)
      std::queue<cubmem::block> m_data_queue;

      cubmethod::header m_client_header; // header sending to cubridcs
      cubmethod::header m_java_header; // header sending to cub_javasp
      bool m_transaction_control;

      template <typename ... Args>
      int send_data_to_client (Args &&... args)
      {
	return xs_callback_send_no_receive (m_thread_p, m_client_header, std::forward<Args> (args)...);
      }

      template <typename ... Args>
      int send_data_to_client_recv (const xs_callback_func &func, Args &&... args)
      {
	return xs_callback_send_and_receive (m_thread_p, func, m_client_header, std::forward<Args> (args)...);
      }

      template <typename ... Args>
      int send_data_to_java (Args &&... args)
      {
	m_java_header.req_id = get_and_increment_request_id ();
	connection_view &conn = get_connection();
	if (!conn)
	  {
	    assert (er_errid () != NO_ERROR);
	    return er_errid (); // Handle the case where connection is unavailable
	  }

	return conn->send_buffer_args (m_java_header, std::forward<Args> (args)...);
      }

      int
      read_data_from_java (cubmem::block &b)
      {
	connection_view &conn = get_connection();
	if (!conn)
	  {
	    assert (er_errid () != NO_ERROR);
	    return er_errid (); // Handle the case where connection is unavailable
	  }

	pl_callback_func interrupt_func = [this]()
	{
	  return interrupt_handler ();
	};

	return conn->receive_buffer (b, &interrupt_func, 500);
      }

      void
      set_cs_command (int command)
      {
	m_client_header.command = command;
      }

      void
      set_java_command (int command)
      {
	m_java_header.command = command;
      }

      int m_req_id;
      inline int get_and_increment_request_id ()
      {
	return m_req_id++;
      }

      std::string m_error_msg;
      void set_error_message (std::string error_msg)
      {
	m_error_message = error_msg;
      }

      std::string get_error_message ()
      {
	return m_error_message;
      }

      void read_payload_block (cubpacking::unpacker &unpacker);
  };
}

using PL_EXECUTION_STACK = cubpl::execution_stack;

#endif //_PL_EXECUTION_STACK_HPP_
