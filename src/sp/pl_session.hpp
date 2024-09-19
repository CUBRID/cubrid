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

//
// pl_session.hpp
//

#ifndef _PL_SESSION_HPP_
#define _PL_SESSION_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <condition_variable>
#include <string>

#include "method_connection_pool.hpp"

#include "pl_execution_stack_context.hpp"
#include "pl_signature.hpp"

// thread_entry.hpp
namespace cubthread
{
  class entry;
}

namespace cubmethod
{
  class method_invoke_group;
  class db_parameter_info;
}

namespace cubsp
{
  class connection_pool;
}

namespace cubpl
{
  // forward declarations
  class query_cursor;
  class execution_stack;

  using THREAD_ENTRY_IDX = int;
  using QUERY_ID = std::uint64_t;

  class session
  {
    public:
      session ();
      ~session ();

      using exec_stack_map_type = std::unordered_map <PL_STACK_ID, execution_stack *>;
      using exec_stack_id_type = std::vector <PL_STACK_ID>;
      using exec_stack_iter = std::unordered_map <PL_STACK_ID, execution_stack *>::iterator;
      using cursor_map_type = std::unordered_map <QUERY_ID, query_cursor *>;
      using cursor_iter = std::unordered_map <QUERY_ID, query_cursor *>::iterator;

      /* cursor management */
      query_cursor *create_cursor (cubthread::entry *thread_p, QUERY_ID query_id, bool oid_included = false);
      query_cursor *get_cursor (cubthread::entry *thread_p, QUERY_ID query_id);
      void destroy_cursor (cubthread::entry *thread_p, QUERY_ID query_id);

      void add_session_cursor (cubthread::entry *thread_p, QUERY_ID query_id);
      void remove_session_cursor (cubthread::entry *thread_p, QUERY_ID query_id);

      /* stack management */

      // Currently these functions are used for debugging purpose.
      // In the recursive call situation, each time the function is called, a new worker from the thread pool is assigned. With this code, you can easily know the current state.
      // In the future, these functions will resolve some cases when it is necessary to set an error for all threads participating in a recursive call e.g. interrupt
      execution_stack *create_and_push_stack (cubthread::entry *thread_p);
      void pop_and_destroy_stack (execution_stack *&stack);
      execution_stack *top_stack ();

      /* getter */
      SESSION_ID get_id ();

      void set_interrupt (int reason, std::string msg = "");
      bool is_interrupted ();
      int get_interrupt_id ();
      std::string get_interrupt_msg ();

      void wait_for_interrupt ();
      void set_local_error_for_interrupt (); // set interrupt on thread local error manager

      int get_depth ();

      bool is_running ();

      inline METHOD_REQ_ID get_and_increment_request_id ()
      {
	return m_req_id++;
      }

      cubmethod::connection_pool *get_connection_pool ();

      cubmethod::db_parameter_info *get_db_parameter_info () const;
      void set_db_parameter_info (cubmethod::db_parameter_info *param_info);

    private:
      execution_stack *top_stack_internal ();
      void destroy_all_cursors ();

      std::mutex m_mutex;
      std::condition_variable m_cond_var;

      std::unordered_set <QUERY_ID> m_session_cursors;

      exec_stack_map_type m_stack_map; // method executor storage
      exec_stack_id_type m_exec_stack; // runtime stack (implemented using vector)
      int m_stack_idx;


      cursor_map_type m_cursor_map; // server-side cursor storage

      exec_stack_id_type m_deferred_free_stack;

      cubmethod::connection_pool m_conn_pool;

      std::atomic <METHOD_REQ_ID> m_req_id;

      cubmethod::db_parameter_info *m_param_info;

      bool m_is_interrupted;
      int m_interrupt_id;
      std::string m_interrupt_msg;

      bool m_is_running;

      SESSION_ID m_id;
  };

  /* global interface */
  session *get_session ();
} // cubmethod

// alias declaration for legacy C files
using PL_SESSION = cubpl::session;

#endif // _PL_SESSION_HPP_
