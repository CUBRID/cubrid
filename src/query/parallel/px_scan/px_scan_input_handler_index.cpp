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
#include "dbtype.h"
#include "error_code.h"
#include "object_primitive.h"
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
  input_handler_index::init_on_main (THREAD_ENTRY *thread_p, INDX_INFO *indx_info, SCAN_ID *scan_id, val_descr *vd,
				     int parallelism)
  {
    assert (indx_info != nullptr);
    BTID_COPY (&m_btid, &indx_info->btid);
    m_indx_info = indx_info;
    m_use_desc_index = (indx_info->use_desc_index != 0);
    m_scan_id = scan_id;
    m_vd = vd;

    VPID_SET_NULL (&m_current_leaf_vpid);
    m_leaf_ended = false;
    m_descent_done = false;
    m_descent_key_initialized = false;
    m_descent_key_valid = false;
    db_make_null (&m_descent_key_range.key1);
    db_make_null (&m_descent_key_range.key2);
    m_descent_key_range.range = NA_NA;
    m_descent_key_range.is_truncated = false;
    m_descent_key_range.num_index_term = 0;
    return NO_ERROR;
  }

  /* Evaluate the single-range descent bound (asc → key1, desc → key2) once
   * m_btid_int.key_type is known. Multi-range / unbounded scans skip this and
   * fall back to endmost-slot descent. Caller holds m_leaf_mutex. */
  void
  input_handler_index::try_prepare_descent_key (THREAD_ENTRY *thread_p)
  {
    if (m_descent_key_initialized)
      {
	return;
      }
    m_descent_key_initialized = true;

    if (m_indx_info == nullptr || m_scan_id == nullptr || m_vd == nullptr)
      {
	return;
      }
    if (m_indx_info->key_info.key_cnt != 1)
      {
	/* would need MIN(key1) for asc / MAX(key2) for desc across all ranges */
	return;
      }

    KEY_RANGE *kr = &m_indx_info->key_info.key_ranges[0];
    if (kr->range == NA_NA || kr->range == INF_INF)
      {
	return;
      }
    /* scan_regu_key_to_index_key asserts at least one of key1/key2 is non-NULL */
    if (kr->key1 == nullptr && kr->key2 == nullptr)
      {
	return;
      }
    /* DESC index stores keys with reversed compare order: leftmost storage = largest semantic.
     * Effective scan direction = storage_desc XOR use_desc_index. */
    TP_DOMAIN *first_col_dom = m_btid_int.key_type;
    if (first_col_dom != nullptr && TP_DOMAIN_TYPE (first_col_dom) == DB_TYPE_MIDXKEY)
      {
	first_col_dom = first_col_dom->setdomain;
      }
    bool storage_desc = (first_col_dom != nullptr && first_col_dom->is_desc);
    bool use_upper_bound = storage_desc ^ m_use_desc_index;
    if (use_upper_bound ? (kr->key2 == nullptr) : (kr->key1 == nullptr))
      {
	return;
      }

    m_descent_key_range.range = kr->range;
    m_descent_key_range.is_truncated = false;
    m_descent_key_range.num_index_term = 0;
    db_make_null (&m_descent_key_range.key1);
    db_make_null (&m_descent_key_range.key2);

    INDX_SCAN_ID *isidp = &m_scan_id->s.isid;
    int ret = scan_regu_key_to_index_key (thread_p, kr, &m_descent_key_range, isidp,
					  m_btid_int.key_type, m_vd, 0);
    if (ret != NO_ERROR)
      {
	pr_clear_value (&m_descent_key_range.key1);
	pr_clear_value (&m_descent_key_range.key2);
	db_make_null (&m_descent_key_range.key1);
	db_make_null (&m_descent_key_range.key2);
	m_descent_key_range.range = NA_NA;
	er_clear ();
	return;
      }

    m_descent_key_valid = true;
  }

  /* latch-coupled root→leaf descent; out_leaf hands back the leaf latch on S_SUCCESS (cf. btree_find_AR_sampling_leaf).
   * Uses btree_search_nonleaf_page when a single-range descent key is available; otherwise falls back to
   * endmost-slot descent (asc→leftmost, desc→rightmost). */
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

      try_prepare_descent_key (thread_p);

      DB_VALUE *descent_key = nullptr;
      if (m_descent_key_valid)
	{
	  TP_DOMAIN *first_col_dom = m_btid_int.key_type;
	  if (first_col_dom != nullptr && TP_DOMAIN_TYPE (first_col_dom) == DB_TYPE_MIDXKEY)
	    {
	      first_col_dom = first_col_dom->setdomain;
	    }
	  bool storage_desc = (first_col_dom != nullptr && first_col_dom->is_desc);
	  bool use_upper_bound = storage_desc ^ m_use_desc_index;
	  descent_key = use_upper_bound ? &m_descent_key_range.key2 : &m_descent_key_range.key1;
	  if (DB_IS_NULL (descent_key))
	    {
	      descent_key = nullptr;
	    }
	}

      short node_level = root_header->node.node_level;

      /* invariant: P_page latched throughout descent */
      while (node_level > 1)
	{
	  VPID child_vpid;
	  VPID_SET_NULL (&child_vpid);

	  if (descent_key != nullptr)
	    {
	      INT16 slot_id = NULL_SLOTID;
	      int err = btree_search_nonleaf_page (thread_p, &m_btid_int, P_page, descent_key,
						   &slot_id, &child_vpid, NULL);
	      if (err != NO_ERROR || VPID_ISNULL (&child_vpid))
		{
		  /* abort rather than mid-descent slot fallback (would land in wrong subtree) */
		  goto fallback;
		}
	    }
	  else
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
	      child_vpid.pageid = OR_GET_INT (rec.data);
	      child_vpid.volid = OR_GET_SHORT (rec.data + OR_INT_SIZE);

	      if (VPID_ISNULL (&child_vpid))
		{
		  goto fallback;
		}
	    }

	  C_vpid = child_vpid;

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
    if (m_descent_key_valid)
      {
	pr_clear_value (&m_descent_key_range.key1);
	pr_clear_value (&m_descent_key_range.key2);
	db_make_null (&m_descent_key_range.key1);
	db_make_null (&m_descent_key_range.key2);
	m_descent_key_valid = false;
      }
    m_descent_key_initialized = false;
    return NO_ERROR;
  }

  void
  input_handler_index::cleanup_keys (THREAD_ENTRY *thread_p)
  {
    /* No split keys or worker key values to clean up in the leaf-page cursor design. */
  }
}
