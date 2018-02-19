#include "master_replication_channel_mock.hpp"

#define SERVER_MODE
#include "master_replication_channel_manager.hpp"
#if defined (WINDOWS)
#include "tcp.h"
#else
#include "wintcp.h"
#endif
#include "error_code.h"
#include <sys/poll.h>
#include "thread_manager.hpp"
#include <assert.h>
#include "test_output.hpp"
#include "connection_cl.h"
#include "thread_looper.hpp"
#include <iostream>

typedef struct pollfd POLL_FD;

namespace master
{

	void init ()
	{
		master_replication_channel_manager::init();
	}

	void finish ()
	{
		master_replication_channel_manager::reset();
	}

	receive_from_slave_daemon_mock::receive_from_slave_daemon_mock (std::shared_ptr<master_replication_channel> ch) : channel (ch)
    {
			num_of_received_messages = 0;
    }

  void receive_from_slave_daemon_mock::execute (cubthread::entry &context)
  	{
      int rc;
      #define MAX_LENGTH 100
      char buffer [MAX_LENGTH];
      int recv_length = MAX_LENGTH;

      if (IS_INVALID_SOCKET (channel->get_slave_fd ().fd) || !channel->is_connected ())
        {
          /* don't go any further, wait for the manager supervisor to destroy it */
          return;
        }

      rc = poll (&channel->get_slave_fd (), 1, 100);
      if (rc < 0)
        {
          /* smth went wrong with the connection, destroy it */
          channel->set_is_connected (false);
          return;
        }

      if ((channel->get_slave_fd ().revents & POLLIN) != 0)
        {
          rc = channel->recv (channel->get_slave_fd ().fd, buffer, recv_length, communication_channel::get_max_timeout());
          if (rc == ERROR_WHEN_READING_SIZE || rc == ERROR_ON_READ)
            {
              channel->set_is_connected (false);
              return;
            }
          else if (rc != NO_ERRORS)
            {
              assert (false);
              return;
            }
          buffer[recv_length] = '\0';
					if (strcmp (buffer, "test") == 0)
						{
							num_of_received_messages++;
						}

					if (num_of_received_messages >= NUM_OF_MSG_SENT)
						{
							channel->set_is_connected (false);
						}
        }
      #undef MAX_LENGTH
    }

}
