/*
 *
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
#include "object_representation.h"
#include "log_common_impl.h"
#include "log_lsa.hpp"
#include "log_manager.h"
#include "heartbeat.h"
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
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <string>
#include <type_traits>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if 0
#define er_log_conn(...) er_log_debug (__VA_ARGS__)
#else
#define er_log_conn(...)
#endif

#define NEXT_STATE(c, x) do { \
    er_log_conn (__FILE__, __LINE__, "fd = %d, set state = %d\n", c->m_conn ? c->m_conn->fd : -1, state::x); \
    (c->m_state = state::x); \
} while (0)

namespace cubconn::master
{
  /* Master connector uses Main_entry_p (TT_MASTER) instead of claiming a separate entry. */
  /* It is still registered here for consistency with other connection components.	  */
  REGISTER_CONNECTION (master_connector, 0);

  connector::connector () :
    m_stop (false),
    m_entry (nullptr),
    m_master_state (master_state::CLOSED),
    m_connection_pool (nullptr)
  {
    context *ctx;

    m_eventfd = eventfd (0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (m_eventfd == -1)
      {
	er_log_conn (__FILE__, __LINE__, "master::connector: failed to create eventfd\n");
	assert_release (false);
      }
    ctx = new context ();
    if (!ctx)
      {
	er_log_conn (__FILE__, __LINE__, "master::connector: failed to allocate memory\n");
	assert_release (false);
      }
    ctx->m_conn = reinterpret_cast<css_conn_entry *> (new int { m_eventfd });
    if (!ctx->m_conn)
      {
	er_log_conn (__FILE__, __LINE__, "master::connector: failed to allocate memory\n");
	assert_release (false);
      }
    if (!m_events.add_descriptor (m_eventfd, EPOLLIN, ctx))
      {
	er_log_conn (__FILE__, __LINE__, "master::connector: add_descriptor failed\n");
	delete ctx->m_conn;
	assert_release (false);
      }
    m_context.reset ();
  }

  connector::~connector ()
  {
  }

  void connector::stop () noexcept
  {
    std::uint64_t u;
    ssize_t bytes;

    /* stop */
    m_stop = true;

    /* and wakeup */
    u = 1;
    while (true)
      {
	bytes = ::write (m_eventfd, &u, sizeof (u));
	if (bytes == sizeof (u))
	  {
	    break;
	  }

	if (bytes == 0 || (bytes > 0 && static_cast<unsigned long> (bytes) < sizeof (u)))
	  {
	    assert_release (false);
	  }

	assert (bytes < 0);

	if (errno == EINTR)
	  {
	    continue;
	  }
	if (errno == EAGAIN)
	  {
	    break;
	  }
	assert_release (false);
      }
  }

  bool connector::attach (cubthread::entry &entry) noexcept
  {
    m_entry = &entry;
    return true;
  }

  bool connector::attach (connection::pool &pool) noexcept
  {
    m_connection_pool = &pool;
    return true;
  }

  bool connector::run (int port, std::string &server_name) noexcept
  {
    m_master_port = port;
    m_server_name = server_name;
    if (!this->connect (port))
      {
	er_log_conn (__FILE__, __LINE__, "master::connector->run: connect failed");
	return false;
      }

    if (!this->prepare_handshake (server_name))
      {
	er_log_conn (__FILE__, __LINE__, "master::connector->run: prepare_handshake failed");
	return false;
      }

    if (!this->execute ())
      {
	er_log_conn (__FILE__, __LINE__, "master::connector->run: execute failed");
	return false;
      }

    return true;
  }

  inline bool connector::make_nonblocking (int fd) noexcept
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

  inline bool connector::opt_socket (int fd) noexcept
  {
    int value;

    /* setsockopt with IPPROTO_TCP can fail if the fd is not a TCP socket (e.g. unix domain) */

    value = static_cast<int> (prm_get_bool_value (PRM_ID_TCP_KEEPALIVE));
    setsockopt (fd, SOL_SOCKET, SO_KEEPALIVE, &value, sizeof (value));

    value = prm_get_integer_value (PRM_ID_TCP_KEEPALIVE_IDLE);
    setsockopt (fd, IPPROTO_TCP, TCP_KEEPIDLE, &value, sizeof (value));

    value = prm_get_integer_value (PRM_ID_TCP_KEEPALIVE_INTERVAL);
    setsockopt (fd, IPPROTO_TCP, TCP_KEEPINTVL, &value, sizeof (value));

    value = prm_get_integer_value (PRM_ID_TCP_KEEPALIVE_COUNT);
    setsockopt (fd, IPPROTO_TCP, TCP_KEEPCNT, &value, sizeof (value));

    return true;
  }

  inline bool connector::dispose_connection (context *ctx)
  {
    /* remove the fd which is reset by peer */
    if (!m_events.remove_descriptor (ctx->m_conn->fd))
      {
	er_log_conn (__FILE__, __LINE__, "master::connector->dispose_connection: m_events->remove_descriptor failed: %s",
		     strerror (errno));
	return false;
      }
    /* if this is an error (refuse) context, the connection entry was temporarily allocated. */
    /* DO NOT PASS it to css_free_conn (which expects a pool entry). */
    if (ctx->m_has_error)
      {
	if (!IS_INVALID_SOCKET (ctx->m_conn->fd))
	  {
	    css_shutdown_socket (ctx->m_conn->fd);
	    ctx->m_conn->fd = INVALID_SOCKET;
	  }
	delete ctx->m_conn;
      }
    else
      {
	css_prepare_shutdown_conn (ctx->m_conn);
	css_free_conn (ctx->m_conn);
      }
    delete ctx;

    return true;
  }

  inline bool connector::update_epoll_events (context *ctx)
  {
    std::uint32_t flags;

    flags = EPOLLIN | EPOLLRDHUP;
    if (ctx->has_data_to_send ())
      {
	flags |= EPOLLOUT;
      }

    if (!m_events.modify_descriptor (ctx->m_conn->fd, flags, ctx))
      {
	er_log_conn (__FILE__, __LINE__, "master::connector->update_epoll_events: m_events->modify_descriptor failed: %s",
		     strerror (errno));
	return false;
      }

    return true;
  }

  inline context *connector::make_context ()
  {
    context *ctx;

    ctx = new context;
    if (!ctx)
      {
	er_log_conn (__FILE__, __LINE__, "memory allocation failed: %s", strerror (errno));
	assert_release (false);
      }
    ctx->reset ();

    return ctx;
  }

  inline int connector::connect_to_master (int port) noexcept
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
	er_log_conn (__FILE__, __LINE__, "master::connector->connect_to_master: failed to connect - error: %s",
		     strerror (errno));
	return -1;
      }

    return fd;
  }

  bool connector::connect (int port) noexcept
  {
    css_conn_entry *conn;
    SOCKET fd;

    fd = this->connect_to_master (port);
    if (IS_INVALID_SOCKET (fd))
      {
	er_log_conn (__FILE__, __LINE__, "master::connector->connect: failed to connect - error: %s", strerror (errno));
	return false;
      }

    assert (!this->m_events.is_nonblocking (fd));

    if (!this->make_nonblocking (fd))
      {
	er_log_conn (__FILE__, __LINE__, "master::connector->connect: make_nonblocking failed - error: %s", strerror (errno));
	::close (fd);
	return false;
      }

    /* make new connection */
    conn = css_make_conn (fd);
    if (!conn)
      {
	er_log_conn (__FILE__, __LINE__, "master::connector->connect: css_make_conn failed: can't recover this");
	::close (fd);
	return false;
      }
    m_context.m_conn = conn;
    m_master_state = master_state::CONNECTED;

    return true;
  }

  inline void connector::set_registrant (css_server_proc_register *proc_register,
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

  inline bool connector::prepare_handshake (std::string &server_name) noexcept
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
	er_log_conn (__FILE__, __LINE__, "master::connector->prepare_handshake: m_events->add_descriptor failed: %s",
		     strerror (errno));
	return false;
      }
    m_master_state = master_state::WAIT_RESPONSE;

    return true;
  }

  inline bool connector::prepare_switch_to_unix_socket (context *ctx) noexcept
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
	er_log_conn (__FILE__, __LINE__, "master::connector->execute: update_epoll_events failed: %s", strerror (errno));
	return false;
      }
    return true;
  }

  inline bool connector::prepare_reply (context *ctx, int reason) noexcept
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

    return true;
  }

  inline bool connector::prepare_reply_refuse_connection (context *ctx, int reason) noexcept
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

    conn->db_error = er_errid ();
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
	er_log_conn (__FILE__, __LINE__,
		     "master::connector->prepare_reply_refuse_connection: m_events->add_descriptor failed: %s", strerror (errno));
	return false;
      }

    er_clear ();
    return true;
  }

  inline bool connector::prepare_heartbeat_send_request (context *ctx, CSS_SERVER_REQUEST command) noexcept
  {
    int *response;

    /* clear the packet buffer */
    ctx->m_sendbuf.clear ();
    response = ctx->allocate<int> ();
    *response = htonl (command);

    ctx->push ({ reinterpret_cast<std::byte *> (response), sizeof (int) });

    /* make the packets to msghdr */
    ctx->m_sendbuf.stamp_msghdr ();

    /* update the events */
    if (!this->update_epoll_events (ctx))
      {
	er_log_conn (__FILE__, __LINE__, "master::connector->prepare_heartbeat_send_request: update_epoll_events failed: %s",
		     strerror (errno));
	return false;
      }
    return true;
  }

  inline bool connector::prepare_heartbeat_send_request_with_data (context *ctx, CSS_SERVER_REQUEST command,
      std::byte *data, std::size_t size) noexcept
  {
#define BODY_SIZE 2048
    std::aligned_storage_t<BODY_SIZE, 8> *body;
    int *response;

    /* clear the packet buffer */
    ctx->m_sendbuf.clear ();

    response = ctx->allocate<int> ();
    *response = htonl (command);

    /* this must be fixed if you wanna store the data bigger than BODY_SIZE-bytes */
    /* you can generalize size using template */
    assert_release (size <= BODY_SIZE);
    body = ctx->allocate<std::aligned_storage_t<BODY_SIZE, 8>> ();
    std::memcpy (body, data, size);

    ctx->push ({ reinterpret_cast<std::byte *> (response), sizeof (int) });
    ctx->push ({ reinterpret_cast<std::byte *> (body), size });

    /* make the packets to msghdr */
    ctx->m_sendbuf.stamp_msghdr ();

#undef BODY_SIZE

    /* update the events */
    if (!this->update_epoll_events (ctx))
      {
	er_log_conn (__FILE__, __LINE__,
		     "master::connector->prepare_heartbeat_send_request_with_data: update_epoll_events failed: %s", strerror (errno));
	return false;
      }
    return true;
  }

  inline bool connector::prepare_heartbeat_register (context *ctx) noexcept
  {
    hbp_proc_register *hbp_register;

    hbp_register = hb_make_set_hbp_register (HB_PTYPE_SERVER);
    if (hbp_register == NULL)
      {
	er_log_conn (ARG_FILE_LINE, "master::connector->hb_make_set_hbp_register: hbp_register failed. \n");
	return false;
      }

    if (!this->prepare_heartbeat_send_request_with_data (ctx, SERVER_REGISTER_HA_PROCESS,
	reinterpret_cast<std::byte *> (hbp_register), sizeof (*hbp_register)))
      {
	free_and_init (hbp_register);
	return false;
      }
    free_and_init (hbp_register);
    return true;
  }

  inline bool connector::prepare_heartbeat_ha_mode (context *ctx) noexcept
  {
    int *response;

    /* clear the packet buffer */
    ctx->m_sendbuf.clear ();
    response = ctx->allocate<int> ();

    if (HA_DISABLED ())
      {
	*response = htonl (HA_SERVER_STATE_NA);
      }
    else
      {
	*response = htonl (css_ha_server_state ());
      }

    ctx->push ({ reinterpret_cast<std::byte *> (response), sizeof (int) });

    /* make the packets to msghdr */
    ctx->m_sendbuf.stamp_msghdr ();

    /* update the events */
    if (!this->update_epoll_events (ctx))
      {
	er_log_conn (__FILE__, __LINE__, "master::connector->prepare_heartbeat_ha_mode: update_epoll_events failed: %s",
		     strerror (errno));
	return false;
      }
    return true;
  }

  inline bool connector::prepare_heartbeat_log_eof (context *ctx) noexcept
  {
    LOG_LSA *eof_lsa;
    static LOG_LSA prev_eof_lsa = LSA_INITIALIZER;
    alignas (8) std::byte reply[OR_LOG_LSA_ALIGNED_SIZE];

    assert (m_entry != nullptr);
    LOG_CS_ENTER_READ_MODE (m_entry);

    eof_lsa = log_get_eof_lsa ();
    (void) or_pack_log_lsa (reinterpret_cast<char *> (reply), eof_lsa);

    LOG_CS_EXIT (m_entry);

    if (LSA_EQ (&prev_eof_lsa, eof_lsa))
      {
	er_log_debug (ARG_FILE_LINE, "Disk failure has been occurred: prev_eof_lsa(%lld, %d), eof_lsa(%lld, %d)\n",
		      LSA_AS_ARGS (&prev_eof_lsa), LSA_AS_ARGS (eof_lsa));
      }
    else
      {
	LSA_COPY (&prev_eof_lsa, eof_lsa);
      }

    if (!this->prepare_heartbeat_send_request_with_data (ctx, SERVER_GET_EOF, reply, OR_LOG_LSA_ALIGNED_SIZE))
      {
	return false;
      }
    return true;
  }

  inline bool connector::switch_to_unix_socket (context *ctx) noexcept
  {
    int datagram_fd;

    /* wait to be reqeusted to connect from master */
    if (!css_tcp_listen_server_datagram (m_unixsocket, &datagram_fd))
      {
	er_log_conn (__FILE__, __LINE__, "master::connector->switch_to_unix_socket: css_tcp_listen_server_datagram failed: %s",
		     strerror (errno));

	(void) ::unlink (m_unixpath.c_str ());
	::close (m_unixsocket);
	er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_ERROR_DURING_SERVER_CONNECT, 1);
	return false;
      }

    /* remove original */
    if (!m_events.remove_descriptor (ctx->m_conn->fd))
      {
	er_log_conn (__FILE__, __LINE__, "master::connector->switch_to_unix_socket: m_events->remove_descriptor failed: %s",
		     strerror (errno));
	return false;
      }

    /* only connected file descriptor is needed */
    (void) ::unlink (m_unixpath.c_str ());
    css_prepare_shutdown_conn (ctx->m_conn);
    css_free_conn (ctx->m_conn);
    ::close (m_unixsocket);

    /* new connection */
    ctx->m_conn = css_make_conn (datagram_fd);

    /* make new socket non-blocking */
    assert (!this->m_events.is_nonblocking (datagram_fd));
    if (!this->make_nonblocking (datagram_fd))
      {
	er_log_conn (__FILE__, __LINE__, "master::connector->switch_to_unix_socket: m_events->make_nonblocking failed: %s",
		     strerror (errno));
	return false;
      }

    if (!m_events.add_descriptor (ctx->m_conn->fd, EPOLLIN | EPOLLRDHUP, ctx))
      {
	er_log_conn (__FILE__, __LINE__, "master::connector->switch_to_unix_socket: m_events->add_descriptor failed: %s",
		     strerror (errno));
	return false;
      }

    er_log_debug (__FILE__, __LINE__, "successfully switched to unix domain socket\n");
    m_master_state = master_state::ESTABLISHED;

    return true;
  }

  inline result connector::handshake_from_master (context *ctx) noexcept
  {
    const int *buf;
    result status;
    int response;

    std::tie (status, buf) = buffered_socket::read_fixed_size<int> (ctx->m_conn->fd, ctx->m_recvbuf);
    if (status != result::Ok)
      {
	er_log_conn (__FILE__, __LINE__, "master::connector->execute: read_fixed_size returned %d", status);
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

  inline result connector::request_new_client (context *ctx) noexcept
  {
    context *new_ctx;
    CSS_CONN_ENTRY *conn;
    unsigned short request_id;
    SOCKET new_fd;
    result status;

    /* master context goes back to waiting for next request regardless of send path */
    NEXT_STATE (ctx, RecvRequestType);

    /* receive new socket descriptor from the master */
    new_fd = css_open_new_socket_from_master (ctx->m_conn->fd, &request_id);
    er_log_conn (__FILE__, __LINE__, "master::connector->request_new_client: unpack new socket: %d\n", new_fd);
    if (IS_INVALID_SOCKET (new_fd))
      {
	er_log_debug (__FILE__, __LINE__,
		      "master::connector->request_new_client: failed to receive client socket from master. \
		      this usually indicates the master process cannot accept new connections. \
		      check master process logs and system fd limits (ulimit -n, /proc/sys/fs/file-max)");
	return result::Reset;
      }

    if (!this->opt_socket (new_fd) || !this->make_nonblocking (new_fd))
      {
	er_log_conn (__FILE__, __LINE__, "master::connector->request_new_client: %s", strerror (errno));
	::close (new_fd);

	/* this return value indicates the status of the master connection, so */
	/* return RefuseConnection and close new connections here. */
	return result::RefuseConnection;
      }

    /* make new context and conn */
    new_ctx = make_context ();

    /* check */
    if (prm_get_bool_value (PRM_ID_ACCESS_IP_CONTROL) == true && css_check_accessibility (new_fd) != NO_ERROR)
      {
	NEXT_STATE (ctx, RecvRequestType);
	new_ctx->m_conn = new css_conn_entry;
	css_initialize_conn (new_ctx->m_conn, new_fd);
	new_ctx->m_conn->request_id = request_id;

	if (!this->prepare_reply_refuse_connection (new_ctx, SERVER_INACCESSIBLE_IP))
	  {
	    delete new_ctx->m_conn;
	    delete new_ctx;

	    return result::RefuseConnection;
	  }

	NEXT_STATE (new_ctx, SendReplyToClient);
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
	    delete new_ctx->m_conn;
	    delete new_ctx;

	    return result::RefuseConnection;
	  }

	NEXT_STATE (new_ctx, SendReplyToClient);
	return result::RefuseConnection;
      }

    new_ctx->m_conn = conn;
    new_ctx->m_conn->request_id = request_id;
    if (!this->prepare_reply (new_ctx, SERVER_CONNECTED))
      {
	css_prepare_shutdown_conn (new_ctx->m_conn);
	css_free_conn (new_ctx->m_conn);
	delete new_ctx;

	return result::RefuseConnection;
      }

    /* try to send and register the fd to epoll if fails. */
    status = buffered_socket::send_partial (new_ctx->m_conn->fd, new_ctx->m_sendbuf);
    if (status == result::Ok)
      {
	this->sent_reply_to_client (new_ctx);

	return result::Ok;
      }
    else if (status == result::PeerReset || status == result::Error)
      {
	css_prepare_shutdown_conn (new_ctx->m_conn);
	css_free_conn (new_ctx->m_conn);
	delete new_ctx;

	return result::RefuseConnection;
      }

    assert (status == result::Pending);

    if (!m_events.add_descriptor (new_ctx->m_conn->fd, EPOLLIN | EPOLLOUT | EPOLLRDHUP, new_ctx))
      {
	er_log_conn (__FILE__, __LINE__,
		     "master::connector->request_new_client: m_events->add_descriptor failed: %s", strerror (errno));
	css_prepare_shutdown_conn (new_ctx->m_conn);
	css_free_conn (new_ctx->m_conn);
	delete new_ctx;

	return result::RefuseConnection;
      }

    NEXT_STATE (new_ctx, SendReplyToClient);
    return result::Ok;
  }

  inline result connector::handle_request (context *ctx) noexcept
  {
    const int *buf;
    result status;
    int request;

    std::tie (status, buf) = buffered_socket::read_fixed_size<int> (ctx->m_conn->fd, ctx->m_recvbuf);
    if (status != result::Ok)
      {
	er_log_conn (__FILE__, __LINE__, "master::connector->handle_request: read_fixed_size returned %d", status);
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
	if (!HA_DISABLED ())
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_PROCESS_EVENT, 2,
		    "Disconnected with the cub_master and will shut itself down", "");
	  }
	m_stop = true;
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
	if (!this->prepare_heartbeat_ha_mode (ctx))
	  {
	    return result::Error;
	  }
	NEXT_STATE (ctx, SendHBToMaster);
	break;

      case SERVER_CHANGE_HA_MODE:
	NEXT_STATE (ctx, RecvHAMode);
	break;

      case SERVER_GET_EOF:
	if (!this->prepare_heartbeat_log_eof (ctx))
	  {
	    return result::Error;
	  }
	NEXT_STATE (ctx, SendHBToMaster);
	break;

      default:
	er_log_debug (__FILE__, __LINE__, "cub_server received unexpected request: %d\n", request);
	return result::Error;
      }

    return result::Ok;
  }

  inline result connector::change_ha_mode (context *ctx) noexcept
  {
    HA_SERVER_STATE state;
    const int *buf;
    result status;

    std::tie (status, buf) = buffered_socket::read_fixed_size<int> (ctx->m_conn->fd, ctx->m_recvbuf);
    if (status != result::Ok)
      {
	er_log_conn (__FILE__, __LINE__, "master::connector->change_ha_mode: read_fixed_size returned %d", status);
	return status;
      }

    state = (HA_SERVER_STATE) ntohl (*buf);
    ctx->m_recvbuf.mark_consumed ();

    er_log_debug (__FILE__, __LINE__, "cub_server received request to change ha mode = %d\n", state);

    assert (m_entry != nullptr);

    if (state == HA_SERVER_STATE_ACTIVE || state == HA_SERVER_STATE_STANDBY)
      {
	if (css_change_ha_server_state (m_entry, state, false, HA_CHANGE_MODE_IMMEDIATELY, true) != NO_ERROR)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_ERROR_FROM_SERVER, 1, "Cannot change server HA mode");
	  }
      }
    else
      {
	er_log_debug (ARG_FILE_LINE, "ERROR : unexpected state. (state :%d). \n", state);
      }

    state = (HA_SERVER_STATE) htonl ((int) css_ha_server_state ());

    if (!this->prepare_heartbeat_send_request_with_data (ctx, SERVER_CHANGE_HA_MODE, reinterpret_cast<std::byte *> (&state),
	sizeof (state)))
      {
	return result::Error;
      }

    NEXT_STATE (ctx, SendHBToMaster);
    return result::Ok;
  }

  inline bool connector::handle_master_reception (context *ctx) noexcept
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

      case state::RecvHAMode:
	status = this->change_ha_mode (ctx);
	/* next state have already been set in change_ha_mode. */
	break;

      case state::SendInHandshake:
      case state::SwitchToUnixSocket:
      case state::SendReplyToClient:
      case state::SendHBToMaster:
	/* these will be handled in handle_master_transmission */
	break;

      default:
	er_log_conn (__FILE__, __LINE__, "master::connector->handle_master_connection failed: m_context->state: %d",
		     ctx->m_state);
	assert_release (false);
	break;
      }

    /* Is there an error */
    if (status == result::Reset)
      {
	er_log_conn (__FILE__, __LINE__, "master::connector->handle_master_transmission: protocol is messed up somewhere");
	ctx->m_recvbuf.reset ();
	ctx->m_sendbuf.clear ();
	NEXT_STATE (ctx, RecvRequestType);
      }
    else if (status == result::PeerReset)
      {
	er_log_conn (__FILE__, __LINE__, "master::connector->handle_master_connection: reset by peer");
	return false;
      }
    else if (status == result::Error)
      {
	er_log_conn (__FILE__, __LINE__, "master::connector->handle_master_connection: failed");
	return false;
      }

    return true;
  }

  inline void connector::sent_reply_to_client (context *ctx) noexcept
  {
    if (!ctx->m_has_error)
      {
	css_insert_into_active_conn_list (ctx->m_conn);

	ctx->m_conn->request_id = 0;
	m_connection_pool->dispatch (ctx->m_conn);
      }
    else
      {
	/* In error context, this conn entry has been temporarily allocated */
	if (!IS_INVALID_SOCKET (ctx->m_conn->fd))
	  {
	    css_shutdown_socket (ctx->m_conn->fd);
	    ctx->m_conn->fd = INVALID_SOCKET;
	  }
	delete ctx->m_conn;
      }

    ctx->m_conn = nullptr;
    delete ctx;
  }

  inline bool connector::handle_master_transmission (context *ctx) noexcept
  {
    result status;

    assert (ctx->m_state != state::RecvInHandshake &&
	    ctx->m_state != state::RecvRequestType &&
	    ctx->m_state != state::RecvNewClient &&
	    ctx->m_state != state::RecvHAMode);
    assert (ctx && ctx->m_conn);

    if (!ctx->has_data_to_send ())
      {
	/* no data to send */
	return true;
      }

    status = buffered_socket::send_partial (ctx->m_conn->fd, ctx->m_sendbuf);
    if (status == result::PeerReset || status == result::Error)
      {
	return false;
      }
    if (status == result::Pending)
      {
	/* pending */
	return true;
      }

    assert (status == result::Ok);

    /* fully send */
    er_log_conn (__FILE__, __LINE__, "master::connector->handle_master_transmission: fully sent the data to fd = %d\n",
		 ctx->m_conn->fd);

    /* move to next state */
    switch (ctx->m_state)
      {
      case state::SendInHandshake:
	NEXT_STATE (ctx, RecvInHandshake);
	break;

      case state::SwitchToUnixSocket:
	/* switching to unix domain socket */
	if (!this->switch_to_unix_socket (ctx))
	  {
	    er_log_conn (__FILE__, __LINE__,
			 "master::connector->handle_master_transmission: master->switch_to_unix_socket failed");
	    return false;
	  }
	/* register myself to master */
	if (!HA_DISABLED ())
	  {
	    if (!this->prepare_heartbeat_register (ctx))
	      {
		er_log_conn (__FILE__, __LINE__,
			     "master::connector->handle_master_transmission: prepare_heartbeat_register failed");
		return false;
	      }
	    NEXT_STATE (ctx, SendHBToMaster);
	  }
	else
	  {
	    NEXT_STATE (ctx, RecvRequestType);
	  }
	break;

      case state::SendReplyToClient:
	er_log_conn (__FILE__, __LINE__, "master::connector->sent_reply_to_client: remove fd = %d\n", ctx->m_conn->fd);
	if (!m_events.remove_descriptor (ctx->m_conn->fd))
	  {
	    er_log_conn (__FILE__, __LINE__, "master::connector->sent_reply_to_client: m_events->remove_descriptor failed: %s",
			 strerror (errno));
	    return false;
	  }
	this->sent_reply_to_client (ctx);
	/* return here to avoid segfault */
	return true;

      case state::SendHBToMaster:
	NEXT_STATE (ctx, RecvRequestType);
	break;

      default:
	/* impossible ! */
	assert_release (false);
	break;
      }

    /* update */
    if (!ctx->has_data_to_send () && !this->update_epoll_events (ctx))
      {
	er_log_conn (__FILE__, __LINE__, "master::connector->handle_master_transmission: update_epoll_events failed: %s",
		     strerror (errno));
	return false;
      }
    return true;
  }

  inline bool connector::dispose_master_connection () noexcept
  {
    if (m_master_state == master_state::WAIT_RESPONSE || m_master_state == master_state::ESTABLISHED)
      {
	/* remove the fd which is reset by peer */
	if (!m_events.remove_descriptor (m_context.m_conn->fd))
	  {
	    er_log_conn (__FILE__, __LINE__,
			 "master::connector->dispose_master_connection: m_events->remove_descriptor failed: %s",
			 strerror (errno));
	    return false;
	  }
      }

    if (m_master_state != master_state::CLOSED && m_context.m_conn)
      {
	css_prepare_shutdown_conn (m_context.m_conn);
	css_free_conn (m_context.m_conn);
	m_context.m_conn = nullptr;
      }

    m_context.reset ();
    m_master_state = master_state::CLOSED;

    return true;
  }

  inline bool connector::try_to_reestablish_with_master () noexcept
  {
    fprintf (stderr, "try to re-establish the connection with master...\n");

    if (!this->dispose_master_connection ())
      {
	er_log_conn (__FILE__, __LINE__, "master::connector->try_to_reestablish_with_master: dispose_master_connection failed");
	return false;
      }

    er_log_conn (__FILE__, __LINE__,
		 "master::connector->try_to_reestablish_with_master: reestablish the connection with master");

    if (!this->connect (m_master_port))
      {
	er_log_conn (__FILE__, __LINE__, "master::connector->try_to_reestablish_with_master: connect failed");
	return false;
      }

    if (!this->prepare_handshake (m_server_name))
      {
	er_log_conn (__FILE__, __LINE__, "master::connector->try_to_reestablish_with_master: prepare_handshake failed");
	/* ensure state goes back to CLOSED and new conn is freed */
	(void) this->dispose_master_connection ();
	return false;
      }

    return true;
  }

  inline bool connector::disconnect (context *ctx) noexcept
  {
    if (ctx->m_conn->fd == m_context.m_conn->fd)
      {
	if (!HA_DISABLED ())
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_PROCESS_EVENT, 2,
		    "Disconnected with the cub_master and will shut itself down", "");
	    m_stop = true;
	    return true;
	  }
	/* WAIT RESPONSE or ESTABLISHED */
	this->try_to_reestablish_with_master ();

	return true;
      }

    return this->dispose_connection (ctx);
  }

  inline bool connector::execute () noexcept
  {
    std::array<epoll_event, 512> events;
    context *ctx;
    int nfds, i;

    while (!m_stop)
      {
	nfds = m_events.wait (events.data (), events.size (), 5 * 1000 /* timeout for re-establish */);
	if (nfds < 0)
	  {
	    if (errno == EINTR)
	      {
		continue;
	      }
	    er_log_conn (__FILE__, __LINE__, "master::connector->execute: m_events->wait failed: %s", strerror (errno));
	    assert_release (false);
	    continue;
	  }

	if (__builtin_expect (m_master_state == master_state::CLOSED, 0))
	  {
	    /* re-establish the connection with master if it died */
	    this->try_to_reestablish_with_master ();
	  }

	for (i = 0; i < nfds; i++)
	  {
	    assert (events[i].data.ptr);

	    ctx = reinterpret_cast<context *> (events[i].data.ptr);
	    /* handle hangup/error first to avoid writes on dead sockets */
	    if (events[i].events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR) && ctx->m_conn->fd != m_eventfd)
	      {
		er_log_conn (__FILE__, __LINE__, "master::connector->execute: master connection closed: %s", strerror (errno));
		if (!this->disconnect (ctx))
		  {
		    return false;
		  }
		continue;
	      }
	    if (events[i].events & EPOLLIN)
	      {
		if (ctx->m_conn->fd == m_eventfd)
		  {
		    /* finalize */
		    return true;
		  }
		if (ctx->has_data_to_send ())
		  {
		    /* don't read while there is pending data to send */
		  }
		else if (!this->handle_master_reception (ctx))
		  {
		    er_log_conn (__FILE__, __LINE__, "master::connector->execute: handle_master_reception failed: %d\n", 0);
		    if (!this->disconnect (ctx))
		      {
			return false;
		      }
		    continue;
		  }
	      }
	    if (events[i].events & EPOLLOUT)
	      {
		if (!this->handle_master_transmission (ctx))
		  {
		    er_log_conn (__FILE__, __LINE__, "master::connector->execute: handle_master_transmission failed");
		    if (!this->disconnect (ctx))
		      {
			return false;
		      }
		    continue;
		  }
	      }
	  }
      }
    return true;
  }
}
