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
  request_handlers_map_t::value_type log_boot_info_handler_value =
	  std::make_pair (page_to_tran_request::SEND_LOG_BOOT_INFO,
			  std::bind (&passive_tran_server::receive_log_boot_info,
				     std::ref (*this), std::placeholders::_1));

  std::map<page_to_tran_request, std::function<void (cubpacking::unpacker &upk)>> handlers_map =
	      tran_server::get_request_handlers ();

  handlers_map.insert (log_boot_info_handler_value);

  return handlers_map;
}

void
passive_tran_server::receive_log_boot_info (cubpacking::unpacker &upk)
{
  std::string message;
  upk.unpack_string (message);

  // pass to caller thread; it has the thread context needed to access engine functions
  {
    std::lock_guard<std::mutex> lockg { m_log_boot_info_mtx };
    m_log_boot_info.swap (message);
  }
  m_log_boot_info_condvar.notify_one ();
}

void passive_tran_server::send_and_receive_log_boot_info (THREAD_ENTRY *thread_p)
{
  assert (m_log_boot_info.empty ());

  // empty message request
  push_request (tran_to_page_request::SEND_LOG_INIT_BOOT_FETCH, std::string ());

  {
    std::unique_lock<std::mutex> ulock { m_log_boot_info_mtx };
    // TODO: wait_for a limited time in case page server hangs
    m_log_boot_info_condvar.wait (ulock, [this] ()
    {
      return !m_log_boot_info.empty ();
    });
  }

  const int log_page_size = db_log_page_size ();
  assert (m_log_boot_info.size () == sizeof (struct log_header) + log_page_size + sizeof (struct log_lsa));

  const char *message_buf = m_log_boot_info.c_str ();

  // log header, copy and initialize header
  const struct log_header *const log_hdr = reinterpret_cast<const struct log_header *> (message_buf);
  log_Gl.hdr = *log_hdr;
  message_buf += sizeof (struct log_header);

  // log append
  assert (log_Gl.append.log_pgptr == nullptr);
  /* fetch the append page; append pages are always new pages */
  log_Gl.append.log_pgptr = logpb_create_page (thread_p, log_Gl.hdr.append_lsa.pageid);
  std::memcpy (reinterpret_cast<char *> (log_Gl.append.log_pgptr), message_buf, log_page_size);
  message_buf += log_page_size;

  // prev lsa
  std::memcpy (&log_Gl.append.prev_lsa, message_buf, sizeof (struct log_lsa));
  message_buf += sizeof (struct log_lsa);

  // safe-guard that the message has been consumed
  assert (message_buf == m_log_boot_info.c_str () + m_log_boot_info.size ());

  // do not leave m_log_boot_info empty as a safeguard as this function is only supposed
  // to be called once
  m_log_boot_info = "not empty";

  if (prm_get_bool_value (PRM_ID_ER_LOG_PRIOR_TRANSFER))
    {
      _er_log_debug (ARG_FILE_LINE,
		     "Received log boot info to from page server with prev_lsa = (%lld|%d), append_lsa = (%lld|%d)\n",
		     LSA_AS_ARGS (&log_Gl.append.prev_lsa), LSA_AS_ARGS (&log_Gl.hdr.append_lsa));
    }
}
