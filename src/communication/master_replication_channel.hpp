#ifndef _MASTER_REPLICATION_CHANNEL_HPP
#define _MASTER_REPLICATION_CHANNEL_HPP

#include "connection_support.h"
#include "thread_entry_task.hpp"
#include "connection_defs.h"
#include "communication_channel.hpp"

class master_replication_channel
{
  public:
    master_replication_channel (int slave_fd = -1);
    ~master_replication_channel ();

    communication_channel &get_cub_server_slave_channel ();

    bool is_connected ();
  private:
    communication_channel cub_server_slave_channel;
};

class receive_from_slave_daemon : public cubthread::entry_task
{
  public:
    receive_from_slave_daemon () : channel (NULL)
    {

    }

    receive_from_slave_daemon (std::shared_ptr<master_replication_channel> ch) : channel (ch)
    {
    }

    void execute (cubthread::entry &context)
    {
      int rc;
#define MAX_LENGTH 100
      char buffer [MAX_LENGTH];
      int recv_length = MAX_LENGTH;
      unsigned short int revents = 0;

      if (!channel->is_connected ())
	{
	  /* don't go any further, wait for the manager supervisor to destroy it */
	  return;
	}

      rc = channel->get_cub_server_slave_channel ().wait_for (POLLIN, revents);
      if (rc < 0)
	{
	  /* smth went wrong with the connection, destroy it */
	  channel->get_cub_server_slave_channel ().close_connection ();
	  return;
	}

      if ((revents & POLLIN) != 0)
	{
	  rc = channel->get_cub_server_slave_channel ().recv (buffer, recv_length);
	  if (rc == ERROR_WHEN_READING_SIZE || rc == ERROR_ON_READ)
	    {
	      /* this usually means that the connection is closed
	        TODO[arnia] maybe add this case to recv to know for sure ?
	      */
	      channel->get_cub_server_slave_channel ().close_connection ();
	      return;
	    }
	  else if (rc != NO_ERRORS)
	    {
	      assert (false);
	      return;
	    }
	  buffer[recv_length] = '\0';
	  _er_log_debug (ARG_FILE_LINE, "master::execute:" "received=%s\n", buffer);
	}
#undef MAX_LENGTH
    }

    void set_channel (std::shared_ptr<master_replication_channel> channel)
    {
      this->channel = channel;
    }

  private:
    std::shared_ptr<master_replication_channel> channel;
};

#endif /* _MASTER_REPLICATION_CHANNEL_HPP */
