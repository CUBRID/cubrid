/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * udp_rpc.hpp - remote procedure call (RPC) over UDP transport module
 */

#ifndef _UDP_RPC_HPP_
#define _UDP_RPC_HPP_

#include "error_context.hpp"
#include "hostname.hpp"
#if defined (LINUX)
#include "tcp.h"
#endif
#if defined (WINDOWS)
#include "wintcp.h"
#endif

#include <cstring>
#include <functional>
#include <map>
#include <thread>

// type aliases
using socket_type = SOCKET;
using ipv4_type = std::uint32_t;
using port_type = std::uint16_t;

// constants
static const std::size_t BUFFER_SIZE = 4096;

/**
 * Server to Server communication model:
 *
 *    cub_master[udp_server host1:port] |                    | cub_master[udp_server host2:port]
 *                                      |                    |
 *                    client_request -> |                    | ->  server_request --
 *                                      |                    |                     |
 *                                      |                    |                     |
 *                                      |                    |                     |
 *                    server_request <- |                    | <- server_response --
 *
 *
 * description:
 *   A UDP server used for RPC (Remote Procedure Call) between two heartbeat instances.
 *   Since this is a server to server communication, a server instance can act as both client and server
 *
 *   This is how request/response pattern works:
 *     1. server[host1] - creates a client_request and sends it to server[host2]
 *     2. server[host2] - receives data from the socket and create a server request
 *     3. server[host2] - searches in m_handlers map for appropriate handler to handle the request
 *     3. server[host2] - ends the request by sending some data
 *     4. server[host1] - receives the response as a server request
 *
 *
 * usage:
 *   enum example_message
 *   {
 *     MSG_1
 *   };
 *
 *   using example_server = udp_server<example_message>
 *   example_server server (port);
 *   int error_code = server->start ();
 *   if (error_code != NO_ERROR)
 *     {
 *       // something happened, handle the error
 *       return;
 *     }
 *
 *   // server started successfully
 *   // do stuff
 *
 *   // on shutdown
 *   server->stop ();
 *
 *
 * send a client request:
 *   cubbase:hostname_type host ("destination_hostname");
 *   cubpacking::packable_object some_message; // message to be sent
 *
 *   example_server::client_request request = server.create_client_request (host);
 *
 *   request.set_message (example_message::MSG_1, some_message);
 *   request.end ();
 *
 *
 * handle a server request:
 *   example_server::server_request_handler handler = <use some_handler function>
 *   server.register_handler (example_message::MSG_1, handler);
 *
 *   void
 *   some_handler (example_server::server_request &request)
 *   {
 *     cubpacking::packable_object request_message;
 *     request.get_message (request_message);
 *
 *     // do your magic with message from the request
 *
 *     // send the response message
 *     cubpacking::packable_object response_message;
 *     request.get_response ().set_message (response_message);
 *   }
 */

template <typename MsgId>
class udp_server
{
  public:
    class client_request
    {
      public:
	client_request () = delete;
	client_request (socket_type sfd, const cubbase::hostname_type &host, port_type port);

	// send the request to destination host
	void end () const;

	// template function: set the payload of the client request
	// Msg will be automatically packed into a buffer
	template <typename Msg>
	void set_message (MsgId msg_id, const Msg &msg);

      private:
	port_type m_port;
	cubbase::hostname_type m_host;
	socket_type m_sfd;
	cubmem::extensible_block m_buffer;
    };

    class server_response
    {
      public:
	server_response ();

	// template function: set the payload of the server response
	// Msg will be automatically packed into a buffer
	template <typename Msg>
	void set_message (const Msg &msg);

      private:
	friend class udp_server<MsgId>::server_request;

	MsgId m_message_id;
	cubmem::extensible_block m_buffer;
    };

    class server_request
    {
      public:
	server_request () = delete;
	server_request (socket_type sfd, ipv4_type ip_addr, port_type port, const char *buffer, std::size_t buffer_size);

	MsgId get_message_id () const;
	bool is_response_requested () const;
	ipv4_type get_remote_ip_address () const;
	server_response &get_response ();

