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

/* px_scan_index_key_range_list.cpp — main-thread range conversion + shared BTID_INT. */

#include "px_scan_index_key_range_list.hpp"

#include "btree.h"
#include "btree_load.h"
#include "dbtype.h"
#include "error_code.h"
#include "error_manager.h"
#include "memory_alloc.h"
#include "object_primitive.h"
#include "page_buffer.h"
#include "scan_manager.h"
#include "storage_common.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_index_scan
{
  /* key buffers live on main heap so XASL cleanup's pr_clear_value matches mspace. */
  int
  key_range_list::init_on_main (THREAD_ENTRY *thread_p, INDX_INFO *indx_info, SCAN_ID *scan_id, val_descr *vd)
  {
    assert (indx_info != nullptr);
    BTID_COPY (&m_btid, &indx_info->btid);
    m_indx_info = indx_info;
    m_use_desc_index = (indx_info->use_desc_index != 0);
    m_key_val_ranges.clear ();
    m_part_key_desc = false;

    VPID root_vpid;
    root_vpid.volid = m_btid.vfid.volid;
    root_vpid.pageid = m_btid.root_pageid;
    PAGE_PTR root_page = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
    if (root_page == NULL)
      {
	ASSERT_ERROR ();
	return ER_FAILED;
      }

    (void) pgbuf_check_page_ptype (thread_p, root_page, PAGE_BTREE);

    BTREE_ROOT_HEADER *root_header = btree_get_root_header (thread_p, root_page);
    if (root_header == NULL)
      {
	pgbuf_unfix_and_init (thread_p, root_page);
	return ER_FAILED;
      }

    if (btree_glean_root_header_info (thread_p, root_header, &m_btid_int, true) != NO_ERROR)
      {
	pgbuf_unfix_and_init (thread_p, root_page);
	return ER_FAILED;
      }
    m_btid_int.sys_btid = &m_btid;

    pgbuf_unfix_and_init (thread_p, root_page);

    int conv_err = convert_all_key_ranges (thread_p, scan_id, vd);
    if (conv_err != NO_ERROR)
      {
	return conv_err;
      }

    return NO_ERROR;
  }

  /* idempotent; sort + part_key_desc swap. */
  int
  key_range_list::convert_all_key_ranges (THREAD_ENTRY *thread_p, SCAN_ID *worker_scan_id, val_descr *vd)
  {
    if (!m_key_val_ranges.empty ())
      {
	return NO_ERROR;
      }

    int key_cnt = (m_indx_info != nullptr) ? m_indx_info->key_info.key_cnt : 0;

    if (key_cnt <= 0)
      {
	m_key_val_ranges.resize (1);
	m_key_val_ranges[0].range = INF_INF;
	m_key_val_ranges[0].is_truncated = false;
	m_key_val_ranges[0].num_index_term = 0;
	db_make_null (&m_key_val_ranges[0].key1);
	db_make_null (&m_key_val_ranges[0].key2);
	return NO_ERROR;
      }

    /* scan_id needs coordinator's prebuilt_midxkey_domains; scan_dbvals_to_midxkey NULL-derefs on F_MIDXKEY otherwise. */
    if (worker_scan_id == nullptr)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
	return ER_FAILED;
      }
    INDX_SCAN_ID *isidp = &worker_scan_id->s.isid;
    TP_DOMAIN *btree_domainp = m_btid_int.key_type;

    /* lazy-alloc prebuilt_midxkey_domains (parallel path bypasses scan_open_index_scan); scan_dbvals_to_midxkey would NULL-deref otherwise. */
    if (isidp->prebuilt_midxkey_domains == NULL)
      {
	size_t alloc_size = (size_t) key_cnt * sizeof (TP_DOMAIN *);
	isidp->prebuilt_midxkey_domains = (TP_DOMAIN **) db_private_alloc (thread_p, alloc_size);
	if (isidp->prebuilt_midxkey_domains == NULL)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, alloc_size);
	    return ER_FAILED;
	  }
	for (int j = 0; j < key_cnt; j++)
	  {
	    isidp->prebuilt_midxkey_domains[j] = NULL;
	  }
      }

    m_part_key_desc = false;
    m_key_val_ranges.resize (key_cnt);

    for (int i = 0; i < key_cnt; i++)
      {
	KEY_RANGE *kr = &m_indx_info->key_info.key_ranges[i];

	db_make_null (&m_key_val_ranges[i].key1);
	db_make_null (&m_key_val_ranges[i].key2);
	m_key_val_ranges[i].range = kr->range;
	m_key_val_ranges[i].is_truncated = false;
	m_key_val_ranges[i].num_index_term = 0;

	if (kr->range == NA_NA || kr->range == INF_INF)
	  {
	    continue;
	  }

	int ret = scan_regu_key_to_index_key (thread_p, kr, &m_key_val_ranges[i],
					      isidp, btree_domainp, vd, i);
	if (ret != NO_ERROR)
	  {
	    for (int j = 0; j <= i; j++)
	      {
		pr_clear_value (&m_key_val_ranges[j].key1);
		pr_clear_value (&m_key_val_ranges[j].key2);
	      }
	    m_key_val_ranges.clear ();
	    return ret;
	  }

	/* Prefix index: truncated bounds become inclusive (GT->GE, LT->LE). */
	if (m_key_val_ranges[i].is_truncated)
	  {
	    switch (m_key_val_ranges[i].range)
	      {
	      case GT_INF:
		m_key_val_ranges[i].range = GE_INF;
		break;
	      case GT_LE:
	      case GT_LT:
	      case GE_LT:
		m_key_val_ranges[i].range = GE_LE;
		break;
	      case INF_LT:
		m_key_val_ranges[i].range = INF_LE;
		break;
	      default:
		break;
	      }
	  }
      }

    /* part_key_desc detection from first valid range — matches btree_prepare_bts. */
    for (int i = 0; i < static_cast<int> (m_key_val_ranges.size ()); i++)
      {
	if (m_key_val_ranges[i].range != NA_NA && m_key_val_ranges[i].num_index_term > 0)
	  {
	    TP_DOMAIN *dom = btree_domainp;
	    if (dom != nullptr && TP_DOMAIN_TYPE (dom) == DB_TYPE_MIDXKEY)
	      {
		dom = dom->setdomain;
	      }
	    for (int k = 1; k < m_key_val_ranges[i].num_index_term && dom != nullptr; k++, dom = dom->next)
	      ;
	    if (dom != nullptr)
	      {
		m_part_key_desc = (dom->is_desc != 0);
	      }
	    break;
	  }
      }

    /* full XOR mirrors btree_prepare_bts:15970-15977 — swap whenever traversal direction disagrees with partial-key DESC. */
    if ((m_part_key_desc && !m_use_desc_index) || (m_use_desc_index && !m_part_key_desc))
      {
	for (int i = 0; i < static_cast<int> (m_key_val_ranges.size ()); i++)
	  {
	    if (m_key_val_ranges[i].range == NA_NA || m_key_val_ranges[i].range == INF_INF)
	      {
		continue;
	      }
	    range_reverse (m_key_val_ranges[i].range);
	    DB_VALUE tmp_key = m_key_val_ranges[i].key1;
	    m_key_val_ranges[i].key1 = m_key_val_ranges[i].key2;
	    m_key_val_ranges[i].key2 = tmp_key;
	  }
      }

    /* delegate sort + dedup/merge to scan_manager helper (R_KEYLIST: eliminate_duplicated_keys; R_RANGELIST: merge_key_ranges) so serial and parallel share the same overlap/IN-dup handling. */
    if (m_key_val_ranges.size () > 1 && m_indx_info != nullptr)
      {
	int new_cnt = scan_dedup_or_merge_key_ranges (m_indx_info->range_type, m_key_val_ranges.data (),
		      static_cast<int> (m_key_val_ranges.size ()));
	if (new_cnt < 0)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
	    m_key_val_ranges.clear ();
	    return ER_FAILED;
	  }
	m_key_val_ranges.resize (new_cnt);
      }

    return NO_ERROR;
  }

  void
  key_range_list::cleanup_keys (THREAD_ENTRY *thread_p)
  {
    /* main thread post worker-release: pr_clear matches db_private_alloc mspace from convert_all_key_ranges. */
    for (auto &kvr : m_key_val_ranges)
      {
	pr_clear_value (&kvr.key1);
	pr_clear_value (&kvr.key2);
      }
    m_key_val_ranges.clear ();
  }
}
