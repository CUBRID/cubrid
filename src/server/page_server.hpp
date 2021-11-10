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

#include "async_page_fetcher.hpp"
#include "request_sync_client_server.hpp"
#include "tran_page_requests.hpp"

#include <memory>

// forward declaration
namespace cublog
{
  class replicator;
}
namespace cubpacking
{
  class unpacker;
}
namespace cubthread
{
  class entry;
}

class page_server
{
  private:
    class connection_handler;
  public:
    page_server () = default;
    ~page_server ();

    void set_active_tran_server_connection (cubcomm::channel &&chn);
    void set_passive_tran_server_connection (cubcomm::channel &&chn);
    void disconnect_tran_server (connection_handler *conn);
    void disconnect_all_tran_server ();
    bool is_active_tran_server_connected () const;
    bool is_passive_tran_server_connected () const;
    void push_request_to_active_tran_server (page_to_tran_request reqid, std::string &&payload);
    cublog::async_page_fetcher *get_page_fetcher ();
    cublog::replicator &get_replicator ();
    void start_log_replicator (const log_lsa &start_lsa);
    void finish_replication_during_shutdown (cubthread::entry &thread_entry);

    void init_page_fetcher ();
    void finalize_page_fetcher ();

  private:
    class connection_handler
    {
	using tran_server_conn_t =
		cubcomm::request_sync_client_server<page_to_tran_request, tran_to_page_request, std::string>;

      public:
	connection_handler () = delete;
	~connection_handler () = default;
	connection_handler (cubcomm::channel &chn, page_server &ps);
	void push_request (page_to_tran_request id, std::string msg);
	std::string get_channel_id ();

      private:
	void on_log_page_read_result (const LOG_PAGE *log_page, int error_code);
	void on_data_page_read_result (const FILEIO_PAGE *page_ptr, int error_code);
	void receive_boot_info_request (cubpacking::unpacker &upk);
	void receive_log_prior_list (cubpacking::unpacker &upk);
	void receive_log_page_fetch (cubpacking::unpacker &upk);
	void receive_data_page_fetch (cubpacking::unpacker &upk);
	void receive_disconnect_request (cubpacking::unpacker &upk);

	std::unique_ptr<tran_server_conn_t> m_conn;
	page_server &m_ps;
    };

    std::unique_ptr<connection_handler> m_active_tran_server_conn;
    std::vector<std::unique_ptr<connection_handler>> m_passive_tran_server_conn;

    std::unique_ptr<cublog::replicator> m_replicator;
    std::unique_ptr<cublog::async_page_fetcher> m_page_fetcher;
};

extern page_server ps_Gl;

#endif // !_PAGE_SERVER_HPP_

