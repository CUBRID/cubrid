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

#include "communication_server_channel.hpp"
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

page_server::~page_server ()
{
  assert (m_replicator == nullptr);

  // for now, application logic dictates that all transaction server must disconnect from their end
  // (either proactively or not)
  // when/if needed connections can be moved here to the disconnect handler and that one waited for
  assert (m_active_tran_server_conn == nullptr);
  assert (m_passive_tran_server_conn.size () == 0);

  m_async_disconnect_handler.terminate ();
}

page_server::tran_server_connection_handler::tran_server_connection_handler (cubcomm::channel &&chn,
    transaction_server_type server_type,
    page_server &ps)
  : m_server_type { server_type }
  , m_connection_id { chn.get_channel_id () }
  , m_ps (ps)
  , m_abnormal_tran_server_disconnect { false }
{
  assert (!m_connection_id.empty ());

  constexpr size_t RESPONSE_PARTITIONING_SIZE = 1; // Arbitrarily chosen

  m_conn.reset (
	  new tran_server_conn_t (std::move (chn),
  {
    // common
    {
      // TODO: rename handler with _async / _sync
      tran_to_page_request::GET_BOOT_INFO,
      std::bind (&page_server::tran_server_connection_handler::receive_boot_info_request, std::ref (*this), std::placeholders::_1)
    },
    {
      tran_to_page_request::SEND_LOG_PAGE_FETCH,
      std::bind (&page_server::tran_server_connection_handler::receive_log_page_fetch, std::ref (*this), std::placeholders::_1)
    },
    {
      tran_to_page_request::SEND_DATA_PAGE_FETCH,
      std::bind (&page_server::tran_server_connection_handler::receive_data_page_fetch, std::ref (*this), std::placeholders::_1)
    },
    {
      tran_to_page_request::SEND_DISCONNECT_MSG,
      std::bind (&page_server::tran_server_connection_handler::receive_disconnect_request, std::ref (*this), std::placeholders::_1)
    },
    // active only
    {
      tran_to_page_request::SEND_LOG_PRIOR_LIST,
      std::bind (&page_server::tran_server_connection_handler::receive_log_prior_list, std::ref (*this), std::placeholders::_1)
    },
    {
      tran_to_page_request::GET_OLDEST_ACTIVE_MVCCID,
      std::bind (&page_server::tran_server_connection_handler::handle_oldest_active_mvccid_request, std::ref (*this), std::placeholders::_1)
    },
    {
      tran_to_page_request::SEND_START_CATCH_UP,
      std::bind (&page_server::tran_server_connection_handler::receive_start_catch_up, std::ref (*this), std::placeholders::_1)
    },
    // passive only
    {
      tran_to_page_request::SEND_LOG_BOOT_INFO_FETCH,
      std::bind (&page_server::tran_server_connection_handler::receive_log_boot_info_fetch, std::ref (*this), std::placeholders::_1)
    },
    {
      tran_to_page_request::SEND_STOP_LOG_PRIOR_DISPATCH,
      std::bind (&page_server::tran_server_connection_handler::receive_stop_log_prior_dispatch, std::ref (*this),
		 std::placeholders::_1)
    },
    {
      tran_to_page_request::SEND_OLDEST_ACTIVE_MVCCID,
      std::bind (&page_server::tran_server_connection_handler::receive_oldest_active_mvccid, std::ref (*this), std::placeholders::_1)
    }
  },
  page_to_tran_request::RESPOND,
  tran_to_page_request::RESPOND,
  RESPONSE_PARTITIONING_SIZE,
  std::bind (&page_server::tran_server_connection_handler::abnormal_tran_server_disconnect,
	     std::ref (*this), std::placeholders::_1, std::placeholders::_2), nullptr));
  m_ps.get_tran_server_responder ().register_connection (m_conn.get ());

  assert (m_conn != nullptr);
  m_conn->start ();
}

page_server::tran_server_connection_handler::~tran_server_connection_handler ()
{
  assert (!m_prior_sender_sink_hook_func);

  // blocking call
  // internally, this will also wait pending outgoing roundtrip (send-receive) requests
  m_conn->stop_incoming_communication_thread ();

  // blocking call
  // wait async responder to finish processing in-flight incoming roundtrip requests
  m_ps.get_tran_server_responder ().wait_connection_to_become_idle (m_conn.get ());

  m_conn->stop_outgoing_communication_thread ();
}

const std::string &
page_server::tran_server_connection_handler::get_connection_id () const
{
  return m_connection_id;
}

void
page_server::tran_server_connection_handler::push_request (page_to_tran_request id, std::string &&msg)
{
  m_conn->push (id, std::move (msg));
}

