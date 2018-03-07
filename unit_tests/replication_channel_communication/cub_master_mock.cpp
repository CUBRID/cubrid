#define CS_MODE

#include "cub_master_mock.hpp"

#include "master_replication_channel_mock.hpp"
#include "thread_entry_task.hpp"
#include "thread_manager.hpp"
#include "thread_looper.hpp"
#include "thread_daemon.hpp"
#if !defined (WINDOWS)
#include "tcp.h"
#else
#include "wintcp.h"
#endif
#include "connection_cl.h"

#if defined (WINDOWS)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

typedef CSS_CONN_ENTRY *(*MAKE_CONN_ENTRY_FP) (SOCKET fd);
typedef void (*FREE_CONN_ENTRY_FP) (CSS_CONN_ENTRY * conn);

#if !defined (WINDOWS)
static void *cubridcs_lib = NULL;
#else
static HINSTANCE cubridcs_lib;
#endif
static MAKE_CONN_ENTRY_FP make_conn_entry_fp = NULL;
static FREE_CONN_ENTRY_FP free_conn_entry_fp = NULL;


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
	int rc = css_platform_independent_poll (&listen_poll_fd, 1, 3000);
	assert (rc >= 0);

	if (rc == 0)
	  {
	    //timeout
	    return;
	  }

	if ((listen_poll_fd.revents & POLLIN) != 0)
	  {
	    int new_sockfd = css_master_accept (listen_poll_fd.fd);
	    CSS_CONN_ENTRY *conn = NULL;
	    int function_code;
	    int buffer_size;
	    unsigned short rid;

	    buffer_size = sizeof (NET_HEADER);

	    if (IS_INVALID_SOCKET (new_sockfd))
	      {
		return;
	      }

	    conn = make_conn_entry_fp (new_sockfd);
	    if (conn == NULL)
	      {
		return;
	      }

	    if (css_check_magic (conn) != NO_ERRORS)
	      {
		free_conn_entry_fp (conn);
		return;
	      }

	    do
	      {
		rc = css_read_one_request (conn, &rid, &function_code, &buffer_size);
	      }
	    while (rc == WRONG_PACKET_TYPE);

	    if (function_code != SERVER_REQUEST_CONNECT_NEW_SLAVE)
	      {
		free_conn_entry_fp (conn);
		assert (false);
		return;
	      }

	    rc = master_replication_channel_manager::add_master_replication_channel (master_replication_channel_entry (new_sockfd,
		RECEIVE_FROM_SLAVE, new master::receive_from_slave_daemon_mock (), ANOTHER_DAEMON_FOR_TESTING,
		new master::dummy_print_daemon ()));
            if (rc == NO_ERROR)
              {
                conn->fd = INVALID_SOCKET;
              }

            free_conn_entry_fp (conn);
	  }
      }
  };

  int init ()
  {
    #if defined (WINDOWS)
    cubridcs_lib = LoadLibrary ("cubridcs.dll");

    if (cubridcs_lib != NULL)
      {
        make_conn_entry_fp = (MAKE_CONN_ENTRY_FP) GetProcAddress (cubridcs_lib, "css_make_conn");
        free_conn_entry_fp = (FREE_CONN_ENTRY_FP) GetProcAddress (cubridcs_lib, "css_free_conn");

        assert (make_conn_entry_fp != NULL && free_conn_entry_fp != NULL);
      }
    else
      {
        assert (false);
      }
    #else
    cubridcs_lib = dlopen ("libcubridcs.so", RTLD_NOW | RTLD_GLOBAL);

    if (cubridcs_lib != NULL)
      {
        make_conn_entry_fp = (MAKE_CONN_ENTRY_FP) dlsym (cubridcs_lib, "_Z13css_make_conni");
        free_conn_entry_fp = (FREE_CONN_ENTRY_FP) dlsym (cubridcs_lib, "_Z13css_free_connP14css_conn_entry");

        assert (make_conn_entry_fp != NULL && free_conn_entry_fp != NULL);
      }
    else
      {
        assert (false);
      }
    #endif

    int rc = css_tcp_master_open (LISTENING_PORT, listen_fd);
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

    cub_master_daemon = new cubthread::daemon (std::chrono::seconds (0), new cubthread::daemon_entry_manager (), new cub_master_daemon_task ());

    return NO_ERROR;
  }

  int finish ()
  {
    cub_master_daemon->stop_execution();
    delete cub_master_daemon;
    cub_master_daemon = NULL;
#if !defined (WINDOWS)
    return close (listen_poll_fd.fd);
#else
	return closesocket (listen_poll_fd.fd);
#endif
  }
}
