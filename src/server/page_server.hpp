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
#include "request_sync_client_server.hpp"
#include "server_request_responder.hpp"
#include "server_type_enum.hpp"
#include "tran_page_requests.hpp"
#include "async_disconnect_handler.hpp"

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
  public:
    page_server (const char *db_name)
      : m_server_name { db_name }
    { }
    page_server (const page_server &) = delete;
    page_server (page_server &&) = delete;

    ~page_server ();

    page_server &operator = (const page_server &) = delete;
    page_server &operator = (page_server &&) = delete;

    void set_active_tran_server_connection (cubcomm::channel &&chn);
    void set_passive_tran_server_connection (cubcomm::channel &&chn);
    void set_follower_page_server_connection (cubcomm::channel &&chn);
    void disconnect_all_tran_servers ();

    int connect_to_followee_page_server (std::string &&hostname, int32_t port);

    void push_request_to_active_tran_server (page_to_tran_request reqid, std::string &&payload);
    cublog::replicator &get_replicator ();
    void start_log_replicator (const log_lsa &start_lsa);
    void finish_replication_during_shutdown (cubthread::entry &thread_entry);

    void init_request_responder ();
    void finalize_request_responder ();

  private: // types
    class tran_server_connection_handler
    {
      public:
	using tran_server_conn_t =
		cubcomm::request_sync_client_server<page_to_tran_request, tran_to_page_request, std::string>;

	tran_server_connection_handler () = delete;
	tran_server_connection_handler (cubcomm::channel &&chn, transaction_server_type server_type, page_server &ps);

	tran_server_connection_handler (const tran_server_connection_handler &) = delete;
	tran_server_connection_handler (tran_server_connection_handler &&) = delete;

	~tran_server_connection_handler ();

	tran_server_connection_handler &operator= (const tran_server_connection_handler &) = delete;
	tran_server_connection_handler &operator= (tran_server_connection_handler &&) = delete;

	void push_request (page_to_tran_request id, std::string &&msg);
	const std::string &get_connection_id () const;

	void remove_prior_sender_sink ();

	// request disconnection of this connection (TS)
	void push_disconnection_request ();

      private:
	// Request handlers for the request server:
	void receive_boot_info_request (tran_server_conn_t::sequenced_payload &&a_sp);
	void receive_log_page_fetch (tran_server_conn_t::sequenced_payload &&a_sp);
	void receive_data_page_fetch (tran_server_conn_t::sequenced_payload &&a_sp);
	void receive_disconnect_request (tran_server_conn_t::sequenced_payload &&a_sp);
	void receive_log_prior_list (tran_server_conn_t::sequenced_payload &&a_sp);
	void handle_oldest_active_mvccid_request (tran_server_conn_t::sequenced_payload &&a_sp);
	void receive_log_boot_info_fetch (tran_server_conn_t::sequenced_payload &&a_sp);
	void receive_stop_log_prior_dispatch (tran_server_conn_t::sequenced_payload &&a_sp);
	void receive_oldest_active_mvccid (tran_server_conn_t::sequenced_payload &&a_sp);
	void receive_start_catch_up (tran_server_conn_t::sequenced_payload &&a_sp);

	void abnormal_tran_server_disconnect (css_error_code error_code, bool &abort_further_processing);

	// Helper function to convert above functions into responder specific tasks.
	template<class F, class ... Args>
	void push_async_response (F &&, tran_server_conn_t::sequenced_payload &&a_sp, Args &&...args);

	// Function used as sink for log transfer
	void prior_sender_sink_hook (std::string &&message) const;

      private:
	/* there is another mode in which the connection handler for active transaction server
	 * can be differentiated from the connection handler for passive transaction server: the
	 * presence of prior sender sink hook function pointer below;
	 * however, at some point, the hook function will be removed - following a request from
	 * the peer transaction server and the check will no longer be valid
	 */
	const transaction_server_type m_server_type;
	const std::string m_connection_id;

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

    /*
     *  TODO add some explanation and diagrams for this.
     */
    class follower_connection_handler
    {
      public:
	using follower_server_conn_t =
		cubcomm::request_sync_client_server<followee_to_follower_request, follower_to_followee_request, std::string>;

	follower_connection_handler () = delete;
	follower_connection_handler (cubcomm::channel &&chn, page_server &ps);

	follower_connection_handler (const follower_connection_handler &) = delete;
	follower_connection_handler (follower_connection_handler &&) = delete;

	follower_connection_handler &operator= (const follower_connection_handler &) = delete;
	follower_connection_handler &operator= (follower_connection_handler &&) = delete;

	~follower_connection_handler ();

      private:
	void receive_log_pages_fetch (follower_server_conn_t::sequenced_payload &&a_sp);

	void serve_log_pages (THREAD_ENTRY &, std::string &payload_in_out);

	page_server &m_ps;
	std::unique_ptr<follower_server_conn_t> m_conn;
    };

    class followee_connection_handler
    {
      public:
	using followee_server_conn_t =
		cubcomm::request_sync_client_server<follower_to_followee_request, followee_to_follower_request, std::string>;

	followee_connection_handler () = delete;
	followee_connection_handler (cubcomm::channel &&chn, page_server &ps);

	followee_connection_handler (const followee_connection_handler &) = delete;
	followee_connection_handler (followee_connection_handler &&) = delete;

	followee_connection_handler &operator= (const followee_connection_handler &) = delete;
	followee_connection_handler &operator= (followee_connection_handler &&) = delete;

	~followee_connection_handler ();

	void start_catchup (const LOG_LSA catchup_lsa);

      private:
	void push_request (follower_to_followee_request reqid, std::string &&msg);
	int send_receive (follower_to_followee_request reqid, std::string &&payload_in, std::string &payload_out);

	int request_log_pages (LOG_PAGEID start_pageid, int count, std::vector<LOG_PAGE *> &log_pages_out);

	void execute_catchup (const LOG_LSA catchup_lsa);

      private:
	page_server &m_ps;
	std::unique_ptr<followee_server_conn_t> m_conn;

	std::thread m_catchup_thread;
	std::atomic<bool> m_terminated;
    };

    /*
     * helper class to track the active oldest mvccids of each Page Transaction Server.
     * This provides the globally oldest active mvcc id to the vacuum on ATS.
     * The vacuum has to take mvcc status of all PTSes into considerations,
     * or it would clean up some data seen by a active snapshot on a PTS.
     */
    class pts_mvcc_tracker
    {
      public:
	pts_mvcc_tracker () = default;

	pts_mvcc_tracker (const pts_mvcc_tracker &) = delete;
	pts_mvcc_tracker (pts_mvcc_tracker &&) = delete;

	pts_mvcc_tracker &operator = (const pts_mvcc_tracker &) = delete;
	pts_mvcc_tracker &operator = (pts_mvcc_tracker &&) = delete;

	void init_oldest_active_mvccid (const std::string &pts_channel_id);
	void update_oldest_active_mvccid (const std::string &pts_channel_id, const MVCCID mvccid);
	void delete_oldest_active_mvccid (const std::string &pts_channel_id);

	MVCCID get_global_oldest_active_mvccid ();

      private:
	/* <channel_id -> the oldest active mvccid of the PTS>. used by the vacuum on the ATS */
	std::unordered_map<std::string, MVCCID> m_pts_oldest_active_mvccids;
	std::mutex m_pts_oldest_active_mvccids_mtx;
    };

  private:
    using tran_server_connection_handler_uptr_t = std::unique_ptr<tran_server_connection_handler>;
    using follower_connection_handler_uptr_t = std::unique_ptr<follower_connection_handler>;
    using followee_connection_handler_uptr_t = std::unique_ptr<followee_connection_handler>;

    using tran_server_responder_t = server_request_responder<tran_server_connection_handler::tran_server_conn_t>;
    using follower_responder_t = server_request_responder<follower_connection_handler::follower_server_conn_t>;

  private: // functions that depend on private types
    void disconnect_active_tran_server ();
    void disconnect_tran_server_async (const tran_server_connection_handler *conn);
    bool is_active_tran_server_connected () const;

    tran_server_responder_t &get_tran_server_responder ();
    follower_responder_t &get_follower_responder ();

  private: // members
    const std::string m_server_name;

    tran_server_connection_handler_uptr_t m_active_tran_server_conn;
    std::vector<tran_server_connection_handler_uptr_t> m_passive_tran_server_conn;
    std::mutex m_conn_mutex; // for the thread-safe connection and disconnection
    std::condition_variable m_conn_cv;

    std::unique_ptr<cublog::replicator> m_replicator;

    std::unique_ptr<tran_server_responder_t> m_tran_server_responder;
    std::unique_ptr<follower_responder_t> m_follower_responder;

    async_disconnect_handler<tran_server_connection_handler> m_async_disconnect_handler;
    pts_mvcc_tracker m_pts_mvcc_tracker;

    followee_connection_handler_uptr_t m_followee_conn;
    std::vector<follower_connection_handler_uptr_t> m_follower_conn_vec;
};

#endif // !_PAGE_SERVER_HPP_
