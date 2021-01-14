/*
 * Copyright 2008 Search Solution Corporation
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
// Monitoring queries
//

#ifndef _QUERY_MONITORING_HPP_
#define _QUERY_MONITORING_HPP_

#include "storage_common.h"

typedef struct tran_query_exec_info TRAN_QUERY_EXEC_INFO;
struct tran_query_exec_info
{
  char *wait_for_tran_index_string;
  float query_time;
  float tran_time;
  char *query_stmt;
  char *sql_id;
  XASL_ID xasl_id;
};

#endif // !_QUERY_MONITORING_HPP_
