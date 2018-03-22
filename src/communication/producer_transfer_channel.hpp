#ifndef _MASTER_TRANSFER_CHANNEL_HPP
#define _MASTER_TRANSFER_CHANNEL_HPP

#include "communication_channel.hpp"

#include <atomic>

namespace cubthread
{
  class daemon;
  class entry_task;
};

class producer_transfer_channel : public stream_handler
{

  public:
    friend class default_producer_transfer_channel_sender_task;

    producer_transfer_channel (communication_channel *chn, cubthread::entry_task *sender_task,
			       stream_position begin_sending_position = 0);
    producer_transfer_channel (communication_channel *chn, stream_position begin_sending_position = 0);
    ~producer_transfer_channel ();

    std::atomic<stream_position> &get_last_sent_position ();
    std::atomic<stream_position> &get_soft_limit ();
    std::atomic<stream_position> &get_hard_limit ();
    communication_channel &get_communication_channel ();

    int handling_action (BUFFER_UNIT *ptr, std::size_t byte_count) override;

    inline void set_stream (packing_stream *stream)
    {
      this->stream = stream;
    }
    inline void set_soft_limit (stream_position position)
    {
      this->m_soft_limit_position = position;
    }
    inline void set_hard_limit (stream_position position)
    {
      this->m_hard_limit_position = position;
    }

  private:
    communication_channel *m_channel;
    std::atomic<stream_position> m_last_sent_position, m_soft_limit_position, m_hard_limit_position;
    cubthread::daemon *m_sender_daemon;
    char m_buffer[MTU];
    packing_stream *stream;
};
#endif /* _MASTER_TRANSFER_CHANNEL_HPP */
