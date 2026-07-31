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
 * cas_sql_log_sp.c - CAS sink for SP-issued SQL logging
 *
 * The shared method library formats each log body; this CAS-only writer routes
 * it into the per-CAS *.sql.log through cas_log_write, which itself no-ops when
 * the broker SQL_LOG parameter is OFF (SQL_LOG_MODE_NONE).
 */

#include "cas_common.h"
#include "cas_log.h"
#include "broker_config.h"
#include "cas_common_vars.h"

#include "cas_sql_log_sp.h"
#include "method_sql_log.hpp"

static bool
cas_sql_log_sp_is_enabled (void)
{
  return as_info != NULL && as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE;
}

static void
cas_sql_log_sp_emit (const char *body)
{
  cas_log_write (0, false, "%s", body);
}

static const
  cubmethod::sql_log_writer
  cas_sql_log_sp_writer = {
  cas_sql_log_sp_is_enabled,
  cas_sql_log_sp_emit
};

void
cas_sql_log_sp_init (void)
{
  cubmethod::set_sql_log_writer (&cas_sql_log_sp_writer);
}
