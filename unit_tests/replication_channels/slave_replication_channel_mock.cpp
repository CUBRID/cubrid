#include "slave_replication_channel_mock.hpp"

#include <iostream>
#include "thread_looper.hpp"
#include "connection_sr.h"
#include "cub_master_mock.hpp"
#include "thread_manager.hpp"
#include "thread_worker_pool.hpp"
#include "thread_entry_task.hpp"

static cubthread::entry_workpool *workpool = NULL;
static std::vector <slave_replication_channel_mock *> slaves;
static std::mutex slave_vector_mutex;

slave_replication_channel_mock::slave_replication_channel_mock (cubcomm::server_channel &&chn)
  : slave_channel (std::forward <cubcomm::server_channel> (chn), m_stream, 0)
{
  m_stream.init (0);
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
    css_error_code err = NO_ERRORS;
    cubcomm::server_channel chn ("", 1000);
    slave_replication_channel_mock *mock;

    err = chn.connect ("127.0.0.1", port, SERVER_REQUEST_CONNECT_NEW_SLAVE);
    assert (err == NO_ERRORS);
    assert (chn.is_connection_alive ());

    mock = new slave_replication_channel_mock (std::move (chn));

    return mock;
  }

  int init ()
  {
    workpool = cubthread::get_manager()->create_worker_pool (4, NUM_MOCK_SLAVES, NULL, NULL, 1, false);

    for (int i = 0; i < NUM_MOCK_SLAVES; i++)
      {
	cubthread::get_manager()->push_task (workpool, new start_slaves_task ());
      }

    return NO_ERROR;
  }

  int finish ()
  {
    if (workpool != NULL)
      {
	cubthread::get_manager()->destroy_worker_pool (workpool);
      }

    return NO_ERROR;
  }

  int destroy ()
  {
    for (unsigned int i = 0; i < slaves.size(); i++)
      {
	delete slaves[i];
      }
    slaves.clear ();

    return NO_ERROR;
  }

  std::vector <slave_replication_channel_mock *> &get_slaves ()
  {
    return slaves;
  }

} /* namespace slave */
