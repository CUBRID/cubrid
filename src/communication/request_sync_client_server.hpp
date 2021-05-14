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
    public:
      using request_client_server_t = cubcomm::request_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID>;
      using incoming_request_handler_t = typename request_client_server_t::server_request_handler;

    public:
      request_sync_client_server () = default;
      ~request_sync_client_server ();
      request_sync_client_server (const request_sync_client_server &) = delete;
      request_sync_client_server (request_sync_client_server &&) = delete;

      request_sync_client_server &operator = (const request_sync_client_server &) = delete;
      request_sync_client_server &operator = (request_sync_client_server &&) = delete;

    public:
      /* initialization functions should be called in mandatory order:
       *   - init
       *   - register_request_handler [x many]
       *   - connect
       */
      void init (cubcomm::channel &&a_channel);
      void register_request_handler (T_INCOMING_MSG_ID a_incoming_message_id,
				     const incoming_request_handler_t &a_incoming_request_handler);
      void connect ();

      /* disconnect function expected to be called before dtor is hit
       */
      void disconnect ();

      bool is_connected () const;

      void push (T_OUTGOING_MSG_ID a_outgoing_message_id,
		 T_PAYLOAD &&a_payload);

    private:
      using request_sync_send_queue_t = cubcomm::request_sync_send_queue<request_client_server_t, T_PAYLOAD>;
      using request_queue_autosend_t = cubcomm::request_queue_autosend<request_sync_send_queue_t>;

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
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::~request_sync_client_server ()
  {
    assert (false == is_connected ());
  }

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  void
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::init (
	  cubcomm::channel &&a_channel)
  {
    assert (m_conn == nullptr);
    assert (m_queue == nullptr);
    assert (m_queue_autosend == nullptr);

    // TODO: set_channel_name here, or outside (as is now)?

    m_conn.reset (new request_client_server_t (std::move (a_channel)));
    m_queue.reset (new request_sync_send_queue_t (*m_conn));
    m_queue_autosend.reset (new request_queue_autosend_t (*m_queue));
  }

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  void
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::register_request_handler (
	  T_INCOMING_MSG_ID a_incoming_message_id,
	  const typename request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::
	  incoming_request_handler_t &a_incoming_request_handler)
  {
    assert (m_conn != nullptr);
    assert (false == m_conn->is_connected ());

    m_conn->register_request_handler (a_incoming_message_id, a_incoming_request_handler);
  }

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  void
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::connect ()
  {
    assert (m_conn != nullptr);
    assert (m_queue != nullptr);
    assert (m_queue_autosend != nullptr);
    assert (m_conn->has_registered_handlers ());
    assert (false == m_conn->is_connected ());

    m_conn->start_thread ();
    m_queue_autosend->start_thread ();
  }

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  void
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::disconnect ()
  {
    m_queue_autosend.reset (nullptr);
    m_queue.reset (nullptr);
    m_conn.reset (nullptr);
  }

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  bool
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::is_connected () const
  {
    return m_conn != nullptr && m_conn->is_connected ();
  }

  template <typename T_OUTGOING_MSG_ID, typename T_INCOMING_MSG_ID, typename T_PAYLOAD>
  void
  request_sync_client_server<T_OUTGOING_MSG_ID, T_INCOMING_MSG_ID, T_PAYLOAD>::push (
	  T_OUTGOING_MSG_ID a_outgoing_message_id, T_PAYLOAD &&a_payload)
  {
    assert (m_conn != nullptr);
    assert (m_queue != nullptr);
    assert (m_queue_autosend != nullptr);
    assert (m_conn->has_registered_handlers ());
    assert (m_conn->is_connected ());

    m_queue->push (a_outgoing_message_id, std::move (a_payload));
  }
}

#endif // REQUEST_SYNC_CLIENT_SERVER_HPP
