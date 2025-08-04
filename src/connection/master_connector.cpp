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
 * master_connector.cpp
 */

#include "connection_globals.h"
#include "system_parameter.h"
#include "log_common_impl.h"
#include "error_manager.h"
#include "master_connector.hpp"
#include "server_support.h"
#include "filesys_temp.hpp"
#include "connection_sr.h"
#include "tcp.h"
#include "buffer.hpp"
#include "packet_buffer.hpp"
#include "DMRB_SPSC.hpp"
#include "epoll.hpp"
#include "span.hpp"
#include "porting.h"

#include <tuple>
#include <cstdint>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <optional>
#include <string>
#include <type_traits>

namespace cubconn
{
  master_connector::master_context::master_context () :
      m_sendbuf (16)
    {
    }

  master_connector::master_context::~master_context ()
    {
      m_recvbuf.reset ();
      m_sendbuf.clear ();
    }

  void master_connector::master_context::reset ()
    {
      m_recvbuf.reset ();
      m_sendbuf.clear ();

      state = master_state::SendInHandshake;
    }

  bool master_connector::master_context::has_data_to_send ()
    {
      if (m_sendbuf.get_msghdr ().msg_iovlen)
	{
	  return true;
	}

      return false;
    }

  css_conn_entry *master_connector::get_connection () noexcept
    {
      return m_conn;
    }

  master_connector::master_connector ()
    {
    }

  master_connector::~master_connector ()
    {
    }

  bool master_connector::run (int port, std::string &server_name) noexcept
    {
      if (!this->connect (port))
	{
	  _er_log_debug (__FILE__, __LINE__, "master_connector->run: connect failed");
	  return false;
	}

      if (!this->prepare_handshake (server_name))
	{
	  _er_log_debug (__FILE__, __LINE__, "master_connector->run: prepare_handshake failed");
	  return false;
	}

      if (!this->execute ())
	{
	  _er_log_debug (__FILE__, __LINE__, "master_connector->run: execute failed");
	  return false;
	}

      return true;
    }

  inline bool master_connector::make_nonblocking (int fd) noexcept
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

  inline bool master_connector::update_epoll_events (int fd)
    {
      std::uint32_t flags;

      flags = EPOLLIN;
      if (m_context.has_data_to_send ())
	{
	  flags |= EPOLLOUT;
	}

      if (!m_events.modify_descriptor (fd, flags, m_conn))
	{
	  _er_log_debug (__FILE__, __LINE__, "master_connector->update_epoll_events: m_events->modify_descriptor failed: %s", strerror (errno));
	  return false;
	}

      return true;
    }

