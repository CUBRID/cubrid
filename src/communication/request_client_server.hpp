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

#ifndef _REQUEST_CLIENT_SERVER_HPP_
#define _REQUEST_CLIENT_SERVER_HPP_

#include "communication_channel.hpp"
#include "mem_block.hpp"
#include "object_representation_constants.h"
#include "packer.hpp"

#include <functional>
#include <map>
#include <memory>
#include <thread>

namespace cubcomm
{
  // This header implements request handling over network. Each request has an identifier and for each identifier there
  // is a handler function that is called when the request is received. The handler unpacks the request message and
  // triggers an action based on the message.
  //
  // To send a request, a package is created with the request id and a message containing packed arguments.
  //
  // The request is handled on the other endpoint of the communication channel, by unpacking the id and choosing the
  // appropriate handler, a std::function<void(cubpacking::unpacker &>, for this id. The handler function is
  // responsible to unpack the arguments and to execute the request actions.
  //
  // Request handling specifications:
  //
  //	- The requests are sent and no response is waited.
  //	- The network layer is hidden in the communication::channel. The request layer is on top and uses the channel
  //	function send/sent_int, recv/recv_int, connect/accept, wait_for. Any channel specialization may be used.
  //
  // The classes:
  //
  //	request_client: sends requests over communication channel. is specialized by the type of requests sent.
  //	request_server: a thread receives the requests and calls the appropriate handlers. is specialized by the type of
  //			request received.
  //	request_client_server: send and receive requests on the same channel. is specialized by the type of requests
  //			sent and by the type of requests received.
  //
  // Unidirectional requests.
  //
  //	To send requests from an endpoint to another, the sender require a request_client and the receiver requires a
  //	request_server. The request_client and the request_server are specialized on the same MsgId. The request_server
  //	has handlers registered for each request id.
  //
  //	The code to achieve this must be something along the next lines:
  //
  //	Client:							Server:
  //
  //	cubcomm::channel chn;
  //	chn.connect (server_hostname, server_port);
  //
  //								cubcomm::channel chn;
  //								chn.accept (fd);
  //
  //	cubcomm::request_client<ReqId> req_client (std::move(chn));
  //
  //								cubcomm::request_server<ReqId> req_server (
  //								  std::move (chn));
  //								// register all handlers:
  //								req_server.register_handler (ReqId::FIRST,
  //											     first_handler);  // ...
  //
  //	req_client.send (ReqId::FIRST, arg_first, ...);
  //
  //								first_handler (unpacker)
  //								  unpacker.unpack_all (arg_first, ...); // ...
  //								  do_what_is_requested (arg_first, ...);
  //
  // Bidirectional requests.
  //
  //	To exchange requests between two endpoints, there must be a request_client_server on both endpoints. Each can
  //	have a different specialization if the types of requests sent by one side is different from the types sent by the
  //	other side.
  //
  //	The two request_client_servers need to be compatible. The type of request sent by first must match the type of
  //	requests received by the second. And vice-versa.
  //
  //	The code to achieve this must be something along the next lines:
  //
  //	Left:							Right:
  //
  //	cubcomm::channel chn;
  //	chn.connect (server_hostname, server_port);
  //
  //								cubcomm::channel chn;
  //								chn.accept (fd);
  //
  //	cubcomm::request_client_server<LeftReqId, RightReqId>
  //	  req_clsr (std::move (chn));
  //	// register all handlers
  //	req_clsr.register_handler (RightId::FIRST,
  //				   first_right_handler); // ...
  //
  //								cubcomm::request_client_server<RightReqId, LeftReqId>
  //								  req_clsr (std::move (chn));
  //								// register all handlers
  //								req_server.register_handler (LeftReqId::FIRST,
  //											     first_left_handler);
  //
  //	req_clsr.send (LeftReqId::FIRST, arg_first, ...);
  //
  //							        first_left_handler (unpacker)
  //								  unpacker.unpack_all (arg_first, ...);
  //								  do_what_is_requested (arg_first, ...);
  //
  //								req_clsr.sent (RightReqId::FIRST, arg_first, ...);
  //
  //	first_right_handler (unpacker);
  //	  unpacker.unpack_all (arg_first, ...);
  //	  do_what_is_requested2 (arg_first, ...);
  //


  // A client that sends the specialized request messages
  template <typename MsgId>
  class request_client
  {
    public:
      using client_request_id = MsgId;

      request_client () = delete;
      request_client (channel &&chn);
      request_client (const request_client &) = delete;
      request_client (request_client &&other) = default;

