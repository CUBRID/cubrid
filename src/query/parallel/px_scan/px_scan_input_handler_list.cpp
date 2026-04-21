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
#include "query_manager.h"
#include "query_list.h"
#include "error_code.h"
#include "error_manager.h"
#include "bit.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_scan
{
  thread_local ftab_set *input_handler_list::m_tl_ftab_set = nullptr;
  thread_local VPID input_handler_list::m_tl_vpid = VPID_INITIALIZER;
  thread_local size_t input_handler_list::m_tl_pgoffset = 0;
  thread_local FILE_PARTIAL_SECTOR input_handler_list::m_tl_ftab = FILE_PARTIAL_SECTOR_INITIALIZER;
  thread_local bool input_handler_list::m_tl_is_membuf_worker = false;
  thread_local int input_handler_list::m_tl_membuf_pageid = 0;

  int
  input_handler_list::init_on_main (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id, int parallelism)
  {
    m_list_id = list_id;

    if (parallelism <= 0 || list_id == nullptr || VPID_ISNULL (&list_id->first_vpid))
      {
	m_splited_ftab_set.clear ();
	m_splited_ftab_set.resize (parallelism > 0 ? parallelism : 0);
	m_splited_ftab_set_idx.store (0);
	m_has_membuf = false;
	m_membuf_last = -1;
	m_tfile_vfid = nullptr;
	return NO_ERROR;
      }

    /* Check for membuf */
    m_tfile_vfid = list_id->tfile_vfid;
    m_has_membuf = (m_tfile_vfid != NULL
		    && m_tfile_vfid->membuf != NULL
		    && m_tfile_vfid->membuf_last >= 0);
    if (m_has_membuf)
      {
	m_membuf_last = m_tfile_vfid->membuf_last;
      }
    else
      {
	m_membuf_last = -1;
      }

    /* Collect sectors from the base file */
    if (m_tfile_vfid != NULL && !VFID_ISNULL (&m_tfile_vfid->temp_vfid))
      {
	FILE_FTAB_COLLECTOR collector = FILE_FTAB_COLLECTOR_INITIALIZER;
	int error_code;

	error_code = file_get_all_data_sectors (thread_p, &m_tfile_vfid->temp_vfid, &collector);
	if (error_code != NO_ERROR)
	  {
	    if (collector.partsect_ftab != NULL)
	      {
		db_private_free_and_init (thread_p, collector.partsect_ftab);
	      }
	    return error_code;
	  }
	m_ftab_set.convert (&collector);
	if (collector.partsect_ftab != NULL)
	  {
	    db_private_free_and_init (thread_p, collector.partsect_ftab);
	  }
      }

    /* Collect sectors from dependent files */
    for (QFILE_LIST_ID *dep = list_id->dependent_list_id; dep != NULL; dep = dep->dependent_list_id)
      {
	if (dep->tfile_vfid == NULL || VFID_ISNULL (&dep->tfile_vfid->temp_vfid))
	  {
	    continue;
	  }

	FILE_FTAB_COLLECTOR dep_collector = FILE_FTAB_COLLECTOR_INITIALIZER;
	int error_code;

	error_code = file_get_all_data_sectors (thread_p, &dep->tfile_vfid->temp_vfid, &dep_collector);
	if (error_code != NO_ERROR)
	  {
	    if (dep_collector.partsect_ftab != NULL)
	      {
		db_private_free_and_init (thread_p, dep_collector.partsect_ftab);
	      }
	    return error_code;
	  }
	m_ftab_set.append_from_collector (&dep_collector);
	if (dep_collector.partsect_ftab != NULL)
	  {
	    db_private_free_and_init (thread_p, dep_collector.partsect_ftab);
	  }
      }

    /* Split sectors among workers */
    m_splited_ftab_set = m_ftab_set.split (parallelism);
    m_splited_ftab_set_idx.store (0);
    m_ftab_set.clear ();

    return NO_ERROR;
  }

  int
  input_handler_list::initialize (THREAD_ENTRY *thread_p, HFID *hfid, SCAN_ID *scan_id)
  {
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

    /* Worker 0 handles membuf pages first */
    m_tl_is_membuf_worker = (idx == 0 && m_has_membuf);
    m_tl_membuf_pageid = 0;

    return NO_ERROR;
  }

  SCAN_CODE
  input_handler_list::get_next_vpid_with_fix (THREAD_ENTRY *thread_p, VPID *vpid)
  {
    /* Phase 1: membuf worker processes membuf pages first */
    if (m_tl_is_membuf_worker && m_tl_membuf_pageid <= m_membuf_last)
      {
	vpid->volid = NULL_VOLID;
	vpid->pageid = m_tl_membuf_pageid++;
	return S_SUCCESS;
      }

    /* Phase 2: sector-based page iteration */
    bool found = false;
    while (!found)
      {
	if (VPID_ISNULL (&m_tl_vpid))
	  {
	    m_tl_ftab = m_tl_ftab_set->get_next ();
	    if (VSID_IS_NULL (&m_tl_ftab.vsid))
	      {
		return S_END;
	      }
	    m_tl_pgoffset = 0;
	    m_tl_vpid.volid = m_tl_ftab.vsid.volid;
	    m_tl_vpid.pageid = SECTOR_FIRST_PAGEID (m_tl_ftab.vsid.sectid);
	  }

	for (; m_tl_pgoffset < DISK_SECTOR_NPAGES; m_tl_pgoffset++, m_tl_vpid.pageid++)
	  {
	    if (bit64_is_set (m_tl_ftab.page_bitmap, (int) m_tl_pgoffset))
	      {
		found = true;
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

  int
  input_handler_list::finalize (THREAD_ENTRY *thread_p)
  {
    m_tl_ftab_set = nullptr;
    VPID_SET_NULL (&m_tl_vpid);
    m_tl_pgoffset = 0;
    m_tl_ftab = FILE_PARTIAL_SECTOR_INITIALIZER;
    m_tl_is_membuf_worker = false;
    m_tl_membuf_pageid = 0;
    return NO_ERROR;
  }
}
