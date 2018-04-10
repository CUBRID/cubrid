#include "producer_transfer_channel.hpp"

#include "thread_manager.hpp"
#include "thread_daemon.hpp"
#include "thread_entry_task.hpp"

class default_producer_transfer_channel_sender_task : public cubthread::entry_task
{
  public:
    default_producer_transfer_channel_sender_task (producer_transfer_channel *producer_channel) : this_producer_channel (
	producer_channel) {}

    void execute (cubthread::entry &context) override
    {
      int rc = 0;

      if (this_producer_channel->stream == NULL ||
	  !this_producer_channel->m_channel->is_connection_alive ())
	{
	  return;
	}

      if (this_producer_channel->m_soft_limit_position < this_producer_channel->m_last_sent_position + MTU &&
	  this_producer_channel->m_hard_limit_position <= this_producer_channel->m_last_sent_position)
	{
	  return;
	}

      if (this_producer_channel->m_hard_limit_position > this_producer_channel->m_last_sent_position)
	{
	  rc = this_producer_channel->stream->read (this_producer_channel->m_last_sent_position,
	       this_producer_channel->m_hard_limit_position - this_producer_channel->m_last_sent_position,
	       this_producer_channel);
	}
      else
	{
	  rc = this_producer_channel->stream->read (this_producer_channel->m_last_sent_position,
	       MTU,
	       this_producer_channel);
	}
      if (rc != NO_ERRORS)
	{
	  this_producer_channel->m_channel->close_connection ();
	}
    }

  private:
    producer_transfer_channel *this_producer_channel;
};

producer_transfer_channel::producer_transfer_channel (communication_channel *chn,
    cubthread::entry_task *sender_task,
    stream_position begin_sending_position) : m_channel (chn),
  m_last_sent_position (begin_sending_position),
  stream (NULL)
{
  m_sender_daemon = cubthread::get_manager ()->create_daemon (std::chrono::milliseconds (10), sender_task);
}

producer_transfer_channel::producer_transfer_channel (communication_channel *chn,
    stream_position begin_sending_position) : producer_transfer_channel (chn,
	  new default_producer_transfer_channel_sender_task (this),
	  begin_sending_position)
{
}

producer_transfer_channel::~producer_transfer_channel ()
{
  cubthread::get_manager ()->destroy_daemon (m_sender_daemon);
  delete m_channel;
}

communication_channel &producer_transfer_channel::get_communication_channel ()
{
  assert (m_channel != NULL);
  return *m_channel;
}

std::atomic<stream_position> &producer_transfer_channel::get_last_sent_position ()
{
  return m_last_sent_position;
}

std::atomic<stream_position> &producer_transfer_channel::get_soft_limit ()
{
  return m_soft_limit_position;
}

std::atomic<stream_position> &producer_transfer_channel::get_hard_limit ()
{
  return m_hard_limit_position;
}

int producer_transfer_channel::read_action (const stream_position pos, char *ptr, const size_t byte_count)
{
  int rc = m_channel->send (ptr, byte_count);

  if (rc == NO_ERRORS)
    {
      m_last_sent_position += byte_count;
    }

  return rc;
}
