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
#include <iostream>

namespace master
{

  void init ()
  {
    /* TODO[arnia] add stream */
    cubreplication::master_replication_channel_manager::init (NULL);
  }

  void finish ()
  {
    cubreplication::master_replication_channel_manager::reset ();
  }

  dummy_print_daemon::dummy_print_daemon () : channel (NULL), num_of_loops (0)
  {

  }

  void dummy_print_daemon::execute (void)
  {
    num_of_loops++;
    std::this_thread::sleep_for (std::chrono::milliseconds (850));
  }

  void dummy_print_daemon::retire ()
  {
    /* each slave sends each second a message to be received,
    * so a daemon running with an 850 ms sleeping pattern
    * should have made about NUM_OF_MSG_SENT or more loops before
    * being retired
    */
    // TODO[arnia] make a case here
    assert (num_of_loops > 0);
    delete this;
  }

  void dummy_print_daemon::set_channel (std::shared_ptr<cubreplication::master_replication_channel> &ch)
  {
    channel = ch;
  }

} /* namespace master */
