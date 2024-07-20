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
 * pl_execution_stack.hpp: managing subprograms of a server task
 */

#ifndef _PL_EXECUTION_STACK_HPP_
#define _PL_EXECUTION_STACK_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include <unordered_set>

#include "dbtype_def.h"
#include "query_list.h"
#include "query_executor.h"
#include "mem_block.hpp"
#include "packer.hpp"

#include "method_struct_invoke.hpp"
#include "method_connection_pool.hpp"

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
      cubthread::entry *m_thread_p;
      session *m_rctx;

      SESSION_ID m_sid;
      TRANID m_tid;

      std::unordered_set <int> m_stack_handler_id;
      std::unordered_set <std::uint64_t> m_stack_cursor_id;

      bool m_is_running;
      bool m_is_for_scan;

      void destory_all_cursors ();

    public:
      execution_stack () = delete; // Not DefaultConstructible
      execution_stack (cubthread::entry *thread_p);

      execution_stack (execution_stack &&other) = delete; // Not MoveConstructible
      execution_stack (const execution_stack &copy) = delete; // Not CopyConstructible

      execution_stack &operator= (execution_stack &&other) = delete; // Not MoveAssignable
      execution_stack &operator= (const execution_stack &copy) = delete; // Not CopyAssignable

      virtual ~execution_stack () {};

      /* getters */
      PL_STACK_ID get_id () const;
      TRANID get_tran_id ();
      SESSION_ID get_session_id () const;

      session *get_pl_session () const;
      cubthread::entry *get_thread_entry () const;

      cubmethod::connection *get_connection () const;

      /* resource management */

      query_cursor *create_cursor (QUERY_ID query_id, bool oid_included);
      query_cursor *get_cursor (QUERY_ID query_id);
      void register_returning_cursor (QUERY_ID query_id);
      // void deregister_returning_cursor (QUERY_ID query_id);

      void register_stack_query_handler (int handler_id);
      /*
      PL_QUERY_CURSOR *create_cursor (QUERY_ID query_id, bool oid_included);
      PL_QUERY_CURSOR *get_cursor (QUERY_ID query_id);
      void register_returning_cursor (QUERY_ID query_id);
      void deregister_returning_cursor (QUERY_ID query_id);

      // client handelr
      void register_client_handler (int handler_id);
      */

      const std::unordered_set <int> *get_stack_query_handler () const;
      const std::unordered_set <std::uint64_t> *get_stack_cursor () const;

      virtual bool is_for_scan () const = 0; // METHOD SCAN or SP STACK
  };
}

using PL_EXECUTION_STACK = cubpl::execution_stack;

#endif //_PL_EXECUTION_STACK_HPP_
