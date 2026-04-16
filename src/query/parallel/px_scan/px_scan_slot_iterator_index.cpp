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
      m_keys_descending (false),
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
   * compare_key_natural - Compare keys using natural (ascending) order.
   *
   * btree_compare_key reverses the result for DESC domains.
   * For range checking against query bounds (which are always in
   * natural order), we need to undo that reversal.
   */
  static DB_VALUE_COMPARE_RESULT
  compare_key_natural (DB_VALUE *key1, DB_VALUE *key2, TP_DOMAIN *key_domain,
                       int do_coercion, int total_order, int *start_colp)
  {
    DB_VALUE_COMPARE_RESULT cmp = btree_compare_key (key1, key2, key_domain,
                                  do_coercion, total_order, start_colp);
    if (cmp != DB_UNK && key_domain != nullptr && key_domain->is_desc)
      {
        if (cmp == DB_LT)
          {
            cmp = DB_GT;
          }
        else if (cmp == DB_GT)
          {
            cmp = DB_LT;
          }
      }
    return cmp;
  }

  /*
   * convert_key_range - Convert KEY_RANGE regu variables to DB_VALUE bounds.
   *
   * Converts ALL key ranges from the INDX_INFO into an array of key_val_range
   * structures. Each key range's regu variables (key1, key2) are evaluated
   * into DB_VALUE using fetch_copy_dbval.
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

        if (kr->range == NA_NA)
          {
            continue;
          }

        int ret = scan_regu_key_to_index_key (thread_p, kr, &m_key_val_ranges[i],
                                              isidp, btree_domainp, m_vd, i);
        if (ret != NO_ERROR)
          {
            // scan_regu_key_to_index_key sets range = NA_NA on failure
            er_clear ();
            continue;
          }
      }

    // Sort ranges by lower bound in ascending natural order so that
    // the cursor optimization in check_key_in_range works correctly.
    // INDX_INFO may provide ranges in arbitrary order (e.g. reverse).
    if (m_num_key_ranges > 1)
      {
        TP_DOMAIN *key_domain = m_btid_int->key_type;
        for (int i = 0; i < m_num_key_ranges - 1; i++)
          {
            for (int j = i + 1; j < m_num_key_ranges; j++)
              {
                key_val_range *a = &m_key_val_ranges[i];
                key_val_range *b = &m_key_val_ranges[j];

                // Skip NA_NA ranges (sort them to the end)
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

                // Compare lower bounds (key1). NULL key1 means -INF (goes first).
                DB_VALUE *ak = DB_IS_NULL (&a->key1) ? nullptr : &a->key1;
                DB_VALUE *bk = DB_IS_NULL (&b->key1) ? nullptr : &b->key1;

                if (ak == nullptr && bk != nullptr)
                  {
                    continue;  // a already before b
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
                    DB_VALUE_COMPARE_RESULT cmp = compare_key_natural (ak, bk, key_domain, 1, 1, &start_col);
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
   * Sets *in_range = true if the key satisfies at least one range.
   * Sets *past_upper = true if the key is past all ranges in the scan
   * direction (allows early termination).
   */
  int
  slot_iterator_index::check_key_in_range (DB_VALUE *key, bool *in_range, bool *past_upper)
  {
    *in_range = false;
    *past_upper = false;

    TP_DOMAIN *key_domain = m_btid_int->key_type;

    if (m_keys_descending)
      {
        // Keys arrive high-to-low in natural order.
        // For multi-range, ranges are sorted ascending. We scan ranges from
        // m_current_range_idx downward.
        for (int i = m_current_range_idx; i >= 0; i--)
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

            DB_VALUE_COMPARE_RESULT cmp;
            int start_col = 0;

            // Check upper bound: if key is above range, this key hasn't
            // reached the range yet. Return not-in-range without past_upper.
            bool above_upper = false;
            switch (kvr->range)
              {
              case GE_LE:
              case GT_LE:
              case INF_LE:
                if (!DB_IS_NULL (&kvr->key2))
                  {
                    start_col = 0;
                    cmp = compare_key_natural (key, &kvr->key2, key_domain, 1, 1, &start_col);
                    if (cmp == DB_GT)
                      {
                        above_upper = true;
                      }
                  }
                break;
              case GE_LT:
              case GT_LT:
              case INF_LT:
                if (!DB_IS_NULL (&kvr->key2))
                  {
                    start_col = 0;
                    cmp = compare_key_natural (key, &kvr->key2, key_domain, 1, 1, &start_col);
                    if (cmp == DB_GT || cmp == DB_EQ)
                      {
                        above_upper = true;
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
                    cmp = compare_key_natural (key, &kvr->key1, key_domain, 1, 1, &start_col);
                    if (cmp == DB_GT)
                      {
                        above_upper = true;
                      }
                    else if (cmp == DB_EQ)
                      {
                        *in_range = true;
                        return NO_ERROR;
                      }
                  }
                break;
              default:
                break;
              }

            if (above_upper)
              {
                // Key is above this range. Since ranges are sorted ascending,
                // the key is also above all lower-indexed ranges. But future
                // (smaller) keys may still match this range. Return not-in-range.
                return NO_ERROR;
              }

            // Check lower bound: if key is below range, this range and
            // all higher-indexed ranges are passed. Advance cursor.
            bool below_lower = false;
            switch (kvr->range)
              {
              case GE_LE:
              case GE_LT:
              case GE_INF:
                if (!DB_IS_NULL (&kvr->key1))
                  {
                    start_col = 0;
                    cmp = compare_key_natural (key, &kvr->key1, key_domain, 1, 1, &start_col);
                    if (cmp == DB_LT)
                      {
                        below_lower = true;
                      }
                  }
                break;
              case GT_LE:
              case GT_LT:
              case GT_INF:
                if (!DB_IS_NULL (&kvr->key1))
                  {
                    start_col = 0;
                    cmp = compare_key_natural (key, &kvr->key1, key_domain, 1, 1, &start_col);
                    if (cmp == DB_LT || cmp == DB_EQ)
                      {
                        below_lower = true;
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

            if (below_lower)
              {
                // Key is below this range. Advance cursor past this range
                // since future keys (even smaller) won't match it either.
                m_current_range_idx = i - 1;
                continue;
              }

            // Key is within bounds
            *in_range = true;
            return NO_ERROR;
          }

        // Past all ranges in descending direction
        *past_upper = true;
        return NO_ERROR;
      }

    // Keys arrive low-to-high in natural order; iterate ranges forward
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

        DB_VALUE_COMPARE_RESULT cmp;
        int start_col = 0;

        bool lower_ok = true;
        switch (kvr->range)
          {
          case GE_LE:
          case GE_LT:
          case GE_INF:
            if (!DB_IS_NULL (&kvr->key1))
              {
                cmp = compare_key_natural (key, &kvr->key1, key_domain, 1, 1, &start_col);
                if (cmp == DB_LT)
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
                cmp = compare_key_natural (key, &kvr->key1, key_domain, 1, 1, &start_col);
                if (cmp == DB_LT || cmp == DB_EQ)
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
            return NO_ERROR;
          }

        bool upper_ok = true;
        switch (kvr->range)
          {
          case GE_LE:
          case GT_LE:
          case INF_LE:
            if (!DB_IS_NULL (&kvr->key2))
              {
                start_col = 0;
                cmp = compare_key_natural (key, &kvr->key2, key_domain, 1, 1, &start_col);
                if (cmp == DB_GT)
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
                cmp = compare_key_natural (key, &kvr->key2, key_domain, 1, 1, &start_col);
                if (cmp == DB_GT || cmp == DB_EQ)
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
                cmp = compare_key_natural (key, &kvr->key1, key_domain, 1, 1, &start_col);
                if (cmp != DB_EQ)
                  {
                    upper_ok = false;
                    if (cmp == DB_GT)
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
        // Determine if keys arrive in descending natural order:
        // DESC domain XOR desc_index traversal
        bool domain_desc = (m_btid_int->key_type != nullptr && m_btid_int->key_type->is_desc);
        m_keys_descending = (domain_desc != m_use_desc_index);
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

    // Clear any pending slot OID state
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

    m_current_range_idx = m_keys_descending ? (m_num_key_ranges - 1) : 0;

    return NO_ERROR;
  }

  /*
   * collect_oid_callback - btree_key_process_objects callback.
   *
   * Collects each OID into the vector passed via args.
   * Does NOT clear MVCC flags from the OID — btree_key_process_objects
   * already does that via btree_or_get_object.
   */
  int
  slot_iterator_index::collect_oid_callback (THREAD_ENTRY *thread_p, BTID_INT *btid_int, RECDES *record,
      char *object_ptr, OID *oid, OID *class_oid,
      BTREE_MVCC_INFO *mvcc_info, bool *stop, void *args)
  {
    auto *oid_vec = static_cast<std::vector<OID> *> (args);
    oid_vec->push_back (*oid);
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

    // Heap fetch with MVCC visibility
    RECDES heap_recdes = RECDES_INITIALIZER;
    if (m_scan_id->fixed == false)
      {
        heap_recdes.data = nullptr;
      }

    SCAN_CODE sp_scan = heap_get_visible_version (thread_p, oid, nullptr, &heap_recdes,
                        &isidp->scan_cache, m_scan_id->fixed, NULL_CHN);
    if (sp_scan == S_SNAPSHOT_NOT_SATISFIED || sp_scan == S_DOESNT_EXIST)
      {
        return S_END;  // skip this OID
      }
    if (sp_scan == S_ERROR)
      {
        if (er_errid () == ER_HEAP_UNKNOWN_OBJECT)
          {
            er_clear ();
            return S_END;  // skip
          }
        return S_ERROR;
      }

    if (m_is_covering)
      {
        // Covering index path: read output values from the B-tree key
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
                return S_END;  // skip
              }
          }

        m_scan_id->scan_stats.data_qualified_rows++;

        if (regu_list != nullptr && m_scan_id->val_list != nullptr)
          {
            if (fetch_val_list (thread_p, regu_list, m_vd, &isidp->cls_oid, oid, nullptr, PEEK) != NO_ERROR)
              {
                return S_ERROR;
              }
          }

        return S_SUCCESS;
      }

    // Non-covering index path: read values from heap record

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
            return S_END;  // skip
          }
      }

    m_scan_id->scan_stats.data_qualified_rows++;

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

    // First, drain any pending OIDs from the current slot
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
        // S_END means skip this OID, continue to next
      }

    // Clear previous slot's key if any
    if (m_slot_key_valid && m_slot_clear_key)
      {
        pr_clear_value (&m_slot_key);
      }
    m_slot_key_valid = false;
    m_slot_clear_key = false;

    // Iterate through remaining slots on this page
    while (m_use_desc_index ? (m_current_slot >= 1) : (m_current_slot <= m_num_keys))
      {
        // 1. Read B-tree record at current slot
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

        // 2. Key range check
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

        // 3. Key filter predicate (if exists)
        if (isidp->key_pred.pr_eval_fnc != nullptr && isidp->key_pred.pred_expr != nullptr)
          {
            FILTER_INFO key_filter;
            memset (&key_filter, 0, sizeof (key_filter));
            key_filter.scan_pred = &isidp->key_pred;
            key_filter.scan_attrs = &isidp->key_attrs;
            key_filter.val_list = m_scan_id->val_list;
            key_filter.val_descr = m_vd;
            key_filter.class_oid = &isidp->cls_oid;

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
          }
        else
          {
            m_scan_id->scan_stats.key_qualified_rows++;
          }

        // 4. Collect ALL OIDs from this leaf record (including overflow pages)
        m_slot_oids.clear ();
        m_slot_oid_idx = 0;

        int proc_err = btree_key_process_objects (thread_p, m_btid_int, &rec, offset,
                       &leaf_rec_info, collect_oid_callback, &m_slot_oids);
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

        // Save the key for use in process_oid (covering index needs it)
        m_slot_key = key;
        m_slot_key_valid = true;
        m_slot_clear_key = clear_key;

        // 5. Process OIDs one at a time
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
            // S_END means skip, continue to next OID
          }

        // All OIDs in this slot were skipped; clean up key and continue to next slot
        if (m_slot_key_valid && m_slot_clear_key)
          {
            pr_clear_value (&m_slot_key);
          }
        m_slot_key_valid = false;
        m_slot_clear_key = false;
      }

    // Page exhausted
    if (m_page != nullptr)
      {
        pgbuf_unfix (thread_p, m_page);
        m_page = nullptr;
      }
    return S_END;
  }
}
