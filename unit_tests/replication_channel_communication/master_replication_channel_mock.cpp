#include "master_replication_channel_mock.hpp"

#define SERVER_MODE
#include "master_replication_channel_manager.hpp"
#if !defined (WINDOWS)
#include "tcp.h"
#else
#include "wintcp.h"
#endif
#include "error_code.h"
#include "connection_support.h"
#include "thread_manager.hpp"
#include <assert.h>
#include "test_output.hpp"
#include "connection_cl.h"
#include "thread_looper.hpp"
#include <iostream>

typedef struct pollfd POLL_FD;

namespace master
{

  void init ()
  {
    master_replication_channel_manager::init();
  }

  void finish ()
  {
    master_replication_channel_manager::reset();
  }

  receive_from_slave_daemon_mock::receive_from_slave_daemon_mock () : channel (NULL), num_of_received_messages (0)
  {
  }

  receive_from_slave_daemon_mock::receive_from_slave_daemon_mock (std::shared_ptr<master_replication_channel> ch) :
    channel (ch), num_of_received_messages (0)
  {
  }

  void receive_from_slave_daemon_mock::set_channel (std::shared_ptr<master_replication_channel> ch)
  {
    channel = ch;
  }

  void receive_from_slave_daemon_mock::execute (cubthread::entry &context)
  {
    int rc;
#define MAX_LENGTH 100
    char buffer [MAX_LENGTH];
    int recv_length = MAX_LENGTH;
    unsigned short int revents;

    if (!channel->get_cub_server_slave_channel ().is_connection_alive ())
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
	    channel->get_cub_server_slave_channel ().close_connection ();
	    return;
	  }
	else if (rc != NO_ERRORS)
	  {
	    assert (false);
	    return;
	  }
	buffer[recv_length] = '\0';
	if (strcmp (buffer, "test") == 0)
	  {
	    num_of_received_messages++;
	  }

	if (num_of_received_messages >= NUM_OF_MSG_SENT)
	  {
	    channel->get_cub_server_slave_channel ().close_connection ();
	  }
      }
#undef MAX_LENGTH
  }

  dummy_print_daemon::dummy_print_daemon () : channel (NULL), num_of_loops (0)
  {

  }

  dummy_print_daemon::dummy_print_daemon (std::shared_ptr<master_replication_channel> ch) : channel (ch), num_of_loops (0)
  {


  }

  void dummy_print_daemon::execute (cubthread::entry &context)
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
    assert (num_of_loops >= NUM_OF_MSG_SENT - 1);
    delete this;
  }

  void dummy_print_daemon::set_channel (std::shared_ptr<master_replication_channel> ch)
  {
    channel = ch;
  }

}
