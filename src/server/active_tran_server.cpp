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

#include "active_tran_server.hpp"

#include "error_manager.h"
#include "log_impl.h"
#include "log_lsa.hpp"
#include "log_prior_send.hpp"
#include "server_type.hpp"
#include "system_parameter.h"

#include <cassert>
#include <cstring>
#include <functional>
#include <string>
#include <utility>

active_tran_server ats_Gl;

bool
active_tran_server::uses_remote_storage () const
{
  return m_uses_remote_storage;
}

bool
active_tran_server::get_remote_storage_config ()
{
  m_uses_remote_storage = prm_get_bool_value (PRM_ID_REMOTE_STORAGE);
  return m_uses_remote_storage;
}

void
active_tran_server::receive_saved_lsa (cubpacking::unpacker &upk)
{
  std::string message;
  log_lsa saved_lsa;

  upk.unpack_string (message);
  assert (sizeof (log_lsa) == message.size ());
  std::memcpy (&saved_lsa, message.c_str (), sizeof (log_lsa));

  if (log_Gl.m_max_ps_flushed_lsa < saved_lsa)
    {
      log_Gl.update_max_ps_flushed_lsa (saved_lsa);
    }

  if (prm_get_bool_value (PRM_ID_ER_LOG_COMMIT_CONFIRM))
    {
      _er_log_debug (ARG_FILE_LINE, "[COMMIT CONFIRM] Received LSA = %lld|%d.\n", LSA_AS_ARGS (&saved_lsa));
    }
}

void
active_tran_server::on_boot ()
{
  assert (is_active_transaction_server ());

  cublog::prior_sender::sink_hook sink =
	  std::bind (&active_tran_server::push_request, std::ref (*this), tran_to_page_request::SEND_LOG_PRIOR_LIST,
		     std::placeholders::_1);

  log_Gl.m_prior_sender.add_sink (sink);
}

active_tran_server::request_handlers_map_t
active_tran_server::get_request_handlers ()
{
  request_handlers_map_t::value_type saved_lsa_handler_value =
	  std::make_pair (page_to_tran_request::SEND_SAVED_LSA,
			  std::bind (&active_tran_server::receive_saved_lsa, std::ref (*this), std::placeholders::_1));

  std::map<page_to_tran_request, std::function<void (cubpacking::unpacker &upk)>> handlers_map =
	      tran_server::get_request_handlers();

  handlers_map.insert (saved_lsa_handler_value);

  return handlers_map;
}
