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
#include "epoll.hpp"
#include "span.hpp"
#include "porting.h"

#include <tuple>
#include <cstdint>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <string>
#include <type_traits>

#define NEXT_STATE(c, x) do { \
    er_log_debug (__FILE__, __LINE__, "fd = %d, set state = %d\n", c->m_conn ? c->m_conn->fd : -1, state::x); \
    (c->m_state = state::x); \
} while (0)

namespace cubconn
{
  master_connector::context::context () :
    m_conn (nullptr),
    m_sendbuf (10),
    m_has_error (false)
  {
  }

  master_connector::context::~context ()
  {
    m_recvbuf.reset ();
    m_sendbuf.clear ();

    if (m_conn)
      {
	/* TODO: this may not be used */
      }
  }

  void master_connector::context::reset ()
  {
    m_recvbuf.reset ();
    m_sendbuf.clear ();

    m_state = state::SendInHandshake;
    m_has_error = false;

    if (m_conn)
      {
	/* TODO: this may not be used */
      }
  }

  bool master_connector::context::has_data_to_send ()
  {
    if (m_sendbuf.get_msghdr ().msg_iovlen)
      {
	return true;
      }

    return false;
  }

  master_connector::master_connector ()
  {
    m_context.reset ();
  }

  master_connector::~master_connector ()
  {
  }

  bool master_connector::attach (connection_pool &pool) noexcept
  {
    m_connection_pool = &pool;
    return true;
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

  inline bool master_connector::update_epoll_events (context *ctx)
  {
    std::uint32_t flags;

    flags = EPOLLIN | EPOLLRDHUP;
    if (ctx->has_data_to_send ())
      {
	flags |= EPOLLOUT;
      }

    if (!m_events.modify_descriptor (ctx->m_conn->fd, flags, ctx))
      {
	_er_log_debug (__FILE__, __LINE__, "master_connector->update_epoll_events: m_events->modify_descriptor failed: %s",
		       strerror (errno));
	return false;
      }

    return true;
  }

  inline master_connector::context *master_connector::make_context ()
  {
    context *ctx;

    ctx = new context;
    if (!ctx)
      {
	_er_log_debug (__FILE__, __LINE__, "memory allocation failed: %s", strerror (errno));
	assert_release (false);
      }
    ctx->reset ();

    return ctx;
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
	_er_log_debug (__FILE__, __LINE__, "master_connector->connect_to_master: failed to connect - error: %s",
		       strerror (errno));
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
	::close (fd);
	return false;
      }

    /* make new connection */
    conn = css_make_conn (fd);
    if (!conn)
      {
	_er_log_debug (__FILE__, __LINE__, "master_connector->connect: malloc failed: CSS_CONN_ENTRY");
	::close (fd);
	return false;
      }
    m_context.m_conn = conn;

    return true;
  }

  inline void master_connector::set_registrant (css_server_proc_register *proc_register,
      std::string &server_name) noexcept
  {
    char *p, *last;
    char **argv;

    memcpy (proc_register->server_name, server_name.c_str (), server_name.length () + 1);
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
    css_conn_entry *conn;

    conn = m_context.m_conn;
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
    request_id = css_get_request_id (conn);
    css_set_net_header (header[1], COMMAND_TYPE, SERVER_REQUEST_FROM_SERVER, request_id, sizeof (CSS_SERVER_PROC_REGISTER),
			conn->get_tran_index (), conn->invalidate_snapshot, conn->db_error);
    css_set_net_header (header[2], DATA_TYPE, 0, request_id, sizeof (CSS_SERVER_PROC_REGISTER), conn->get_tran_index (),
			conn->invalidate_snapshot, conn->db_error);
    /* register the packets */
    m_context.push_for_send ({ reinterpret_cast<std::byte *> (header[0]), sizeof (NET_HEADER) });
    m_context.push_for_send ({ reinterpret_cast<std::byte *> (header[1]), sizeof (NET_HEADER) });
    m_context.push_for_send ({ reinterpret_cast<std::byte *> (header[2]), sizeof (NET_HEADER) });
    m_context.push_for_send ({ reinterpret_cast<std::byte *> (registrant), sizeof (CSS_SERVER_PROC_REGISTER) });
    /* make the packets to msghdr */
    m_context.m_sendbuf.stamp_msghdr ();

    if (!m_events.add_descriptor (conn->fd, EPOLLIN | EPOLLOUT | EPOLLRDHUP, &m_context))
      {
	_er_log_debug (__FILE__, __LINE__, "master_connector->prepare_handshake: m_events->add_descriptor failed: %s",
		       strerror (errno));
	return false;
      }

    return true;
  }

