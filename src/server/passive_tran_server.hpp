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

#ifndef _PASSIVE_TRAN_SERVER_HPP_
#define _PASSIVE_TRAN_SERVER_HPP_

#include "log_replication.hpp"
#include "tran_server.hpp"

class passive_tran_server : public tran_server
{
  public:
    passive_tran_server () : tran_server (cubcomm::server_server::CONNECT_PASSIVE_TRAN_TO_PAGE_SERVER)
    {
    }
    ~passive_tran_server () override;

  public:
    void send_and_receive_log_boot_info (THREAD_ENTRY *thread_p);
    void start_log_replicator (const log_lsa &start_lsa);

  public:
    /* read current replicator progress */
    log_lsa get_replicator_lsa () const;
    void finish_replication_during_shutdown (cubthread::entry &thread_entry);

  private:
    bool uses_remote_storage () const final override;
    bool get_remote_storage_config () final override;
    void on_boot () final override;
    request_handlers_map_t get_request_handlers () final override;

    void receive_log_boot_info (cubpacking::unpacker &upk);
    void receive_log_prior_list (cubpacking::unpacker &upk);

  private:
    std::mutex m_log_boot_info_mtx;
    std::string m_log_boot_info;
    std::condition_variable m_log_boot_info_condvar;

    std::unique_ptr<cublog::replicator> m_replicator;
};

#endif // !_passive_tran_server_HPP_