  inline int master_connector::connect_to_master (int port) noexcept
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
	_er_log_debug (__FILE__, __LINE__, "master_connector->connect_to_master: failed to connect - error: %s", strerror (errno));
	return -1;
      }

    return fd;
  }

  bool master_connector::connect (int port) noexcept
    {
      css_conn_entry *conn;
      SOCKET fd;

      fd = this->connect_to_master (port);
      if (IS_INVALID_SOCKET (fd))
	{
	  _er_log_debug (__FILE__, __LINE__, "master_connector->connect: failed to connect - error: %s", strerror (errno));
	  return false;
	}

      assert (!this->m_events.is_nonblocking (fd));

      if (!this->make_nonblocking (fd))
	{
	  _er_log_debug (__FILE__, __LINE__, "master_connector->connect: make_nonblocking failed - error: %s", strerror (errno));
	  close (fd);
	  return false;
	}
      
      /* make new connection */
      conn = css_make_conn (fd);
      if (!conn)
	{
	  _er_log_debug (__FILE__, __LINE__, "master_connector->connect: malloc failed: CSS_CONN_ENTRY");
	  close (fd);
	  return false;
	}
      /* will never be used */
      delete conn->recvbuf;
      conn->recvbuf = nullptr;
      delete conn->sendbuf;
      conn->sendbuf = nullptr;

      m_conn = conn;

      return true;
    }

  inline void master_connector::set_registrant (css_server_proc_register *proc_register,
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

  inline bool master_connector::prepare_handshake (std::string &server_name) noexcept
    {
      NET_HEADER *header[3];
      /* header[0]: magic number packet */
      /* header[1]: command header packet */
      /* header[2]: data header for registrant packet */
      CSS_SERVER_PROC_REGISTER *registrant;
      unsigned short request_id;

      /* clear the packet buffer */
      m_context.m_sendbuf.clear ();
      header[0] = m_context.allocate<NET_HEADER> ();
      header[1] = m_context.allocate<NET_HEADER> ();
      header[2] = m_context.allocate<NET_HEADER> ();
      registrant = m_context.allocate<CSS_SERVER_PROC_REGISTER> ();
      /* cub_server magic number to be delivered to cub_master */
      std::memcpy ((char *) header[0], css_Net_magic, sizeof (css_Net_magic));
      /* make the name pakcet to register this server to cub_master */
      this->set_registrant (registrant, server_name);
      /* headers */
      request_id = css_get_request_id (m_conn);
      css_set_net_header (header[1], COMMAND_TYPE, SERVER_REQUEST_FROM_SERVER, request_id, sizeof (CSS_SERVER_PROC_REGISTER),
			  m_conn->get_tran_index (), m_conn->invalidate_snapshot, m_conn->db_error);
      css_set_net_header (header[2], DATA_TYPE, 0, request_id, sizeof (CSS_SERVER_PROC_REGISTER), m_conn->get_tran_index (),
			  m_conn->invalidate_snapshot, m_conn->db_error);
      /* register the packets */
      m_context.push_for_send ({ reinterpret_cast<std::byte *> (header[0]), sizeof (NET_HEADER) });
      m_context.push_for_send ({ reinterpret_cast<std::byte *> (header[1]), sizeof (NET_HEADER) });
      m_context.push_for_send ({ reinterpret_cast<std::byte *> (header[2]), sizeof (NET_HEADER) });
      m_context.push_for_send ({ reinterpret_cast<std::byte *> (registrant), sizeof (CSS_SERVER_PROC_REGISTER) });
      /* make the packets to msghdr */
      m_context.m_sendbuf.stamp_msghdr ();
       
      if (!m_events.add_descriptor (m_conn->fd, EPOLLIN | EPOLLOUT, m_conn))
	{
	  _er_log_debug (__FILE__, __LINE__, "master_connector->prepare_handshake: m_events->add_descriptor failed: %s", strerror (errno));
	  return false;
	} 

      return true;
    }

  inline bool master_connector::switch_to_unix_socket () noexcept
    {
      int datagram_fd;

      /* wait to be reqeusted to connect from master */
      if (!css_tcp_listen_server_datagram (m_unixsocket, &datagram_fd))
	{
	  _er_log_debug (__FILE__, __LINE__, "master_connector->switch_to_unix_socket: css_tcp_listen_server_datagram failed: %s", strerror (errno));

	  (void) ::unlink (m_unixpath.c_str ());
	  ::close (m_unixsocket);
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_ERROR_DURING_SERVER_CONNECT, 1);
	  return false;
	}

      /* remove original */
      if (!m_events.remove_descriptor (m_conn->fd))
	{
	  _er_log_debug (__FILE__, __LINE__, "master_connector->switch_to_unix_socket: m_events->remove_descriptor failed: %s", strerror (errno));
	  return false;
	}

      /* only connected file descriptor is needed */
      (void) ::unlink (m_unixpath.c_str ());
      css_free_conn (m_conn);
      ::close (m_unixsocket);

      /* new connection */
      m_conn = css_make_conn (datagram_fd);
      /* will never be used */
      delete m_conn->recvbuf;
      m_conn->recvbuf = nullptr;
      delete m_conn->sendbuf;
      m_conn->sendbuf = nullptr;

      /* make new socket non-blocking */
      assert (!this->m_events.is_nonblocking (datagram_fd));
      if (!this->make_nonblocking (datagram_fd))
	{
	  _er_log_debug (__FILE__, __LINE__, "master_connector->switch_to_unix_socket: m_events->make_nonblocking failed: %s", strerror (errno));
	  return false;
	}
      
      if (!m_events.add_descriptor (m_conn->fd, EPOLLIN, m_conn))
	{
	  _er_log_debug (__FILE__, __LINE__, "master_connector->switch_to_unix_socket: m_events->add_descriptor failed: %s", strerror (errno));
	  return false;
	} 

      er_log_debug (__FILE__, __LINE__, "successfully switched to unix domain socket\n");

      return true;
    }

  inline bool master_connector::prepare_switch_to_unix_socket () noexcept
    {
      NET_HEADER *header;

      /* send the pathname for the datagram */
      /* be sure to open the datagram first.  */
      m_unixpath = filesys::temp_directory_path ();
      m_unixpath += "/cubrid_tcp_setup_server" + std::to_string (getpid ());
      (void) ::unlink (m_unixpath.c_str ());

      /* setup unix domain socket and get the path */
      if (!css_tcp_setup_server_datagram (m_unixpath.c_str (), &m_unixsocket))
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_ERROR_DURING_SERVER_CONNECT, 1);
	  return false;
	}

      /* clear the packet buffer */
      m_context.m_sendbuf.clear ();
      header = m_context.allocate<NET_HEADER> ();
      /* unix path to open new unix connection to master */
      css_set_net_header (header, DATA_TYPE, 0, m_conn->request_id, m_unixpath.length () + 1, m_conn->get_tran_index (), m_conn->invalidate_snapshot, m_conn->db_error); 
      m_context.push_for_send ({ reinterpret_cast<std::byte *> (header), sizeof (NET_HEADER) });
      m_context.push_for_send ({ reinterpret_cast<std::byte *> (const_cast<char *> (m_unixpath.c_str ())), m_unixpath.length () + 1 });
      /* make the packets to msghdr */
      m_context.m_sendbuf.stamp_msghdr ();

      /* update the events */
      if (!this->update_epoll_events (m_conn->fd))
	{
	  _er_log_debug (__FILE__, __LINE__, "master_connector->execute: update_epoll_events failed: %s", strerror (errno));
	  return false;
	}
      return true;
    }

  inline master_connector::transfer_result master_connector::handshake_from_master () noexcept
    {
      const int *buf;
      int response;

      buf = buffered_socket::read_fixed_size<int> (m_conn->fd, m_context.m_recvbuf);
      if (!buf)
	{
	  _er_log_debug (__FILE__, __LINE__, "master_connector->execute: partial recv from state %d", m_context.state);
	  return transfer_result::Pending;
	}
      
      response = ntohl (*buf);
      m_context.m_recvbuf.mark_consumed ();

      er_log_debug (__FILE__, __LINE__, "cub_server received %d as response from master\n", response);

      switch (response)
	{
	case SERVER_ALREADY_EXISTS:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_SERVER_ALREADY_EXISTS, 1, "server name");
	  return transfer_result::Error;

	case SERVER_REQUEST_ACCEPTED:
	  er_log_debug (__FILE__, __LINE__, "successfully connected to master\n");
	  if (!this->prepare_switch_to_unix_socket ())
	    {
	      return transfer_result::Error;
	    }
	  break;

	default:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_ERROR_DURING_SERVER_CONNECT, 1, "server name");
	  return transfer_result::Error;
	}
      return transfer_result::Ok;
    }

  inline bool master_connector::handle_master_reception () noexcept
    {
      switch (m_context.state)
	{
	case master_state::RecvInHandshake:
	  if (this->handshake_from_master () == transfer_result::Error)
	    {
	      _er_log_debug (__FILE__, __LINE__, "master_connector->handle_master_connection: handshake_from_master failed");
	      return false;
	    }
	  m_context.state = master_state::SwitchToUnixSocket;
	  break;

	case master_state::SendInHandshake:
	case master_state::SwitchToUnixSocket:
	  /* send only */
	  break;

	default:
	  _er_log_debug (__FILE__, __LINE__, "master_connector->handle_master_connection failed: m_context->state: %d", m_context.state);
	  assert_release (false);
	  break;
	}

      return true;
    }

  inline bool master_connector::handle_master_transmission () noexcept
    {
      if (!m_context.has_data_to_send ())
	{
	  /* no data to send */
	  return true;
	}

      if (!buffered_socket::send_partial (m_conn->fd, m_context.m_sendbuf))
	{
	  /* pending */
	  return true;
	}
      /* fully send */

      /* move to next state */
      switch (m_context.state)
	{
	case master_state::SendInHandshake:
	  m_context.state = master_state::RecvInHandshake;
	  break;

	case master_state::SwitchToUnixSocket:
	  /* switching */
	  this->switch_to_unix_socket ();
	  m_context.state = master_state::RecvRequestType;
	  return false;
	  break;

	default:
	  /* hmm ... */
	  break;
	}

      /* update */
      if (!this->update_epoll_events (m_conn->fd))
	{
	  _er_log_debug (__FILE__, __LINE__, "master_connector->execute: update_epoll_events failed: %s", strerror (errno));
	  return false;
	}
      return true;
    }

  inline bool master_connector::execute () noexcept
    {
      std::array<epoll_event, 2> events;
      int nfds;

      while (true)
	{
	  nfds = m_events.wait (events.data (), events.size (), TIMEOUT_INFINITE);
	  if (nfds < 0)
	    {
	      if (errno == EINTR)
		{
		  continue;
		}
	      _er_log_debug (__FILE__, __LINE__, "master_connector->execute: m_events->wait failed: %s", strerror (errno));
	      assert_release (false);
            }
	  
	  assert (nfds == 1);
	  assert (events[0].data.ptr && events[0].data.ptr == m_conn);
	  
	  if (events[0].events & EPOLLIN)
	    {
	      if (!this->handle_master_reception ())
		{
		  _er_log_debug (__FILE__, __LINE__, "master_connector->execute: handle_master_reception failed");
		  return false;
		}
	    }
	  if (events[0].events & EPOLLOUT)
	    {
	      if (!this->handle_master_transmission ())
		{
		  _er_log_debug (__FILE__, __LINE__, "master_connector->execute: handle_master_transmission failed");
		  return false;
		} 
	    }
	  if (events[0].events & (EPOLLHUP | EPOLLERR))
	    {
	      _er_log_debug (__FILE__, __LINE__, "master_connector->execute: master connection closed: %s", strerror (errno));
	      return false;
	    }
	}

      return true;
    }
}
