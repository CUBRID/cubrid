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
/* Broker-local session controls; never sent on the driver wire protocol. */
#ifndef _BROKER_SESSION_CONTROL_H_
#define _BROKER_SESSION_CONTROL_H_

#include <stdint.h>

#define BROKER_SESSION_SQL_LOG 1u
#define BROKER_SESSION_SLOW_LOG 2u
#define BROKER_SESSION_LOG_MASK (BROKER_SESSION_SQL_LOG | BROKER_SESSION_SLOW_LOG)
#define BROKER_SESSION_RUNTIME 4u
#define BROKER_SESSION_CONFIG_MASK (BROKER_SESSION_LOG_MASK | BROKER_SESSION_RUNTIME)
#define BROKER_SESSION_PATH_MAX 1024

/* These settings were broker-wide, unlike SQL_LOG/SLOW_LOG's per-CAS
 * overrides. A runtime update carries a complete broker-owned snapshot;
 * it never overwrites a session's selected SQL_LOG/SLOW_LOG mode. */
struct broker_runtime_config
{
  int32_t sql_log_max_size;
  int32_t access_log;
  int32_t access_log_max_size;
  int32_t long_query_time;
  int32_t long_transaction_time;
  int32_t jdbc_cache;
  int32_t jdbc_cache_only_hint;
  int32_t jdbc_cache_life_time;
  int32_t statement_pooling;
  int32_t cci_default_autocommit;
  int32_t max_prepared_stmt_count;
  int32_t session_timeout;
  int32_t session_timeout_is_set;
  int32_t query_timeout;
  int32_t max_string_length;
  int32_t trigger_action_flag;
  char log_dir[BROKER_SESSION_PATH_MAX];
  char slow_log_dir[BROKER_SESSION_PATH_MAX];
  char error_log_dir[BROKER_SESSION_PATH_MAX];
};

enum broker_runtime_parameter
{
  BROKER_RUNTIME_NONE,
  BROKER_RUNTIME_SQL_LOG_MAX_SIZE,
  BROKER_RUNTIME_ACCESS_LOG,
  BROKER_RUNTIME_ACCESS_LOG_MAX_SIZE,
  BROKER_RUNTIME_LONG_QUERY_TIME,
  BROKER_RUNTIME_LONG_TRANSACTION_TIME,
  BROKER_RUNTIME_JDBC_CACHE,
  BROKER_RUNTIME_JDBC_CACHE_HINT_ONLY,
  BROKER_RUNTIME_JDBC_CACHE_LIFE_TIME,
  BROKER_RUNTIME_STATEMENT_POOLING,
  BROKER_RUNTIME_MAX_PREPARED_STMT_COUNT,
  BROKER_RUNTIME_SESSION_TIMEOUT,
  BROKER_RUNTIME_MAX_QUERY_TIMEOUT,
  BROKER_RUNTIME_TRIGGER_ACTION,
  BROKER_RUNTIME_LOG_DIR,
  BROKER_RUNTIME_SLOW_LOG_DIR,
  BROKER_RUNTIME_ERROR_LOG_DIR
};

struct broker_session_config
{
  uint32_t mask;
  int32_t sql_log;
  int32_t slow_log;
  struct broker_runtime_config runtime;
};

struct broker_session_change
{
  uint32_t session_id;		/* zero selects all sessions of the broker */
  struct broker_session_config config;
  char database[33];		/* required with session_id, avoids cross-DB ambiguity */
  uint32_t parameter;		/* runtime field changed by the admin client */
};

struct broker_session_change_reply
{
  int32_t result;		/* 0 success, -1 invalid/unknown, -2 incomplete delivery */
  int32_t affected;
};

#endif /* _BROKER_SESSION_CONTROL_H_ */
