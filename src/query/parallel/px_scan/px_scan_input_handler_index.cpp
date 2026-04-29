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
 * px_scan_input_handler_index.cpp
 */

#include "px_scan_input_handler_index.hpp"

#include "btree.h"
#include "btree_load.h"
#include "error_code.h"
#include "object_representation.h"
#include "error_manager.h"
#include "page_buffer.h"
#include "scan_manager.h"
#include "slotted_page.h"
#include "storage_common.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_scan
{
  /* defer descent until first worker — VPID-only republish would race a concurrent split */
  int
  input_handler_index::init_on_main (THREAD_ENTRY *thread_p, INDX_INFO *indx_info, int parallelism)
  {
    assert (indx_info != nullptr);
    BTID_COPY (&m_btid, &indx_info->btid);
    m_indx_info = indx_info;
    m_use_desc_index = (indx_info->use_desc_index != 0);

    VPID_SET_NULL (&m_current_leaf_vpid);
    m_leaf_ended = false;
    m_descent_done = false;
    return NO_ERROR;
  }

  /* latch-coupled root→leaf descent; out_leaf hands back the leaf latch on S_SUCCESS (cf. btree_find_AR_sampling_leaf) */
  SCAN_CODE
  input_handler_index::descend_to_first_leaf (THREAD_ENTRY *thread_p, PAGE_PTR &out_leaf)
  {
    PAGE_PTR P_page = NULL;
    PAGE_PTR C_page = NULL;
    VPID P_vpid;
    VPID C_vpid;

    out_leaf = nullptr;

    P_vpid.volid = m_btid.vfid.volid;
    P_vpid.pageid = m_btid.root_pageid;
    P_page = pgbuf_fix (thread_p, &P_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
    if (P_page == NULL)
      {
	goto fallback;
      }

    (void) pgbuf_check_page_ptype (thread_p, P_page, PAGE_BTREE);

    {
      BTREE_ROOT_HEADER *root_header = btree_get_root_header (thread_p, P_page);
      if (root_header == NULL)
	{
	  goto fallback;
	}

      if (btree_glean_root_header_info (thread_p, root_header, &m_btid_int, true) != NO_ERROR)
	{
	  goto fallback;
	}

      m_btid_int.sys_btid = &m_btid;

      short node_level = root_header->node.node_level;

      /* invariant: P_page latched throughout descent */
      while (node_level > 1)
	{
	  int key_cnt = btree_node_number_of_keys (thread_p, P_page);
	  if (key_cnt <= 0)
	    {
	      goto fallback;
	    }

	  /* asc → slot 1, desc → slot key_cnt */
	  int slot_to_follow = m_use_desc_index ? key_cnt : 1;
	  RECDES rec;
	  rec.data = NULL;
	  rec.area_size = -1;
	  if (spage_get_record (thread_p, P_page, slot_to_follow, &rec, PEEK) != S_SUCCESS)
	    {
	      goto fallback;
	    }

	  /* unpack VPID inline; btree_read_fixed_portion_of_non_leaf_record is btree.c-internal */
	  C_vpid.pageid = OR_GET_INT (rec.data);
	  C_vpid.volid = OR_GET_SHORT (rec.data + OR_INT_SIZE);

	  if (VPID_ISNULL (&C_vpid))
	    {
	      goto fallback;
	    }

	  /* fix child before unfixing parent — blocks concurrent split */
	  C_page = pgbuf_fix (thread_p, &C_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
	  if (C_page == NULL)
	    {
	      goto fallback;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, C_page, PAGE_BTREE);

	  pgbuf_unfix_and_init (thread_p, P_page);

	  BTREE_NODE_HEADER *child_hdr = btree_get_node_header (thread_p, C_page);
	  if (child_hdr == NULL)
	    {
	      P_page = C_page;
	      C_page = NULL;
	      P_vpid = C_vpid;
	      goto fallback;
	    }

	  P_page = C_page;
	  C_page = NULL;
	  P_vpid = C_vpid;
	  node_level = child_hdr->node_level;
	}
    }

    /* hand the leaf latch to the caller */
    out_leaf = P_page;
    return S_SUCCESS;

fallback:
    if (C_page != NULL)
      {
	pgbuf_unfix_and_init (thread_p, C_page);
      }
    if (P_page != NULL)
      {
	pgbuf_unfix_and_init (thread_p, P_page);
      }
    /* generic error so ASSERT_NO_ERROR callers don't trip on the fallback */
    er_clear ();
    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
    return S_ERROR;
  }

  /* mutex-protected leaf cursor; first call descends + hands off, later calls fix m_current_leaf_vpid */
  SCAN_CODE
  input_handler_index::get_next_page_with_fix (THREAD_ENTRY *thread_p, PAGE_PTR &out_page)
  {
    out_page = nullptr;

    std::unique_lock<std::mutex> lock (m_leaf_mutex);

    if (m_leaf_ended)
      {
	return S_END;
      }

    PAGE_PTR page = nullptr;

    if (!m_descent_done)
      {
	SCAN_CODE sc = descend_to_first_leaf (thread_p, page);
	if (sc != S_SUCCESS)
	  {
	    m_leaf_ended = true;
	    return sc;
	  }
	m_descent_done = true;
      }
    else
      {
	VPID ret_vpid = m_current_leaf_vpid;
	page = pgbuf_fix (thread_p, &ret_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
	if (page == nullptr)
	  {
	    m_leaf_ended = true;
	    return S_ERROR;
	  }
      }

    BTREE_NODE_HEADER *hdr = btree_get_node_header (thread_p, page);
    if (hdr == nullptr)
      {
	pgbuf_unfix (thread_p, page);
	m_leaf_ended = true;
	return S_ERROR;
      }

    VPID next = m_use_desc_index ? hdr->prev_vpid : hdr->next_vpid;

    if (VPID_ISNULL (&next))
      {
	m_leaf_ended = true;
      }
    else
      {
	m_current_leaf_vpid = next;
      }

    out_page = page;
    return S_SUCCESS;
  }

  /* No-op: slot_iterator_index drives the leaf-page cursor directly. */
  int
  input_handler_index::initialize (THREAD_ENTRY *thread_p, HFID *hfid, SCAN_ID *scan_id)
  {
    return NO_ERROR;
  }

  int
  input_handler_index::finalize (THREAD_ENTRY *thread_p)
  {
    return NO_ERROR;
  }

  void
  input_handler_index::cleanup_keys (THREAD_ENTRY *thread_p)
  {
    /* No split keys or worker key values to clean up in the leaf-page cursor design. */
  }
}
