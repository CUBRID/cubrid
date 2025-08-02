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

#include "connection_globals.h"
#include "system_parameter.h"
#include "error_manager.h"

#include "server_support.h"
#include "filesys_temp.hpp"
#include "connection_sr.h"
#include "tcp.h"
#include "packet_buffer.hpp"
#include "epoll.hpp"
#include "span.hpp"
#include "porting.h"

#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <optional>
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

      CSS_CONN_ENTRY *get_connection () noexcept;
      bool connect (int port, std::string &server_name) noexcept;
      bool dispatch_connection () noexcept;

    private:
      const int m_bufsize = 8;

      T m_events;
      cubbase::packet_buffer m_packet;

      int m_port;
      CSS_CONN_ENTRY *m_connection;

      bool recv () noexcept;
      bool send () noexcept;

      inline bool make_nonblocking (int fd) noexcept;

      inline int connect_to_master (int port) noexcept;

      inline void set_registrant (CSS_SERVER_PROC_REGISTER *proc_register, std::string &server_name) noexcept;
      inline std::optional<int> send_register_request (CSS_CONN_ENTRY *conn, std::string &server_name) noexcept;
      inline int recv_register_response (CSS_CONN_ENTRY *conn) noexcept;

      inline CSS_CONN_ENTRY *switch_to_unix_socket (CSS_CONN_ENTRY *conn, int request_id) noexcept;

      inline CSS_CONN_ENTRY *register_in_master (CSS_CONN_ENTRY *conn, std::string &server_name) noexcept;
  };

  template <typename T>
  master_connector<T>::master_connector () :
    m_packet (m_bufsize),
    m_connection (nullptr)
  {
  }

  template <typename T>
  master_connector<T>::~master_connector ()
  {
  }

  template <typename T>
  CSS_CONN_ENTRY *master_connector<T>::get_connection () noexcept
  {
    return m_connection;
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
    if (!conn)
      {
	_er_log_debug (__FILE__, __LINE__, "[w] malloc failed: CSS_CONN_ENTRY");
	close (fd);
	return false;
      }

    /* register this cub_server in cub_master */
    conn = this->register_in_master (conn, server_name);
    if (!conn)
      {
	close (fd);
	return false;
      }

    m_connection = conn;

    return true;
  }

  template <typename T>
  bool master_connector<T>::dispatch_connection () noexcept
  {
    const int MAX_EVENTS = 2;

    epoll_event events[MAX_EVENTS];
    struct ::msghdr msg = { 0, 0, 0, 0, 0, 0, 0 };
    struct ::iovec iov = { nullptr, 0 };
    cubsocket::epoll::iores res;
    int nfds, i;
    int response;
    std::size_t available;

    iov.iov_base = &response;
    iov.iov_len = 4;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    /* TODO: add eventfd to wakeup temporary */
    if (!m_events.add_descriptor (m_connection->fd, EPOLLET | EPOLLIN, m_connection))
      {
	return false;
      }

    available = m_connection->recvbuf->available ();
    while (true)
      {
	
	nfds = m_events.wait (events, MAX_EVENTS, TIMEOUT_INFINITE);
	if (nfds <= 0)
	  {
	    _er_log_debug (__FILE__, __LINE__, "[w] epoll_wait failed: %s", strerror (errno));
	    return false;
	  }

	i = 0;
	do
	  {
	    assert (events[i].data.ptr);

	    if (reinterpret_cast<CSS_CONN_ENTRY *> (events[i].data.ptr)->fd == m_connection->fd)
	      {
	      }
	    else
	      {
		/* this must be eventfd */
	      }
	    i++;
	  }
	while (__builtin_expect (i < nfds, 0));

      }


      {
	nfds = m_events.wait (events, MAX_EVENTS, TIMEOUT_INFINITE);
	if (nfds != 1)
	  {
	    /* I added only one socket to this epoll */
	    return false;
	  }
	if (events[0].events & EPOLLIN)
	  {
	    res = m_events.recvmsg (events[0].data.fd, &msg);
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
		_er_log_debug (__FILE__, __LINE__, "[w] m_event->recvmsg error %d", res);
		break;
	      }
	  }
      }
 


        
