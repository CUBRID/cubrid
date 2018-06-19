#include "master_replication_channel_mock.hpp"

#define SERVER_MODE
#include "master_replication_channel_manager.hpp"
#if defined (WINDOWS)
#include "tcp.h"
#else
#include "wintcp.h"
#endif
#include "error_code.h"
#include <sys/poll.h>
#include "thread_manager.hpp"
#include <assert.h>
#include "test_output.hpp"
#include "connection_cl.h"
#include "thread_looper.hpp"
#include "mock_stream.hpp"

static mock_stream master_mock_stream;

namespace master
{

  void init ()
  {
    master_mock_stream.init (0);

    cubreplication::master_replication_channel_manager::init (&master_mock_stream);
  }

  void finish ()
  {
    cubreplication::master_replication_channel_manager::reset ();
  }

  dummy_test_daemon::dummy_test_daemon () : channel (NULL), num_of_loops (0)
  {

  }

  void dummy_test_daemon::execute (void)
  {
    num_of_loops++;
    std::this_thread::sleep_for (std::chrono::milliseconds (850));
  }

  void dummy_test_daemon::retire ()
  {
    /* test whether this ran */

    assert (num_of_loops > 0);
    delete this;
  }

  void dummy_test_daemon::set_channel (std::shared_ptr<cubreplication::master_replication_channel> &ch)
  {
    channel = ch;
  }

  void stream_produce (unsigned int num_bytes)
  {
    master_mock_stream.produce (num_bytes);
  }

} /* namespace master */