void
page_server::tran_server_connection_handler::receive_log_prior_list (tran_server_conn_t::sequenced_payload &&a_sp)
{
  log_Gl.get_log_prior_receiver ().push_message (std::move (a_sp.pull_payload ()));
}

template<class F, class ... Args>
void
page_server::tran_server_connection_handler::push_async_response (F &&a_func,
    tran_server_conn_t::sequenced_payload &&a_sp,
    Args &&... args)
{
  auto handler_func = std::bind (std::forward<F> (a_func), std::placeholders::_1, std::placeholders::_2,
				 std::forward<Args> (args)...);
  m_ps.get_tran_server_responder ().async_execute (std::ref (*m_conn), std::move (a_sp), std::move (handler_func));
}

void
page_server::tran_server_connection_handler::receive_log_page_fetch (tran_server_conn_t::sequenced_payload &&a_sp)
{
  push_async_response (logpb_respond_fetch_log_page_request, std::move (a_sp));
}

void
page_server::tran_server_connection_handler::receive_data_page_fetch (tran_server_conn_t::sequenced_payload &&a_sp)
{
  push_async_response (pgbuf_respond_data_fetch_page_request, std::move (a_sp));
}

void
page_server::tran_server_connection_handler::handle_oldest_active_mvccid_request (tran_server_conn_t::sequenced_payload
    &&a_sp)
{
  assert (m_server_type == transaction_server_type::ACTIVE);
  const MVCCID oldest_mvccid = m_ps.m_pts_mvcc_tracker.get_global_oldest_active_mvccid ();

  std::string response_message;
  response_message.append (reinterpret_cast<const char *> (&oldest_mvccid), sizeof (oldest_mvccid));

  a_sp.push_payload (std::move (response_message));
  m_conn->respond (std::move (a_sp));
}


void
page_server::tran_server_connection_handler::receive_start_catch_up (tran_server_conn_t::sequenced_payload &&a_sp)
{
  auto payload = a_sp.pull_payload ();
  cubpacking::unpacker unpacker { payload.c_str (), payload.size () };

  std::string host;
  int32_t port;
  LOG_LSA catchup_lsa;

  unpacker.unpack_string (host);
  unpacker.unpack_int (port);
  cublog::lsa_utils::unpack (unpacker, catchup_lsa);

  _er_log_debug (ARG_FILE_LINE,
		 "[CATCH-UP] It's been requested to start catch-up with the follower (%s:%d), until LSA = (%lld|%d)\n",
		 host.c_str (), port,LSA_AS_ARGS (&catchup_lsa));
  if (port == -1)
    {
      // TODO: It means that the ATS is booting up.
      // it will be set properly after ATS recovery is implemented.
      return;
    }

  // Establish a connection with the PS to catch up with, and start catchup_worker with it.
  // The connection will be destoryed at the end of the catch-up
  m_ps.connect_to_followee_page_server (std::move (host), port);

  assert (!catchup_lsa.is_null ());
  assert (!log_Gl.hdr.append_lsa.is_null ());
  assert (catchup_lsa >= log_Gl.hdr.append_lsa);

  if (log_Gl.hdr.append_lsa == catchup_lsa)
    {
      _er_log_debug (ARG_FILE_LINE, "[CATCH-UP] There is nothing to catch up.\n");
      return; // TODO the cold-start case. No need to catch up. Just send a catchup_done msg to the ATS
    }

  assert (m_ps.m_catchup_worker == nullptr);
  m_ps.m_catchup_worker.reset (new catchup_worker { m_ps, std::move (m_ps.m_followee_conn), catchup_lsa});
  m_ps.m_catchup_worker->set_on_success ([this]()
  {
    // TODO Send a catchup_done msg to the ATS;
  });

  m_ps.m_catchup_worker->start ();
}

void
page_server::tran_server_connection_handler::receive_log_boot_info_fetch (tran_server_conn_t::sequenced_payload &&a_sp)
{
  m_prior_sender_sink_hook_func =
	  std::bind (&tran_server_connection_handler::prior_sender_sink_hook, this, std::placeholders::_1);

  push_async_response (log_pack_log_boot_info, std::move (a_sp), std::ref (m_prior_sender_sink_hook_func));
}

void
page_server::tran_server_connection_handler::receive_stop_log_prior_dispatch (tran_server_conn_t::sequenced_payload
    &&a_sp)
{
  // empty request message

  assert (static_cast<bool> (m_prior_sender_sink_hook_func));

  remove_prior_sender_sink ();

  // empty response message, the round trip is synchronous
  a_sp.push_payload (std::string ());
  m_conn->respond (std::move (a_sp));
}

