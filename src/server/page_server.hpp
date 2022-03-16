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

#ifndef _PAGE_SERVER_HPP_
#define _PAGE_SERVER_HPP_

#include "log_replication.hpp"
#include "log_storage.hpp"
#include "server_request_responder.hpp"
#include "request_sync_client_server.hpp"
#include "tran_page_requests.hpp"

#include <memory>

/* Sequence diagram of server-server communication:
 *
 *                                                 #
 *                                     TRAN SERVER # PAGE SERVER
 *                                                 #
 *                 ┌───────────────────────────┐   #   ┌────────────────────────────────────────────┐
 *                 │request_sync_client_server │   #   │connection_handler                          │
 *    ┌────┐  (1)  │                           │   #   │                                            │
 * ┌──► Ti ├───────┼─────┐                     │   #   │ ┌───────────────────────────┐              │
 * │  └────┘       │     │                     │   #   │ │request_sync_client_server │              │
 * │               │     │   ┌────────────┐    │   #   │ │(per tran server)          │  ┌─────────┐ │
 * │               │     ├───►  send      │    │(2)#   │ │   ┌─────────────┐  (3)    │  │server   │ │
 * │  ┌────┐ (1)   │     │   │  thread    ├────┼───────┼─┼───►  receive    ├─────────┼──►request  │ │
 * ├──► Tx ├───────┼─────┤   └────────────┘    │   #   │ │   │   thread    │         │  │responder│ │
 * │  └────┘       │     │                     │   #   │ │   └─────────────┘         │  │thread   │ │
 * │               │     │                     │   #   │ │                       (4) │  │pool     │ │
 * │               │     │     ┌──────────┐    │   #   │ │                      ┌────┼──┤         │ │
 * │  ┌────┐  (1)  │     │     │ receive  │    │   #(5)│ │   ┌─────────────┐    │    │  └─────────┘ │
 * ├──► Td ├───────┼─────┘     │  thread  ◄────┼───────┼─┼───┤   send      │    │    │              │
 * │  └────┘       │           └─┬────────┘    │   #   │ │   │   thread    ◄────┘    │              │
 * │               │             │             │   #   │ │   └─────────────┘         │              │
 * │               └─────────────┼─────────────┘   #   │ │                           │              │
 * │       (6)                   │                 #   │ └───────────────────────────┘              │
 * └─────────────────────────────┘                 #   │                                            │
 *                                                 #   └────────────────────────────────────────────┘
 *                                                 #
 *
 * (1)  transactions or system threads in a transaction server produce requests which require resources from
 *      page server (eg: heap pages, log pages)
 * (2)  these requests are serialized into requests for a send thread (request_sync_client_server,
 *      request_sync_send_queue, request_queue_autosend) - the actual send thread is instantiated in
 *      request_queue_autosend - which then sends requests over the network;
 *      there are two two types of requests
 *      - that wait for a response from the other side
 *      - fire&forget messages (eg: log prior messages being sent from active transaction server to page server)
 * (3)  on the page server messages are processed by a receive thread (request_sync_client_server -
 *      request_client_server) with the receive thread actually being instantiated in request_client_server;
 *      the receiving thread has a handler map which, together with the message's request id results in a
 *      certain handler function to be called
 *      received messages are being processed in two modes:
 *      - synchronously: some messages make no sense to be processed asynchronously so they are processed
 *        directly within the connection handler instance
 *      - asynchronously: connection handler dispatches the message for processing and response retrieval
 *        to a thread pool via task (server_request_responder)
 * (4)  the async server request responder then redirects the response to the sending thread of page server
 * (5)  which sends it over the network to the transaction server side
 * (6)  the receive thread on the transaction server side use also the message's request id and a handler map
 *      to know which waiting thread to actually wake to consume the response
 */

class page_server
{
  private:
    class connection_handler;
    using connection_handler_uptr_t = std::unique_ptr<connection_handler>;

  public:
    page_server () = default;
    page_server (const page_server &) = delete;
    page_server (page_server &&) = delete;

    ~page_server ();

    page_server &operator = (const page_server &) = delete;
    page_server &operator = (page_server &&) = delete;

