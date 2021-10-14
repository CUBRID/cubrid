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

#ifndef _TRAN_SERVER_HPP_
#define _TRAN_SERVER_HPP_

#include "communication_node.hpp"
#include "communication_server_channel.hpp"
#include "page_broker.hpp"
#include "request_sync_client_server.hpp"
#include "server_type.hpp"
#include "ts_ps_request.hpp"

#include <memory>
#include <string>
#include <vector>

// forward declaration
namespace cubpacking
{
  class unpacker;
}

// Transaction server class hierarchies:
//
//                            tran_server
//                               /   \
//                              /     \
//                             /       \
//             active_tran_server     passive_tran_server
//
//
// tran_server_abstract:
//   the base class that defines the public interface for the engine and contains all the common parts for
//   passive/active transaction server.
//
// tran_server:
//   template class derived from tran_server_abstract, that also includes all connection handling that is common to
//   passive and active tran servers.
//
// active_tran_server:
//   class derived from tran_server that is specific only to active transaction server.
//
// passive_tran_server:
//   class derived from tran_server that is specific only to passive transaction server.
//
class tran_server
{
  public:
    tran_server () = default;
    tran_server (const tran_server &) = delete;
    tran_server (tran_server &&) = delete;

    virtual ~tran_server ();

    tran_server &operator = (const tran_server &) = delete;
    tran_server &operator = (tran_server &&) = delete;

    int boot (const char *db_name);

    void disconnect_page_server ();
    bool is_page_server_connected () const;
    void push_request (ts_to_ps_request reqid, std::string &&payload);

    virtual bool uses_remote_storage () const;

    void init_page_brokers ();
    void finalize_page_brokers ();
    page_broker<log_page_type> &get_log_page_broker ();
    page_broker<data_page_type> &get_data_page_broker ();

  protected:
    using page_server_conn_t = cubcomm::request_sync_client_server<ts_to_ps_request, ps_to_ts_request, std::string>;
    using request_handlers_map_t = std::map<ps_to_ts_request, typename page_server_conn_t::incoming_request_handler_t>;

  protected:

    // Booting functions that require specialization
    virtual bool get_remote_storage_config () = 0;
    virtual void on_boot () = 0;

    // Common request Handlers
    void receive_boot_info (cubpacking::unpacker &upk);
    void receive_log_page (cubpacking::unpacker &upk);
    void receive_data_page (cubpacking::unpacker &upk);

    virtual request_handlers_map_t get_request_handlers () = 0;

  private:

    int init_page_server_hosts (const char *db_name);
    void get_boot_info_from_page_server ();
    int connect_to_page_server (const cubcomm::node &node, const char *db_name);

    int parse_server_host (const std::string &host);
    int parse_page_server_hosts_config (std::string &hosts);


  private:
    std::unique_ptr<page_broker<log_page_type>> m_log_page_broker;
    std::unique_ptr<page_broker<data_page_type>> m_data_page_broker;

    std::vector<cubcomm::node> m_connection_list;
    cubcomm::server_server m_conn_type;
    std::vector<std::unique_ptr<page_server_conn_t>> m_page_server_conn_vec;

    std::mutex m_boot_info_mutex;
    std::condition_variable m_boot_info_condvar;
    bool m_is_boot_info_received = false;
};

#endif // !_tran_server_HPP_
