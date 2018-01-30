#ifndef _MASTER_REPLICATION_CHANNEL_HPP
#define _MASTER_REPLICATION_CHANNEL_HPP

#include "communication_channel.hpp"
#include <sys/poll.h>
#include "thread_entry_task.hpp"
#include "connection_defs.h"

/* TODO check if this is ok */
#define MAX_SLAVE_CONNECTIONS 32

typedef struct pollfd POLL_FD;
namespace cubthread
{
  class daemon;
  class looper;
};

enum MASTER_DAEMON_THREADS
{
  RECEIVE_FROM_SLAVE = 0,

  NUM_OF_MASTER_DAEMON_THREADS
};

class master_replication_channel : public communication_channel
{
  public:
    friend class receive_from_slave_daemon;
    master_replication_channel (int slave_fd = -1);
    ~master_replication_channel ();
    master_replication_channel (master_replication_channel &&channel);
    master_replication_channel (const master_replication_channel &channel);
    master_replication_channel &operator= (const master_replication_channel &channel);
    master_replication_channel &operator= (master_replication_channel &&channel);

    master_replication_channel &add_daemon_thread (MASTER_DAEMON_THREADS daemon_index, const cubthread::looper &loop_rule, cubthread::entry_task *task);

#if 0
    int add_slave_connection (int sock_fd);
    int poll_for_requests ();
    int get_number_of_slaves ();
    short test_for_events (int slave_index, short flag);
    POLL_FD &get_poll_fd_of_slave (int slave_index);
    void remove_slave_by_index (int slave_index);
#endif
  private:
    cubthread::daemon *m_master_daemon_threads[NUM_OF_MASTER_DAEMON_THREADS];
    int m_slave_fd;

#if 0
    POLL_FD slave_fds [MAX_SLAVE_CONNECTIONS];
    int m_current_number_of_connected_slaves;

    static master_replication_channel *singleton;
    
    master_replication_channel ();
    ~master_replication_channel ();
#endif
};

class receive_from_slave_daemon : public cubthread::entry_task
{
  public:
    receive_from_slave_daemon (const master_replication_channel &ch) : channel (ch)
    {
    }

    void execute (cubthread::entry &context)
    {
      int rc;
      #define MAX_LENGTH 100
      char buffer [MAX_LENGTH];
      int recv_length = MAX_LENGTH;
      rc = channel.recv (channel.m_slave_fd, buffer, recv_length, communication_channel::get_max_timeout());
      if (rc == ERROR_WHEN_READING_SIZE || rc == ERROR_ON_READ)
        {
          /* this usually means that the connection is closed 
            TODO maybe add this case to recv to know for sure ?
          */
          /* TODO add logic for slave disconnect */
          close (channel.m_slave_fd);
        }
      else if (rc != NO_ERRORS)
        {
          assert (false);
        }
      buffer[recv_length] = '\0';
      _er_log_debug (ARG_FILE_LINE, "master::execute:" "received=%s\n", buffer);
      #undef MAX_LENGTH
    }

    void retire ()
    {

    }
  private:
    master_replication_channel channel;
};

#endif /* _MASTER_REPLICATION_CHANNEL_HPP */
