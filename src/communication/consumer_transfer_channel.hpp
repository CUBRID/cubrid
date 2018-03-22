#ifndef _SLAVE_TRANSFER_CHANNEL_HPP
#define _SLAVE_TRANSFER_CHANNEL_HPP

#include "communication_channel.hpp"

#include <atomic>

namespace cubthread
{
  class daemon;
};

class consumer_transfer_channel : public stream_handler
{
  public:
    friend class consumer_transfer_channel_receiver_task;

    consumer_transfer_channel (communication_channel *chn, stream_position received_from_position = 0);
    ~consumer_transfer_channel ();

    int handling_action (BUFFER_UNIT *ptr, std::size_t byte_count) override;

    inline void set_stream (packing_stream *stream)
    {
      this->stream = stream;
    }

  private:
    communication_channel *m_channel;
    std::atomic<stream_position> m_last_received_position;
    cubthread::daemon *m_receiver_daemon;
    char m_buffer[MTU];
    packing_stream *stream;
};

#endif /* _SLAVE_TRANSFER_CHANNEL_HPP */
