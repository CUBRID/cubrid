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
 * stream_senders_manager.hpp - manages stream sender entries 
 *                            - it maintains the minimum successful sent position
 */

#ifndef _STREAM_SENDERS_MANAGER_HPP_
#define _STREAM_SENDERS_MANAGER_HPP_

#include "critical_section.h"
#include "cubstream.hpp"
#include "thread_manager.hpp"
#include "thread_entry_task.hpp"
#include "stream_transfer_sender.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

namespace cubthread
{
  class daemon;
  class looper;
};

namespace cubreplication
{

  class stream_senders_manager
  {
    public:
      stream_senders_manager (cubstream::stream &supervised_stream);
      ~stream_senders_manager ();

      void add_stream_sender (cubstream::transfer_sender *sender);
      void stop_stream_sender (cubstream::transfer_sender *sender);
      bool find_stream_sender (const cubstream::transfer_sender *sender);
      std::size_t get_number_of_stream_senders ();
      void block_until_position_sent (cubstream::stream_position desired_position);

    private:
      void init ();
      void finalize ();

      void execute (cubthread::entry &context);

      friend class master_senders_supervisor_task;

      cubstream::stream &m_stream;
      std::vector<cubstream::transfer_sender *> stream_senders;
      cubthread::daemon *supervisor_daemon;

      static const unsigned int SUPERVISOR_DAEMON_DELAY_MS;
      static const unsigned int SUPERVISOR_DAEMON_CHECK_CONN_MS;
      SYNC_RWLOCK senders_lock;
  };

} /* namespace cubreplication */

#endif /* _STREAM_SENDERS_MANAGER_HPP_ */
