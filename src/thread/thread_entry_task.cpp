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
 * thread_entry_task.cpp - implementation of entry_task and entry_manager
 */

#include "thread_entry_task.hpp"

#include "error_manager.h"
#include "log_impl.h"
#include "porting.h"
#include "thread_entry.hpp"
#include "thread_manager.hpp"

#include <cstring>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubthread
{

  entry &
  entry_manager::create_context (void)
  {
    entry &context = *get_manager ()->claim_entry ();

    // for backward compatibility
    context.register_id ();
    context.type = TT_WORKER;
#if defined (SERVER_MODE)
    context.m_status = entry::status::TS_RUN;
    context.shutdown = false;
#endif // SERVER_MODE

    context.get_error_context ().register_thread_local ();

    on_create (context);
    return context;
  }

  void
  entry_manager::retire_context (entry &context)
  {
    on_retire (context);

    // clear error messages
    context.get_error_context ().deregister_thread_local ();

    context.end_resource_tracks ();

    // todo: here we should do more operations to clear thread entry before being reused
    context.unregister_id ();
    context.tran_index = NULL_TRAN_INDEX;
    context.check_interrupt = true;
    context.private_lru_index = -1;
#if defined (SERVER_MODE)
    context.m_status = entry::status::TS_FREE;
    context.resume_status = THREAD_RESUME_NONE;
#endif // SERVER_MODE

    get_manager ()->retire_entry (context);
  }

  void
  entry_manager::recycle_context (entry &context)
  {
    er_clear ();    // clear errors
    context.end_resource_tracks ();
    std::memset (&context.event_stats, 0, sizeof (context.event_stats));  // clear even stats
    context.tran_index = NULL_TRAN_INDEX;    // clear transaction ID
    context.private_lru_index = -1;
#if defined (SERVER_MODE)
    context.resume_status = THREAD_RESUME_NONE;
    context.shutdown = false;
#endif // SERVER_MODE

    on_recycle (context);
  }

  void
  entry_manager::stop_execution (entry &context)
  {
    context.shutdown = true;
  }

  void
  daemon_entry_manager::on_create (entry &context)
  {
    context.type = TT_DAEMON;
    context.tran_index = LOG_SYSTEM_TRAN_INDEX;

    on_daemon_create (context);
  }

  void
  daemon_entry_manager::on_retire (entry &context)
  {
#if defined (SERVER_MODE)
    context.m_status = entry::status::TS_DEAD;
#endif // SERVER_MODE

    context.unregister_id ();  // unregister thread ID

    on_daemon_retire (context);
  }

} // namespace cubthread
