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
#include "request_sync_send_queue.hpp"

#include <string>

// forward declaration
namespace cubpacking
{
  class unpacker;
}

class active_tran_server
{
  public:
    using page_server_conn = cubcomm::request_client_server<ats_to_ps_request, ps_to_ats_request>;
    using page_server_request_queue = cubcomm::request_sync_send_queue<page_server_conn, std::string>;

    active_tran_server () = default;
    ~active_tran_server ();

    int init_page_server_hosts (const char *db_name);
    int connect_to_page_server (const std::string &host, int port, const char *db_name);
    void disconnect_page_server ();
    bool is_page_server_connected () const;

    void push_request (ats_to_ps_request reqid, std::string &&payload);

  private:
    using page_server_request_autosend = cubcomm::request_queue_autosend<page_server_request_queue>;

    void receive_saved_lsa (cubpacking::unpacker &upk);

    // communication with page server
    std::string m_ps_hostname;
    int m_ps_port = -1;
    page_server_conn *m_ps_conn = nullptr;
    page_server_request_queue *m_ps_request_queue = nullptr;
    page_server_request_autosend *m_ps_request_autosend = nullptr;
};

extern active_tran_server ats_Gl;

#endif // !_ACTIVE_TRAN_SERVER_HPP_
