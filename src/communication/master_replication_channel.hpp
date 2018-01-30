#ifndef _MASTER_REPLICATION_CHANNEL_HPP
#define _MASTER_REPLICATION_CHANNEL_HPP

#include "communication_channel.hpp"
#include <sys/poll.h>
#include "thread_entry_task.hpp"
#include "connection_defs.h"

/* TODO check if this is ok */
#define MAX_SLAVE_CONNECTIONS 32

typedef struct pollfd POLL_FD;


class master_replication_channel : public communication_channel
{
  public:
    friend class receive_from_slave_daemon;
    master_replication_channel (int slave_fd = -1);
    ~master_replication_channel ();
  private:
    POLL_FD m_slave_fd;
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

      rc = poll (&channel->m_slave_fd, 1, -1);
      if (rc < 0)
        {
          /*TODO[arnia] add logic to close this daemon */
          assert (false);
        }

      if ((channel->m_slave_fd.revents & POLLIN) != 0)
        {
          rc = channel->recv (channel->m_slave_fd.fd, buffer, recv_length, communication_channel::get_max_timeout());
          if (rc == ERROR_WHEN_READING_SIZE || rc == ERROR_ON_READ)
            {
              /* this usually means that the connection is closed 
                TODO[arnia] maybe add this case to recv to know for sure ?
              */
              /* TODO[arnia] add logic for slave disconnect */
              assert (false);
            }
          else if (rc != NO_ERRORS)
            {
              assert (false);
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