void
page_server::tran_server_connection_handler::receive_oldest_active_mvccid (tran_server_conn_t::sequenced_payload &&a_sp)
{
  assert (m_server_type == transaction_server_type::PASSIVE);

  const auto oldest_mvccid = *reinterpret_cast<const MVCCID *> (a_sp.pull_payload ().c_str ());

  m_ps.m_pts_mvcc_tracker.update_oldest_active_mvccid (get_connection_id (), oldest_mvccid);
}

void
page_server::tran_server_connection_handler::receive_disconnect_request (tran_server_conn_t::sequenced_payload &&)
{
  // if this instance acted as a prior sender sink - in other words, if this connection handler was for a
  // passive transaction server - it should have been disconnected beforehand
  assert (m_prior_sender_sink_hook_func == nullptr);

  if (m_server_type == transaction_server_type::PASSIVE)
    {
      m_ps.m_pts_mvcc_tracker.delete_oldest_active_mvccid (get_connection_id ());
    }

  m_ps.disconnect_tran_server_async (this);
}

void
page_server::tran_server_connection_handler::abnormal_tran_server_disconnect (css_error_code error_code,
    bool &abort_further_processing)
{
  /* Explanation for the mutex lock.
   * A connection handler has 2 threads underneath:
   *  - a thread that handles incoming requests and matches them to handling functions - cubcomm:request_server
   *  - a thread that handles continuous send of messages (either responses or not) - cubcomm::request_queue_autosend
   * For now, only the second one - the send part - implements the handler but, technically, any of these can,
   * at some point trigger this disconnect function.
   * */
  std::lock_guard<std::mutex> lockg { m_abnormal_tran_server_disconnect_mtx };

  /* when a transaction server suddenly disconnects, if the page server happens to be either
   * proactively sending data or responding to a request, an error is reported from the network layer;
   * this function is a handler for such an error - see cubcomm::send_queue_error_handler.
   *
   * NOTE: if needed, functionality can be extended with more advanced features (ie: retry policy, timeouts ..)
   * */
  if (!m_abnormal_tran_server_disconnect)
    {
      abort_further_processing = true;

      if (m_server_type == transaction_server_type::PASSIVE)
	{
	  er_log_debug (ARG_FILE_LINE, "abnormal_tran_server_disconnect: PTS disconnected from PS. Error code: %d\n",
			(int)error_code);

	  remove_prior_sender_sink ();

	  m_ps.m_pts_mvcc_tracker.delete_oldest_active_mvccid (get_connection_id ());
	}
      else
	{
	  er_log_debug (ARG_FILE_LINE, "abnormal_tran_server_disconnect: ATS disconnected from PS. Error code: %d\n",
			(int)error_code);
	}

      m_ps.disconnect_tran_server_async (this);

      m_abnormal_tran_server_disconnect = true;
    }
  else
    {
      er_log_debug (ARG_FILE_LINE, "abnormal_tran_server_disconnect: already requested to disconnect\n");
    }
}

/* NOTE : Since TS don't need the information about the number of permanent volume during boot,
 *        this message has no actual use currently. However, this mechanism will be reserved,
 *        because it can be used in the future when multiple PS's are supported. */
void
page_server::tran_server_connection_handler::receive_boot_info_request (tran_server_conn_t::sequenced_payload &&a_sp)
{
  /* It is simply a dummy value to check whether the TS (get_boot_info_from_page_server) receives the message well */
  DKNVOLS nvols_perm = VOLID_MAX;

  std::string response_message;
  response_message.reserve (sizeof (nvols_perm));
  response_message.append (reinterpret_cast<const char *> (&nvols_perm), sizeof (nvols_perm));

  a_sp.push_payload (std::move (response_message));
  m_conn->respond (std::move (a_sp));
}

void
page_server::tran_server_connection_handler::prior_sender_sink_hook (std::string &&message) const
{
  assert (m_conn != nullptr);
  assert (message.size () > 0);

  std::lock_guard<std::mutex> lockg { m_prior_sender_sink_removal_mtx };
  m_conn->push (page_to_tran_request::SEND_TO_PTS_LOG_PRIOR_LIST, std::move (message));
}

/* Page Server to Passive Transaction Server log prior sender sink can be de-activated via the following routes:
 *  - as a direct request from PTS as part of PTS shutting down:
 *      - via receive_stop_log_prior_dispatch followed by receive_disconnect_request
 *  - abnormal disconnect request following error to send data to PTS
 *      - via abnormal_tran_server_disconnect
 *  - as a request from PS itself shuttind down
 *      - via disconnect_all_tran_server from
 *        - xboot_shutdown_server first and then
 *        - via boot_server_all_finalize - finalize_server_type
 * In all these scenarios, log prior dispatch sink must be disconnected explicitly.
 * */
