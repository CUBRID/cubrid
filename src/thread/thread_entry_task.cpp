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
#include "porting.h"
#if defined (SERVER_MODE)
#include "thread.h"
#endif // SERVER_MODE
#include "thread_entry.hpp"
#include "thread_manager.hpp"

namespace cubthread
{

  entry &
  entry_manager::create_context (void)
  {
    entry &context = *get_manager ()->claim_entry ();

    // for backward compatibility
    context.tid = pthread_self ();
    context.type = TT_WORKER;

    context.get_error_context ().register_thread_local ();

    // TODO: daemon type

    on_create (context);
    return context;
  }

  void
  entry_manager::retire_context (entry &context)
  {
    on_retire (context);

    // clear error messages
    context.get_error_context ().deregister_thread_local ();

    // for backward compatibility
    context.tid = (pthread_t) 0;

    // todo: here we should do more operations to clear thread entry before being reused
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
    er_clear ();

    on_recycle (context);
  }


} // namespace cubthread
