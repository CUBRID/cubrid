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
 * communication_channel.cpp - wrapper for communication primitives
 *                           - it can establish a connection, send/receive messages, wait for events and close the connection
 */

#include "communication_channel.hpp"

#include "connection_support.h"
#include "connection_globals.h"
#include "error_manager.h"
#include "system_parameter.h"

#if defined(WINDOWS)
#include "wintcp.h"
#else /* WINDOWS */
#include <netinet/in.h>
#include "tcp.h"
#endif /* WINDOWS */

#include <algorithm>  /* std::min */
#include <string>

namespace cubcomm
{
#define er_log_chn_debug(...) \
  if (prm_get_bool_value (PRM_ID_ER_LOG_COMM_CHANNEL)) _er_log_debug (ARG_FILE_LINE, "[COMM_CHN]" __VA_ARGS__)

  std::atomic<uint64_t> channel::unique_id_allocator = 0;

  channel::channel (int max_timeout_in_ms)
    : m_max_timeout_in_ms (max_timeout_in_ms)
    , m_type (CHANNEL_TYPE::NO_TYPE)
    , m_socket (INVALID_SOCKET)
    , m_unique_id (unique_id_allocator++)
  {
  }

  channel::channel (std::string &&channel_name)
    : m_channel_name { std::move (channel_name) }
    , m_unique_id (unique_id_allocator++)
  {
  }

  channel::channel (int max_timeout_in_ms, std::string &&channel_name)
    : m_max_timeout_in_ms (max_timeout_in_ms)
    , m_channel_name { std::move (channel_name) }
    , m_unique_id (unique_id_allocator++)
  {
  }

  channel::channel (channel &&comm)
    : m_max_timeout_in_ms (comm.m_max_timeout_in_ms)
    , m_unique_id (comm.m_unique_id)
  {
    m_type = comm.m_type;
    comm.m_type = NO_TYPE;

    m_socket = comm.m_socket;
    comm.m_socket = INVALID_SOCKET;

    m_channel_name = std::move (comm.m_channel_name);
    m_hostname = std::move (comm.m_hostname);

    m_port = comm.m_port;
    comm.m_port = INVALID_PORT;
  }

  channel::~channel ()
  {
    close_connection ();
  }

  css_error_code channel::send (const std::string &message)
  {
    return channel::send (message.c_str (), message.length ());
  }

  css_error_code channel::recv (char *buffer, std::size_t &maxlen_in_recvlen_out)
  {
    int actual_recv_maxlen = (int) maxlen_in_recvlen_out;

    assert (m_type != NO_TYPE);

    const css_error_code rc = (css_error_code) css_net_recv (m_socket, buffer, &actual_recv_maxlen,
			      m_max_timeout_in_ms);
    er_log_chn_debug ("[%s] Receive buffer of size = %d, max_size = %zu, result = %d", get_channel_id ().c_str (),
		      actual_recv_maxlen, maxlen_in_recvlen_out, (int)rc);
    maxlen_in_recvlen_out = actual_recv_maxlen;
    return rc;
  }

  css_error_code channel::recv_allow_truncated (char *buffer, std::size_t &maxlen_in_recvlen_out,
      std::size_t &remaining_len)
  {
    int actual_recv_maxlen = (int) maxlen_in_recvlen_out;
    int actual_remaining_len = (int) remaining_len;

    assert (m_type != NO_TYPE);
    assert (actual_remaining_len == 0);

    const css_error_code rc = css_net_recv_allow_truncated (m_socket, buffer, &actual_recv_maxlen,
			      &actual_remaining_len, m_max_timeout_in_ms);
    er_log_chn_debug ("[%s] Receive%s buffer of size = %d, max_size = %zu, remain = %d, result = %d",
		      get_channel_id ().c_str (), ((actual_remaining_len > 0) ? " truncated" : " full"),
		      actual_recv_maxlen, maxlen_in_recvlen_out,
		      actual_remaining_len, (int)rc);

    assert (actual_recv_maxlen >= 0);
    maxlen_in_recvlen_out = static_cast<std::size_t> (actual_recv_maxlen);

    assert (actual_remaining_len >= 0);
    remaining_len = static_cast<std::size_t> (actual_remaining_len);

    return rc;
  }

  css_error_code channel::recv_remainder (char *buffer, std::size_t &maxlen_in_recvlen_out)
  {
    int actual_recv_maxlen = (int) maxlen_in_recvlen_out;

    assert (m_type != NO_TYPE);

    const css_error_code rc = css_net_recv_remainder (m_socket, buffer, &actual_recv_maxlen, m_max_timeout_in_ms);
    er_log_chn_debug ("[%s] Receive remainder buffer of size = %d, max_size = %zu, result = %d",
		      get_channel_id ().c_str (), actual_recv_maxlen, maxlen_in_recvlen_out, (int)rc);

    assert (actual_recv_maxlen >= 0);
    maxlen_in_recvlen_out = static_cast<std::size_t> (actual_recv_maxlen);

    return rc;
  }

