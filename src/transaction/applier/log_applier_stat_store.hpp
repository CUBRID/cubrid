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

/*
 * log_applier_stat_store.hpp - interface for persisting parallel apply statistics
 */

#ifndef _LOG_APPLIER_STAT_STORE_HPP_
#define _LOG_APPLIER_STAT_STORE_HPP_

#include "dbtype_def.h"

#include <time.h>

typedef int (*LA_STAT_EXECUTE_FUNC) (const char *sql, int arg_count, DB_VALUE *vals, bool au_disable);

typedef struct la_stat_applier LA_STAT_APPLIER;
struct la_stat_applier
{
  const char *db_name;
  const char *copied_log_path;
  time_t start_time;
  int worker_count;
  const char *apply_state;
  time_t done_hold_since;
};

extern int la_stat_init_rows (const LA_STAT_APPLIER *applier, LA_STAT_EXECUTE_FUNC execute);
extern int la_stat_store (const LA_STAT_APPLIER *applier, LA_STAT_EXECUTE_FUNC execute);

#endif /* _LOG_APPLIER_STAT_STORE_HPP_ */
