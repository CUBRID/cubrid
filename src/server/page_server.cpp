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
#include <functional>

page_server ps_Gl;

static void assert_page_server_type ();

page_server::~page_server ()
{
  disconnect_active_tran_server ();
}

void
page_server::set_active_tran_server_connection (cubcomm::channel &&chn)
{
  assert_page_server_type ();

  chn.set_channel_name ("ATS_PS_comm");
  er_log_debug (ARG_FILE_LINE, "Active transaction server connected to this page server. Channel id: %s.\n",
		chn.get_channel_id ().c_str ());

  m_ats_conn = new active_tran_server_conn (std::move (chn));

  m_ats_conn->register_request_handler (ats_to_ps_request::SEND_LOG_PRIOR_LIST,
					std::bind (&page_server::receive_log_prior_list, std::ref (*this),
					    std::placeholders::_1));

  m_ats_conn->start_thread ();
}

void
page_server::disconnect_active_tran_server ()
{
  delete m_ats_conn;
  m_ats_conn = nullptr;
}

bool
page_server::is_active_tran_server_connected () const
{
  assert_page_server_type ();

  return m_ats_conn != nullptr;
}

void
page_server::receive_log_prior_list (cubpacking::unpacker &upk)
{
  std::string message;
  upk.unpack_string (message);

  if (prm_get_bool_value (PRM_ID_ER_LOG_PRIOR_TRANSFER))
    {
      _er_log_debug (ARG_FILE_LINE, "[LOG PRIOR TRANSFER] Received message with size %d.\n", message.size ());
    }
  log_Gl.m_prior_recver.push_message (std::move (message));
}

void
assert_page_server_type ()
{
  assert (get_server_type () == SERVER_TYPE::SERVER_TYPE_PAGE);
}