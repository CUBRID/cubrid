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

#include "ats_ps_request.hpp"
#include "communication_node.hpp"
#include "page_broker.hpp"
#include "request_sync_client_server.hpp"
#include "server_type.hpp"

#include <memory>
#include <string>
#include <vector>

// forward declaration
namespace cubpacking
{
  class unpacker;
}

class active_tran_server
{
  public:
    active_tran_server () = default;
    active_tran_server (const active_tran_server &) = delete;
    active_tran_server (active_tran_server &&) = delete;

    ~active_tran_server ();

    active_tran_server &operator = (const active_tran_server &) = delete;
    active_tran_server &operator = (active_tran_server &&) = delete;

    int init_page_server_hosts (const char *db_name);
    void disconnect_page_server ();
    bool is_page_server_connected () const;

    void init_page_brokers ();
    void finalize_page_brokers ();

    page_broker<log_page_type> &get_log_page_broker ();
    page_broker<data_page_type> &get_data_page_broker ();

    bool uses_remote_storage () const;

    void push_request (ats_to_ps_request reqid, std::string &&payload);

  private:
    using page_server_conn_t =
	    cubcomm::request_sync_client_server<ats_to_ps_request, ps_to_ats_request, std::string>;

  private:
    int connect_to_page_server (const cubcomm::node &node, const char *db_name);

    int parse_server_host (const std::string &host);
    int parse_page_server_hosts_config (std::string &hosts);
    void receive_saved_lsa (cubpacking::unpacker &upk);
    void receive_log_page (cubpacking::unpacker &upk);
    void receive_data_page (cubpacking::unpacker &upk);

  private:
    // communication with page server
    std::string m_ps_hostname;
    int m_ps_port = -1;

    std::vector<std::unique_ptr<page_server_conn_t>> m_page_server_conn_vec;

    std::unique_ptr<page_broker<log_page_type>> m_log_page_broker;
    std::unique_ptr<page_broker<data_page_type>> m_data_page_broker;
    std::vector<cubcomm::node> m_connection_list;

    bool m_uses_remote_storage = false;
};

extern active_tran_server ats_Gl;

#endif // !_ACTIVE_TRAN_SERVER_HPP_

