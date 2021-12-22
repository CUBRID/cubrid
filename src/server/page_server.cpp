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

#include "disk_manager.h"
#include "error_manager.h"
#include "log_impl.h"
#include "log_lsa_utils.hpp"
#include "log_manager.h"
#include "log_prior_recv.hpp"
#include "log_replication.hpp"
#include "packer.hpp"
#include "page_buffer.h"
#include "server_type.hpp"
#include "system_parameter.h"
#include "vpid_utilities.hpp"

#include <cassert>
#include <cstring>
#include <functional>
#include <thread>

page_server ps_Gl;

page_server::~page_server ()
{
  assert (m_replicator == nullptr);
  assert (m_active_tran_server_conn == nullptr);
}

page_server::connection_handler::connection_handler (cubcomm::channel &chn, page_server &ps) : m_ps (ps)
{
  m_conn.reset (new tran_server_conn_t (std::move (chn),
  {
    // common
    {
      tran_to_page_request::GET_BOOT_INFO,
      std::bind (&page_server::connection_handler::receive_boot_info_request, std::ref (*this), std::placeholders::_1)
    },
    {
      tran_to_page_request::SEND_LOG_PAGE_FETCH,
      std::bind (&page_server::connection_handler::receive_log_page_fetch, std::ref (*this), std::placeholders::_1)
    },
    {
      tran_to_page_request::SEND_DATA_PAGE_FETCH,
      std::bind (&page_server::connection_handler::receive_data_page_fetch, std::ref (*this), std::placeholders::_1)
    },
    {
      tran_to_page_request::SEND_DISCONNECT_MSG,
      std::bind (&page_server::connection_handler::receive_disconnect_request, std::ref (*this), std::placeholders::_1)
    },
    // active only
    {
      tran_to_page_request::SEND_LOG_PRIOR_LIST,
      std::bind (&page_server::connection_handler::receive_log_prior_list, std::ref (*this), std::placeholders::_1)
    },
    // passive only
    {
      tran_to_page_request::SEND_LOG_BOOT_INFO_FETCH,
      std::bind (&page_server::connection_handler::receive_log_boot_info_fetch, std::ref (*this), std::placeholders::_1)
    },
    {
      tran_to_page_request::SEND_STOP_LOG_PRIOR_DISPATCH,
      std::bind (&page_server::connection_handler::receive_stop_log_prior_dispatch, std::ref (*this),
		 std::placeholders::_1)
    }
  }, page_to_tran_request::RESPOND, tran_to_page_request::RESPOND, 1));

  assert (m_conn != nullptr);
  m_conn->start ();
}

page_server::connection_handler::~connection_handler ()
{
  assert (!m_prior_sender_sink_hook_func);
}

std::string
page_server::connection_handler::get_channel_id ()
{
  return m_conn->get_underlying_channel_id ();
}

void
page_server::connection_handler::push_request (page_to_tran_request id, std::string msg)
{
  m_conn->push (id, std::move (msg));
}

void
page_server::connection_handler::receive_log_prior_list (tran_server_conn_t::sequenced_payload &a_ip)
{
  log_Gl.get_log_prior_receiver ().push_message (std::move (a_ip.pull_payload ()));
}

template<class F>
void
page_server::connection_handler::async_response (F &&a_func, tran_server_conn_t::sequenced_payload &&a_sp)
{
  m_ps.get_responder ().async_execute (std::ref (*m_conn), std::move (a_sp),
				       std::bind (std::forward<F> (a_func), this,
					   std::placeholders::_1, std::placeholders::_2));
}

void
page_server::connection_handler::async_log_page_fetch (cubthread::entry &context, std::string &a_payload)
{
  // Unpack the message data
  LOG_PAGEID log_pageid;
  assert (a_payload.size () == sizeof (log_pageid));
  std::memcpy (&log_pageid, a_payload.c_str (), sizeof (log_pageid));

  log_lsa fetch_lsa { log_pageid, 0 };
  log_reader lr { LOG_CS_SAFE_READER };

  if (log_pageid == LOGPB_HEADER_PAGE_ID)
    {
      // Make sure log page header is updated
      logpb_force_flush_header_and_pages (&context);
    }

  int error = lr.set_lsa_and_fetch_page (fetch_lsa);

  // Response message
  if (prm_get_bool_value (PRM_ID_ER_LOG_READ_LOG_PAGE))
    {
      _er_log_debug (ARG_FILE_LINE,
		     "[READ LOG] Sending log page to Active Tran Server. Page ID: %lld Error code: %ld\n",
		     log_pageid, error);
    }

  // pack error first
  a_payload = { reinterpret_cast<const char *> (&error), sizeof (error) };

  if (error == NO_ERROR)
    {
      // pack page data too
      a_payload.append (reinterpret_cast<const char *> (lr.get_page ()), db_log_page_size ());
    }
}

