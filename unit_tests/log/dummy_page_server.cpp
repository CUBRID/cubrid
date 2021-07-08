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

#include "log_replication.hpp"
#include "page_server.hpp"

cublog::replicator replicator_GL (NULL_LSA);
page_server ps_Gl;

namespace cublog
{
  class redo_parallel {};
  class minimum_log_lsa_monitor {};

  replicator::replicator (const log_lsa &start_redo_lsa)
    : m_perfmon_redo_sync{ PSTAT_REDO_REPL_LOG_REDO_SYNC }
  {
  }

  replicator::~replicator ()
  {
  }

  void
  replicator::wait_past_target_lsa (const log_lsa &lsa)
  {
  }
}

namespace cubcomm
{
  channel::~channel ()
  {
  }

  void channel::close_connection ()
  {
  }

  css_error_code channel::connect (const char *hostname, int port)
  {
    return NO_ERRORS;
  }
}

perfmon_counter_timer_tracker::perfmon_counter_timer_tracker (PERF_STAT_ID a_stat_id)
  : m_stat_id (a_stat_id)
{
}

page_server::~page_server ()
{
}

cublog::replicator &
page_server::get_replicator ()
{
  return replicator_GL;
}
