#include "master_replication_channel.hpp"

#include "connection_defs.h"
#include "thread_manager.hpp"
#include "thread_entry_task.hpp"
#include "thread_looper.hpp"

class master_server_loop : public cubthread::entry_task
{
  public:
    master_server_loop (master_replication_channel *ch) : channel (ch)
    {
    }

    void execute (cubthread::entry &context)
    {
      int rc, num_of_fds;

      while (channel->get_is_loop_running())
	{
	  num_of_fds = channel->get_number_of_slaves ();
	  rc = channel->poll_for_requests();

	  for (int i = 0; i < num_of_fds; i++)
	    {
	      if (channel->test_for_events (i, POLLIN) != 0)
		{
		  //channel->recv ()
		}
	    }

	}
    }

    void retire ()
    {

    }
  private:
    master_replication_channel *channel;
};

master_replication_channel::master_replication_channel () : m_current_number_of_connected_slaves (0),
  is_loop_running (true)
{
  /* start communication daemon thread */
  cubthread::manager *session_manager = cubthread::get_manager ();

  master_loop_daemon = session_manager->create_daemon (cubthread::looper (std::chrono::seconds (0)),
		       new master_server_loop (this));
}

int master_replication_channel::add_slave_connection (int sock_fd)
{
  if (m_current_number_of_connected_slaves >= MAX_SLAVE_CONNECTIONS)
    {
      return REQUEST_REFUSED;
    }

  slave_fds [m_current_number_of_connected_slaves].fd = sock_fd;
  slave_fds [m_current_number_of_connected_slaves].events = POLLIN;

  m_current_number_of_connected_slaves++;
  return NO_ERRORS;
}

int master_replication_channel::poll_for_requests()
{
  return poll (slave_fds, m_current_number_of_connected_slaves, TCP_MAX_TIMEOUT_IN_MS);
}

bool master_replication_channel::get_is_loop_running ()
{
  return is_loop_running;
}

void master_replication_channel::stop_loop ()
{
  is_loop_running = false;
}

int master_replication_channel::get_number_of_slaves()
{
  return m_current_number_of_connected_slaves;
}

short master_replication_channel::test_for_events (int slave_index, short flag)
{
  if (slave_index >= m_current_number_of_connected_slaves)
    {
      return 0;
    }

  return slave_fds[slave_index].revents & POLLIN;
}

master_replication_channel::~master_replication_channel ()
{
}