void
page_server::connection_handler::receive_log_page_fetch (tran_server_conn_t::sequenced_payload &a_sp)
{
  async_response (&connection_handler::async_log_page_fetch, std::move (a_sp));
}

void
page_server::connection_handler::async_data_page_fetch (cubthread::entry &context, std::string &a_payload)
{
  // Unpack the message data
  cubpacking::unpacker message_upk (a_payload.c_str (), a_payload.size ());

  VPID vpid;
  vpid_utils::unpack (message_upk, vpid);

  LOG_LSA target_repl_lsa;
  cublog::lsa_utils::unpack (message_upk, target_repl_lsa);

  // Fetch data page. But first make sure that replication hits its target LSA
  if (!target_repl_lsa.is_null ())
    {
      // TODO: FIXME
      // The transaction server boots and reads pages before initializing its log module and before knowing a safe target
      // LSA for replication. A way of knowing this target LSA is required, but disable this wait until that's fixed.
      m_ps.get_replicator ().wait_past_target_lsa (target_repl_lsa);
    }

  int error = NO_ERROR;
  PAGE_PTR page_ptr = pgbuf_fix (&context, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_ptr == nullptr)
    {
      ASSERT_ERROR_AND_SET (error);
      // respond with the error
      a_payload = { reinterpret_cast<const char *> (&error), sizeof (error) };

      if (prm_get_bool_value (PRM_ID_ER_LOG_READ_DATA_PAGE))
	{
	  _er_log_debug (ARG_FILE_LINE,
			 "[READ DATA] Error %lld while fixing page %d|%d, replication LSA = %lld|%d\n",
			 error, VPID_AS_ARGS (&vpid), LSA_AS_ARGS (&target_repl_lsa));
	}
    }
  else
    {
      FILEIO_PAGE *io_pgptr = nullptr;
      pgbuf_cast_pgptr_to_iopgptr (page_ptr, io_pgptr);
      assert (io_pgptr != nullptr);

      // pack NO_ERROR first
      a_payload = { reinterpret_cast<const char *> (&error), sizeof (error) };

      // add io_page
      a_payload.append (reinterpret_cast<const char *> (io_pgptr), (size_t) db_io_page_size ());

      pgbuf_unfix (&context, page_ptr);

      if (prm_get_bool_value (PRM_ID_ER_LOG_READ_DATA_PAGE))
	{
	  _er_log_debug (ARG_FILE_LINE,
			 "[READ DATA] Successful while fixing page %d|%d, replication LSA = %lld|%d\n",
			 VPID_AS_ARGS (&vpid), LSA_AS_ARGS (&target_repl_lsa));
	}
    }
}

void
page_server::connection_handler::receive_data_page_fetch (tran_server_conn_t::sequenced_payload &a_sp)
{
  async_response (&connection_handler::async_data_page_fetch, std::move (a_sp));
}

void
page_server::connection_handler::async_log_boot_info (cubthread::entry &context, std::string &a_payload)
{
  assert (a_payload.empty ());
  log_lsa append_lsa, prev_lsa, most_recent_trantable_snapshot_lsa;

  m_prior_sender_sink_hook_func =
	  std::bind (&connection_handler::prior_sender_sink_hook, this, std::placeholders::_1);

  // log_pack_log_boot_info does all the work
  a_payload = log_pack_log_boot_info (&context, append_lsa, prev_lsa, most_recent_trantable_snapshot_lsa,
				      m_prior_sender_sink_hook_func);

  if (prm_get_bool_value (PRM_ID_ER_LOG_PRIOR_TRANSFER))
    {
      _er_log_debug (ARG_FILE_LINE,
		     "[LOG PRIOR TRANSFER] Sent log boot info to passive tran server with prev_lsa = (%lld|%d), "
		     "append_lsa = (%lld|%d), most_recent_trantable_snapshot_lsa = (%lld|%d)\n",
		     LSA_AS_ARGS (&prev_lsa), LSA_AS_ARGS (&append_lsa),
		     LSA_AS_ARGS (&most_recent_trantable_snapshot_lsa));
    }
}

