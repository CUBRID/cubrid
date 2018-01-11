#ifndef _MASTER_REPLICATION_CHANNEL_HPP
#define _MASTER_REPLICATION_CHANNEL_HPP

#include "replication_channel.hpp"
#include <sys/poll.h>

/* TODO check if this is ok */
#define MAX_SLAVE_CONNECTIONS 32

typedef struct pollfd POLL_FD;
namespace cubthread
{
  class daemon;
};

class master_replication_channel : public replication_channel
{
  public:
    master_replication_channel ();
    ~master_replication_channel ();
    int add_slave_connection (int sock_fd);
    int poll_for_requests ();
    bool get_is_loop_running ();
    void stop_loop ();
    int get_number_of_slaves ();
    short test_for_events (int slave_index, short flag);
  private:
    POLL_FD slave_fds [MAX_SLAVE_CONNECTIONS];
    int m_current_number_of_connected_slaves;
    bool is_loop_running;
    cubthread::daemon *master_loop_daemon;
};

#endif /* _MASTER_REPLICATION_CHANNEL_HPP */
