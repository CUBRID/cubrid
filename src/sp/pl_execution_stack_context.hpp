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
      connection_view m_connection;

      std::unordered_set <int> m_stack_handler_id;
      std::unordered_set <std::uint64_t> m_stack_cursor_id;
      std::map <std::uint64_t, int> m_stack_cursor_map;

      /* error */
      std::string m_error_message;

      bool m_is_running;

      /* PARALLEL_ENABLE contract (#108 D108-1/2): a declared SP promises never to touch the
       * server-side connection, so its client (CAS) callbacks are refused instead of relayed.
       * The px-worker arm is a safety net for an undeclared SP that reaches a worker through an
       * escape path the parallel checkers do not visit (an SP inside an aggregate operand): a
       * worker thread carries no request id of its own, so a callback from it would be sent to
       * the wrong rid. */
      bool m_is_parallel_enabled_sp;
      /* Decided once, at construction: the px marker is owned by the parallel task that set it,
       * and code that runs after the SP returns must reach the same verdict as code that ran
       * before it. */
      bool m_is_px_worker;
      /* sticky: set when a callback was refused, so the call fails even if the SP catches the
       * SQLException it was handed. */
      bool m_client_callback_rejected;

      int reject_client_callback ();

      int interrupt_handler ();

    public:
      execution_stack () = delete; // Not DefaultConstructible
      execution_stack (cubthread::entry *thread_p, session *sess);

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
      void destory_all_cursors (session *sess);

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

      bool is_px_worker_stack () const
      {
	return m_is_px_worker;
      }

      /* One predicate, enforced at both ends: the server refuses the callback, and the PL server
       * is told up front so it refuses jdbc:default:connection before a Connection object is
       * created (that object is cached per session, so concurrent px workers would share one). */
      bool is_server_side_sql_forbidden () const
      {
	return m_is_parallel_enabled_sp || m_is_px_worker;
      }

      void set_parallel_enabled_sp (bool is_parallel_enabled)
      {
	m_is_parallel_enabled_sp = is_parallel_enabled;
      }

      bool was_client_callback_rejected () const
      {
	return m_client_callback_rejected;
      }

      /* The PL server refused the connection before any callback was attempted, so the guard in
       * send_data_to_client_recv () never ran. Arm the same sticky flag from that report, so the
       * invocation fails in response_result () whatever the SP did with its exception. */
      void mark_client_callback_rejected ()
      {
	m_client_callback_rejected = true;
      }

      /* The two entry points below are the single chokepoint for everything this stack sends to
       * CAS; guarding them covers today's callbacks and any added later, with no way around. */

      template <typename ... Args>
      int send_data_to_client (Args &&... args)
      {
	if (is_px_worker_stack ())
	  {
	    /* The only no-receive callbacks are the execution-rights push/pop and the
	     * close-query-handlers notice. Neither carries SQL and neither is a reply Java is
	     * waiting for, so they are dropped rather than turned into an error: from a worker
	     * thread there is no rid to send them on, and once server-side SQL is impossible
	     * nothing on the CAS side can observe them. */
	    return NO_ERROR;
	  }

	return xs_callback_send_no_receive (m_thread_p, m_client_header, std::forward<Args> (args)...);
      }

      template <typename ... Args>
      int send_data_to_client_recv (const xs_callback_func &func, Args &&... args)
      {
	if (is_server_side_sql_forbidden ())
	  {
	    return reject_client_callback ();
	  }

	return xs_callback_send_and_receive (m_thread_p, func, m_client_header, std::forward<Args> (args)...);
      }

      template <typename ... Args>
      int send_data_to_java (Args &&... args)
      {
	connection_view &conn = get_connection();
	if (!conn)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_CONNECT_PL_SERVER, 1, "connection pool");
	    return ER_SP_CANNOT_CONNECT_PL_SERVER; // Handle the case where connection is unavailable
	  }

	return conn->send_buffer_args (m_java_header, std::forward<Args> (args)...);
      }

      int
      read_data_from_java (cubmem::block &b)
      {
	connection_view &conn = get_connection();
	if (!conn)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_CONNECT_PL_SERVER, 1, "connection pool");
	    return ER_SP_CANNOT_CONNECT_PL_SERVER; // Handle the case where connection is unavailable
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

      std::string m_error_msg;
      void set_error_message (std::string error_msg)
      {
	m_error_message = error_msg;
      }

      std::string get_error_message ()
      {
	if (m_error_message.empty () && er_errid () != NO_ERROR)
	  {
	    m_error_message = er_msg ();
	  }
	return m_error_message;
      }

      void read_payload_block (cubpacking::unpacker &unpacker);
  };
}

using PL_EXECUTION_STACK = cubpl::execution_stack;

#endif //_PL_EXECUTION_STACK_HPP_
