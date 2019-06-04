#ifndef _SLAVE_REPLICATION_CHANNEL_MOCK_HPP
#define _SLAVE_REPLICATION_CHANNEL_MOCK_HPP

#define SERVER_MODE

#include "stream_transfer_receiver.hpp"
#include "communication_server_channel.hpp"
#include "connection_defs.h"
#include "mock_stream.hpp"

#include <vector>

class slave_replication_channel_mock
{
  public:
    slave_replication_channel_mock (cubcomm::server_channel &&chn);
    ~slave_replication_channel_mock () = default;

    cubstream::transfer_receiver slave_channel;
    mock_stream m_stream;
};

namespace slave
{
  slave_replication_channel_mock *init_mock (int port);
  int init ();
  int finish ();
  int destroy ();

  std::vector <slave_replication_channel_mock *> &get_slaves ();
} /* namespace slave */

#endif /* _SLAVE_REPLICATION_CHANNEL_MOCK_HPP */
