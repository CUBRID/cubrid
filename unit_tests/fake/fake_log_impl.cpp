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

#include "log_impl.h"
#include "log_prior_recv.hpp"

#include "fake_packable_object.hpp"

//log_prior_lsa_info g_log_prior_lsa_info;
log_global::log_global ()
  : m_prior_sender { nullptr }
  , m_prior_recver { nullptr }
{
};
log_global::~log_global () = default;
LOG_GLOBAL log_Gl;

log_append_info::log_append_info () = default;
log_prior_lsa_info::log_prior_lsa_info () = default;

mvcctable::mvcctable () = default;
mvcctable::~mvcctable () = default;

mvcc_trans_status::mvcc_trans_status () = default;
mvcc_trans_status::~mvcc_trans_status () = default;

mvcc_active_tran::mvcc_active_tran () = default;
mvcc_active_tran::~mvcc_active_tran () = default;

mvcc_snapshot::mvcc_snapshot () = default;

bool
log_does_allow_replication (void)
{
  return false;
}

namespace cublog
{
  EXPAND_PACKABLE_OBJECT_EMPTY_DEF (meta)
  EXPAND_PACKABLE_OBJECT_EMPTY_DEF (checkpoint_info)
}
