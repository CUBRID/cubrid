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
#include <memory> /* unique_ptr */
#include <set>
#include <queue>

#include "pl_connection.hpp" /* cubpl::connection */

#include "method_struct_parameter_info.hpp" /* db_parameter_info */
#include "mem_block.hpp"	/* cubmem::block, cubmem::extensible_block */
#include "porting.h" /* SOCKET */
#include "storage_common.h"

#include "pl_execution_stack_context.hpp"
#include "pl_signature.hpp"
#include "pl_session.hpp"

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

  class method_invoke_group
  {
    public:
      method_invoke_group () = delete; // Not DefaultConstructible
      method_invoke_group (cubpl::pl_signature_array &sig);

      method_invoke_group (method_invoke_group &&other) = delete; // Not MoveConstructible
      method_invoke_group (const method_invoke_group &copy) = delete; // Not CopyConstructible

      method_invoke_group &operator= (method_invoke_group &&other) = delete; // Not MoveAssignable
      method_invoke_group &operator= (const method_invoke_group &copy) = delete; // Not CopyAssignable

      ~method_invoke_group ();

      void begin ();
      int prepare (std::vector<std::reference_wrapper<DB_VALUE>> &arg_base);
      int execute (std::vector<std::reference_wrapper<DB_VALUE>> &arg_base);
      int reset (bool is_end_query);
      void end ();

      int get_num_methods ();
      DB_VALUE &get_return_value (int index);

      METHOD_GROUP_ID get_id () const;
      std::queue<cubmem::block> &get_data_queue ();

      bool is_running () const;

      // error
      std::string get_error_msg ();
      void set_error_msg (const std::string &msg);

      void destroy_resources ();

    private:
      bool m_is_running;

      std::string m_err_msg;

      METHOD_GROUP_ID m_id;
      cubpl::execution_stack *m_stack;
      cubpl::pl_signature_array &m_sig_array;

      std::vector <DB_VALUE> m_result_vector;	/* placeholder for result value */
  };

} // namespace cubmethod

#endif				/* _METHOD_INVOKE_GROUP_HPP_ */