      template <typename ... PackableArgs>
      css_error_code send (MsgId msgid, const PackableArgs &... args);	//  pack args and send request of type msgid

      const channel &get_channel () const;			// get underlying channel

    private:
      channel m_channel;					// requests are sent on this channel
  };

  // A server that handles request messages. All requests must be preregistered.
  template <typename MsgId>
  class request_server
  {
    public:
      using server_request_handler = std::function<void (cubpacking::unpacker &upk)>;
      using server_request_id = MsgId;

      request_server () = delete;
      request_server (channel &&chn);
      request_server (const request_server &) = delete;
      request_server (request_server &&other);
      ~request_server ();

      void start_thread ();	  // start thread that receives and handles requests
      void stop_thread ();	  // stop the thread

      void register_request_handler (MsgId msgid, const server_request_handler &handler);	  // register a handler

      const channel &get_channel () const;						  // get underlying channel

      bool is_thread_started () const;

    protected:
      channel m_channel;	  // request are received on this channel

    private:
      using request_handlers_container = std::map<MsgId, server_request_handler>;

      void loop_handle_requests ();	    // the thread loop to receive and handle requests
      bool has_request_on_channel ();
      // get request buffer and its size from channel
      css_error_code receive_request_buffer (std::unique_ptr<char[]> &message_buffer, size_t &message_size);
      // get request from buffer and call its handler
      void handle_request (std::unique_ptr<char[]> &message_buffer, size_t message_size);

      std::thread m_thread;				// thread that loops and handles requests
      bool m_shutdown = true;					// set to true when thread must stop
      request_handlers_container m_request_handlers;	// request handler map
  };

  // Both a client and a server using same channel. Client messages and server messages can have different specializations
  // Extend the request_server with the client interface
  template <typename ClientMsgId, typename ServerMsgId = ClientMsgId>
  class request_client_server : public request_server<ServerMsgId>
  {
    public:
      using client_request_id = ClientMsgId;

      request_client_server (channel &&chn);
      request_client_server (const request_client_server &) = delete;
      request_client_server (request_client_server &&other) = default;

      request_client_server &operator= (const request_client_server &) = delete;
      request_client_server &operator= (request_client_server &&) = delete;

      template <typename ... PackableArgs>
      css_error_code send (ClientMsgId msgid, const PackableArgs &... args);
  };

  // Helper function that packs MsgId & PackableArgs and sends them on chn
  template <typename MsgId, typename ... PackableArgs>
  css_error_code send_client_request (channel &chn, MsgId msgid, const PackableArgs &... args);

  // Err logging functions
  void er_log_send_request (const channel &chn, int msgid, size_t size);
  void er_log_recv_request (const channel &chn, int msgid, size_t size);
  void er_log_send_fail (const channel &chn, css_error_code err);
  void er_log_recv_fail (const channel &chn, css_error_code err);
  void er_log_thread_started (const void *instance_ptr, const void *thread_ptr, std::thread::id thread_id);
  void er_log_thread_finished (const void *instance_ptr, const void *thread_ptr, std::thread::id thread_id);
}

namespace cubcomm
{
  // --- request_client ---
  template <typename MsgId>
  request_client<MsgId>::request_client (channel &&chn)
    : m_channel (std::move (chn))
  {
  }

  template <typename MsgId>
  template <typename ... PackableArgs>
  css_error_code request_client<MsgId>::send (MsgId msgid, const PackableArgs &... args)
  {
    return send_client_request (m_channel, msgid, args...);
  }

  template <typename MsgId>
  const channel &request_client<MsgId>::get_channel () const
  {
    return m_channel;
  }

  // --- request_server ---
  template <typename MsgId>
  request_server<MsgId>::request_server (channel &&chn)
    : m_channel (std::move (chn))
  {
  }

  template <typename MsgId>
  request_server<MsgId>::request_server (request_server &&other)
  {
    // only movable if in idle phase
    assert (!m_thread.joinable ());
    assert (!other.m_thread.joinable ());

    m_channel = std::move (other.m_channel);
    m_request_handlers = std::move (other.m_request_handlers);
  }

  template <typename MsgId>
  void request_server<MsgId>::register_request_handler (MsgId msgid, const server_request_handler &handler)
  {
    const auto it = m_request_handlers.find (msgid);
    assert (it == m_request_handlers.cend ());
    if (it == m_request_handlers.cend ())
      {
	m_request_handlers[msgid] = handler;
      }
  }

  template <typename MsgId>
  const channel &request_server<MsgId>::get_channel () const
  {
    return m_channel;
  }

