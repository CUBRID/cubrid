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

#ifndef REQUEST_SYNC_CLIENT_SERVER_HPP
#define REQUEST_SYNC_CLIENT_SERVER_HPP

#include "request_client_server.hpp"
#include "request_sync_send_queue.hpp"

#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>

//
// declarations
//
namespace cubcomm
{
  /* wrapper/helper encapsulating instances of request_client_server, request_sync_send_queue
   * and request_queue_autosend working together
   */
  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  class request_sync_client_server
  {
    public:
      using outgoing_msg_id_t = T_OUTGOING_MSG_ID;

      using request_client_server_t = cubcomm::request_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID>;
      using incoming_request_handler_t = typename request_client_server_t::server_request_handler;

      using response_id_t = std::uint64_t;

    public:
      request_sync_client_server (cubcomm::channel &&a_channel,
				  std::map<T_INCOMING_MSG_ID, incoming_request_handler_t> &&a_incoming_request_handlers,
				  T_OUTGOING_MSG_ID a_outgoing_response_msgid, T_INCOMING_MSG_ID a_incoming_response_msgid);
      ~request_sync_client_server ();
      request_sync_client_server (const request_sync_client_server &) = delete;
      request_sync_client_server (request_sync_client_server &&) = delete;

      request_sync_client_server &operator = (const request_sync_client_server &) = delete;
      request_sync_client_server &operator = (request_sync_client_server &&) = delete;

    public:
      void start ();

      /* only used by unit tests
       */
      std::string get_underlying_channel_id () const;

      void push (T_OUTGOING_MSG_ID a_outgoing_message_id, T_PAYLOAD &&a_payload);
      void send_receive (T_OUTGOING_MSG_ID a_outgoing_message_id, T_PAYLOAD &&a_payload);
      void respond (response_id_t a_rid, T_PAYLOAD &&a_payload);

    private:
      using underlaying_payload_t = std::pair<response_id_t,

	    using request_sync_send_queue_t = cubcomm::request_sync_send_queue<request_client_server_t, T_PAYLOAD>;
      using request_queue_autosend_t = cubcomm::request_queue_autosend<request_sync_send_queue_t>;

      response_id_t NO_RESPONSE = std::numeric_limits<response_id_t>::max ();

    private:
      std::unique_ptr<request_client_server_t> m_conn;
      std::unique_ptr<request_sync_send_queue_t> m_queue;
      std::unique_ptr<request_queue_autosend_t> m_queue_autosend;

      // Outgoing response management
      T_OUTGOING_MSG_ID m_outgoing_respond_msgid;     // a message ID to use for outgoing responses

      // Incoming response management
      T_INCOMING_MSG_ID m_incoming_respond_msgid;     // a message ID to use for handling incoming responses
      std::atomic<response_id_t> m_response_id_generator;   // generate ID's for future responses
      std::mutex m_response_mutex;
      std::condition_variable m_response_condvar;
      std::map<response_id_t, T_PAYLOAD> m_responses;
  };
}

//
// implementations
//
namespace cubcomm
{
  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::request_sync_client_server (
	  cubcomm::channel &&a_channel,
	  std::map<T_INCOMING_MSG_ID, incoming_request_handler_t> &&a_incoming_request_handlers,
	  T_OUTGOING_MSG_ID a_outgoing_response_msgid, T_INCOMING_MSG_ID a_incoming_response_msgid)
    : m_conn { new request_client_server_t (std::move (a_channel)) }
  , m_queue { new request_sync_send_queue_t (*m_conn) }
  , m_queue_autosend { new request_queue_autosend_t (*m_queue) }
  , m_outgoing_respond_msgid { a_outgoing_response_msgid }
  , m_incoming_respond_msgid { a_incoming_response_msgid }
  {
    assert (a_incoming_request_handlers.size () > 0);
    for (const auto &pair: a_incoming_request_handlers)
      {
	assert (pair.second != nullptr);
	m_conn->register_request_handler (pair.first, pair.second);
      }
  }

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  void
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::start ()
  {
    m_conn->start_thread ();
    m_queue_autosend->start_thread ();
  }

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::~request_sync_client_server ()
  {
    m_queue_autosend.reset (nullptr);
    m_queue.reset (nullptr);
    m_conn.reset (nullptr);
  }

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  std::string
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::get_underlying_channel_id () const
  {
    return m_conn->get_channel ().get_channel_id ();
  }

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  void
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::push (
	  T_OUTGOING_MSG_ID a_outgoing_message_id, T_PAYLOAD &&a_payload)
  {
    assert (m_conn != nullptr && m_conn->is_thread_started ());

    m_queue->push (a_outgoing_message_id, std::move (a_payload));
  }
}

#endif // REQUEST_SYNC_CLIENT_SERVER_HPP
