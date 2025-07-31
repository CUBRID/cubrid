/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * master_connector.hpp
 */

#ifndef _THREAD_MASTER_CONNECTOR_HPP_
#define _THREAD_MASTER_CONNECTOR_HPP_

#include "packet_buffer.hpp"
#include "connection_globals.h"
#include "epoll.hpp"
#include "connection_sr.h"
#include "tcp.h"
#include "span.hpp"
#include "porting.h"

#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <string>
#include <type_traits>

namespace cubthread
{
  template <typename T>
  class master_connector
  {
      static_assert (
	      std::is_same<T, cubsocket::epoll>::value,
	      "T must be the child of cubsocket::nonblocking (cubsocket::epoll)"
      );

    public:
      master_connector ();
      ~master_connector ();

      bool connect (int port, std::string &server_name) noexcept;

    private:
      const int m_bufsize = 8;

      T m_events;
      cubbase::packet_buffer m_sendbuf;

      int m_port;

      bool recv () noexcept;
      bool send () noexcept;

      inline bool make_nonblocking (int fd) noexcept;
      inline void set_registrant (CSS_SERVER_PROC_REGISTER *proc_register, std::string &server_name) noexcept;
      inline int connect_to_master (int port) noexcept;
      inline bool register_in_master (CSS_CONN_ENTRY *conn, std::string &server_name) noexcept;
  };

  template <typename T>
  master_connector<T>::master_connector () :
    m_sendbuf (m_bufsize)
  {
  }

  template <typename T>
  master_connector<T>::~master_connector ()
  {
  }

  template <typename T>
  bool master_connector<T>::connect (int port, std::string &server_name) noexcept
  {
    CSS_CONN_ENTRY *conn;
    SOCKET fd;

    fd = this->connect_to_master (port);
    if (IS_INVALID_SOCKET (fd))
      {
	return false;
      }

    assert (!this->m_events.is_nonblocking (fd));

    if (!this->make_nonblocking (fd))
      {
	close (fd);
	return false;
      }

    assert (this->m_events.is_nonblocking (fd));

    /* make new connection */
    conn = css_make_conn (fd);

    /* at first, it must be registered */
    if (!m_events.add_descriptor (fd, EPOLLET | EPOLLOUT))
      {
	close (fd);
	return false;
      }

    /* register this cub_server in cub_master */
    if (!this->register_in_master (conn, server_name))
      {
	close (fd);
	return false;
      }

    close (fd);
    return true;
  }

  template <typename T>
  bool master_connector<T>::recv () noexcept
  {

  }

  template <typename T>
  bool master_connector<T>::send () noexcept
  {
    const int MAX_EVENTS = 2;

    epoll_event events[MAX_EVENTS];
    struct ::msghdr msg = { 0, 0, 0, 0, 0, 0, 0 };
    cubsocket::epoll::iores res;
    int nfds;

    assert (m_sendbuf.get_buffer ().size () != 0);

    msg.msg_iov = reinterpret_cast<struct ::iovec *> (m_sendbuf.get_buffer ().data ());
    msg.msg_iovlen = m_sendbuf.get_buffer ().size ();

    res = cubsocket::epoll::iores::unknown;
    do
      {
	nfds = m_events.wait (events, MAX_EVENTS, TIMEOUT_INFINITE);
	if (nfds != 1)
	  {
	    /* I added only one socket to this epoll */
	    return false;
	  }
	if (events[0].events & EPOLLOUT)
	  {
	    res = m_events.send (events[nfds].data.fd, &msg, /* budget */ m_sendbuf.get_length () + 1);
	    switch (res)
	      {
	      case cubsocket::epoll::iores::peer_reset:
		if (__builtin_expect (!m_events.remove_descriptor (events[nfds].data.fd), 0))
		  {
		    _er_log_debug (__FILE__, __LINE__, "[w] fcntl failed.");
		    assert_release (false);
		  }

	      case cubsocket::epoll::iores::done:
	      case cubsocket::epoll::iores::would_block:
		break;

	      default:
		/* something was wrong */
		_er_log_debug (__FILE__, __LINE__, "[w] send error %d", res);
		break;
	      }
	  }
      }
    while (res == cubsocket::epoll::iores::would_block);

    return res == cubsocket::epoll::iores::done;
  }

