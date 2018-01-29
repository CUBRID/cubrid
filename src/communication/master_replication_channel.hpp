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
    int add_slave_connection (int sock_fd);
    int poll_for_requests ();
    int get_number_of_slaves ();
    short test_for_events (int slave_index, short flag);
    POLL_FD &get_poll_fd_of_slave (int slave_index);
    void remove_slave_by_index (int slave_index);

    static void init ();
    static void reset_singleton ();
    static master_replication_channel *get_channel ();
  private:
    POLL_FD slave_fds [MAX_SLAVE_CONNECTIONS];
    int m_current_number_of_connected_slaves;
    cubthread::daemon *master_loop_daemon;

    static master_replication_channel *singleton;
    
    master_replication_channel ();
    ~master_replication_channel ();
};

#endif /* _MASTER_REPLICATION_CHANNEL_HPP */
