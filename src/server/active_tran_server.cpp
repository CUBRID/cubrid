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
#include "server_type.hpp"
#include "system_parameter.h"

#include <cassert>
#include <cstring>
#include <functional>
#include <string>
#include <utility>

bool
active_tran_server::uses_remote_storage () const
{
  return m_uses_remote_storage;
}

MVCCID
active_tran_server::get_oldest_active_mvccid_from_page_server () const
{
  std::string response_message;
  const int error_code = send_receive (tran_to_page_request::GET_OLDEST_ACTIVE_MVCCID, std::string (), response_message);
  if (error_code != NO_ERROR)
    {
      return MVCCID_NULL;
    }

  MVCCID oldest_mvccid = MVCCID_NULL;
  std::memcpy (&oldest_mvccid, response_message.c_str (), sizeof (oldest_mvccid));

  /*
   * MVCCID_ALL_VISIBLE means the PS is waiting for a PTS which is connected but haven't updated its value.
   * See page_server::pts_mvcc_tracker::init_oldest_active_mvccid().
   *
   * It could be also MVCCID_LAST which means there is no PTS.
   */
  assert (MVCCID_IS_NORMAL (oldest_mvccid) || MVCCID_ALL_VISIBLE == oldest_mvccid);

  return oldest_mvccid;
}

bool
active_tran_server::get_remote_storage_config ()
{
  m_uses_remote_storage = prm_get_bool_value (PRM_ID_REMOTE_STORAGE);
  return m_uses_remote_storage;
}

void
active_tran_server::on_boot ()
{
  assert (is_active_transaction_server ());
}

void
active_tran_server::stop_outgoing_page_server_messages ()
{
}

active_tran_server::connection_handler::connection_handler (cubcomm::channel &&chn, tran_server &ts)
  : tran_server::connection_handler (std::move (chn), ts, get_request_handlers ())
{
  m_prior_sender_sink_hook_func = std::bind (&tran_server::connection_handler::push_request, this,
				  tran_to_page_request::SEND_LOG_PRIOR_LIST, std::placeholders::_1);
  log_Gl.m_prior_sender.add_sink (m_prior_sender_sink_hook_func);
}

active_tran_server::connection_handler::request_handlers_map_t
active_tran_server::connection_handler::get_request_handlers ()
{
  /* start from the request handlers in common on ATS and PTS */
  auto handlers_map = tran_server::connection_handler::get_request_handlers ();

  auto saved_lsa_handler = std::bind (&active_tran_server::connection_handler::receive_saved_lsa, this,
				      std::placeholders::_1);

  handlers_map.insert (std::make_pair (page_to_tran_request::SEND_SAVED_LSA, saved_lsa_handler));

  return handlers_map;
}

void
active_tran_server::connection_handler::disconnect ()
{
  remove_prior_sender_sink ();
  tran_server::connection_handler::disconnect ();
}

void
active_tran_server::connection_handler::receive_saved_lsa (page_server_conn_t::sequenced_payload &a_ip)
{
  std::string message = a_ip.pull_payload ();
  log_lsa saved_lsa;

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
active_tran_server::connection_handler::remove_prior_sender_sink ()
{
  /*
   * Now, it's removed only when disconencting all page servers during shutdown.
   * TODO: used when abnormal or normal disonnection of PS. It may need a latch.
   */
  if (static_cast<bool> (m_prior_sender_sink_hook_func))
    {
      log_Gl.m_prior_sender.remove_sink (m_prior_sender_sink_hook_func);
      m_prior_sender_sink_hook_func = nullptr;
    }
}

active_tran_server::connection_handler *
active_tran_server::create_connection_handler (cubcomm::channel &&chn, tran_server &ts) const
{
  // active_tran_server::connection_handler
  return new connection_handler (std::move (chn), ts);
}