    void set_active_tran_server_connection (cubcomm::channel &&chn);
    void set_passive_tran_server_connection (cubcomm::channel &&chn);
    void disconnect_all_tran_server ();
    void push_request_to_active_tran_server (page_to_tran_request reqid, std::string &&payload);
    cublog::replicator &get_replicator ();
    void start_log_replicator (const log_lsa &start_lsa);
    void finish_replication_during_shutdown (cubthread::entry &thread_entry);

    void init_request_responder ();
    void finalize_request_responder ();

  private:

    void disconnect_active_tran_server ();
    void disconnect_tran_server_async (connection_handler *conn);
    bool is_active_tran_server_connected () const;

    class connection_handler
    {
      public:
	using tran_server_conn_t =
		cubcomm::request_sync_client_server<page_to_tran_request, tran_to_page_request, std::string>;

	connection_handler () = delete;
	connection_handler (cubcomm::channel &chn, page_server &ps);

	connection_handler (const connection_handler &) = delete;
	connection_handler (connection_handler &&) = delete;

	~connection_handler ();

	connection_handler &operator= (const connection_handler &) = delete;
	connection_handler &operator= (connection_handler &&) = delete;

	void push_request (page_to_tran_request id, std::string msg);
	std::string get_channel_id ();

      private:

	// Request handlers for the request server:
	void receive_boot_info_request (tran_server_conn_t::sequenced_payload &a_ip);
	void receive_log_prior_list (tran_server_conn_t::sequenced_payload &a_ip);
	void receive_log_page_fetch (tran_server_conn_t::sequenced_payload &a_ip);
	void receive_data_page_fetch (tran_server_conn_t::sequenced_payload &a_ip);
	void receive_disconnect_request (tran_server_conn_t::sequenced_payload &a_ip);
	void receive_log_boot_info_fetch (tran_server_conn_t::sequenced_payload &a_ip);
	void receive_stop_log_prior_dispatch (tran_server_conn_t::sequenced_payload &a_sp);

	void abnormal_tran_server_disconnect (css_error_code error_code, bool &abort_further_processing);

	// Helper function to convert above functions into responder specific tasks.
	template<class F, class ... Args>
	void push_async_response (F &&, tran_server_conn_t::sequenced_payload &&a_sp, Args &&...args);

	// Function used as sink for log transfer
	void prior_sender_sink_hook (std::string &&message) const;

      private:
	std::unique_ptr<tran_server_conn_t> m_conn;
	page_server &m_ps;

	// only passive transaction servers receive log in the form of prior list;
	cublog::prior_sender::sink_hook_t m_prior_sender_sink_hook_func;

	// exclusive lock between the hook function that executes the dispatch and the
	// function that will, at some moment, remove that hook
	mutable std::mutex m_prior_sender_sink_removal_mtx;

	std::mutex m_abnormal_tran_server_disconnect_mtx;
	bool m_abnormal_tran_server_disconnect;
    };

    /* helper class with the task of destroying connnection handlers and, by this,
     * also waiting for the receive and transmit threads inside the handlers to terminate
     */
    class async_disconnect_handler
    {
      public:
	async_disconnect_handler ();

	async_disconnect_handler (const async_disconnect_handler &) = delete;
	async_disconnect_handler (async_disconnect_handler &&) = delete;

	~async_disconnect_handler ();

	async_disconnect_handler &operator = (const async_disconnect_handler &) = delete;
	async_disconnect_handler &operator = (async_disconnect_handler &&) = delete;

	void disconnect (connection_handler_uptr_t &&handler);
	void terminate ();

      private:
	void disconnect_loop ();

      private:
	std::atomic_bool m_terminate;
	std::queue<connection_handler_uptr_t> m_disconnect_queue;
	std::mutex m_queue_mtx;
	std::condition_variable m_queue_cv;
	std::thread m_thread;
    };

    using responder_t = server_request_responder<connection_handler::tran_server_conn_t>;
    responder_t &get_responder ();

    connection_handler_uptr_t m_active_tran_server_conn;
    std::vector<connection_handler_uptr_t> m_passive_tran_server_conn;

    std::unique_ptr<cublog::replicator> m_replicator;

    std::unique_ptr<responder_t> m_responder;

    async_disconnect_handler m_async_disconnect_handler;
};

extern page_server ps_Gl;

#endif // !_PAGE_SERVER_HPP_

