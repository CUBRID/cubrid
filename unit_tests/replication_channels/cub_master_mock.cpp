#define SERVER_MODE

#include "cub_master_mock.hpp"

#include "master_replication_channel_mock.hpp"
#include "thread_entry_task.hpp"
#include "thread_manager.hpp"
#include "thread_looper.hpp"
#if !defined (WINDOWS)
#include "tcp.h"
#include <netinet/in.h>
#else
#include "wintcp.h"
#include <Winsock2.h>
#endif
#include "connection_cl.h"

namespace cub_master_mock
{

  static cubthread::daemon *cub_master_daemon = NULL;
  static SOCKET listen_fd[2];
  static POLL_FD listen_poll_fd;

  class cub_master_daemon_task : public cubthread::entry_task
  {
      /* this simulates a cub_master process */

      void execute (cubthread::entry &context)
      {
	int rc;

	rc = css_platform_independent_poll (&listen_poll_fd, 1, 3000);
	assert (rc >= 0);

	if (rc == 0)
	  {
	    //timeout
	    return;
	  }

	if ((listen_poll_fd.revents & POLLIN) != 0)
	  {
	    int new_sockfd;
	    int buffer_size;
	    unsigned short rid;
	    css_error_code err = NO_ERRORS;
	    NET_HEADER header;

	    new_sockfd = css_master_accept (listen_poll_fd.fd);
	    buffer_size = sizeof (NET_HEADER);

	    if (IS_INVALID_SOCKET (new_sockfd))
	      {
		assert (false);
		return;
	      }

	    if (css_check_magic_with_socket (new_sockfd) != NO_ERRORS)
	      {
		assert (false);
		return;
	      }

	    rc = css_net_read_header (new_sockfd, (char *) &header, &buffer_size, -1);
	    if (rc != NO_ERRORS)
	      {
		assert (false);
		return;
	      }

	    if ((int) (unsigned short) ntohs (header.function_code) != SERVER_REQUEST_CONNECT_NEW_SLAVE
                && (int) (unsigned short) ntohs (header.function_code) != SERVER_REQUEST_CONNECT_SLAVE_COPY_DB)
	      {
		assert (false);
		return;
	      }

	    cubcomm::channel listener_chn;

	    err = listener_chn.accept (new_sockfd);
	    if (err != NO_ERRORS)
	      {
		assert (false);
		return;
	      }

	    cubreplication::master_senders_manager::add_stream_sender (
		    new cubstream::transfer_sender (std::move (listener_chn),
						    cubreplication::master_senders_manager::get_stream ()));
	  }
      }
  };

  int init ()
  {
    int rc;

    rc = css_tcp_master_open (LISTENING_PORT, listen_fd);
    if (rc != NO_ERROR)
      {
	return rc;
      }

#if !defined (WINDOWS)
    listen_poll_fd.fd = listen_fd[1];
#else
    listen_poll_fd.fd = listen_fd[0];
#endif

    listen_poll_fd.events = POLLIN;

    cub_master_daemon = cubthread::get_manager()->create_daemon (
				cubthread::looper (std::chrono::seconds (0)), new cub_master_daemon_task (),
				"cub_master_daemon");

    return NO_ERROR;
  }

  int finish ()
  {
    cubthread::get_manager ()->destroy_daemon (cub_master_daemon);
#if !defined (WINDOWS)
    return close (listen_poll_fd.fd);
#else
    return closesocket (listen_poll_fd.fd);
#endif
  }

} /* namespace cub_master_mock */


