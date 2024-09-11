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
#include "method_def.hpp"
#include "pl_execution_stack.hpp"
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

      using invoke_stack_map_type = std::unordered_map <PL_STACK_ID, execution_stack *>;
      using invoke_stack_type = std::deque <PL_STACK_ID>;
      using invoke_stack_iter = std::unordered_map <PL_STACK_ID, execution_stack *>::iterator;
      using cursor_map_type = std::unordered_map <QUERY_ID, query_cursor *>;
      using cursor_iter = std::unordered_map <QUERY_ID, query_cursor *>::iterator;

      query_cursor *create_cursor (cubthread::entry *thread_p, QUERY_ID query_id, bool oid_included = false);
      query_cursor *get_cursor (cubthread::entry *thread_p, QUERY_ID query_id);
      void destroy_cursor (cubthread::entry *thread_p, QUERY_ID query_id);

      void register_returning_cursor (cubthread::entry *thread_p, QUERY_ID query_id);
      void deregister_returning_cursor (cubthread::entry *thread_p, QUERY_ID query_id);

      execution_stack *create_stack (cubthread::entry *thread_p, pl_signature *sig);
      execution_stack *create_stack (cubthread::entry *thread_p, pl_signature_array *sig); // for scan

      // Currently these functions are used for debugging purpose.
      // In the recursive call situation, each time the function is called, a new worker from the thread pool is assigned. With this code, you can easily know the current state.
      // In the future, these functions will resolve some cases when it is necessary to set an error for all threads participating in a recursive call e.g. interrupt
      void push_stack (cubthread::entry *thread_p, execution_stack *stack);
      void pop_stack (cubthread::entry *thread_p, execution_stack *claimed);
      execution_stack *top_stack ();

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

      cubmethod::connection_pool &get_connection_pool ();

      cubmethod::db_parameter_info *get_db_parameter_info () const;
      void set_db_parameter_info (cubmethod::db_parameter_info *param_info);

    private:
      void destroy_group (METHOD_GROUP_ID id);

      void destroy_all_groups ();
      void destroy_all_cursors ();

      std::mutex m_mutex;
      std::condition_variable m_cond_var;

      invoke_stack_type m_group_stack; // runtime stack
      std::unordered_set <QUERY_ID> m_returning_cursors;

      invoke_stack_map_type m_stack_map; // method executor storage
      cursor_map_type m_cursor_map; // server-side cursor storage

      invoke_stack_type m_deferred_free_stack;

      cubmethod::connection_pool m_conn_pool;

      std::atomic <METHOD_REQ_ID> m_req_id;

      cubmethod::db_parameter_info *m_param_info;

      bool m_is_interrupted;
      int m_interrupt_id;
      std::string m_interrupt_msg;

      bool m_is_running;
  };

  /* global interface */
  session *get_session (cubthread::entry *thread_p);
} // cubmethod

// alias declaration for legacy C files
using PL_SESSION = cubpl::session;

#endif // _PL_SESSION_HPP_