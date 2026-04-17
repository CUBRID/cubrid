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
 * px_scan_slot_iterator_index.cpp
 */

#include "px_scan_slot_iterator_index.hpp"
#include "px_scan_input_handler_index.hpp"

#include "btree.h"
#include "btree_load.h"
#include "dbtype.h"
#include "error_code.h"
#include "error_manager.h"
#include "fetch.h"
#include "heap_file.h"
#include "memory_alloc.h"
#include "object_primitive.h"
#include "page_buffer.h"
#include "query_evaluator.h"
#include "scan_manager.h"
#include "slotted_page.h"
#include "storage_common.h"
#include "access_spec.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_scan
{
  slot_iterator_index::slot_iterator_index ()
    : m_scan_id (nullptr),
      m_vd (nullptr),
      m_btid_int (nullptr),
      m_input_handler (nullptr),
      m_page (nullptr),
      m_num_keys (0),
      m_current_slot (1),
      m_data_filter (),
      m_key_range_converted (false),
      m_is_covering (false),
      m_use_desc_index (false),
      m_part_key_desc (false),
      m_key_val_ranges (nullptr),
      m_num_key_ranges (0),
      m_current_range_idx (0),
      m_slot_oid_idx (0),
      m_slot_key_valid (false),
      m_slot_clear_key (false)
  {
    memset (&m_data_filter, 0, sizeof (m_data_filter));
    db_make_null (&m_slot_key);
  }

  slot_iterator_index::~slot_iterator_index ()
  {
  }

  int
  slot_iterator_index::initialize (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, val_descr *vd)
  {
    m_scan_id = scan_id;
    m_vd = vd;
    m_key_range_converted = false;

    INDX_INFO *indx_info = scan_id->s.isid.indx_info;
    m_is_covering = (indx_info != nullptr && indx_info->coverage != 0);
    m_use_desc_index = (indx_info != nullptr && indx_info->use_desc_index != 0);

    return NO_ERROR;
  }

  int
  slot_iterator_index::finalize (THREAD_ENTRY *thread_p)
  {
    if (m_page != nullptr)
      {
	pgbuf_unfix (thread_p, m_page);
	m_page = nullptr;
      }

    if (m_key_val_ranges != nullptr)
      {
	for (int i = 0; i < m_num_key_ranges; i++)
	  {
	    pr_clear_value (&m_key_val_ranges[i].key1);
	    pr_clear_value (&m_key_val_ranges[i].key2);
	  }
	db_private_free_and_init (thread_p, m_key_val_ranges);
	m_num_key_ranges = 0;
      }
    m_key_range_converted = false;

    if (m_slot_key_valid && m_slot_clear_key)
      {
	pr_clear_value (&m_slot_key);
      }
    m_slot_key_valid = false;
    m_slot_clear_key = false;
    m_slot_oids.clear ();
    m_slot_oid_idx = 0;

    m_scan_id = nullptr;
    m_vd = nullptr;
    m_btid_int = nullptr;
    m_input_handler = nullptr;
    return NO_ERROR;
  }

  /*
   * convert_key_range - Convert KEY_RANGE regu variables to DB_VALUE bounds.
   *
   * Converts ALL key ranges from the INDX_INFO into an array of key_val_range
   * structures. Each key range's regu variables (key1, key2) are evaluated
   * into DB_VALUE using fetch_copy_dbval.
   *
   * When part_key_desc is set (last partial-key domain is DESC), the key
   * bounds and range type are swapped to match B-tree storage order, exactly
   * as btree_prepare_bts does for the non-parallel scan path.
   */
  int
  slot_iterator_index::convert_key_range (THREAD_ENTRY *thread_p)
  {
    INDX_INFO *indx_info = m_scan_id->s.isid.indx_info;
    int key_cnt = (indx_info != nullptr) ? indx_info->key_info.key_cnt : 0;

    if (key_cnt <= 0)
      {
	m_num_key_ranges = 1;
	m_key_val_ranges = (key_val_range *) db_private_alloc (thread_p, sizeof (key_val_range));
	if (m_key_val_ranges == nullptr)
	  {
	    return ER_FAILED;
	  }
	m_key_val_ranges[0].range = INF_INF;
	m_key_val_ranges[0].is_truncated = false;
	m_key_val_ranges[0].num_index_term = 0;
	db_make_null (&m_key_val_ranges[0].key1);
	db_make_null (&m_key_val_ranges[0].key2);
	m_key_range_converted = true;
	return NO_ERROR;
      }

    INDX_SCAN_ID *isidp = &m_scan_id->s.isid;
    TP_DOMAIN *btree_domainp = m_btid_int->key_type;

    /* Compute part_key_desc the same way btree_prepare_bts does:
     * find the domain of the last partial-key element. */
    m_part_key_desc = false;

    m_num_key_ranges = key_cnt;
    m_key_val_ranges = (key_val_range *) db_private_alloc (thread_p, sizeof (key_val_range) * key_cnt);
    if (m_key_val_ranges == nullptr)
      {
	return ER_FAILED;
      }

    for (int i = 0; i < key_cnt; i++)
      {
	KEY_RANGE *kr = &indx_info->key_info.key_ranges[i];

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
					      isidp, btree_domainp, m_vd, i);
	if (ret != NO_ERROR)
	  {
	    er_clear ();
	    continue;
	  }

	/* Prefix index: when key bounds were truncated, make existing bounds
	 * inclusive (GT->GE, LT->LE) without closing open (INF) ends.
	 * The truncated key is shorter than the search key, so strict
	 * comparisons must become inclusive to avoid missing matches.
	 * The data filter on the heap record handles exact re-checking. */
	if (m_key_val_ranges[i].is_truncated)
	  {
	    switch (m_key_val_ranges[i].range)
	      {
	      case GT_INF:
		m_key_val_ranges[i].range = GE_INF;
		break;
	      case GT_LE:
		m_key_val_ranges[i].range = GE_LE;
		break;
	      case GT_LT:
		m_key_val_ranges[i].range = GE_LE;
		break;
	      case GE_LT:
		m_key_val_ranges[i].range = GE_LE;
		break;
	      case INF_LT:
		m_key_val_ranges[i].range = INF_LE;
		break;
	      case GE_INF: /* already inclusive lower */
		break;
	      case INF_LE: /* already inclusive upper */
		break;
	      case GE_LE:  /* already inclusive both */
		break;
	      default:
		break;
	      }
	  }
      }

    /* Compute part_key_desc from the first valid range's num_index_term,
     * matching btree_prepare_bts logic. */
    for (int i = 0; i < m_num_key_ranges; i++)
      {
	if (m_key_val_ranges[i].range != NA_NA && m_key_val_ranges[i].num_index_term > 0)
	  {
	    TP_DOMAIN *dom = btree_domainp;
	    if (TP_DOMAIN_TYPE (dom) == DB_TYPE_MIDXKEY)
	      {
		dom = dom->setdomain;
	      }
	    for (int k = 1; k < m_key_val_ranges[i].num_index_term && dom != nullptr; k++, dom = dom->next)
	      {
		;
	      }
	    if (dom != nullptr)
	      {
		m_part_key_desc = (dom->is_desc != 0);
	      }
	    break;
	  }
      }

    /* When part_key_desc is set and use_desc_index is false, the B-tree
     * stores keys in reverse order for the partial-key domain. Swap key
     * bounds and reverse range type to match B-tree order, exactly as
     * btree_prepare_bts does. use_desc_index is always false for parallel
     * scan (blocked by the checker). */
    if (m_part_key_desc && !m_use_desc_index)
      {
	for (int i = 0; i < m_num_key_ranges; i++)
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

    /* Sort ranges by lower bound (key1) in ascending B-tree order so that
     * the cursor optimization in check_key_in_range works correctly.
     * INDX_INFO may provide ranges in arbitrary order. */
    if (m_num_key_ranges > 1)
      {
	TP_DOMAIN *key_domain = m_btid_int->key_type;
	for (int i = 0; i < m_num_key_ranges - 1; i++)
	  {
	    for (int j = i + 1; j < m_num_key_ranges; j++)
	      {
		key_val_range *a = &m_key_val_ranges[i];
		key_val_range *b = &m_key_val_ranges[j];

		if (a->range == NA_NA && b->range != NA_NA)
		  {
		    key_val_range tmp = *a;
		    *a = *b;
		    *b = tmp;
		    continue;
		  }
		if (b->range == NA_NA)
		  {
		    continue;
		  }

		DB_VALUE *ak = DB_IS_NULL (&a->key1) ? nullptr : &a->key1;
		DB_VALUE *bk = DB_IS_NULL (&b->key1) ? nullptr : &b->key1;

		if (ak == nullptr && bk != nullptr)
		  {
		    continue;
		  }
		if (ak != nullptr && bk == nullptr)
		  {
		    key_val_range tmp = *a;
		    *a = *b;
		    *b = tmp;
		    continue;
		  }
		if (ak != nullptr && bk != nullptr)
		  {
		    int start_col = 0;
		    DB_VALUE_COMPARE_RESULT cmp = btree_compare_key (ak, bk, key_domain, 1, 1, &start_col);
		    if (cmp == DB_GT)
		      {
			key_val_range tmp = *a;
			*a = *b;
			*b = tmp;
		      }
		  }
	      }
	  }
      }

    m_key_range_converted = true;
    return NO_ERROR;
  }

  /*
   * check_key_in_range - Check if a key falls within any of the query's key ranges.
   *
   * All comparisons use btree_compare_key in B-tree storage order (which
   * already accounts for DESC domains per-column). Key bounds have been
   * swapped for part_key_desc in convert_key_range, matching the non-parallel
   * btree_prepare_bts + btree_apply_key_range_and_filter logic.
   *
   * Keys always arrive in ascending B-tree order (left-to-right leaf traversal,
   * use_desc_index is blocked by the checker).
   */
  int
  slot_iterator_index::check_key_in_range (DB_VALUE *key, bool *in_range, bool *past_upper)
  {
    *in_range = false;
    *past_upper = false;

    TP_DOMAIN *key_domain = m_btid_int->key_type;

    /* Keys arrive in ascending B-tree order; iterate ranges forward */
    for (int i = m_current_range_idx; i < m_num_key_ranges; i++)
      {
	key_val_range *kvr = &m_key_val_ranges[i];

	if (kvr->range == NA_NA)
	  {
	    continue;
	  }

	if (kvr->range == INF_INF)
	  {
	    *in_range = true;
	    return NO_ERROR;
	  }

	DB_VALUE_COMPARE_RESULT c;
	int start_col = 0;

	/* Lower bound check: key1 is the lower bound in B-tree order.
	 * btree_compare_key(cur_key, lower_key): GT or EQ means cur >= lower */
	bool lower_ok = true;
	switch (kvr->range)
	  {
	  case GE_LE:
	  case GE_LT:
	  case GE_INF:
	    if (!DB_IS_NULL (&kvr->key1))
	      {
		start_col = 0;
		c = btree_compare_key (key, &kvr->key1, key_domain, 1, 1, &start_col);
		if (c == DB_LT)
		  {
		    lower_ok = false;
		  }
	      }
	    break;
	  case GT_LE:
	  case GT_LT:
	  case GT_INF:
	    if (!DB_IS_NULL (&kvr->key1))
	      {
		start_col = 0;
		c = btree_compare_key (key, &kvr->key1, key_domain, 1, 1, &start_col);
		if (c == DB_LT || c == DB_EQ)
		  {
		    lower_ok = false;
		  }
	      }
	    break;
	  case INF_LE:
	  case INF_LT:
	  case INF_INF:
	  case EQ_NA:
	    break;
	  default:
	    break;
	  }

	if (!lower_ok)
	  {
	    /* cur_key has not reached this range's lower bound yet.
	     * Since ranges are sorted ascending and keys arrive ascending,
	     * the key might reach this range later. Don't advance cursor. */
	    return NO_ERROR;
	  }

	/* Upper bound check: key2 is the upper bound in B-tree order.
	 * btree_compare_key(upper_key, cur_key): GT means upper > cur (in range).
	 * This matches btree_apply_key_range_and_filter logic exactly. */
	bool upper_ok = true;
	switch (kvr->range)
	  {
	  case GE_LE:
	  case GT_LE:
	  case INF_LE:
	    if (!DB_IS_NULL (&kvr->key2))
	      {
		start_col = 0;
		c = btree_compare_key (&kvr->key2, key, key_domain, 1, 1, &start_col);
		if (c == DB_LT)
		  {
		    upper_ok = false;
		    m_current_range_idx = i + 1;
		  }
	      }
	    break;
	  case GE_LT:
	  case GT_LT:
	  case INF_LT:
	    if (!DB_IS_NULL (&kvr->key2))
	      {
		start_col = 0;
		c = btree_compare_key (&kvr->key2, key, key_domain, 1, 1, &start_col);
		if (c == DB_LT || c == DB_EQ)
		  {
		    upper_ok = false;
		    m_current_range_idx = i + 1;
		  }
	      }
	    break;
	  case GE_INF:
	  case GT_INF:
	  case INF_INF:
	    break;
	  case EQ_NA:
	    if (!DB_IS_NULL (&kvr->key1))
	      {
		start_col = 0;
		c = btree_compare_key (key, &kvr->key1, key_domain, 1, 1, &start_col);
		if (c != DB_EQ)
		  {
		    upper_ok = false;
		    if (c == DB_GT)
		      {
			m_current_range_idx = i + 1;
		      }
		  }
	      }
	    break;
	  default:
	    break;
	  }

	if (upper_ok)
	  {
	    *in_range = true;
	    return NO_ERROR;
	  }
      }

    *past_upper = true;
    return NO_ERROR;
  }

  /*
   * set_page - Fix the leaf page for iteration.
   */
  int
  slot_iterator_index::set_page (THREAD_ENTRY *thread_p, VPID *vpid)
  {
    if (m_input_handler != nullptr && m_btid_int == nullptr)
      {
	m_btid_int = m_input_handler->get_btid_int ();
      }

    if (!m_key_range_converted)
      {
	int err = convert_key_range (thread_p);
	if (err != NO_ERROR)
	  {
	    return err;
	  }
      }

    if (m_page != nullptr)
      {
	pgbuf_unfix (thread_p, m_page);
	m_page = nullptr;
      }

    /* Clear any pending slot OID state */
    if (m_slot_key_valid && m_slot_clear_key)
      {
	pr_clear_value (&m_slot_key);
      }
    m_slot_key_valid = false;
    m_slot_clear_key = false;
    m_slot_oids.clear ();
    m_slot_oid_idx = 0;

    m_page = pgbuf_fix (thread_p, vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
    if (m_page == nullptr)
      {
	m_num_keys = 0;
	m_current_slot = m_use_desc_index ? 0 : 1;
	return NO_ERROR;
      }

    m_num_keys = btree_node_number_of_keys (thread_p, m_page);
    m_current_slot = m_use_desc_index ? m_num_keys : 1;

    m_current_range_idx = 0;

    return NO_ERROR;
  }

  /*
   * collect_oid_helper - helper struct for collect_oid_callback.
   * Carries both the output OID vector and the MVCC snapshot used
   * to filter out B-tree entries whose delete is already visible.
   */
  struct collect_oid_helper
  {
    std::vector<OID> *oid_vec;
    MVCC_SNAPSHOT *snapshot;
  };

  /*
   * collect_oid_callback - btree_key_process_objects callback.
   *
   * Checks MVCC visibility of each OID using the snapshot and
   * collects only visible OIDs into the vector.  Without this
   * check, deleted B-tree entries (e.g. from MVCC-updated rows
   * in a filtered index) would be collected, and
   * heap_get_visible_version would return the updated version
   * that may no longer satisfy the filter condition.
   */
  int
  slot_iterator_index::collect_oid_callback (THREAD_ENTRY *thread_p, BTID_INT *btid_int, RECDES *record,
      char *object_ptr, OID *oid, OID *class_oid,
      BTREE_MVCC_INFO *mvcc_info, bool *stop, void *args)
  {
    auto *helper = static_cast<collect_oid_helper *> (args);

    if (helper->snapshot != nullptr)
      {
	MVCC_REC_HEADER mvcc_header;
	btree_mvcc_info_to_heap_mvcc_header (mvcc_info, &mvcc_header);
	if (helper->snapshot->snapshot_fnc (thread_p, &mvcc_header, helper->snapshot) != SNAPSHOT_SATISFIED)
	  {
	    return NO_ERROR;
	  }
      }

    helper->oid_vec->push_back (*oid);
    return NO_ERROR;
  }

  /*
   * process_oid - Process a single OID: heap fetch, predicate evaluation,
   * and val_list fill.
   *
   * Returns S_SUCCESS if the OID qualifies, S_END if it should be skipped,
   * S_ERROR on error.
   */
  SCAN_CODE
  slot_iterator_index::process_oid (THREAD_ENTRY *thread_p, OID *oid)
  {
    INDX_SCAN_ID *isidp = &m_scan_id->s.isid;

    if (m_is_covering)
      {
	/* Covering index path: read output values from the B-tree key without
	 * a heap lookup. MVCC visibility has already been enforced by the
	 * snapshot check inside collect_oid_callback. */
	HEAP_CACHE_ATTRINFO *attr_info = nullptr;
	REGU_VARIABLE_LIST regu_list = nullptr;

	if (isidp->rest_attrs.num_attrs > 0)
	  {
	    attr_info = isidp->rest_attrs.attr_cache;
	    regu_list = isidp->rest_regu_list;
	  }
	else if (isidp->pred_attrs.num_attrs > 0)
	  {
	    attr_info = isidp->pred_attrs.attr_cache;
	    regu_list = isidp->scan_pred.regu_list;
	  }

	if (attr_info != nullptr)
	  {
	    int read_err = btree_attrinfo_read_dbvalues (thread_p, &m_slot_key, nullptr,
			   isidp->bt_attr_ids, isidp->bt_num_attrs, attr_info,
			   isidp->indx_cov.func_index_col_id, nullptr);
	    if (read_err != NO_ERROR)
	      {
		return S_ERROR;
	      }
	  }

	if (isidp->scan_pred.pr_eval_fnc != nullptr && isidp->scan_pred.pred_expr != nullptr)
	  {
	    DB_LOGICAL ev_res = (*isidp->scan_pred.pr_eval_fnc) (thread_p, isidp->scan_pred.pred_expr,
				m_vd, oid);
	    ev_res = update_logical_result (thread_p, ev_res, (int *) &m_scan_id->qualification);
	    if (ev_res != V_TRUE)
	      {
		if (ev_res == V_ERROR)
		  {
		    return S_ERROR;
		  }
		return S_END;  /* skip */
	      }
	  }

	m_scan_id->scan_stats.data_qualified_rows++;
	m_scan_id->scan_stats.qualified_rows++;

	if (regu_list != nullptr && m_scan_id->val_list != nullptr)
	  {
	    if (fetch_val_list (thread_p, regu_list, m_vd, &isidp->cls_oid, oid, nullptr, PEEK) != NO_ERROR)
	      {
		return S_ERROR;
	      }
	  }

	return S_SUCCESS;
      }

    /* Non-covering index path: read values from heap record */
    RECDES heap_recdes = RECDES_INITIALIZER;
    if (m_scan_id->fixed == false)
      {
	heap_recdes.data = nullptr;
      }

    SCAN_CODE sp_scan = heap_get_visible_version (thread_p, oid, nullptr, &heap_recdes,
			&isidp->scan_cache, m_scan_id->fixed, NULL_CHN);
    if (sp_scan == S_SNAPSHOT_NOT_SATISFIED || sp_scan == S_DOESNT_EXIST)
      {
	return S_END;  /* skip this OID */
      }
    if (sp_scan == S_ERROR)
      {
	if (er_errid () == ER_HEAP_UNKNOWN_OBJECT)
	  {
	    er_clear ();
	    return S_END;  /* skip */
	  }
	return S_ERROR;
      }

    if (isidp->scan_pred.pr_eval_fnc != nullptr && isidp->scan_pred.pred_expr != nullptr)
      {
	FILTER_INFO data_filter;
	memset (&data_filter, 0, sizeof (data_filter));
	data_filter.scan_pred = &isidp->scan_pred;
	data_filter.scan_attrs = &isidp->pred_attrs;
	data_filter.val_list = m_scan_id->val_list;
	data_filter.val_descr = m_vd;
	data_filter.class_oid = &isidp->cls_oid;

	DB_LOGICAL ev_res = eval_data_filter (thread_p, oid, &heap_recdes, &isidp->scan_cache, &data_filter);
	ev_res = update_logical_result (thread_p, ev_res, (int *) &m_scan_id->qualification);
	if (ev_res != V_TRUE)
	  {
	    if (ev_res == V_ERROR)
	      {
		return S_ERROR;
	      }
	    return S_END;  /* skip */
	  }
      }

    m_scan_id->scan_stats.data_qualified_rows++;
    m_scan_id->scan_stats.qualified_rows++;

    if (isidp->rest_regu_list != nullptr)
      {
	if (heap_attrinfo_read_dbvalues (thread_p, oid, &heap_recdes, isidp->rest_attrs.attr_cache) != NO_ERROR)
	  {
	    return S_ERROR;
	  }

	if (m_scan_id->val_list != nullptr)
	  {
	    if (fetch_val_list (thread_p, isidp->rest_regu_list, m_vd, &isidp->cls_oid, oid,
				nullptr, PEEK) != NO_ERROR)
	      {
		return S_ERROR;
	      }
	  }
      }

    return S_SUCCESS;
  }

  /*
   * next_qualified_slot_with_peek - Core algorithm: iterate through leaf page
   * slots, read each key, check key range, collect ALL OIDs from qualifying
   * slots, then process OIDs one at a time (heap fetch, predicate eval, fill
   * val_list).
   *
   * Returns S_SUCCESS when a qualified row is available,
   *         S_END when the current page is exhausted,
   *         S_ERROR on error.
   */
  SCAN_CODE
  slot_iterator_index::next_qualified_slot_with_peek (THREAD_ENTRY *thread_p)
  {
    INDX_SCAN_ID *isidp = &m_scan_id->s.isid;

    /* First, drain any pending OIDs from the current slot */
    while (m_slot_oid_idx < m_slot_oids.size ())
      {
	OID oid = m_slot_oids[m_slot_oid_idx++];
	SCAN_CODE sc = process_oid (thread_p, &oid);
	if (sc == S_SUCCESS)
	  {
	    return S_SUCCESS;
	  }
	if (sc == S_ERROR)
	  {
	    if (m_page != nullptr)
	      {
		pgbuf_unfix (thread_p, m_page);
		m_page = nullptr;
	      }
	    return S_ERROR;
	  }
	/* S_END means skip this OID, continue to next */
      }

    /* Clear previous slot's key if any */
    if (m_slot_key_valid && m_slot_clear_key)
      {
	pr_clear_value (&m_slot_key);
      }
    m_slot_key_valid = false;
    m_slot_clear_key = false;

    /* Iterate through remaining slots on this page */
    while (m_use_desc_index ? (m_current_slot >= 1) : (m_current_slot <= m_num_keys))
      {
	/* 1. Read B-tree record at current slot */
	RECDES rec;
	rec.data = nullptr;
	rec.area_size = -1;

	if (spage_get_record (thread_p, m_page, m_current_slot, &rec, PEEK) != S_SUCCESS)
	  {
	    m_use_desc_index ? m_current_slot-- : m_current_slot++;
	    continue;
	  }

	DB_VALUE key;
	db_make_null (&key);
	LEAF_REC leaf_rec_info;
	bool clear_key = false;
	int offset = 0;

	int rerr = btree_read_record (thread_p, m_btid_int, m_page, &rec, &key,
				      &leaf_rec_info, BTREE_LEAF_NODE,
				      &clear_key, &offset, COPY, nullptr);
	m_use_desc_index ? m_current_slot-- : m_current_slot++;

	if (rerr != NO_ERROR)
	  {
	    continue;
	  }

	m_scan_id->scan_stats.read_keys++;

	/* 2. Key range check */
	bool in_range = false;
	bool past_upper = false;
	check_key_in_range (&key, &in_range, &past_upper);

	if (!in_range)
	  {
	    if (clear_key)
	      {
		pr_clear_value (&key);
	      }
	    if (past_upper)
	      {
		if (m_page != nullptr)
		  {
		    pgbuf_unfix (thread_p, m_page);
		    m_page = nullptr;
		  }
		return S_END;
	      }
	    continue;
	  }

	m_scan_id->scan_stats.qualified_keys++;

	/* 3. Key filter predicate (if exists) */
	if (isidp->key_pred.pr_eval_fnc != nullptr && isidp->key_pred.pred_expr != nullptr)
	  {
	    FILTER_INFO key_filter;
	    memset (&key_filter, 0, sizeof (key_filter));
	    key_filter.scan_pred = &isidp->key_pred;
	    key_filter.scan_attrs = &isidp->key_attrs;
	    key_filter.val_list = m_scan_id->val_list;
	    key_filter.val_descr = m_vd;
	    key_filter.class_oid = &isidp->cls_oid;
	    key_filter.btree_attr_ids = isidp->bt_attr_ids;
	    key_filter.num_vstr_ptr = &isidp->num_vstr;
	    key_filter.vstr_ids = isidp->vstr_ids;
	    key_filter.btree_num_attrs = isidp->bt_num_attrs;
	    key_filter.func_idx_col_id = isidp->indx_info->func_idx_col_id;

	    DB_LOGICAL ev_res = eval_key_filter (thread_p, &key, 0, nullptr, &key_filter);
	    if (ev_res != V_TRUE)
	      {
		if (clear_key)
		  {
		    pr_clear_value (&key);
		  }
		if (ev_res == V_ERROR)
		  {
		    if (m_page != nullptr)
		      {
			pgbuf_unfix (thread_p, m_page);
			m_page = nullptr;
		      }
		    return S_ERROR;
		  }
		continue;
	      }

	    m_scan_id->scan_stats.key_qualified_rows++;
	    m_scan_id->scan_stats.read_rows++;
	  }
	else
	  {
	    m_scan_id->scan_stats.key_qualified_rows++;
	    m_scan_id->scan_stats.read_rows++;
	  }

	/* 4. Collect visible OIDs from this leaf record (including overflow pages).
	 * The MVCC snapshot filters out B-tree entries whose delete is already
	 * visible, preventing stale OIDs from being processed (important for
	 * filtered indexes where the updated version may not satisfy the filter). */
	m_slot_oids.clear ();
	m_slot_oid_idx = 0;

	collect_oid_helper oid_helper;
	oid_helper.oid_vec = &m_slot_oids;
	oid_helper.snapshot = isidp->scan_cache.mvcc_snapshot;

	int proc_err = btree_key_process_objects (thread_p, m_btid_int, &rec, offset,
		       &leaf_rec_info, collect_oid_callback, &oid_helper);
	if (proc_err != NO_ERROR)
	  {
	    if (clear_key)
	      {
		pr_clear_value (&key);
	      }
	    continue;
	  }

	if (m_slot_oids.empty ())
	  {
	    if (clear_key)
	      {
		pr_clear_value (&key);
	      }
	    continue;
	  }

	/* Save the key for use in process_oid (covering index needs it) */
	m_slot_key = key;
	m_slot_key_valid = true;
	m_slot_clear_key = clear_key;

	/* 5. Process OIDs one at a time */
	while (m_slot_oid_idx < m_slot_oids.size ())
	  {
	    OID oid = m_slot_oids[m_slot_oid_idx++];
	    SCAN_CODE sc = process_oid (thread_p, &oid);
	    if (sc == S_SUCCESS)
	      {
		return S_SUCCESS;
	      }
	    if (sc == S_ERROR)
	      {
		if (m_slot_key_valid && m_slot_clear_key)
		  {
		    pr_clear_value (&m_slot_key);
		  }
		m_slot_key_valid = false;
		m_slot_clear_key = false;
		if (m_page != nullptr)
		  {
		    pgbuf_unfix (thread_p, m_page);
		    m_page = nullptr;
		  }
		return S_ERROR;
	      }
	    /* S_END means skip, continue to next OID */
	  }

	/* All OIDs in this slot were skipped; clean up key and continue to next slot */
	if (m_slot_key_valid && m_slot_clear_key)
	  {
	    pr_clear_value (&m_slot_key);
	  }
	m_slot_key_valid = false;
	m_slot_clear_key = false;
      }

    /* Page exhausted */
    if (m_page != nullptr)
      {
	pgbuf_unfix (thread_p, m_page);
	m_page = nullptr;
      }
    return S_END;
  }
}
