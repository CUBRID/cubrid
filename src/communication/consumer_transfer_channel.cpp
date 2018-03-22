#include "consumer_transfer_channel.hpp"

#include "thread_manager.hpp"
#include "thread_daemon.hpp"
#include "thread_entry_task.hpp"

class consumer_transfer_channel_receiver_task : public cubthread::entry_task
{
  public:
    consumer_transfer_channel_receiver_task (consumer_transfer_channel *consumer_channel) : this_consumer_channel (
	consumer_channel) {}

    void execute (cubthread::entry &context) override
    {
      int rc = 0;

      if (this_consumer_channel->stream == NULL ||
	  !this_consumer_channel->m_channel->is_connection_alive ())
	{
	  return;
	}

      rc = this_consumer_channel->stream->write (MTU, this_consumer_channel);
      //assert (rc == NO_ERRORS);
      if (rc != NO_ERRORS)
	{
	  this_consumer_channel->m_channel->close_connection ();
	}
    }

  private:
    consumer_transfer_channel *this_consumer_channel;
};

consumer_transfer_channel::consumer_transfer_channel (communication_channel *chn,
    stream_position received_from_position) : m_channel (chn),
  m_last_received_position (received_from_position),
  stream (NULL)
{
  m_receiver_daemon = cubthread::get_manager ()->create_daemon (std::chrono::milliseconds (10),
		      new consumer_transfer_channel_receiver_task (this));
}

consumer_transfer_channel::~consumer_transfer_channel ()
{
  cubthread::get_manager ()->destroy_daemon (m_receiver_daemon);
  delete m_channel;
}

int consumer_transfer_channel::handling_action (BUFFER_UNIT *ptr, std::size_t byte_count)
{
  std::size_t recv_bytes = byte_count;
  int rc;

  rc = m_channel->recv (ptr, recv_bytes);
  if (rc == NO_ERRORS)
    {
      m_last_received_position += recv_bytes;
    }
  return rc;
}
