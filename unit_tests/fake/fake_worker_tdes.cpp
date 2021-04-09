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

// fake all objects that worker tdes depends on

#include "log_impl.h"

bool
log_tdes::is_active_worker_transaction () const
{
  return true;
}

bool
log_tdes::is_system_transaction () const
{
  return false;
}

bool
log_tdes::is_allowed_sysop () const
{
  return true;
}

void
log_tdes::lock_topop () {}

void
log_tdes::unlock_topop () {}

void
log_tdes::on_sysop_start () {}

void
log_tdes::on_sysop_end () {}

// fix dependencies locally:

mvcc_active_tran::~mvcc_active_tran () = default;

mvcc_info::mvcc_info () = default;
