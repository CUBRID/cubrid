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

class page_server
{
  private:
    class connection_handler;

  public:
    page_server () = default;
    ~page_server ();

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
    void disconnect_tran_server (connection_handler *conn);
    bool is_active_tran_server_connected () const;

    class connection_handler
    {
      public:
	using tran_server_conn_t =
		cubcomm::request_sync_client_server<page_to_tran_request, tran_to_page_request, std::string>;

	connection_handler () = delete;
	~connection_handler ();
	connection_handler (cubcomm::channel &chn, page_server &ps);
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

	void abnormal_tran_server_disconnect (css_error_code error_code);

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
    };

    using responder_t = server_request_responder<connection_handler::tran_server_conn_t>;
    responder_t &get_responder ();

    std::unique_ptr<connection_handler> m_active_tran_server_conn;
    std::vector<std::unique_ptr<connection_handler>> m_passive_tran_server_conn;

    std::unique_ptr<cublog::replicator> m_replicator;

    std::unique_ptr<responder_t> m_responder;
};

extern page_server ps_Gl;

#endif // !_PAGE_SERVER_HPP_

