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
// method_sql_log.hpp: log SQL (prepare/bind/execute) issued from stored procedures
//
// The formatting (including the recursion-depth dash prefix and password masking)
// lives here in the shared client library so that CAS and CSQL produce identical
// log bodies. The actual sink is provided by the host process through a registered
// writer: CAS writes into its *.sql.log (see src/broker), CSQL writes into csql.err
// (see src/executables). Hosts that do not register a writer get a no-op.
//

#ifndef _METHOD_SQL_LOG_HPP_
#define _METHOD_SQL_LOG_HPP_

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* SERVER_MODE */

#include <cstdio>
#include <vector>

#include "dbtype_def.h"
#include "hide_password.h"

namespace cubmethod
{
  /*
   * cubmethod::sql_log_writer
   *
   * Host-registered sink for SP-issued SQL logging.
   *   is_enabled : return true while logging is currently turned on (checked before
   *                any formatting is done, so a disabled host pays almost no cost).
   *   emit       : write one already-formatted log body line to the host's log file.
   */
  struct sql_log_writer
  {
    bool (*is_enabled) (void);
    void (*emit) (const char *body);
  };

  void set_sql_log_writer (const sql_log_writer *writer);

  /*
   * Injection points, called from callback_handler while handling SP-issued SQL.
   * Each is a no-op unless a writer is registered and currently enabled.
   * The recursion depth (tran_get_libcas_depth) is read internally and rendered as a
   * leading run of '-' so nested SP -> SQL -> SP chains are visible.
   */
  void sql_log_prepare (int handler_id, const char *sql, HIDE_PWD_INFO_PTR hide_pwd);
  void sql_log_bind (int handler_id, const std::vector<DB_VALUE> &params);
  void sql_log_execute (int handler_id, int tuple_count, int error);
}

#endif /* _METHOD_SQL_LOG_HPP_ */
