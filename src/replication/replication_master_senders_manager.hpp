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
 * replication_master_senders_manager.hpp - manages master replication channel entries; it is a singleton
 *                                        - it maintains the minimum successful sent position
 */

#ifndef _REPLICATION_MASTER_SENDERS_MANAGER_HPP_
#define _REPLICATION_MASTER_SENDERS_MANAGER_HPP_

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

  class master_senders_manager
  {
    public:
      master_senders_manager () = delete;
      ~master_senders_manager () = delete;

      static void init (cubstream::stream *stream);
      static void add_stream_sender (cubstream::transfer_sender *sender);
      static std::size_t get_number_of_stream_senders ();
      static void reset ();
      static void block_until_position_sent (cubstream::stream_position desired_position);

      static inline cubstream::stream &get_stream ()
      {
	assert (g_stream != NULL);
	return *g_stream;
      }

      static cubstream::stream_position g_minimum_successful_stream_position;

    private:

      class master_senders_supervisor_task : public cubthread::entry_task
      {
	public:
	  master_senders_supervisor_task ()
	  {
	  }

	  void execute (cubthread::entry &context)
	  {
	    static unsigned int check_conn_delay_counter = 0;

	    if (check_conn_delay_counter >
		SUPERVISOR_DAEMON_CHECK_CONN_MS / SUPERVISOR_DAEMON_DELAY_MS)
	      {
		std::vector <cubstream::transfer_sender *>::iterator it;

		rwlock_read_lock (&master_senders_lock);
		for (it = master_server_stream_senders.begin (); it != master_server_stream_senders.end ();)
		  {
		    if (! (*it)->get_channel ().is_connection_alive ())
		      {
			rwlock_read_unlock (&master_senders_lock);

			rwlock_write_lock (&master_senders_lock);
			it = master_server_stream_senders.erase (it);
			rwlock_write_unlock (&master_senders_lock);

			rwlock_read_lock (&master_senders_lock);
		      }
		    else
		      {
			++it;
		      }
		  }
		rwlock_read_unlock (&master_senders_lock);

		check_conn_delay_counter = 0;
	      }

#if 0
	    master_senders_manager::g_minimum_successful_stream_position =
	      std::numeric_limits <cubstream::stream_position>::max();

	    std::lock_guard<std::mutex> guard (master_senders_mutex);

	    for (cubstream::transfer_sender *sender : master_channels)
	      {
		if (master_senders_manager::g_minimum_successful_stream_position >
		    sender->get_last_sent_position ())
		  {
		    master_senders_manager::g_minimum_successful_stream_position =
		      sender->get_last_sent_position ();
		  }
	      }
#endif
	    check_conn_delay_counter++;
	  }
      };

      friend class master_senders_supervisor_task;

      static std::vector <cubstream::transfer_sender *> master_server_stream_senders;
      static cubthread::daemon *master_channels_supervisor_daemon;
      static bool is_initialized;
      static std::mutex mutex_for_singleton;
      static cubstream::stream *g_stream;

      static const unsigned int SUPERVISOR_DAEMON_DELAY_MS;
      static const unsigned int SUPERVISOR_DAEMON_CHECK_CONN_MS;
      static SYNC_RWLOCK master_senders_lock;
  };

} /* namespace cubreplication */

#endif /* _REPLICATION_MASTER_SENDERS_MANAGER_HPP_ */
