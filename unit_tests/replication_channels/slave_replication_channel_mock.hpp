#ifndef _SLAVE_REPLICATION_CHANNEL_MOCK_HPP
#define _SLAVE_REPLICATION_CHANNEL_MOCK_HPP

#define SERVER_MODE

#include "slave_replication_channel.hpp"
#include "connection_defs.h"

#include <vector>

class slave_replication_channel_mock
{
  public:
    slave_replication_channel_mock (cub_server_communication_channel &&chn);
    ~slave_replication_channel_mock () = default;

    cubreplication::slave_replication_channel slave_channel;
    cubstream::mock_packing_stream mock_stream;
};

namespace slave
{
  slave_replication_channel_mock *init_mock (int port);
  int init ();
  int finish ();

  std::vector <slave_replication_channel_mock *> &get_slaves ();
} /* namespace slave */

#endif /* _SLAVE_REPLICATION_CHANNEL_MOCK_HPP */