void
page_server::connection_handler::receive_log_boot_info_fetch (tran_server_conn_t::sequenced_payload &a_sp)
{
  async_response (&connection_handler::async_log_boot_info, std::move (a_sp));
}

void
page_server::connection_handler::receive_stop_log_prior_dispatch (tran_server_conn_t::sequenced_payload &a_sp)
{
  // empty request message

  assert ((bool) m_prior_sender_sink_hook_func);

  {
    std::lock_guard<std::mutex> lockg { m_prior_sender_sink_removal_mtx };

    log_Gl.m_prior_sender.remove_sink (m_prior_sender_sink_hook_func);
    m_prior_sender_sink_hook_func = nullptr;
  }

  // empty response message, the round trip is synchronous
  a_sp.push_payload (std::string ());
  m_conn->respond (std::move (a_sp));
}

void
page_server::connection_handler::on_log_boot_info_result (tran_server_conn_t::sequenced_payload &&sp,
    std::string &&message)
{

}

void
page_server::connection_handler::receive_disconnect_request (tran_server_conn_t::sequenced_payload &)
{
  // if this instance acted as a prior sender sink - in other words, if this connection handler was for a
  // passive transaction server - it should have been disconnected beforehand
  assert (! (bool) m_prior_sender_sink_hook_func);

  //start a thread to destroy the ATS/PTS to PS connection object
  std::thread disconnect_thread (&page_server::disconnect_tran_server, std::ref (m_ps), this);
  disconnect_thread.detach ();
}

void
page_server::connection_handler::receive_boot_info_request (tran_server_conn_t::sequenced_payload &a_sp)
{
  DKNVOLS nvols_perm = disk_get_perm_volume_count ();

  std::string response_message;
  response_message.reserve (sizeof (nvols_perm));
  response_message.append (reinterpret_cast<const char *> (&nvols_perm), sizeof (nvols_perm));

  a_sp.push_payload (std::move (response_message));
  m_conn->respond (std::move (a_sp));
}

