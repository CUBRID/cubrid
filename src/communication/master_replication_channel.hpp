#ifndef _MASTER_REPLICATION_CHANNEL_HPP
#define _MASTER_REPLICATION_CHANNEL_HPP

#include "communication_channel.hpp"
#include <sys/poll.h>
#include "thread_entry_task.hpp"
#include "connection_defs.h"
#include <atomic>

typedef struct pollfd POLL_FD;

class master_replication_channel : public communication_channel
{
  public:
    master_replication_channel (int slave_fd = -1);
    ~master_replication_channel ();

    POLL_FD &get_slave_fd ();

    std::atomic_bool &is_connected();
    void set_is_connected (bool flag);
  private:
    POLL_FD m_slave_fd;
    std::atomic_bool m_is_connection_alive;
};

class receive_from_slave_daemon : public cubthread::entry_task
{
  public:
    receive_from_slave_daemon (master_replication_channel *ch) : channel (ch)
    {
    }

    void execute (cubthread::entry &context)
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

      rc = poll (&channel->get_slave_fd (), 1, -1);
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
              /* this usually means that the connection is closed 
                TODO[arnia] maybe add this case to recv to know for sure ?
              */
              channel->set_is_connected (false);
              return;
            }
          else if (rc != NO_ERRORS)
            {
              assert (false);
              return;
            }
          buffer[recv_length] = '\0';
          _er_log_debug (ARG_FILE_LINE, "master::execute:" "received=%s\n", buffer);
        }
      #undef MAX_LENGTH
    }

    void retire ()
    {

    }
  private:
    master_replication_channel *channel;
};

#endif /* _MASTER_REPLICATION_CHANNEL_HPP */
