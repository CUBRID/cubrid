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
#include "log_replication.hpp"
#include "packer.hpp"
#include "server_type.hpp"
#include "system_parameter.h"

#include <cassert>
#include <cstring>
#include <functional>

page_server ps_Gl;

static void assert_page_server_type ();

page_server::~page_server ()
{
  assert (m_replicator == nullptr);
  assert (m_ats == nullptr);
}

void page_server::set_active_tran_server_connection (cubcomm::channel &&chn)
{
  assert_page_server_type ();

  chn.set_channel_name ("ATS_PS_comm");

  assert (m_ats == nullptr);
  m_ats.reset (new ats_t ());

  m_ats->init (std::move (chn));

  m_ats->register_request_handler (ats_to_ps_request::SEND_LOG_PRIOR_LIST,
				   std::bind (&page_server::receive_log_prior_list, std::ref (*this),
				       std::placeholders::_1));
  m_ats->register_request_handler (ats_to_ps_request::SEND_LOG_PAGE_FETCH,
				   std::bind (&page_server::receive_log_page_fetch, std::ref (*this),
				       std::placeholders::_1));
  m_ats->register_request_handler (ats_to_ps_request::SEND_DATA_PAGE_FETCH,
				   std::bind (&page_server::receive_data_page_fetch, std::ref (*this),
				       std::placeholders::_1));
  m_ats->connect ();
  er_log_debug (ARG_FILE_LINE, "Active transaction server connected to this page server. Channel id: %s.\n",
		"ATS_PS_comm");
}

void page_server::disconnect_active_tran_server ()
{
  m_ats->disconnect ();
  m_ats.reset (nullptr);
}

bool page_server::is_active_tran_server_connected () const
{
  assert_page_server_type ();

  return m_ats != nullptr && m_ats->is_connected ();
}

void page_server::receive_log_prior_list (cubpacking::unpacker &upk)
{
  std::string message;
  upk.unpack_string (message);
  log_Gl.m_prior_recver.push_message (std::move (message));
}

void page_server::receive_log_page_fetch (cubpacking::unpacker &upk)
{
  LOG_PAGEID pageid;
  std::string message;

  upk.unpack_string (message);
  assert (message.size () == sizeof (pageid));
  std::memcpy (&pageid, message.c_str (), sizeof (pageid));

  if (prm_get_bool_value (PRM_ID_ER_LOG_READ_LOG_PAGE))
    {
      _er_log_debug (ARG_FILE_LINE, "Received request for log from Transaction Server. Page ID: %lld \n", pageid);
    }

  assert (m_log_page_fetcher);
  m_log_page_fetcher->fetch_page (pageid, std::bind (&page_server::on_log_page_read_result, this, std::placeholders::_1,
				  std::placeholders::_2));
}

void page_server::receive_data_page_fetch (cubpacking::unpacker &upk)
{
  if (prm_get_bool_value (PRM_ID_ER_LOG_READ_DATA_PAGE))
    {
      VPID vpid;
      std::string message;

      upk.unpack_string (message);
      std::memcpy (&vpid, message.c_str (), sizeof (vpid));
      _er_log_debug (ARG_FILE_LINE, "Received request for Data Page from Transaction Server. pageid: %ld volid: %d\n",
		     vpid.pageid, vpid.volid);
    }
}

void page_server::push_request_to_active_tran_server (ps_to_ats_request reqid, std::string &&payload)
{
  assert_page_server_type ();
  assert (is_active_tran_server_connected ());

  m_ats->push (reqid, std::move (payload));
}

void page_server::on_log_page_read_result (const LOG_PAGE *log_page, int error_code)
{
  char buffer[sizeof (int) + IO_MAX_PAGE_SIZE];
  std::memcpy (buffer, &error_code, sizeof (error_code));
  std::size_t buffer_size = sizeof (error_code);

  if (error_code == NO_ERROR)
    {
      std::memcpy (buffer + sizeof (error_code), log_page, db_log_page_size ());
      buffer_size += db_log_page_size ();
    }

  std::string message (buffer, buffer_size);
  m_ats->push (ps_to_ats_request::SEND_LOG_PAGE, std::move (message));

  if (prm_get_bool_value (PRM_ID_ER_LOG_READ_LOG_PAGE))
    {
      LOG_PAGEID page_id = NULL_PAGEID;
      if (error_code == NO_ERROR)
	{
	  page_id = log_page->hdr.logical_pageid;
	}

      _er_log_debug (ARG_FILE_LINE, "Sending log page to Active Tran Server. Page ID: %lld Error code: %ld\n", page_id,
		     error_code);
    }
}
void
page_server::start_log_replicator (const log_lsa &start_lsa)
{
  assert_page_server_type ();
  assert (m_replicator == nullptr);

  m_replicator.reset (new cublog::replicator (start_lsa));
}

void
page_server::finish_replication_during_shutdown (cubthread::entry &thread_entry)
{
  assert (m_replicator != nullptr);

  logpb_force_flush_pages (&thread_entry);
  m_replicator->wait_replication_finish_during_shutdown ();
  m_replicator.reset (nullptr);
}

void
page_server::init_log_page_fetcher ()
{
  m_log_page_fetcher.reset (new cublog::async_page_fetcher ());
}

void
page_server::finalize_log_page_fetcher ()
{
  m_log_page_fetcher.reset ();
}


void
assert_page_server_type ()
{
  assert (get_server_type () == SERVER_TYPE::SERVER_TYPE_PAGE);
}

