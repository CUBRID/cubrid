/*
 *
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

/*
 * px_heap_scan_input_handler_single_table.cpp
 */


#include "px_heap_scan_input_handler_single_table.hpp"

#if !defined(NDEBUG)
#include <sys/syscall.h>
#include "error_manager.h"
#endif

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_heap_scan
{
  thread_local HEAP_SCANCACHE *input_handler_single_table::m_tl_scan_cache = NULL;
  thread_local PGBUF_WATCHER input_handler_single_table::m_tl_old_page_watcher = {0};
  SCAN_CODE input_handler_single_table::get_next_vpid_with_fix (THREAD_ENTRY *thread_p, VPID *vpid)
  {
    std::unique_lock<std::mutex> lock (m_vpid_mutex);
    VPID ret_vpid;
    SCAN_CODE ret_code;
    if (m_vpid_ended)
      {
	if (m_tl_old_page_watcher.pgptr != NULL)
	  {
	    pgbuf_ordered_unfix (thread_p, &m_tl_old_page_watcher);
	  }
	return S_END;
      }
    if (VPID_ISNULL (&m_vpid))
      {
	m_vpid.pageid = m_hfid.hpgid;
	m_vpid.volid = m_hfid.vfid.volid;
      }
    *vpid = m_vpid;
    if (m_tl_scan_cache->page_watcher.pgptr != NULL)
      {
	pgbuf_replace_watcher (thread_p, &m_tl_scan_cache->page_watcher, &m_tl_old_page_watcher);
      }
    ret_code = heap_page_next_fix_old (thread_p, &m_hfid, &m_vpid, m_tl_scan_cache);
    if (m_tl_old_page_watcher.pgptr != NULL)
      {
	pgbuf_ordered_unfix (thread_p, &m_tl_old_page_watcher);
      }
    if (ret_code == S_END)
      {
	m_vpid_ended = true;
	ret_code = S_SUCCESS;
      }
    if (ret_code == S_ERROR)
      {
	m_err_messages_p->move_top_error_message_to_this();
	m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	return S_ERROR;
      }
    assert (m_tl_scan_cache->page_watcher.pgptr != NULL);
    return ret_code;
  }

  int input_handler_single_table::initialize (THREAD_ENTRY *thread_p, HFID *hfid, SCAN_ID *scan_id)
  {
    m_tl_scan_cache = &scan_id->s.hsid.scan_cache;
    /* open_scan should have succeeded */
    assert (m_tl_scan_cache->debug_initpattern == 12345);
    PGBUF_INIT_WATCHER (&m_tl_old_page_watcher, PGBUF_ORDERED_HEAP_NORMAL, hfid);
    std::unique_lock<std::mutex> lock (m_vpid_mutex);
    if (m_hfid.hpgid == NULL_PAGEID)
      {
	m_hfid = *hfid;
      }
    return NO_ERROR;
  }

  int input_handler_single_table::finalize (THREAD_ENTRY *thread_p)
  {
    if (m_tl_old_page_watcher.pgptr != NULL)
      {
	pgbuf_ordered_unfix (thread_p, &m_tl_old_page_watcher);
      }
    if (m_tl_scan_cache->page_watcher.pgptr != NULL)
      {
	pgbuf_ordered_unfix (thread_p, &m_tl_scan_cache->page_watcher);
      }
    m_tl_scan_cache = NULL;
    m_tl_old_page_watcher.pgptr = NULL;
    return NO_ERROR;
  }
}