	// send the response
	void end () const;

	// template function: get the payload of the server request
	// payload will be automatically unpacked into Msg type
	template <typename Msg>
	void get_message (Msg &msg) const;

      private:
	socket_type m_sfd;
	ipv4_type m_remote_ip_addr;
	port_type m_remote_port;
	cubmem::block m_buffer;
	server_response m_response;
    };

    using server_request_handler = std::function<void (server_request &)>;

    explicit udp_server (port_type port);
    ~udp_server ();

    // Don't allow copy/move of udp_server
    udp_server (udp_server &&other) = delete;
    udp_server (const udp_server &other) = delete;
    udp_server &operator= (udp_server &&other) = delete;
    udp_server &operator= (const udp_server &other) = delete;

    int start ();
    void stop ();

    client_request create_client_request (const cubbase::hostname_type &host) const;
    void register_handler (MsgId msg_id, server_request_handler &handler);

  private:
    using request_handlers_type = std::map<MsgId, server_request_handler>;

    std::thread m_thread;
    bool m_shutdown;
    port_type m_port;
    socket_type m_sfd;

    request_handlers_type m_handlers;

    int listen ();
    int init_socket ();
    void close_socket ();
    static void poll (udp_server *arg);
    void handle (server_request &request) const;
};

// serialize message and message id into the buffer
template <typename MsgId, typename Msg>
void udp_serialize (cubmem::extensible_block &buffer, MsgId &msg_id, const Msg &msg, bool response_requested);

// deserialize buffer into message and message id
template <typename MsgId, typename Msg>
void udp_deserialize (const cubmem::block &buffer, MsgId &msg_id, Msg &msg, bool &response_requested);

inline void udp_send_to (const char *buffer, std::size_t buffer_size, socket_type sfd, ipv4_type ip_addr,
			 port_type port);

// Template function implementation

//
// udp_server
//
template <typename MsgId>
udp_server<MsgId>::udp_server (port_type port)
  : m_thread ()
  , m_shutdown (true)
  , m_port (port)
  , m_sfd (INVALID_SOCKET)
  , m_handlers ()
{
  //
}

template <typename MsgId>
udp_server<MsgId>::~udp_server ()
{
  stop ();
  m_handlers.clear ();
}

template <typename MsgId>
int
udp_server<MsgId>::start ()
{
  int error_code = listen ();
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  m_shutdown = false;
  m_thread = std::thread (udp_server::poll, this);

  return NO_ERROR;
}

template <typename MsgId>
void
udp_server<MsgId>::stop ()
{
  if (m_shutdown)
    {
      return;
    }

  close_socket ();

  m_shutdown = true;
  m_thread.join ();
}

template <typename MsgId>
typename udp_server<MsgId>::client_request
udp_server<MsgId>::create_client_request (const cubbase::hostname_type &host) const
{
  return {m_sfd, host, m_port};
}

template <typename MsgId>
void
udp_server<MsgId>::register_handler (MsgId msg_id, server_request_handler &handler)
{
  m_handlers.emplace (msg_id, handler);
}

template <typename MsgId>
int
udp_server<MsgId>::listen ()
{
  int error_code = init_socket ();
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  sockaddr_in udp_sockaddr;
  std::memset (&udp_sockaddr, 0, sizeof (udp_sockaddr));
  udp_sockaddr.sin_family = AF_INET;
  udp_sockaddr.sin_addr.s_addr = htonl (INADDR_ANY);
  udp_sockaddr.sin_port = htons (m_port);

  int bind_result = bind (m_sfd, (sockaddr *) &udp_sockaddr, sizeof (udp_sockaddr));
  if (bind_result < 0)
    {
      close_socket ();
      return ERR_CSS_TCP_DATAGRAM_BIND;
    }

  return NO_ERROR;
}

template <typename MsgId>
int
udp_server<MsgId>::init_socket ()
{
#if defined (WINDOWS)
  int error_code = css_windows_startup ();
  if (error_code != NO_ERROR)
    {
      close_socket ();
      return error_code;
    }
#endif

  m_sfd = socket (AF_INET, SOCK_DGRAM, 0);
  if (m_sfd < 0)
    {
      close_socket ();
      return ERR_CSS_TCP_DATAGRAM_SOCKET;
    }
}