  inline bool master_connector::prepare_switch_to_unix_socket (context *ctx) noexcept
  {
    NET_HEADER *header;
    css_conn_entry *conn;

    conn = ctx->m_conn;
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
    ctx->m_sendbuf.clear ();
    header = ctx->allocate<NET_HEADER> ();
    /* unix path to open new unix connection to master */
    css_set_net_header (header, DATA_TYPE, 0, conn->request_id, m_unixpath.length () + 1, conn->get_tran_index (),
			conn->invalidate_snapshot, conn->db_error);
    ctx->push_for_send ({ reinterpret_cast<std::byte *> (header), sizeof (NET_HEADER) });
    ctx->push_for_send ({ reinterpret_cast<std::byte *> (const_cast<char *> (m_unixpath.c_str ())), m_unixpath.length () + 1 });
    /* make the packets to msghdr */
    ctx->m_sendbuf.stamp_msghdr ();

    /* update the events */
    if (!this->update_epoll_events (ctx))
      {
	_er_log_debug (__FILE__, __LINE__, "master_connector->execute: update_epoll_events failed: %s", strerror (errno));
	return false;
      }
    return true;
  }

  inline bool master_connector::prepare_reply (context *ctx, int reason) noexcept
  {
    css_conn_entry *conn;
    NET_HEADER *header;
    int *reason_buffer;

    conn = ctx->m_conn;

    /* clear the packet buffer */
    ctx->m_sendbuf.clear ();
    header = ctx->allocate<NET_HEADER> ();
    reason_buffer = ctx->allocate<int> ();

    css_set_net_header (header, DATA_TYPE, 0, conn->request_id, sizeof (int), conn->get_tran_index (),
			conn->invalidate_snapshot, conn->db_error);
    *reinterpret_cast<int *> (reason_buffer) = htonl (reason);

    ctx->push_for_send ({ reinterpret_cast<std::byte *> (header), sizeof (NET_HEADER) });
    ctx->push_for_send ({ reinterpret_cast<std::byte *> (reason_buffer), sizeof (int) });

    /* make the packets to msghdr */
    ctx->m_sendbuf.stamp_msghdr ();

    if (!m_events.add_descriptor (ctx->m_conn->fd, EPOLLIN | EPOLLOUT | EPOLLRDHUP, ctx))
      {
	_er_log_debug (__FILE__, __LINE__,
		       "master_connector->prepare_reply_refuse_connection: m_events->add_descriptor failed: %s", strerror (errno));
	return false;
      }
    return true;
  }