void
page_server::tran_server_connection_handler::remove_prior_sender_sink ()
{
  std::lock_guard<std::mutex> lockg { m_prior_sender_sink_removal_mtx };

  if (static_cast<bool> (m_prior_sender_sink_hook_func))
    {
      log_Gl.get_log_prior_sender ().remove_sink (m_prior_sender_sink_hook_func);
      m_prior_sender_sink_hook_func = nullptr;
    }
}

void
page_server::tran_server_connection_handler::push_disconnection_request ()
{
  push_request (page_to_tran_request::SEND_DISCONNECT_REQUEST_MSG, std::string ());
}

page_server::follower_connection_handler::follower_connection_handler (cubcomm::channel &&chn, page_server &ps)
  : m_ps { ps }
{
  constexpr size_t RESPONSE_PARTITIONING_SIZE = 1; // Arbitrarily chosen

  m_conn.reset (new follower_server_conn_t (std::move (chn),
  {
    {
      follower_to_followee_request::SEND_LOG_PAGES_FETCH,
      std::bind (&page_server::follower_connection_handler::receive_log_pages_fetch, std::ref (*this), std::placeholders::_1)
    },
  },
  followee_to_follower_request::RESPOND,
  follower_to_followee_request::RESPOND,
  RESPONSE_PARTITIONING_SIZE,
  nullptr,
  nullptr)); // TODO handle abnormal disconnection.

  m_ps.get_follower_responder ().register_connection (m_conn.get ());

  m_conn->start ();
}

void
page_server::follower_connection_handler::receive_log_pages_fetch (follower_server_conn_t::sequenced_payload &&a_sp)
{
  auto log_serving_func = std::bind (&page_server::follower_connection_handler::serve_log_pages, std::ref (*this),
				     std::placeholders::_1, std::placeholders::_2);
  m_ps.get_follower_responder ().async_execute (std::ref (*m_conn), std::move (a_sp), std::move (log_serving_func));
}

void
page_server::follower_connection_handler::serve_log_pages (THREAD_ENTRY &, std::string &payload_in_out)
{
  // Unpack the message data
  cubpacking::unpacker payload_unpacker { payload_in_out.c_str (), payload_in_out.size () };
  LOG_PAGEID start_pageid;
  int cnt;

  assert (payload_in_out.size () == (sizeof (LOG_PAGEID) + OR_INT_SIZE));
  payload_unpacker.unpack_bigint (start_pageid);
  payload_unpacker.unpack_int (cnt);

  const bool perform_logging = prm_get_bool_value (PRM_ID_ER_LOG_READ_LOG_PAGE);

  // Pack the requested log pages
  int error = NO_ERROR;
  payload_in_out = { reinterpret_cast<const char *> (&error), sizeof (error) }; // Pack NO_ERROR assuming it'll succeed.
  log_reader lr { LOG_CS_SAFE_READER };

  for (int i = 0; i < cnt; i++)
    {
      const log_lsa fetch_lsa { start_pageid + i, 0 };

      assert (fetch_lsa.pageid != LOGPB_HEADER_PAGE_ID);

      error = lr.set_lsa_and_fetch_page (fetch_lsa);

      if (error != NO_ERROR)
	{
	  // All or Nothing. Abandon all appended pages and just set the error.
	  payload_in_out = { reinterpret_cast<const char *> (&error), sizeof (error) };
	  break;
	}
      else
	{
	  payload_in_out.append (reinterpret_cast<const char *> (lr.get_page ()), LOG_PAGESIZE);
	}
    }

  if (perform_logging)
    {
      _er_log_debug (ARG_FILE_LINE,
		     "[READ LOG] Sending log pages to the pager server (%s). Page Id from %lld to %lld, Error code: %d\n",
		     m_conn->get_underlying_channel_id ().c_str (), start_pageid, start_pageid + cnt - 1, error);
    }
}

page_server::follower_connection_handler::~follower_connection_handler ()
{
  // blocking call
  // internally, this will also wait pending outgoing roundtrip (send-receive) requests
  m_conn->stop_incoming_communication_thread ();

  // blocking call
  // wait async responder to finish processing in-flight incoming roundtrip requests
  m_ps.get_follower_responder ().wait_connection_to_become_idle (m_conn.get ());

  m_conn->stop_outgoing_communication_thread ();
}