void
page_server::connection_handler::on_log_page_read_result (tran_server_conn_t::sequenced_payload &&sp,
    const LOG_PAGE *log_page, int error_code)
{
  std::string message;

  message.append (reinterpret_cast<const char *> (&error_code), sizeof (error_code));

  if (error_code == NO_ERROR)
    {
      message.append (reinterpret_cast<const char *> (log_page), db_log_page_size ());
    }

  sp.push_payload (std::move (message));
  m_conn->respond (std::move (sp));

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
page_server::connection_handler::on_data_page_read_result (tran_server_conn_t::sequenced_payload &&sp,
    const FILEIO_PAGE *io_page, int error_code)
{
  std::string message;
  message.append (reinterpret_cast<const char *> (&error_code), sizeof (error_code));

  if (error_code != NO_ERROR)
    {
      if (prm_get_bool_value (PRM_ID_ER_LOG_READ_DATA_PAGE))
	{
	  _er_log_debug (ARG_FILE_LINE, "[READ DATA] Sending data page.. VPID: %d|%d, LSA: %lld|%d\n",
			 io_page->prv.volid, io_page->prv.pageid, LSA_AS_ARGS (&io_page->prv.lsa));
	}
    }
  else
    {
      message.append (reinterpret_cast<const char *> (io_page), db_io_page_size ());
      if (prm_get_bool_value (PRM_ID_ER_LOG_READ_DATA_PAGE))
	{
	  _er_log_debug (ARG_FILE_LINE, "[READ DATA] Sending data page.. Error code: %d\n", error_code);
	}
    }

  sp.push_payload (std::move (message));
  m_conn->respond (std::move (sp));
}

void
page_server::connection_handler::prior_sender_sink_hook (std::string &&message) const
{
  assert (m_conn != nullptr);
  assert (message.size () > 0);

  std::lock_guard<std::mutex> lockg { m_prior_sender_sink_removal_mtx };
  m_conn->push (page_to_tran_request::SEND_TO_PTS_LOG_PRIOR_LIST, std::move (message));
}

void
page_server::set_active_tran_server_connection (cubcomm::channel &&chn)
{
  assert (is_page_server ());

  chn.set_channel_name ("ATS_PS_comm");
  er_log_debug (ARG_FILE_LINE, "Active transaction server connected to this page server. Channel id: %s.\n",
		chn.get_channel_id ().c_str ());

  if (m_active_tran_server_conn != nullptr)
    {
      // ATS must have crashed without disconnecting from us. Destroy old connection to create a new one.
      disconnect_active_tran_server ();
    }
  assert (m_active_tran_server_conn == nullptr);
  m_active_tran_server_conn.reset (new connection_handler (chn, *this));
}

void
page_server::set_passive_tran_server_connection (cubcomm::channel &&chn)
{
  assert (is_page_server ());

  chn.set_channel_name ("PTS_PS_comm");
  er_log_debug (ARG_FILE_LINE, "Passive transaction server connected to this page server. Channel id: %s.\n",
		chn.get_channel_id ().c_str ());

  m_passive_tran_server_conn.emplace_back (new connection_handler (chn, *this));
}

void
page_server::disconnect_active_tran_server ()
{
  er_log_debug (ARG_FILE_LINE, "Page server disconnected from active transaction server with channel id: %s.\n",
		m_active_tran_server_conn->get_channel_id ().c_str ());
  m_active_tran_server_conn.reset (nullptr);
}

void
page_server::disconnect_tran_server (connection_handler *conn)
{
  assert (conn != nullptr);
  if (conn == m_active_tran_server_conn.get ())
    {
      disconnect_active_tran_server ();
    }
  else
    {
      for (auto it = m_passive_tran_server_conn.begin (); it != m_passive_tran_server_conn.end (); ++it)
	{
	  if (conn == it->get ())
	    {
	      er_log_debug (ARG_FILE_LINE, "Page server disconnected from passive transaction server with channel id: %s.\n",
			    (*it)->get_channel_id ().c_str ());
	      m_passive_tran_server_conn.erase (it);
	      break;
	    }
	}
    }
}

void
page_server::disconnect_all_tran_server ()
{
  if (m_active_tran_server_conn == nullptr)
    {
      er_log_debug (ARG_FILE_LINE, "Page server was never connected with an active transaction server.\n");
    }
  else
    {
      disconnect_active_tran_server ();
    }
  if (m_passive_tran_server_conn.empty ())
    {
      er_log_debug (ARG_FILE_LINE, "Page server was never connected with an passive transaction server.\n");
    }
  else
    {
      for (size_t i = 0; i < m_passive_tran_server_conn.size (); i++)
	{
	  er_log_debug (ARG_FILE_LINE, "Page server disconnected from passive transaction server with channel id: %s.\n",
			m_passive_tran_server_conn[i]->get_channel_id ().c_str ());
	  m_passive_tran_server_conn[i].reset (nullptr);
	}
      m_passive_tran_server_conn.clear ();
    }
}

bool
page_server::is_active_tran_server_connected () const
{
  assert (is_page_server ());

  return m_active_tran_server_conn != nullptr;
}

cublog::async_page_fetcher &
page_server::get_page_fetcher ()
{
  assert (m_page_fetcher != nullptr);
  return *m_page_fetcher.get ();
}

void
page_server::push_request_to_active_tran_server (page_to_tran_request reqid, std::string &&payload)
{
  assert (is_page_server ());
  assert (is_active_tran_server_connected ());

  m_active_tran_server_conn->push_request (reqid, std::move (payload));
}

cublog::replicator &
page_server::get_replicator ()
{
  assert (m_replicator != nullptr);
  return *m_replicator;
}

void
page_server::start_log_replicator (const log_lsa &start_lsa)
{
  assert (is_page_server ());
  assert (m_replicator == nullptr);

  const int replication_parallel_count = prm_get_integer_value (PRM_ID_REPLICATION_PARALLEL_COUNT);
  assert (replication_parallel_count >= 0);
  m_replicator.reset (new cublog::replicator (start_lsa, RECOVERY_PAGE, replication_parallel_count));
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
page_server::init_page_fetcher ()
{
  m_page_fetcher.reset (new cublog::async_page_fetcher ());
  m_responder = std::make_unique<responder_t> ();
}

void
page_server::finalize_page_fetcher ()
{
  m_page_fetcher.reset (nullptr);
  m_responder.reset (nullptr);
}
