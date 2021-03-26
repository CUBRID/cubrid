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

#include "page_server.hpp"

#include "error_manager.h"
#include "log_impl.h"
#include "log_prior_recv.hpp"
#include "packer.hpp"
#include "server_type.hpp"
#include "system_parameter.h"

#include <cassert>
#include <cstring>
#include <functional>

page_server ps_Gl;

static void assert_page_server_type();

page_server::~page_server()
{
        disconnect_active_tran_server();
}

void page_server::set_active_tran_server_connection(cubcomm::channel &&chn)
{
        assert_page_server_type();

        chn.set_channel_name("ATS_PS_comm");
        er_log_debug(ARG_FILE_LINE, "Active transaction server connected to this page server. Channel id: %s.\n",
                     chn.get_channel_id().c_str());

        m_ats_conn.reset(new active_tran_server_conn(std::move(chn)));
        m_ats_conn->register_request_handler(ats_to_ps_request::SEND_LOG_PRIOR_LIST,
                                             std::bind(&page_server::receive_log_prior_list, std::ref(*this),
                                                       std::placeholders::_1));
        m_ats_conn->register_request_handler(ats_to_ps_request::SEND_LOG_PAGE_FETCH,
                                             std::bind(&page_server::receive_log_page_fetch, std::ref(*this),
                                                       std::placeholders::_1));
        m_ats_conn->register_request_handler(ats_to_ps_request::SEND_DATA_PAGE_FETCH,
                                             std::bind(&page_server::receive_data_page_fetch, std::ref(*this),
                                                       std::placeholders::_1));

        m_ats_conn->register_request_handler(ats_to_ps_request::SEND_DATA_PAGE_FETCH,
                                             std::bind(&page_server::receive_data_page_fetch, std::ref(*this),
                                                       std::placeholders::_1));

        m_ats_conn->start_thread();

        m_ats_request_queue.reset(new active_tran_server_request_queue(*m_ats_conn));
        m_ats_request_autosend.reset(new active_tran_server_request_autosend(*m_ats_request_queue));
        m_ats_request_autosend->start_thread();
}

void page_server::disconnect_active_tran_server()
{
        m_ats_request_autosend.reset(nullptr);
        m_ats_request_queue.reset(nullptr);
        m_ats_conn.reset(nullptr);
}

bool page_server::is_active_tran_server_connected() const
{
        assert_page_server_type();

        return m_ats_conn != nullptr;
}

void page_server::receive_log_prior_list(cubpacking::unpacker &upk)
{
        std::string message;
        upk.unpack_string(message);
        log_Gl.m_prior_recver.push_message(std::move(message));
}

void page_server::receive_log_page_fetch(cubpacking::unpacker &upk)
{
        LOG_PAGEID pageid;
        std::string message;

        upk.unpack_string(message);
        std::memcpy(&pageid, message.c_str(), sizeof(pageid));
        assert(message.size() == sizeof(pageid));

        if (prm_get_bool_value(PRM_ID_ER_LOG_READ_LOG_PAGE))
        {
                _er_log_debug(ARG_FILE_LINE, "Received request for log from Transaction Server. Page ID: %lld \n", pageid);
        }

        if (!m_log_page_fetcher)
        {
                m_log_page_fetcher.reset(new cublog::async_page_fetcher());
        }
        m_log_page_fetcher->fetch_page(pageid, std::bind(&page_server::on_log_page_read_result, this, std::placeholders::_1, std::placeholders::_2));
}

void page_server::receive_data_page_fetch(cubpacking::unpacker &upk)
{
        if (prm_get_bool_value(PRM_ID_ER_LOG_READ_DATA_PAGE))
        {
                VPID vpid;
                std::string message;

                upk.unpack_string(message);
                std::memcpy(&vpid, message.c_str(), sizeof(vpid));
                _er_log_debug(ARG_FILE_LINE, "Received request for Data Page from Transaction Server. pageid: %ld volid: %d\n",
                              vpid.pageid, vpid.volid);
        }
}

void page_server::push_request_to_active_tran_server(ps_to_ats_request reqid, std::string &&payload)
{
        assert_page_server_type();
        assert(is_active_tran_server_connected());

        m_ats_request_queue->push(reqid, std::move(payload));
}

void page_server::on_log_page_read_result(const LOG_PAGE *log_page, int error_code)
{
        char buffer[sizeof(int) + db_log_page_size()];
        std::memcpy(buffer, &error_code, sizeof(error_code));
        std::size_t bufferSize = sizeof(error_code);

        if (error_code == NO_ERROR)
        {
                std::memcpy(buffer + sizeof(error_code), & (log_page->hdr), sizeof (log_page->hdr));
                bufferSize += sizeof (log_page->hdr);
        }

        std::string message(buffer, bufferSize);
        m_ats_request_queue->push(ps_to_ats_request::SEND_LOG_PAGE, std::move(message));

        if (prm_get_bool_value(PRM_ID_ER_LOG_READ_LOG_PAGE))
        {
                _er_log_debug(ARG_FILE_LINE, "Sending log page to Active Tran Server. Page ID: %ld \n", log_page->hdr.logical_pageid);
        }
}

void assert_page_server_type()
{
        assert(get_server_type() == SERVER_TYPE::SERVER_TYPE_PAGE);
}