page_server::followee_connection_handler::followee_connection_handler (cubcomm::channel &&chn, page_server &ps)
  : m_ps { ps }
{
  constexpr size_t RESPONSE_PARTITIONING_SIZE = 1; // Arbitrarily chosen

  m_conn.reset (new followee_server_conn_t (std::move (chn),
		{}, // followee doesn't request anything
		follower_to_followee_request::RESPOND,
		followee_to_follower_request::RESPOND,
		RESPONSE_PARTITIONING_SIZE,
		nullptr,
		nullptr)); // TODO handle abnormal disconnection.

  m_conn->start ();
}

void
page_server::followee_connection_handler::push_request (follower_to_followee_request reqid, std::string &&msg)
{
  m_conn->push (reqid, std::move (msg));
}

int
page_server::followee_connection_handler::send_receive (follower_to_followee_request reqid, std::string &&payload_in,
    std::string &payload_out)
{
  const css_error_code error_code =  m_conn->send_recv (reqid, std::move (payload_in), payload_out);
  if (error_code != NO_ERRORS)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CONN_PAGE_SERVER_CANNOT_BE_REACHED, 0);
      return ER_CONN_PAGE_SERVER_CANNOT_BE_REACHED;
    }

  return NO_ERROR;
}

int
page_server::followee_connection_handler::request_log_pages (LOG_PAGEID start_pageid, int count,
    std::vector<LOG_PAGE *> &log_pages_out)
{
  int error_code = NO_ERROR;
  std::string response_message;
  cubpacking::packer payload_packer;
  size_t size = 0;

  assert (0 < count);
  assert ((size_t) count <= log_pages_out.size ());

  const bool perform_logging = prm_get_bool_value (PRM_ID_ER_LOG_READ_LOG_PAGE);

  size += payload_packer.get_packed_bigint_size (size); // start_pageid
  size += payload_packer.get_packed_int_size (size); // count

  std::unique_ptr < char[] > buffer (new char[size]);
  payload_packer.set_buffer (buffer.get (), size);

  payload_packer.pack_bigint (start_pageid);
  payload_packer.pack_int (count);

  if (perform_logging)
    {
      _er_log_debug (ARG_FILE_LINE, "[READ LOG] Sent request for log to the page server (%s). Page ID from %lld to %lld\n",
		     m_conn->get_underlying_channel_id ().c_str (), start_pageid, start_pageid + count - 1);
    }

  error_code = send_receive (follower_to_followee_request::SEND_LOG_PAGES_FETCH, std::string (buffer.get (), size),
			     response_message);
  if (error_code != NO_ERROR)
    {
      /* client-side error: it fails to send or receive.
       */
      if (perform_logging)
	{
	  _er_log_debug (ARG_FILE_LINE, "[READ LOG] Received error log page message from the page server (%s). Error code: %d\n",
			 m_conn->get_underlying_channel_id ().c_str (), error_code);
	}
      return error_code;
    }

  assert (response_message.size () > 0);

  const char *message_ptr = response_message.c_str ();
  std::memcpy (&error_code, message_ptr, sizeof (error_code));
  message_ptr += sizeof (error_code);

  if (error_code != NO_ERROR)
    {
      /*  server-side error from followee: the follwee fails to fetch log pages. For example,
       *  - the followee PS amy truncate some log pages requested.
       *  - some requested pages are corrupted
       *  - or something
       */
      if (perform_logging)
	{
	  _er_log_debug (ARG_FILE_LINE, "[READ LOG] Received error log page message from the page server (%s). Error code: %d\n",
			 m_conn->get_underlying_channel_id ().c_str (), error_code);
	}
      return error_code;
    }

  for (int i = 0; i < count; i++)
    {
      std::memcpy (log_pages_out[i], message_ptr, LOG_PAGESIZE);
      message_ptr += LOG_PAGESIZE;

      assert (log_pages_out[i]->hdr.logical_pageid == start_pageid + i);
      if (perform_logging)
	{
	  _er_log_debug (ARG_FILE_LINE, "[READ LOG] Received log page message from the page server (%s). Page ID: %lld\n",
			 m_conn->get_underlying_channel_id ().c_str (), start_pageid + i);
	}
    }

  return error_code;
}

