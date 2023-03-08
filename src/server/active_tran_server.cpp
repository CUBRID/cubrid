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
active_tran_server::stop_outgoing_page_server_messages ()
{
}

active_tran_server::connection_handler::connection_handler (cubcomm::channel &&chn, page_server_node &node,
    tran_server &ts)
  : tran_server::connection_handler (std::move (chn), node, ts, get_request_handlers ())
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
  auto &node_vec = dynamic_cast<active_tran_server *> (&m_ts)->m_node_vec; // casting to access m_node_vec
  const auto total_node_cnt = node_vec.size();
  const auto quorum = total_node_cnt / 2 + 1; // For now, it's fixed to the number of the majority.
  std::vector<log_lsa> collected_saved_lsa;
  std::string message = a_ip.pull_payload ();
  log_lsa saved_lsa;

  assert (sizeof (log_lsa) == message.size ());
  std::memcpy (&saved_lsa, message.c_str (), sizeof (log_lsa));

  assert (saved_lsa > m_node.get_saved_lsa ()); // increasing monotonically
  m_node.set_saved_lsa (saved_lsa);

  /*
   * Gather all PS'es saved_lsa and sort it in descending order.
   * The "the total node count - quorum]'th element is the consensus LSA, upon which the majority (quorumn) of PS agrees.
   * [10, 9, "6", 5, 5] -> "6" is the consensus LSA.
   */
  for (const auto &node : node_vec)
    {
      collected_saved_lsa.emplace_back (node->get_saved_lsa ());
    }
  std::sort (collected_saved_lsa.begin(), collected_saved_lsa.end(),
	     std::greater<log_lsa>());

  log_lsa consensus_lsa = collected_saved_lsa[total_node_cnt - 1];

  if (log_Gl.m_ps_consensus_flushed_lsa < consensus_lsa)
    {
      log_Gl.update_ps_consensus_flushed_lsa (consensus_lsa);
    }

  if (prm_get_bool_value (PRM_ID_ER_LOG_COMMIT_CONFIRM))
    {
      std::stringstream ss;
      ss << "[COMMIT CONFIRM] Received saved LSA = " << saved_lsa.pageid << "|" << saved_lsa.offset << std::endl;
      ss << " Quorum = " << quorum << ", Collected saved lsa list = [ ";
      for (const auto &lsa : collected_saved_lsa)
	{
	  ss << lsa.pageid << "|" << lsa.offset << " ";
	}
      ss << "]" << std::endl;
      _er_log_debug (ARG_FILE_LINE, ss.str ().c_str ());
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
active_tran_server::create_connection_handler (cubcomm::channel &&chn, page_server_node &node, tran_server &ts) const
{
  // active_tran_server::connection_handler
  return new connection_handler (std::move (chn), node, ts);
}
