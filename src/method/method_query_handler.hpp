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
#include <map>

#include "dbtype.h"
#include "mem_block.hpp"
#include "method_error.hpp"
#include "method_query_result.hpp"
#include "method_struct_query.hpp"

namespace cubmethod
{
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
      query_handler (error_context &ctx, int id);
      ~query_handler ();

      /* request */
      prepare_info prepare (std::string sql, int flag);
      execute_info execute (const execute_request &request);
      next_result_info next_result (int flag);
      get_generated_keys_info generated_keys ();

      /* TODO: execute_batch, execute_array */
      // TODO: int get_system_parameter ();

      void end_qresult ();

      void reset (); /* called after 1 iteration on method scan */

      /* getters */
      bool is_prepared ();
      bool get_query_count ();
      bool has_result_set ();

      int get_id ();
      std::string get_sql_stmt ();
      bool get_is_occupied ();
      void set_is_occupied (bool flag);
      bool get_prepare_info (prepare_info &info);
      DB_SESSION *get_db_session ();
      DB_QUERY_TYPE *get_column_info ();

      const query_result &get_result ();

      /* set result info */
      void set_prepare_column_list_info (std::vector<column_info> &infos);
      int set_qresult_info (query_result_info &qinfo);

      /* set result info */
      void set_prepare_column_list_info (std::vector<column_info> &infos, query_result &result);
      int set_qresult_info (std::vector<query_result_info> &qinfo);

    protected:
      /* prepare */
      int prepare_query (prepare_info &info, int &flag);
      int prepare_call (prepare_info &info, int &flag);

      /* execute */
      int execute_internal (execute_info &info, int flag, int max_col_size, int max_row,
			    const std::vector<DB_VALUE> &bind_values);
      int execute_internal_call (execute_info &info, int flag, int max_col_size, int max_row,
				 const std::vector<DB_VALUE> &bind_values);

      int set_host_variables (int num_bind, DB_VALUE *value_list);
      bool has_stmt_result_set (char stmt_type);

      /* generated keys */
      int get_generated_keys_client_insert (get_generated_keys_info &info, DB_QUERY_RESULT &qres);
      int get_generated_keys_server_insert (get_generated_keys_info &info, DB_QUERY_RESULT &qres);
      int make_attributes_by_oid_value (get_generated_keys_info &info, const DB_VALUE &oid_val, int tuple_offset);

      /* column info */
      column_info set_column_info (int dbType, int setType, short scale, int prec, char charset, const char *col_name,
				   const char *attr_name,
				   const char *class_name, char is_non_null);

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
      query_result m_query_result;

      /* statement handler cache */
      bool m_is_occupied; // Is occupied by CUBRIDServerSideStatement

      bool m_is_updatable; // TODO: not implemented yet
      bool m_query_info_flag; // TODO: not implemented yet
  };
}		// namespace cubmethod
#endif				/* _METHOD_QUERY_HPP_ */