void page_server::pts_mvcc_tracker::init_oldest_active_mvccid (const std::string &pts_channel_id)
{
  std::lock_guard<std::mutex> lockg { m_pts_oldest_active_mvccids_mtx };
  /*
   * The entry must not already be present. If the same passive transaction server has been connected
   * before, the entry must have been removed when the PTS disconnected or when the connection
   *  to the PTS was aborted.
   */
  assert (m_pts_oldest_active_mvccids.find (pts_channel_id) == m_pts_oldest_active_mvccids.end ());

  /*
   * MVCCID_ALL_VISIBLE means that it hasn't yet received. It will prevent the ATS to run vacuum.
   * This is a guard for the window in which a PTS is connected but has't sent its oldest active mvccid.
   * In this window, if we vaccum without considering the PTS, we possibly end up cleaning up the data
   * a read-only transaction on the PTS see.
   */
  m_pts_oldest_active_mvccids[pts_channel_id] = MVCCID_ALL_VISIBLE;
}

void page_server::pts_mvcc_tracker::update_oldest_active_mvccid (const std::string &pts_channel_id, const MVCCID mvccid)
{
  assert (MVCCID_IS_NORMAL (mvccid));

  std::lock_guard<std::mutex> lockg { m_pts_oldest_active_mvccids_mtx };

  /*
   * 1. The entry is already created when ths PTS is connected.
   * 2. It is updated by the PTS only when it move foward.
   *    Without update, it is MVCCID_ALL_VISIBLE by default, which is lower than any mvccid assigned.
   */
  assert (m_pts_oldest_active_mvccids.find (pts_channel_id) != m_pts_oldest_active_mvccids.end ());
  assert (m_pts_oldest_active_mvccids[pts_channel_id] < mvccid);

  m_pts_oldest_active_mvccids[pts_channel_id] = mvccid;

#if !defined(NDEBUG)
  std::string msg;
  std::stringstream ss;
  ss << "receive_oldest_active_mvccid: update the oldest active mvccid to " << mvccid << " of " << pts_channel_id <<
     std::endl;
  ss << "oldest mvcc ids:" ;
  for (const auto &it : m_pts_oldest_active_mvccids)
    {
      ss << " " << it.second;
    }
  er_log_debug (ARG_FILE_LINE, ss.str ().c_str ());
#endif
}
void page_server::pts_mvcc_tracker::delete_oldest_active_mvccid (const std::string &pts_channel_id)
{
  std::lock_guard<std::mutex> lockg { m_pts_oldest_active_mvccids_mtx };
  /* The entry is already created when ths PTS is connected. */
  assert (m_pts_oldest_active_mvccids.find (pts_channel_id) != m_pts_oldest_active_mvccids.end ());
  m_pts_oldest_active_mvccids.erase (pts_channel_id);
}

MVCCID page_server::pts_mvcc_tracker::get_global_oldest_active_mvccid ()
{
  std::lock_guard<std::mutex> lockg { m_pts_oldest_active_mvccids_mtx };

  MVCCID oldest_mvccid = MVCCID_LAST;
  for (const auto &it : m_pts_oldest_active_mvccids)
    {
      if (oldest_mvccid > it.second)
	{
	  oldest_mvccid = it.second;
	}
    }

  /* it can return either
   * - MVCCID_LAST: no PTS is being tracked
   * - or MVCCID_ALL_VISIBLE: at least one PTS has connected, but hasn't updated yet
   * - or the computed oldest one */
  return oldest_mvccid;
}

void
page_server::set_active_tran_server_connection (cubcomm::channel &&chn)
{
  assert (is_page_server ());

  chn.set_channel_name ("ATS_PS_comm");

  assert (chn.is_connection_alive ());
  const auto channel_id = chn.get_channel_id ();

  er_log_debug (ARG_FILE_LINE, "Active transaction server connected to this page server. Channel id: %s.\n",
		channel_id.c_str ());

  // Even if the functions set_active_tran_server_connection and set_passive_tran_server_connection are
  // called from the same thread (master-server connection handler) the mutex is actually needed.
  // The usage of the mutex is to synchronize with the disconnects which are trigered from each connection
  // handler's connection threads (inbound or outbound).
  std::lock_guard lk_guard { m_conn_mutex };

  if (m_active_tran_server_conn != nullptr)
    {
      // When [A]TS crashes there are two possibilities:
      //  - either the crash is detected an the connection is dropped - see abnormal_tran_server_disconnect
      //    (this happens because page server is still supposed to send data to the transaction server and
      //    is no longer able to do this as data is not consumed anymore)
      //  - or the crahs is not detected and the connection remains hanging
      disconnect_active_tran_server ();
    }

  m_active_tran_server_conn.reset (new tran_server_connection_handler (std::move (chn), transaction_server_type::ACTIVE,
				   *this));
}

