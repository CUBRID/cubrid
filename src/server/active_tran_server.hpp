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

#include "communication_node.hpp"
#include "page_broker.hpp"
#include "request_sync_client_server.hpp"
#include "server_type.hpp"
#include "tran_server.hpp"
#include "ts_ps_request.hpp"

#include <memory>
#include <string>
#include <vector>

// forward declaration
namespace cubpacking
{
  class unpacker;
}

class active_tran_server : public tran_server
{
  public:
    bool uses_remote_storage () const override;

  private:
    void on_boot () final;
    bool get_remote_storage_config () final;

    request_handlers_map_t get_request_handlers () final;

    void receive_saved_lsa (cubpacking::unpacker &upk);

  private:

    bool m_uses_remote_storage = false;
};

extern active_tran_server ats_Gl;

#endif // !_ACTIVE_TRAN_SERVER_HPP_

