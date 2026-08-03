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
// method_sql_log.cpp: log SQL (prepare/bind/execute) issued from stored procedures
//

#include "method_sql_log.hpp"

#include <string>

#include "transaction_cl.h"
#include "string_buffer.hpp"
#include "db_value_printer.hpp"

#include "memory_wrapper.hpp" // XXX: SHOULD BE THE LAST INCLUDE HEADER

namespace cubmethod
{
  static const sql_log_writer *g_sql_log_writer = NULL;

  void
  set_sql_log_writer (const sql_log_writer *writer)
  {
    g_sql_log_writer = writer;
  }

  static bool
  sql_log_active (void)
  {
    return g_sql_log_writer != NULL && g_sql_log_writer->is_enabled != NULL && g_sql_log_writer->is_enabled ();
  }

  /*
   * Render the recursion-depth prefix. The depth is the nesting level of method
   * callbacks (SP -> SQL -> SP ...) maintained by tran_begin/end_libcas_function,
   * so identical nesting produces an identical dash count regardless of whether the
   * host is CAS or CSQL.
   */
  static void
  append_depth_prefix (std::string &line)
  {
    int depth = tran_get_libcas_depth ();
    for (int i = 0; i < depth; i++)
      {
	line += '-';
      }
  }

  void
  sql_log_prepare (int handler_id, const char *sql, HIDE_PWD_INFO_PTR hide_pwd)
  {
    if (!sql_log_active ())
      {
	return;
      }

    /* mask passwords in the SQL text (password_snprint auto-detects when hide_pwd is NULL) */
    int sql_len = (int) strlen (sql);
    std::string masked (sql_len + 1, '\0');
    int n = password_snprint (&masked[0], sql_len + 1, (char *) sql, hide_pwd);
    masked.resize ((n > 0 && n <= sql_len) ? n : sql_len);

    std::string line;
    append_depth_prefix (line);
    line += "prepare srv_h_id ";
    line += std::to_string (handler_id);
    line += ' ';
    line += masked;

    g_sql_log_writer->emit (line.c_str ());
  }

  void
  sql_log_bind (int handler_id, const std::vector<DB_VALUE> &params)
  {
    if (!sql_log_active () || params.empty ())
      {
	return;
      }

    for (size_t i = 0; i < params.size (); i++)
      {
	string_buffer sb;
	db_sprint_value (&params[i], sb);

	std::string line;
	append_depth_prefix (line);
	line += "bind ";
	line += std::to_string (i + 1);
	line += " : ";
	line += sb.get_buffer ();

	g_sql_log_writer->emit (line.c_str ());
      }
  }

  void
  sql_log_execute (int handler_id, int tuple_count, int error)
  {
    if (!sql_log_active ())
      {
	return;
      }

    std::string line;
    append_depth_prefix (line);
    line += "execute srv_h_id ";
    line += std::to_string (handler_id);
    if (error < 0)
      {
	line += " error:";
	line += std::to_string (error);
      }
    else
      {
	line += " tuple ";
	line += std::to_string (tuple_count);
      }

    g_sql_log_writer->emit (line.c_str ());
  }
}
