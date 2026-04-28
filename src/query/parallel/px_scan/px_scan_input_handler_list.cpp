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
 * px_scan_input_handler_list.cpp
 */

#include "px_scan_input_handler_list.hpp"
#include "bit.h"
#include "error_code.h"
#include "error_manager.h"
#include "list_file.h"
#include "object_representation.h"	/* OR_GET_INT used by QFILE_GET_TUPLE_COUNT */
#include "query_list.h"
#include "query_manager.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_scan
{
  thread_local input_handler_list::worker_slice *input_handler_list::m_tl_slice = nullptr;
  thread_local UINT64 input_handler_list::m_tl_bitmap = 0;
  thread_local VSID input_handler_list::m_tl_vsid = {NULL_SECTID, NULL_VOLID};
  thread_local QMGR_TEMP_FILE *input_handler_list::m_tl_current_tfile = nullptr;
  thread_local bool input_handler_list::m_tl_is_membuf_worker = false;
  thread_local int input_handler_list::m_tl_membuf_pageid = 0;

  int
  input_handler_list::init_on_main (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id, int parallelism)
  {
    /* Trivial: free + empty slices; leave m_list_id untouched. */
    if (parallelism <= 0 || list_id == nullptr || VPID_ISNULL (&list_id->first_vpid))
      {
	qfile_free_list_sector_info (thread_p, &m_sector_info);
	m_worker_slices.clear ();
	m_worker_slices.resize (parallelism > 0 ? parallelism : 0);
	m_worker_slice_idx.store (0);
	return NO_ERROR;
      }

    qfile_free_list_sector_info (thread_p, &m_sector_info);

    /* Defer publishing m_list_id until collect succeeds. */
    int error_code = qfile_collect_list_sector_info (thread_p, list_id, &m_sector_info);
    if (error_code != NO_ERROR)
      {
	m_list_id = nullptr;
	return error_code;
      }

    m_list_id = list_id;

    int n = m_sector_info.sector_cnt;
    int per = n / parallelism;
    int rem = n % parallelism;

    m_worker_slices.clear ();
    m_worker_slices.resize (parallelism);

    int cur = 0;
    for (int i = 0; i < parallelism; i++)
      {
	int sz = per + (i < rem ? 1 : 0);
	m_worker_slices[i].start = cur;
	m_worker_slices[i].end = cur + sz;
	m_worker_slices[i].iter = cur;
	cur += sz;
      }

    m_worker_slice_idx.store (0);
    return NO_ERROR;
  }

  int
  input_handler_list::initialize (THREAD_ENTRY *thread_p, HFID *hfid, SCAN_ID *scan_id)
  {
    int idx = m_worker_slice_idx.fetch_add (1);
    if (idx < 0 || (size_t) idx >= m_worker_slices.size ())
      {
	assert_release (false);
	return ER_FAILED;
      }

    m_tl_slice = &m_worker_slices[idx];
    m_tl_bitmap = 0;
    VSID_SET_NULL (&m_tl_vsid);
    m_tl_current_tfile = nullptr;

    m_tl_is_membuf_worker = (idx == 0 && m_sector_info.membuf_tfile != nullptr);
    m_tl_membuf_pageid = 0;

    return NO_ERROR;
  }

  SCAN_CODE
  input_handler_list::get_next_page_with_fix (THREAD_ENTRY *thread_p,
      PAGE_PTR &out_page,
      QMGR_TEMP_FILE *&out_tfile)
  {
    out_page = nullptr;
    out_tfile = nullptr;

    while (true)
      {
	VPID vpid;

	/* Phase 1: worker 0 drains membuf pages (NULL_VOLID, sequential pageid). */
	if (m_tl_is_membuf_worker
	    && m_sector_info.membuf_tfile != nullptr
	    && m_tl_membuf_pageid <= m_sector_info.membuf_tfile->membuf_last)
	  {
	    vpid.volid = NULL_VOLID;
	    vpid.pageid = m_tl_membuf_pageid++;
	    m_tl_current_tfile = m_sector_info.membuf_tfile;
	  }
	else
	  {
	    /* Phase 2: refill bitmap from next sector when exhausted. */
	    if (m_tl_bitmap == 0)
	      {
		int sidx = m_tl_slice->iter++;
		if (sidx >= m_tl_slice->end)
		  {
		    return S_END;
		  }
		m_tl_vsid = m_sector_info.sectors[sidx].vsid;
		m_tl_bitmap = m_sector_info.sectors[sidx].page_bitmap;
		m_tl_current_tfile = (QMGR_TEMP_FILE *) m_sector_info.tfiles[sidx];
		if (m_tl_bitmap == 0)
		  {
		    continue;	/* defensive: empty sector */
		  }
	      }

#if defined(__GNUC__) || defined(__clang__)
	    int bit_pos = __builtin_ctzll (m_tl_bitmap);
#else
	    int bit_pos = bit64_count_trailing_zeros (m_tl_bitmap);
#endif
	    m_tl_bitmap &= m_tl_bitmap - 1;	/* clear lowest set bit */

	    vpid.volid = m_tl_vsid.volid;
	    vpid.pageid = SECTOR_FIRST_PAGEID (m_tl_vsid.sectid) + bit_pos;
	  }

	/* Single READ-latch fix; transfer ownership to caller on S_SUCCESS. */
	PAGE_PTR page_p = qmgr_get_old_page_read_only (thread_p, &vpid, m_tl_current_tfile);
	if (page_p == nullptr)
	  {
	    assert_release_error (er_errid () != NO_ERROR);
	    return S_ERROR;
	  }

	/* Overflow continuations: the start-page owner walks the chain via qfile_get_tuple. */
	if (QFILE_GET_TUPLE_COUNT (page_p) == QFILE_OVERFLOW_TUPLE_COUNT_FLAG)
	  {
	    qmgr_free_old_page (thread_p, page_p, m_tl_current_tfile);
	    continue;
	  }

	out_page = page_p;
	out_tfile = m_tl_current_tfile;
	return S_SUCCESS;
      }
  }

  int
  input_handler_list::finalize (THREAD_ENTRY *thread_p)
  {
    m_tl_slice = nullptr;
    m_tl_bitmap = 0;
    VSID_SET_NULL (&m_tl_vsid);
    m_tl_current_tfile = nullptr;
    m_tl_is_membuf_worker = false;
    m_tl_membuf_pageid = 0;
    return NO_ERROR;
  }

  void
  input_handler_list::cleanup_on_main (THREAD_ENTRY *thread_p)
  {
    qfile_free_list_sector_info (thread_p, &m_sector_info);
    m_worker_slices.clear ();
    m_worker_slice_idx.store (0);
    m_list_id = nullptr;
  }
}
