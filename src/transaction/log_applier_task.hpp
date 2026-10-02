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
 */

#ifndef _LOG_APPLIER_TASK_HPP_
#define _LOG_APPLIER_TASK_HPP_

#include "log_lsa.hpp"
#include "storage_common.h"

#include <cstddef>
#include <cstdint>
#include <ctime>

struct la_apply;

/* A transaction task assembled by the coordinator and submitted to the shared worker queue. */
struct la_apply_task
{
  /* Slave-local order used to match worker results and retire dispatched tasks in order. */
  std::uint64_t dispatch_sequence;
  int tranid;
  int record_type;
  LOG_LSA commit_lsa;
  LOG_LSA dependency_seq;
  bool dependency_is_ref;
  LOG_PAGEID final_pageid;
  std::time_t log_record_time;
  /* The coordinator retains the LA_APPLY slot until the matching result is retired. */
  la_apply *apply;
};

/* The result of applying a transaction returned through the shared result queue. */
struct la_apply_result
{
  /* Copied from the task so the coordinator can find its Dispatch Order entry. */
  std::uint64_t dispatch_sequence;
  std::size_t worker_index;
  int tranid;
  int record_type;
  int error;
  LOG_LSA commit_lsa;
  LOG_LSA committed_rep_lsa;
  std::time_t log_record_time;
};

#endif /* _LOG_APPLIER_TASK_HPP_ */