  template <typename T>
  inline bool master_connector<T>::make_nonblocking (int fd) noexcept
  {
    int flags;

    if (__builtin_expect (
		(flags = m_events.get_flags (fd)) == -1 ||
		m_events.set_flags (fd, flags | O_NONBLOCK) == -1
		, 0))
      {
	return false;
      }
    return true;
  }

  template <typename T>
  inline void master_connector<T>::set_registrant (CSS_SERVER_PROC_REGISTER *proc_register,
      std::string &server_name) noexcept
  {
    char *p, *last;
    char **argv;

    memcpy (proc_register->server_name, server_name.c_str (), server_name.length ());
    proc_register->server_name_length = server_name.length ();
    proc_register->pid = getpid ();
    strncpy_bufsize (proc_register->exec_path, css_get_exec_path ());

    p = (char *) proc_register->args;
    last = p + proc_register->CSS_SERVER_MAX_SZ_PROC_ARGS;
    for (argv = css_get_argv (); *argv; argv++)
      {
	p += snprintf (p, MAX ((last - p), 0), "%s ", *argv);
      }
  }

  template <typename T>
  inline int master_connector<T>::connect_to_master (int port) noexcept
  {
    char hostname[CUB_MAXHOSTNAMELEN];
    int fd;

    if (GETHOSTNAME (hostname, CUB_MAXHOSTNAMELEN) != 0)
      {
	er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_TCP_HOST_NAME_ERROR, 0);
	return -1;
      }

    /* connect to cub_master */
    fd = css_tcp_client_open ((char *) hostname, port);
    if (IS_INVALID_SOCKET (fd))
      {
	/* error has already been set. */
	return -1;
      }

    return fd;
  }

  template <typename T>
  inline bool master_connector<T>::register_in_master (CSS_CONN_ENTRY *conn, std::string &server_name) noexcept
  {
    /* header[0]: magic number packet */
    /* header[1]: command header packet */
    /* header[2]: data header for registrant packet */
    NET_HEADER header[3] = { { 0, 0, 0, 0, 0, 0, 0, 0, 0 }, DEFAULT_HEADER_DATA, DEFAULT_HEADER_DATA };
    CSS_SERVER_PROC_REGISTER registrant = CSS_SERVER_PROC_REGISTER_INITIALIZER;
    unsigned short request_id;

    assert (conn != NULL);

    /* cub_server magic number to be delivered to cub_master */
    memcpy ((char *) &header[0], css_Net_magic, sizeof (css_Net_magic));

    /* make the name pakcet to register this server to cub_master */
    this->set_registrant (&registrant, server_name);

    /* headers */
    request_id = css_get_request_id (conn);
    css_set_net_header (&header[1], COMMAND_TYPE, SERVER_REQUEST_FROM_SERVER, request_id, sizeof (CSS_SERVER_PROC_REGISTER),
			conn->get_tran_index (),
			conn->invalidate_snapshot, conn->db_error);
    css_set_net_header (&header[2], DATA_TYPE, 0, request_id, sizeof (CSS_SERVER_PROC_REGISTER), conn->get_tran_index (),
			conn->invalidate_snapshot, conn->db_error);

    /* clear the packet buffer */
    m_sendbuf.clear ();
    /* register the packets */
    m_sendbuf.push ({ reinterpret_cast<std::byte *> (&header[0]), sizeof (NET_HEADER) });
    m_sendbuf.push ({ reinterpret_cast<std::byte *> (&header[1]), sizeof (NET_HEADER) });
    m_sendbuf.push ({ reinterpret_cast<std::byte *> (&header[2]), sizeof (NET_HEADER) });
    m_sendbuf.push ({ reinterpret_cast<std::byte *> (&registrant), sizeof (CSS_SERVER_PROC_REGISTER) });

    /* send all */
    if (__builtin_expect (!this->send (), 0))
      {
	return false;
      }

    /* if not clear here, it may reference dangling pointer */
    m_sendbuf.clear ();

    return true;
  }
}

#endif
