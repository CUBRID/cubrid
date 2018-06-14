#define SERVER_MODE

#include "cub_master_mock.hpp"

#include "master_replication_channel_mock.hpp"
#include "thread_entry_task.hpp"
#include "thread_manager.hpp"
#include "thread_looper.hpp"
#if defined (WINDOWS)
#include "tcp.h"
#else
#include "wintcp.h"
#endif
#include "connection_cl.h"

cubstream::mock_packing_stream master_mock_stream;

namespace cub_master_mock
{

  static cubthread::daemon *cub_master_daemon = NULL;
  static int listen_fd[2];
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
          CSS_CONN_ENTRY *conn;
          int function_code;
          int buffer_size;
          unsigned short rid;
          css_error_code err = NO_ERRORS;

          new_sockfd = css_master_accept (listen_poll_fd.fd);
          buffer_size = sizeof (NET_HEADER);

          if (IS_INVALID_SOCKET (new_sockfd))
            {
              assert (false);
              return;
            }

          conn = css_make_conn (new_sockfd);
          if (conn == NULL)
            {
              return;
            }

          if (css_check_magic (conn) != NO_ERRORS)
            {
              css_free_conn (conn);
              return;
            }

          do
            {
              rc = css_read_one_request (conn, &rid, &function_code, &buffer_size);
            }
          while (rc == WRONG_PACKET_TYPE);

          if (function_code != SERVER_REQUEST_CONNECT_NEW_SLAVE)
          {
            css_free_conn (conn);
            assert (false);
            return;
          }

          conn->fd = INVALID_SOCKET;
          css_free_conn (conn);

          communication_channel listener_chn;

          err = listener_chn.accept (new_sockfd);
          if (err != NO_ERRORS)
            {
              assert (false);
              return;
            }

          cubreplication::master_replication_channel_manager::add_master_replication_channel (cubreplication::master_replication_channel_entry (std::move (listener_chn),
                                                                                              master_mock_stream,
                                                                                              cubreplication::CHECK_FOR_GC,
                                                                                              new cubreplication::check_for_gc_task (),
                                                                                              cubreplication::FOR_TESTING,
                                                                                              new master::dummy_print_daemon ()));
          }
      }
    };

  int init ()
  {
    int rc;

    master_mock_stream.init (0);

    rc = css_tcp_master_open (LISTENING_PORT, listen_fd);
    if (rc != NO_ERROR)
      {
        return rc;
      }

    listen_poll_fd.fd = listen_fd[1];
    listen_poll_fd.events = POLLIN;

    cub_master_daemon = cubthread::get_manager()->create_daemon (cubthread::looper (std::chrono::seconds (0)), new cub_master_daemon_task (), "cub_master_daemon");

    return NO_ERROR;
  }

  int finish ()
  {
    cubthread::get_manager ()->destroy_daemon (cub_master_daemon);
    return close (listen_poll_fd.fd);
  }

  void stream_produce (unsigned int num_bytes)
  {
    master_mock_stream.produce (num_bytes);
  }

} /* namespace cub_master_mock */
