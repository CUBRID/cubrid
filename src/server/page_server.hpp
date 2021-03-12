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

#include "ats_ps_request.hpp"
#include "request_client_server.hpp"

// forward declaration
namespace cubpacking
{
  class unpacker;
}

class page_server
{
  public:
    using active_tran_server_conn = cubcomm::request_client_server<ps_to_ats_request, ats_to_ps_request>;

    page_server () = default;
    ~page_server ();

    void set_active_tran_server_connection (cubcomm::channel &&chn);
    void disconnect_active_tran_server ();
    bool is_active_tran_server_connected () const;

  private:
    void receive_log_prior_list (cubpacking::unpacker &upk);

    active_tran_server_conn *m_ats_conn = nullptr;
};

extern page_server ps_Gl;

#endif // !_PAGE_SERVER_HPP_
