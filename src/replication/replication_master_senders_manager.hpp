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

#include "cubstream.hpp"
#include "thread_manager.hpp"
#include "thread_entry_task.hpp"
#include "stream_transfer_sender.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

#define SUPERVISOR_DAEMON_DELAY_MS 10
#define SUPERVISOR_DAEMON_CHECK_CONN_MS 5000

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
      static bool check_if_senders_reached_position (cubstream::stream_position desired_position);

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
		std::lock_guard<std::mutex> guard (master_senders_mutex);

		auto new_end = std::remove_if (master_server_stream_senders.begin (), master_server_stream_senders.end (),
					       [] (cubstream::transfer_sender *sender)
		{
		  return !sender->get_channel ().is_connection_alive ();
		});

		master_server_stream_senders.erase (new_end, master_server_stream_senders.end ());

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
      static std::mutex mutex_for_singleton, master_senders_mutex;
      static cubstream::stream *g_stream;
  };

} /* namespace cubreplication */

#endif /* _REPLICATION_MASTER_SENDERS_MANAGER_HPP_ */
