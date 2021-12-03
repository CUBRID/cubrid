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
    private:

      // Wrap the user payload with a response sequence number
      using response_sequence_number_t = std::uint64_t;
      static constexpr response_sequence_number_t NO_RESPONSE =
	      std::numeric_limits<response_sequence_number_t>::max ();

    public:
      using outgoing_msg_id_t = T_OUTGOING_MSG_ID;
      using request_client_server_t = cubcomm::request_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID>;

      class internal_payload : public cubpacking::packable_object
      {
	public:
	  internal_payload () = default;
	  internal_payload (response_sequence_number_t a_rsn, T_PAYLOAD &&a_payload);
	  internal_payload (internal_payload &&other);
	  internal_payload (const internal_payload &other) = delete;
	  ~internal_payload () = default;

	  internal_payload &operator= (internal_payload &&other);
	  internal_payload &operator= (const internal_payload &) = delete;

	  void push_payload (T_PAYLOAD &&a_payload);
	  T_PAYLOAD pull_payload ();

	  size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset = 0) const final override;
	  void pack (cubpacking::packer &serializator) const final override;
	  void unpack (cubpacking::unpacker &deserializator) final override;

	private:
	  response_sequence_number_t m_rsn = NO_RESPONSE;
	  T_PAYLOAD m_user_payload;
      };

      using incoming_request_handler_t = std::function<void (internal_payload &)>;

    public:
      request_sync_client_server (cubcomm::channel &&a_channel,
				  std::map<T_INCOMING_MSG_ID, incoming_request_handler_t> &&a_incoming_request_handlers);
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

    private:
      using request_sync_send_queue_t = cubcomm::request_sync_send_queue<request_client_server_t, internal_payload>;
      using request_queue_autosend_t = cubcomm::request_queue_autosend<request_sync_send_queue_t>;

      void unpack_and_handle (cubpacking::unpacker &deserializator, const incoming_request_handler_t &handler);

    private:
      std::unique_ptr<request_client_server_t> m_conn;
      std::unique_ptr<request_sync_send_queue_t> m_queue;
      std::unique_ptr<request_queue_autosend_t> m_queue_autosend;
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
	  std::map<T_INCOMING_MSG_ID, incoming_request_handler_t> &&a_incoming_request_handlers)
    : m_conn { new request_client_server_t (std::move (a_channel)) }
  , m_queue { new request_sync_send_queue_t (*m_conn) }
  , m_queue_autosend { new request_queue_autosend_t (*m_queue) }
  {
    assert (a_incoming_request_handlers.size () > 0);
    for (const auto &pair: a_incoming_request_handlers)
      {
	assert (pair.second != nullptr);
	request_client_server_t::server_request_handler converted_handler =
		std::bind (&request_sync_client_server::unpack_and_handle, std::ref (*this), std::placeholders::_1, pair.second);
	m_conn->register_request_handler (pair.first, converted_handler);
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

    internal_payload ip (NO_RESPONSE, std::move (a_payload));

    m_queue->push (a_outgoing_message_id, std::move (ip));
  }

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  void
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::unpack_and_handle (
	  cubpacking::unpacker &deserializator, const incoming_request_handler_t &handler)
  {
    internal_payload ip;

    ip.unpack (deserializator);
    handler (ip);
  }

  //
  // Internal payload
  //
  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::internal_payload::internal_payload (
	  response_sequence_number_t a_rsn, T_PAYLOAD &&a_payload)
    : m_rsn (a_rsn)
  {
    push_payload (std::move (a_payload));
  }

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::internal_payload::internal_payload (
	  internal_payload &&other)
    : m_rsn (std::move (other.m_rsn))
    , m_user_payload (std::move (other.m_user_payload))
  {
    other.m_rsn = NO_RESPONSE;
  }

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  typename request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::internal_payload &
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::internal_payload::operator=
  (internal_payload &&other)
  {
    if (this != &other)
      {
	m_rsn = std::move (other.m_rsn);
	m_user_payload = std::move (other.m_user_payload);

	other.m_rsn = NO_RESPONSE;
      }
    return *this;
  }

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  void
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::internal_payload::push_payload (
	  T_PAYLOAD &&a_payload)
  {
    m_user_payload = std::move (a_payload);
  }

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  typename T_PAYLOAD
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::internal_payload::pull_payload ()
  {
    return std::move (m_user_payload);
  }

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  size_t
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::internal_payload::get_packed_size (
	  cubpacking::packer &serializator, std::size_t start_offset /*= 0*/) const
  {
    return serializator.get_all_packed_size_starting_offset (start_offset, m_rsn, m_user_payload);
  }

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  void
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::internal_payload::pack (
	  cubpacking::packer &serializator) const
  {
    serializator.pack_all (m_rsn, m_user_payload);
  }

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  void
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::internal_payload::unpack (
	  cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_all (m_rsn, m_user_payload);
  }
}

#endif // REQUEST_SYNC_CLIENT_SERVER_HPP
