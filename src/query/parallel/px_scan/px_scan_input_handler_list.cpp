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
#include "object_representation.h"
#include "query_manager.h"
#include "query_list.h"
#include "error_code.h"
#include "error_manager.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_scan
{
  thread_local std::vector<VPID> *input_handler_list::m_tl_vpid_list = nullptr;
  thread_local int input_handler_list::m_tl_vpid_idx = 0;

  int
  input_handler_list::init_on_main (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id, int parallelism)
  {
    m_list_id = list_id;

    if (parallelism <= 0 || list_id == nullptr || VPID_ISNULL (&list_id->first_vpid))
      {
	m_split_vpids.clear ();
	m_split_vpids.resize (parallelism);
	m_split_vpids_idx.store (0);
	return NO_ERROR;
      }

    /* Traverse page chain and collect all VPIDs */
    std::vector<VPID> all_vpids;
    VPID cur_vpid = list_id->first_vpid;
    while (!VPID_ISNULL (&cur_vpid))
      {
	all_vpids.push_back (cur_vpid);
	PAGE_PTR page_p = qmgr_get_old_page (thread_p, &cur_vpid, list_id->tfile_vfid);
	if (page_p == nullptr)
	  {
	    return ER_FAILED;
	  }
	VPID next_vpid;
	QFILE_GET_NEXT_VPID (&next_vpid, page_p);
	qmgr_free_old_page (thread_p, page_p, list_id->tfile_vfid);
	cur_vpid = next_vpid;
      }

    /* Split VPIDs among workers */
    m_split_vpids.clear ();
    m_split_vpids.resize (parallelism);
    size_t total = all_vpids.size ();
    size_t per_worker = total / (size_t) parallelism;
    size_t remainder = total % (size_t) parallelism;
    size_t start = 0;
    for (int i = 0; i < parallelism; i++)
      {
	size_t count = per_worker + ((size_t) i < remainder ? 1 : 0);
	m_split_vpids[i] = std::vector<VPID> (all_vpids.begin () + start,
					      all_vpids.begin () + start + count);
	start += count;
      }
    m_split_vpids_idx.store (0);
    return NO_ERROR;
  }

  int
  input_handler_list::initialize (THREAD_ENTRY *thread_p, HFID *hfid, SCAN_ID *scan_id)
  {
    int idx = m_split_vpids_idx.fetch_add (1);
    if (idx < 0 || (size_t) idx >= m_split_vpids.size ())
      {
	assert_release (false);
	return ER_FAILED;
      }
    m_tl_vpid_list = &m_split_vpids[idx];
    m_tl_vpid_idx = 0;
    return NO_ERROR;
  }

  SCAN_CODE
  input_handler_list::get_next_vpid_with_fix (THREAD_ENTRY *thread_p, VPID *vpid)
  {
    if (m_tl_vpid_list == nullptr || m_tl_vpid_idx >= (int) m_tl_vpid_list->size ())
      {
	return S_END;
      }
    *vpid = (*m_tl_vpid_list)[m_tl_vpid_idx++];
    return S_SUCCESS;
  }

  int
  input_handler_list::finalize (THREAD_ENTRY *thread_p)
  {
    m_tl_vpid_list = nullptr;
    m_tl_vpid_idx = 0;
    return NO_ERROR;
  }
}