  inline bool master_connector::prepare_reply_refuse_connection (context *ctx, int reason) noexcept
  {
    NET_HEADER *header[2];
    css_conn_entry *conn;
    std::aligned_storage_t<1024, 8> *error_buffer;
    int *reason_buffer;
    int error_length;

    conn = ctx->m_conn;

    /* clear the packet buffer */
    ctx->m_sendbuf.clear ();
    header[0] = ctx->allocate<NET_HEADER> ();
    header[1] = ctx->allocate<NET_HEADER> ();
    reason_buffer = ctx->allocate<int> ();
    error_buffer = ctx->allocate<std::aligned_storage_t<1024, 8>> ();

    /* set reason */
    error_length = 1024;

    css_set_net_header (header[0], DATA_TYPE, 0, conn->request_id, sizeof (int), conn->get_tran_index (),
			conn->invalidate_snapshot, conn->db_error);
    *reinterpret_cast<int *> (reason_buffer) = htonl (reason);

    er_get_area_error (reinterpret_cast<char *> (error_buffer), &error_length);
    css_set_net_header (header[1], ERROR_TYPE, 0, conn->request_id, error_length, conn->get_tran_index (),
			conn->invalidate_snapshot, conn->db_error);

    ctx->push_for_send ({ reinterpret_cast<std::byte *> (header[0]), sizeof (NET_HEADER) });
    ctx->push_for_send ({ reinterpret_cast<std::byte *> (reason_buffer), sizeof (int) });
    ctx->push_for_send ({ reinterpret_cast<std::byte *> (header[1]), sizeof (NET_HEADER) });
    ctx->push_for_send ({ reinterpret_cast<std::byte *> (error_buffer), static_cast<std::size_t> (error_length) });

    /* make the packets to msghdr */
    ctx->m_sendbuf.stamp_msghdr ();
    ctx->m_has_error = true;

    if (!m_events.add_descriptor (ctx->m_conn->fd, EPOLLIN | EPOLLOUT | EPOLLRDHUP, ctx))
      {
	_er_log_debug (__FILE__, __LINE__,
		       "master_connector->prepare_reply_refuse_connection: m_events->add_descriptor failed: %s", strerror (errno));
	return false;
      }

    er_clear ();
    return true;
  }

  inline bool master_connector::switch_to_unix_socket (context *ctx) noexcept
  {
    int datagram_fd;

    /* wait to be reqeusted to connect from master */
    if (!css_tcp_listen_server_datagram (m_unixsocket, &datagram_fd))
      {
	_er_log_debug (__FILE__, __LINE__, "master_connector->switch_to_unix_socket: css_tcp_listen_server_datagram failed: %s",
		       strerror (errno));

	(void) ::unlink (m_unixpath.c_str ());
	::close (m_unixsocket);
	er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_ERROR_DURING_SERVER_CONNECT, 1);
	return false;
      }

    /* remove original */
    if (!m_events.remove_descriptor (ctx->m_conn->fd))
      {
	_er_log_debug (__FILE__, __LINE__, "master_connector->switch_to_unix_socket: m_events->remove_descriptor failed: %s",
		       strerror (errno));
	return false;
      }

    /* only connected file descriptor is needed */
    (void) ::unlink (m_unixpath.c_str ());
    css_free_conn (ctx->m_conn);
    ::close (m_unixsocket);

    /* new connection */
    ctx->m_conn = css_make_conn (datagram_fd);

    /* make new socket non-blocking */
    assert (!this->m_events.is_nonblocking (datagram_fd));
    if (!this->make_nonblocking (datagram_fd))
      {
	_er_log_debug (__FILE__, __LINE__, "master_connector->switch_to_unix_socket: m_events->make_nonblocking failed: %s",
		       strerror (errno));
	return false;
      }

    if (!m_events.add_descriptor (ctx->m_conn->fd, EPOLLIN | EPOLLRDHUP, ctx))
      {
	_er_log_debug (__FILE__, __LINE__, "master_connector->switch_to_unix_socket: m_events->add_descriptor failed: %s",
		       strerror (errno));
	return false;
      }

    er_log_debug (__FILE__, __LINE__, "successfully switched to unix domain socket\n");

