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
 * pl_executor.hpp
 */

#ifndef _PL_EXECUTOR_HPP_
#define _PL_EXECUTOR_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "pl_execution_stack_context.hpp"
#include "pl_signature.hpp"
#include "pl_session.hpp"

// forward definitions
struct regu_variable_list_node;

namespace cubpl
{
  struct invoke_java : public cubpacking::packable_object
  {
    invoke_java () = delete;
    invoke_java (uint64_t g_id, int tran_id, pl_signature *sig, bool tc);

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    uint64_t g_id;
    int tran_id;

    std::string signature;
    std::string auth;
    int lang;
    int num_args;
    std::vector<int> arg_mode;
    std::vector<int> arg_type;
    int result_type;

    bool transaction_control; // TODO: wrap it with proper structs
  };

  class executor
  {
    public:
      executor () = delete; // Not DefaultConstructible
      executor (pl_signature &sig);

      executor (executor &&other) = delete; // Not MoveConstructible
      executor (const executor &copy) = delete; // Not CopyConstructible

      executor &operator= (executor &&other) = delete; // Not MoveAssignable
      executor &operator= (const executor &copy) = delete; // Not CopyAssignable

      ~executor ();

      // args
      int fetch_args_peek (regu_variable_list_node *val_list_p, VAL_DESCR *val_desc_p, OID *obj_oid_p,
			   QFILE_TUPLE tuple); // QUERY
      int fetch_args_peek (std::vector <std::reference_wrapper <DB_VALUE>> args); // CALL

      // execute
      int execute (DB_VALUE &value);

      // getter
      std::vector <DB_VALUE> &get_out_args ();
      execution_stack *get_stack ();

    private:
      execution_stack *m_stack;
      pl_signature &m_sig;

      std::vector <std::reference_wrapper <DB_VALUE>> m_args;
      std::vector <DB_VALUE> m_out_args;

      // check
      int check_unsupported_dbtype ();
      bool is_supported_dbtype (const DB_VALUE &val);

      int change_exec_rights (const char *auth_name);

      void handle_type_resultset (DB_VALUE &returnval);

      // command handling
      int request_invoke_command ();
      int response_invoke_command (DB_VALUE &value);

      int response_result (int code, DB_VALUE &returnval);
      int response_callback_command ();

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
      int callback_change_auth_rights (cubthread::entry &thread_ref, packing_unpacker &unpacker);
      int callback_get_code_attr (cubthread::entry &thread_ref, packing_unpacker &unpacker);
  };
}

#endif