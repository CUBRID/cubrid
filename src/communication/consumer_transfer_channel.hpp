#ifndef _SLAVE_TRANSFER_CHANNEL_HPP
#define _SLAVE_TRANSFER_CHANNEL_HPP

#include "communication_channel.hpp"
#include "cubstream.hpp"
#include <atomic>

namespace cubthread
{
  class daemon;
  class entry_task;
};

class consumer_transfer_channel : public cubstream::write_handler
{
  public:
    friend class default_consumer_transfer_channel_receiver_task;

    consumer_transfer_channel (communication_channel *chn, cubthread::entry_task *recv_task, stream_position received_from_position = 0);
    consumer_transfer_channel (communication_channel *chn, stream_position received_from_position = 0);
    virtual ~consumer_transfer_channel ();

    virtual int write_action (const stream_position pos, char *ptr, const size_t byte_count) override;

    inline void set_stream (cubstream::stream *stream)
    {
      this->stream = stream;
    }

  private:
    communication_channel *m_channel;
    std::atomic<stream_position> m_last_received_position;
    cubthread::daemon *m_receiver_daemon;
    char m_buffer[MTU];
    cubstream::stream *stream;
};

#endif /* _SLAVE_TRANSFER_CHANNEL_HPP */
