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
 * csql_sql_log.c - CSQL sink for SP-issued SQL logging
 *
 * The shared method library formats each log body; this CSQL-only writer routes
 * it into csql.err (the error manager log opened by CSQL). _er_log_debug is used
 * directly - not the er_log_debug macro - so logging does not depend on the
 * er_log_debug parameter or on a debug build; gating is done by the
 * csql_log_sql_from_sp system parameter instead.
 */

#include "error_manager.h"
#include "system_parameter.h"

#include "csql_sql_log.h"
#include "method_sql_log.hpp"

static bool
csql_sql_log_sp_is_enabled (void)
{
  return prm_get_bool_value (PRM_ID_CSQL_LOG_SQL_FROM_SP);
}

static void
csql_sql_log_sp_emit (const char *body)
{
  _er_log_debug (ARG_FILE_LINE, "%s\n", body);
}

static const cubmethod::sql_log_writer csql_sql_log_sp_writer = {
  csql_sql_log_sp_is_enabled,
  csql_sql_log_sp_emit
};

void
csql_sql_log_sp_init (void)
{
  cubmethod::set_sql_log_writer (&csql_sql_log_sp_writer);
}
