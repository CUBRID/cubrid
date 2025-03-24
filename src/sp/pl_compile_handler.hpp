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
 * pl_compile_handler.hpp
 */

#ifndef _PL_COMPILE_HANDLER_HPP_
#define _PL_COMPILE_HANDLER_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "pl_execution_stack_context.hpp"
#include "pl_signature.hpp"
#include "pl_session.hpp"

#include "pl_struct_compile.hpp"

// forward definitions
struct regu_variable_list_node;

namespace cubpl
{
  class EXPORT_IMPORT compile_handler
  {
    public:
      compile_handler ();
      ~compile_handler ();

      int compile (const compile_request &req, cubmem::extensible_block &out_blk);

    private:
      int read_request (cubmem::block &response_blk, int &code);

      void create_error_response (cubmem::extensible_block &res, int error_code);

      execution_stack *m_stack;
  };
}

#endif // _PL_COMPILE_HANDLER_HPP_