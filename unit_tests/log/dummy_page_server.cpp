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

#include "async_page_fetcher.hpp"
#include "log_replication.hpp"
#include "page_server.hpp"

PSTAT_GLOBAL pstat_Global;

cublog::replicator replicator_GL (NULL_LSA);
page_server ps_Gl;

namespace cublog
{
  class reusable_jobs_stack {};
  class redo_parallel {};

  replicator::replicator (const log_lsa &start_redo_lsa)
    : m_perfmon_redo_sync{ PSTAT_REDO_REPL_LOG_REDO_SYNC }
    , m_perf_stat_idle { cublog::perf_stats::do_not_record_t {} }
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

  css_error_code channel::accept (SOCKET socket)
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

page_server::connection_handler::~connection_handler ()
{
}

cublog::replicator &
page_server::get_replicator ()
{
  return replicator_GL;
}

log_rv_redo_context::log_rv_redo_context (const log_lsa &end_redo_lsa, log_reader::fetch_mode fetch_mode)
{
}

log_rv_redo_context::~log_rv_redo_context ()
{
}

namespace cubpacking
{
  size_t packer::get_packed_size_overloaded (const std::uint64_t &value, size_t curr_offset)
  {
    assert ("function is not used in the context of this unit test" == nullptr);
    return 0;
  }
  void packer::pack_overloaded (const std::uint64_t &value)
  {
    assert ("function is not used in the context of this unit test" == nullptr);
  }
  void unpacker::unpack_overloaded (std::uint64_t &value)
  {
    assert ("function is not used in the context of this unit test" == nullptr);
  }

  size_t packer::get_packed_size_overloaded (const std::string &value, size_t curr_offset)
  {
    assert ("function is not used in the context of this unit test" == nullptr);
    return 0;
  }
  void packer::pack_overloaded (const std::string &value)
  {
    assert ("function is not used in the context of this unit test" == nullptr);
  }
  void unpacker::unpack_overloaded (std::string &value)
  {
    assert ("function is not used in the context of this unit test" == nullptr);
  }
}