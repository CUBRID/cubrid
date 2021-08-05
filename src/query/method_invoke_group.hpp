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
 * method_invoke_group.hpp
 */

#ifndef _METHOD_INVOKE_GROUP_HPP_
#define _METHOD_INVOKE_GROUP_HPP_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include <algorithm>
#include <functional>		/* std::function */
#include <vector>
#include <unordered_set>

#include "method_def.hpp"	/* method_sig_node */
#include "mem_block.hpp"	/* cubmem::block, cubmem::extensible_block */
#include "porting.h" /* SOCKET */

// thread_entry.hpp
namespace cubthread
{
  class entry;
}

namespace cubmethod
{
//////////////////////////////////////////////////////////////////////////
// Method Group to invoke together
//////////////////////////////////////////////////////////////////////////
  // forward declarations
  class method_invoke;

  class method_connection
  {
    public:
      method_connection (METHOD_TYPE m_type);

      using request_prepare_func = std::function <int (cubthread::entry *thread_p, cubmem::block &)>;
      using request_invoke_func = std::function <int (cubthread::entry *thread_p, cubmem::block &)>;

      request_prepare_func m_prepare_func;

    private:
      METHOD_TYPE m_type;
  };

  class method_invoke_group
  {
    public:
      method_invoke_group () = delete; // Not DefaultConstructible
      method_invoke_group (method_sig_list *sigs);

      method_invoke_group (method_invoke_group &&other) = delete; // Not MoveConstructible
      method_invoke_group (const method_invoke_group &copy) = delete; // Not CopyConstructible

      method_invoke_group &operator= (method_invoke_group &&other) = delete; // Not MoveAssignable
      method_invoke_group &operator= (const method_invoke_group &copy) = delete; // Not CopyAssignable

      ~method_invoke_group ();

      int begin (cubthread::entry *thread_p);
      int prepare (std::vector <DB_VALUE> &arg_base);
      int execute (std::vector <DB_VALUE> &arg_base);
      int reset ();
      int end ();

      DB_VALUE &get_return_value (int index);

      int get_num_methods () const;
      int64_t get_id () const;
      SOCKET get_socket () const;

    private:
      /* Temporarily, method_invoke_group has socket fd here */
      /* javasp will get/release connection from globally */
      SOCKET m_socket;
      int connect ();
      int disconnect ();

      cubthread::entry *m_thread_p;
      int64_t m_id;
      std::vector <METHOD_TYPE> m_kind_type;
      std::vector <method_invoke *> m_method_vector;

      std::vector <DB_VALUE> m_result_vector;	/* placeholder for result value */
  };

}				// namespace cubmethod

#endif				/* _METHOD_INVOKE_GROUP_HPP_ */
