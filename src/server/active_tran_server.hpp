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

#ifndef _ACTIVE_TRAN_SERVER_HPP_
#define _ACTIVE_TRAN_SERVER_HPP_

#include "log_prior_send.hpp"
#include "tran_server.hpp"

#include <atomic>
#include <memory>
#include <vector>

class active_tran_server : public tran_server
{
  public:
    active_tran_server () : tran_server (cubcomm::server_server::CONNECT_ACTIVE_TRAN_TO_PAGE_SERVER)
    {
    }

    bool uses_remote_storage () const final override;
    MVCCID get_oldest_active_mvccid_from_page_server ();
    log_lsa compute_consensus_lsa ();

  private:
    class connection_handler : public tran_server::connection_handler
    {
      public:
	connection_handler () = delete;
	connection_handler (tran_server &ts, cubcomm::node &&node);

	connection_handler (const connection_handler &) = delete;
	connection_handler (connection_handler &&) = delete;

	connection_handler &operator= (const connection_handler &) = delete;
	connection_handler &operator= (connection_handler &&) = delete;

	~connection_handler () override;

      private:
	request_handlers_map_t get_request_handlers () final override;

	// request handlers
	void receive_saved_lsa (page_server_conn_t::sequenced_payload &&a_sp);
	void receive_catchup_complete (page_server_conn_t::sequenced_payload &&a_sp);

	// a request only used internally
	void send_start_catch_up_request (std::string &&host, int32_t port, LOG_LSA &&catchup_lsa);

	log_lsa get_saved_lsa () const override final;

	void on_connecting () override final;
	void on_disconnecting () override final;

	// Function used as sink for log transfer
	void prior_sender_sink_hook (std::string &&message);

      private:
	cublog::prior_sender::sink_hook_t m_prior_sender_sink_hook_func;
	std::atomic<log_lsa> m_saved_lsa;
    };

  private:
    bool get_remote_storage_config () final override;

    void stop_outgoing_page_server_messages () final override;
    connection_handler *create_connection_handler (tran_server &ts, cubcomm::node &&node) const final override;

  private:
    bool m_uses_remote_storage = false;
};

#endif // !_ACTIVE_TRAN_SERVER_HPP_

