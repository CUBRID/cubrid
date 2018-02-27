/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * thread_entry_task.cpp - implementation of entry_task and entry_manager
 */

#include "thread_entry_task.hpp"

#include "error_manager.h"
#include "log_impl.h"
#include "porting.h"
#if defined (SERVER_MODE)
#include "thread.h"
#endif // SERVER_MODE
#include "thread_entry.hpp"
#include "thread_manager.hpp"

#include <cstring>

namespace cubthread
{

  entry &
  entry_manager::create_context (void)
  {
    entry &context = *get_manager ()->claim_entry ();

    // for backward compatibility
    context.register_id ();
    context.type = TT_WORKER;

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

    // todo: here we should do more operations to clear thread entry before being reused
    context.unregister_id ();
    context.tran_index = -1;
    context.check_interrupt = true;
#if defined (SERVER_MODE)
    context.status = TS_FREE;
#endif // SERVER_MODE

    get_manager ()->retire_entry (context);
  }

  void
  entry_manager::recycle_context (entry &context)
  {
    er_clear ();    // clear errors
    std::memset (&context.event_stats, 0, sizeof (context.event_stats));  // clear even stats
    context.tran_index = -1;    // clear transaction ID

    context.unregister_id ();  // unregister thread ID

    on_recycle (context);
  }

  void
  daemon_entry_manager::on_create (entry &context)
  {
#if defined (SERVER_MODE)
    context.status = TS_RUN;
#endif // SERVER_MODE
    context.type = TT_DAEMON;
    context.tran_index = LOG_SYSTEM_TRAN_INDEX;

    on_daemon_create (context);
  }

  void
  daemon_entry_manager::on_retire (entry &context)
  {
#if defined (SERVER_MODE)
    context.status = TS_DEAD;
#endif // SERVER_MODE

    on_daemon_retire (context);
  }

} // namespace cubthread
