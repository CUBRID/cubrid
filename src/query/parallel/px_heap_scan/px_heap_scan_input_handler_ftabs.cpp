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
 * px_heap_scan_input_handler_ftabs.cpp
 */


#include "px_heap_scan_input_handler_ftabs.hpp"
#include "error_code.h"
#include "bit.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_heap_scan
{
  thread_local HEAP_SCANCACHE *input_handler_ftabs::m_tl_scan_cache = NULL;
  thread_local PGBUF_WATCHER input_handler_ftabs::m_tl_old_page_watcher = {0};
  thread_local ftab_set *input_handler_ftabs::m_tl_ftab_set = NULL;
  thread_local VPID input_handler_ftabs::m_tl_vpid = VPID_INITIALIZER;
  thread_local size_t input_handler_ftabs::m_tl_pgoffset = 0;
  thread_local FILE_PARTIAL_SECTOR input_handler_ftabs::m_tl_ftab = FILE_PARTIAL_SECTOR_INITIALIZER;


  int input_handler_ftabs::initialize (THREAD_ENTRY *thread_p, HFID *hfid, SCAN_ID *scan_id)
  {
    m_tl_scan_cache = &scan_id->s.hsid.scan_cache;
    /* open_scan should have succeeded */
    assert (m_tl_scan_cache->debug_initpattern == 12345);
    PGBUF_INIT_WATCHER (&m_tl_old_page_watcher, PGBUF_ORDERED_HEAP_NORMAL, hfid);
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

  int input_handler_ftabs::init_on_main (THREAD_ENTRY *thread_p, HFID hfid, int parallelism)
  {
    FILE_FTAB_COLLECTOR collector;
    int error_code;
    m_hfid = hfid;

    error_code = file_get_all_data_sectors (thread_p, &m_hfid.vfid, &collector);
    if (error_code != NO_ERROR)
      {
	if (collector.partsect_ftab != NULL)
	  {
	    db_private_free_and_init (thread_p, collector.partsect_ftab);
	  }
	return error_code;
      }
    m_ftab_set.convert (&collector);
    m_splited_ftab_set = m_ftab_set.split (parallelism);
    m_splited_ftab_set_idx.store (0);
    m_ftab_set.clear();

    if (collector.partsect_ftab != NULL)
      {
	db_private_free_and_init (thread_p, collector.partsect_ftab);
      }
    return NO_ERROR;
  }

  SCAN_CODE input_handler_ftabs::get_next_vpid_with_fix (THREAD_ENTRY *thread_p, VPID *vpid)
  {
    int error_code = NO_ERROR;

    bool found = false;
    while (!found)
      {
	if (VPID_ISNULL (&m_tl_vpid))
	  {
	    m_tl_ftab = m_tl_ftab_set->get_next();
	    if (VSID_IS_NULL (&m_tl_ftab.vsid))
	      {
		if (m_tl_old_page_watcher.pgptr != NULL)
		  {
		    pgbuf_ordered_unfix (thread_p, &m_tl_old_page_watcher);
		  }
		return S_END;
	      }
	    m_tl_pgoffset = 0;
	    m_tl_vpid.volid = m_tl_ftab.vsid.volid;
	    m_tl_vpid.pageid = SECTOR_FIRST_PAGEID (m_tl_ftab.vsid.sectid);
	    if (m_tl_vpid.volid == m_hfid.vfid.volid && m_tl_vpid.pageid == m_hfid.vfid.fileid)
	      {
		/* skip heap header page */
		m_tl_pgoffset++;
		m_tl_vpid.pageid++;
	      }
	  }

	for (; m_tl_pgoffset < DISK_SECTOR_NPAGES; m_tl_pgoffset++, m_tl_vpid.pageid++)
	  {
	    if (bit64_is_set (m_tl_ftab.page_bitmap, (int) m_tl_pgoffset))
	      {
		found = true;

		if (m_tl_scan_cache->page_watcher.pgptr != NULL)
		  {
		    pgbuf_replace_watcher (thread_p, &m_tl_scan_cache->page_watcher, &m_tl_old_page_watcher);
		  }

		error_code = pgbuf_ordered_fix (thread_p, &m_tl_vpid, OLD_PAGE_MAYBE_DEALLOCATED, PGBUF_LATCH_READ,
						&m_tl_scan_cache->page_watcher);

		if (m_tl_scan_cache->page_watcher.pgptr == NULL)
		  {
		    if (error_code != NO_ERROR && error_code != ER_PB_BAD_PAGEID)
		      {
			/* non-dealloc error (e.g. ER_INTERRUPTED): propagate */
			m_err_messages_p->move_top_error_message_to_this ();
			m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
			return S_ERROR;
		      }
		    /* when bitmap is built, that page was valid.
		     * but now, it's deallocated in some reasons.
		     * this is not error, it can be ignored */
		    if (m_tl_old_page_watcher.pgptr != NULL)
		      {
			pgbuf_ordered_unfix (thread_p, &m_tl_old_page_watcher);
		      }
		    er_clear ();
		    found = false;
		    continue;
		  }

		if (m_tl_old_page_watcher.pgptr != NULL)
		  {
		    pgbuf_ordered_unfix (thread_p, &m_tl_old_page_watcher);
		  }

		assert (pgbuf_get_page_ptype (thread_p, m_tl_scan_cache->page_watcher.pgptr) == PAGE_HEAP);

		if (error_code != NO_ERROR)
		  {
		    m_err_messages_p->move_top_error_message_to_this();
		    m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
		    return S_ERROR;
		  }

		*vpid = m_tl_vpid;
		m_tl_pgoffset++;
		m_tl_vpid.pageid++;
		return S_SUCCESS;
	      }
	  }

	if (m_tl_pgoffset >= DISK_SECTOR_NPAGES)
	  {
	    VPID_SET_NULL (&m_tl_vpid);
	  }
      }
    return S_ERROR;	/* unreachable */
  }

  int input_handler_ftabs::finalize (THREAD_ENTRY *thread_p)
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