    return true;
  }

  inline result master_connector::handshake_from_master (context *ctx) noexcept
  {
    const int *buf;
    result status;
    int response;

    std::tie (status, buf) = buffered_socket::read_fixed_size<int> (ctx->m_conn->fd, ctx->m_recvbuf);
    if (status != result::Ok)
      {
	_er_log_debug (__FILE__, __LINE__, "master_connector->execute: read_fixed_size returned %d", status);
	return status;
      }

    response = ntohl (*buf);
    ctx->m_recvbuf.mark_consumed ();

    er_log_debug (__FILE__, __LINE__, "cub_server received %d as response from master\n", response);

    switch (response)
      {
      case SERVER_ALREADY_EXISTS:
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_SERVER_ALREADY_EXISTS, 1, "server name");
	return result::Error;

      case SERVER_REQUEST_ACCEPTED:
	er_log_debug (__FILE__, __LINE__, "successfully connected to master\n");
	if (!this->prepare_switch_to_unix_socket (ctx))
	  {
	    return result::Error;
	  }
	break;

      default:
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_ERROR_DURING_SERVER_CONNECT, 1, "server name");
	return result::Error;
      }

    return result::Ok;
  }

  inline result master_connector::request_new_client (context *ctx) noexcept
  {
    context *new_ctx;
    CSS_CONN_ENTRY *conn;
    unsigned short request_id;
    SOCKET new_fd;

    /* receive new socket descriptor from the master */
    new_fd = css_open_new_socket_from_master (ctx->m_conn->fd, &request_id);
    _er_log_debug (__FILE__, __LINE__, "master_connector->request_new_client: unpack new socket: %d\n", new_fd);
    if (IS_INVALID_SOCKET (new_fd))
      {
	_er_log_debug (__FILE__, __LINE__, "master_connector->request_new_client: css_open_new_socket_from_master failed");
	return result::Reset;
      }

    if (!this->make_nonblocking (new_fd))
      {
	_er_log_debug (__FILE__, __LINE__, "master_connector->connect: request_new_client failed - error: %s",
		       strerror (errno));
	::close (new_fd);
	return result::Error;
      }

    /* make new context and conn */
    new_ctx = make_context ();

    /* check */
    if (prm_get_bool_value (PRM_ID_ACCESS_IP_CONTROL) == true && css_check_accessibility (new_fd) != NO_ERROR)
      {
	new_ctx->m_conn = new css_conn_entry;
	css_initialize_conn (new_ctx->m_conn, new_fd);
	new_ctx->m_conn->request_id = request_id;

	if (!this->prepare_reply_refuse_connection (new_ctx, SERVER_INACCESSIBLE_IP))
	  {
	    return result::Error;
	  }

	NEXT_STATE (new_ctx, SendReplyToClient);
	NEXT_STATE (ctx, RecvRequestType);
	return result::RefuseConnection;
      }

    conn = css_make_conn (new_fd);
    if (conn == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_CLIENTS_EXCEEDED, 1, NUM_NORMAL_TRANS);

	new_ctx->m_conn = new css_conn_entry;
	css_initialize_conn (new_ctx->m_conn, new_fd);
	new_ctx->m_conn->request_id = request_id;

	if (!this->prepare_reply_refuse_connection (new_ctx, SERVER_CLIENTS_EXCEEDED))
	  {
	    return result::Error;
	  }

	NEXT_STATE (new_ctx, SendReplyToClient);
	NEXT_STATE (ctx, RecvRequestType);
	return result::RefuseConnection;
      }

    new_ctx->m_conn = conn;
    new_ctx->m_conn->request_id = request_id;
    if (!this->prepare_reply (new_ctx, SERVER_CONNECTED))
      {
	return result::Error;
      }

    NEXT_STATE (new_ctx, SendReplyToClient);
    NEXT_STATE (ctx, RecvRequestType);
    return result::Ok;
  }

  inline result master_connector::handle_request (context *ctx) noexcept
  {
    const int *buf;
    result status;
    int request;

    std::tie (status, buf) = buffered_socket::read_fixed_size<int> (ctx->m_conn->fd, ctx->m_recvbuf);
    if (status != result::Ok)
      {
	_er_log_debug (__FILE__, __LINE__, "master_connector->handle_request: read_fixed_size returned %d", status);
	return status;
      }

    request = ntohl (*buf);
    ctx->m_recvbuf.mark_consumed ();

    er_log_debug (__FILE__, __LINE__, "cub_server received %d as request from master\n", request);

    switch (request)
      {
      case SERVER_START_NEW_CLIENT:
	NEXT_STATE (ctx, RecvNewClient);
	break;

      case SERVER_START_SHUTDOWN:
	/* nothing here */
	NEXT_STATE (ctx, RecvRequestType);
	break;

      case SERVER_STOP_SHUTDOWN:
      case SERVER_SHUTDOWN_IMMEDIATE:
      case SERVER_START_TRACING:
      case SERVER_STOP_TRACING:
      case SERVER_HALT_EXECUTION:
      case SERVER_RESUME_EXECUTION:
      case SERVER_REGISTER_HA_PROCESS:
	NEXT_STATE (ctx, RecvRequestType);
	break;

      case SERVER_GET_HA_MODE:
	/* TODO: css_process_get_server_ha_mode_request (master_fd); */
	NEXT_STATE (ctx, RecvRequestType);
	break;

      case SERVER_CHANGE_HA_MODE:
	/* TODO: css_process_change_server_ha_mode_request (master_fd); */
	NEXT_STATE (ctx, RecvRequestType);
	break;

      case SERVER_GET_EOF:
	/* TODO: SendLogEof (master_fd); */
	NEXT_STATE (ctx, RecvRequestType);
	break;

      default:
	er_log_debug (__FILE__, __LINE__, "cub_server received unexpected request: %d\n", request);
	return result::Error;
      }

    return result::Ok;
  }

  inline bool master_connector::handle_master_reception (context *ctx) noexcept
  {
    result status = result::Ok;

    switch (ctx->m_state)
      {
      case state::RecvInHandshake:
	status = this->handshake_from_master (ctx);
	NEXT_STATE (ctx, SwitchToUnixSocket);
	break;

      case state::RecvRequestType:
	status = this->handle_request (ctx);
	/* next state have already been set in handle_request. */
	break;

      case state::RecvNewClient:
	status = this->request_new_client (ctx);
	/* next state have already been set in request_new_client. */
	break;

      case state::SendInHandshake:
      case state::SwitchToUnixSocket:
      case state::SendReplyToClient:
	/* these will be handled in handle_master_transmission */
	break;

      default:
	_er_log_debug (__FILE__, __LINE__, "master_connector->handle_master_connection failed: m_context->state: %d",
		       ctx->m_state);
	assert_release (false);
	break;
      }

    /* Is there an error */
    if (status == result::Reset)
      {
	_er_log_debug (__FILE__, __LINE__, "master_connector->handle_master_transmission: protocol is messed up somewhere");
	ctx->m_recvbuf.reset ();
	ctx->m_sendbuf.clear ();
	NEXT_STATE (ctx, RecvRequestType);
      }
    else if (status == result::PeerReset)
      {
	/* TODO */
	_er_log_debug (__FILE__, __LINE__, "master_connector->handle_master_connection: reset by peer");
      }
    else if (status == result::Error)
      {
	_er_log_debug (__FILE__, __LINE__, "master_connector->handle_master_connection: failed");
	return false;
      }

    return true;
  }

  inline bool master_connector::sent_reply_to_client (context *ctx) noexcept
  {
    _er_log_debug (__FILE__, __LINE__, "master_connector->sent_reply_to_client: remove fd = %d\n", ctx->m_conn->fd);
    if (!m_events.remove_descriptor (ctx->m_conn->fd))
      {
	_er_log_debug (__FILE__, __LINE__, "master_connector->sent_reply_to_client: m_events->remove_descriptor failed: %s",
		       strerror (errno));
	return false;
      }

    if (!ctx->m_has_error)
      {
	css_insert_into_active_conn_list (ctx->m_conn);

	ctx->m_conn->request_id = 0;
	m_connection_pool->dispatch (ctx->m_conn);
      }
    else
      {
	/* In error context, this conn entry has been temporarily allocated */
	delete ctx->m_conn;
      }

    ctx->m_conn = nullptr;
    delete ctx;

    return true;
  }

  inline bool master_connector::handle_master_transmission (context *ctx) noexcept
  {
    assert (ctx->m_state != state::RecvInHandshake &&
	    ctx->m_state != state::RecvRequestType &&
	    ctx->m_state != state::RecvNewClient);
    assert (ctx && ctx->m_conn);

    if (!ctx->has_data_to_send ())
      {
	/* no data to send */
	return true;
      }

    if (!buffered_socket::send_partial (ctx->m_conn->fd, ctx->m_sendbuf))
      {
	/* pending */
	return true;
      }
    /* fully send */
    _er_log_debug (__FILE__, __LINE__, "master_connector->handle_master_transmission: fully sent the data to fd = %d\n",
		   ctx->m_conn->fd);

    /* move to next state */
    switch (ctx->m_state)
      {
      case state::SendInHandshake:
	NEXT_STATE (ctx, RecvInHandshake);
	break;

      case state::SwitchToUnixSocket:
	/* switching */
	if (!this->switch_to_unix_socket (ctx))
	  {
	    _er_log_debug (__FILE__, __LINE__,
			   "master_connector->handle_master_transmission: master->switch_to_unix_socket failed");
	    return false;
	  }
	NEXT_STATE (ctx, RecvRequestType);
	break;

      case state::SendReplyToClient:
	if (!this->sent_reply_to_client (ctx))
	  {
	    _er_log_debug (__FILE__, __LINE__, "master_connector->handle_master_transmission: sent_reply_to_client failed");
	    return false;
	  }
	/* return here to avoid segfault */
	return true;

      default:
	/* impossible ! */
	assert_release (false);
	break;
      }

    /* update */
    if (!ctx->has_data_to_send () && !this->update_epoll_events (ctx))
      {
	_er_log_debug (__FILE__, __LINE__, "master_connector->handle_master_transmission: update_epoll_events failed: %s",
		       strerror (errno));
	return false;
      }
    return true;
  }

  inline bool master_connector::execute () noexcept
  {
    std::array<epoll_event, 32> events;
    context *ctx;
    int nfds, i;

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

	if (nfds == 0)
	  {
	    /* TODO: maybe heartbeat ? */
	  }

	assert (nfds > 0);

	for (i = 0; i < nfds; i++)
	  {
	    assert (events[i].data.ptr);

	    ctx = reinterpret_cast<context *> (events[i].data.ptr);
	    if (events[i].events & EPOLLERR)
	      {
		_er_log_debug (__FILE__, __LINE__, "master_connector->execute: master connection closed: %s", strerror (errno));
		/* TODO: reestablish the connection */
		return false;
	      }
	    if (events[i].events & EPOLLIN)
	      {
		if (!this->handle_master_reception (ctx))
		  {
		    _er_log_debug (__FILE__, __LINE__, "master_connector->execute: handle_master_reception failed: %d\n", 0);
		    return false;
		  }
	      }
	    if (events[i].events & EPOLLOUT)
	      {
		if (!this->handle_master_transmission (ctx))
		  {
		    _er_log_debug (__FILE__, __LINE__, "master_connector->execute: handle_master_transmission failed");
		    return false;
		  }
	      }
	    if (events[i].events & (EPOLLHUP | EPOLLRDHUP))
	      {
		_er_log_debug (__FILE__, __LINE__, "master_connector->execute: master connection closed: %s", strerror (errno));
		/* TODO: reestablish the connection */
		return false;
	      }
	  }
      }
    return true;
  }
}
