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

#include <vector>
#include <memory>

class active_tran_server : public tran_server
{
  public:
    active_tran_server () : tran_server (cubcomm::server_server::CONNECT_ACTIVE_TRAN_TO_PAGE_SERVER)
    {
    }

    bool uses_remote_storage () const final override;
    MVCCID get_oldest_active_mvccid_from_page_server () const;
    void stop_outgoing_page_server_messages () final override;

  private:
    class connection_handler : public tran_server::connection_handler
    {
      public:
	connection_handler () = delete;
	connection_handler (cubcomm::channel &&chn, tran_server &ts);

	connection_handler (const connection_handler &) = delete;
	connection_handler (connection_handler &&) = delete;

	connection_handler &operator= (const connection_handler &) = delete;
	connection_handler &operator= (connection_handler &&) = delete;

	void disconnect () final override;

      private:
	request_handlers_map_t get_request_handlers () final override;
	void remove_prior_sender_sink ();

	// request handlers
	void receive_saved_lsa (page_server_conn_t::sequenced_payload &a_ip);

      private:
	cublog::prior_sender::sink_hook_t m_prior_sender_sink_hook_func;
    };

  private:
    bool get_remote_storage_config () final override;

    connection_handler *create_connection_handler (cubcomm::channel &&chn,
	tran_server &ts) const final override;

  private:
    bool m_uses_remote_storage = false;
};

#endif // !_ACTIVE_TRAN_SERVER_HPP_

