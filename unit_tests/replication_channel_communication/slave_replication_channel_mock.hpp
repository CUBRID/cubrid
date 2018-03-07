#ifndef _SLAVE_REPLICATION_CHANNEL_MOCK_HPP
#define _SLAVE_REPLICATION_CHANNEL_MOCK_HPP

#define SERVER_MODE

#include "slave_replication_channel.hpp"
#include "connection_defs.h"

class slave_replication_channel_mock
{
  public:
    slave_replication_channel_mock (int port);
    slave_replication_channel slave_channel;
    int init ();
};

namespace slave
{
  slave_replication_channel_mock *init_mock (int port);
  int init ();
  int finish ();
}

#endif /* _SLAVE_REPLICATION_CHANNEL_MOCK_HPP */
