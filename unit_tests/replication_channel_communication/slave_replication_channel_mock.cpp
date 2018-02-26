#include "slave_replication_channel_mock.hpp"

#include <iostream>
#include "thread_looper.hpp"
#include "connection_sr.h"
#include "cub_master_mock.hpp"
#include "thread_manager.hpp"
#include "thread_worker_pool.hpp"

#if defined(WINDOWS)
#include "wintcp.h"
#endif

static cubthread::entry_workpool *workpool = NULL;
static std::vector <slave_replication_channel_mock *> slaves;
static std::mutex slave_vector_mutex;

class slave_mock_send_msg : public cubthread::entry_task
{
  public:
    slave_mock_send_msg (slave_replication_channel *ch) : channel (ch)
    {
      if (GETHOSTNAME (this_hostname, MAXHOSTNAMELEN) != 0)
	{
	  strcpy (this_hostname, "unknown");
	}
      this_hostname[MAXHOSTNAMELEN-1] = '\0';
    }

    void execute (cubthread::entry &context)
    {
      if (!IS_INVALID_SOCKET (channel->get_master_conn_entry()->fd))
	{
	  int rc = channel->send (channel->get_master_conn_entry(), std::string ("test"),
				  communication_channel::get_max_timeout());
	  if (rc == ERROR_ON_WRITE)
	    {
	      /* this probably means that the connection was closed by master*/
	      channel->close_master_conn();
	      return;
	    }
	  else if (rc != NO_ERRORS)
	    {
	      assert (false);
	    }
	}
    }

  private:
    slave_replication_channel *channel;
    char this_hostname[MAXHOSTNAMELEN];
};

slave_replication_channel_mock::slave_replication_channel_mock (int port) : slave_channel ("localhost", "", port)
{

}

int slave_replication_channel_mock::init ()
{
  int rc;

  rc = slave_channel.connect_to_master();
  if (rc != NO_ERRORS)
    {
      return rc;
    }

  rc = slave_channel.start_daemon (cubthread::looper (std::chrono::seconds (1)),
				   new slave_mock_send_msg (&slave_channel));
  if (rc != NO_ERROR)
    {
      return rc;
    }

  return NO_ERROR;
}

namespace slave
{
  class start_slaves_task : public cubthread::entry_task
  {
      void execute (cubthread::entry &context)
      {
	slave_replication_channel_mock *mock = slave::init_mock (LISTENING_PORT);

	std::lock_guard<std::mutex> guard (slave_vector_mutex);
	slaves.push_back (mock);
      }
  };

  slave_replication_channel_mock *init_mock (int port)
  {
    slave_replication_channel_mock *mock = new slave_replication_channel_mock (port);

    int rc = mock->init();
    assert (rc == NO_ERROR);

    return mock;
  }

  int init ()
  {
    workpool = cubthread::get_manager()->create_worker_pool (4, NUM_MOCK_SLAVES);

    for (int i = 0; i < NUM_MOCK_SLAVES; i++)
      {
	cubthread::get_manager()->push_task (cubthread::get_manager()->get_entry(), workpool, new start_slaves_task ());
      }

    return NO_ERROR;
  }

  int finish ()
  {
    if (workpool != NULL)
      {
	cubthread::get_manager()->destroy_worker_pool (workpool);
      }

    for (unsigned int i = 0; i < slaves.size(); i++)
      {
	delete slaves[i];
      }

    return NO_ERROR;
  }

}
