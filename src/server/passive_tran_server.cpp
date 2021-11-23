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
#include "thread_manager.hpp"

// non-owning "shadow" pointer of globally visible ps_Gl
passive_tran_server *pts_Gl = nullptr;

void
init_passive_tran_server_shadow_ptr (passive_tran_server *ptr)
{
  assert (pts_Gl == nullptr);
  assert (ptr != nullptr);

  pts_Gl = ptr;
}

void
reset_passive_tran_server_shadow_ptr ()
{
  assert (pts_Gl != nullptr);

  pts_Gl = nullptr;
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
  request_handlers_map_t::value_type log_header_log_append_prev_lsa_handler_value =
	  std::make_pair (page_to_tran_request::SEND_LOG_HEADER_LOG_APPEND_PREV_LSA,
			  std::bind (&passive_tran_server::receive_log_header_log_append_prev_lsa,
				     std::ref (*this), std::placeholders::_1));

  std::map<page_to_tran_request, std::function<void (cubpacking::unpacker &upk)>> handlers_map =
	      tran_server::get_request_handlers ();

  handlers_map.insert (log_header_log_append_prev_lsa_handler_value);

  return handlers_map;
}

void
passive_tran_server::receive_log_header_log_append_prev_lsa (cubpacking::unpacker &upk)
{
  std::string message;
  upk.unpack_string (message);

  const int log_page_size = db_log_page_size ();
  assert (message.size () == (2 * log_page_size + sizeof (struct log_lsa)));

  // pass to caller thread; it has the thread context needed to access engine functions
  {
    std::lock_guard<std::mutex> lockg { m_log_header_log_append_prev_lsa_mtx };
    m_log_header_log_append_prev_lsa.swap (message);
  }
  m_log_header_log_append_prev_lsa_condvar.notify_one ();
}

void passive_tran_server::send_and_receive_log_header_log_append_prev_lsa (THREAD_ENTRY *thread_p)
{
  assert (m_log_header_log_append_prev_lsa.empty ());

  // empty message request
  push_request (tran_to_page_request::SEND_LOG_HEADER_LOG_APPEND_PREV_LSA_FETCH, std::string ());

  {
    std::unique_lock<std::mutex> ulock { m_log_header_log_append_prev_lsa_mtx };
    // TODO: wait_for a limited time in case page server hangs
    m_log_header_log_append_prev_lsa_condvar.wait (ulock, [this] ()
    {
      return !m_log_header_log_append_prev_lsa.empty ();
    });
  }

  const char *message_buf = m_log_header_log_append_prev_lsa.c_str ();
  const int log_page_size = db_log_page_size ();

  // log header, copy and initialize header
  assert (log_Gl.loghdr_pgptr != nullptr);
  std::memcpy (reinterpret_cast<char *> (log_Gl.loghdr_pgptr), message_buf, log_page_size);
  LOG_HEADER *const log_hdr = reinterpret_cast<LOG_HEADER *> (log_Gl.loghdr_pgptr->area);
  log_Gl.hdr = *log_hdr;
  message_buf += log_page_size;

  // log append
  assert (log_Gl.append.log_pgptr == nullptr);
  /* fetch the append page; append pages are always new pages */
  log_Gl.append.log_pgptr = logpb_create_page (thread_p, log_Gl.hdr.append_lsa.pageid);
  std::memcpy (reinterpret_cast<char *> (log_Gl.append.log_pgptr), message_buf, log_page_size);
  message_buf += log_page_size;

  // prev lsa
  std::memcpy (&log_Gl.append.prev_lsa, message_buf, sizeof (struct log_lsa));

  // do not leave m_log_header_log_append_prev_lsa empty as a safeguard as this function is only supposed
  // to be called once
  m_log_header_log_append_prev_lsa = "not empty";
}

void send_and_receive_log_header_log_append_prev_lsa (THREAD_ENTRY *thread_p)
{
  assert (pts_Gl != nullptr);

  pts_Gl->send_and_receive_log_header_log_append_prev_lsa (thread_p);
}
