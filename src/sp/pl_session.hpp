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
#include <map>

#include "system_parameter.h"
#include "packable_object.hpp"

#include "pl_connection.hpp"
#include "pl_execution_stack_context.hpp"
#include "pl_signature.hpp"

// thread_entry.hpp
namespace cubthread
{
  class entry;
}

namespace cubmethod
{
  struct db_parameter_info;
}

namespace cubpl
{
  // forward declarations
  class query_cursor;
  class execution_stack;

  using THREAD_ENTRY_IDX = int;
  using QUERY_ID = std::uint64_t;

  enum class sys_param_id : int
  {
    PRM_ID_BEGIN = 100000,
    PRM_ID_DBMS_OUTPUT = PRM_ID_BEGIN,
    PRM_ID_END
  };

  struct EXPORT_IMPORT sys_param : public cubpacking::packable_object
  {
    int prm_id; // if value >= PRM_ID_BEGIN: PL's parameter, otherwise: DBMS's parameter
    int prm_type;
    std::string prm_value;

    sys_param () = default;
    explicit sys_param (const SYSPRM_ASSIGN_VALUE *db_param);
    explicit sys_param (const SYSPRM_PARAM *db_param);
    sys_param (int prm_id, int prm_type, std::string prm_value);

    void set_prm_value (const SYSPRM_PARAM *prm);

    bool get_prm_value_bool ()
    {
      return (prm_value.size () == 1 && (prm_value[0] == '1' || prm_value[0] == 't'));
    }
    int get_prm_value_int ()
    {
      return std::stoi (prm_value);
    }
    float get_prm_value_float ()
    {
      return std::stof (prm_value);
    }
    std::string get_prm_value_string ()
    {
      return prm_value;
    }

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;
  };
  class session
  {
    public:
      session (SESSION_ID id);
      ~session ();

      using exec_stack_map_type = std::unordered_map <PL_STACK_ID, execution_stack *>;
      using exec_stack_id_type = std::vector <PL_STACK_ID>;
      using exec_stack_iter = std::unordered_map <PL_STACK_ID, execution_stack *>::iterator;
      using cursor_map_type = std::unordered_map <QUERY_ID, query_cursor *>;
      using cursor_iter = std::unordered_map <QUERY_ID, query_cursor *>::iterator;

      /* cursor management */
      query_cursor *create_cursor (cubthread::entry *thread_p, QUERY_ID query_id, bool oid_included = false);
      query_cursor *get_cursor (cubthread::entry *thread_p, QUERY_ID query_id);
      void destroy_cursor (cubthread::entry *thread_p, QUERY_ID query_id);

      void add_session_cursor (cubthread::entry *thread_p, QUERY_ID query_id);
      void remove_session_cursor (cubthread::entry *thread_p, QUERY_ID query_id);
      bool is_session_cursor (QUERY_ID query_id);

      /* stack management */
      // Currently these functions are used for debugging purpose.
      // In the recursive call situation, each time the function is called, a new worker from the thread pool is assigned. With this code, you can easily know the current state.
      // In the future, these functions will resolve some cases when it is necessary to set an error for all threads participating in a recursive call e.g. interrupt
      execution_stack *create_and_push_stack (cubthread::entry *thread_p);
      void pop_and_destroy_stack (const PL_STACK_ID sid);
      execution_stack *top_stack ();

      /* connection management */
      connection_view claim_connection ();
      void release_connection (connection_view &conn);

      /* thread */
      bool is_thread_involved (thread_id_t id);

      /* getter */
      SESSION_ID get_id ();

      void set_interrupt (int reason, std::string msg = "");
      bool is_interrupted ();
      int get_interrupt_id ();
      std::string get_interrupt_msg ();
      void clear_interrupt ();

      void wait_until_pl_session_done ();
      void set_local_error_for_interrupt (); // set interrupt on thread local error manager

      bool is_sp_running ();

      inline METHOD_REQ_ID get_and_increment_request_id ()
      {
	return m_req_id++;
      }

      cubmethod::db_parameter_info *get_db_parameter_info () const;
      void set_db_parameter_info (cubmethod::db_parameter_info *param_info);

      // handling DB and PL session parameters
      bool check_reloading_pl_context_required (const connection_view &cv);
      const std::vector <sys_param> obtain_session_parameters (const connection_view &conn);
      void mark_session_param_changed (int prm_id);
      void set_session_params_all_required (bool is_required);
      void set_session_param (const sys_param &param);

    private:
      void destroy_all_cursors ();
      void destroy_pl_context_jvm ();

      std::mutex m_mutex_stack;
      std::mutex m_mutex_connection;
      std::mutex m_mutex_cursor;
      std::condition_variable m_cond_target_stack_at_top;
      std::condition_variable m_cond_pl_session_done;

      std::unordered_set <QUERY_ID> m_session_cursors;
      std::map <QUERY_ID, int> m_session_handler_map;

      exec_stack_map_type m_stack_map; // method executor storage
      exec_stack_id_type m_exec_stack; // runtime stack (implemented using vector)

      cursor_map_type m_cursor_map; // server-side cursor storage

      std::deque <connection_view> m_session_connections;

      std::atomic <METHOD_REQ_ID> m_req_id;

      cubmethod::db_parameter_info *m_param_info;

      /* session parameters */
      std::unordered_map<int, sys_param> m_session_params;

      // session parameters: The following variables are used to check if the session parameters have changed and updateing to the PL server is required
      std::unordered_set<int> m_session_param_changed_ids;
      bool m_all_session_params_required;
      int m_last_conn_epoch;
      int m_stack_idx;

      // interrupt
      int m_interrupt_id;
      std::string m_interrupt_msg;

      SESSION_ID m_id;
  };

  /* global interface */
  session *get_session ();
} // cubmethod

// alias declaration for legacy C files
using PL_SESSION = cubpl::session;

#endif // _PL_SESSION_HPP_
