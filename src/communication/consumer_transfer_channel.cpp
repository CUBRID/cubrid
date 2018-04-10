#include "consumer_transfer_channel.hpp"

#include "thread_manager.hpp"
#include "thread_daemon.hpp"
#include "thread_entry_task.hpp"

class default_consumer_transfer_channel_receiver_task : public cubthread::entry_task
{
  public:
    default_consumer_transfer_channel_receiver_task (consumer_transfer_channel *consumer_channel) : this_consumer_channel (
	consumer_channel) {}

    void execute (cubthread::entry &context) override
    {
      int rc = 0;
      size_t max_len = MTU;

      if (this_consumer_channel->stream == NULL ||
	  !this_consumer_channel->m_channel->is_connection_alive ())
	{
	  return;
	}

      rc = this_consumer_channel->m_channel->recv (this_consumer_channel->m_buffer, max_len);
      if (rc != NO_ERRORS)
	{
	  this_consumer_channel->m_channel->close_connection ();
	  return;
	}

      rc = this_consumer_channel->stream->write (max_len, this_consumer_channel);
      if (rc != NO_ERRORS)
	{
	  this_consumer_channel->m_channel->close_connection ();
	  return;
	}
    }

  private:
    consumer_transfer_channel *this_consumer_channel;
};

consumer_transfer_channel::consumer_transfer_channel (communication_channel *chn, cubthread::entry_task *recv_task,
    stream_position received_from_position) : m_channel (chn),
  m_last_received_position (received_from_position),
  stream (NULL)
{
  m_receiver_daemon = cubthread::get_manager ()->create_daemon (std::chrono::milliseconds (10), recv_task);
}

consumer_transfer_channel::consumer_transfer_channel (communication_channel *chn,
    stream_position received_from_position) : consumer_transfer_channel (chn,
                                                                         new default_consumer_transfer_channel_receiver_task (this),
                                                                         received_from_position)
{

}

consumer_transfer_channel::~consumer_transfer_channel ()
{
  cubthread::get_manager ()->destroy_daemon (m_receiver_daemon);
  delete m_channel;
}

int consumer_transfer_channel::write_action (const stream_position pos, char *ptr, const size_t byte_count)
{
  std::size_t recv_bytes = byte_count;
  int rc = NO_ERRORS;

  memcpy (ptr + pos, m_buffer, recv_bytes);

  if (rc == NO_ERRORS)
    {
      m_last_received_position += recv_bytes;
    }
  return rc;
}
