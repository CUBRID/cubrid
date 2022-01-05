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

#include "response_broker.hpp"
#include "request_client_server.hpp"
#include "request_sync_send_queue.hpp"

//
// declarations
//
namespace cubcomm
{
  /* wrapper/helper encapsulating instances of request_client_server, request_sync_send_queue
   * and request_queue_autosend working together
   *
   * request_sync_client_server extends the request handling with the ability of sending requests awaiting responses
   * and the ability to respond.
   *
   * the request payloads are preceded by a response sequence number. the request sequence number is generated, packed
   * and unpacked by request_sync_client_server; its value is not transparent to the users making the requests and the
     responses. see nested class internal_payload.
   */
  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  class request_sync_client_server
  {
    private:

    public:
      using outgoing_msg_id_t = T_OUTGOING_MSG_ID;
      using request_client_server_t = cubcomm::request_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID>;
      using payload_t = T_PAYLOAD;

      // The user payload (of type T_PAYLOAD) is accompanied by a response sequence number set by
      // request_sync_client_server.
      class sequenced_payload;

      using incoming_request_handler_t = std::function<void (sequenced_payload &)>;

    public:
      request_sync_client_server (cubcomm::channel &&a_channel,
				  std::map<T_INCOMING_MSG_ID, incoming_request_handler_t> &&a_incoming_request_handlers,
				  T_OUTGOING_MSG_ID a_outgoing_response_msgid,
				  T_INCOMING_MSG_ID a_incoming_response_msgid, size_t response_partition_count);
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
      void send_recv (T_OUTGOING_MSG_ID a_outgoing_message_id, T_PAYLOAD &&a_request_payload, T_PAYLOAD &a_response_payload);
      void respond (sequenced_payload &&seq_payload);

    private:
      using request_sync_send_queue_t = cubcomm::request_sync_send_queue<request_client_server_t, sequenced_payload>;
      using request_queue_autosend_t = cubcomm::request_queue_autosend<request_sync_send_queue_t>;

      void unpack_and_handle (cubpacking::unpacker &deserializator, const incoming_request_handler_t &handler);
      void handle_response (sequenced_payload &seq_payload);
      void register_handler (T_INCOMING_MSG_ID msgid, const incoming_request_handler_t &handler);

    private:
      std::unique_ptr<request_client_server_t> m_conn;
      std::unique_ptr<request_sync_send_queue_t> m_queue;
      std::unique_ptr<request_queue_autosend_t> m_queue_autosend;

      T_OUTGOING_MSG_ID m_outgoing_response_msgid;
      T_INCOMING_MSG_ID m_incoming_response_msgid;
      response_sequence_number_generator m_rsn_generator;
      response_broker<T_PAYLOAD> m_response_broker;
  };

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  class request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::sequenced_payload
    : public cubpacking::packable_object
  {
    public:
      sequenced_payload () = default;
      sequenced_payload (response_sequence_number a_rsn, T_PAYLOAD &&a_payload);
      sequenced_payload (sequenced_payload &&other);
      sequenced_payload (const sequenced_payload &other) = delete;
      ~sequenced_payload () = default;

      sequenced_payload &operator= (sequenced_payload &&other);
      sequenced_payload &operator= (const sequenced_payload &) = delete;

      void push_payload (T_PAYLOAD &&a_payload);
      T_PAYLOAD pull_payload ();

      response_sequence_number get_response_sequence_number () const
      {
	return m_rsn;
      }

      size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset = 0) const final override;
      void pack (cubpacking::packer &serializator) const final override;
      void unpack (cubpacking::unpacker &deserializator) final override;

