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

#include <vector>
#include <unordered_map>
#include <queue>

#include "dbtype.h"		/* db_value_* */
#include "method_def.hpp"	/* method_sig_node */
#include "method_query_cursor.hpp"
#include "method_struct_invoke.hpp" /* cubmethod::header */
#include "mem_block.hpp"	/* cubmem::block, cubmem::extensible_block */
#include "porting.h" /* SOCKET */

#if defined (SA_MODE)
#include "query_method.hpp"
#endif

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
      method_invoke () = delete; // Not DefaultConstructible
      method_invoke (method_invoke_group *group, method_sig_node *sig) : m_group (group), m_method_sig (sig) {}
      virtual ~method_invoke () {};

      method_invoke (method_invoke &&other) = delete; // Not MoveConstructible
      method_invoke (const method_invoke &copy) = delete; // Not CopyConstructible

      method_invoke &operator= (method_invoke &&other) = delete; // Not MoveAssignable
      method_invoke &operator= (const method_invoke &copy) = delete; // Not CopyAssignable

      virtual int invoke (cubthread::entry *thread_p, std::vector<std::reference_wrapper<DB_VALUE>> &arg_base) = 0;
      virtual int get_return (cubthread::entry *thread_p, std::vector<std::reference_wrapper<DB_VALUE>> &arg_base,
			      DB_VALUE &result) = 0;

      uint64_t get_id ()
      {
	return (uint64_t) this;
      }

    protected:
      method_invoke_group *m_group;
      method_sig_node *m_method_sig;
  };

  class method_invoke_builtin : public method_invoke
  {
    public:
      method_invoke_builtin () = delete;
      method_invoke_builtin (method_invoke_group *group, method_sig_node *method_sig);

      int invoke (cubthread::entry *thread_p, std::vector<std::reference_wrapper<DB_VALUE>> &arg_base) override;
      int get_return (cubthread::entry *thread_p, std::vector<std::reference_wrapper<DB_VALUE>> &arg_base,
		      DB_VALUE &result) override;
  };

  class method_invoke_java : public method_invoke
  {
    public:
      method_invoke_java () = delete;
      method_invoke_java (method_invoke_group *group, method_sig_node *method_sig);
      ~method_invoke_java ();

      int invoke (cubthread::entry *thread_p, std::vector<std::reference_wrapper<DB_VALUE>> &arg_base) override;
      int get_return (cubthread::entry *thread_p, std::vector<std::reference_wrapper<DB_VALUE>> &arg_base,
		      DB_VALUE &result) override;

    private:
      int receive_result (std::vector<std::reference_wrapper<DB_VALUE>> &arg_base,
			  DB_VALUE &returnval);
      int receive_error ();

      int callback_dispatch (cubthread::entry &thread_ref);

      int callback_get_db_parameter (cubthread::entry &thread_ref, packing_unpacker &unpacker);
      int callback_prepare (cubthread::entry &thread_ref, packing_unpacker &unpacker);
      int callback_execute (cubthread::entry &thread_ref, packing_unpacker &unpacker);
      int callback_fetch (cubthread::entry &thread_ref, packing_unpacker &unpacker);
      int callback_oid_get (cubthread::entry &thread_ref, packing_unpacker &unpacker);
      int callback_oid_put (cubthread::entry &thread_ref, packing_unpacker &unpacker);
      int callback_oid_cmd (cubthread::entry &thread_ref, packing_unpacker &unpacker);
      int callback_collection_cmd (cubthread::entry &thread_ref, packing_unpacker &unpacker);
      int callback_make_outresult (cubthread::entry &thread_ref, packing_unpacker &unpacker);
      int callback_get_generated_keys (cubthread::entry &thread_ref, packing_unpacker &unpacker);
      int callback_end_transaction (cubthread::entry &thread_ref, packing_unpacker &unpacker);

      void erase_query_cursor (const std::uint64_t query_id);



      const cubmethod::header &get_next_java_header (cubmethod::header &header);

      cubmethod::header m_client_header; // header sending to cubridcs
      cubmethod::header m_java_header; // header sending to cub_javasp
  };

} // namespace cubmethod

#endif				/* _METHOD_INVOKE_HPP_ */
