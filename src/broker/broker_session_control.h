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

struct broker_session_config
{
  uint32_t mask;
  int32_t sql_log;
  int32_t slow_log;
};

struct broker_session_change
{
  uint32_t session_id;		/* zero selects all sessions of the broker */
  struct broker_session_config config;
  char database[33];		/* required with session_id, avoids cross-DB ambiguity */
};

struct broker_session_change_reply
{
  int32_t result;		/* 0 success, -1 invalid/unknown, -2 incomplete delivery */
  int32_t affected;
};

#endif /* _BROKER_SESSION_CONTROL_H_ */
