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

#include "method_connection_pool.hpp" /* cubmethod::connection */
#include "method_def.hpp"	/* method_sig_node */
#include "method_runtime_context.hpp" /* cubmethod::runtime_context */
#include "method_struct_parameter_info.hpp" /* db_parameter_info */
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

  class method_invoke_group
  {
    public:
      method_invoke_group () = delete; // Not DefaultConstructible
      method_invoke_group (cubthread::entry *thread_p, const method_sig_list &sigs, bool is_for_scan);

      method_invoke_group (method_invoke_group &&other) = delete; // Not MoveConstructible
      method_invoke_group (const method_invoke_group &copy) = delete; // Not CopyConstructible

      method_invoke_group &operator= (method_invoke_group &&other) = delete; // Not MoveAssignable
      method_invoke_group &operator= (const method_invoke_group &copy) = delete; // Not CopyAssignable

      ~method_invoke_group ();

      void begin ();
      int prepare (std::vector<std::reference_wrapper<DB_VALUE>> &arg_base, const std::vector<bool> &arg_use_vec);
      int execute (std::vector<std::reference_wrapper<DB_VALUE>> &arg_base);
      int reset (bool is_end_query);
      void destroy_resources ();
      void end ();

      DB_VALUE &get_return_value (int index);

      int get_num_methods () const;
      METHOD_GROUP_ID get_id () const;
      SOCKET get_socket () const;
      cubthread::entry *get_thread_entry () const;
      std::queue<cubmem::block> &get_data_queue ();
      cubmethod::runtime_context *get_runtime_context ();
      connection_pool &get_connection_pool ();
      SESSION_ID get_session_id () const
      {
	return m_sid;
      }

      bool is_running () const;
      bool is_for_scan () const;

      // cursor interface for method_invoke
      query_cursor *create_cursor (QUERY_ID query_id, bool oid_included);
      query_cursor *get_cursor (QUERY_ID query_id);
      void register_returning_cursor (QUERY_ID query_id);
      void deregister_returning_cursor (QUERY_ID query_id);

      // client handelr
      void register_client_handler (int handler_id);

      // error
      std::string get_error_msg ();
      void set_error_msg (const std::string &msg);
      db_parameter_info *get_db_parameter_info () const;

      void set_db_parameter_info (db_parameter_info *param_info);

      inline METHOD_REQ_ID get_and_increment_request_id ()
      {
	return m_rctx->get_and_increment_request_id();
      }

    private:
      void destory_all_cursors ();

      runtime_context *m_rctx;
      bool m_is_running;
      bool m_is_for_scan;

      connection *m_connection;
      std::queue<cubmem::block> m_data_queue;

      std::unordered_set <std::uint64_t> m_cursor_set;
      std::unordered_set <int> m_handler_set;

      std::string m_err_msg;

      SESSION_ID m_sid;
      METHOD_GROUP_ID m_id;

      db_parameter_info *m_parameter_info;
      cubthread::entry *m_thread_p;
      std::set <METHOD_TYPE> m_kind_type;
      std::vector <method_invoke *> m_method_vector;

      std::vector <DB_VALUE> m_result_vector;	/* placeholder for result value */
  };

} // namespace cubmethod

#endif				/* _METHOD_INVOKE_GROUP_HPP_ */