    private:
      response_sequence_number m_rsn = NO_RESPONSE_SEQUENCE_NUMBER;
      T_PAYLOAD m_user_payload;
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
	  T_OUTGOING_MSG_ID a_outgoing_response_msgid,
	  T_INCOMING_MSG_ID a_incoming_response_msgid, size_t response_partition_count)
    : m_conn { new request_client_server_t (std::move (a_channel)) }
  , m_queue { new request_sync_send_queue_t (*m_conn) }
  , m_queue_autosend { new request_queue_autosend_t (*m_queue) }
  , m_outgoing_response_msgid { a_outgoing_response_msgid }
  , m_incoming_response_msgid { a_incoming_response_msgid }
  , m_response_broker { response_partition_count }
  {
    assert (a_incoming_request_handlers.size () > 0);
    for (const auto &pair: a_incoming_request_handlers)
      {
	assert (pair.second != nullptr);
	register_handler (pair.first, pair.second);
      }
    // Add the handler for responses
    incoming_request_handler_t bound_response_handler =
	    std::bind (&request_sync_client_server::handle_response, this, std::placeholders::_1);
    register_handler (m_incoming_response_msgid, bound_response_handler);
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

    sequenced_payload ip (NO_RESPONSE_SEQUENCE_NUMBER, std::move (a_payload));

    m_queue->push (a_outgoing_message_id, std::move (ip));
  }

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  void
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::send_recv (
	  T_OUTGOING_MSG_ID a_outgoing_message_id, T_PAYLOAD &&a_request_payload, T_PAYLOAD &a_response_payload)
  {
    // Get a unique sequence number of response and group with the payload
    response_sequence_number rsn = m_rsn_generator.get_unique_number ();
    sequenced_payload seq_payload (rsn, std::move (a_request_payload));

    // Send the request
    m_queue->push (a_outgoing_message_id, std::move (seq_payload));

    // Get the answer
    a_response_payload = m_response_broker.get_response (rsn);
  }

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  void
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::respond (
	  sequenced_payload &&seq_payload)
  {
    m_queue->push (m_outgoing_response_msgid, std::move (seq_payload));
  }

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  void
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::handle_response (
	  sequenced_payload &seq_payload)
  {
    m_response_broker.register_response (seq_payload.get_response_sequence_number (),
					 std::move (seq_payload.pull_payload ()));
  }

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  void
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::unpack_and_handle (
	  cubpacking::unpacker &deserializator, const incoming_request_handler_t &handler)
  {
    sequenced_payload ip;

    ip.unpack (deserializator);
    handler (ip);
  }

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  void
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::register_handler (
	  T_INCOMING_MSG_ID msgid, const incoming_request_handler_t &handler)
  {
    typename request_client_server_t::server_request_handler converted_handler =
	    std::bind (&request_sync_client_server::unpack_and_handle, std::ref (*this), std::placeholders::_1, handler);
    m_conn->register_request_handler (msgid, converted_handler);
  }

  //
  // Internal payload
  //
  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::sequenced_payload::sequenced_payload (
	  response_sequence_number a_rsn, T_PAYLOAD &&a_payload)
    : m_rsn (a_rsn)
  {
    push_payload (std::move (a_payload));
  }

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::sequenced_payload::sequenced_payload (
	  sequenced_payload &&other)
    : m_rsn (std::move (other.m_rsn))
    , m_user_payload (std::move (other.m_user_payload))
  {
    other.m_rsn = NO_RESPONSE_SEQUENCE_NUMBER;
  }

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  typename request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::sequenced_payload &
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::sequenced_payload::operator= (
	  sequenced_payload &&other)
  {
    if (this != &other)
      {
	m_rsn = std::move (other.m_rsn);
	m_user_payload = std::move (other.m_user_payload);

	other.m_rsn = NO_RESPONSE_SEQUENCE_NUMBER;
      }
    return *this;
  }

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  void
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::sequenced_payload::push_payload (
	  T_PAYLOAD &&a_payload)
  {
    m_user_payload = std::move (a_payload);
  }

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  T_PAYLOAD
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::sequenced_payload::pull_payload ()
  {
    return std::move (m_user_payload);
  }

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  size_t
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::sequenced_payload::get_packed_size (
	  cubpacking::packer &serializator, std::size_t start_offset /*= 0*/) const
  {
    return serializator.get_all_packed_size_starting_offset (start_offset, m_rsn, m_user_payload);
  }

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  void
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::sequenced_payload::pack (
	  cubpacking::packer &serializator) const
  {
    serializator.pack_all (m_rsn, m_user_payload);
  }

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  void
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::sequenced_payload::unpack (
	  cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_all (m_rsn, m_user_payload);
  }
}

#endif // REQUEST_SYNC_CLIENT_SERVER_HPP
