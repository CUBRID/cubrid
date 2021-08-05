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

#ifndef _METHOD_INVOKE_HPP_
#define _METHOD_INVOKE_HPP_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include <functional>		/* std::function */
#include <vector>

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
  // forward declarations
  class method_invoke_group;

  class method_invoke
  {
    public:
      virtual ~method_invoke () = default;

      virtual int invoke (cubthread::entry *thread_p) = 0;
      virtual int get_return (cubthread::entry *thread_p, std::vector <DB_VALUE> &arg_base, DB_VALUE &result) = 0;
  };

  class method_invoke_builtin : public method_invoke
  {
    public:
      method_invoke_builtin () = delete;
      method_invoke_builtin (method_invoke_group *group, method_sig_node *method_sig);

      int invoke (cubthread::entry *thread_p) override;
      int get_return (cubthread::entry *thread_p, std::vector <DB_VALUE> &arg_base, DB_VALUE &result) override;
    private:
      method_invoke_group *m_group;
      method_sig_node *m_method_sig;
  };

  class method_invoke_java : public method_invoke
  {
    public:
      method_invoke_java () = delete;
      method_invoke_java (method_invoke_group *group, method_sig_node *method_sig);

      int invoke (cubthread::entry *thread_p) override;
      int get_return (cubthread::entry *thread_p, std::vector <DB_VALUE> &arg_base, DB_VALUE &result) override;

    private:
      int alloc_response (cubmem::extensible_block &blk);
      int receive_result (cubmem::extensible_block &blk, std::vector <DB_VALUE> &arg_base, DB_VALUE &returnval);
      int receive_error (cubmem::extensible_block &blk);
      int callback_dispatch (cubmem::extensible_block &blk);

      method_invoke_group *m_group;
      SOCKET m_sock_fd = INVALID_SOCKET;
      method_sig_node *m_method_sig;
  };

}				// namespace cubmethod

#endif				/* _METHOD_INVOKE_HPP_ */