  template <typename MsgId>
  void request_server<MsgId>::start_thread ()
  {
    assert (false == m_thread.joinable ());

    m_shutdown = false;
    m_thread = std::thread (&request_server::loop_handle_requests, std::ref (*this));

    er_log_thread_started (this, &m_thread, m_thread.get_id ());
  }

  template <typename MsgId>
  void request_server<MsgId>::stop_thread ()
  {
    assert (is_thread_started ());

    m_shutdown = true;
    m_thread.join ();
  }

  template <typename MsgId>
  bool request_server<MsgId>::is_thread_started () const
  {
    return m_thread.joinable ();
  }

  template <typename MsgId>
  void request_server<MsgId>::loop_handle_requests ()
  {
    std::unique_ptr<char[]> message_buffer;
    size_t message_size;
    while (!m_shutdown)
      {
	if (!has_request_on_channel ())
	  {
	    continue;
	  }
	if (receive_request_buffer (message_buffer, message_size) != NO_ERRORS)
	  {
	    break;
	  }
	handle_request (message_buffer, message_size);
      }
    er_log_thread_finished (this, &m_thread, m_thread.get_id ());
  }

  template <typename MsgId>
  bool request_server<MsgId>::has_request_on_channel ()
  {
    unsigned short events = POLLIN;
    unsigned short revents = 0;
    (void) m_channel.wait_for (events, revents);
    bool received_message = (revents & POLLIN) != 0;
    return received_message;
  }

  template <typename MsgId>
  css_error_code request_server<MsgId>::receive_request_buffer (std::unique_ptr<char[]> &message_buffer,
      size_t &message_size)
  {
    int ilen = 0;
    css_error_code err = m_channel.recv_int (ilen);
    if (err != NO_ERRORS)
      {
	er_log_recv_fail (m_channel, err);
	return err;
      }

    size_t expected_size = static_cast<size_t> (ilen);
    message_buffer.reset (new char [ilen]);

    size_t receive_size = expected_size;
    err = m_channel.recv (message_buffer.get (), receive_size);
    if (err != NO_ERRORS)
      {
	er_log_recv_fail (m_channel, err);
	return err;
      }
    assert (receive_size == expected_size);

    message_size = receive_size;

    return NO_ERRORS;
  }

  template <typename MsgId>
  void request_server<MsgId>::handle_request (std::unique_ptr<char[]> &message_buffer, size_t message_size)
  {
    assert (message_size >= OR_INT_SIZE);

    cubpacking::unpacker upk (message_buffer.get (), message_size);
    MsgId msgid;
    upk.unpack_from_int (msgid);
    auto req_handle_it = m_request_handlers.find (msgid);
    if (req_handle_it == m_request_handlers.end ())
      {
	// no such handler
	assert (false);
	return;
      }
    er_log_recv_request (m_channel, static_cast<int> (msgid), message_size);
    req_handle_it->second (upk);
  }

  template <typename MsgId>
  request_server<MsgId>::~request_server ()
  {
    m_shutdown = true;
    m_channel.close_connection ();
    if (m_thread.joinable ())
      {
	m_thread.join ();
      }
  }

  // --- request_client_server ---
  template <typename ClientMsgId, typename ServerMsgId>
  request_client_server<ClientMsgId, ServerMsgId>::request_client_server (channel &&chn)
    : request_server<ServerMsgId>::request_server (std::move (chn))
  {
  }

  template <typename ClientMsgId, typename ServerMsgId>
  template <typename ... PackableArgs>
  css_error_code request_client_server<ClientMsgId, ServerMsgId>::send (ClientMsgId msgid,
      const PackableArgs &... args)
  {
    return send_client_request (this->request_server<ServerMsgId>::m_channel, msgid, args...);
  }

  template <typename MsgId, typename ... PackableArgs>
  css_error_code send_client_request (channel &chn, MsgId msgid, const PackableArgs &... args)
  {
    packing_packer packer;
    cubmem::extensible_block eb;
    packer.set_buffer_and_pack_all (eb, static_cast<int> (msgid), args...);

    er_log_send_request (chn, static_cast<int> (msgid), packer.get_current_size ());

    const css_error_code css_size_err = chn.send_int (static_cast<int> (packer.get_current_size ()));
    if (css_size_err != NO_ERRORS)
      {
	er_log_send_fail (chn, css_size_err);
	return css_size_err;
      }

    const css_error_code css_err = chn.send (eb.get_ptr (), packer.get_current_size ());
    if (css_err != NO_ERRORS)
      {
	er_log_send_fail (chn, css_err);
      }
    return css_err;
  }
}

#endif // _SERVER_SERVER_HPP_
