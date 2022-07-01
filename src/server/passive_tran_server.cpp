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

#include "log_impl.h"
#include "passive_tran_server.hpp"
#include "server_type.hpp"
#include "system_parameter.h"
#include "thread_manager.hpp"

passive_tran_server::~passive_tran_server ()
{
  assert (m_replicator == nullptr);
}

bool
passive_tran_server::uses_remote_storage () const
{
  return true;
}

bool
passive_tran_server::get_remote_storage_config ()
{
  return true;
}

void
passive_tran_server::on_boot ()
{
  assert (is_passive_transaction_server ());
}

tran_server::request_handlers_map_t
passive_tran_server::get_request_handlers ()
{
  std::map<page_to_tran_request, page_server_conn_t::incoming_request_handler_t> handlers_map =
	  tran_server::get_request_handlers ();

  auto from_ps_log_prior_list_handler = std::bind (&passive_tran_server::receive_log_prior_list,
					std::ref (*this), std::placeholders::_1);
  handlers_map.insert (std::make_pair (page_to_tran_request::SEND_TO_PTS_LOG_PRIOR_LIST,
				       from_ps_log_prior_list_handler));

  return handlers_map;
}

void
passive_tran_server::receive_log_prior_list (page_server_conn_t::sequenced_payload &a_ip)
{
  std::string message = a_ip.pull_payload ();
  log_Gl.get_log_prior_receiver ().push_message (std::move (message));
}

int
passive_tran_server::send_and_receive_log_boot_info (THREAD_ENTRY *thread_p,
    log_lsa &most_recent_trantable_snapshot_lsa)
{
  std::string log_boot_info;

  const int error_code = send_receive (tran_to_page_request::SEND_LOG_BOOT_INFO_FETCH, std::string (),
				       log_boot_info);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  assert (!log_boot_info.empty ());

  const int log_page_size = db_log_page_size ();
  const char *message_buf = log_boot_info.c_str ();

  // log header, copy and initialize header
  const log_header *const log_hdr = reinterpret_cast<const log_header *> (message_buf);
  log_Gl.hdr = *log_hdr;
  message_buf += sizeof (log_header);

  // log append
  assert (log_Gl.append.log_pgptr == nullptr);
  /* fetch the append page; append pages are always new pages */
  log_Gl.append.log_pgptr = logpb_create_page (thread_p, log_Gl.hdr.append_lsa.pageid);
  std::memcpy (reinterpret_cast<char *> (log_Gl.append.log_pgptr), message_buf, log_page_size);
  message_buf += log_page_size;

  // prev lsa
  std::memcpy (&log_Gl.append.prev_lsa, message_buf, sizeof (log_lsa));
  message_buf += sizeof (log_lsa);

  // most recent trantable snapshot lsa
  std::memcpy (reinterpret_cast<char *> (&most_recent_trantable_snapshot_lsa),
	       message_buf, sizeof (log_lsa));
  message_buf += sizeof (log_lsa);

  // safe-guard that the entire message has been consumed
  assert (message_buf == log_boot_info.c_str () + log_boot_info.size ());

  // at this point, page server has already started log prior dispatch towards this passive transaction server;
  // for now, log prior consumption is held back by the log prior lsa mutex held while calling this function

  if (prm_get_bool_value (PRM_ID_ER_LOG_PRIOR_TRANSFER))
    {
      _er_log_debug (ARG_FILE_LINE,
		     "[LOG_PRIOR_TRANSFER] Received log boot info to from page server with\n"
		     "    prev_lsa=(%lld|%d), append_lsa=(%lld|%d), most_recent_trantable_snapshot_lsa=(%lld|%d)",
		     LSA_AS_ARGS (&log_Gl.append.prev_lsa), LSA_AS_ARGS (&log_Gl.hdr.append_lsa),
		     LSA_AS_ARGS (&most_recent_trantable_snapshot_lsa));
    }

  return NO_ERROR;
}

void passive_tran_server::start_log_replicator (const log_lsa &start_lsa)
{
  assert (m_replicator == nullptr);

  // passive transaction server executes replication synchronously, for the time being, due to complexity of
  // executing it in parallel while also providing a consistent view of the data
  m_replicator.reset (new cublog::replicator (start_lsa, OLD_PAGE_IF_IN_BUFFER_OR_IN_TRANSIT, 0));
}

void passive_tran_server::send_and_receive_stop_log_prior_dispatch ()
{
  // empty message request

  std::string expected_empty_answer;
  // blocking call
  (void) send_receive (tran_to_page_request::SEND_STOP_LOG_PRIOR_DISPATCH, std::string (), expected_empty_answer);
  // do not care about possible communication error with server here as long as transaction server is
  // going down anyway (on the other side of the fence, page server[s] should be resilient to transaction
  // server[s] crashing anyway)

  // at this point, no log prior is flowing from the connected page server(s)
  // outside this context, all log prior currently present on the passive transaction server
  // needs to be consumed (aka: waited to be consumed/serialized to log)
}

log_lsa passive_tran_server::get_highest_processed_lsa () const
{
  return m_replicator->get_highest_processed_lsa ();
}

log_lsa passive_tran_server::get_lowest_unapplied_lsa () const
{
  return m_replicator->get_lowest_unapplied_lsa ();
}

void passive_tran_server::finish_replication_during_shutdown (cubthread::entry &thread_entry)
{
  assert (m_replicator != nullptr);

  m_replicator->wait_replication_finish_during_shutdown ();
  m_replicator.reset (nullptr);
}

void passive_tran_server::wait_replication_past_target_lsa (LOG_LSA lsa)
{
  m_replicator->wait_past_target_lsa (lsa);
}
