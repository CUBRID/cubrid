#ifndef _SLAVE_REPLICATION_CHANNEL_HPP
#define _SLAVE_REPLICATION_CHANNEL_HPP

#include "cub_server_communication_channel.hpp"
#include "connection_defs.h"
#include "thread_entry_task.hpp"
#include "stream_transfer_receiver.hpp"

namespace cubthread
{
  class daemon;
  class looper;
};

namespace cubreplication {

class slave_replication_channel
{
  public:
    /* for testing purposes */
    //friend class slave_replication_channel_mock;

    slave_replication_channel (cub_server_communication_channel &&chn,
                                cubstream::stream &stream,
                                cubstream::stream_position received_from_position);
    ~slave_replication_channel ();

    bool is_connected ();
    inline cubstream::transfer_receiver &get_stream_receiver() { return m_stream_receiver; }

  private:
    cub_server_communication_channel m_with_master_comm_chn;
    cubstream::transfer_receiver m_stream_receiver;
};

} /* namespace cubreplication */

#endif