void
page_server::set_passive_tran_server_connection (cubcomm::channel &&chn)
{
  assert (is_page_server ());

  chn.set_channel_name ("PTS_PS_comm");

  assert (chn.is_connection_alive ());
  const auto channel_id = chn.get_channel_id ();

  er_log_debug (ARG_FILE_LINE, "Passive transaction server connected to this page server. Channel id: %s.\n",
		channel_id.c_str ());

  {
    // Even if the functions set_active_tran_server_connection and set_passive_tran_server_connection are
    // called from the same thread (master-server connection handler) the mutex is actually needed.
    // The usage of the mutex is to synchronize with the disconnects which are trigered from each connection
    // handler's connection threads (inbound or outbound).
    std::lock_guard lk_guard { m_conn_mutex };

    m_passive_tran_server_conn.emplace_back (new tran_server_connection_handler (std::move (chn),
	transaction_server_type::PASSIVE,
	*this));
  }

  m_pts_mvcc_tracker.init_oldest_active_mvccid (channel_id);
}

void
page_server::set_follower_page_server_connection (cubcomm::channel &&chn)
{
  chn.set_channel_name ("PS_PS_catchup_comm");

  assert (chn.is_connection_alive ());
  const auto channel_id = chn.get_channel_id ();

  m_follower_conn_vec.emplace_back (new follower_connection_handler (std::move (chn), *this));

  er_log_debug (ARG_FILE_LINE,
		"A follower page server connected to this page server to catch up. Channel id: %s.\n",
		channel_id.c_str ());
}

int
page_server::connect_to_followee_page_server (std::string &&hostname, int32_t port)
{
  auto ps_conn_error_lambda = [&hostname] ()
  {
    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_PAGESERVER_CONNECTION, 1, hostname.c_str ());
    return ER_NET_PAGESERVER_CONNECTION;
  };

  constexpr int CHANNEL_POLL_TIMEOUT = 1000;    // 1000 milliseconds = 1 second
  cubcomm::server_channel srv_chn (m_server_name.c_str (), SERVER_TYPE_PAGE, CHANNEL_POLL_TIMEOUT);

  srv_chn.set_channel_name ("PS_PS_catchup_comm");

  auto comm_error_code = srv_chn.connect (hostname.c_str (), port, CMD_SERVER_SERVER_CONNECT);
  if (comm_error_code != css_error_code::NO_ERRORS)
    {
      return ps_conn_error_lambda ();
    }

  constexpr auto conn_type = cubcomm::server_server::CONNECT_PAGE_TO_PAGE_SERVER;
  if (srv_chn.send_int (static_cast<int> (conn_type)) != NO_ERRORS)
    {
      return ps_conn_error_lambda ();
    }

  int returned_code;
  if (srv_chn.recv_int (returned_code) != css_error_code::NO_ERRORS)
    {
      return ps_conn_error_lambda ();
    }
  if (returned_code != static_cast<int> (conn_type))
    {
      return ps_conn_error_lambda ();
    }

  assert (m_followee_conn == nullptr);
  m_followee_conn.reset (new followee_connection_handler (std::move (srv_chn), *this));

  er_log_debug (ARG_FILE_LINE,
		"This page server successfully connected to the followee page server to catch up. Channel id: %s.\n",
		srv_chn.get_channel_id ().c_str ());

  return NO_ERROR;
}

void
page_server::disconnect_active_tran_server ()
{
  if (m_active_tran_server_conn != nullptr)
    {
      er_log_debug (ARG_FILE_LINE, "disconnect_active_tran_server:"
		    " Disconnect active transaction server connection with channel id: %s.\n",
		    m_active_tran_server_conn->get_connection_id ().c_str ());
      m_active_tran_server_conn.reset (nullptr);
    }
  else
    {
      er_log_debug (ARG_FILE_LINE, "disconnect_active_tran_server: Active transaction server is not connected.\n");
    }
}

void
page_server::disconnect_tran_server_async (const tran_server_connection_handler *conn)
{
  assert (conn != nullptr);

  std::lock_guard lk_guard { m_conn_mutex };

  if (conn == m_active_tran_server_conn.get ())
    {
      er_log_debug (ARG_FILE_LINE, "The active transaction server is disconnected. Channel id: %s.\n",
		    conn->get_connection_id ().c_str ());
      m_async_disconnect_handler.disconnect (std::move (m_active_tran_server_conn));
      assert (m_active_tran_server_conn == nullptr);
    }
  else
    {
      for (auto it = m_passive_tran_server_conn.begin (); it != m_passive_tran_server_conn.end (); ++it)
	{
	  if (conn == it->get ())
	    {
	      er_log_debug (ARG_FILE_LINE, "The passive transaction server is disconnected. Channel id: %s.\n",
			    (*it)->get_connection_id ().c_str ());
	      m_async_disconnect_handler.disconnect (std::move (*it));
	      assert (*it == nullptr);
	      m_passive_tran_server_conn.erase (it);
	      break;
	    }
	}
    }

  m_conn_cv.notify_one ();
}

