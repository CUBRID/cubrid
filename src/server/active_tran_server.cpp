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
active_tran_server::get_oldest_active_mvccid_from_page_server ()
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

log_lsa
active_tran_server::compute_consensus_lsa ()
{
  const int total_node_cnt = m_page_server_conn_vec.size ();
  const int quorum = total_node_cnt / 2 + 1; // For now, it's fixed to the number of the majority.
  int cur_node_cnt = 0;
  std::vector<log_lsa> collected_saved_lsa;

  for (const auto &conn : m_page_server_conn_vec)
    {
      if (conn->is_connected ())
	{
	  collected_saved_lsa.emplace_back (conn->get_saved_lsa ());
	  cur_node_cnt++;
	}
    }

  if (cur_node_cnt < quorum)
    {
      quorum_consenesus_er_log ("compute_consensus_lsa - Quorum unsatisfied: total node count = %d, curreunt node count = %d, quorum = %d\n",
				total_node_cnt, cur_node_cnt, quorum);
      return NULL_LSA;
    }
  /*
   * Gather all PS'es saved_lsa and sort it in ascending order.
   * The <cur_node_count - quorum>'th element is the consensus LSA, upon which the majority (quorumn) of PS agrees.
   * total: 5, cur: 5 - [5, 5, 6, 9, 10] -> "6" is the consensus LSA.
   * total: 2, cur: 2 - [9, 10] -> "9"
   * total: 5, cur: 4 - [5, 6, 9, 10] -> "6"
   * total: 3, cur: 2 - [9, 10] -> "9"
   */
  std::sort (collected_saved_lsa.begin (), collected_saved_lsa.end ());

  const auto consensus_lsa = collected_saved_lsa[cur_node_cnt - quorum];

  if (prm_get_bool_value (PRM_ID_ER_LOG_QUORUM_CONSENSUS))
    {
      constexpr int BUF_SIZE = 1024;
      char msg_buf[BUF_SIZE];
      int n = 0;
      // cppcheck-suppress [wrongPrintfScanfArgNum]
      n = snprintf (msg_buf, BUF_SIZE,
		    "compute_consensus_lsa - total node count = %d, current node count = %d, quorum = %d, consensus LSA = %lld|%d\n",
		    total_node_cnt, cur_node_cnt, quorum, LSA_AS_ARGS (&consensus_lsa));
      n += snprintf (msg_buf + n, BUF_SIZE - n, "Collected saved lsa list = [ ");
      for (const auto &lsa : collected_saved_lsa)
	{
	  // cppcheck-suppress [wrongPrintfScanfArgNum]
	  n += snprintf (msg_buf + n, BUF_SIZE - n, "%lld|%d ", LSA_AS_ARGS (&lsa));
	}
      snprintf (msg_buf + n, BUF_SIZE - n, "]\n");
      assert (n < BUF_SIZE);

      quorum_consenesus_er_log ("%s", msg_buf);
    }

  return consensus_lsa;
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

active_tran_server::connection_handler::connection_handler (tran_server &ts, cubcomm::node &&node)
  : tran_server::connection_handler (ts, std::move (node))
  ,m_saved_lsa { NULL_LSA }
{
}

active_tran_server::connection_handler::~connection_handler ()
{
  assert (m_prior_sender_sink_hook_func == nullptr);
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
active_tran_server::connection_handler::receive_saved_lsa (page_server_conn_t::sequenced_payload &&a_sp)
{
  std::string message = a_sp.pull_payload ();
  log_lsa saved_lsa;

  assert (sizeof (log_lsa) == message.size ());
  std::memcpy (&saved_lsa, message.c_str (), sizeof (log_lsa));

  assert (saved_lsa >= get_saved_lsa ()); // PS can send the same saved_lsa as before in some cases
  if (saved_lsa > get_saved_lsa ())
    {
      m_saved_lsa.store (saved_lsa);
      log_Gl.wakeup_ps_flush_waiters ();
    }

  quorum_consenesus_er_log ("Received saved LSA = %lld|%d from %s.\n", LSA_AS_ARGS (&saved_lsa),
			    get_channel_id ().c_str ());
}

log_lsa
active_tran_server::connection_handler::get_saved_lsa () const
{
  return m_saved_lsa.load ();
}

void
active_tran_server::connection_handler::on_connecting ()
{
  assert (m_prior_sender_sink_hook_func == nullptr);

  m_prior_sender_sink_hook_func = std::bind (&active_tran_server::connection_handler::prior_sender_sink_hook, this,
				  std::placeholders::_1);
  log_Gl.m_prior_sender.add_sink (m_prior_sender_sink_hook_func);
}

void
active_tran_server::connection_handler::on_disconnecting ()
{
  if (m_prior_sender_sink_hook_func != nullptr)
    {
      log_Gl.m_prior_sender.remove_sink (m_prior_sender_sink_hook_func);
      m_prior_sender_sink_hook_func = nullptr;
    }
}

void
active_tran_server::connection_handler::prior_sender_sink_hook (std::string &&message)
{
  assert (message.size () > 0);

  push_request_regardless_of_state (tran_to_page_request::SEND_LOG_PRIOR_LIST, std::move (message));
}

active_tran_server::connection_handler *
active_tran_server::create_connection_handler (tran_server &ts, cubcomm::node &&node) const
{
  // active_tran_server::connection_handler
  return new connection_handler (ts, std::move (node));
}