template <typename MsgId>
void
udp_server<MsgId>::close_socket ()
{
#if defined (WINDOWS)
  css_windows_shutdown ();
#endif

  if (m_sfd == INVALID_SOCKET)
    {
      return;
    }

#if defined (LINUX)
  ::close (m_sfd);
#endif
#if defined (WINDOWS)
  css_shutdown_socket (m_sfd);
#endif

  m_sfd = INVALID_SOCKET;
}

template <typename MsgId>
void
udp_server<MsgId>::poll (udp_server *arg)
{
  sockaddr_in from;
  socklen_t from_len = sizeof (sockaddr_in);
  std::memset (&from, 0, from_len);

  pollfd fds[1] = {{0, 0, 0}};
  socket_type sfd = arg->m_sfd;

  char buffer[BUFFER_SIZE + MAX_ALIGNMENT];
  char *aligned_buffer = PTR_ALIGN (buffer, MAX_ALIGNMENT);

  cuberr::context er_context (true);

  while (!arg->m_shutdown)
    {
      fds[0].fd = sfd;
      fds[0].events = POLLIN;

      int error_code = ::poll (fds, 1, 1);
      if (error_code <= 0)
	{
	  continue;
	}

      if ((fds[0].revents & POLLIN) && sfd == arg->m_sfd)
	{
	  ssize_t buffer_size = recvfrom (sfd, aligned_buffer, BUFFER_SIZE, 0, (sockaddr *) &from, &from_len);
	  if (buffer_size <= 0)
	    {
	      continue;
	    }

	  ipv4_type client_ip = from.sin_addr.s_addr;
	  port_type client_port = ntohs (from.sin_port);

	  server_request request (sfd, client_ip, client_port, aligned_buffer, buffer_size);
	  arg->handle (request);
	}
    }
}

template <typename MsgId>
void
udp_server<MsgId>::handle (server_request &request) const
{
  auto found = m_handlers.find (request.get_message_id ());
  if (found == m_handlers.end ())
    {
      assert (false);
      return;
    }

  // call request handler
  found->second (request);

  // send response
  if (request.is_response_requested ())
    {
      request.end ();
    }
}

//
// client_request
//
template <typename MsgId>
udp_server<MsgId>::client_request::client_request (socket_type sfd, const cubbase::hostname_type &host, port_type port)
  : m_port (port)
  , m_host (host)
  , m_sfd (sfd)
  , m_buffer ()
{
  //
}

template <typename MsgId>
void
udp_server<MsgId>::client_request::end () const
{
  unsigned char sin_addr[4];
  int error_code = css_hostname_to_ip (m_host.as_c_str (), sin_addr);
  if (error_code != NO_ERROR)
    {
      return;
    }

  ipv4_type remote_ip_addr = 0;
  std::memcpy (&remote_ip_addr, sin_addr, sizeof (ipv4_type));

  udp_send_to (m_buffer.get_read_ptr (), m_buffer.get_size (), m_sfd, remote_ip_addr, m_port);
}

template <typename MsgId>
template <typename Msg>
void
udp_server<MsgId>::client_request::set_message (MsgId msg_id, const Msg &msg)
{
  udp_serialize (m_buffer, msg_id, msg, true);
}

//
// server_response
//
template <typename MsgId>
udp_server<MsgId>::server_response::server_response ()
  : m_message_id ()
  , m_buffer ()
{
  //
}

template <typename MsgId>
template <typename Msg>
void
udp_server<MsgId>::server_response::set_message (const Msg &msg)
{
  udp_serialize (m_buffer, m_message_id, msg, false);
}

//
// server_request
//
template <typename MsgId>
udp_server<MsgId>::server_request::server_request (socket_type sfd, ipv4_type ip_addr, port_type port,
    const char *buffer, std::size_t buffer_size)
  : m_sfd (sfd)
  , m_remote_ip_addr (ip_addr)
  , m_remote_port (port)
  , m_buffer (buffer_size, (void *) buffer)
  , m_response ()
{
  //
}