//    m_events.wait ();

    return false;
  }

  template <typename T>
  bool master_connector<T>::recv () noexcept
  {
    const int MAX_EVENTS = 2;

    epoll_event events[MAX_EVENTS];
    struct ::msghdr msg = { 0, 0, 0, 0, 0, 0, 0 };
    cubsocket::epoll::iores res;
    int nfds;

    assert (m_packet.get_buffer ().size () != 0);

    msg.msg_iov = reinterpret_cast<struct ::iovec *> (m_packet.get_buffer ().data ());
    msg.msg_iovlen = m_packet.get_buffer ().size ();

    res = cubsocket::epoll::iores::unknown;
    do
      {
	nfds = m_events.wait (events, MAX_EVENTS, TIMEOUT_INFINITE);
	if (nfds != 1)
	  {
	    /* I added only one socket to this epoll */
	    return false;
	  }
	if (events[0].events & EPOLLIN)
	  {
	    res = m_events.recvmsg (events[0].data.fd, &msg);
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
		_er_log_debug (__FILE__, __LINE__, "[w] m_event->recvmsg error %d", res);
		break;
	      }
	  }
      }
    while (res == cubsocket::epoll::iores::would_block);

    return res == cubsocket::epoll::iores::done;
  }

  template <typename T>
  bool master_connector<T>::send () noexcept
  {
    const int MAX_EVENTS = 2;

    epoll_event events[MAX_EVENTS];
    struct ::msghdr msg = { 0, 0, 0, 0, 0, 0, 0 };
    cubsocket::epoll::iores res;
    int nfds;

    assert (m_packet.get_buffer ().size () != 0);

    msg.msg_iov = reinterpret_cast<struct ::iovec *> (m_packet.get_buffer ().data ());
    msg.msg_iovlen = m_packet.get_buffer ().size ();

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
	    res = m_events.sendmsg (events[0].data.fd, &msg);
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
		_er_log_debug (__FILE__, __LINE__, "[w] m_event->send error %d", res);
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
  inline std::optional<int> master_connector<T>::send_register_request (CSS_CONN_ENTRY *conn, std::string &server_name) noexcept
  {
    NET_HEADER header[3] = { { 0, 0, 0, 0, 0, 0, 0, 0, 0 }, DEFAULT_HEADER_DATA, DEFAULT_HEADER_DATA };
    /* header[0]: magic number packet */
    /* header[1]: command header packet */
    /* header[2]: data header for registrant packet */
    CSS_SERVER_PROC_REGISTER registrant = CSS_SERVER_PROC_REGISTER_INITIALIZER;
    unsigned short request_id;

    assert (conn != NULL);

    /* at first, it must be registered */
    if (!m_events.add_descriptor (conn->fd, EPOLLET | EPOLLOUT))
      {
	return std::nullopt;
      }

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
    m_packet.clear ();
    /* register the packets */
    m_packet.push_for_send ({ reinterpret_cast<std::byte *> (&header[0]), sizeof (NET_HEADER) });
    m_packet.push_for_send ({ reinterpret_cast<std::byte *> (&header[1]), sizeof (NET_HEADER) });
    m_packet.push_for_send ({ reinterpret_cast<std::byte *> (&header[2]), sizeof (NET_HEADER) });
    m_packet.push_for_send ({ reinterpret_cast<std::byte *> (&registrant), sizeof (CSS_SERVER_PROC_REGISTER) });
    /* send all */
    er_log_debug (__FILE__, __LINE__, "[w] send register packet: start\n");
    if (__builtin_expect (!this->send (), 0))
      {
	return std::nullopt;
      }
    er_log_debug (__FILE__, __LINE__, "[w] send register packet: done\n");
    return request_id;
  }

  template <typename T>
  inline int master_connector<T>::recv_register_response (CSS_CONN_ENTRY *conn) noexcept
  {
    int response;

    assert (conn != NULL);

    if (!m_events.modify_descriptor (conn->fd, EPOLLET | EPOLLIN))
      {
	return -1;
      }

    /* clear the packet buffer */
    m_packet.clear ();
    /* register the packets */
    m_packet.push ({ reinterpret_cast<std::byte *> (&response), sizeof (int) });
    /* recv */
    er_log_debug (__FILE__, __LINE__, "[w] recv register packet: start\n");
    if (__builtin_expect (!this->recv (), 0))
      {
	return -1;
      }
    er_log_debug (__FILE__, __LINE__, "[w] recv register packet: done\n");

    response = ntohl (response);
    return response;
  }

  template <typename T>
  inline CSS_CONN_ENTRY *master_connector<T>::switch_to_unix_socket (CSS_CONN_ENTRY *conn, int request_id) noexcept
  {
    NET_HEADER header = DEFAULT_HEADER_DATA;
    std::string unix_path;
    int unix_socket, datagram_fd;

    /* add EPOLLOUT */
    if (!m_events.modify_descriptor (conn->fd, EPOLLET | EPOLLOUT))
      {
	return nullptr;
      }

    /* send the "pathname" for the datagram */
    /* be sure to open the datagram first.  */
    unix_path = filesys::temp_directory_path ();
    unix_path += "/cubrid_tcp_setup_server" + std::to_string (getpid ());
    (void) ::unlink (unix_path.c_str ());

    /* setup unix domain socket and get thethe  path */
    if (!css_tcp_setup_server_datagram (unix_path.c_str (), &unix_socket))
      {
	er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_ERROR_DURING_SERVER_CONNECT, 1);

	return nullptr;
      }

    /* send unix path to open new unix connection to master */
    css_set_net_header (&header, DATA_TYPE, 0, request_id, unix_path.length () + 1, conn->get_tran_index (), conn->invalidate_snapshot, conn->db_error);
    /* clear the packet buffer */
    m_packet.clear ();
    /* register the packets */
    m_packet.push_for_send ({ reinterpret_cast<std::byte *> (&header), sizeof (NET_HEADER) });
    m_packet.push_for_send ({ reinterpret_cast<std::byte *> (const_cast<char *> (unix_path.c_str ())), unix_path.length () + 1 });
    /* send */
    er_log_debug (__FILE__, __LINE__, "[w] send unix path packet: start\n");
    if (__builtin_expect (!this->send (), 0))
      {
	(void) ::unlink (unix_path.c_str ());
	::close (unix_socket);
	er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_ERROR_DURING_SERVER_CONNECT, 1);

	return nullptr;
      }
    er_log_debug (__FILE__, __LINE__, "[w] send unix path packet: done\n");

    /* wait to be reqeusted to connect from master */
    if (!css_tcp_listen_server_datagram (unix_socket, &datagram_fd))
      {
	(void) ::unlink (unix_path.c_str ());
	::close (unix_socket);
	er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_ERROR_DURING_SERVER_CONNECT, 1);

	return nullptr;
      }

    /* remove origin */
    if (!m_events.remove_descriptor (conn->fd))
      {
	return nullptr;
      }

    /* only connected file descriptor is needed */
    (void) ::unlink (unix_path.c_str ());
    css_free_conn (conn);
    ::close (unix_socket);

    return css_make_conn (datagram_fd);
  }

  template <typename T>
  inline CSS_CONN_ENTRY *master_connector<T>::register_in_master (CSS_CONN_ENTRY *conn, std::string &server_name) noexcept
  {
    std::optional<int> request_id;
    int response;

    request_id = this->send_register_request (conn, server_name);
    if (!request_id)
      {
	return nullptr;
      }

    response = this->recv_register_response (conn);
    if (response < 0)
      {
	return nullptr;
      }

    er_log_debug (__FILE__, __LINE__, "cub_server received %d as response from master\n", response);

    switch (response)
      {
      case SERVER_ALREADY_EXISTS:
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_SERVER_ALREADY_EXISTS, 1, server_name);
	return nullptr;

      case SERVER_REQUEST_ACCEPTED:
	er_log_debug (__FILE__, __LINE__, "successfully connected to master\n");
	return this->switch_to_unix_socket (conn, *request_id);

      default:
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_ERROR_DURING_SERVER_CONNECT, 1, server_name);
	return nullptr;
      }

    /* impossible ! */
    assert_release (false);
    return nullptr;
  }
}

#endif
