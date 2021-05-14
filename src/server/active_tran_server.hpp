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

#include "log_page_broker.hpp"
#include "ats_ps_request.hpp"
#include "request_sync_client_server.hpp"

#include <memory>
#include <string>

// forward declaration
namespace cubpacking
{
  class unpacker;
}

class active_tran_server
{
  public:
    active_tran_server () = default;
    ~active_tran_server ();

    int init_page_server_hosts (const char *db_name);
    int connect_to_page_server (const std::string &host, int port, const char *db_name);
    void disconnect_page_server ();
    bool is_page_server_connected () const;

    void init_log_page_broker ();
    void finalize_log_page_broker ();

    cublog::page_broker &get_log_page_broker ();

    void push_request (ats_to_ps_request reqid, std::string &&payload);

  private:
    using ps_t = cubcomm::request_sync_client_server<ats_to_ps_request, ps_to_ats_request, std::string>;

    void receive_saved_lsa (cubpacking::unpacker &upk);
    void receive_log_page (cubpacking::unpacker &upk);

    // communication with page server
    std::string m_ps_hostname;
    int m_ps_port = -1;

    std::unique_ptr<ps_t> m_ps;

    std::unique_ptr<cublog::page_broker> m_log_page_broker;
};

extern active_tran_server ats_Gl;

#endif // !_ACTIVE_TRAN_SERVER_HPP_