  css_error_code channel::send (const char *buffer, std::size_t length)
  {
    int templen = 0;
    constexpr int vector_length = 2;
    int total_len = 0, rc = NO_ERRORS;
    struct iovec iov[vector_length];

    assert (m_type != NO_TYPE);

    css_set_io_vector (&iov[0], &iov[1], buffer, (int) length, &templen);
    total_len = (int) (sizeof (int) + length);

    rc = css_send_io_vector_with_socket (m_socket, iov, total_len, vector_length, m_max_timeout_in_ms);
    er_log_chn_debug ("[%s] Send buffer of size = %zu, result = %d.\n", get_channel_id ().c_str (), length, rc);
    return (css_error_code) rc;
  }

  css_error_code channel::send_int (int val)
  {
    int v = htonl (val);
    const int sent_byte_count = ::send (m_socket, reinterpret_cast<const char *> (&v), sizeof (v), 0);
    if (sent_byte_count == sizeof (v))
      {
	er_log_chn_debug ("[%s] Success send int value %d\n", get_channel_id ().c_str (), val);
	return NO_ERRORS;
      }
    else
      {
	er_log_chn_debug ("[%s] Failed send int value %d\n", get_channel_id ().c_str (), val);
	return ERROR_ON_WRITE;
      }
  }

  css_error_code channel::recv_int (int &received)
  {
    const size_t len = sizeof (received);

    const int readlen = css_readn (m_socket, reinterpret_cast<char *> (&received), (int) len, m_max_timeout_in_ms);
    css_error_code error = NO_ERRORS;
    if (readlen < 0)
      {
	error = css_error_code::ERROR_ON_COMMAND_READ;
      }
    else if ((size_t) readlen != len)
      {
	error = css_error_code::READ_LENGTH_MISMATCH;
      }
    else
      {
	received = ntohl (received);
      }
    er_log_chn_debug ("[%s] Receive int value = %d, error = %d.\n", get_channel_id ().c_str (), received, error);
    return error;
  }

  css_error_code channel::connect (const char *hostname, int port)
  {
    if (is_connection_alive ())
      {
	assert (false);
	return INTERNAL_CSS_ERROR;
      }

    m_socket = css_tcp_client_open (hostname, port);

    er_log_chn_debug ("[%s] Connect to %s:%d socket = %d.\n", get_channel_id ().c_str (), hostname, port, m_socket);

    if (IS_INVALID_SOCKET (m_socket))
      {
	return REQUEST_REFUSED;
      }

    m_type = CHANNEL_TYPE::INITIATOR;
    m_hostname = hostname;
    m_port = port;

    return NO_ERRORS;
  }

  css_error_code channel::accept (SOCKET socket)
  {
    er_log_chn_debug ("[%s] Accept connection to socket = %d.\n", get_channel_id ().c_str (), socket);

    if (is_connection_alive () || IS_INVALID_SOCKET (socket))
      {
	return INTERNAL_CSS_ERROR;
      }

    m_type = CHANNEL_TYPE::LISTENER;
    m_socket = socket;

    return NO_ERRORS;
  }

  void channel::close_connection ()
  {
    if (!IS_INVALID_SOCKET (m_socket))
      {
	er_log_chn_debug ("[%s] Shutdown socket %d.\n", get_channel_id ().c_str (), m_socket);
	css_shutdown_socket (m_socket);
	m_socket = INVALID_SOCKET;
	m_type = NO_TYPE;
      }

    m_hostname.clear ();
    m_port = INVALID_PORT;
  }

  int channel::get_max_timeout_in_ms () const
  {
    return m_max_timeout_in_ms;
  }

  int channel::wait_for (unsigned short int events, unsigned short int &revents) const
  {
    POLL_FD poll_fd = {0, 0, 0};
    int rc = 0;
    revents = 0;

    if (!is_connection_alive ())
      {
	return -1;
      }

    assert (m_type != NO_TYPE);

    poll_fd.fd = m_socket;
    poll_fd.events = events;
    poll_fd.revents = 0;

    rc = css_platform_independent_poll (&poll_fd, 1, m_max_timeout_in_ms);
    revents = poll_fd.revents;
    er_log_chn_debug ("[%s] Poll events=%d revents=%d result = %d.\n", get_channel_id ().c_str (), events, revents, rc);

    return rc;
  }

  bool channel::is_connection_alive () const
  {
    return !IS_INVALID_SOCKET (m_socket);
  }

  SOCKET channel::get_socket () const
  {
    return m_socket;
  }

  int channel::get_port () const
  {
    return m_port;
  }

} /* namespace cubcomm */