template <typename MsgId>
MsgId
udp_server<MsgId>::server_request::get_message_id () const
{
  assert (m_buffer.is_valid ());

  cubpacking::unpacker unpacker (m_buffer.ptr, m_buffer.dim);
  MsgId msg_id;
  unpacker.unpack_from_int (msg_id);

  return msg_id;
}

template <typename MsgId>
bool
udp_server<MsgId>::server_request::is_response_requested () const
{
  assert (m_buffer.is_valid ());
  cubpacking::unpacker unpacker (m_buffer.ptr, m_buffer.dim);

  MsgId msg_id;
  unpacker.unpack_from_int (msg_id);

  bool response_requested;
  unpacker.unpack_bool (response_requested);

  return response_requested;
}

template <typename MsgId>
ipv4_type
udp_server<MsgId>::server_request::get_remote_ip_address () const
{
  return m_remote_ip_addr;
}

template <typename MsgId>
typename udp_server<MsgId>::server_response &
udp_server<MsgId>::server_request::get_response ()
{
  // make sure that response will have same message id as request
  m_response.m_message_id = get_message_id ();
  return m_response;
}

template <typename MsgId>
void
udp_server<MsgId>::server_request::end () const
{
  udp_send_to (m_response.m_buffer.get_read_ptr (), m_response.m_buffer.get_size (), m_sfd, m_remote_ip_addr,
	       m_remote_port);
}

template <typename MsgId>
template <typename Msg>
void
udp_server<MsgId>::server_request::get_message (Msg &msg) const
{
  MsgId msg_id;
  bool response_requested;
  udp_deserialize (m_buffer, msg_id, msg, response_requested);
}

template <typename MsgId, typename Msg>
void
udp_serialize (cubmem::extensible_block &buffer, MsgId &msg_id, const Msg &msg, bool response_requested)
{
  cubpacking::packer packer;

  std::size_t msg_id_size = packer.get_packed_int_size (0); // message id
  std::size_t response_requested_size = packer.get_packed_bool_size (0); // response_requested
  std::size_t msg_size = packer.get_packed_size_overloaded (msg, 0); // message buffer
  buffer.extend_to (msg_id_size + response_requested_size + msg_size);

  packer.set_buffer (buffer.get_ptr (), buffer.get_size ());
  packer.pack_to_int (msg_id);
  packer.pack_bool (response_requested);
  packer.pack_overloaded (msg);

  // make sure that entire buffer has been filled
  bool buffer_filled = packer.is_ended ();
  assert (buffer_filled);
}

template <typename MsgId, typename Msg>
void
udp_deserialize (const cubmem::block &buffer, MsgId &msg_id, Msg &msg, bool &response_requested)
{
  assert (buffer.is_valid ());
  cubpacking::unpacker unpacker (buffer.ptr, buffer.dim);

  unpacker.unpack_from_int (msg_id);
  unpacker.unpack_bool (response_requested);
  unpacker.unpack_overloaded (msg);

  // make sure that entire buffer has been consumed
  bool buffer_consumed = unpacker.is_ended ();
  assert (buffer_consumed);
}

void
udp_send_to (const char *buffer, std::size_t buffer_size, socket_type sfd, ipv4_type ip_addr, port_type port)
{
  if (buffer == NULL || buffer_size == 0 || sfd == INVALID_SOCKET)
    {
      return;
    }

  sockaddr_in remote_sock_addr;
  socklen_t remote_sock_addr_len = sizeof (sockaddr_in);
  std::memset (&remote_sock_addr, 0, remote_sock_addr_len);

  remote_sock_addr.sin_family = AF_INET;
  remote_sock_addr.sin_port = htons (port);
  remote_sock_addr.sin_addr.s_addr = ip_addr;

  sendto (sfd, buffer, buffer_size, 0, (const sockaddr *) &remote_sock_addr, sizeof (sockaddr_in));
}

#endif /* _UDP_RPC_HPP_ */
