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
#include "request_sync_client_server.hpp"
#include "tran_page_requests.hpp"

#include <string>
#include <vector>

// forward declaration
namespace cubpacking
{
  class unpacker;
}

// Transaction server class hierarchies:
//
//                              tran_server
//                               /      |
//                              /       |
//                             /        |
//             active_tran_server     passive_tran_server
//
//
// tran_server:
//   the base class that defines the common public interface for the transaction servers. The common parts are:
//      - Connection management
//      - Page brokers
//      - Booting
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
    tran_server () = delete;
    tran_server (cubcomm::server_server conn_type)
      : m_conn_type (conn_type)
    {
    }
    tran_server (const tran_server &) = delete;
    tran_server (tran_server &&) = delete;

    virtual ~tran_server ();

    tran_server &operator = (const tran_server &) = delete;
    tran_server &operator = (tran_server &&) = delete;

    int boot (const char *db_name);

    void disconnect_page_server ();
    bool is_page_server_connected () const;
    void push_request (tran_to_page_request reqid, std::string &&payload);
    int send_receive (tran_to_page_request reqid, std::string &&payload_in, std::string &payload_out);

    virtual bool uses_remote_storage () const;

  protected:
    using page_server_conn_t = cubcomm::request_sync_client_server<tran_to_page_request, page_to_tran_request, std::string>;
    using request_handlers_map_t = std::map<page_to_tran_request, page_server_conn_t::incoming_request_handler_t>;

  protected:

    // Booting functions that require specialization
    virtual bool get_remote_storage_config () = 0;
    virtual void on_boot () = 0;

    // Before disconnecting page server, make sure no message is being sent anymore to the page server.
    virtual void stop_outgoing_page_server_messages () = 0;

    virtual request_handlers_map_t get_request_handlers ();

  private:

    int init_page_server_hosts (const char *db_name);
    int get_boot_info_from_page_server ();
    int connect_to_page_server (const cubcomm::node &node, const char *db_name);

    int parse_server_host (const std::string &host);
    int parse_page_server_hosts_config (std::string &hosts);

  private:

    std::vector<cubcomm::node> m_connection_list;
    cubcomm::server_server m_conn_type;
    std::vector<std::unique_ptr<page_server_conn_t>> m_page_server_conn_vec;
};

#endif // !_tran_server_HPP_
