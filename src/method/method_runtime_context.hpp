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
// method_runtime_context.hpp
//

#ifndef _METHOD_RUNTIME_CONTEXT_HPP_
#define _METHOD_RUNTIME_CONTEXT_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include <unordered_map>
#include <unordered_set>
#include <deque>

#if defined (SERVER_MODE)
#include "server_support.h"
#endif

#include "method_connection_pool.hpp"
#include "method_def.hpp"

// thread_entry.hpp
namespace cubthread
{
  class entry;
}

namespace cubmethod
{
  // forward declarations
  class method_invoke_group;
  class query_cursor;
  class connection_pool;

  using THREAD_ENTRY_IDX = int;
  using QUERY_ID = std::uint64_t;
  using METHOD_GROUP_ID = std::uint64_t;

  class runtime_context
  {
    public:
      runtime_context ();
      ~runtime_context ();

      query_cursor *create_cursor (cubthread::entry *thread_p, QUERY_ID query_id, bool oid_included = false);
      query_cursor *get_cursor (cubthread::entry *thread_p, QUERY_ID query_id);
      void destroy_cursor (cubthread::entry *thread_p, QUERY_ID query_id);
      void register_returning_cursor (cubthread::entry *thread_p, QUERY_ID query_id);

      method_invoke_group *create_invoke_group (cubthread::entry *thread_p, const method_sig_list &siglist);

      void push_stack (method_invoke_group *group);
      void pop_stack ();
      method_invoke_group *top_stack ();

    private:
      std::deque <METHOD_GROUP_ID> m_group_stack;
      std::unordered_map <METHOD_GROUP_ID, method_invoke_group *> m_groups;
      std::unordered_map <QUERY_ID, query_cursor *> m_cursor_map; // server-side cursor storage
      std::unordered_set <QUERY_ID> m_returning_cursors;
  };

  /* global interface */
  runtime_context *get_rctx (cubthread::entry *thread_p);
} // cubmethod

// alias declaration for legacy C files
using method_runtime_context = cubmethod::runtime_context;

#endif // _METHOD_RUNTIME_CONTEXT_HPP_