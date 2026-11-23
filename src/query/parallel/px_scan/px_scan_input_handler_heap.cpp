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
 * px_scan_input_handler_heap.cpp
 */

#include "px_scan_input_handler_heap.hpp"
#include "error_code.h"
#include "heap_file.h"
#include "log_impl.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_scan
{
  thread_local HEAP_SCANCACHE *input_handler_heap::m_tl_scan_cache = NULL;
  thread_local ftab_set *input_handler_heap::m_tl_ftab_set = NULL;
  thread_local VPID input_handler_heap::m_tl_vpid = VPID_INITIALIZER;
  thread_local size_t input_handler_heap::m_tl_pgoffset = 0;
  thread_local FILE_PARTIAL_SECTOR input_handler_heap::m_tl_ftab = FILE_PARTIAL_SECTOR_INITIALIZER;

  int input_handler_heap::initialize (THREAD_ENTRY *thread_p, HFID *hfid, SCAN_ID *scan_id)
  {
    m_tl_scan_cache = &scan_id->s.hsid.scan_cache;
    /* open_scan should have succeeded */
    assert (m_tl_scan_cache->debug_initpattern == 12345);
    int idx = m_splited_ftab_set_idx.fetch_add (1);
    if (idx < 0 || (size_t) idx >= m_splited_ftab_set.size ())
      {
	assert_release (false);
	return ER_FAILED;
      }
    m_tl_ftab_set = &m_splited_ftab_set[idx];
    m_tl_vpid = VPID_INITIALIZER;
    m_tl_pgoffset = 0;
    m_tl_ftab = FILE_PARTIAL_SECTOR_INITIALIZER;
    return NO_ERROR;
  }

  int input_handler_heap::init_on_main (THREAD_ENTRY *thread_p, HFID hfid, int parallelism,
					const FILE_FTAB_COLLECTOR *page_list)
  {
    m_hfid = hfid;

    /* the page bitmap the scan's open took after the statement's snapshot (scan_heap_scan_page_list ()), split
     * among the workers again at every open and rewind */
    m_ftab_set.convert (page_list);
    m_splited_ftab_set = m_ftab_set.split (parallelism);
    m_splited_ftab_set_idx.store (0);
    m_ftab_set.clear();
    return NO_ERROR;
  }

  SCAN_CODE input_handler_heap::get_next_vpid_with_fix (THREAD_ENTRY *thread_p, VPID *vpid)
  {
    for (;;)
      {
	if (VPID_ISNULL (&m_tl_vpid))
	  {
	    m_tl_ftab = m_tl_ftab_set->get_next();
	    if (VSID_IS_NULL (&m_tl_ftab.vsid))
	      {
		return S_END;
	      }
	    m_tl_pgoffset = 0;
	    m_tl_vpid.volid = m_tl_ftab.vsid.volid;
	    m_tl_vpid.pageid = SECTOR_FIRST_PAGEID (m_tl_ftab.vsid.sectid);
	  }

	SCAN_CODE code = heap_page_list_fix_next (thread_p, &m_hfid, &m_tl_ftab, &m_tl_pgoffset,
			 &m_tl_scan_cache->page_watcher, vpid);
	if (code == S_SUCCESS)
	  {
	    return S_SUCCESS;
	  }
	if (code == S_ERROR)
	  {
	    m_err_messages_p->move_top_error_message_to_this ();
	    m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	    return S_ERROR;
	  }
	/* the sector is done */
	VPID_SET_NULL (&m_tl_vpid);
      }
  }

  int input_handler_heap::finalize (THREAD_ENTRY *thread_p)
  {
    if (m_tl_scan_cache->page_watcher.pgptr != NULL)
      {
	pgbuf_ordered_unfix (thread_p, &m_tl_scan_cache->page_watcher);
      }
    m_tl_scan_cache = NULL;
    return NO_ERROR;
  }
}
