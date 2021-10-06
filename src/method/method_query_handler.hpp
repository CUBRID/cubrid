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

#ifndef _METHOD_QUERY_HPP_
#define _METHOD_QUERY_HPP_

#ident "$Id$"

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* SERVER_MODE */

#include <string>
#include <vector>

#include "dbtype.h"
#include "mem_block.hpp"
#include "method_query_result.hpp"
#include "method_struct_query.hpp"

namespace cubmethod
{
#define CUBRID_STMT_CALL_SP	0x7e

  /* PREPARE_FLAG, EXEC_FLAG are ported from cas_cci.h */
  enum PREPARE_FLAG
  {
    PREPARE_INCLUDE_OID = 0x01,
    PREPARE_UPDATABLE = 0x02,
    PREPARE_QUERY_INFO = 0x04,
    PREPARE_HOLDABLE = 0x08,
    PREPARE_XASL_CACHE_PINNED = 0x10,
    PREPARE_CALL = 0x40
  };

  enum EXEC_FLAG
  {
    EXEC_QUERY_ALL = 0x02,
    EXEC_QUERY_INFO = 0x04,
    EXEC_ONLY_QUERY_PLAN = 0x08,
    EXEC_THREAD = 0x10,
    EXEC_RETURN_GENERATED_KEYS = 0x40
  };

#define CLASS_NAME_PATTERN_MATCH 1
#define ATTR_NAME_PATTERN_MATCH	2

  struct prepare_call_info
  {
    prepare_call_info () = default;

    DB_VALUE dbval_ret;
    int num_args;
    std::vector<DB_VALUE> dbval_args; /* # of num_args + 1 */
    std::vector<char> param_mode; /* # of num_args */
    bool is_first_out;

    int set_is_first_out (std::string &sql_stmt);
    int set_prepare_call_info (int num_args);
  };

  class error_context;

  /*
   * cubmethod::query_handler
   *
   * description
   *
   *
   * how to use
   *
   */
  class query_handler
  {
    public:
      query_handler (error_context &ctx, int id)
	: m_id (id), m_error_ctx (ctx)
      {
	m_is_prepared = false;
	m_use_plan_cache = false;
      }
      ~query_handler ();

      /* request */
      prepare_info prepare (std::string sql, int flag);
      execute_info execute (std::vector<DB_VALUE> &bind_values, int flag, int max_col_size, int max_row);
      next_result_info next_result (int flag);

      int get_generated_keys ();
      /* TODO: execute_batch, execute_array */
      // TODO: int get_system_parameter ();

      void end_qresult (bool is_self_free);

      /* getter : TODO for statement handler cache */
      bool is_prepared ();
      bool get_query_count ();
      bool has_result_set ();

      std::string get_sql_stmt ();

    protected:
      /* prepare */
      int prepare_query (prepare_info &info, int &flag);
      int prepare_call (prepare_info &info, int &flag);

      /* execute */
      int execute_internal (execute_info &info, int flag, int max_col_size, int max_row, std::vector<DB_VALUE> &bind_values);
      int execute_internal_call (execute_info &info, int flag, int max_col_size, int max_row,
				 std::vector<DB_VALUE> &bind_values);
      int execute_internal_all (execute_info &info, int flag, int max_col_size, int max_row,
				std::vector<DB_VALUE> &bind_values);

      int set_host_variables (int num_bind, DB_VALUE *value_list);
      bool has_stmt_result_set (char stmt_type);

      /* column info */
      int set_prepare_column_list_info (std::vector<column_info> &infos, query_result &result);
      column_info set_column_info (int dbType, int setType, short scale, int prec, char charset, const char *col_name,
				   const char *attr_name,
				   const char *class_name, char is_non_null);

      /* qresult */
      int set_qresult_info (std::vector<query_result_info> &qinfo);

      /* session */
      void close_and_free_session ();

      /* etc */
      void set_dbobj_to_oid (DB_OBJECT *obj, OID *oid);

    private:
      int m_id;

      /* stats error, log */
      int m_num_errors;
      error_context &m_error_ctx;

      /* prepare info */
      std::string m_sql_stmt; /* sql statement literal */
      int m_prepare_flag;
      CUBRID_STMT_TYPE m_stmt_type; /* statement type */
      DB_SESSION *m_session;
      bool m_is_prepared;
      bool m_use_plan_cache;

      /* call */
      prepare_call_info m_prepare_call_info;

      /* execute info */
      int m_num_markers; /* # of input markers */
      int m_max_col_size;
      int m_max_row;

      bool m_has_result_set;
      std::vector<query_result> m_q_result;
      query_result *m_current_result;
      int m_current_result_index; // It has a value of -1 when no queries have been executed

      bool m_is_updatable; // TODO: not implemented yet
      bool m_query_info_flag; // TODO: not implemented yet
  };
}		// namespace cubmethod
#endif				/* _METHOD_QUERY_HPP_ */