void
page_server::disconnect_all_tran_servers ()
{
  std::unique_lock ulock { m_conn_mutex };

  /* Request the ATS to disconnect */
  if (m_active_tran_server_conn == nullptr)
    {
      er_log_debug (ARG_FILE_LINE, "disconnect_all_tran_server: Active transaction server is not connected.\n");
    }
  else
    {
      er_log_debug (ARG_FILE_LINE,
		    "disconnect_all_tran_server: Request active transaction server to disconnect. Channel id: %s.\n",
		    m_active_tran_server_conn->get_connection_id ().c_str ());
      m_active_tran_server_conn->push_disconnection_request ();
    }

  /* Request all PTSes to disconnect */
  if (m_passive_tran_server_conn.empty ())
    {
      er_log_debug (ARG_FILE_LINE, "disconnect_all_tran_server: No passive transaction server connected.\n");
    }
  else
    {
      for (size_t i = 0; i < m_passive_tran_server_conn.size (); i++)
	{
	  er_log_debug (ARG_FILE_LINE,
			"disconnect_all_tran_server: Request passive transaction server to disconnect. Channel id: %s.\n",
			m_passive_tran_server_conn[i]->get_connection_id ().c_str ());
	  m_passive_tran_server_conn[i]->remove_prior_sender_sink ();
	  m_passive_tran_server_conn[i]->push_disconnection_request ();
	}
    }

  er_log_debug (ARG_FILE_LINE,
		"disconnect_all_tran_server: Wait until all connections are disconnected.\n");

  /*
   *  m_active_tran_server_conn == nullptr : The ATS has been disconnected or disconnected in progress (by async_disconnect_handler)
   *  The connection for a PTS is not in m_passive_tran_server_conn : the PTS has been disconnected or disconnected in progress(by async_disconnect_handler)
   *
   *  The disconnection of a TS starts when tran_to_page_request::SEND_DISCONNECT_MSG is arrived from the TS. It's either tiggered by page_to_tran_request::SEND_DISCONNECT_REQUEST_MSG from this page server or just initiated by the TS itself.
   *
   *  If some disconnections are underway, they will be waited for at the m_async_disconnect_handler.terminate () below.
   */
  constexpr auto millis_20 = std::chrono::milliseconds { 20 };
  while (!m_conn_cv.wait_for (ulock, millis_20, [this]
  {
    return m_active_tran_server_conn == nullptr && m_passive_tran_server_conn.empty ();
    }));

  ulock.unlock ();

  m_async_disconnect_handler.terminate ();

  er_log_debug (ARG_FILE_LINE, "disconnect_all_tran_server: All connections have been disconnected.\n");
}

bool
page_server::is_active_tran_server_connected () const
{
  assert (is_page_server ());

  return m_active_tran_server_conn != nullptr;
}

page_server::tran_server_responder_t &
page_server::get_tran_server_responder ()
{
  assert (m_tran_server_responder);
  return *m_tran_server_responder;
}

page_server::follower_responder_t &
page_server::get_follower_responder ()
{
  assert (m_follower_responder);
  return *m_follower_responder;
}

void
page_server::push_request_to_active_tran_server (page_to_tran_request reqid, std::string &&payload)
{
  assert (is_page_server ());

  if (is_active_tran_server_connected ())
    {
      m_active_tran_server_conn->push_request (reqid, std::move (payload));
    }
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
  m_replicator.reset (new cublog::replicator (start_lsa, RECOVERY_PAGE, replication_parallel_count, TT_REPLICATION_PS));
}

void
page_server::finish_replication_during_shutdown (cubthread::entry &thread_entry)
{
  assert (m_replicator != nullptr);

  // at this point, no connection to transaction server should be active
  // after the replicator is destroyed, no further requests should be processed
  assert (m_active_tran_server_conn == nullptr);
  assert (m_passive_tran_server_conn.empty ());

  logpb_force_flush_pages (&thread_entry);
  m_replicator->wait_replication_finish_during_shutdown ();
  m_replicator.reset (nullptr);
}

void
page_server::init_request_responder ()
{
  m_tran_server_responder = std::make_unique<tran_server_responder_t> ();
  m_follower_responder = std::make_unique<follower_responder_t> ();
}

void
page_server::finalize_request_responder ()
{
  m_tran_server_responder.reset (nullptr);
  m_follower_responder.reset (nullptr);
}
